# STM32F4 Kernel 実装詳細

このドキュメントはSTM32F4固有の実装詳細を記述します。起動シーケンス、カーネル構成、ISR、ペリフェラルDMA処理フロー、ファイル構成、ビルドサイズを含みます。

---

## 1. 起動シーケンス

### 1.1 Reset_Handler → main()

```
Reset_Handler()
  ├─ FPU有効化 (SCB::enable_fpu)
  ├─ .data セクションコピー (Flash → RAM)
  ├─ .bss ゼロクリア
  ├─ VTOR書き換え (SRAM ベクタテーブル)
  │   └─ umi::irq::init()
  │       ├─ SRAMにベクタテーブル確保 (alignas(512))
  │       ├─ 全エントリを default_handler で初期化
  │       ├─ [0]=_estack, [1]=Reset_Handler をセット (内部参照)
  │       ├─ SCB::set_vtor(SRAM table address)
  │       └─ DSB + ISB バリア
  ├─ IRQハンドラ登録 (SRAMテーブルに動的登録)
  │   ├─ Fault系 → LED赤点灯+ハング
  │   ├─ SVCall, PendSV, SysTick → arch.cc ハンドラ
  │   ├─ DMA1_Stream3 → PDM IRQ
  │   ├─ DMA1_Stream5 → I2S Audio IRQ
  │   └─ OTG_FS → USB IRQ
  ├─ 例外優先度設定
  │   ├─ SysTick  = 0xF0 (DMAより低い)
  │   └─ PendSV   = 0xFF (最低優先度)
  ├─ C++グローバルコンストラクタ呼び出し
  └─ main()
```

### 1.2 main() 初期化シーケンス

```
main()
  ├─ Phase 1: クロック・GPIO
  │   ├─ init_clocks()        168 MHz 設定
  │   └─ init_gpio()          GPIO ピン設定
  │
  ├─ Phase 2: カーネル初期化
  │   ├─ init_shared_memory() SharedMemory 構造体初期化
  │   ├─ init_loader()        AppLoader 初期化 (RAM境界設定)
  │   └─ load_app()           .umia バイナリロード
  │       ├─ AppHeader検証 (magic, ABI)
  │       └─ エントリポイント取得 (Thumbビット付き)
  │
  ├─ Phase 3: MPU設定 (アプリロード成功時)
  │   ├─ Region 0: Kernel SRAM (RW, non-exec)
  │   ├─ Region 1: App Flash (RO, exec)
  │   ├─ Region 2: App RAM (RW, non-exec)
  │   ├─ Region 3: App Stack (RW, non-exec)
  │   ├─ Region 4: Shared Memory (RW, non-exec)
  │   ├─ Region 5: Peripherals (RW, non-exec)
  │   ├─ Region 6: CCM (RW, non-exec)
  │   └─ Region 7: Kernel Flash (RO, exec)
  │
  ├─ Phase 4: USB コールバック設定 ※init_usb()より前に必須
  │   ├─ set_midi_callback()         → SpscQueue にキュー
  │   ├─ set_sysex_callback()        → g_sysex_ready + resume_task
  │   ├─ set_sample_rate_callback()  → サンプルレート変更要求
  │   ├─ on_streaming_change()       → Blue LED 制御
  │   ├─ on_audio_in_change()        → Orange LED 制御
  │   └─ on_audio_rx()               → Green LED トグル
  │
  ├─ Phase 5: ペリフェラル初期化
  │   ├─ init_usb()           USB スタック初期化, NVIC有効
  │   ├─ init_systick()       SysTick 1kHz (1ms周期)
  │   ├─ init_cycle_counter() DWT サイクルカウンタ
  │   ├─ init_audio()         I2S3 オーディオコーデック
  │   └─ init_pdm_mic()       I2S2 PDM マイク
  │
  └─ Phase 6: RTOS開始
      └─ start_rtos(app_entry)
          ├─ create_task() × 4 (audio, system, control, idle)
          ├─ init_task() × 4 (ハードウェアTCB初期化)
          ├─ suspend_task(audio)   ← Blocked開始 (DMA notify待ち)
          ├─ suspend_task(system)  ← Blocked開始 (SysTick resume待ち)
          ├─ prepare_switch(control) ← 最初のRunningタスク
          └─ start_scheduler()    → 以後リターンしない
```

---

## 2. カーネル構成

### 2.1 umi::Kernel

`umi::Kernel<8, 4, HW, 1>` — O(1)ビットマップスケジューラ。

