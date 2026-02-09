# 06. RAL (Register Access Layer) アーキテクチャ提案

> **移動済み**: 本ドキュメントは [../pal/03_ARCHITECTURE.md](../pal/03_ARCHITECTURE.md) に移動しました。
> RAL → PAL (Peripheral Access Layer) に命名変更済み。

**ステータス:** 設計中
**関連文書:**
- [../pal/03_ARCHITECTURE.md](../pal/03_ARCHITECTURE.md) — PAL アーキテクチャ提案 (最新版)
- [../pal/04_ANALYSIS.md](../pal/04_ANALYSIS.md) — 既存 PAL アプローチの横断分析
- umimmio/docs/DESIGN.md — 現行 umimmio 設計

---

## 1. 本ドキュメントの目的

umimmio（MMIO フレームワーク）の上に MCU 固有のレジスタ定義を**どう構成・生成・配置するか**を提案する。

---

## 2. 前提の整理

### 2.1 umimmio と RAL の責務分離

```
umimmio (MMIO フレームワーク)          RAL (MCU 固有レジスタ定義)
─────────────────────────          ──────────────────────────
Device, Block, Register, Field      「GPIOA は 0x40020000」
RegOps (read/write/modify)          「MODER は offset 0x00, 32bit」
ByteAdapter (I2C/SPI ブリッジ)       「MODER13 は bit 26-27」
Transport concepts                  「0b01 = 出力モード」
Access policies (RW/RO/WO)          「AHB1ENR は RW, offset 0x30」
Error policies                      リセット値、列挙値

汎用・MCU 非依存                     MCU 固有・型番依存
```

RAL は umimmio の型テンプレートに **具体的な MCU データを流し込んでインスタンス化する** 層である。

### 2.2 RAL が定義すべき情報

各ペリフェラルについて:

1. **ベースアドレス** — `0x40020000`
2. **レジスタオフセットとビット幅** — `MODER @ +0x00, 32bit`
3. **フィールドのビット位置と幅** — `MODER13: bit[27:26]`
4. **アクセスポリシー** — `RW`, `RO`, `WO`
5. **リセット値** — `0x00000000`
6. **列挙値（可能な範囲で）** — `Input=0, Output=1, Alternate=2, Analog=3`
7. **Transport 制約** — 内蔵ペリフェラルは `DirectTransportTag` のみ

---

## 3. 設計判断

### 3.1 判断 1: 段階的アプローチ（手書き先行 → 生成移行）

**根拠:** 過去の umi_mmio プロジェクトで svd2ral v1/v2 が実装済みであり、SVD パーサー・バリデータ・コード生成器は動作実績がある。ただし旧 mmio.hh API を前提としているため、現行 umimmio API に適合させるリファクタリングが必要。

**方針:**

| フェーズ | 内容 | 成果物 |
|---------|------|--------|
| **Phase 1** (現在) | 使用するペリフェラルのみ手書きで RAL を定義 | GPIO, RCC, USART, I2C, SPI, Timer の最小セット |
| **Phase 2** | 既存 svd2ral を umimmio API 向けにリファクタリング | パッチ済み SVD → umimmio 型ベースの C++ ヘッダ生成 |
| **Phase 3** | 全ペリフェラルの自動生成 + 手書きとの共存 | MCU 追加が低コストに |

Phase 1 の手書き定義は Phase 2-3 の生成コードと **同じ型・同じ構造** を使う。生成に移行しても利用側コードの変更は不要。

**既存資産（`/Users/tekitou/work/umi_mmio/.archive/tools/`）:**

Phase 2 で再利用可能なコンポーネント:
- SVD パーサー（derivedFrom 解決、レジスタ配列展開）
- バリデータ（アドレス重複、フィールド重複、列挙値範囲チェック）
- ペリフェラルグループ自動検出 → テンプレート化（v2）
- 配列レジスタ・フィールド配列の自動検出
- バックエンド自動選択（アドレス範囲によるDirect/I2C/SPI判定）
- YAML 設定スキーマ（出力モード、フィルタ、バックエンドマッピング）
- 命名規則変換・C++ 予約語エスケープ

