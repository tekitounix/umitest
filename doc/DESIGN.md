# UMI-OS 設計書

**電子楽器のためのミニマルRTOS**

---

## 概要

UMI-OSは電子楽器（シンセサイザー、エフェクター、MIDI機器など）に特化したリアルタイムオペレーティングシステムである。

### 設計原則

| 原則 | 説明 |
|------|------|
| **初期化時確定** | 実行時の動的メモリ確保は避ける。初期化時は許容（C++の制約上コンパイル時に決定できないものがあるため） |
| **ヘッダオンリー** | `#include` のみで使用可能。リンク依存なし |
| **C++23** | 最新の言語機能を積極活用（concepts, constexpr, coroutines） |
| **ゼロオーバーヘッド抽象化** | テンプレートとconstexprで実行時コストなし |
| **マクロ排除** | プリプロセッサマクロは極力使用しない。constexpr/template/conceptsで代替 |

### ターゲット

- **MCU**: ARM Cortex-M4F (STM32F4xx など)
- **用途**: シンセサイザー、エフェクター、MIDI機器、オーディオインターフェース

---

## アーキテクチャ

```
┌─────────────────────────────────────────────────────┐
│                  Application                         │
│  (Synthesizer, Effects, MIDI Controllers, etc.)     │
├─────────────────────────────────────────────────────┤
│                 System Servers                       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐            │
│  │  Audio   │ │   MIDI   │ │  Shell   │            │
│  │  Engine  │ │  Router  │ │ (Debug)  │            │
│  └──────────┘ └──────────┘ └──────────┘            │
├─────────────────────────────────────────────────────┤
│                   Core Kernel                        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐            │
│  │ Scheduler│ │  Timer   │ │  IPC     │            │
│  │ 4-Level  │ │  Queue   │ │ SpscQueue│            │
│  │ Priority │ │(Tickless)│ │ Notify   │            │
│  └──────────┘ └──────────┘ └──────────┘            │
├─────────────────────────────────────────────────────┤
│               Hardware Abstraction                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐            │
│  │  Cortex  │ │   DMA    │ │   GPIO   │            │
│  │   M4F    │ │  I2S/SAI │ │ UART/SPI │            │
│  └──────────┘ └──────────┘ └──────────┘            │
└─────────────────────────────────────────────────────┘
```

---

## 1. カーネル (`core/umi_kernel.hh`)

### 1.1 優先度スケジューラ

4段階の固定優先度 + 同一優先度内でのラウンドロビン。

| 優先度 | 用途 | 特徴 |
|--------|------|------|
| **Realtime** | オーディオISR, DMA完了 | 最高優先度、即座にプリエンプション |
| **Server** | MIDI処理, USB, I2C | イベント駆動 |
| **User** | UI更新, LED, エンコーダ | ラウンドロビンで公平化 |
| **Idle** | スリープ, 低優先度処理 | 他にタスクがないとき実行 |

```cpp
enum class Priority : std::uint8_t { Realtime = 0, Server = 1, User = 2, Idle = 3 };
```

### 1.2 タスク管理

```cpp
struct TaskConfig {
    void (*entry)(void*) {nullptr};
    void* arg {nullptr};
    Priority prio {Priority::Idle};
    std::uint8_t core_affinity {static_cast<std::uint8_t>(Core::Any)};
    bool uses_fpu {false};
    const char* name {"<unnamed>"};
};

// 作成と通知
TaskId id = kernel.create_task(config);
kernel.notify(id, Event::AudioReady);

// タスク情報取得
const char* name = kernel.get_task_name(id);
const char* state = kernel.get_task_state_str(id);
```

### 1.3 イベントシステム

```cpp
enum class Event : std::uint32_t {
    None = 0,
    AudioReady = 1 << 0,
    MidiReady = 1 << 1,
    UsbReady = 1 << 2,
    // ... 最大32イベント
};

// 待機とタイムアウト
if (kernel.wait(Event::AudioReady, 1000_usec)) {
    // イベント受信
}
```

