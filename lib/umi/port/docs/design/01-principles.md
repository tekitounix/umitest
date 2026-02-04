# 01: 基本原則

## ルール1: ソースコードに切り替えロジックを書かない

```cpp
// ✗ 禁止 — ソースコード内での条件分岐
#ifdef STM32F4
#include <umi/port/mcu/stm32f4/rcc.hh>
#elif defined(STM32H7)
#include <umi/port/mcu/stm32h7/rcc.hh>
#endif

// ✗ 禁止 — フルパスによるMCU固有インクルード
#include <umi/port/mcu/stm32f4/rcc.hh>
```

```cpp
// ✓ 正解 — MCU非依存のインクルード
#include <mcu/rcc.hh>
```

どの `mcu/rcc.hh` が使われるかは **xmakeのインクルードディレクトリ設定のみ** で決まる。

## ルール2: 同名ヘッダで差し替え可能なインターフェースを維持する

各レイヤーは決められたディレクトリ名の下に同名のヘッダを配置する。
インクルードディレクトリを切り替えるだけで実装が差し替わる。

```cpp
#include <mcu/rcc.hh>      // MCU固有ドライバ
#include <mcu/gpio.hh>
#include <board/bsp.hh>     // ボード固有定数
#include <board/codec.hh>
#include <arch/context.hh>  // アーキテクチャ固有
#include <arch/cache.hh>
```

## ルール3: ビルドターゲットがレイヤーの組み合わせを定義する

```lua
-- xmakeがインクルードパスを設定 → 実装が決まる
target("daisy_pod_kernel")
    add_includedirs("lib/umiport/arch/cm7/")          -- arch/
    add_includedirs("lib/umiport/mcu/stm32h7/")       -- mcu/
    add_includedirs("lib/umiport/board/daisy_seed/")   -- board/
    add_includedirs("examples/daisy_pod_kernel/src/")     -- ボード拡張

target("stm32f4_kernel")
    add_includedirs("lib/umiport/arch/cm4/")           -- arch/
    add_includedirs("lib/umiport/mcu/stm32f4/")        -- mcu/
    add_includedirs("lib/umiport/board/stm32f4_disco/") -- board/
```

## ソースコードのインクルード例

### board/mcu_init.cc（ボード層のMCU初期化）

```cpp
#include <board/bsp.hh>       // ボード定数
#include <board/codec.hh>     // コーデック初期化
#include <board/audio.hh>     // オーディオ構成

#include <mcu/rcc.hh>         // クロック設定
#include <mcu/gpio.hh>        // GPIO
#include <mcu/dma.hh>         // DMA

#include <common/nvic.hh>     // 割り込み制御
```

### kernel側（arch層のみ使用）

```cpp
#include <arch/context.hh>    // コンテキストスイッチ
#include <arch/fpu.hh>        // FPU設定
#include <arch/cache.hh>      // キャッシュ管理

#include <common/systick.hh>  // SysTick
#include <common/scb.hh>      // SCB
```

### kernel.cc（カーネル — HW非依存）

```cpp
#include <board/bsp.hh>              // 定数参照のみ
#include <arch/traits.hh>            // アーキ定数
#include <umi/core/audio_context.hh>  // コアAPI（常に同一パス）
```