### 3.2 判断 2: 配置場所 — umiport 内の ral/ サブディレクトリ

**根拠:** RAL の消費者は umiport 内のペリフェラルドライバであり、アプリケーションが直接参照することは稀。独立パッケージにするメリットよりも、umiport との近接性の方が重要。

```
lib/umiport/
├── include/umiport/
│   ├── mcu/
│   │   └── stm32f4/
│   │       ├── ral/                    ← RAL 定義
│   │       │   ├── gpio.hh             ← GPIO ペリフェラル定義
│   │       │   ├── rcc.hh              ← RCC ペリフェラル定義
│   │       │   ├── usart.hh            ← USART ペリフェラル定義
│   │       │   ├── i2c.hh              ← I2C ペリフェラル定義
│   │       │   ├── spi.hh              ← SPI ペリフェラル定義
│   │       │   ├── timer.hh            ← Timer ペリフェラル定義
│   │       │   └── common.hh           ← MCU 共通定義（ベースアドレスマップ等）
│   │       ├── gpio_driver.hh          ← Peripheral Driver (HAL 実装)
│   │       ├── uart_output.hh
│   │       └── ...
│   └── ...
```

将来 MCU ファミリが増えた場合:

```
lib/umiport/include/umiport/mcu/
├── stm32f4/ral/
├── stm32h7/ral/
├── nrf52/ral/
└── rp2040/ral/
```

### 3.3 判断 3: SVD パッチは stm32-rs 互換フォーマットを採用

**根拠:** stm32-rs コミュニティが数百のパッチを維持しており、このデータを再利用できる。独自パッチフォーマットを作る理由がない。

**パイプライン（Phase 2 以降）:**

```
ベンダー SVD
    │
    ├── stm32-rs YAML パッチ（転用 or 参照）
    │
    ↓
svdtools apply
    │
    ↓
パッチ済み SVD
    │
    ↓
umi-svd2cpp (独自ツール)
    │
    ↓
umimmio 型を使った C++ ヘッダ
```

### 3.4 判断 4: コード生成ツールは Python + Jinja2

**根拠:** modm (lbuild) と同じスタック。過去の svd2ral v1 で Jinja2 テンプレートベースの生成が実装・検証済み。SVD パースには既存の独自パーサー（`cmsis-svd` Python ライブラリも `.ref/` に保持）が使え、テンプレートベースの生成は保守性が高い。xmake のビルドフックから呼び出し可能。

---

## 4. RAL 定義の具体的な形式

### 4.1 ペリフェラル定義の例: GPIO

