# 05: 移行マッピング

## 現行ファイル → 新構成の対応表

| 現在の場所 | 新しい場所 | レイヤー |
|-----------|-----------|---------|
| `backend/cm/common/nvic.hh` | `port/common/common/nvic.hh` | common |
| `backend/cm/common/scb.hh` | `port/common/common/scb.hh` | common |
| `backend/cm/common/systick.hh` | `port/common/common/systick.hh` | common |
| `backend/cm/common/dwt.hh` | `port/common/common/dwt.hh` | common |
| `backend/cm/common/irq.hh` | `port/common/common/irq.hh` | common |
| `backend/cm/common/fault.hh` | `port/common/common/fault.hh` | common |
| `backend/cm/common/vector_table.hh` | `port/common/common/vector_table.hh` | common |
| `backend/cm/cortex_m4.hh` | `port/arch/cm4/arch/cortex_m4.hh` | arch |
| `backend/cm/svc_handler.hh` | `port/arch/cm4/arch/svc_handler.hh` | arch |
| `kernel/port/cm4/context.hh` | `port/arch/cm4/arch/context.hh` | arch |
| `kernel/port/cm4/handlers.cc` | `port/arch/cm4/arch/handlers.cc` | arch |
| `kernel/port/cm4/switch.hh` | `port/arch/cm4/arch/switch.hh` | arch |
| `backend/cm/stm32f4/rcc.hh` | `port/mcu/stm32f4/mcu/rcc.hh` | mcu |
| `backend/cm/stm32f4/gpio.hh` | `port/mcu/stm32f4/mcu/gpio.hh` | mcu |
| `backend/cm/stm32f4/i2s.hh` | `port/mcu/stm32f4/mcu/i2s.hh` | mcu |
| `backend/cm/stm32f4/i2c.hh` | `port/mcu/stm32f4/mcu/i2c.hh` | mcu |
| `backend/cm/stm32f4/usb_otg.hh` | `port/mcu/stm32f4/mcu/usb_otg.hh` | mcu |
| `backend/cm/stm32f4/cs43l22.hh` | `port/board/stm32f4_disco/board/cs43l22.hh` | board |
| `backend/cm/stm32f4/pdm_mic.hh` | `port/board/stm32f4_disco/board/pdm_mic.hh` | board |
| `examples/.../bsp.hh` | `port/board/stm32f4_disco/board/bsp.hh` | board |
| `backend/cm/uart_midi_input.hh` | `port/common/common/uart_midi_input.hh` | common |
| `backend/cm/usb_midi_input.hh` | `port/common/common/usb_midi_input.hh` | common |
| `backend/cm/drivers/systick_driver.hh` | `port/common/common/systick_driver.hh` | common |
| `backend/cm/drivers/uart_driver.hh` | `port/common/common/uart_driver.hh` | common |
| `backend/cm/platform/privilege.hh` | `port/platform/embedded/platform/privilege.hh` | platform |
| `backend/cm/platform/protection.hh` | `port/platform/embedded/platform/protection.hh` | platform |
| `backend/cm/platform/syscall.hh` | `port/platform/embedded/platform/syscall.hh` | platform |
| `backend/wasm/platform/privilege.hh` | `port/platform/wasm/platform/privilege.hh` | platform |
| `backend/wasm/platform/syscall.hh` | `port/platform/wasm/platform/syscall.hh` | platform |
| `backend/wasm/web_hal.hh` | `port/platform/wasm/platform/web_hal.hh` | platform |
| `backend/wasm/web_sim.hh` | `port/platform/wasm/platform/web_sim.hh` | platform |
| `umiusb/include/hal/stm32_otg.hh` | `port/mcu/stm32f4/mcu/usb_otg.hh` | mcu |

### 注目すべき分類判断

- **cs43l22.hh, pdm_mic.hh** → MCU層ではなく**board層**。CS43L22はSTM32F4-Discovery固有のコーデックであり、STM32F4の他のボードでは使わない
- **uart_midi_input.hh, usb_midi_input.hh** → **common**。MidiInput Conceptを満たすCM共通実装
- **kernel/port/cm4/** → **arch**に統合。コンテキストスイッチはアーキテクチャ固有機能
- **stm32_otg.hh** → umiusbライブラリ内からport/mcu/へ。ミドルウェアにHW実装を置かない

## インクルードパス変更

```
#include <umi/port/mcu/stm32f4/rcc.hh>     →  #include <mcu/rcc.hh>
#include <umi/port/mcu/stm32f4/gpio.hh>     →  #include <mcu/gpio.hh>
#include <umi/port/common/common/nvic.hh>       →  #include <common/nvic.hh>
```

## 移行手順

1. `lib/umiport/` 配下に新ディレクトリ構造を作成
2. 既存ファイルを上記マッピング表に従い移動
3. `#include` パスを全て更新
4. xmake.lua のインクルードパス設定を `umi-port` ルールに移行
5. ビルド確認（全ターゲット: stm32f4_kernel, headless_webhost, テスト）
6. `lib/umi/port/backend/` を削除、`kernel/port/` を削除
