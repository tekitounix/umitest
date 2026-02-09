# ボード設定スキーマの比較分析

**ステータス:** 完了
**作成日:** 2026-02-09
**前提文書:** ../research/zephyr.md, ../research/modm.md, ../research/mbed_os.md, ../research/platformio.md, ../research/arduino.md, ../research/esp_idf.md, ../research/rust_embedded.md, ../research/cmsis_pack.md, ../research/other_frameworks.md
**目的:** 調査した全フレームワークのボード設定スキーマを横断的に比較し、UMI の board.lua + mcu/*.lua アーキテクチャの設計判断を根拠に基づいて導出する

---

## 1. 本ドキュメントの目的

config.md で提案された UMI の board.lua + mcu/*.lua アーキテクチャは、クロック設定・ピン定義・メモリマップ・バリデーション等の多岐にわたる設計判断を含む。本ドキュメントでは、これらの個別判断が「なぜそう決めたのか」を、調査済みフレームワーク群との横断比較によって体系的に裏付ける。

対象とする比較軸:

1. スキーマの類型化 -- 設定フォーマットの構造的分類
2. クロック設定 -- PLL 係数・プリスケーラの記述と検証方法
3. ピン定義 -- GPIO ピンと Alternate Function の表現方式
4. メモリマップ -- Flash/RAM 領域の記述とリンカスクリプト生成
5. MCU 層 vs ボード層 -- データの責務分離
6. バリデーション -- 設定値の正当性検証手法
7. MCU ファミリー間の差異対応 -- 異なるアーキテクチャへの適応

---

## 2. スキーマの類型化

調査した全フレームワークのボード設定スキーマは、以下の 6 つの類型に分類される。

### 2.1 構造化ハードウェア記述型 (Structured Hardware Description)

**代表:** Zephyr DeviceTree

ハードウェアの物理構造をツリー状に記述する専用言語を用い、バインディング定義でスキーマを検証する。ペリフェラルの親子関係・割り込み接続・クロック接続がツリー構造として表現されるため、最も表現力が高い。

```dts
&pll {
    div-m = <8>;
    mul-n = <336>;
    div-p = <2>;
    div-q = <7>;
    clocks = <&clk_hse>;
    status = "okay";
};
```

**特徴:** バインディング YAML でプロパティの型・範囲・必須性を宣言的に検証。ビルド時にスキーマ違反を検出。

### 2.2 フラット宣言型 (Flat Declarative)

**代表:** PlatformIO board.json, Arduino boards.txt

各ボードをフラットなキーバリュー（JSON またはプロパティファイル）で記述する。継承メカニズムを持たず、ボード間のデータ重複が不可避。

```json
{
  "build": { "cpu": "cortex-m4", "mcu": "stm32f446ret6", "f_cpu": "180000000L" },
  "upload": { "maximum_ram_size": 131072, "maximum_size": 524288 }
}
```

**特徴:** 参入障壁が最低だが、データ継承がなく Fork-and-Modify パターンが常態化する。

### 2.3 コード生成型 (Code Generation)

**代表:** modm (Python + lbuild + Jinja2), STM32CubeMX

デバイスデータベースを入力とし、テンプレートエンジンが startup/vectors/linker script/GPIO 定義等を生成する。設定値は「生成の入力パラメータ」として扱われる。

```python
def build(env):
    device = env[":target"]
    env.template("uart.hpp.in", "uart.hpp",
                 substitutions={"instances": device.get_driver("uart:stm32")["instance"]})
```

**特徴:** 4,557 デバイスをカバーする modm-devices が単一源泉。手書きを極限まで排除。

### 2.4 Kconfig 型 (Runtime-Selectable Configuration)

**代表:** ESP-IDF

`soc_caps.h` のマクロ定義を唯一源泉とし、Kconfig シンボルを自動生成する。ユーザーは `menuconfig` で設定を選択し、`sdkconfig.h` として C コードに反映される。

```c
/* soc_caps.h */
#define SOC_UART_NUM            3
#define SOC_GPIO_PIN_COUNT      49
```

**特徴:** C マクロが唯一源泉で Kconfig は自動生成される一方、ボード概念が薄く MCU 固有設定に偏る。

### 2.5 継承型 (Inheritance)

**代表:** Mbed OS targets.json

OOP の継承に類似した `inherits` チェーンでプロパティを累積する。`_add`/`_remove` 修飾子で配列プロパティの差分管理が可能。

```json
{
  "NUCLEO_F411RE": {
    "inherits": ["MCU_STM32F411xE"],
    "detect_code": ["0740"],
    "device_name": "STM32F411RETx"
  }
}
```

**特徴:** 差分管理が直感的だが、モノリシック JSON（10,000 行超）の保守性が著しく低い。

### 2.6 トレイト型 (Type System as Configuration)

**代表:** Rust embedded-hal + PAC/HAL/BSP

ハードウェア設定を型システムで表現する。ピンの方向（入力/出力）、ペリフェラルの状態（初期化済/未初期化）がコンパイル時に検証される。memory.x のみ手書き。

```rust
let gpioa = dp.GPIOA.split();
let mut led = gpioa.pa5.into_push_pull_output();  // 型: PA5<Output<PushPull>>
```

**特徴:** 型安全性が最高だが、BSP カバレッジが低く memory.x は手書き。

### 2.7 類型別特性比較

| 類型 | 代表 | 表現力 | 検証 | 継承 | 参入障壁 | スケーラビリティ |
|------|------|:------:|:----:|:----:|:--------:|:----------------:|
| 構造化 HW 記述 | Zephyr | ★ | ◎ | ◎ | 高 | ◎ |
| フラット宣言 | PlatformIO | 低 | -- | -- | 最低 | -- |
| コード生成 | modm | ◎ | ◎ | ◎ | 中 | ★ |
| Kconfig | ESP-IDF | ○ | ○ | △ | 中 | ○ |
| 継承 | Mbed OS | ○ | ○ | ◎ | 低 | △ |
| トレイト | Rust | ◎ | ★ | -- | 高 | ○ |

---

## 3. クロック設定の比較

クロック設定はボード定義の中で最も複雑な領域である。HSE/HSI ソース選択、PLL パラメータ計算、バスプリスケーラ設定、USB 48MHz 導出の全てを正しく構成する必要がある。

### 3.1 フレームワーク別クロック設定方式

| フレームワーク | フォーマット | PLL パラメータ | バリデーション | プリスケーラ | 複数 PLL |
|--------------|------------|:-------------:|:-------------:|:----------:|:--------:|
| **Zephyr** | DTS ノード | 明示的 (div-m, mul-n, div-p, div-q) | DTS binding で型・範囲検証 | DTS prescaler ノード | ○ (PLL, PLLI2S) |
| **modm** | Python + XML | enable() 関数で手動設定 | prepare() で max-frequency チェック | enable() 引数 | ○ |
| **Mbed OS** | targets.json | clock_source フィールドのみ | ビルドシステム linting | HAL_RCC_OscConfig() で設定 | -- (HAL に委譲) |
| **PlatformIO** | board.json f_cpu | 目標周波数のみ | なし | なし | -- |
| **Arduino** | boards.txt f_cpu | なし (system_init で固定) | なし | なし | -- |
| **ESP-IDF** | Kconfig + soc_caps.h | Kconfig で CPU 周波数選択 | Kconfig range 制約 | periph_module_enable() | -- (PLL なし) |
| **Rust** | BSP コード内 constexpr | HAL の rcc::Config で設定 | コンパイル時アサーション（HAL 依存） | Config 構造体 | ○ |
| **CMSIS-Pack** | PDSC Dclock 属性 | なし (最大クロック記述のみ) | なし | なし | -- |

### 3.2 PLL 設定の詳細比較

**Zephyr** は DTS で PLL パラメータを明示的に記述する。ユーザーが M/N/P/Q を手動で計算して DTS に書く方式:

```dts
/* nucleo_f411re.dts */
&pll {
    div-m = <4>;
    mul-n = <192>;
    div-p = <4>;
    div-q = <8>;
    clocks = <&clk_hse>;
    status = "okay";
};
```

DTS binding で値の型と範囲を検証する:

```yaml
# st,stm32f4-pll-clock.yaml
properties:
  div-m: { type: int, required: true, enum: [2, 3, ..., 63] }
  mul-n: { type: int, required: true }
  div-p: { type: int, required: true, enum: [2, 4, 6, 8] }
