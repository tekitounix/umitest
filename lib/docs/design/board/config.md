# ボード設定アーキテクチャ — 設定の統一と局所化

**ステータス:** 設計中
**関連文書:**
- [config_analysis.md](config_analysis.md) — 全フレームワーク横断比較と根拠付け
- [../research/board_config_zephyr.md](../research/board_config_zephyr.md) — Zephyr DTS/binding/board.yml 詳細調査
- [../research/board_config_esp_platformio_arduino.md](../research/board_config_esp_platformio_arduino.md) — ESP-IDF/PlatformIO/Arduino 調査
- [../research/board_config_modm_cmsis_mbed_rust.md](../research/board_config_modm_cmsis_mbed_rust.md) — modm/CMSIS-Pack/Mbed OS/Rust 調査
- [../research/board_config_mcu_families.md](../research/board_config_mcu_families.md) — MCU ファミリー間設定差異調査

## 1. 本ドキュメントの目的

ビルドシステムにおけるマイコン・ボード関連の設定を**統一**し**局所化**するための理想のアーキテクチャを議論する。ユーザーが新しいボードを追加するとき、触るべき場所が1箇所に集約されていること — これが本ドキュメントのゴールである。

---

## 2. 現状分析: 設定の散在

### 2.1 設定データの所在マップ

現在、MCU/ボード関連のデータは以下の **10箇所** に散在している:

| # | ファイル | フォーマット | 内容 | 書く人 | 消費者 |
|---|---------|-------------|------|--------|--------|
| 1 | `mcu-database.json` | JSON | core型, flash/ramサイズ, ベースアドレス, vendor, openocd_target, renode_repl | 人間 | embedded rule (Lua) |
| 2 | `cortex-m.json` | JSON | CPU型→FPU型, LLVM target triple, コンパイラフラグ | 人間 | embedded rule (Lua) |
| 3 | `build-options.json` | JSON | 最適化レベル, デバッグレベル, LTO, 言語標準 | 人間 | embedded rule (Lua) |
| 4 | `toolchain-configs.json` | JSON | リンカシンボル名, パッケージパス | 人間 | embedded rule (Lua) |
| 5 | `flash-targets.json` | JSON | PyOCD pack情報, デバイス名 | 人間 | flash plugin (Lua) |
| 6 | `board.hh` | C++ constexpr | HSE周波数, sysclk, ピン番号, メモリマップ | 人間 | C++コード |
| 7 | `platform.hh` | C++ struct | Output backend, Timer, init() | 人間 | C++コード |
| 8 | `linker.ld` | LD script | MEMORY定義, セクション配置 | 人間 | GNU LD |
| 9 | `startup.cc` | C++ | ベクタテーブル, FPU有効化, メモリ初期化 | 人間 | CPU |
| 10 | `rcc.hh` 等 | C++ | PLL係数, GPIO AF番号（**ハードコード**） | 人間 | C++コード |

### 2.2 データの重複と不整合リスク

同じ情報が複数箇所に存在し、手動同期が必要:

| データ | 存在箇所 | 不整合の影響 |
|--------|---------|-------------|
| メモリサイズ (flash/ram) | mcu-database.json, board.hh, linker.ld | リンクエラー or 実行時クラッシュ |
| メモリベースアドレス | mcu-database.json, board.hh, linker.ld, .resc (VTOR) | ベクタテーブル不整合でハードフォルト |
| HSE周波数 | board.hh, rcc.hh (PLL計算) | PLL設定ミスでクロック異常 |
| ピン番号/AF番号 | board.hh, GPIO初期化コード | ペリフェラル動作不良 |
| OpenOCD target | mcu-database.json, flash-targets.json | デバッグ/フラッシュ失敗 |
| core型 (cortex-m4f等) | mcu-database.json, cortex-m.json | コンパイラフラグ不一致 |

### 2.3 ユーザーが新しいボードを追加するときのタッチポイント

新しいMCU（例: STM32H533RE）を新しいボード（例: nucleo-h533re）で追加する場合、現状では以下のファイルを **手動で** 編集する必要がある:

1. **mcu-database.json** — MCUエントリ追加（core, flash, ram, vendor, openocd_target等）
2. **flash-targets.json** — PyOCD pack情報追加
3. **board.hh** — HSE周波数、ピン番号、メモリ定数
4. **platform.hh** — Output/Timer実装の選択と組み立て
5. **linker.ld** — メモリレイアウト（新MCU familyなら新規作成）
6. **startup.cc** — ベクタテーブル、FPU/クロック初期化（新MCU familyなら新規作成）
7. **rcc.hh相当** — PLL係数の計算とレジスタ設定（HSEが異なれば必須）
8. **xmake.lua** — ターゲット定義（`set_values("embedded.mcu", "...")` 等）
9. *(任意)* `.repl` ファイル — Renode対応する場合

**8-9箇所のファイルに手を入れる必要がある。** これはエラーが起きやすく、新規ボード追加の障壁が高い。

---

## 3. 概念階層: HAL・Driver・BSP

本ドキュメントで提案するアーキテクチャを理解するために、UMI における HAL・Driver・BSP の概念階層を明確にする。この分類は業界の標準的な用語法（Zephyr, Linux, Mbed OS, modm, Rust embedded）に準拠する。

### 3.1 概念定義

