# STM32F4 Kernel フロードキュメント

## 1. 起動〜アプリロード

### Reset_Handler → main() (main.cc)

```
Reset_Handler()
  ├─ FPU有効化 (SCB::enable_fpu)
  ├─ .data セクションコピー (Flash → RAM)
  ├─ .bss ゼロクリア
  ├─ VTOR書き換え (SRAM ベクタテーブル)
  │   └─ umi::irq::init(initial_sp, Reset_Handler)
  │       ├─ SRAMにベクタテーブル確保 (alignas(512))
  │       ├─ 全エントリを default_handler で初期化
  │       ├─ [0]=初期SP, [1]=Reset_Handler をセット
  │       ├─ SCB::set_vtor(SRAM table address)
  │       └─ DSB + ISB バリア
  ├─ IRQハンドラ登録 (SRAMテーブルに動的登録)
  │   ├─ Fault系 → LED赤点灯+ハング
  │   ├─ SVCall, PendSV, SysTick → arch.cc ハンドラ
  │   ├─ DMA1_Stream3 → PDM IRQ
  │   ├─ DMA1_Stream5 → I2S Audio IRQ
  │   └─ OTG_FS → USB IRQ
  ├─ C++グローバルコンストラクタ呼び出し
  └─ main()
```

### main() 初期化シーケンス

```
main()
  ├─ Phase 1: クロック・GPIO
  │   ├─ init_clocks()        168 MHz 設定
  │   └─ init_gpio()          GPIO ピン設定
  │
  ├─ Phase 2: カーネル初期化
  │   ├─ init_shared_memory() SharedMemory 構造体初期化
  │   ├─ init_loader()        AppLoader 初期化 (RAM境界設定)
  │   └─ load_app()           .umiapp バイナリロード
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
  │   ├─ set_midi_callback()         → g_midi_queue にキュー
  │   ├─ set_sysex_callback()        → g_sysex_ready フラグ
  │   ├─ set_sample_rate_callback()  → サンプルレート変更要求
  │   ├─ on_streaming_change()       → Blue LED 制御
  │   └─ on_audio_in_change()        → Orange LED 制御
  │
  ├─ Phase 5: ペリフェラル初期化
  │   ├─ init_usb()           USB スタック初期化, NVIC有効
  │   ├─ init_systick()       SysTick 1kHz (1ms周期)
  │   ├─ init_cycle_counter() DWT サイクルカウンタ
  │   ├─ init_audio()         I2S3 オーディオコーデック
  │   └─ init_pdm_mic()       I2S2 PDM マイク
  │
  └─ Phase 6: RTOS開始
      └─ start_rtos(app_entry) → 以後リターンしない
```

---

## 2. タスク構成

### タスク一覧

| タスク | エントリ関数 | スタック | 優先度 | 用途 | FPU |
|--------|-------------|---------|--------|------|-----|
| Audio | `audio_task_entry()` | 1024B (CCM) | 0 (最高) | オーディオバッファ処理 | Yes |
| System | `system_task_entry()` | 512B (CCM) | 1 | PDM/SysEx/サンプルレート変更 | No |
| Control | `control_task_entry()` | 2048B (CCM) | 2 | アプリ main() 実行 | Yes |
| Idle | `idle_task_entry()` | 64B (CCM) | 3 (最低) | WFI (低電力) | No |

### タスク状態遷移

```
         ┌──────────┐
         │ Blocked  │ ←── task_yield() (ブロック条件あり)
         └────┬─────┘
              │ ISR/SysTick がNotify + Ready化
              ▼
         ┌──────────┐
         │  Ready   │ ←── スケジューラ選択待ち
         └────┬─────┘
              │ PendSV コンテキストスイッチ
              ▼
         ┌──────────┐
         │ Running  │ ←── 実行中
         └──────────┘
```

### スケジューラ

`select_next_task()` による単純優先度方式:

```
Audio(Ready?) → Yes → Audio実行
    ↓ No
System(Ready?) → Yes → System実行
    ↓ No
Control(Ready?) → Yes → Control実行
    ↓ No
Idle実行
```