```

**modm** は Board Support Package の `initialize()` 関数内で RCC を設定する。PLL 係数はユーザーが計算してコードに書く:

```cpp
// modm board.hpp
static void initialize() {
    Rcc::enableExternalCrystal();
    const Rcc::PllFactors pll{8, 336, 2, 7};  // M=8, N=336, P=2, Q=7
    Rcc::enablePll(Rcc::PllSource::ExternalCrystal, pll);
    Rcc::setFlashLatency<Frequency>();
    Rcc::enableSystemClock(Rcc::SystemClockSource::Pll);
    Rcc::setAhbPrescaler(Rcc::AhbPrescaler::Div1);
    Rcc::setApb1Prescaler(Rcc::Apb1Prescaler::Div4);
    Rcc::setApb2Prescaler(Rcc::Apb2Prescaler::Div2);
}
```

modm-devices XML が MCU の最大周波数制約を保持する:

```xml
<driver type="rcc" name="stm32-f2f4">
  <ip max-frequency="168000000"/>
</driver>
```

### 3.3 PLL を持たない MCU への対応

nRF52840 は PLL を持たず、64MHz の内部発振器で動作する。各フレームワークの対応:

| フレームワーク | nRF52840 対応 |
|--------------|-------------|
| **Zephyr** | clock ノードに `hfclk-div = <1>` のみ。PLL ノード不在 |
| **modm** | nRF52 モジュールに PLL 関連コードが存在しない |
| **ESP-IDF** | ESP32 も PLL 構造が STM32 と異なる。Kconfig の CPU 周波数選択で抽象化 |
| **Rust** | nrf52840-hal の clocks モジュールで HFCLK/LFCLK を設定。PLL 概念なし |

**示唆:** PLL 設定は MCU ファミリー固有のオプショナルセクションとして扱い、PLL を持たない MCU では当該セクションを省略可能にすべきである。

### 3.4 USB 48MHz 導出

USB 動作には正確な 48MHz クロックが必要であり、PLL の Q 分周器で導出するのが一般的:

- **Zephyr:** `div-q` パラメータで明示。48MHz 制約は DTS binding では検証不可（ユーザー責任）
- **modm:** `PllFactors.q` で指定。周波数チェックはランタイム assert
- **ESP-IDF:** `SOC_USB_OTG_SUPPORTED` マクロで USB 対応を宣言。クロック設定は soc 内部

---

## 4. ピン定義の比較

ピン定義は「物理ピン → ペリフェラル機能」のマッピングを記述する。型安全性、AF (Alternate Function) 検証、コンフリクト検出の 3 軸で比較する。

### 4.1 フレームワーク別ピン定義方式

| フレームワーク | フォーマット | 型安全性 | AF 検証 | コンフリクト検出 |
|--------------|-----------|:--------:|:-------:|:---------------:|
| **Zephyr** | `STM32_PINMUX('A', 9, AF7)` マクロ | -- (マクロ) | DTS binding | DTS 重複検出 |
| **modm** | `GpioA2::Tx` 型エイリアス | ◎ (コンパイル時) | `connect<>()` テンプレート | コンパイルエラー |
| **Mbed OS** | `PinNames.h` enum (`PA_9 = 0x09`) | △ (enum) | なし | なし |
| **PlatformIO** | board.json にピン情報なし | -- | -- | -- |
| **Arduino** | `pins_arduino.h` 定数 (`LED_BUILTIN = 13`) | -- (マクロ/定数) | なし | なし |
| **ESP-IDF** | `GPIO_NUM_x` enum + GPIO Matrix | △ (enum) | `gpio_iomux_out()` 実行時 | 実行時チェック |
| **Rust** | `bsp_pins!` マクロ / 型別名 | ◎ (型システム) | コンパイル時 | 所有権システム |
| **CMSIS-Pack** | PDSC XML pin 要素 | -- (データのみ) | ツール依存 | ツール依存 |

### 4.2 各方式の詳細

**Zephyr pinctrl:**

```dts
&usart1 {
    pinctrl-0 = <&usart1_tx_pa9 &usart1_rx_pa10>;
    pinctrl-names = "default";
    status = "okay";
};
```

`STM32_PINMUX` マクロが port, pin, AF の 3 値をパックする:

```c
#define STM32_PINMUX(port, pin, af) ((port << 12) | (pin << 4) | af)
/* usart1_tx_pa9 = STM32_PINMUX('A', 9, AF7) */
```

**modm GpioA2 型エイリアス:**

```cpp
using LedGreen = GpioOutputD12;
using ButtonUser = GpioInputA0;