```
┌─────────────────────────────────────────────────────────┐
│                   Application Code                       │
│         umi::hal concepts を通じてハードウェアにアクセス     │
├─────────────────────────────────────────────────────────┤
│                                                         │
│    Device Driver (umidevice) — MCU 非依存               │
│    ┌─────────────────────────────────────────┐          │
│    │ 外部デバイス (DAC, CODEC, センサ) の制御   │          │
│    │ HAL concepts に依存、ペリフェラルドライバ    │          │
│    │ をテンプレートパラメータとして注入            │          │
│    │                                         │          │
│    │ template <I2cTransport Bus>              │          │
│    │ class Cs43l22 { ... };                   │          │
│    └──────────────────┬──────────────────────┘          │
│                       │ depends on HAL concepts          │
│    Peripheral Driver (umiport) — MCU 固有              │
│    ┌──────────────────┴──────────────────────┐          │
│    │ MCU 内蔵ペリフェラルのレジスタ操作          │          │
│    │ HAL concepts を満たす実装                  │          │
│    │                                         │          │
│    │ class Stm32I2c {  // models I2cTransport │          │
│    │     void write(addr, data) { ... }       │          │
│    │ };                                       │          │
│    └─────────────────────────────────────────┘          │
│                                                         │
├─────────────────────────────────────────────────────────┤
│    HAL (umihal)                                         │
│    ┌─────────────────────────────────────────┐          │
│    │ C++23 concepts による抽象インターフェース    │          │
│    │                                         │          │
│    │ template <typename T>                    │          │
│    │ concept I2cTransport = requires(T t) {   │          │
│    │     t.write(addr, data);                 │          │
│    │     t.read(addr, buf);                   │          │
│    │ };                                       │          │
│    └─────────────────────────────────────────┘          │
├─────────────────────────────────────────────────────────┤
│    BSP = board.lua                                      │
│    ┌─────────────────────────────────────────┐          │
│    │ ボード固有の設定値の集合体                   │          │
│    │ （コードではない）                          │          │
│    │                                         │          │
│    │ clock.hse = 8000000                      │          │
│    │ pin.console.tx = { port="A", pin=9 }     │          │
│    │ peripherals = { "USART1", "I2C1" }       │          │
│    │ platform.output = "rtt"                  │          │
│    └─────────────────────────────────────────┘          │
└─────────────────────────────────────────────────────────┘
```

| 概念 | UMI の対応 | 役割 | 性質 |
|------|-----------|------|------|
| **HAL** | umihal | デバイス操作の抽象インターフェース（C++23 concepts） | コード（型制約） |
| **Peripheral Driver** | umiport | MCU 固有のレジスタ操作で HAL concepts を実装 | コード（MCU依存） |
| **Device Driver** | umidevice | 外部デバイスの制御。HAL concepts に依存し、Peripheral Driver をテンプレート注入で受け取る。MCU 非依存 | コード（デバイス依存） |
| **BSP** | board.lua | ボード固有の設定値の集合体。どのドライバを使い、どのピンをどう接続し、クロックをいくつにするかという**選択と設定** | データ（設定値） |

### 3.2 依存性注入パターン

Device Driver は HAL concepts に依存し、具象の Peripheral Driver はテンプレートパラメータとして注入される:

```cpp
// HAL concept (umihal)
template <typename T>
concept I2cTransport = requires(T t, uint8_t addr, std::span<uint8_t> data) {
    { t.write(addr, data) } -> std::same_as<Result<void>>;
    { t.read(addr, data) }  -> std::same_as<Result<void>>;
};

// Peripheral Driver (umiport) — HAL の実装、MCU 固有
class Stm32I2c {
    // STM32 I2C レジスタを直接操作
    Result<void> write(uint8_t addr, std::span<uint8_t> data) { /* ... */ }
    Result<void> read(uint8_t addr, std::span<uint8_t> data)  { /* ... */ }
};
static_assert(I2cTransport<Stm32I2c>);

// Device Driver (umidevice) — HAL に依存、Peripheral Driver を注入
template <I2cTransport Bus>
class Cs43l22 {
    Bus& bus;
    void init() { bus.write(0x4A, ...); }  // CS43L22 固有のレジスタ操作
};

// BSP (board.lua) がこの組み立てを定義:
//   peripherals = { "I2C1" }
//   pin.audio.i2c_sda = { port = "B", pin = 9, af = 4 }
```

BSP（board.lua）は**コードを含まない**。「I2C1 を使う」「SDA は PB9」「SCL は PB6」という設定値を宣言するだけであり、これらの設定値からコード生成器が GPIO 初期化コードや platform.hh を生成する。

この「BSP = 設定値の集合体」という捉え方は、調査した全ての成熟したフレームワーク（Zephyr, modm, Mbed OS, Rust embedded, CMSIS-Pack）で確認された共通原則である（→ [config_analysis.md §9.7](config_analysis.md)）。

---

## 4. 設定の分類

散在する設定を性質で分類すると、3つの層に分かれる:

### 4.1 MCU層（MCU型番で一意に決まる）

MCUのデータシートから機械的に導出できる情報。同じMCUを使う全ボードで共通。

| データ | 例 | 導出元 |
|--------|---|--------|
| CPUコア型 | cortex-m4f | MCU型番 → データシート |
| FPU型 | fpv4-sp-d16 | CPUコア型 → ARM仕様 |
| Flash/RAMサイズ | 1M / 128K | MCU型番のサフィックス |
| メモリベースアドレス | 0x08000000 / 0x20000000 | MCU family共通 |
| ペリフェラル一覧 | USART1, SPI1, I2C1, ... | MCU型番 → データシート |
| DMAチャネル構成 | DMA1: 8 streams | MCU型番 → データシート |
| 割り込みベクタ数 | 82 (STM32F407) | MCU型番 → データシート |
| NVIC優先度ビット数 | 4 | CPUコア型 → ARM仕様 |
| デバッグIF | SWD + JTAG | MCU family共通 |
| OpenOCDターゲット | stm32f4x | MCU family → マッピング |
| PyOCD pack | stm32f4 | vendor + family → マッピング |

**特徴:** ボードに依存しない。MCU DBとして一度登録すれば、そのMCUを使う全ボードで再利用可能。