### 1.4 タイマーキュー（Tickless）

```cpp
// ワンショットタイマー
kernel.call_later(deadline_usec, [] { handle_envelope(); });

// 周期タイマー
kernel.call_every(interval_usec, [] { update_lfo(); });
```

用途: エンベロープ、LFO、アルペジエータ、デバウンス

### 1.5 ウォッチドッグとパニック

```cpp
// ウォッチドッグ
kernel.register_watchdog(audio_task_id, 10000_usec);  // 10msタイムアウト
kernel.kick_watchdog(audio_task_id);

// パニック（復帰不可能なエラー）
kernel.panic("Buffer underrun", current_file, current_line);
```

---

## 2. オーディオサブシステム (`core/umi_audio.hh`)

### 2.1 オーディオエンジン

DMAダブルバッファリングによる低レイテンシオーディオ処理。

```cpp
template<typename HW, std::size_t BufferSize = 256>
class AudioEngine {
    static_assert(BufferSize % 2 == 0);  // ダブルバッファ
    
    alignas(4) std::array<Sample, BufferSize> buffer_a_;
    alignas(4) std::array<Sample, BufferSize> buffer_b_;
    
    // DMA転送完了コールバック
    void (*process_callback_)(Sample* buffer, std::size_t frames);
};
```

### 2.2 レイテンシ設計

| バッファサイズ | サンプルレート | レイテンシ |
|----------------|----------------|------------|
| 32 samples | 48kHz | 0.67ms |
| 64 samples | 48kHz | 1.33ms |
| 128 samples | 48kHz | 2.67ms |
| 256 samples | 48kHz | 5.33ms |

### 2.3 負荷モニタリング

```cpp
auto load = audio_engine.get_load_percent();  // 0-100%
if (load > 80) {
    reduce_polyphony();  // 負荷軽減
}
```

---

## 3. MIDIサブシステム (`core/umi_midi.hh`)

### 3.1 MIDIパーサー

ストリーミングパーサーでランニングステータス対応。

```cpp
namespace midi {
    struct Event {
        Type type;
        std::uint8_t channel;
        std::uint8_t data1;
        std::uint8_t data2;
    };
    
    class Parser {
        std::optional<Event> parse(std::uint8_t byte);
    };
}
```

### 3.2 対応メッセージ

| カテゴリ | メッセージ |
|----------|-----------|
| **チャンネル** | NoteOn, NoteOff, CC, ProgramChange, PitchBend, Aftertouch |
| **システム** | Clock, Start, Stop, Continue |
| **SysEx** | 可変長対応 |

---

## 4. コルーチン (`core/umi_coro.hh`)

C++20コルーチンによる非同期処理。

### 4.1 基本使用

```cpp
umi::coro::Task<void> blink_led() {
    while (true) {
        led.toggle();
        co_await ctx.sleep(500ms);  // 非ブロッキング待機
    }
}

umi::coro::Task<int> async_read_sensor() {
    start_adc_conversion();
    co_await ctx.sleep(100us);
    co_return read_adc_result();
}
```

### 4.2 スケジューラ

```cpp
umi::coro::Scheduler<8> scheduler;  // 最大8コルーチン

scheduler.spawn(blink_led());
scheduler.spawn(handle_encoder());

// メインループ
while (true) {
    scheduler.run();
}
```

---

## 5. IPC機構

### 5.1 SpscQueue（ロックフリー）

```cpp
SpscQueue<midi::Event, 64> midi_queue;   // MIDI ISR → Audio Task
SpscQueue<ParamChange, 32> param_queue;  // UI → Audio Task

// Producer (ISR)
midi_queue.try_push(event);

// Consumer (Task)
if (auto ev = midi_queue.try_pop()) {
    process(*ev);
}
```

### 5.2 Notification

```cpp
Notification<MaxTasks> notify;

// 送信側
notify.send(task_id, Event::MidiReady);

// 受信側
Event events = notify.wait(Event::MidiReady | Event::UsbReady, timeout);
```

### 5.3 電子楽器におけるIPCパターン