```cpp
// umiport/include/umiport/mcu/stm32f4/ral/gpio.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::mcu::stm32f4 {

namespace mm = umi::mmio;

/// @brief GPIO ペリフェラル定義
/// @note STM32F4 Reference Manual RM0090 Section 8
struct Gpio : mm::Device<mm::RW, mm::DirectTransportTag> {
    /// @brief Mode Register — 各ピンの I/O 方向を設定
    struct MODER : mm::Register<Gpio, 0x00, mm::bits32, 0, mm::bits32, mm::RW, 0xA800'0000> {
        struct MODER0  : mm::Field<MODER, 0,  2, mm::Inherit, 0> {};
        struct MODER1  : mm::Field<MODER, 2,  2, mm::Inherit, 0> {};
        // ... MODER2-MODER14 ...
        struct MODER15 : mm::Field<MODER, 30, 2, mm::Inherit, 0> {};
    };

    /// @brief Output Type Register
    struct OTYPER : mm::Register<Gpio, 0x04, mm::bits32, 0, mm::bits32, mm::RW, 0> {
        struct OT0  : mm::Field<OTYPER, 0,  1, mm::Inherit, 0> {};
        // ...
        struct OT15 : mm::Field<OTYPER, 15, 1, mm::Inherit, 0> {};
    };

    /// @brief Output Speed Register
    struct OSPEEDR : mm::Register<Gpio, 0x08, mm::bits32, 0, mm::bits32, mm::RW, 0> {};

    /// @brief Pull-up/Pull-down Register
    struct PUPDR : mm::Register<Gpio, 0x0C, mm::bits32, 0, mm::bits32, mm::RW, 0x6400'0000> {};

    /// @brief Input Data Register
    struct IDR : mm::Register<Gpio, 0x10, mm::bits32, 0, mm::bits32, mm::RO, 0> {};

    /// @brief Output Data Register
    struct ODR : mm::Register<Gpio, 0x14, mm::bits32, 0, mm::bits32, mm::RW, 0> {};

    /// @brief Bit Set/Reset Register
    struct BSRR : mm::Register<Gpio, 0x18, mm::bits32, 0, mm::bits32, mm::WO, 0> {};

    /// @brief Alternate Function Low Register (pin 0-7)
    struct AFRL : mm::Register<Gpio, 0x20, mm::bits32, 0, mm::bits32, mm::RW, 0> {};

    /// @brief Alternate Function High Register (pin 8-15)
    struct AFRH : mm::Register<Gpio, 0x24, mm::bits32, 0, mm::bits32, mm::RW, 0> {};
};

/// @brief GPIO Mode 列挙値
enum class GpioMode : std::uint8_t {
    INPUT     = 0b00,
    OUTPUT    = 0b01,
    ALTERNATE = 0b10,
    ANALOG    = 0b11,
};

/// @brief GPIO ペリフェラルインスタンス（ベースアドレス付き）
/// @note Block を使ってベースアドレスを設定
struct GPIOA : mm::Block<Gpio, 0x4002'0000> {};
struct GPIOB : mm::Block<Gpio, 0x4002'0400> {};
struct GPIOC : mm::Block<Gpio, 0x4002'0800> {};
struct GPIOD : mm::Block<Gpio, 0x4002'0C00> {};
struct GPIOE : mm::Block<Gpio, 0x4002'1000> {};
struct GPIOF : mm::Block<Gpio, 0x4002'1400> {};
struct GPIOG : mm::Block<Gpio, 0x4002'1800> {};
struct GPIOH : mm::Block<Gpio, 0x4002'1C00> {};
struct GPIOI : mm::Block<Gpio, 0x4002'2000> {};

} // namespace umi::mcu::stm32f4
```

### 4.2 利用例

```cpp
#include <umiport/mcu/stm32f4/ral/gpio.hh>
#include <umimmio/transport/direct.hh>

using namespace umi::mcu::stm32f4;

// DirectTransport でメモリマップドアクセス
mm::DirectTransport transport;

// PD12 を出力モードに設定
transport.write<GPIOD::MODER>(
    mm::DynamicValue<GPIOD::MODER::MODER12, std::uint32_t>{
        static_cast<std::uint32_t>(GpioMode::OUTPUT)
    }
);

// PD12 を High に設定
transport.write<GPIOD::BSRR>(
    mm::Value<GPIOD::BSRR::BS12, 1>{}
);

// PA0 の入力値を読み取り
auto idr = transport.read<GPIOA::IDR>();
```

### 4.3 ペリフェラル定義と インスタンスの分離

上記の例では `Gpio` がペリフェラル構造（レジスタ配置）を定義し、`GPIOA`〜`GPIOI` が具体的なベースアドレスを持つインスタンスとなる。この分離により:

- 同一 IP ブロックを持つペリフェラル間でレジスタ定義を共有
- SVD の `derivedFrom` に自然に対応
- svd2rust の「コード重複」問題を回避

---

## 5. 列挙値の扱い

### 5.1 方針: enum class + Value テンプレートの二重定義

```cpp
// 1. 人間向けの enum class
enum class GpioMode : std::uint8_t {
    INPUT = 0b00, OUTPUT = 0b01, ALTERNATE = 0b10, ANALOG = 0b11,
};

// 2. umimmio の Value テンプレートによるコンパイル時定数
// Phase 1 では手書き、Phase 2 以降は生成
template <class MODER_FIELD>
struct GpioModeValues {
    static constexpr auto input     = mm::Value<MODER_FIELD, 0b00>{};
    static constexpr auto output    = mm::Value<MODER_FIELD, 0b01>{};
    static constexpr auto alternate = mm::Value<MODER_FIELD, 0b10>{};
    static constexpr auto analog    = mm::Value<MODER_FIELD, 0b11>{};
};
```