### 各タスクの動作

**Audio Task** (優先度0):
```
audio_task_entry:
  loop:
    g_audio_ready_count == 0 ?
      → Yes: Blocked化, yield, 復帰後continue
      → No:  バッファをデキュー
             process_audio_frame(buf) 実行
```

**System Task** (優先度1):
```
system_task_entry:
  USB初期化待ち (yield)
  loop:
    handle_sample_rate_change()
    PDM処理 (g_pdm_ready && < 96kHz時)
      → cic_decimator.process_buffer()
    SysEx処理 (g_sysex_ready)
      → g_stdio.process_message()
    Blocked化, yield
```

**Control Task** (優先度2):
```
control_task_entry:
  app_entry()  ← アプリの main() を呼ぶ
  (main戻り後) Blocked化, shutdown待ち
```

**Idle Task** (優先度3):
```
idle_task_entry:
  loop:
    WFI (Wait For Interrupt)
```

---

## 3. ISR構成

### 例外ハンドラ

| 例外 | ハンドラ | 実装 | 用途 |
|------|---------|------|------|
| SysTick | `SysTick_Handler` | arch.cc | 1ms tick, タスク起床 |
| SVCall | `SVC_Handler` | arch.cc | アプリsyscall処理 |
| PendSV | `PendSV_Handler` | arch.cc | コンテキストスイッチ |
| HardFault等 | fault handler | main.cc | LED赤+ハング |

### ペリフェラル割り込み

| IRQ | ハンドラ | 優先度 | ソース | 処理 |
|-----|---------|--------|--------|------|
| DMA1_Stream5 | `DMA1_Stream5_IRQHandler` | 5 | I2S Audio DMA TC | バッファキュー→Audio Task起床 |
| DMA1_Stream3 | `DMA1_Stream3_IRQHandler` | 5 | PDM DMA TC | フラグセットのみ |
| OTG_FS | `OTG_FS_IRQHandler` | - | USB | `usb_device.poll()` |

### SysTick コールバック (1ms周期)

```
tick_callback():
  g_tick_us += 1000
  System Task → Ready化
  Control Task → タイムアウト判定 → Ready化
  変更あり → request_context_switch()
```

### SVCハンドラ (Syscall)

```
SVC_Handler (arch.cc, naked):
  PSP/MSP判定 → スタックポインタ取得
  → svc_handler_c() → svc_handler_impl()

Syscall一覧:
  0: Exit          アプリ終了
  1: RegisterProc  オーディオプロセッサ登録
  2: WaitEvent     イベント待ちブロック
  5: Yield         コンテキストスイッチ要求
 10: GetTime       g_tick_us 取得
 40: GetShared     SharedMemory ポインタ取得
 50: MidiSend      USB MIDI送信
 51: MidiRecv      MIDIキューからデキュー
```

### PendSV コンテキストスイッチ

```
PendSV_Handler (arch.cc, naked, アセンブリ):
  1. 現タスクのコンテキスト保存
     ├─ R4-R11, LR → 現TCBスタック
     └─ LR[4]==0 ? → S16-S31 FPUレジスタも保存
  2. BASEPRI = 0x50 (割り込みマスク)
  3. umi_cm4_switch_context() → スケジューラ呼び出し
  4. BASEPRI = 0 (マスク解除)
  5. 新タスクのコンテキスト復帰
     ├─ R4-R11, LR ← 新TCBスタック
     └─ LR[4]==0 ? → S16-S31 復帰
  6. BX LR (新タスクへ復帰)
```

---

## 4. ペリフェラルDMA/処理フロー

### オーディオ出力 (I2S3 + DMA1_Stream5)

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
    │ g_audio_ready_bufs[] にキュー (2スロット循環)
    │ Audio Task → Ready化
    │ request_context_switch() → PendSV
    ▼
audio_task_entry [Task, 優先度0]
    │ バッファをデキュー
    ▼
