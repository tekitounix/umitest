# UMIOS内容分類とSTM32F4カーネルでの使用状況

このドキュメントは `lib/umios` の内容を分類し、現在のプロジェクト `examples/stm32f4_kernel` で「直接参照されているもの／されていないもの」を整理したものです。調査対象は `examples/stm32f4_kernel/src` と `examples/stm32f4_kernel/xmake.lua` の直接参照です。

## 1. `lib/umios` の内容分類

### adapter/ (外部環境アダプタ)
- `embedded.hh`, `embedded_adapter.hh`, `umim_adapter.hh`
- `web/` (`web_adapter.hh`, `umi.js`, worklet関連)
- 目的: 組み込みやWeb環境向けにUMI APIを橋渡しする層。

### app/ (アプリ側ランタイム／ビルド支援)
- `umi_app.hh`, `syscall.hh`, `crt0.cc`, `app.ld`, `xmake.lua`
- 目的: `.umiapp` 向けのスタートアップやリンクスクリプト、アプリ側API。
- 備考: `examples/synth_app/xmake.lua` で `lib/umios/app/crt0.cc` を `add_files` している（アプリ側ビルド向け）。

### backend/ (プラットフォーム実装)
- `cm/` (Cortex-M向け)
  - `common/` (IRQ/NVIC/SysTick/DWTなど)
  - `stm32f4/` (STM32F4周辺デバイス: GPIO/I2C/I2S/USBなど)
  - `drivers/`, `platform/`, `svc_handler.hh`, `cortex_m4.hh`
- `wasm/` (Webシミュレーション／WASM向け)
  - `web_sim*.js`, `web_sim.hh`, `web_hal.hh`, `renode_hal.hh`
  - `platform/` (WASM側のplatform抽象実装)

### core/ (共通の基礎型とオーディオ処理基盤)
- `types.hh`, `time.hh`, `event.hh`, `error.hh`, `irq.hh`, `processor.hh`, `triple_buffer.hh`
- `audio_context.hh` (process()向けオーディオコンテキスト)
- `ui/` (UI関連: view/controller/map)
- 目的: OS/アプリ両方から使う共通API群。

### crypto/ (署名検証などの暗号基盤)
- `sha512.*`, `ed25519.*`, `public_key.hh`
- 目的: `.umiapp` 等の署名検証・ハッシュ計算。

### kernel/ (UMI OSカーネル本体)
- コア: `umi_kernel.hh`, `umi_audio.hh`, `umi_midi.hh`, `umi_shell.hh`
- 起動/保護: `umi_startup.hh`, `mpu_config.hh`, `protection.hh`
- アプリローダ: `loader.hh/.cc`, `app_header.hh`
- 監視/ログ: `umi_monitor.hh`, `log.hh`, `metrics.hh`, `fault_handler.hh`, `assert.hh`
- syscalls: `syscall_handler.hh`, `syscall/` (`syscall_numbers.hh`)
- port: `port/cm4/*` (コンテキスト切替などのCM4実装)
- modules: `modules/audio_module.hh`, `modules/usb_audio_module.hh`

### platform/ (抽象プラットフォームIF)
- `README.md` のみ (platform抽象とbackendの対応を説明)

---

## 2. `examples/stm32f4_kernel` で**直接参照**されているもの

以下は `#include` と `xmake.lua` の `add_files` から確認できる直接参照です。

### kernel
- `lib/umios/kernel/umi_kernel.hh`
- `lib/umios/kernel/shell_commands.hh`
- `lib/umios/kernel/app_header.hh`
- `lib/umios/kernel/loader.hh`
- `lib/umios/kernel/loader.cc`
- `lib/umios/kernel/port/cm4/context.hh` (`<port/cm4/context.hh>`)

### core
- `lib/umios/core/audio_context.hh`
  - （内部で `types.hh`, `event.hh`, `error.hh` を参照）

### backend/cm/common
- `lib/umios/backend/cm/common/dwt.hh`
- `lib/umios/backend/cm/common/scb.hh`
- `lib/umios/backend/cm/common/systick.hh`
- `lib/umios/backend/cm/common/nvic.hh`
- `lib/umios/backend/cm/common/irq.hh`
- `lib/umios/backend/cm/common/irq.cc`

