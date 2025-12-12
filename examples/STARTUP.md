# UMI-Kernel 起動シーケンス

## 概要

UMI-Kernelはヘッダオンリーのカーネルで、プラットフォーム固有のコードと組み合わせて使用します。

```
+------------------+     +------------------+     +------------------+
|   Platform Layer |     |   UMI-Kernel     |     |   Application    |
|   (hw_xxx.cc)    |---->|   (umi_kernel.hh)|<----|   (app.cc)       |
+------------------+     +------------------+     +------------------+
        |                        |                        |
        v                        v                        v
   ハードウェア依存            ハードウェア非依存          ユーザーロジック
   (レジスタ操作)            (スケジューラ、IPC)         (オーディオ処理)
```

## 起動フロー

```
Reset
  │
  ▼
┌─────────────────────────────────────────┐
│ 1. プラットフォーム初期化 (hw_xxx.cc)    │
│    - クロック設定                        │
│    - 割り込みベクタ設定                  │
│    - DWT/SysTick有効化                  │
└─────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────┐
│ 2. C++ランタイム初期化                   │
│    - .bss クリア                         │
│    - .data コピー                        │
│    - グローバルコンストラクタ呼び出し    │
└─────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────┐
│ 3. main() 実行                          │
│    - app_init()                         │
│      - watchdog_init()                  │
│      - create_task() x N                │
│      - call_later() (タイマー登録)       │
│    - 最初のスケジューリング              │
└─────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────┐
│ 4. カーネル実行ループ                    │
│    - tick() / on_timer_irq()            │
│    - get_next_task()                    │
│    - prepare_switch()                   │
│    - (PendSV でコンテキストスイッチ)     │
└─────────────────────────────────────────┘
```

## コード例

### 1. カーネルインスタンス定義

```cpp
#include "umi_kernel.hh"

// プラットフォーム層で定義
struct PlatformHw;
using HW = umi::Hw<PlatformHw>;

// カーネルインスタンス（グローバル、静的配置）
umi::Kernel<8, 16, HW, 1> kernel;
```

### 2. タスク定義

```cpp
// タスクID（起動時に割り当て）
umi::TaskId audio_task;
umi::TaskId ui_task;
umi::TaskId idle_task;

// Realtimeタスク: オーディオ処理
void audio_entry(void* arg) {
    while (true) {
        // DMA割り込みで起床
        kernel.wait_block(audio_task, NOTIFY_BUFFER_READY);
        // 処理...
    }
}

// Userタスク: UI処理（round-robin）
void ui_entry(void* arg) {
    while (true) {
        kernel.wait_block(ui_task, NOTIFY_UI_EVENT);
        // 画面更新...
    }
}

// Idleタスク: スリープ管理
void idle_entry(void* arg) {
    while (true) {
        HW::watchdog_feed();
        HW::enter_sleep();
    }
}
```

### 3. アプリケーション初期化

```cpp
void app_init() {
    // Watchdog設定
    HW::watchdog_init(1000);  // 1秒
    
    // タスク作成
    audio_task = kernel.create_task({
        .entry = audio_entry,
        .prio = umi::Priority::Realtime,
        .uses_fpu = true,
        .name = "audio",
    });
    
    ui_task = kernel.create_task({
        .entry = ui_entry,
        .prio = umi::Priority::User,
        .name = "ui",
    });
    
    idle_task = kernel.create_task({
        .entry = idle_entry,
        .prio = umi::Priority::Idle,
        .name = "idle",
    });
    
    // 定期タイマー
    kernel.call_later(16'000, {ui_refresh, nullptr});
}
```

### 4. 割り込みハンドラ

```cpp
// SysTick: 1ms周期
extern "C" void SysTick_Handler() {
    kernel.tick(1000);  // 1000us = 1ms
}

// Audio DMA完了
extern "C" void DMA1_Stream5_IRQHandler() {
    audio_engine.on_buffer_complete(kernel, in_buf, out_buf, midi_queue);
}

// MIDI UART受信
extern "C" void USART2_IRQHandler() {
    uint8_t byte = USART2->DR;
    // MIDIパース、midi_queue.try_push()
}
```

### 5. メインエントリ

```cpp
int main() {
    app_init();
    
    // 最初のタスクへ切り替え
    // (実際はPendSVトリガー後、idle_entryから開始)
    idle_entry(nullptr);
}
```

## タスク状態遷移

```
                create_task()
                     │
                     ▼
              ┌──────────┐
              │  Ready   │◄────────────────────┐
              └────┬─────┘                     │
                   │ schedule()                │ resume_task()
                   │ (最高優先度なら)           │ notify() [blocked時]
                   ▼                           │
              ┌──────────┐                     │
              │ Running  │                     │
              └────┬─────┘                     │
                   │                           │
      ┌────────────┼────────────┐              │
      │            │            │              │
      ▼            ▼            ▼              │
┌──────────┐ ┌──────────┐ ┌──────────┐         │
│ Blocked  │ │  Ready   │ │  Unused  │         │
│(wait_blk)│ │(preempt) │ │(delete)  │         │
└────┬─────┘ └──────────┘ └──────────┘         │
     │                                         │
     └─────────────────────────────────────────┘
```

## プラットフォーム層の責務

| 機能 | Hwメソッド | 実装例 (Cortex-M) |
|------|-----------|-------------------|
| クリティカルセクション | `enter_critical()` | `BASEPRI = 優先度マスク` |
| コンテキストスイッチ | `request_context_switch()` | `SCB->ICSR = PENDSVSET` |
| 時刻取得 | `monotonic_time_usecs()` | `TIM2->CNT` (32bit timer) |
| スリープ | `enter_sleep()` | `__WFI()` |
| Watchdog | `watchdog_feed()` | `IWDG->KR = 0xAAAA` |

## 動作確認チェックリスト

- [ ] `kernel.create_task()` が有効なTaskIdを返す
- [ ] `kernel.tick()` が定期的に呼ばれる
- [ ] `kernel.notify()` でBlockedタスクが起床する
- [ ] Audio DMA割り込みで `on_buffer_complete()` が呼ばれる

## デバッグシェル

UARTからコマンドを受け付けるデバッグシェルを使用できます。

```cpp
#include "umi_shell.hh"

// UART出力関数
void uart_write(const char* str) {
    while (*str) {
        USART2->DR = *str++;
        while (!(USART2->SR & USART_SR_TXE)) {}
    }
}

// シェルインスタンス
umi::Shell<HW, decltype(kernel)> shell(kernel, uart_write);

// UART受信割り込みハンドラ
extern "C" void USART2_IRQHandler() {
    if (USART2->SR & USART_SR_RXNE) {
        char c = USART2->DR;
        shell.process_char(c);
    }
}

// main() で初期化後に呼び出し
shell.start();
```

### 利用可能なコマンド

| コマンド | 説明 |
|----------|------|
| `ps` | タスク一覧を表示 |
| `mem` | メモリ使用状況を表示 |
| `load` | CPU負荷/稼働時間を表示 |
| `reboot` | システムリセット |
| `help` | ヘルプを表示 |

### 出力例

```
umi> ps
ID  Name            Prio     State
--  --------------  -------  -------
 0  audio           RT       Blocked
 1  ui              User     Ready
 2  idle            Idle     Ready

umi> load
Uptime: 5m 23s
Active Tasks: 3
```
- [ ] Idle時に `enter_sleep()` が呼ばれる
- [ ] Watchdogが定期的にfeedされる