```
[MIDI ISR] ──SpscQueue──> [Audio Task] ──DMA──> [DAC]
                               ↑
[Encoder ISR] ──SpscQueue─────┘
                               ↑
                          Notification (buffer complete)
```

**設計判断**: MPSC/Mutexは不要。電子楽器のデータフローはSPSCで完結する。

---

## 6. モニタリング (`core/umi_monitor.hh`)

### 6.1 リソースモニター

```cpp
// スタック使用量
std::size_t used = StackMonitor::get_used(task_id);
std::size_t max = StackMonitor::get_max(task_id);

// ヒープ使用量（静的プール）
auto [used, total] = HeapMonitor::get_usage();

// タスクCPU使用率
float cpu = TaskProfiler::get_cpu_percent(task_id);

// 割り込みレイテンシ
usec worst = IrqLatencyMonitor::get_worst_case();
```

### 6.2 負荷モニター

```cpp
LoadMonitor<HW> monitor(48000, 256);  // 48kHz, 256 samples

// オーディオコールバック内
monitor.begin_processing();
// ... DSP処理 ...
monitor.end_processing();

float load = monitor.get_load_percent();
```

---

## 7. デバッグシェル (`core/umi_shell.hh`)

### 7.1 コマンド

| コマンド | 説明 |
|----------|------|
| `ps` | タスク一覧（名前、状態、優先度、スタック使用量） |
| `mem` | メモリ使用状況 |
| `load` | CPU負荷 |
| `reboot` | システム再起動 |
| `help` | ヘルプ表示 |

### 7.2 出力先

```cpp
// UART出力
umi::Shell<HW, Kernel> shell(kernel, uart_write);

// MIDI SysEx出力（製品版でもデバッグ可能）
umi::Shell<HW, Kernel> shell(kernel, sysex_write);

void sysex_write(const char* str) {
    // 0xF0 <manufacturer> <data...> 0xF7
    midi_send_sysex(str, strlen(str));
}
```

---

## 8. エラーハンドリング (`core/umi_expected.hh`)

### 8.1 Result型

C++23 `std::expected` 互換の軽量実装。

```cpp
enum class Error {
    OutOfMemory, OutOfTasks, OutOfTimers,
    InvalidTask, InvalidState, InvalidParam,
    Timeout, WouldBlock,
    BufferOverrun, BufferUnderrun,
    MidiParseError, MidiBufferFull,
};

template<typename T>
using Result = umi::expected<T, Error>;

// 使用例
Result<TaskId> result = kernel.try_create_task(config);
if (result) {
    TaskId id = *result;
} else {
    Error err = result.error();
}
```

### 8.2 クリティカルエラー

電子楽器では「音が止まらない」ことが最優先。

```cpp
[[noreturn]] void HardFault_Handler() {
    HW::mute_audio_dma();   // 即座にミュート
    HW::save_crash_dump();  // 診断情報保存
    while (true) {}
}
```

---

## 9. ハードウェア抽象化

### 9.1 HWトレイト

```cpp
struct HW {
    // 必須
    static void enable_interrupts();
    static void disable_interrupts();
    static usec monotonic_time_usecs();
    
    // オプション
    static void mute_audio_dma();
    static void reboot();
};
```

### 9.2 ポート構成

```
port/
├── arm/cortex-m/
│   ├── cortex_m4.hh      # Cortex-M4固有
│   └── common/
│       ├── dwt.hh        # サイクルカウンタ
│       ├── nvic.hh       # 割り込みコントローラ
│       ├── scb.hh        # システム制御
│       ├── systick.hh    # SysTick タイマー
│       └── vector_table.hh
├── board/
│   ├── stm32f4/          # STM32F4xx ボード
│   │   ├── hw_impl.hh
│   │   ├── startup.hh
│   │   ├── linker.ld
│   │   └── syscalls.cc
│   └── stub/             # ホストテスト用スタブ
│       └── hw_impl.hh
└── vendor/stm32/         # STM32 ペリフェラル
    └── stm32f4/
        ├── gpio.hh
        ├── rcc.hh
        └── uart.hh
```