### backend/cm/stm32f4
- `lib/umios/backend/cm/stm32f4/cs43l22.hh`
- `lib/umios/backend/cm/stm32f4/gpio.hh`
- `lib/umios/backend/cm/stm32f4/i2c.hh`
- `lib/umios/backend/cm/stm32f4/i2s.hh`
- `lib/umios/backend/cm/stm32f4/pdm_mic.hh`
- `lib/umios/backend/cm/stm32f4/rcc.hh`
- `lib/umios/backend/cm/stm32f4/irq_num.hh`

### crypto
- `lib/umios/crypto/sha512.cc`
- `lib/umios/crypto/sha512.hh`
- `lib/umios/crypto/ed25519.cc`
- `lib/umios/crypto/ed25519.hh`

---

## 3. 実際にコードで使われているUMIOSシンボル（抜粋）

`examples/stm32f4_kernel/src` の実コードで**呼び出し・型利用が確認できる**ものを列挙します。

### `examples/stm32f4_kernel/src/kernel.cc`
- `umios/kernel/umi_kernel.hh`
  - `umi::Kernel`（タスク管理: `create_task`, `wait_block`, `notify`, `suspend_task`, `resume_task`, `tick`, `get_next_task`, `prepare_switch`）
  - `umi::TaskId`（タスクID保持）
  - `umi::Priority`, `umi::FpuPolicy`
  - `umi::KernelEvent::AudioReady`（イベント待ち/通知）
  - `umi::SpscQueue`（MIDI/ボタン/オーディオバッファのキュー）
  - `umi::usec`（モノトニック時間型）
- `umios/kernel/loader.hh`
  - `umi::kernel::AppLoader`（`load`, `start`, `set_app_memory`, `set_shared_memory`, `register_processor`, `call_process`, `terminate` など）
  - `umi::kernel::AppState`（`Running` 判定）
  - `umi::kernel::ProcessFn`（プロセッサ登録）
  - `umi::kernel::SharedMemory`（`g_shared`, `set_sample_rate`, `sample_position` など）
- `umios/kernel/app_header.hh`
  - `umi::kernel::AppHeader`（`valid_magic`, `compatible_abi`, `entry_point`）
- `umios/kernel/shell_commands.hh`
  - `umi::os::ShellCommands`（シェルコマンド実行）
  - `umi::os::KernelStateView`, `umi::os::ShellConfig`, `umi::os::ErrorLog`, `umi::os::SystemMode`
- `umios/core/audio_context.hh`
  - `umi::AudioContext`（`inputs/outputs`, `input_events`, `output_events` などの構築）
  - `umi::sample_t`（出力バッファ型）
  - `umi::Event`, `umi::EventQueue`（MIDI/ボタンイベント生成・出力）
  - `umi::sample_position_t`（`AudioContext`内で使用）

### `examples/stm32f4_kernel/src/arch.cc`
- `umios/backend/cm/common/systick.hh`
  - `umi::port::arm::SysTick::init_us`
- `umios/backend/cm/common/dwt.hh`
  - `umi::port::arm::DWT::enable`, `umi::port::arm::DWT::cycles`
- `umios/backend/cm/common/scb.hh`
  - `umi::port::arm::SCB::trigger_pendsv`
- `umios/kernel/port/cm4/context.hh`
  - `umi::port::cm4::TaskContext`, `umi::port::cm4::init_task_context`

### `examples/stm32f4_kernel/src/main.cc`
- `umios/backend/cm/common/irq.hh`
  - `umi::irq::init`, `umi::irq::set_handler`
  - `umi::backend::cm::exc::*`（例外番号）
- `umios/backend/cm/common/nvic.hh`
  - `umi::port::arm::NVIC::set_prio`
- `umios/backend/cm/common/scb.hh`
  - `umi::port::arm::SCB`（直接使用）
- `umios/backend/cm/stm32f4/irq_num.hh`
  - `umi::stm32f4::irq::*`（IRQ番号）

### `examples/stm32f4_kernel/src/mcu.cc`
- `umios/backend/cm/stm32f4/gpio.hh`
  - `umi::stm32::GPIO`（`config_output`, `config_af`, `set`, `reset` など）
- `umios/backend/cm/stm32f4/i2c.hh`
  - `umi::stm32::I2C::init`
- `umios/backend/cm/stm32f4/i2s.hh`
  - `umi::stm32::I2S`（`init_48khz`, `init_with_divider`, `enable`, `disable`, `enable_dma`）
  - `umi::stm32::DMA_I2S`（`init`, `enable`, `disable`, `current_buffer`, `transfer_complete`, `clear_tc`）