### 5.2 SVD に列挙値がない場合

SVD に enumeratedValues が定義されていないフィールドは、コード生成時に:

1. `DynamicValue<Field, T>` による任意値書き込みを許可
2. ユーザーが後から enum を手書き追加できるよう、拡張ポイントを設ける
3. stm32-rs パッチで列挙値を補完した SVD を使用（推奨）

---

## 6. ペリフェラルドライバからの利用パターン

### 6.1 現行の散在パターン（Before）

```cpp
// uart_output.hh 内にペリフェラル定義が埋め込まれている
struct RCC : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x40023800;
    struct AHB1ENR : mm::Register<RCC, 0x30, mm::bits32, 0, mm::bits32, mm::Inherit, 0> {
        struct GPIODEN : mm::Field<AHB1ENR, 3, 1, mm::Inherit, 0> {};
    };
};
```

### 6.2 RAL 分離後のパターン（After）

```cpp
// ドライバは RAL をインクルードするだけ
#include <umiport/mcu/stm32f4/ral/rcc.hh>
#include <umiport/mcu/stm32f4/ral/gpio.hh>
#include <umiport/mcu/stm32f4/ral/usart.hh>

class Stm32f4UartOutput {
    void init() {
        // RAL 定義を直接参照
        transport.write<RCC::AHB1ENR::GPIODEN>(mm::Value<RCC::AHB1ENR::GPIODEN, 1>{});
        transport.write<RCC::APB2ENR::USART1EN>(mm::Value<RCC::APB2ENR::USART1EN, 1>{});
        // ...
    }
};
```

---

## 7. SVD 生成パイプライン（Phase 2 設計）

### 7.1 ツール構成

既存の svd2ral v1/v2（`/Users/tekitou/work/umi_mmio/.archive/tools/`）を umimmio API 向けにリファクタリングして構築する。

```
tools/umi-svd2cpp/
├── svd2cpp.py              ← メインスクリプト（既存 cli.py ベース）
├── parser.py               ← SVD パーサー（既存 parser.py リファクタリング）
├── models.py               ← 中間表現（既存 models.py ベース）
├── validator.py            ← バリデータ（既存 validator.py 流用）
├── codegen/
│   ├── generator.py        ← umimmio 型向けコード生成（既存 generator.py 再設計）
│   ├── naming.py           ← 命名規則変換（既存 naming.py 流用）
│   └── backends.py         ← バックエンド選択（既存 backends.py 流用）
├── templates/
│   ├── peripheral.hh.j2    ← ペリフェラルヘッダテンプレート（umimmio API 向け新規）
│   ├── register.hh.j2      ← レジスタ定義テンプレート
│   └── instance.hh.j2      ← インスタンス（ベースアドレス）テンプレート
├── config/
│   ├── schema.py           ← 設定スキーマ（既存 schema.py ベース）
│   └── loader.py           ← 設定ローダー（既存 loader.py 流用）
├── patches/
│   └── stm32f4/            ← stm32-rs 互換パッチ
│       ├── stm32f407.yaml
│       └── ...
└── requirements.txt        ← svdtools, jinja2
```

### 7.2 生成フロー

```
vendor/STM32F407.svd
    │
    ↓ svdtools patch (stm32-rs 互換 YAML)
    │
patches/stm32f4/stm32f407.yaml
    │
    ↓ umi-svd2cpp.py --target stm32f407 --output lib/umiport/include/umiport/mcu/stm32f4/ral/
    │
    ↓ Jinja2 テンプレート処理
    │
生成物:
  ├── gpio.hh       ← Gpio struct + GPIOA..GPIOI instances
  ├── rcc.hh        ← Rcc struct + RCC instance
  ├── usart.hh      ← Usart struct + USART1..USART6 instances
  └── ...
```

