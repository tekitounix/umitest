# 02: port/ アーキテクチャ

## 現状の問題

現在の `lib/umi/port/` は関心事が混在し、局所性が低い:

```
lib/umi/port/
├── cm/                              ← "Cortex-M" だが中身は複数の関心事が混在
│   ├── common/                      ← アーキテクチャ共通レジスタ (NVIC, SCB, SysTick)
│   ├── drivers/                     ← 汎用ドライバ (SysTick, UART)
│   ├── platform/                    ← プラットフォーム抽象 (syscall, privilege)
│   ├── stm32f4/                     ← MCU固有 (RCC, GPIO, I2S, USB)
│   ├── cortex_m4.hh                 ← アーキ層が cm/ 直下に散在
│   ├── svc_handler.hh               ←   〃
│   ├── uart_midi_input.hh           ← MIDI入力が cm/ 直下に
│   └── usb_midi_input.hh            ←   〃
├── wasm/
│   └── platform/                    ← cm/platform/ と同じファイル名で重複
└── (concepts は未定義)

lib/umi/kernel/
└── port/cm4/                        ← カーネルポートが backend と別の場所に

examples/stm32f4_kernel/src/
└── bsp.hh                           ← BSPが examples に散在（再利用不可）
```

**問題点:**
1. `cm/` の下にアーキテクチャ(M4)、MCU(STM32F4)、ドライバ、プラットフォーム抽象が全て混在
2. `kernel/port/cm4/` がbackendから分離 — 同じ関心事が2箇所に
3. BSPがexamples内 — ボードを増やすたびにexamplesが肥大化
4. `cm/` 直下にファイルが散在（cortex_m4.hh, svc_handler.hh, *_midi_input.hh）
5. 新MCU追加時に `cm/stm32h7/` を作っても、arch層やMIDI層は `cm/` 直下のまま

## 新構成: port/

### 原則: 1つの関心事 = 1つのディレクトリ

あるターゲットを追加・変更・削除するとき、**触るべきディレクトリが最小限**になる構成。

```
lib/umiport/                        ← backend → port にリネーム（明確な名前）
├── concepts/                          レイヤー契約（全ターゲット共通）
├── common/                            切り替え不要の共通コード
│   └── common/                        → #include <common/...>
├── arch/                              アーキテクチャ実装（切り替え単位: CPU）
│   ├── cm4/
│   │   └── arch/                      → #include <arch/...>
│   └── cm7/
│       └── arch/                      → #include <arch/...>
├── mcu/                               MCU実装（切り替え単位: SoC）
│   ├── stm32f4/
│   │   └── mcu/                       → #include <mcu/...>
│   └── stm32h7/
│       └── mcu/                       → #include <mcu/...>
├── board/                             ボード実装（切り替え単位: 基板）
│   ├── stm32f4_disco/
│   │   └── board/                     → #include <board/...>
│   └── daisy_seed/
│       └── board/                     → #include <board/...>
└── platform/                          プラットフォーム抽象（切り替え単位: 実行環境）
    ├── embedded/
    │   └── platform/                  → #include <platform/...>
    └── wasm/
        └── platform/                  → #include <platform/...>
```

### なぜ `port/` か

- `backend` は曖昧（何のバックエンド？）
- `port` は「移植層」として明確 — **同じコアをターゲットごとに移植する**という意味
- 業界慣例でも RTOS のターゲット固有コードを "port" と呼ぶ (FreeRTOS, Zephyr等)

## 5つのレイヤー

| レイヤー | `#include` | 切り替え単位 | 責務 | 例 |
|----------|------------|-------------|------|-----|
| **common** | `<common/...>` | 切り替え不要 | 全ターゲット共通のCM基盤 | NVIC, SCB, SysTick, DWT |
| **arch** | `<arch/...>` | CPUコア | コンテキストスイッチ、キャッシュ、FPU | cm4, cm7 |
| **mcu** | `<mcu/...>` | SoCファミリ | ペリフェラルレジスタ操作（HAL） | stm32f4, stm32h7 |
| **board** | `<board/...>` | 基板 | HALを組み合わせてConceptを満たすドライバ | stm32f4_disco, daisy_seed |
| **platform** | `<platform/...>` | 実行環境 | syscall, 特権、メモリ保護 | embedded, wasm |

## HAL・Driver・Port の関係

`port/` の内部には実質的に **HAL** と **Driver** の2つの役割がある。
これらを別のトップレベルディレクトリに分けるのではなく、`port/` の中でレイヤーとして自然に分離する。