- `umios/backend/cm/stm32f4/cs43l22.hh`
  - `umi::stm32::CS43L22`（`init`, `power_on`, `set_volume`）
- `umios/backend/cm/stm32f4/pdm_mic.hh`
  - `umi::stm32::PdmMic` / `DmaPdm` / `CicDecimator`（`init`, `enable`, `process_buffer`, `current_buffer` 等）
- `umios/backend/cm/stm32f4/rcc.hh`
  - `umi::stm32::RCC`（クロック/周辺有効化）
- `umios/backend/cm/common/nvic.hh`
  - `umi::port::arm::NVIC::set_prio`, `umi::port::arm::NVIC::enable`

### `lib/umios/kernel/loader.cc`（`xmake.lua` でビルド対象）
- `umios/crypto/ed25519.hh`
  - `umi::crypto::ed25519_verify`（署名検証）
- `umios/crypto/public_key.hh`
  - `umi::crypto::RELEASE_PUBLIC_KEY`, `umi::crypto::DEVELOPMENT_PUBLIC_KEY`
- `umios/kernel/mpu_config.hh`
  - `umi::kernel::configure_mpu`（アプリ領域の保護設定）
- `umios/backend/cm/platform/privilege.hh`
  - `umi::backend::cm::platform::*`（特権/非特権切替の補助。ARM環境で条件付き使用）

---

## 4. `examples/stm32f4_kernel` で**直接参照されていない**もの

以下は `examples/stm32f4_kernel` からの直接参照が確認できない項目です（※ヘッダ内部での間接参照はあり得ます）。

### adapter/
- `embedded.hh`, `embedded_adapter.hh`, `umim_adapter.hh`
- `web/*`

### app/
- `umi_app.hh`, `syscall.hh`, `crt0.cc`, `app.ld`, `xmake.lua`

### backend/
- `backend/wasm/**` 全体
- `backend/cm/drivers/**`
- `backend/cm/platform/**`（※ `loader.cc` から `privilege.hh` を使用）
- `backend/cm/cortex_m4.hh`, `backend/cm/svc_handler.hh`
- `backend/cm/stm32f4/` のうち未使用: `exti.hh`, `hw.hh`, `power.hh`, `usb_midi.hh`, `usb_otg.hh`

### core/
- `app.hh`, `error.hh`, `event.hh`, `irq.hh`, `processor.hh`, `time.hh`, `triple_buffer.hh`, `types.hh`
- `ui/*`
  - ※ただし `audio_context.hh` から `types.hh/event.hh/error.hh` は**間接参照**されます。

### crypto/
- （`public_key.hh` は `loader.cc` から使用される）

### kernel/
- `assert.hh`, `coro.hh`, `driver.hh`, `embedded_state_provider.hh`, `fault_handler.hh`
- `log.hh`, `metrics.hh`, `protection.hh`（`mpu_config.hh` は `loader.cc` から使用）
- `syscall_handler.hh`, `syscall/syscall_numbers.hh`
- `umi_audio.hh`, `umi_midi.hh`, `umi_monitor.hh`, `umi_shell.hh`, `umi_startup.hh`
- `modules/*`
- `port/cm4/handlers.hh`, `port/cm4/handlers.cc`, `port/cm4/switch.hh`, `port/cm4/cm4.hh`

### platform/
- `README.md`

---

## 5. 補足
- ここでの「直接参照」は `#include` と `xmake.lua` に基づく分類です。
- **実際のコード使用**では、`audio_context.hh` 経由で `types.hh` / `event.hh` / `error.hh` の型が使われています（例: `umi::sample_t`, `umi::Event`, `umi::EventQueue`）。
- **ビルド時のリンク／依存の可能性**:
  - `examples/stm32f4_kernel/xmake.lua` は `add_deps("umi.embedded.full")` を指定しており、`lib/umi/xmake.lua` 定義により `umi.embedded` / `umi.dsp` / `umi.midi` / `umi.boot` / `umi.usb` を依存として取り込みます。
  - これらは主に **ヘッダオンリー**（include path 付与）ですが、ビルド構成によってはコンパイル／リンク対象が増える可能性があります。
  - 実際にリンクされる `.o` は、このドキュメントでは `add_files` で明示された `lib/umios/kernel/loader.cc` と `lib/umios/backend/cm/common/irq.cc`、および暗号ソース（`sha512.cc` / `ed25519.cc`）を確認済みです。