### 7.3 テンプレート例

```jinja2
{# peripheral.hh.j2 #}
// Auto-generated from {{ svd_file }} — DO NOT EDIT
#pragma once
#include <umimmio/register.hh>

namespace umi::mcu::{{ family }} {

namespace mm = umi::mmio;

/// @brief {{ peripheral.description }}
struct {{ peripheral.name | capitalize }} : mm::Device<mm::RW, mm::DirectTransportTag> {
{% for reg in peripheral.registers %}
    /// @brief {{ reg.description }}
    struct {{ reg.name }} : mm::Register<{{ peripheral.name | capitalize }}, {{ "0x%04X" | format(reg.offset) }}, mm::bits{{ reg.size }}, 0, mm::bits{{ reg.size }}, mm::{{ reg.access }}, {{ "0x%08X" | format(reg.reset) }}> {
{% for field in reg.fields %}
        struct {{ field.name }} : mm::Field<{{ reg.name }}, {{ field.offset }}, {{ field.width }}, mm::{{ field.access | default("Inherit") }}, 0> {};
{% endfor %}
    };
{% endfor %}
};

{% for instance in peripheral.instances %}
struct {{ instance.name }} : mm::Block<{{ peripheral.name | capitalize }}, {{ "0x%08X" | format(instance.base) }}> {};
{% endfor %}

} // namespace umi::mcu::{{ family }}
```

### 7.4 xmake 統合

```lua
-- xmake.lua 内
rule("umiport.ral-generate")
    before_build(function (target)
        local svd = target:values("umiport.svd")
        local patches = target:values("umiport.svd_patches")
        if svd then
            os.execv("python3", {
                "tools/umi-svd2cpp/svd2cpp.py",
                "--svd", svd,
                "--patches", patches,
                "--output", target:autogendir() .. "/ral/"
            })
        end
    end)
```

---

## 8. 外部デバイスの RAL

### 8.1 MCU 内蔵ペリフェラルとの違い

外部デバイス（I2C/SPI 接続のセンサ、DAC、CODEC 等）の RAL は:

- Transport が `I2CTransportTag` / `SPITransportTag`
- ベースアドレスはデバイスアドレス（I2C: 7-bit addr、SPI: CS ピン）
- レジスタは SVD ではなく**デバイスのデータシート**から手書き
- `umidevice/` に配置

### 8.2 例: CS43L22 (I2C Audio DAC)

```cpp
// umidevice/include/umidevice/cs43l22/ral.hh
namespace umi::device::cs43l22 {

namespace mm = umi::mmio;

struct Cs43l22 : mm::Device<mm::RW, mm::I2CTransportTag> {
    struct ID : mm::Register<Cs43l22, 0x01, mm::bits8, 0, mm::bits8, mm::RO, 0> {
        struct REVID : mm::Field<ID, 0, 3, mm::Inherit, 0> {};
        struct DEVID : mm::Field<ID, 3, 5, mm::Inherit, 0> {};
    };

    struct POWER_CTL1 : mm::Register<Cs43l22, 0x02, mm::bits8, 0, mm::bits8, mm::RW, 0x01> {
        struct PDN : mm::Field<POWER_CTL1, 0, 1, mm::Inherit, 1> {};
    };

    // ...
};

} // namespace umi::device::cs43l22
```

この構造は MCU 内蔵ペリフェラルの RAL と **完全に同じ型システム** を使う。Transport の違いは `Device` テンプレートの `AllowedTransports` パラメータで表現される。

---

## 9. ファミリ間共有と derivedFrom 対応

### 9.1 同一 IP ブロックの共有

STM32F4 と STM32F7 は多くのペリフェラル IP を共有する。RAL 定義の重複を避けるため:

