# 15 — Boot Sequence

## 概要

Reset から RTOS 開始までの起動フロー。
ハードウェア初期化、カーネル初期化、アプリロード、タスク作成の全フェーズを規定する。

| 項目 | 状態 |
|------|------|
| Reset Handler | 実装済み |
| main() 初期化シーケンス | 実装済み |
| RTOS タスク作成・起動 | 実装済み |

---

## 起動フロー全体

```
Reset_Handler
  ├─ Phase 0: ハードウェア基盤
  │   ├─ FPU 有効化 (CPACR)
  │   ├─ .data コピー (Flash → RAM)
  │   ├─ .bss ゼロクリア
  │   ├─ VTOR 書き換え (SRAM ベクタテーブル)
  │   ├─ IRQ ハンドラ動的登録
  │   ├─ 例外優先度設定
  │   ├─ C++ グローバルコンストラクタ
  │   └─ main() 呼び出し
  │
main()
  ├─ Phase 1: クロック・GPIO 初期化
  ├─ Phase 2: カーネル初期化
  │   ├─ SharedMemory 初期化
  │   ├─ AppLoader 初期化
  │   └─ .umia ロード（検証含む）
  ├─ Phase 3: MPU 設定（アプリロード成功時）
  ├─ Phase 4: USB コールバック設定
  ├─ Phase 5: ペリフェラル初期化
  │   ├─ USB (OTG FS)
  │   ├─ SysTick (1ms)
  │   ├─ DWT サイクルカウンタ
  │   ├─ I2S (Audio DAC/ADC)
  │   └─ PDM マイク
  └─ Phase 6: RTOS 開始
      ├─ 4 タスク作成
      ├─ SystemTask を Blocked で開始
      └─ スケジューラ起動 → ControlTask が最初に実行
```

---

## Phase 0: Reset Handler

### ベクタテーブル

ブートベクタテーブルは Flash 先頭に最小限（SP + Reset）のみ配置する:

```cpp
const void* const g_boot_vectors[2] __attribute__((section(".isr_vector"))) = {
    reinterpret_cast<const void*>(&_estack),
    reinterpret_cast<const void*>(Reset_Handler),
};
```

### 動的 IRQ システム

SRAM 上にベクタテーブルを配置し、VTOR を書き換える。
ハンドラは `umi::irq::set_handler()` で動的に登録する:

```cpp
umi::irq::init();  // SRAM テーブル初期化 + VTOR 設定

// 例外ハンドラ登録
umi::irq::set_handler(IRQn::HardFault, HardFault_Handler);
umi::irq::set_handler(IRQn::MemManage, MemManage_Handler);
umi::irq::set_handler(IRQn::BusFault, BusFault_Handler);
umi::irq::set_handler(IRQn::UsageFault, UsageFault_Handler);
umi::irq::set_handler(IRQn::SVCall, SVC_Handler);
umi::irq::set_handler(IRQn::PendSV, PendSV_Handler);
umi::irq::set_handler(IRQn::SysTick, SysTick_Handler);
```

### 例外優先度設定

| 例外 / IRQ | 優先度 | 備考 |
|-----------|--------|------|
| DMA1_Stream5 (I2S RX) | 0x10 | 最高（オーディオ入力） |
| DMA1_Stream3 (PDM) | 0x20 | |
| OTG_FS (USB) | 0x40 | |
| SysTick | 0xF0 | DMA より低く、PendSV より高い |
| PendSV | 0xFF | 最低（コンテキストスイッチ） |

PendSV が最低優先度であることで、ISR 処理中にコンテキストスイッチが割り込まないことを保証する。

---

## Phase 1: クロック・GPIO

```cpp
umi::mcu::init_clocks();   // HSE → PLL → 168 MHz (STM32F407)
umi::mcu::init_gpio();     // LED、ボタン、ペリフェラルピン
```

起動直後に Blue LED を点灯し、初期化進行中であることを示す。

---

## Phase 2: カーネル初期化

### SharedMemory 初期化

OS/アプリ間の共有メモリ構造体を初期化する。詳細は [10-shared-memory.md](10-shared-memory.md) を参照。

```cpp
init_shared_memory();
// - オーディオバッファ（256 frames @ 48kHz, stereo）
// - イベントキュー（64 エントリ、atomic）
// - パラメータ状態、チャンネル状態、入力状態
// - ヒープ情報
```

### AppLoader 初期化

```cpp
init_loader(app_ram_start, app_ram_size);
// - アプリ RAM 領域のアドレスとサイズを設定
// - メモリレイアウトの検証
```

### アプリロード

```cpp
void* app_entry = load_app(app_image_start);
// - AppHeader 検証（magic, ABI, CRC32, 署名）
// - メモリレイアウト設定
// - エントリポイント取得（Thumb ビット付き）
// - 失敗時: app_entry = nullptr → Shell モードで起動
```