### 4.2 ボード層（基板設計で決まる）

回路図（Schematic）に記載される情報。同じMCUでもボードごとに異なる。

| データ | 例 | 導出元 |
|--------|---|--------|
| HSE周波数 | 8 MHz / 25 MHz | 水晶振動子の選定 |
| LSE周波数 | 32.768 kHz | RTC用水晶の有無 |
| ピン割り当て | PA9=USART1_TX (AF7) | 回路図のピン接続 |
| I2Cバス上のデバイス | WM8731 @ 0x1A on I2C1 | 回路図の接続 |
| SPIバス上のデバイス | Flash @ SPI1, CS=PA4 | 回路図の接続 |
| LED/ボタン | LED=PD12, BTN=PA0 | 回路図の接続 |
| 電源ドメイン | VDDA=3.3V, VDD=3.3V | 電源回路設計 |
| デバッグプローブ | ST-Link V2 (on-board) | ボード設計 |
| Renodeサポート | .repl ファイルパス | 仮想ボード定義の有無 |

**特徴:** MCU層からは導出できない。ボード定義として別途記述が必要。

### 4.3 アプリケーション層（ユーザーの選択で決まる）

ビルド設定。xmake.luaのtarget定義でユーザーが指定する。

| データ | 例 | 導出元 |
|--------|---|--------|
| ツールチェイン | gcc-arm / clang-arm | ユーザー選択 |
| 最適化レベル | size / speed / debug | ユーザー選択 |
| LTO | thin / full / none | ユーザー選択 |
| 言語標準 | C++23 / C++20 | ユーザー選択 |
| 出力フォーマット | elf, hex, bin, map | ユーザー選択 |
| セミホスティング | on / off | ユーザー選択 |

**特徴:** ハードウェアに依存しない。ビルドシステム側の関心事。

---

## 5. 理想のアーキテクチャ

### 5.1 設計原則

1. **Single Source of Truth** — 各データは1箇所にのみ定義される
2. **層の分離** — MCU層・ボード層・アプリケーション層を明確に分離する
3. **局所化** — ボード追加時に触るファイルはボードディレクトリ内に閉じる
4. **統一** — 全ボードが同じフォーマット・同じ方法で設定される
5. **導出可能なものは導出する** — 人間が書くのはソースデータのみ、残りは生成

### 5.2 ファイル構造

```
lib/umiport/
├── database/
│   └── mcu/
│       ├── stm32f407vg.lua     ← MCU層（MCU型番ごとに1ファイル）
│       ├── stm32h533re.lua
│       └── esp32c3.lua
│
├── boards/
│   ├── stm32f4-disco/
│   │   └── board.lua           ← ボード層（ボードごとに1ファイル）
│   ├── stm32f4-renode/
│   │   └── board.lua
│   └── nucleo-h533re/
│       └── board.lua
│
├── rules/
│   ├── board.lua               ← ビルドルール（共通、ボードごとに書かない）
│   └── board_generator.lua     ← ジェネレータ（共通）
│
├── templates/                  ← 生成テンプレート（共通）
│   ├── board.hh.tmpl
│   ├── memory.ld.tmpl
│   └── clock_config.hh.tmpl
│
├── include/umiport/board/      ← 生成出力先（build/ 配下でもよい）
│   └── (generated)
│
└── src/
    ├── arm/cortex-m/            ← MCU family共通コード
    │   ├── startup.cc
    │   ├── syscalls.cc
    │   └── sections.ld
    └── riscv/                   ← 別アーキテクチャ
        └── ...
```

### 5.3 MCU定義ファイル（MCU層）

```lua
-- database/mcu/stm32f407vg.lua
return {
    -- 識別
    vendor = "st",
    family = "stm32f4",
    device_name = "STM32F407VG",

    -- CPU
    core = "cortex-m4f",

    -- メモリ
    memory = {
        flash = { base = 0x08000000, size = 1024 * 1024 },
        ram   = { base = 0x20000000, size = 128 * 1024 },
        ccm   = { base = 0x10000000, size = 64 * 1024 },
    },

    -- ペリフェラル
    peripherals = {
        "USART1", "USART2", "USART3", "UART4", "UART5", "USART6",
        "SPI1", "SPI2", "SPI3",
        "I2C1", "I2C2", "I2C3",
        "I2S2", "I2S3",
        "TIM1", "TIM2", "TIM3", "TIM4", "TIM5",
        "DMA1", "DMA2",
        "ADC1", "ADC2", "ADC3",
        "DAC1",
        "USB_OTG_FS", "USB_OTG_HS",
    },

    -- クロック制約
    clock = {
        hse_range = { min = 4000000, max = 26000000 },
        sysclk_max = 168000000,
        apb1_max = 42000000,
        apb2_max = 84000000,
        pll = {
            m_range = { min = 2, max = 63 },
            n_range = { min = 50, max = 432 },
            p_values = { 2, 4, 6, 8 },
            q_range = { min = 2, max = 15 },
            vco_range = { min = 100000000, max = 432000000 },
            input_range = { min = 1000000, max = 2000000 },
        },
    },

    -- デバッグ
    debug = {
        openocd_target = "stm32f4x",
        pyocd_pack = "stm32f4",
    },

    -- スタートアップ
    startup = {
        vector_table_size = 98,     -- 16 core + 82 IRQ
        has_fpu = true,
        has_ccm = true,
    },
}
```

**設計判断:**
- **Luaフォーマット** — 計算式（`1024 * 1024`）が使える、xmakeでそのまま `dofile()` で読める
- **MCU型番ごとに1ファイル** — `mcu-database.json` の巨大な1ファイルではなく、個別ファイルで管理性向上
- **PLL制約** を含む — ビルド時にPLL係数のバリデーションが可能に
- **ペリフェラル一覧** を含む — 使用ペリフェラルの存在チェック、不要ドライバ除外に使用