// AF 検証はコンパイル時テンプレート
Usart1::connect<GpioA9::Tx, GpioA10::Rx>();  // AF7 を自動選択
// GpioA9 に USART1_TX の AF がなければコンパイルエラー
```

**ESP-IDF GPIO Matrix:**

ESP32 は GPIO Matrix により任意の GPIO を任意のペリフェラル信号に接続可能:

```c
gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << GPIO_NUM_4),
    .mode = GPIO_MODE_OUTPUT,
};
gpio_config(&io_conf);
esp_rom_gpio_connect_out_signal(GPIO_NUM_4, UART1_TXD_IDX, false, false);
```

固定ピン（IOMUX）より遅延が大きいが、ピン割り当ての自由度が極めて高い。

### 4.3 ピン定義の本質: 3 値 (port + pin + AF) の十分性

全てのフレームワークを横断して分析すると、ピン定義の基本単位は以下の 3 値に収束する:

| 値 | 意味 | 例 |
|----|------|---|
| port | GPIO ポート | A, B, C, ... |
| pin | ピン番号 | 0-15 |
| AF | Alternate Function 番号 | 0-15 (STM32), -- (GPIO Matrix) |

ESP-IDF の GPIO Matrix は AF 番号を持たないが、ペリフェラル信号 ID が同等の役割を果たす。この 3 値モデルは ARM Cortex-M の STM32, nRF52, RP2040 全てで適用可能である。

---

## 5. メモリマップの比較

メモリマップはリンカスクリプト生成の入力であり、MCU 定義の最も基本的な構成要素である。

### 5.1 フレームワーク別メモリマップ方式

| フレームワーク | Flash Base | RAM リージョン | リンカ生成 | TZ 分割 |
|--------------|:----------:|:--------------:|:----------:|:-------:|
| **Zephyr** | DTS memory ノード | 複数リージョン対応 (CCM, DTCM, SRAM1-4) | ◎ (テンプレート完全生成) | ○ (TF-M 連携) |
| **modm** | modm-devices XML | ★ (CCM, SRAM1/2, BACKUP 全対応) | ◎ (テンプレート完全生成) | -- |
| **Mbed OS** | targets.json `mbed_rom_start` | FLASH + RAM の 2 リージョンのみ | ○ (テンプレート + マクロ) | ○ |
| **PlatformIO** | board.json `upload.maximum_size` | FLASH + RAM の 2 リージョン | △ (2 リージョン限定テンプレート) | -- |
| **Arduino** | boards.txt `upload.maximum_size` | FLASH + RAM の 2 リージョン | -- (手書き) | -- |
| **ESP-IDF** | memory.ld.in テンプレート | IRAM, DRAM, RTC_SLOW 等 | ◎ (ldgen テンプレート) | -- |
| **Rust** | memory.x 手書き | ユーザーが自由に定義 | -- (cortex-m-rt がセクション提供) | -- |
| **CMSIS-Pack** | PDSC `<memory>` 要素 | 複数リージョン + アクセス権限 | ○ (ツールチェーン依存) | ○ |
| **libopencm3** | devices.data | CCM 条件付き対応 | ◎ (genlink + C プリプロ) | -- |

### 5.2 複数 RAM リージョンへの対応

STM32F407VG は以下の 4 つのメモリ領域を持つ:

| 領域 | アドレス | サイズ | 特徴 |
|------|---------|--------|------|
| FLASH | 0x08000000 | 1 MB | コード + 読み取り専用データ |
| SRAM1 | 0x20000000 | 112 KB | 汎用 RAM |
| SRAM2 | 0x2001C000 | 16 KB | 汎用 RAM (SRAM1 と連続) |
| CCM | 0x10000000 | 64 KB | DMA アクセス不可、高速 |

この 4 領域全てをリンカスクリプトに反映できるかどうかが、フレームワークの成熟度指標となる:

| フレームワーク | SRAM1 | SRAM2 | CCM | BACKUP |
|--------------|:-----:|:-----:|:---:|:------:|
| **modm** | ○ | ○ | ○ | ○ |
| **Zephyr** | ○ | ○ | ○ | ○ |
| **libopencm3** | ○ | △ | ○ | -- |
| **ESP-IDF** | -- (ESP32 固有) | -- | -- | -- |
| **Mbed OS** | ○ (SRAM1+2 合算) | -- | -- | -- |
| **PlatformIO** | ○ (合算) | -- | -- | -- |
| **Arduino** | ○ (合算) | -- | -- | -- |
| **Rust** | memory.x に書けば対応 | 同左 | 同左 | 同左 |

### 5.3 SoftDevice 予約 (nRF52)

Nordic nRF52 シリーズでは BLE SoftDevice がフラッシュと RAM の先頭を占有するため、アプリケーションは残りの領域を使用する:

- **Zephyr:** DTS の `zephyr,flash` と `zephyr,sram` ノードで使用可能領域を指定
- **Mbed OS:** targets.json の `mbed_rom_start` / `mbed_ram_start` をオフセット付きで定義
- **Rust:** memory.x でユーザーが手動でオフセットを調整

### 5.4 TrustZone 分割 (STM32U5 等)

ARMv8-M (Cortex-M33/M55) の TrustZone では Secure/Non-Secure でメモリを分割する:

- **Zephyr:** TF-M (Trusted Firmware-M) と連携し、Secure/Non-Secure 両方の DTS を管理
- **CMSIS-Pack:** `<memory>` 要素の `access` 属性で `s` (secure) / `n` (non-secure) を指定
- **Mbed OS:** TrustZone 対応の `mbed_rom_start` / `mbed_rom_size` をターゲットごとに定義

---

## 6. MCU 層 vs ボード層の分離

「MCU 型番で一意に決まるデータ」と「回路図（基板設計）で決まるデータ」の分離は、全ての成熟したフレームワークが到達した共通設計原則である。

### 6.1 フレームワーク別分離方式

| フレームワーク | MCU ソース | ボードソース | 関係性 |
|--------------|-----------|------------|--------|
| **Zephyr** | SoC DTSI (ペリフェラル disabled) | Board DTS (enable + configure) | include チェーン (6-8 段) |
| **modm** | modm-devices XML (4,557+ デバイス) | board module.lb + board.hpp | lbuild モジュール依存 |
| **Mbed OS** | MCU_STM32F4 → MCU_STM32F411xE | NUCLEO_F411RE | inherits チェーン (4-5 段) |
| **PlatformIO** | board.json に MCU 情報を含む | board.json (同一ファイル) | **分離なし** |
| **Arduino** | boards.txt に MCU 情報を含む | boards.txt + variant/ | **分離なし** (variant はピンのみ) |
| **ESP-IDF** | soc_caps.h (MCU 固有) | sdkconfig (プロジェクト設定) | パス切り替え (`soc/<chip>/`) |
| **Rust** | PAC クレート (SVD から生成) | BSP クレート (手書き) | クレート依存 (独立パッケージ) |
| **CMSIS-Pack** | DFP (Device Family Pack) | BSP (Board Support Pack) | パック間参照 |
| **libopencm3** | devices.data (パターンマッチ) | **ボード概念なし** | -- |

### 6.2 分離の深度比較

分離の「深度」を、MCU 情報とボード情報がどの程度独立して管理されているかで評価する:

| レベル | 意味 | 該当フレームワーク |
|:------:|------|------------------|
| 0 | 分離なし (同一ファイルに混在) | PlatformIO, Arduino |
| 1 | 概念的分離 (inherits でチェーン) | Mbed OS |
| 2 | ファイル分離 (別ファイルだが同一リポジトリ) | Zephyr, modm, ESP-IDF, libopencm3 |
| 3 | パッケージ分離 (独立パッケージ) | Rust (PAC/HAL/BSP), CMSIS-Pack (DFP/BSP) |

**UMI の設計:** MCU 定義 (`database/mcu/*.lua`) とボード定義 (`boards/*/board.lua`) をファイル分離（レベル 2）とする。レベル 3（パッケージ分離）は UMI の規模にはオーバーエンジニアリングであり、レベル 0-1 はスケーラビリティに欠ける。

### 6.3 Zephyr の SoC DTSI / Board DTS パターン

Zephyr の分離は最も教科書的である:

```dts
/* stm32f411.dtsi — SoC 定義: ペリフェラルは全て disabled */
/ {
    soc {
        usart1: serial@40011000 {
            compatible = "st,stm32-usart";
            reg = <0x40011000 0x400>;
            interrupts = <37 0>;
            status = "disabled";   /* デフォルト: 無効 */
        };
    };
};
```

```dts
/* nucleo_f411re.dts — ボード定義: 使うペリフェラルのみ enable */
&usart1 {
    pinctrl-0 = <&usart1_tx_pa9 &usart1_rx_pa10>;
    current-speed = <115200>;
    status = "okay";   /* このボードでは有効化 */
};
```

MCU は「何ができるか」を宣言し、ボードは「何を使うか」を宣言する。この責務分離は UMI の MCU 定義 (`peripherals` リスト) とボード定義 (`peripherals` サブセット) に直接対応する。

---

## 7. バリデーション手法の比較

ボード設定値の正当性を「いつ」「何を」「どのように」検証するかを比較する。

### 7.1 フレームワーク別バリデーション方式

| フレームワーク | 検証タイミング | 検証対象 | 検証方法 |
|--------------|:------------:|---------|---------|
| **Zephyr** | ビルド時 | プロパティの型・範囲・必須性 | DTS binding YAML (type, enum, const, required) |
| **modm** | ビルド時 + コンパイル時 | デバイス対応可否 + AF 正当性 | lbuild prepare() + C++ `connect<>()` テンプレート |
| **Mbed OS** | ビルド時 | 継承パターン・機能宣言 | Python linting ルール |
| **PlatformIO** | なし | -- | -- (人間レビュー) |
| **Arduino** | なし | -- | -- (人間レビュー) |
| **ESP-IDF** | ビルド時 | Kconfig シンボルの範囲 | Kconfig `range`, `depends on`, `select` |
| **Rust** | コンパイル時 | 型の一致・所有権 | 型システム + 所有権システム |
| **CMSIS-Pack** | ツール依存 | XML スキーマ準拠 | XSD スキーマ検証 |

### 7.2 バリデーションの階層

検証は以下の 3 階層で分類される:

**階層 1: スキーマ検証** (フォーマットレベル)

必須フィールドの存在、値の型、enum 範囲のチェック。

- Zephyr: DTS binding YAML で宣言的に定義
- CMSIS-Pack: XSD スキーマで XML 構造を検証
- UMI: Lua 関数で `board.clock.hse` の存在・型チェックが可能

**階層 2: セマンティクス検証** (意味レベル)

PLL 係数が解を持つか、指定ペリフェラルが MCU に存在するか、ピン割り当てが重複しないかのチェック。

- modm: `prepare()` でデバイスドライバの有無を検証
- Zephyr: DTS の `compatible` プロパティでドライバマッチングを検証
- UMI: `validate_clock()`, `validate_peripherals()`, `validate_pins()` で実装

**階層 3: ハードウェア整合性検証** (物理レベル)

AF 番号が実際に当該ペリフェラル機能に対応するか、クロック周波数が MCU の最大値以下かのチェック。

- modm: `connect<GpioA9::Tx>()` がコンパイル時に AF テーブルを参照
- Rust: 型システムにより誤った AF の指定がコンパイルエラー
- UMI: MCU 定義の `clock.sysclk_max` / `pll` 制約で PLL バリデーション

### 7.3 バリデーション能力の総合評価

| フレームワーク | スキーマ検証 | セマンティクス検証 | HW 整合性検証 | 総合 |
|--------------|:----------:|:-----------------:|:-------------:|:----:|
| **Zephyr** | ◎ | ◎ | ○ | ◎ |
| **modm** | ○ | ◎ | ◎ | ◎ |
| **Rust** | -- | ○ | ◎ | ○ |
| **Mbed OS** | ○ | ○ | -- | ○ |
| **ESP-IDF** | ○ | △ | -- | △ |
| **PlatformIO** | -- | -- | -- | -- |
| **Arduino** | -- | -- | -- | -- |

---

## 8. MCU ファミリー間の設定差異への対応

MCU ファミリーが異なると、ハードウェアの基本構造が根本的に異なる。統一的なスキーマでこれらの差異をどう吸収するかが設計上の重大な課題である。

### 8.1 主要な差異

| 差異 | STM32F4 | nRF52840 | ESP32 | RP2040 |
|------|---------|----------|-------|--------|
| クロック | PLL (M/N/P/Q) | 内部 64MHz | RTC + PLL | Ring Osc + PLL |
| 割り込み | NVIC (Cortex-M) | NVIC (Cortex-M) | 独自 (Xtensa) | NVIC (Cortex-M0+) |
| フラッシュ | 内蔵 (0x08000000) | 内蔵 (0x00000000) | 外部 SPI Flash (XIP) | 外部 QSPI Flash (XIP) |
| GPIO AF | AF テーブル (0-15) | PSEL レジスタ | GPIO Matrix | PIO + Function Select |
| コア数 | シングル | シングル | デュアル (PRO + APP) | デュアル (Cortex-M0+) |
| 特殊機能 | CCM-RAM, FPU | BLE SoftDevice | WiFi/BLE coex | PIO (プログラマブル IO) |

### 8.2 フレームワーク別の差異吸収方式

**Zephyr:**

DTS binding の多態性で吸収する。各 MCU ファミリーが固有の binding YAML を持ち、同じ `compatible` プロパティで異なる実装を選択する:

```dts
/* STM32F4: PLL ノードあり */
&pll { div-m = <8>; mul-n = <336>; div-p = <2>; };