```
port/ = HAL + Driver を包含する移植層
│
├── mcu/   ← HAL（Hardware Abstraction Layer）
│           レジスタレベルの操作。MCU固有。
│           例: RCC初期化、GPIO set/clear、SAIレジスタ設定、DMAチャネル制御
│           個々のペリフェラルに対して薄いAPIを提供する。
│
└── board/  ← Driver
              HAL（mcu/）の呼び出しを組み合わせて、高レベルConceptを満たす。
              例: AudioDriver = SAI + DMA + コーデックI2C初期化を束ねる
              カーネルやUSBスタックが依存するのはこのレイヤーのConcept実装。
```

**なぜ分けないのか:**

- HALとDriverは常にセットで使われる（Driverは必ずHALに依存する）
- `port/mcu/` = HAL、`port/board/` = Driver という対応が自然かつ明確
- 別トップレベルにすると、同じボード追加作業で `hal/stm32h7/` と `driver/daisy_seed/` の2箇所を行き来する必要があり局所性が下がる
- `port/` 1つの中で `mcu/ → board/` の依存方向が保たれていれば十分

**役割の違い:**

| | mcu/（HAL） | board/（Driver） |
|--|------------|-----------------|
| **粒度** | 1ペリフェラル = 1ヘッダ | 複数ペリフェラルを束ねる |
| **依存先** | レジスタアドレスのみ | mcu/ のAPI |
| **提供先** | board/ | カーネル、USBスタック等 |
| **Concept** | `GpioPort`, `DmaStream`, `AudioBus` 等 | `AudioDriver`, `McuInit` 等 |
| **例** | `mcu/sai.hh` — SAIレジスタ設定 | `board/audio_driver.hh` — SAI+DMA+codec初期化 |

## arch/ の位置づけ（PAL）

`arch/` は HAL/Driver の軸とは**直交**する独立レイヤーで、
伝統的には **PAL（Processor Abstraction Layer）** と呼ばれる。

| port/レイヤー | 伝統的名称 | 依存の軸 | 例 |
|--|--|--|--|
| **mcu/** | HAL | SoC依存 | STM32F4のRCC、GPIO、SAI |
| **board/** | Driver | 基板依存 | Daisy Seedのオーディオ初期化 |
| **arch/** | PAL / Arch port | CPUコア依存 | M7キャッシュ、M4コンテキストスイッチ |

arch/はDriverからもHALからも使われる。例えばDriverがDMA転送前にキャッシュフラッシュを行う場合、
`arch/cache.hh` を直接使う。ただし依存方向は常に `board/ → arch/`、`board/ → mcu/` であり逆はない。

## ミドルウェア（プロトコルスタック）

`lib/umiusb/`, `lib/umi/kernel/` 等のライブラリはHWに依存しない純粋なロジック層であり、
**ミドルウェア** または **プロトコルスタック** と呼ぶ。

```
ミドルウェア（umiusb, kernel等）
│   HW非依存。Conceptをテンプレートパラメータとして受け取る。
│
↓ Concept経由で注入
board/（Driver）
│   ミドルウェアのConcept + mcu/ のHAL を組み合わせて実装を提供。
│
↓ 使う
mcu/（HAL）
    レジスタ操作のみ。

arch/（PAL）— 上記とは直交。board/, mcu/ のどちらからも使われうる。
```

**Driverの役割が明確になる:**

board/ (Driver) は**2つの依存を橋渡し**する層:
1. 上位のミドルウェアが要求するConceptを満たす
2. 下位のHAL（mcu/）を呼び出して実際のHW操作を行う

```cpp
// board/daisy_seed/board/audio_driver.hh — Driver の例
// ミドルウェア（kernel）が要求する AudioDriver Conceptを満たす
// 内部では mcu/ の SAI, DMA, I2C を組み合わせる

#include <mcu/sai.hh>          // HAL
#include <mcu/dma.hh>          // HAL
#include <mcu/i2c.hh>          // HAL
#include <board/codec.hh>      // 同じboard層のコーデック制御
#include <arch/cache.hh>       // PAL（DMA前のキャッシュフラッシュ）

namespace umi::board {
struct SeedAudioDriver {
    void start() {
        codec_.init();           // I2C経由でコーデック初期化
        sai_.enable();           // SAI開始
        dma_.enable();           // DMA開始
    }
    void stop() { /* ... */ }
    // ...AudioDriver Conceptの要件を全て満たす
};
} // namespace umi::board
static_assert(umi::concepts::AudioDriver<umi::board::SeedAudioDriver>);
```

**USBスタックが既にこのパターン:**

```
lib/umiusb/include/core/hal.hh    ← Hal Concept定義（インターフェース）
port/mcu/stm32f4/mcu/usb_otg.hh   ← HAL実装（レジスタ操作）
```

USBスタックは `Hal` Conceptをテンプレートパラメータで受け取り、プロトコル処理に専念する。

## 派生ボード（Daisy Pod等）

Daisy PodはDaisy Seedを搭載した拡張ボード。Seed共通のDriverに加え、
Pod固有のUI（エンコーダ、LED、スイッチ）を追加する必要がある。

**xmakeインクルードパスの積み重ね（stacking）で実現:**

```lua
-- Daisy Seed共通のDriverを含み、さらにPod固有のオーバーレイを追加
target("daisy_pod_kernel")
    add_rules("arm-embedded", "umi-port")
    add_includedirs(
        "lib/umiport/arch/cm7/",
        "lib/umiport/mcu/stm32h7/",
        "lib/umiport/board/daisy_seed/",     -- ベースボード（Seed共通Driver）
        "examples/daisy_pod_kernel/src/"          -- Pod固有の拡張
    )