process_audio_frame(buf)
    │
    ├─ 1. USB Audio OUT読み取り
    │     usb_audio.read_audio() → i2s_work_buf[64]
    │
    ├─ 2. I2S出力パッキング
    │     pack_i2s_24(buf, i2s_work_buf) ← DMAバッファに直接書き込み
    │
    ├─ 3. アプリプロセッサ呼び出し
    │     ├─ MIDIイベント収集 (g_midi_queue)
    │     ├─ ボタンイベント収集 (g_button_queue)
    │     ├─ AudioContext構築
    │     └─ g_loader.call_process(ctx) → アプリの process()
    │
    ├─ 4. ソフトクリッピング
    │     synth_out_mono[] → [-1.0, 1.0] クランプ → 16bit変換
    │
    └─ 5. USB Audio IN書き込み
          L=マイク(PCM), R=シンセ → usb_audio.write_audio_in()
```

### PDMマイク (I2S2 + DMA1_Stream3)

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
    │ g_pdm_ready をポーリング
    ▼
cic_decimator.process_buffer()
    │ PDM 256サンプル → PCM 64サンプル
    ▼
pcm_buf[64]
    │ (次のaudio処理でUSB Audio INに送信)
```

### USB Audio

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
process_audio_frame()
  → stereo_buf: L=pcm_buf(マイク), R=last_synth_out(シンセ)
  → usb_audio.write_audio_in(stereo_buf, 64)
      ↓
USB ISR → ホストPC へ送信
```

### USB MIDI

**MIDI IN (PC → アプリ)**:
```
USB ISR → set_midi_callback()
  → g_midi_queue[write_idx] にエンキュー
      ↓
process_audio_frame() [Audio Task]
  → g_midi_queue からデキュー
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

### USB SysEx (Shell)

```
USB ISR → set_sysex_callback()
  → g_sysex_buf にコピー, g_sysex_ready = true
  → System Task を起床
      ↓
system_task_entry
  → g_stdio.process_message(g_sysex_buf, g_sysex_len)
  → シェルコマンド処理
  → g_stdio.write_stdout() → usb_audio.send_sysex()
```

### サンプルレート変更

```
USB ISR → set_sample_rate_callback(new_rate)
  → g_pending_sample_rate = new_rate
  → g_sample_rate_change_pending = true
      ↓
system_task_entry
  → handle_sample_rate_change()
      ├─ I2S IRQ無効化
      ├─ DMA + I2S 停止
      ├─ configure_plli2s(new_rate) ← PLL再設定
      ├─ DMA + I2S 再初期化
      └─ g_current_sample_rate 更新
```

---

## 5. タイミング定数

| パラメータ | 値 | 備考 |
|-----------|-----|------|
| CPUクロック | 168 MHz | STM32F4 最大 |
| SysTick | 1 kHz (1ms) | System Task起床, 時刻更新 |
| オーディオバッファ | 64サンプル @48kHz | ≈1.33ms レイテンシ |
| Audio DMA割り込み | 64サンプル毎 | ≈1.33ms 周期 |
| PDMバッファ | 256サンプル @2MHz | 64 PCMサンプルにデシメーション |
| USBフレーム | 1ms | SysTick同期 |

---

## 6. ファイル構成

| ファイル | 役割 |
|---------|------|
| `main.cc` | ブートシーケンス、Reset_Handler、Faultハンドラ、初期化順序 |
| `kernel.cc` | RTOSスケジューラ、タスクエントリ、オーディオ処理、syscall |
| `kernel.hh` | カーネル公開API (init, コールバック) |
| `arch.cc` | 例外ハンドラ (SVC, PendSV, SysTick)、タスクコンテキスト初期化 |
| `arch.hh` | アーキテクチャ抽象化API |
| `mcu.cc` | MCUペリフェラル初期化、DMA/IRQハンドラ、バッファアクセサ |
| `mcu.hh` | MCU抽象化API |
| `bsp.hh` | ボード定数 (ピン、メモリ、IRQ番号) |