- 最大8タスク、4優先度レベル、1コア
- `Stm32F4Hw` 構造体がハードウェア抽象化を提供
- PRIMASK ベースのネスト可能クリティカルセクション
- `KernelEvent` ビットマスクによる通知/待機 (`notify` / `wait_block`)

### 2.2 IPC

SpscQueue (ロックフリー SPSC リングバッファ):
- `g_audio_ready_queue<uint16_t*, 4>` — ISR→Audio Task (DMAバッファポインタ)
- `g_midi_queue<MidiMsg, 64>` — USB ISR→Audio Task (MIDI)
- `g_button_queue<ButtonEvent, 16>` — ボタンイベント (定義済み、投入コード未実装)

### 2.3 タスク一覧

| タスク | エントリ関数 | スタック | 優先度 | 用途 | FPU |
|--------|-------------|---------|--------|------|-----|
| Audio | `audio_task_entry()` | 1024×4B (CCM) | 0 Realtime | オーディオ処理 | Yes |
| System | `system_task_entry()` | 512×4B (CCM) | 1 Server | PDM/SysEx/サンプルレート変更 | No |
| Control | `control_task_entry()` | 2048×4B (CCM) | 2 User | アプリ main() 実行 | Yes |
| Idle | `idle_task_entry()` | 64×4B (CCM) | 3 Idle | WFI (低電力) | No |

### 2.4 タスク状態遷移

```
         ┌──────────┐
         │ Blocked  │ ←── wait_block() / suspend_task()
         └────┬─────┘
              │ notify() / resume_task()
              ▼
         ┌──────────┐
         │  Ready   │ ←── ビットマップにセット、スケジューラ選択待ち
         └────┬─────┘
              │ PendSV コンテキストスイッチ
              ▼
         ┌──────────┐
         │ Running  │ ←── 実行中
         └──────────┘
```

### 2.5 スケジューラ

O(1)ビットマップ方式。各優先度レベルのReadyビットをCLZで走査:

```
priority_bitmap → CLZ → 最高優先度のReadyタスクを選択
```

### 2.6 各タスクの動作

**Audio Task** (優先度0 Realtime):
```
audio_task_entry:
  loop:
    wait_block(AudioReady)         ← notify() で起床
    while (audio_ready_queue.try_pop()):
      process_audio_frame(buf)     ← USB読み/I2S書き/アプリ処理
```

> `wait_block()` はクリティカルセクション内で atomically に take+block を行う。
> フラグ消費後にフラグが残っていなければ Blocked 遷移するため、
> DMA通知の合間に下位優先度タスクが正常にスケジュールされる。

**System Task** (優先度1 Server):
```
system_task_entry:
  シェル初期化 (ShellCommands, LineBuffer, stdin callback)
  loop:
    handle_sample_rate_change()    ← 直接呼び出し
    PDM処理 (g_pdm_ready時)        ← 直接呼び出し
    SysEx処理 (g_sysex_ready時)    ← 直接呼び出し
    suspend_task(self)             ← SysTick の resume_task() で起床
```

> audio_task が wait_block() で正しく Blocked に遷移するため、
> system_task は SysTick (1ms) の resume_task() で起床し、
> PDM/SysEx/サンプルレート変更を処理する。

**Control Task** (優先度2 User):
```
control_task_entry:
  app_entry()  ← アプリの main() を呼ぶ
  (main戻り後) suspend_task(self) で停止
```

**Idle Task** (優先度3 Idle):
```
idle_task_entry:
  loop:
    WFI (Wait For Interrupt)
```

---

## 3. ISR構成

### 3.1 例外ハンドラ

| 例外 | ハンドラ | 実装 | 優先度 | 用途 |
|------|---------|------|--------|------|
| SysTick | `SysTick_Handler` | arch.cc | 0xF0 | 1ms tick, タスク起床 |
| SVCall | `SVC_Handler` | arch.cc | デフォルト | アプリsyscall処理 |
| PendSV | `PendSV_Handler` | arch.cc | 0xFF (最低) | コンテキストスイッチ |
| HardFault等 | fault handler | main.cc | — | LED赤+ハング |

### 3.2 ペリフェラル割り込み

| IRQ | ハンドラ | 優先度 | ソース | 処理 |
|-----|---------|--------|--------|------|
| DMA1_Stream5 | `DMA1_Stream5_IRQHandler` | 5 | I2S Audio DMA TC | SpscQueue push → notify(AudioReady) |
| DMA1_Stream3 | `DMA1_Stream3_IRQHandler` | 5 | PDM DMA TC | g_pdm_ready フラグセットのみ |
| OTG_FS | `OTG_FS_IRQHandler` | 6 | USB | `usb_device.poll()` |