### 5.4 ボード定義ファイル（ボード層）

```lua
-- boards/stm32f4-disco/board.lua
return {
    schema_version = 1,
    name = "stm32f4-disco",
    description = "STM32F4 Discovery board",
    mcu = "stm32f407vg",           -- → database/mcu/stm32f407vg.lua を参照

    -- クロック設定（ボード固有: 水晶振動子で決まる）
    clock = {
        hse = 8000000,              -- 8 MHz crystal on board
        lse = 32768,                -- 32.768 kHz RTC crystal
        sysclk = 168000000,         -- Target system clock
    },

    -- ピン割り当て（ボード固有: 回路図で決まる）
    pin = {
        console = {
            tx = { port = "A", pin = 9, af = 7 },    -- USART1_TX
            rx = { port = "A", pin = 10, af = 7 },   -- USART1_RX
        },
        audio = {
            i2s_ws  = { port = "A", pin = 4, af = 6 },   -- I2S3_WS
            i2s_ck  = { port = "C", pin = 10, af = 6 },  -- I2S3_CK
            i2s_sd  = { port = "C", pin = 12, af = 6 },  -- I2S3_SD
            i2c_sda = { port = "B", pin = 9, af = 4 },   -- I2C1_SDA
            i2c_scl = { port = "B", pin = 6, af = 4 },   -- I2C1_SCL
        },
        led = {
            green  = { port = "D", pin = 12 },
            orange = { port = "D", pin = 13 },
            red    = { port = "D", pin = 14 },
            blue   = { port = "D", pin = 15 },
        },
        button = {
            user = { port = "A", pin = 0, active = "high" },
        },
    },

    -- 使用ペリフェラル（MCU定義にある中から、このボードで使うもの）
    peripherals = { "USART1", "I2C1", "SPI3", "I2S3", "DMA1" },

    -- デバッグ（ボード固有: プローブの種類）
    debug = {
        probe = "stlink-v2",
        rtt = true,
    },

    -- Platform 実装の選択
    platform = {
        output = "rtt",             -- "uart", "rtt", "semihosting"
        timer = "dwt",              -- "dwt", "systick", "none"
    },
}
```

**設計判断:**
- `mcu` フィールドで MCU定義を参照 → メモリ・コア型等はMCU層から自動取得
- ピン定義はフラットな構造（`port + pin + af`） → GPIO初期化コード生成の入力
- `peripherals` はMCU定義のサブセット → ビルド時にMCU定義と突合可能
- `platform.output/timer` でC++のPlatform実装を選択 → platform.hh を生成

### 5.5 生成物

board.lua（ボード層）+ MCU定義（MCU層）から以下を生成:

```
board.lua + mcu/stm32f407vg.lua
    │
    ├──→ board.hh           C++ constexpr構造体
    │                       HSE, sysclk, ピン番号, メモリマップ
    │
    ├──→ platform.hh        Platform struct
    │                       Output/Timer の using 宣言 + static_assert
    │
    ├──→ memory.ld          リンカスクリプトの MEMORY セクション
    │                       MCU定義の flash/ram/ccm から導出
    │
    ├──→ clock_config.hh    PLL設定の constexpr
    │                       HSE + 目標sysclk → PLL M/N/P/Q 自動計算
    │
    ├──→ pin_config.hh      GPIO AF 設定テーブル
    │                       ピン定義 → GPIO初期化コード
    │
    ├──→ launch.json        デバッグ設定
    │                       MCU定義のopenocd_target + ボードのprobe
    │
    └──→ .resc              Renode設定（対応ボードのみ）
                            MCU定義のrenode_repl + メモリマップ
```

**生成されるboard.hh の例:**

```cpp
// Auto-generated from boards/stm32f4-disco/board.lua — DO NOT EDIT
#pragma once
#include <cstdint>

namespace umi::board {

struct Board {
    static constexpr uint32_t hse_frequency = 8'000'000;
    static constexpr uint32_t system_clock  = 168'000'000;
    static constexpr uint32_t apb1_clock    = 42'000'000;  // sysclk / 4（MCU制約から導出）
    static constexpr uint32_t apb2_clock    = 84'000'000;  // sysclk / 2（MCU制約から導出）

    struct Pin {
        static constexpr uint32_t console_tx = 9;   // PA9  AF7 (USART1_TX)
        static constexpr uint32_t console_rx = 10;  // PA10 AF7 (USART1_RX)
    };

    struct Memory {
        static constexpr uint32_t flash_base = 0x0800'0000;
        static constexpr uint32_t flash_size = 1024 * 1024;
        static constexpr uint32_t ram_base   = 0x2000'0000;
        static constexpr uint32_t ram_size   = 128 * 1024;
    };
};

} // namespace umi::board
```

**生成されるclock_config.hh の例:**

```cpp
// Auto-generated — PLL configuration for 8MHz HSE → 168MHz SYSCLK
#pragma once
#include <cstdint>

namespace umi::board::clock {

// PLL coefficients: SYSCLK = HSE / M * N / P
//   8MHz / 8 * 336 / 2 = 168MHz
//   VCO = 8MHz / 8 * 336 = 336MHz (range: 100-432MHz ✓)
//   PLL input = 8MHz / 8 = 1MHz (range: 1-2MHz ✓)
//   USB = 8MHz / 8 * 336 / 7 = 48MHz ✓
static constexpr uint32_t pll_m = 8;
static constexpr uint32_t pll_n = 336;
static constexpr uint32_t pll_p = 2;
static constexpr uint32_t pll_q = 7;

// Bus prescalers
static constexpr uint32_t ahb_prescaler  = 1;    // AHB  = 168MHz
static constexpr uint32_t apb1_prescaler = 4;    // APB1 = 42MHz (max 42MHz)
static constexpr uint32_t apb2_prescaler = 2;    // APB2 = 84MHz (max 84MHz)

} // namespace umi::board::clock
```