/* nRF52: PLL ノードなし。clock ノードの構造が根本的に異なる */
&clock { hfclk-div = <1>; };
```

**modm:**

デバイスデータの `driver` 属性の `name` フィールドで IP バージョンを識別し、対応するテンプレートを選択する:

```xml
<!-- STM32F4 -->
<driver type="gpio" name="stm32-f2f4"/>
<!-- nRF52 -->
<driver type="gpio" name="nrf52"/>
```

ファミリー固有のテンプレートが生成されるため、統一スキーマの必要がない。

**ESP-IDF:**

`soc_caps.h` で能力を宣言し、`#if SOC_UART_NUM > 0` で条件分岐する。ファミリー間差異は `components/soc/<chip>/` のディレクトリ切り替えで吸収する。

**Rust:**

PAC クレートがファミリーごとに独立しているため、統一スキーマという概念自体がない。`embedded-hal` トレイトが共通インターフェースとなり、実装はクレートごとに完全に異なる。

### 8.3 統一 vs 分岐の設計判断

| アプローチ | 利点 | 欠点 | 採用例 |
|-----------|------|------|--------|
| 完全統一スキーマ | 1 つのパーサーで全 MCU を扱える | 最小公倍数問題、無意味なフィールド多数 | なし (どのフレームワークも不採用) |
| 共通スキーマ + ファミリー固有拡張 | 共通部分は統一、差異は拡張で吸収 | 拡張の定義が煩雑 | Zephyr, CMSIS-Pack |
| ファミリー別スキーマ | 各ファミリーに最適化 | コードの重複 | modm, Rust, libopencm3 |