---

## 10. ビルドシステム

### 10.1 xmake 構成

```lua
-- ホストテスト
target("test_kernel")
    set_kind("binary")
    add_files("test/test_kernel.cc")
    set_group("tests/host")

-- ファームウェア
target("firmware")
    set_kind("binary")
    add_rules("cortex-m4f")
    add_files("examples/example_app.cc")
    set_group("firmware")
```

### 10.2 ビルドコマンド

```bash
xmake                    # 全ターゲットビルド
xmake build firmware     # ファームウェアのみ
xmake test               # ホストテスト実行
xmake renode-test        # Renode エミュレーションテスト
xmake robot              # Robot Framework テスト
```

### 10.3 Renode 統合

```bash
# エミュレーション実行
xmake renode

# 自動テスト
xmake robot
```

---

## 11. 実装状況

### コアファイル

| ファイル | 行数 | 状態 | 説明 |
|----------|------|------|------|
| `umi_kernel.hh` | ~1000 | ✅ 完了 | カーネル本体 |
| `umi_audio.hh` | ~500 | ✅ 完了 | オーディオエンジン |
| `umi_coro.hh` | ~470 | ✅ 完了 | コルーチンサポート |
| `umi_midi.hh` | ~350 | ✅ 完了 | MIDIパーサー |
| `umi_monitor.hh` | ~370 | ✅ 完了 | リソースモニタリング |
| `umi_shell.hh` | ~220 | ✅ 完了 | デバッグシェル |
| `umi_expected.hh` | ~170 | ✅ 完了 | Result型 |
| `umi_startup.hh` | ~155 | ✅ 完了 | 起動処理 |
| `umi_app_types.hh` | ~100 | ✅ 完了 | アプリケーション型定義 |

### テスト

| テスト種別 | 状態 | 説明 |
|------------|------|------|
| ホストテスト | ✅ 33/33 pass | `xmake test` |
| Renode テスト | ✅ pass | `xmake renode-test` |
| Robot Framework | ✅ 4/4 pass | `xmake robot` |

---

## 12. 追加しない機能

電子楽器に不要、または設計方針に反するため採用しない機能:

| 機能 | 理由 |
|------|------|
| **Mutex/Semaphore** | CriticalSection + SpscQueue で十分 |
| **CondVar/Barrier** | Notification で代替可能 |
| **MPSCキュー** | 電子楽器はSPSCで完結 |
| **動的メモリ確保** | 設計原則に反する |
| **ファイルシステム** | フラッシュ直接アクセスで十分 |
| **ネットワークスタック** | 電子楽器には過剰 |

---

## 付録: 電子楽器のデータフローモデル

```
┌─────────────────────────────────────────────────────┐
│                    Hardware                          │
│  [MIDI IN] [USB] [ADC/Encoder] [I2S/SAI DMA]        │
└──────┬───────┬──────────┬─────────────┬─────────────┘
       │       │          │             │
       ▼       ▼          ▼             │
    ┌──────────────────────────┐        │
    │     SpscQueue (ISR)      │        │
    │  MIDI | USB | Encoder    │        │
    └──────────┬───────────────┘        │
               │                        │
               ▼                        │
    ┌──────────────────────────┐        │
    │      Audio Task          │◀───────┘
    │   (Priority::Realtime)   │   DMA Complete
    │                          │   Notification
    │  - MIDI → Note/CC        │
    │  - DSP Processing        │
    │  - Buffer Fill           │
    └──────────┬───────────────┘
               │
               ▼
    ┌──────────────────────────┐
    │        DAC (I2S)         │
    └──────────────────────────┘
```

このモデルでは:
- **ISR** がハードウェアイベントを検出し SpscQueue に投入
- **Audio Task** が全てのデータを消費し DSP 処理
- **DMA** がダブルバッファでオーディオ出力
- **Notification** で DMA 完了を Audio Task に通知

シンプルで予測可能な、電子楽器に最適化されたアーキテクチャ。