### 5.6 ビルドシステム統合

ユーザーの xmake.lua は変わらない:

```lua
target("my_app")
    set_kind("binary")
    add_rules("embedded", "umiport.board")
    set_values("embedded.mcu", "stm32f407vg")       -- MCU選択
    set_values("umiport.board", "stm32f4-disco")     -- ボード選択
    set_values("embedded.optimize", "size")           -- アプリケーション層
    add_files("main.cc")
    add_deps("umirtm", "umiport")
```

`umiport.board` ルールの内部処理:

```lua
rule("umiport.board")
    on_config(function(target)
        local board_name = target:values("umiport.board")
        if not board_name then return end

        -- 1. ボード定義を読む
        local board = load_board(board_name)          -- boards/stm32f4-disco/board.lua

        -- 2. MCU定義を読む（ボード定義の mcu フィールドから）
        local mcu = load_mcu(board.mcu)               -- database/mcu/stm32f407vg.lua

        -- 3. バリデーション
        validate_clock(board.clock, mcu.clock)         -- PLL制約チェック
        validate_peripherals(board.peripherals, mcu.peripherals)  -- 存在チェック
        validate_pins(board.pin, mcu)                  -- AF番号の正当性チェック

        -- 4. 生成
        generate_board_header(board, mcu, target)      -- board.hh
        generate_clock_config(board.clock, mcu.clock, target)  -- clock_config.hh
        generate_pin_config(board.pin, target)          -- pin_config.hh
        generate_platform_header(board.platform, target)  -- platform.hh
        generate_memory_ld(mcu.memory, target)          -- memory.ld

        -- 5. ビルド設定に反映
        target:add("includedirs", target:autogendir(), {public = false})
        apply_startup(mcu, target)                     -- startup.cc / syscalls.cc
        apply_linker_script(mcu, target)               -- sections.ld + memory.ld
    end)
```

---

## 6. データフォーマットの選択

### 6.1 候補

MCU定義・ボード定義のフォーマットとして以下を比較する:

| 判断軸 | Lua | TOML | JSON | YAML | DeviceTree |
|--------|-----|------|------|------|------------|
| xmake消費 | `dofile()` 即座 | パーサー必要 | `io.load()` 対応 | パーサー必要 | パーサー必要 |
| 計算式 | `128 * 1024` ✓ | ✗ | ✗ | ✗ | ✗ |
| 16進数 | `0x08000000` ✓ | `0x0800_0000` ✓ | ✗ | ✗ | `<0x08000000>` ✓ |
| コメント | `--` ✓ | `#` ✓ | ✗ | `#` ✓ | `/* */` ✓ |
| 副作用の安全性 | `dofile()` で任意コード実行可能 | 構造的に不可能 | 構造的に不可能 | 構造的に不可能 | 構造的に不可能 |
| スキーマ検証 | Lua関数で実装 | taplo + JSON Schema | JSON Schema | JSON Schema | dtschema |
| エディタ補完 | Lua LSP（弱い） | taplo（強い） | 強い | 強い | 専用エディタ |
| ネスト可読性 | テーブルリテラルで自然 | インラインテーブル or 冗長 | 可読 | インデント依存で誤りやすい | 専用構文 |
| 既存エコシステム | xmake全体がLua | プロジェクトに新規 | MCU DBが既にJSON | プロジェクトに新規 | Zephyr等で実績 |
| 学習コスト | xmakeユーザーはゼロ | 低い（広く普及） | ゼロ | 低い | 高い |

### 6.2 Lua vs TOML の深掘り

この2つが最も有力な候補である。

**Lua を選ぶ場合の利点:**

1. **xmakeとの親和性** — パーサー不要、既存のビルドシステムと同じ言語
2. **計算式** — `size = 128 * 1024` が自然に書ける
3. **条件分岐** — 必要なら分岐ロジックを書ける（ただしデータファイルでやるべきか？）
4. **学習コストゼロ** — xmakeユーザーはLuaを既に知っている
5. **依存ゼロ** — 追加パーサー不要

**Lua を選ぶ場合のリスク:**

1. **副作用** — `dofile()` は任意のLuaコードを実行する。`os.execute()` や `io.open()` が書ける
   - 緩和策: サンドボックス化された `loadfile()` で実行（`setfenv` でグローバルを制限）
   - 緩和策: コードレビューで `return { ... }` のみであることを確認する規約
2. **データとロジックの境界曖昧化** — ボード定義にロジックが入り込むと保守性低下
   - 緩和策: lint ルールで `return { ... }` 以外の構文を禁止
3. **外部ツール連携** — Python/Rust等でボード情報を読みたい場合にLuaパーサーが必要
   - 緩和策: JSON エクスポート機能を提供（`xmake board-export`）

**TOML を選ぶ場合の利点:**

1. **純粋なデータ** — 副作用が構造的に不可能。安全性が言語仕様で保証される
2. **スキーマ検証** — taplo + JSON Schema でエディタ上のリアルタイムバリデーション
3. **言語中立** — Python, Rust, Go 等どの言語からでもパーサーが豊富
4. **広い普及** — Cargo.toml, pyproject.toml, Hugo等で広く使われている

**TOML を選ぶ場合のリスク:**