**全てのフレームワークが「完全統一スキーマ」を不採用としている。** これは MCU ファミリー間の差異が本質的に大きく、統一することのコストが利点を上回るためである。

---

## 9. UMI への設計示唆

全フレームワークの横断比較から、以下の 7 つの設計決定を根拠付ける。

### 9.1 MCU 定義とボード定義の分離は正しい

| フレームワーク | MCU/ボード分離 | 分離方式 |
|--------------|:------------:|---------|
| Zephyr | ○ | SoC DTSI (disabled) + Board DTS (enable) |
| modm | ○ | modm-devices XML + board module.lb |
| Mbed OS | ○ | inherits チェーン (MCU_STM32F4 → NUCLEO_F411RE) |
| Rust | ○ | PAC/HAL/BSP 独立クレート |
| CMSIS-Pack | ○ | DFP / BSP パック分離 |
| ESP-IDF | △ | soc/<chip>/ ディレクトリ切り替え |
| PlatformIO | -- | board.json に混在 |
| Arduino | -- | boards.txt に混在 |

**結論:** 調査した 8 フレームワークのうち 5 つが明示的に分離し、2 つが暗黙的に分離している。分離しないのは PlatformIO と Arduino のみであり、両者ともスケーラビリティに問題を抱えている。UMI の `database/mcu/*.lua` と `boards/*/board.lua` の分離は正しい。