### 3.3 SysTick コールバック (1ms周期)

```
tick_callback():
  g_tick_us += 1000
  g_kernel.tick(1000)
  System Task  → resume_task()
  Control Task → タイムアウト判定 → resume_task()
```

### 3.4 SVCハンドラ (Syscall)

```
SVC_Handler (arch.cc, naked):
  PSP/MSP判定 → スタックポインタ取得
  → svc_handler_c() → svc_handler_impl()

Syscall一覧:
  0: Exit          アプリ終了
  1: RegisterProc  オーディオプロセッサ登録
  2: WaitEvent     イベント待ちブロック (suspend_task + タイムアウト設定)
  5: Yield         コンテキストスイッチ要求
 10: GetTime       g_tick_us 取得
 40: GetShared     SharedMemory ポインタ取得
 50: MidiSend      USB MIDI送信
 51: MidiRecv      MIDIキューからデキュー (SpscQueue)
```

### 3.5 PendSV コンテキストスイッチ

```
PendSV_Handler (arch.cc, naked, アセンブリ):
  1. 現タスクのコンテキスト保存
     ├─ R4-R11, LR → 現TCBスタック
     └─ LR[4]==0 ? → S16-S31 FPUレジスタも保存
  2. BASEPRI = 0x60 (割り込みマスク)
  3. switch_context_callback()
     └─ enter_critical() (PRIMASK)
        ├─ g_kernel.get_next_task() → prepare_switch()
        ├─ task_id_to_hw_tcb() でTCB切替
        └─ exit_critical()
     ※ DMA ISRがPendSV中にnotify()でkernel状態を変更する
       レースを防止するためクリティカルセクションで保護
  4. BASEPRI = 0 (マスク解除)
  5. 新タスクのコンテキスト復帰
     ├─ R4-R11, LR ← 新TCBスタック
     └─ LR[4]==0 ? → S16-S31 復帰
  6. BX LR (新タスクへ復帰)
```

---

## 4. ペリフェラルDMA/処理フロー

### 4.1 オーディオ出力 (I2S3 + DMA1_Stream5)

**ハードウェア構成**:
- I2S3: 24-bit, Master TX (CS43L22 DAC)
- DMA: ダブルバッファ (ping-pong), 各256 uint16_t (64フレーム×4ワード)
- NVIC優先度: 5

**フロー**:
```
DMA (ping-pong連続動作)
  audio_buf0[256] ←→ audio_buf1[256]
    │
    │ Transfer Complete (約1.33ms @48kHz)
    ▼
DMA1_Stream5_IRQHandler [ISR]
    │ 完了バッファ特定 (DMAが今バッファ1なら、完了はバッファ0)
    ▼
on_audio_buffer_ready(buf) [ISR]
    │ g_audio_ready_queue.try_push(buf)  ← SpscQueue (4スロット)
    │ g_kernel.notify(AudioReady)         ← wait_block() を起床
    ▼
audio_task_entry [Task, 優先度0]
    │ wait_block(AudioReady) から復帰
    │ SpscQueue からバッファをdrain
    ▼
process_audio_frame(buf)
    │
    ├─ 1. USB Audio OUT読み取り
    │     usb_audio.read_audio() → i2s_work_buf[128]
    │
    ├─ 2. I2S出力パッキング
    │     pack_i2s_24(buf, i2s_work_buf) ← DMAバッファに直接書き込み
    │
    ├─ 3. アプリプロセッサ呼び出し
    │     ├─ MIDIイベント収集 (g_midi_queue SpscQueue)
    │     ├─ ボタンイベント収集 (g_button_queue SpscQueue)
    │     ├─ AudioContext構築
    │     └─ g_loader.call_process(ctx) → アプリの process()
    │
    ├─ 4. ソフトクリッピング
    │     synth_out_mono[] → [-1.0, 1.0] クランプ → 16bit変換
    │
    └─ 5. USB Audio IN書き込み (is_audio_in_streaming時)
          ├─ 96kHz以上: ゼロ埋め (PDMマイク非対応)
          └─ それ以外:  L=マイク(PCM), R=シンセ → usb_audio.write_audio_in()

    ▼ (drain完了後)
    wait_block(AudioReady) → Blocked遷移 → 下位タスクにCPU譲渡
```