```

Pod固有の `board/` ヘッダは `examples/daisy_pod_kernel/src/board/` に配置する。
xmakeのインクルードパス解決順により、Pod側に同名ヘッダがあればオーバーライドされ、
なければSeed側にフォールバックする。

```
examples/daisy_pod_kernel/src/
└── board/
    ├── pod.hh              Pod固有：エンコーダ、LED、スイッチ定義
    └── bsp.hh              （オプション）Seed の bsp.hh をオーバーライドする場合
```

**他の派生ボードも同じパターン:**

```lua
target("daisy_patch_kernel")
    add_includedirs(
        -- arch, mcu は同じ
        "lib/umiport/board/daisy_seed/",     -- Seed共通
        "examples/daisy_patch_kernel/src/"        -- Patch固有の拡張
    )
```

Seed部分を共有しつつ、各派生ボード固有の定義だけを追加・上書きできる。

## レイヤー間の依存関係

```
target (examples/)
  └→ board   (ピン定義、コーデック)
      └→ mcu      (ペリフェラルドライバ)
          └→ arch     (CPU固有機能)
              └→ common  (CM共通基盤)

platform (syscall, 特権) は独立軸 — arch/mcu/board とは直交
```

**依存方向は上→下のみ。逆方向は禁止。**

## 局所性の検証

| 作業 | 触るディレクトリ |
|------|------------------|
| Daisy Seed対応追加 | `port/mcu/stm32h7/` + `port/arch/cm7/` + `port/board/daisy_seed/` + `examples/daisy_pod_kernel/` |
| Daisy Patchボード追加 | `examples/daisy_patch_kernel/` のみ（board/daisy_seed/を共有） |
| STM32F4のGPIOバグ修正 | `port/mcu/stm32f4/mcu/gpio.hh` のみ |
| RISC-V対応追加 | `port/arch/rv32/` + `port/mcu/<riscv_soc>/` + `port/board/<board>/` |
| WASMバックエンド修正 | `port/platform/wasm/` のみ |

## xmake統合

```lua
-- common/ と concepts/ は全ターゲット共通のため、ルールで自動付与
rule("umi-port")
    on_load(function (target)
        target:add("includedirs", "lib/umiport/common/")
        target:add("includedirs", "lib/umiport/concepts/")
    end)
rule_end()

-- ターゲット定義は切り替え部分のみ記述
target("stm32f4_kernel")
    add_rules("arm-embedded", "umi-port")
    add_includedirs(
        "lib/umiport/arch/cm4/",
        "lib/umiport/mcu/stm32f4/",
        "lib/umiport/board/stm32f4_disco/",
        "lib/umiport/platform/embedded/"
    )

target("daisy_pod_kernel")
    add_rules("arm-embedded", "umi-port")
    add_includedirs(
        "lib/umiport/arch/cm7/",
        "lib/umiport/mcu/stm32h7/",
        "lib/umiport/board/daisy_seed/",
        "lib/umiport/platform/embedded/"
    )

target("headless_webhost")
    add_rules("umi-port")
    add_includedirs(
        "lib/umiport/platform/wasm/"
        -- WASM は arch/mcu/board 不要
    )