```
lib/umiport/include/umiport/mcu/
├── stm32_common/ral/
│   ├── gpio_v2.hh          ← STM32F4/F7/L4 共通 GPIO IP
│   └── usart_v2.hh         ← STM32F4/F7 共通 USART IP
├── stm32f4/ral/
│   ├── gpio.hh             ← #include "../stm32_common/ral/gpio_v2.hh" + インスタンス定義
│   ├── rcc.hh              ← F4 固有（RCC は MCU ファミリごとに異なる）
│   └── ...
└── stm32h7/ral/
    ├── gpio.hh             ← #include "../stm32_common/ral/gpio_v2.hh" + インスタンス定義
    ├── rcc.hh              ← H7 固有
    └── ...
```

### 9.2 同一ファミリ内の derivedFrom

SVD の `derivedFrom` は umimmio の `Block` テンプレートで自然に表現される:

```cpp
// GPIOB は GPIOA と同じレジスタ構造、ベースアドレスのみ異なる
struct Gpio : mm::Device<mm::RW, mm::DirectTransportTag> { /* レジスタ定義 */ };
struct GPIOA : mm::Block<Gpio, 0x4002'0000> {};
struct GPIOB : mm::Block<Gpio, 0x4002'0400> {};  // derivedFrom="GPIOA"
```

---

## 10. 未解決の論点

### 10.1 列挙値の網羅的な生成

SVD に列挙値が存在しないフィールドが大半。Phase 2 でパッチを適用するとしても、全フィールドに列挙値を付与することは現実的でない。`DynamicValue` による任意値書き込みと、重要フィールドへの手動列挙値追加のバランスをどうとるか。

### 10.2 RCC の MCU ファミリ差異

RCC（クロック制御）は MCU ファミリごとに構造が大きく異なり、共有が困難。Phase 1 では各 MCU ファミリごとに手書きする。

### 10.3 レジスタ配列 (dim/dimIncrement)

SVD の `dim` による配列レジスタ（例: DMA チャネル 0-7）の umimmio での表現方法。C++ テンプレートの非型パラメータでインデックスを渡す設計が候補。既存 svd2ral v2 では `dim/dimIncrement` の自動展開が実装済みであり、このロジックを流用可能。

### 10.4 生成コードと手書きコードの共存ルール

Phase 2 以降、自動生成ファイルと手書き拡張ファイルが混在する。`// Auto-generated — DO NOT EDIT` マーカーによる区別と、手書き拡張用の別ヘッダ（`gpio_ext.hh`）を用意するパターンが候補。

### 10.5 コンパイル時間への影響

svd2rust の教訓から、MCU 全ペリフェラルを一つのヘッダに生成するとコンパイル時間が爆発する。ペリフェラルごとに個別ヘッダを生成し、必要なものだけ include する現在の方針を維持。

---

## 11. ロードマップ

| フェーズ | 内容 | 判断基準 |
|---------|------|---------|
| **Phase 1** | STM32F4 の GPIO, RCC, USART, I2C, SPI, Timer を手書き | 現行 umiport ドライバが RAL を参照する形にリファクタリングできれば完了 |
| **Phase 2** | 既存 svd2ral のリファクタリング + umimmio API 向け再設計。STM32F4 で検証 | 生成コードと手書きコードが同一の利用体験を提供できれば完了 |
| **Phase 3** | 複数 MCU ファミリへの展開 (STM32H7, nRF52 等) | 新 MCU 追加が「SVD + パッチ + 生成」のみで完結すれば完了 |

---

## 12. 参考文献

本提案は [06a_RAL_ANALYSIS.md](06a_RAL_ANALYSIS.md) の横断分析に基づく。特に以下のアプローチから設計要素を採用:

| 採用元 | 採用した要素 |
|--------|------------|
| **svd2rust** | クロージャ的な型安全 Write API の概念 |
| **chiptool** | ペリフェラル構造とインスタンスの分離（Fieldset 的アプローチ） |
| **modm** | Python + Jinja2 による生成スタック、ペリフェラルごとの個別ヘッダ |
| **stm32-rs** | YAML パッチフォーマットの互換性 |
| **mmio.hh (umi_mmio)** | Transport 統一設計（umimmio に継承済み）、svd2ral v1/v2 のパーサー・バリデータ・生成器（リファクタリングして再利用） |
| **Kvasir** | 型安全な列挙値による操作の概念 |