### 4.2 PDMマイク (I2S2 + DMA1_Stream3)

**ハードウェア構成**:
- I2S2: PDMモード, RX
- DMA: ダブルバッファ, 各256 uint16_t
- CICデシメータ: PDM→PCM変換

**フロー**:
```
DMA (ping-pong連続動作)
  pdm_buf0[256] ←→ pdm_buf1[256]
    │
    │ Transfer Complete
    ▼
DMA1_Stream3_IRQHandler [ISR]
    │ g_active_pdm_buf = 完了バッファ
    │ g_pdm_ready = true
    │ (タスク起床なし、フラグのみ)
    ▼
system_task_entry [Task, 優先度1]
    │ g_pdm_ready をチェック (< 96kHz時のみ処理)
    ▼
cic_decimator.process_buffer()
    │ PDM 256サンプル → PCM 64サンプル
    ▼
pcm_buf[64]
    │ (次のaudio処理でUSB Audio INに送信)
```

### 4.3 USB Audio

**Audio OUT (PC → デバイス → コーデック)**:
```
USB ISR (OTG_FS_IRQHandler)
  → usb_device.poll()
  → USBバッファにデータ蓄積
      ↓
process_audio_frame()
  → usb_audio.read_audio(i2s_work_buf, 64)
  → pack_i2s_24() → DMAバッファ(I2S出力)
```

**Audio IN (マイク/シンセ → PC)**:
```
process_audio_frame() [is_audio_in_streaming時のみ]
  ├─ 96kHz以上: stereo_buf をゼロ埋め (PDMマイク非対応)
  └─ それ以外:  L=pcm_buf(マイク), R=last_synth_out(シンセ)
  → usb_audio.write_audio_in(stereo_buf, 64)
      ↓
USB ISR → ホストPC へ送信
```

### 4.4 USB MIDI

**MIDI IN (PC → アプリ)**:
```
USB ISR → set_midi_callback()
  → g_midi_queue.try_push()  ← SpscQueue
      ↓
process_audio_frame() [Audio Task]
  → g_midi_queue.try_pop() でdrain
  → Event::make_midi() に変換
  → AudioContext.input_events に追加
  → アプリの process() に渡す
```

**MIDI OUT (アプリ → PC)**:
```
アプリ syscall MidiSend (SVC #0)
  → svc_handler_impl()
  → usb_audio.send_midi()
```

### 4.5 USB SysEx (Shell)

```
USB ISR → set_sysex_callback()
  → g_sysex_buf にコピー, g_sysex_ready = true
  → g_kernel.resume_task(system_task) ← 起床
      ↓
system_task_entry [Task, 優先度1]
  → g_stdio.process_message(g_sysex_buf, g_sysex_len)
  → シェルコマンド処理
  → g_stdio.write_stdout() → usb_audio.send_sysex()
```

### 4.6 サンプルレート変更

```
USB ISR → set_sample_rate_callback(new_rate)
  → g_new_sample_rate = new_rate
  → g_sample_rate_change_pending = true
      ↓
system_task_entry [Task, 優先度1]
  → handle_sample_rate_change()
      ├─ usb_audio.block_audio_out_rx()
      ├─ I2S IRQ無効化
      ├─ DMA + I2S 停止
      ├─ configure_plli2s(new_rate) ← PLL再設定
      ├─ DMA再初期化
      ├─ audio_ready_queue drain
      ├─ I2S IRQ有効化
      ├─ DMA + I2S 再開
      ├─ g_current_sample_rate 更新
      └─ usb_audio.set_actual_rate() / reset_audio_out()
```

---

## 5. タイミング定数

| パラメータ | 値 | 備考 |
|-----------|-----|------|
| CPUクロック | 168 MHz | STM32F4 最大 |
| SysTick | 1 kHz (1ms) | tick更新, タスク起床 |
| オーディオバッファ | 64サンプル @48kHz | ≈1.33ms レイテンシ |
| Audio DMA割り込み | 64サンプル毎 | ≈1.33ms 周期 |
| PDMバッファ | 256サンプル @2MHz | 64 PCMサンプルにデシメーション |
| USBフレーム | 1ms | SysTick同期 |
| サポートサンプルレート | 44100, 48000, 96000 Hz | PLLI2S再設定で切替 |

---

## 6. ファイル構成

### 6.1 カーネルソース