ロードの詳細は [16-app-loader.md](16-app-loader.md) を参照。

---

## Phase 3: MPU 設定

アプリロード成功時のみ MPU を設定する。8 リージョンを使用:

| リージョン | 対象 | アプリからの権限 |
|-----------|------|----------------|
| 0 | Kernel RAM | アクセス不可 |
| 1 | App .text (Flash) | 読取 + 実行 |
| 2 | App .data (RAM) | 読み書き |
| 3 | App Stack (RAM) | 読み書き |
| 4 | SharedMemory | 読み書き |
| 5 | ペリフェラル | 読み書き (Device) |
| 6 | CCM | 読み書き |
| 7 | Kernel Flash | 読取 + 実行 |

詳細は [12-memory-protection.md](12-memory-protection.md) を参照。

---

## Phase 4: USB コールバック設定

USB ドライバのコールバックを設定してから USB を初期化する（順序重要）:

```cpp
setup_usb_callbacks();
// - MIDI 受信コールバック
// - SysEx 受信コールバック（Shell / Updater）
// - オーディオサンプルレート変更通知
// - ストリーミング開始/停止通知
```

---

## Phase 5: ペリフェラル初期化

```cpp
umi::mcu::init_usb();              // USB OTG FS
umi::arch::cm4::init_systick(cpu_freq_hz, 1000);  // SysTick 1ms
umi::arch::cm4::init_cycle_counter();              // DWT
umi::mcu::init_audio();            // I2S (DAC/ADC)
umi::mcu::init_pdm_mic();          // PDM マイク (SPI + DMA)
```

初期化完了後、Blue LED を消灯する。

---

## Phase 6: RTOS 開始

### タスク作成

```cpp
// Kernel ソフトウェアスケジューラにタスクを登録
g_audio_task_id   = g_kernel.create_task({...prio=Realtime, uses_fpu=true...});
g_system_task_id  = g_kernel.create_task({...prio=Server,   uses_fpu=false...});
g_control_task_id = g_kernel.create_task({...prio=User,     uses_fpu=true...});
g_idle_task_id    = g_kernel.create_task({...prio=Idle,     uses_fpu=false...});

// ハードウェア TCB 初期化（FPU ポリシーはコンパイル時自動決定）
arch::cm4::init_task<audio_fpu_policy>(...);
arch::cm4::init_task<system_fpu_policy>(...);
arch::cm4::init_task<control_fpu_policy>(...);
arch::cm4::init_task<idle_fpu_policy>(...);
```

FPU ポリシーの自動決定については [11-scheduler.md](11-scheduler.md) を参照。

### タスク初期状態

| タスク | 初期状態 | 理由 |
|--------|---------|------|
| AudioTask | Ready | `audio_task_entry()` で即座に `wait_block(AudioReady)` → Blocked |
| SystemTask | Blocked (suspend) | SysTick の `resume_task()` で起床 |
| ControlTask | Running | 最初に実行されるタスク。アプリの `_start()` を呼ぶ |
| IdleTask | Ready | 他に Ready タスクがないとき実行 |

### スケジューラ起動

```cpp
g_kernel.prepare_switch(g_control_task_id.value);
arch::cm4::start_scheduler(&g_control_tcb, control_task_entry, app_entry, stack_top);
// → PSP 切り替え → ControlTask 開始 → 二度と戻らない
```

`start_scheduler` は MSP → PSP に切り替え、ControlTask のエントリを呼び出す。
ControlTask はアプリの `_start()` を非特権モードで実行する。

---

## エラー時の起動

### アプリロード失敗

`load_app()` が `nullptr` を返した場合:
- MPU 設定をスキップ
- ControlTask はアプリなしで起動
- SystemTask が Shell を有効化し、デバッグ可能な状態にする

### Fault LED パターン

起動中の例外発生時:

| 例外 | LED |
|------|-----|
| HardFault | Red 点灯（無限ループ） |
| MemManage | Red 点灯（無限ループ） |
| BusFault | Red 点灯（無限ループ） |
| UsageFault | Red 点灯（無限ループ） |

RTOS 起動後の Fault 処理については [20-diagnostics.md](20-diagnostics.md) を参照。

---

## ターゲット依存性

本ドキュメントは STM32F407VG を前提とする。ターゲット固有の詳細（クロックツリー、DMA チャンネル割り当て、ピン配置等）は `examples/stm32f4_kernel/` を参照。

---

## 関連ドキュメント

- [10-shared-memory.md](10-shared-memory.md) — SharedMemory 構造体
- [11-scheduler.md](11-scheduler.md) — タスクモデル、FPU ポリシー
- [12-memory-protection.md](12-memory-protection.md) — MPU リージョン設計
- [13-system-services.md](13-system-services.md) — サービスアーキテクチャ概要
- [16-app-loader.md](16-app-loader.md) — アプリロード詳細