### 9.2 MCU 定義に PLL 制約範囲を含めるべき

PLL パラメータの制約範囲を MCU 定義に含め、ビルド時にバリデーションを行うアプローチは以下のフレームワークに前例がある:

- **Zephyr:** DTS binding で `div-m: { enum: [2, 3, ..., 63] }` として制約を宣言
- **modm:** modm-devices XML の `<ip max-frequency="168000000"/>` で最大周波数を記録
- **CMSIS-Pack:** PDSC の `Dclock` 属性で最大クロックを記述

UMI の MCU 定義における `clock.pll` セクション:

```lua
pll = {
    m_range = { min = 2, max = 63 },
    n_range = { min = 50, max = 432 },
    p_values = { 2, 4, 6, 8 },
    q_range = { min = 2, max = 15 },
    vco_range = { min = 100000000, max = 432000000 },
    input_range = { min = 1000000, max = 2000000 },
},
```

これにより、board.lua で指定された HSE + 目標 sysclk から PLL 係数を自動探索し、制約違反をビルド時に検出できる。

### 9.3 ピン定義は port + pin + AF の 3 値で十分

| フレームワーク | ピン表現 | 本質的な情報量 |
|--------------|---------|:------------:|
| Zephyr | `STM32_PINMUX('A', 9, AF7)` | port + pin + AF |
| modm | `GpioA9::Tx` (AF7 を内部で解決) | port + pin (+ AF 暗黙) |
| Mbed OS | `PA_9 = 0x09` | port + pin |
| Rust | `gpioa.pa9.into_alternate::<7>()` | port + pin + AF |