1. **パーサー追加** — xmake標準ではTOMLパーサーが無い可能性。Lua TOML パーサーの追加依存
2. **計算式不可** — `128 * 1024` が書けない。`size = "128K"` とし、パーサー側で変換が必要
3. **エコシステム不一致** — プロジェクト内で唯一のTOMLファイルになる。xmake.lua, rules/*.lua と一貫性が無い
4. **ネスト表現** — ピン定義等の深い構造がセクションヘッダの羅列になり冗長

### 6.3 判断

**未決定。** 以下の観点で最終判断する:

- ボード定義の**消費者がxmake（Lua）のみ**である限り、Luaの方が自然。副作用リスクはサンドボックスで緩和可能
- 将来的に**外部ツール**（CLI、IDE拡張、CI）がボード情報を直接読む必要があるなら、TOMLの言語中立性が活きる
- **xmake自体がTOMLサポートを追加する**場合は、TOMLのデメリットが大幅に減少する

いずれのフォーマットでも、本ドキュメントで定義する**スキーマ（フィールド定義）は同一**である。フォーマットの選択はスキーマとは独立。

---

## 7. 生成と手書きの境界

### 7.1 何を生成し、何を手書きのままにするか

| ファイル | 現状 | 理想 | 理由 |
|---------|------|------|------|
| `board.hh` | 手書き | **生成** | ボード定義から全て導出可能 |
| `platform.hh` | 手書き | **生成** | `platform.output/timer` から組み立て可能 |
| `clock_config.hh` | 存在しない（rcc.hhにハードコード） | **生成** | HSE + 目標sysclk → PLL係数は機械的に計算可能 |
| `pin_config.hh` | 存在しない（各所にハードコード） | **生成** | ピン定義から GPIO AF 設定コードを導出可能 |
| `memory.ld` | 手書き（linker.ld内） | **生成** | MCU定義のメモリマップから導出 |
| `sections.ld` | 手書き | **手書き維持** | セクション配置はアーキテクチャ固有で複雑。MCU family共通のテンプレートとして管理 |
| `startup.cc` | 手書き | **手書き維持** | ベクタテーブルサイズ等は生成可能だが、初期化ロジックはC++コードとして人間が書く方が保守しやすい |
| `rcc.hh` (クロック初期化) | 手書き | **手書き維持 + 生成データ参照** | レジスタ設定の手順はC++コードだが、PLL係数は `clock_config.hh` の constexpr を参照 |

### 7.2 platform.hh の生成

現状の platform.hh はボードごとに手書きだが、内容は「Output実装の選択」と「Timer実装の選択」の組み合わせに過ぎない:

```cpp
// 現状: 手書き（stm32f4-renode/platform.hh）
struct Platform {
    using Output = stm32f4::RenodeUartOutput;
    using Timer  = cortex_m::DwtTimer;
    static void init() { Output::init(); }
    static constexpr const char* name() { return "stm32f4-renode"; }
};
```

board.lua で `platform.output = "uart"`, `platform.timer = "dwt"` と指定すれば生成可能:

```cpp
// 生成: board.lua の platform セクションから自動生成
struct Platform {
    using Output = umi::port::stm32f4::UartOutput;  // platform.output = "uart"
    using Timer  = umi::port::cortex_m::DwtTimer;   // platform.timer = "dwt"
    static void init() { Output::init(); }
    static constexpr const char* name() { return "stm32f4-renode"; }
};
static_assert(umi::hal::PlatformFull<Platform>);
```

ただし、`init()` の内容がボード固有の場合（例: 複数ペリフェラルの初期化順序が重要）は、生成ではカバーしきれない。この場合は board.lua に `platform.custom = true` を設定し、手書き platform.hh を許容する逃げ道を用意する。

---

## 8. バリデーション

ボード定義 + MCU定義から、ビルド時に以下のチェックを自動実行する:

### 8.1 クロック設定の整合性

```lua
function validate_clock(board_clock, mcu_clock)
    -- HSE が MCU の許容範囲内か
    assert(board_clock.hse >= mcu_clock.hse_range.min)
    assert(board_clock.hse <= mcu_clock.hse_range.max)

    -- 目標 sysclk が MCU の最大値以下か
    assert(board_clock.sysclk <= mcu_clock.sysclk_max)

    -- PLL 係数が解を持つか（M/N/P の組み合わせ探索）
    local m, n, p, q = solve_pll(board_clock.hse, board_clock.sysclk, mcu_clock.pll)
    if not m then
        raise("Cannot achieve %d Hz from %d Hz HSE with PLL constraints",
              board_clock.sysclk, board_clock.hse)
    end
end
```

### 8.2 ペリフェラルの存在チェック

```lua
function validate_peripherals(board_peripherals, mcu_peripherals)
    for _, p in ipairs(board_peripherals) do
        if not table.contains(mcu_peripherals, p) then
            raise("Peripheral '%s' is not available on this MCU", p)
        end
    end
end
```

### 8.3 ピン割り当ての正当性

```lua
function validate_pins(board_pins, mcu)
    -- 同一ピンが複数の機能に割り当てられていないか
    local used = {}
    for group, pins in pairs(board_pins) do
        for name, pin in pairs(pins) do
            local key = pin.port .. tostring(pin.pin)
            if used[key] then
                raise("Pin %s is assigned to both '%s' and '%s'", key, used[key], group.."."..name)
            end
            used[key] = group .. "." .. name
        end
    end
end
```

---

## 9. ユーザー体験: ビフォー・アフター

### Before（現状）: 新ボード追加に 8-9 ファイル

1. `mcu-database.json` にMCUエントリ追加
2. `flash-targets.json` にフラッシュ設定追加
3. `board.hh` を手書き作成
4. `platform.hh` を手書き作成
5. `linker.ld` を作成 or コピー修正
6. `startup.cc` を作成 or コピー修正（新MCU familyの場合）
7. クロック初期化コードを手書き
8. `xmake.lua` にターゲット定義追加
9. *(任意)* Renode設定追加

**→ エラーが発生するのは、これらのファイル間で値が不整合なとき。**

### After（理想）: 新ボード追加に 1-2 ファイル

1. `database/mcu/新mcu.lua` を作成（**MCUが未登録の場合のみ**。既存MCUなら不要）
2. `boards/新ボード/board.lua` を作成
3. `xmake.lua` にターゲット定義追加

**→ board.hh, platform.hh, memory.ld, clock_config.hh, pin_config.hh は全て自動生成。**
**→ バリデーションにより値の不整合はビルド時に検出。**

---

## 10. MCU ファミリー間差異への対応

### 10.1 ファミリー間の構造的差異

MCU ファミリーが異なると、ハードウェアの基本構造が根本的に異なる。統一的なスキーマでこれらの差異をどう吸収するかが設計上の重大な課題である（→ [research/board_config_mcu_families.md](research/board_config_mcu_families.md)）。

| 差異 | STM32F4 | STM32H7 | nRF52840 | RP2040 | ESP32-C3 |
|------|---------|---------|----------|--------|----------|
| クロック | PLL (M/N/P/Q) | 3 PLL (M/N/P/Q/R/FRACN) | 内部 64MHz (PLL なし) | Ring Osc + PLL | PLL + XTAL |
| 割り込み | NVIC (Cortex-M4) | NVIC (Cortex-M7) | NVIC (Cortex-M4) | NVIC (Cortex-M0+) | CLIC (RISC-V) |
| フラッシュ | 内蔵 (0x08000000) | 内蔵 (0x08000000) | 内蔵 (0x00000000) | 外部 QSPI (XIP) | 外部 SPI (XIP) |
| GPIO AF | AF テーブル (0-15) | AF テーブル (0-15) | PSEL レジスタ | Function Select | GPIO Matrix |
| 特殊要件 | CCM-RAM | DTCM/ITCM, D-Cache | BLE SoftDevice | boot2, PIO | パーティションテーブル |

### 10.2 設計方針: 共通スキーマ + ファミリー固有オプショナルセクション

全フレームワークの横断調査から、**「完全統一スキーマ」はどのフレームワークも採用していない**ことが確認された（→ [config_analysis.md §8.3](config_analysis.md)）。UMI は「共通スキーマ + ファミリー固有オプショナルセクション」方式を採用する。

**共通フィールド（全 MCU で必須）:**

```lua
return {
    schema_version = 1,
    name = "...",
    mcu = "...",                    -- MCU 定義ファイルへの参照
    clock = { ... },               -- クロック設定（フィールド名はファミリーで異なりうる）
    pin = { ... },                 -- ピン割り当て
    peripherals = { ... },         -- 使用ペリフェラル
    debug = { ... },               -- デバッグプローブ設定
    platform = { ... },            -- Platform 実装選択
}
```

**ファミリー固有フィールド（オプショナル）:**

```lua
-- nRF52840: SoftDevice 予約領域
nrf = {
    softdevice = "s140",
    softdevice_flash = 0x27000,     -- SoftDevice が占有する Flash サイズ
    softdevice_ram = 0x2800,        -- SoftDevice が占有する RAM サイズ
},

-- RP2040: 外部 Flash + boot2
rp2040 = {
    boot2 = "w25q080",              -- boot2 ブートローダー（Flash チップ依存）
    flash_size = 2 * 1024 * 1024,   -- 外部 Flash サイズ（MCU 定義にはない）
},

-- ESP32: パーティションテーブル + WiFi/BLE
esp32 = {
    partition_table = "partitions.csv",
    wifi = { ... },
    ble = { ... },
},

-- STM32H7: 複数 PLL + キャッシュ
stm32h7 = {
    pll2 = { ... },                 -- PLLI2S パラメータ
    pll3 = { ... },                 -- PLLSAI パラメータ
    dcache = true,                  -- D-Cache 有効化
},
```

### 10.3 クロックフィールドの柔軟性

PLL を持たない MCU（nRF52840）と複数 PLL を持つ MCU（STM32H7）では、clock セクションの構造が根本的に異なる:

```lua
-- STM32F4: 標準的な PLL
clock = {
    hse = 8000000,
    sysclk = 168000000,
},

-- nRF52840: PLL なし
clock = {
    hfclk = "external",            -- "internal" or "external"（HFXO 使用有無）
    lfclk = "xtal",                -- "rc", "xtal", "synth"
},

-- RP2040: 外部水晶 + PLL
clock = {
    xosc = 12000000,               -- HSE ではなく XOSC
    sysclk = 125000000,
},

-- ESP32-C3: RISC-V, PLL 自動管理
clock = {
    xtal = 40000000,               -- XTAL 周波数
    cpu = 160000000,               -- CPU クロック（PLL 係数は内部で決定）
},
```

MCU 定義ファイル側で `clock_model` フィールドを定義し、バリデーターがどのクロックフィールドを検証すべきかを判断する:

```lua
-- database/mcu/nrf52840.lua
return {
    clock_model = "nrf52_hfclk",   -- PLL バリデーション不要
    ...
}

-- database/mcu/stm32f407vg.lua
return {
    clock_model = "stm32_pll",     -- PLL M/N/P/Q バリデーション実行
    ...
}
```

### 10.4 メモリマップの多様性

MCU によって使用可能なメモリ領域の構造が大きく異なる:

| MCU | 領域 | 特殊性 |
|-----|------|--------|
| STM32F407VG | FLASH + SRAM1 + SRAM2 + CCM | CCM は DMA アクセス不可 |
| STM32H743 | FLASH + DTCM + ITCM + SRAM1-3 + BACKUP | DTCM/ITCM は TCM バス直結 |
| nRF52840 | FLASH + RAM | SoftDevice が先頭を占有 |
| RP2040 | 外部 Flash (XIP) + SRAM (4 banks) | 内蔵 Flash なし |
| ESP32-C3 | 外部 Flash (XIP) + IRAM + DRAM + RTC_SLOW | パーティションテーブルで分割 |

これらの差異は MCU 定義ファイルの `memory` セクションで吸収する。リンカスクリプトテンプレートは `memory` テーブルのキーを列挙して MEMORY セクションを生成するため、キー名や数が異なっても同一のジェネレータで対応可能:

```lua
-- database/mcu/stm32h743vi.lua
memory = {
    flash = { base = 0x08000000, size = 2048 * 1024 },
    dtcm  = { base = 0x20000000, size = 128 * 1024, attr = "rw!x" },
    itcm  = { base = 0x00000000, size = 64 * 1024,  attr = "rx"   },
    sram1 = { base = 0x30000000, size = 128 * 1024 },
    sram2 = { base = 0x30020000, size = 128 * 1024 },
    sram3 = { base = 0x30040000, size = 32 * 1024  },
},
```

---

## 11. 未解決の論点

### 11.1 MCU定義の粒度

MCU型番ごとに1ファイルか、MCU family ごとに1ファイルか:

- **型番ごと** (`stm32f407vg.lua`): 正確だが、同一familyの差分が小さい場合に冗長
- **familyごと** (`stm32f4.lua` + 型番でサイズ等をオーバーライド): DRYだが構造が複雑

Zephyr は型番ごとの `.dts` ファイルを family の `.dtsi` で継承する2段構造。UMIでも同様のパターンが有効:

```lua
-- database/mcu/stm32f407vg.lua
local family = dofile("stm32f4_base.lua")
return table.merge(family, {
    device_name = "STM32F407VG",
    memory = {
        flash = { base = 0x08000000, size = 1024 * 1024 },  -- VG = 1M
        ram   = { base = 0x20000000, size = 128 * 1024 },
        ccm   = { base = 0x10000000, size = 64 * 1024 },
    },
})
```

### 11.2 platform.hh の生成限界

ボード固有の初期化ロジック（例: 外部DACの電源シーケンス）は生成できない。`platform.custom = true` の逃げ道の設計が必要。

### 11.3 既存システムとの移行パス

現在の `mcu-database.json` + 手書き `board.hh` から、新しいボード定義システムへの段階的移行をどう行うか。後方互換性の維持期間。

### 11.4 フォーマット最終決定

Lua vs TOML — セクション6.3 で挙げた判断基準に基づき、実装フェーズで最終決定する。

---

## 12. 参考: 他フレームワークとの比較

| フレームワーク | ボード定義フォーマット | Single Source? | 生成物 |
|--------------|---------------------|---------------|--------|
| **Zephyr** | DeviceTree (.dts/.dtsi) | ✓ | C headers, Kconfig symbols |
| **ESP-IDF** | Kconfig + sdkconfig | ✓ | sdkconfig.h (C defines) |
| **PlatformIO** | platformio.ini + JSON | △（board JSON + user ini） | IDE configs |
| **Mbed OS** | targets.json | ✓ | cmake variables |
| **Arduino** | boards.txt + platform.txt | ✓（だが可読性低い） | IDE menus |
| **CMSIS-Pack** | .pdsc (XML) | ✓ | project configs |
| **modm** | .lb (Python) | ✓ | C++ headers, linker scripts |
| **UMI (理想)** | board.lua + mcu/*.lua | ✓ | board.hh, memory.ld, clock_config.hh, pin_config.hh, platform.hh |

modm のアプローチが UMI に最も近い（プログラミング言語でボード定義、C++ヘッダ生成）。

---

## 13. 参考文献

### 設計根拠

本ドキュメントの設計判断は、以下のリサーチ・分析ドキュメントに基づく:

| 文書 | 内容 |
|------|------|
| [config_analysis.md](config_analysis.md) | 全フレームワーク横断比較。6 類型分類、7 設計決定の根拠付け |
| [research/board_config_zephyr.md](research/board_config_zephyr.md) | Zephyr DTS ボード定義、PLL binding、pinctrl、board.yml (HWMv2) |
| [research/board_config_esp_platformio_arduino.md](research/board_config_esp_platformio_arduino.md) | ESP-IDF sdkconfig/soc_caps.h、PlatformIO JSON、Arduino boards.txt |
| [research/board_config_modm_cmsis_mbed_rust.md](research/board_config_modm_cmsis_mbed_rust.md) | modm board.hpp/connect<>()、CMSIS-Pack PDSC、Mbed targets.json、Rust BSP |
| [research/board_config_mcu_families.md](research/board_config_mcu_families.md) | 8 MCU ファミリーの設定差異。PLL/GPIO/メモリ/割り込み構造の比較 |

### 個別フレームワーク調査（一般）

| 文書 | 内容 |
|------|------|
| [research/zephyr.md](research/zephyr.md) | Zephyr 全体アーキテクチャ、HWMv2、DeviceTree include チェーン |
| [research/modm.md](research/modm.md) | modm-devices、lbuild モジュールシステム |
| [research/mbed_os.md](research/mbed_os.md) | targets.json 継承チェーン |
| [research/esp_idf.md](research/esp_idf.md) | 4 層抽象化、ldgen |
| [research/platformio.md](research/platformio.md) | board.json フラット定義 |
| [research/arduino.md](research/arduino.md) | boards.txt、variant/ |
| [research/rust_embedded.md](research/rust_embedded.md) | PAC/HAL/BSP クレート、embedded-hal |
| [research/cmsis_pack.md](research/cmsis_pack.md) | DFP/BSP パック、4 階層ヒエラルキー |