| ファイル | 役割 |
|---------|------|
| `main.cc` | ブートシーケンス、Reset_Handler、Faultハンドラ、初期化順序、例外優先度設定 |
| `kernel.cc` | Kernel実装 (Stm32F4Hw, タスクエントリ, オーディオ処理, syscall) |
| `kernel.hh` | カーネル公開API (init, コールバック) |
| `arch.cc` | 例外ハンドラ (SVC, PendSV, SysTick)、タスクコンテキスト初期化 |
| `arch.hh` | アーキテクチャ抽象化API |
| `mcu.cc` | MCUペリフェラル初期化、DMA/IRQハンドラ、バッファアクセサ |
| `mcu.hh` | MCU抽象化API |
| `bsp.hh` | ボード定数 (ピン、メモリ、IRQ番号、オーディオ設定) |

### 6.2 カーネルライブラリ

| ファイル | 説明 |
|----------|------|
| `examples/stm32f4_kernel/src/main.cc` | カーネルメイン |
| `examples/stm32f4_kernel/kernel.ld` | リンカスクリプト |
| `lib/umios/kernel/umi_kernel.hh` | カーネルコア（O(1)スケジューラ、FPUポリシー含む） |
| `lib/umios/kernel/shared_memory.hh` | 共有メモリ |
| `lib/umios/kernel/shell_commands.hh` | シェルコマンド |
| `lib/umios/kernel/loader.hh` | アプリローダー |
| `lib/umios/kernel/protection.hh` | MPU抽象化レイヤー |
| `lib/umios/kernel/metrics.hh` | パフォーマンス計測（DWT） |
| `lib/umios/kernel/port/cm4/switch.hh` | Cortex-M4 コンテキストスイッチ |
| `lib/umios/kernel/port/cm4/context.hh` | Cortex-M4 コンテキスト管理 |

### 6.3 アプリケーション

| ファイル | 説明 |
|----------|------|
| `lib/umios/app/syscall.hh` | Syscall API |
| `lib/umios/app/crt0.cc` | スタートアップ |
| `lib/umios/app/app_header.hh` | ヘッダーフォーマット |

### 6.4 Web

| ファイル | 説明 |
|----------|------|
| `examples/headless_webhost/web/lib/umi_web/core/protocol.js` | SysExプロトコル |
| `examples/headless_webhost/web/lib/umi_web/core/backends/hardware.js` | WebMIDIバックエンド |
| `examples/headless_webhost/web/lib/umi_web/components/shell/index.js` | シェルUI |

### 6.5 プラットフォームドライバ

| ファイル | 説明 |
|----------|------|
| `lib/umios/backend/cm/stm32f4/gpio.hh` | GPIO |
| `lib/umios/backend/cm/stm32f4/i2s.hh` | I2S Audio |
| `lib/umios/backend/cm/stm32f4/i2c.hh` | I2C |
| `lib/umios/backend/cm/stm32f4/cs43l22.hh` | Audio Codec |
| `lib/umios/backend/cm/stm32f4/pdm_mic.hh` | PDM Microphone |
| `lib/umios/backend/cm/stm32f4/rcc.hh` | クロック制御 |
| `lib/umios/backend/cm/stm32f4/power.hh` | 電力管理・Tickless |
| `lib/umiusb/include/audio_interface.hh` | USB Audio |

---

## 7. ビルドサイズ

### 7.1 stm32f4_kernel

```
Flash: 41,776 / 1,048,576 bytes (4.0%)
RAM:   77,820 / 131,072 bytes (59.4%)
```

| セクション | サイズ | 説明 |
|-----------|--------|------|
| .text | 41,692 B | コード（Flash） |
| .data | 84 B | 初期化済みデータ |
| .bss | 77,736 B | 未初期化データ（RAM） |

### 7.2 主要RAM消費

| シンボル | サイズ | 説明 |
|---------|--------|------|
| usb_audio | 28,320 B | USB Audio Class バッファ |
| g_shared | 6,824 B | 共有メモリ領域 |
| g_app_event_queue | 6,152 B | アプリイベントキュー |
| usb_hal | 3,032 B | USB HAL状態 |
| タスクスタック | 14,336 B | CCM RAM（4K+2K+8K） |

カーネル自体はヘッダーオンリー（テンプレート）でインライン展開されるため、
独立したサイズは計測困難。主要関数：
- PendSV_Handler: ~96 bytes
- umi_cm4_switch_context: ~192 bytes

---

*関連ドキュメント:*
- [OVERVIEW.md](OVERVIEW.md) — カーネル概要
- [ARCHITECTURE.md](ARCHITECTURE.md) — アーキテクチャ詳細