全てのフレームワークが port + pin (+ AF) の 3 値に帰着する。UMI の board.lua における表現:

```lua
tx = { port = "A", pin = 9, af = 7 },
```

は最もプリミティブかつ十分な表現であり、ここから GPIO 初期化コード・AF 設定コード・コンフリクト検出の全てを導出可能である。

### 9.4 クロック設定は HSE + 目標周波数で十分

| フレームワーク | ユーザーが指定する値 | PLL 係数の導出 |
|--------------|-------------------|:------------:|
| Zephyr | div-m, mul-n, div-p, div-q (手動計算) | ユーザー責任 |
| modm | PllFactors{M, N, P, Q} (手動計算) | ユーザー責任 |
| ESP-IDF | CPU 周波数 (Kconfig) | ドライバ内部 |
| Mbed OS | clock_source | HAL 内部 |
| **UMI** | **HSE + 目標 sysclk** | **ビルド時自動探索** |

Zephyr と modm ではユーザーが PLL 係数を手動計算する必要がある。UMI は HSE 周波数と目標 sysclk のみを board.lua に記述し、PLL 係数はビルド時に自動探索する。この方式は ESP-IDF の Kconfig 周波数選択に近いが、ビルド時のバリデーションが加わることで正確性が向上する。

### 9.5 ファミリー固有設定はオプショナルセクションで対応

全てのフレームワークが「完全統一スキーマ」を不採用とした事実から、UMI もファミリー固有設定をオプショナルセクションとして扱う:

```lua
-- nRF52840 の board.lua
return {
    mcu = "nrf52840",
    clock = {
        hfclk = "external",      -- PLL なし: hse/sysclk の代わりに hfclk
    },
    -- nRF52 固有: SoftDevice 予約
    nrf = {
        softdevice = "s140",
        softdevice_flash = 0x27000,
        softdevice_ram = 0x2800,
    },
}
```

```lua
-- RP2040 の board.lua
return {
    mcu = "rp2040",
    clock = {
        xosc = 12000000,         -- 外部水晶 (HSE 相当)
        sysclk = 125000000,
    },
    -- RP2040 固有: Boot2 (XIP Flash)
    rp2040 = {
        boot2 = "w25q080",
        flash_size = 2 * 1024 * 1024,
    },
}
```

nRF52 の SoftDevice、RP2040 の boot2、ESP32 のパーティションテーブルは本質的に異なる概念であり、統一することに価値がない。

### 9.6 バリデーションはビルド時が最適

| 検証タイミング | 利点 | 欠点 | 採用例 |
|:------------:|------|------|--------|
| エディタ時 | 即座のフィードバック | スキーマ検証に限定 | Zephyr (DTS binding) |
| **ビルド時** | **セマンティクス検証が可能** | **ビルドが必要** | **Zephyr, modm, UMI** |
| コンパイル時 | HW 整合性検証が可能 | C++ テンプレートの複雑さ | modm, Rust |
| 実行時 | 動的設定に対応 | 組み込みでは遅い | ESP-IDF (一部) |

Zephyr の DTS binding バリデーションと modm の `prepare()` + `connect<>()` が業界のゴールドスタンダードである。UMI はビルド時（xmake `on_config`）に以下を検証する:

1. **スキーマ検証:** 必須フィールドの存在、値の型
2. **PLL 解の存在:** HSE + 目標 sysclk から PLL 係数が導出可能か
3. **ペリフェラル存在:** board.lua の `peripherals` が MCU 定義のサブセットか
4. **ピン重複:** 同一ピンが複数機能に割り当てられていないか

### 9.7 BSP = 設定値の集合体

BSP (Board Support Package) の本質を「ドライバコード」ではなく「設定値の集合体」として捉えるアプローチは、全ての成熟したフレームワークで確認される:

| フレームワーク | BSP の本質 | 設定値の形式 |
|--------------|-----------|------------|
| modm | board.hpp = constexpr 定数 + enable() 呼び出し | C++ constexpr |
| Zephyr | Board DTS = プロパティ値の宣言 | DTS プロパティ |
| PlatformIO | board.json = フラットなデータ | JSON キーバリュー |
| Mbed OS | targets.json エントリ = 継承 + 差分 | JSON |
| ESP-IDF | sdkconfig = 選択された設定値 | Kconfig 値 |
| **UMI** | **board.lua = Lua テーブル** | **Lua データ** |

BSP は「どのドライバを使うか」「どのピンをどう接続するか」「クロックをいくつにするか」という**選択と設定**であり、ドライバ実装そのものではない。UMI の board.lua はこの原則を忠実に体現する。

---

## 10. modm との詳細比較（UMI 最近縁）

modm は UMI が最も参考にすべきフレームワークである。データ駆動型コード生成と C++ テンプレートメタプログラミングの組み合わせは、UMI の Lua データベース + C++23 concepts と同じ設計空間に位置する。

### 10.1 アーキテクチャ比較