```

## ディレクトリ配置（完全版）

```
lib/umiport/
│
├── concepts/                              レイヤー契約（Concept定義）
│   ├── arch_concepts.hh                   CacheOps, FpuOps, ContextSwitch, ArchTraits
│   ├── mcu_concepts.hh                    GpioPort, ClockControl, DmaStream, AudioBus, I2cBus
│   └── board_concepts.hh                  BoardSpec, Codec, McuInit
│
├── common/                                Cortex-M共通（切り替え不要）
│   └── common/
│       ├── nvic.hh                        割り込みコントローラ
│       ├── scb.hh                         システム制御ブロック
│       ├── systick.hh                     SysTick タイマ
│       ├── dwt.hh                         データウォッチポイント/サイクルカウンタ
│       ├── irq.hh                         動的IRQ管理
│       ├── irq.cc
│       ├── fault.hh                       フォールトハンドラ
│       ├── vector_table.hh                ベクタテーブル
│       ├── systick_driver.hh              SysTick汎用ドライバ
│       ├── uart_driver.hh                 UART汎用ドライバ
│       ├── uart_midi_input.hh             UART MIDI入力（MidiInput Concept実装）
│       └── usb_midi_input.hh              USB MIDI入力（MidiInput Concept実装）
│
├── arch/                                  アーキテクチャ固有
│   ├── cm4/                               Cortex-M4
│   │   └── arch/
│   │       ├── cache.hh                   no-op（M4にキャッシュなし）
│   │       ├── fpu.hh                     単精度FPU有効化
│   │       ├── context.hh                 コンテキストスイッチ（PendSV）
│   │       ├── svc_handler.hh             SVCハンドラ
│   │       ├── handlers.cc                例外ハンドラ実装
│   │       └── traits.hh                  M4特性定数
│   │
│   └── cm7/                               Cortex-M7
│       └── arch/
│           ├── cache.hh                   D-Cache / I-Cache 管理
│           ├── fpu.hh                     倍精度FPU有効化
│           ├── context.hh                 コンテキストスイッチ（PendSV）
│           ├── svc_handler.hh             SVCハンドラ
│           ├── handlers.cc                例外ハンドラ実装
│           └── traits.hh                  M7特性定数
│
├── mcu/                                   MCU固有ペリフェラル
│   ├── stm32f4/                           STM32F4xx
│   │   └── mcu/
│   │       ├── rcc.hh                     クロック設定
│   │       ├── gpio.hh                    GPIO
│   │       ├── i2s.hh                     I2Sドライバ
│   │       ├── i2c.hh                     I2C
│   │       ├── dma.hh                     DMA
│   │       ├── usb_otg.hh                USB OTG FS
│   │       ├── exti.hh                    外部割り込み
│   │       ├── power.hh                   電源管理
│   │       └── irq_num.hh                IRQ番号定義
│   │
│   └── stm32h7/                           STM32H7xx（新規）
│       └── mcu/
│           ├── rcc.hh                     クロック (PLL1/2/3)
│           ├── pwr.hh                     電源 (VOS0, SMPS)
│           ├── gpio.hh                    GPIO
│           ├── sai.hh                     SAI
│           ├── dma.hh                     DMA + DMAMUX
│           ├── i2c.hh                     I2C
│           ├── adc.hh                     ADC
│           ├── fmc.hh                     FMC (SDRAM)
│           ├── qspi.hh                    QUADSPI
│           ├── usb_otg.hh                USB OTG HS
│           └── irq_num.hh                IRQ番号定義
│
├── board/                                 ボード固有
│   ├── stm32f4_disco/                     STM32F4-Discovery
│   │   └── board/
│   │       ├── bsp.hh                    ピン配置、クロック、メモリマップ定数
│   │       ├── codec.hh                  CS43L22 DACコーデック
│   │       ├── audio.hh                  I2S + PDMマイク構成
│   │       └── mcu_init.cc              ボード固有MCU初期化
│   │
│   └── daisy_seed/                        Daisy Seed（全Daisyボード共通）
│       └── board/
│           ├── bsp.hh                    Seedピン配置、クロック、メモリマップ
│           ├── codec.hh                  AK4556/WM8731/PCM3060 検出・初期化
│           ├── audio.hh                  SAI1構成
│           ├── audio_driver.hh           AudioDriver Concept実装（SAI+DMA+codec）
│           └── mcu_init.cc              Seed共通MCU初期化
│
└── platform/                              実行環境抽象
    ├── embedded/                          組み込み（RTOS上）
    │   └── platform/
    │       ├── privilege.hh               特権レベル制御
    │       ├── protection.hh              メモリ保護
    │       └── syscall.hh                 syscall実装
    │
    └── wasm/                              WebAssembly
        └── platform/
            ├── privilege.hh               no-op
            ├── protection.hh              no-op
            ├── syscall.hh                 WASM import
            ├── web_hal.hh                 Web Audio API HAL
            └── web_sim.hh                 シミュレータ
```

## 新規ターゲット追加チェックリスト

- [ ] **arch**: 既存（cm4/cm7等）が使えるか確認。新規なら `port/arch/<name>/arch/` 作成
- [ ] **mcu**: `port/mcu/<name>/mcu/` 作成、同名ヘッダでConcept充足
- [ ] **board**: `port/board/<name>/board/` 作成（bsp.hh, codec.hh, audio.hh, mcu_init.cc）
- [ ] **target**: `examples/<name>/` にカーネル作成
- [ ] **xmake**: `umi-port` ルール + 3つのインクルードパス指定
- [ ] **リンカスクリプト**: メモリレイアウト定義
- [ ] **static_assert**: 全Concept充足をコンパイル時検証
- [ ] `#ifdef` やMCU名ハードコードが**一切ない**ことを確認