| 観点 | modm | UMI (proposed) |
|------|------|----------------|
| **ボード定義フォーマット** | Python module.lb + C++ board.hpp | Lua board.lua |
| **MCU データベース** | modm-devices XML (4,557+ デバイス、ベンダーデータから自動抽出) | Lua テーブル (手動作成、必要なデバイスのみ) |
| **クロック設定** | C++ `Rcc::enablePll(PllFactors{M,N,P,Q})` (手動計算) | board.lua の `clock.hse` + `clock.sysclk` (自動計算) |
| **ピン定義** | C++ `GpioA9::Tx` 型エイリアス + `connect<>()` | board.lua `{ port = "A", pin = 9, af = 7 }` |
| **コード生成** | Jinja2 テンプレート (startup, vectors, linker, GPIO, UART 全て) | xmake Lua (memory.ld, board.hh, clock_config.hh) |
| **バリデーション** | lbuild `prepare()` (デバイス対応可否) + `connect<>()` (AF 正当性) | xmake `on_config()` (PLL, ペリフェラル, ピン) |
| **生成ツール** | lbuild + Jinja2 (Python) | xmake Lua (追加ツール不要) |
| **ボード継承** | `<extends>` タグ | Lua `dofile()` チェーン |
| **ペリフェラル選択** | モジュール単位で選択的包含 | xmake パッケージ依存解決 |

### 10.2 modm の優位性

1. **デバイスカバレッジ:** 4,557+ デバイスのデータベースは圧倒的。CubeMX XML, ATDF, CMSIS-Pack PDSC からの自動抽出パイプラインにより、手動作業なしで全デバイスをカバー

2. **コンパイル時 AF 検証:** `connect<GpioA9::Tx>()` はテンプレートメタプログラミングにより AF テーブルをコンパイル時に参照し、不正な AF 割り当てをコンパイルエラーにする

3. **完全コード生成:** startup, vectors, linker script, GPIO, UART 等の全てがテンプレートから生成される。手書きファイルはユーザーコードのみ

4. **IP バージョンベースのコード共有:** ファミリーではなくペリフェラル IP バージョンでコード共有単位を決定する先進的設計

### 10.3 UMI の差別化ポイント

1. **PLL 自動計算:** modm はユーザーが PLL 係数を手動計算するが、UMI は HSE + 目標周波数から自動探索する。ユーザー体験が向上

2. **追加ツール不要:** modm は lbuild (Python) + Jinja2 が必須だが、UMI は xmake の Lua スクリプトのみで完結する。依存関係が大幅に削減

3. **C++23 concepts:** modm は C++17 テンプレートメタプログラミングだが、UMI は C++23 concepts で型制約を宣言的に記述する。可読性とエラーメッセージの品質が向上

4. **オーディオ特化:** modm は汎用組み込みフレームワークだが、UMI はオーディオ処理に特化した設計（リアルタイム制約、DMA バッファ配置、サンプル精度イベント処理）を持つ

5. **軽量 MCU データベース:** modm-devices の 4,557+ デバイスは大半のユーザーにとって過剰。UMI は必要なデバイスのみを Lua テーブルとして手動定義する軽量アプローチにより、データベースの保守コストを最小化

### 10.4 modm から学ぶべき設計

| 設計要素 | modm の実装 | UMI への応用 |
|---------|-----------|-------------|
| 単一源泉 | modm-devices が全ての生成の唯一のソース | `database/mcu/*.lua` を唯一のソースとする |
| 選択的包含 | lbuild モジュールで必要なコードのみ生成 | xmake パッケージ依存で必要なライブラリのみリンク |
| IP ベース共有 | `stm32-f2f4`, `stm32-extended` で IP バージョン別 | `src/arm/cortex-m/` 内で IP バージョン別ディレクトリ |
| ボード継承 | `<extends>` で既存ボード設定を継承 | Lua `dofile()` で family → MCU のチェーン |
| AF 検証 | `connect<>()` テンプレートでコンパイル時検証 | xmake `on_config()` でビルド時検証 + `static_assert` |

---

## 11. 参考文献

### 個別フレームワーク調査

| 文書 | 内容 |
|------|------|
| research/zephyr.md | DeviceTree + Kconfig + CMake 三重構成、HWMv2 ボードモデル |
| research/modm.md | modm-devices データベース、lbuild モジュールシステム、Jinja2 コード生成 |
| research/mbed_os.md | targets.json 継承チェーン、`_add`/`_remove` 差分管理 |
| research/platformio.md | board.json フラット定義、継承なし、2 リージョン限定リンカ生成 |
| research/arduino.md | boards.txt フラットプロパティ、variant/pins_arduino.h |
| research/esp_idf.md | soc_caps.h 単一源泉、4 層抽象化、ldgen フラグメント |
| research/rust_embedded.md | PAC/HAL/BSP クレート分離、embedded-hal 1.0、memory.x |
| research/cmsis_pack.md | 4 階層デバイスヒエラルキー、DFP/BSP パック分離 |
| research/other_frameworks.md | NuttX, libopencm3, ビルドシステム比較 |

### 設計文書

| 文書 | 内容 |
|------|------|
| [../foundations/comparative_analysis.md](../foundations/comparative_analysis.md) | BSP アーキタイプ分類、HAL 比較、設計レビュー合意事項 |
| [../foundations/architecture.md](../foundations/architecture.md) | UMI の理想アーキテクチャ提案 |
| [config.md](config.md) | board.lua + mcu/*.lua アーキテクチャの詳細設計 |
