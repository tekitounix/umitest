# 06a. RAL (Register Access Layer) 横断分析

> **移動済み**: 本ドキュメントは [../pal/04_ANALYSIS.md](../pal/04_ANALYSIS.md) に移動しました。
> RAL → PAL (Peripheral Access Layer) に命名変更済み。

**ステータス:** 分析完了
**関連文書:**
- [../pal/03_ARCHITECTURE.md](../pal/03_ARCHITECTURE.md) — PAL アーキテクチャ提案 (最新版)
- [../pal/04_ANALYSIS.md](../pal/04_ANALYSIS.md) — 既存 PAL アプローチの横断分析 (最新版)
- umimmio/docs/DESIGN.md — 現行 umimmio 設計

---

## 1. 本ドキュメントの目的

MCU 固有のレジスタ定義（アドレス・フィールド・アクセスポリシー・列挙値）を **どのように定義し、どこに配置し、どう生成するか** を決定するために、以下を横断的に分析する:

1. ユーザーの過去の試み（`/Users/tekitou/work/umi_mmio/`）
2. Rust エコシステム（svd2rust, chiptool, PAC パターン）
3. modm（modm-devices, lbuild, GPIO connect<>）
4. その他の C++ RAL（Kvasir, regbits, svd2cpp, CMSIS, STM32 LL）
5. 現行の umimmio

---

## 2. 用語定義

| 用語 | 定義 |
|------|------|
| **RAL** | Register Access Layer — MCU 固有のレジスタアドレス・フィールド・列挙値の定義 |
| **MMIO フレームワーク** | レジスタ操作の汎用基盤（read/write/modify, アクセスポリシー, Transport 抽象）|
| **PAC** | Peripheral Access Crate — Rust における RAL の呼称 |
| **SVD** | System View Description — ARM CMSIS のレジスタ記述 XML フォーマット |

**umimmio は MMIO フレームワークであり、RAL ではない。** umimmio は `Device`, `Block`, `Register`, `Field` の汎用テンプレートを提供するが、「STM32F407 の GPIOA は 0x40020000 にある」という MCU 固有情報は含まない。RAL はこの MCU 固有情報の層である。

---

## 3. 過去の試み: mmio.hh

### 3.1 概要

`/Users/tekitou/work/umi_mmio/` に存在する C++23 の MMIO ライブラリ。4 層アーキテクチャ（RAL/PAL/Impl/BRD）を設計し、SVD からの C++ コード生成ツール (`svd2ral`) を実装した。v1（Jinja2 テンプレートベース）と v2（直接コード生成）の 2 世代が存在し、STM32F407 の全ペリフェラルを含む生成出力が残っている。

### 3.2 レジスタ定義方式

```cpp
struct GpioC {
    static constexpr uint32_t base_address = 0x40020800;
    struct Moder {
        static constexpr uint32_t address = base_address + 0x00;
        struct Moder13 {
            static constexpr uint32_t position = 26;
            static constexpr uint32_t width = 2;
            static constexpr auto input  = mmio::Value<Moder13, 0>{};
            static constexpr auto output = mmio::Value<Moder13, 1>{};
        };
    };
};
```

### 3.3 コア型

| 型 | 役割 |
|----|------|
| `AccessPolicy<CanRead, CanWrite>` | アクセス制御 |
| `FieldValue<Peripheral, Register, Pos, Width, Val, AccessType, Policy>` | フィールド値の完全修飾表現 |
| `Value<Field, Val>` | 簡略化エイリアス |
| `CombineFields<Fields...>` | 複数フィールドの結合 |
| グローバル関数: `write()`, `modify()`, `is()`, `read()`, `reset()` | レジスタ操作 |

### 3.4 所有権システム

Rust のオーナーシップモデルを C++ で再現しようとした:
- `singleton::take<Peripheral>()` — シングルトンペリフェラル取得
- `unsafe<Peripheral>()` — 安全性チェックの回避
- RAII トークン管理

**結論:** C++ の型システムでは翻訳単位を跨いだ borrow checking は実現不可能。`atomic<bool>` によるランタイムチェックに帰着。

### 3.5 SVD → C++ コード生成ツールチェーン

SVD パッチから C++ コード生成までの完全なパイプラインが実装されている（`.archive/tools/` 配下）。

#### svd2ral v1 (Jinja2 テンプレートベース)

モジュール構成:
- `cli.py` — CLI インターフェース（`python -m svd2ral input.svd -o ral/`）
- `parser.py` — SVD XML パーサー（derivedFrom 解決、bitRange/lsb-msb 全パターン対応）
- `models.py` — 中間表現（Device → Peripheral → Register → Field → EnumValue）
- `validator.py` — 多層バリデーション（アドレス重複、フィールド重複、列挙値範囲チェック）
- `codegen/generator.py` — Jinja2 テンプレートエンジンによるコード生成
- `codegen/backends.py` — バックエンド自動選択（アドレス範囲によるDirect/I2C/SPI判定）
- `codegen/naming.py` — C++ 命名規則変換（snake_case/PascalCase、予約語エスケープ）
- `config/schema.py` — YAML 設定スキーマ（出力モード、ペリフェラルフィルタ、バックエンドマッピング）

機能:
- 3 つの出力モード: `single_file`, `grouped`（ペリフェラルタイプ別）, `individual`
- 配列レジスタパターンの自動検出（3 個以上の連続レジスタ → テンプレート化）
- ペリフェラルフィルタリング（glob パターン対応: `GPIO*`, `USART*`）
- 統合テスト・CLI テスト・設定テスト完備

#### svd2ral v2 (直接コード生成)

v1 を再設計し、以下を追加:
- `parser.py` — ペリフェラルグループの自動検出、`dim/dimIncrement` によるレジスタ配列展開
- `generator.py` — テンプレート不使用の直接コード生成。ペリフェラルグループのテンプレート化（同一 IP ブロックの共有）
- `models.py` — フィールド配列検出（`is_array`, `array_pattern`）、WriteOnce/ReadWriteOnce 対応

v2 の生成コード形式:
```cpp
template<typename Backend = mmio::backend::Direct>
class STM32F407 {
    MMIO_RAL_ALIASES(Backend, uint32_t)
private:
    template<uint32_t Addr> struct TIM : Peripheral<Addr> {
        struct CR1 : Register<TIM, 0x0> {
            using CKD = Field<CR1, 8, 2>;
            using CEN = Field<CR1, 0>;
        };
    };
public:
    using TIM2 = TIM<0x40000000>;
    using TIM3 = TIM<0x40000400>;
};
```

#### 補助ツール群

- `patch_svd.py` — STM32F407 の GPIO 列挙値を SVD に追加するパッチスクリプト
- `cmsis2svd.py` — CMSIS ヘッダ（`stm32f407xx.h`）から SVD への変換ツール
- `validate_svd.py` — XML Schema によるSVD バリデーション
- `device-yaml2cpp.py` — YAML デバイス定義から C++ メモリレイアウトヘッダ生成

#### 生成済み出力

`.archive/generated/ral/` に STM32F407 の生成結果が残存:
- `stm32f407.hh` — v1 による全ペリフェラル生成出力
- `stm32f407_v2.hh` — v2 によるテンプレート化された生成出力
- `cortex_m4.hh` — Cortex-M4 コアレジスタ

#### 参照データ

- `.ref/stm32-rs/` — Rust SVD ツール・パッチ群
- `.ref/cmsis-svd/` — CMSIS SVD スキーマ・Python パーサー
- `.ref/STM32CubeF4/` — STM32 HAL/CMSIS ヘッダ

#### 移行に際しての考察

v1/v2 とも mmio.hh の旧 API（`mmio::BitField<>`, `mmio::MultiField<>`）を前提としているため、現行 umimmio の API（`Device`, `Block`, `Register`, `Field`）に合わせるにはテンプレートの再設計が必要。ただし、以下のコンポーネントは再利用価値が高い:
1. SVD パーサー（Python）— derivedFrom 解決、バリデーション
2. 配列レジスタ・フィールド配列の自動検出ロジック
3. ペリフェラルグループの自動テンプレート化（v2）
4. バックエンド自動選択（アドレス範囲判定）
5. 命名規則変換（C++ 予約語チェック含む）

### 3.6 評価

| 軸 | 評価 |
|----|------|
| レジスタ型の設計 | 良い — 型安全、ゼロコスト |
| Transport 抽象 | 先進的 — Direct/I2C/SPI/Simulator の統一 |
| SVD 統合 | **実装済み** — svd2ral v1/v2 で STM32F407 全ペリフェラル生成成功。現行 umimmio API への適合は未実施 |
| 所有権システム | 野心的だが C++ では限界あり |
| umimmio への継承 | Transport + BitRegion の設計思想が引き継がれた |

---

## 4. svd2rust / Rust PAC パターン

### 4.1 パイプライン

```
SVD XML → [svdtools YAML パッチ] → パッチ済み SVD → [svd2rust] → Rust PAC
```

### 4.2 生成コード構造

```
Peripherals::take() → Option<Peripherals>   (Singleton)
  └── GPIOA → RegisterBlock (repr(C) struct)
        ├── moder() → &Reg<MODER_SPEC>
        │     ├── R (Read proxy) → moder0() → MODER0_R
        │     │     └── variant() → MODER0_A (enum)
        │     └── W (Write proxy) → moder0() → MODER0_W
        │           └── output() → &mut W
        └── odr() → &Reg<ODR_SPEC>
```

### 4.3 操作パターン

```rust
// Read
let pin13 = dp.GPIOC.idr.read().idr13().bit();

// Write (リセット値 + 指定値)
dp.GPIOA.odr.write(|w| w.odr5().set_bit());

// Modify (現在値を維持しつつ変更)
dp.GPIOA.moder.modify(|_, w| w.moder5().output());
```

### 4.4 型安全性の保証

1. **レジスタアドレスの型安全**: 各レジスタは固有の型
2. **R/W 分離**: Read proxy は読み取りのみ、Write proxy は書き込みのみ
3. **EnumeratedValues**: SVD の列挙値が Rust enum に変換
4. **Singleton**: `Peripherals::take()` は 1 回だけ `Some` を返す

### 4.5 既知の問題

| 問題 | 詳細 |
|------|------|
| 巨大な生成コード | i.MX RT1062 で 932K LOC、ビルド 7-8 分、16GB+ RSS |
| SVD 品質依存 | ベンダー SVD の品質がそのまま生成コードの品質を決定 |
| unsafe 必要 | 列挙値が未定義のフィールドへの `bits()` 書き込みは unsafe |
| コード重複 | 同一 IP ブロックのペリフェラル間で共有の仕組みがない |
| modify の非アトミック性 | Read-Modify-Write であり割り込みで競合しうる |

### 4.6 chiptool (embassy-rs) の改善

| 観点 | svd2rust | chiptool |
|------|----------|----------|
| 所有権 | Singleton `take()` | なし（HAL での分割が容易） |
| レジスタ値の保存 | 不可 | **Fieldset パターン**: レジスタ値を変数に保持可能 |
| コード量 | 大量 | R/W/RW ごとの重複削減 |
| SVD パッチ | svdtools (YAML) | YAML + transforms（構造変換） |

---

## 5. SVD の品質問題

ユーザーが強調した重要な知見: **ベンダー提供の SVD はそのままではほとんど役に立たない。**

### 5.1 具体的な問題カテゴリ

| カテゴリ | 具体例 |
|---------|--------|
| 列挙値の欠落 | 大半のフィールドで `enumeratedValues` が未定義 |
| 構造エラー | `<size>` タグ欠落、空 `<alternateRegister>` |
| フィールド定義の不正確さ | データシートとのビットフィールド境界の不一致 |
| タイマーのバグ | stm32-rs によると全 STM32 ファミリのタイマー定義にバグ |
| 書き込み制約の誤り | ワードサイズ制約の設定ミス |
| ライセンス制約 | 一部ベンダーは SVD の再配布を制限 |

### 5.2 各エコシステムの対処法

| エコシステム | 対処法 |
|------------|--------|
| **stm32-rs** (Rust) | svdtools による YAML パッチ。数百のパッチファイルをコミュニティで維持 |
| **modm** | 複数データソース（CubeMX + SVD + CMSIS ヘッダ + PDF）のクロスリファレンス。DFG パイプライン内で自動修正 + 手動パッチリスト |
| **ユーザーの試み** | `patch_svd.py` で GPIO 列挙値を手動追加。stm32-rs のパッチを参考 |

### 5.3 UMI への示唆

SVD を「唯一の真実」とすることはリスクが高い。modm のように **複数データソースのクロスリファレンス** を行うか、**パッチインフラを構築** する必要がある。

---

## 6. modm アプローチ

### 6.1 データ抽出パイプライン (Device File Generator)

```
CubeMX DB + SVD + CMSIS ヘッダ + PDF → [DFG] → 中間 XML → [lbuild + Jinja2] → C++ HAL コード
```

- 1,171 STM32 デバイスを約 62 のマージ済みファイルに統合（19 倍圧縮）
- 全修正は DFG パイプラインで再現可能であることを要求
- 合計 4,557 デバイスをサポート

### 6.2 レジスタ抽象化

```cpp
enum class Control : uint8_t {
    EN = Bit7, FS = Bit6, PRE1 = Bit5, PRE0 = Bit4,
};
MODM_FLAGS8(Control);  // Control_t typedef + 演算子生成

// マルチビットフィールド
typedef Configuration<Control_t, Prescaler, 0b11, 4> Prescaler_t;

// 使用
Control_t ctrl = Control::EN | Prescaler_t(Prescaler::Div2);
// → コンパイル時に単一の定数代入に畳み込まれる
```

### 6.3 GPIO connect<> パターン

コンパイル時のピン-ペリフェラル接続検証:

```cpp
// デバイスデータからコード生成
class GpioA0 {
    template<Peripheral peripheral>
    struct Tx {
        static_assert(peripheral == Peripheral::Uart4,
            "GpioA0::Tx only connects to Uart4!");
    };
};

// 使用
Uart4::connect<GpioA0::Tx>();     // OK
Uart4::connect<GpioA0::Rx>();     // コンパイルエラー
```

### 6.4 評価

| 軸 | 評価 |
|----|------|
| データ品質 | 最高 — 複数ソースのクロスリファレンス |
| コード生成 | 成熟 — Jinja2 テンプレートで HAL 全体を生成 |
| 型安全性 | 高い — Flags<Enum,T> + Configuration + Value |
| GPIO 検証 | 独自 — connect<> でコンパイル時にピン検証 |
| 弱点 | CubeMX データの SLA 制約、独自フォーマットの不安定性 |

---

## 7. C++ RAL ライブラリ比較

### 7.1 Kvasir

**特徴:** `apply()` による遅延評価とマージ最適化。

```cpp
apply(
    clear(AHBClock::Enabled::spi0),
    set(AHBClock::Enabled::spi1)
);
// → 同一レジスタへの操作を自動マージし、1 回の R/W で実行
```

- strongly-typed enum によるフィールド混用防止
- SVD からのコード生成対応
- ゼロコスト — 手書きマクロと同一のアセンブリ（約 12 バイト）

### 7.2 regbits

3 つの主要型: `Bits<>` (単一/組み合わせビット)、`Mskd<>` (マスク付きビットスパン)、`Reg<>` (レジスタラッパー)。

- 異なるペリフェラルの定数混用をコンパイルエラーで防止
- `svd2regbits.py` で SVD から定義を生成

### 7.3 svd2cpp

```cpp
set<USART1::CR1::UE>();           // ビットセット
reset<USART1::CR1::UE>();         // ビットクリア
bool tc = read<DMA1::ISR::TCIF1>(); // フラグ読み取り
```

- 無効な値の書き込み、R/W 方向の違反がすべてコンパイルエラー
- SVD からの全自動生成

### 7.4 CMSIS

```c
USART1->CR1 |= USART_CR1_UE;     // マクロによるビット操作
```

- 型安全性なし: 異なるレジスタのマクロを混用してもエラーにならない
- 業界デファクトスタンダード

### 7.5 STM32 LL API

- 手書きの inline 関数によるレジスタ操作ラッパー
- SVD からの自動生成**ではない**
- CubeMX は LL を使った初期化コードを生成

---

## 8. 横断比較

### 8.1 レジスタ定義方式

| | CMSIS | LL | Kvasir | regbits | svd2cpp | modm | svd2rust | umimmio | mmio.hh |
|-|-------|-----|--------|---------|---------|------|----------|---------|---------|
| 定義方式 | C 構造体 + #define | inline 関数 | 型パラメータ | テンプレート型 | テンプレート | enum + Flags | 生成 struct | テンプレート | テンプレート |
| 型安全 | ✗ | ✗ | ★★★ | ★★★ | ★★★ | ★★☆ | ★★★ | ★★★ | ★★★ |
| SVD 生成 | ✓ | ✗(手書き) | ✓ | ✓ | ✓ | ✓(複合) | ✓ | — | ✓(v1+v2) |
| ゼロコスト | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Transport | Direct のみ | Direct のみ | Direct のみ | Direct のみ | Direct のみ | Direct のみ | Direct のみ | Direct+I2C+SPI | Direct+I2C+SPI+Sim |

### 8.2 データソースと生成パイプライン

| | 入力ソース | 生成ツール | パッチ戦略 |
|-|-----------|-----------|-----------|
| **svd2rust** | SVD 単一 | svd2rust (Rust) | svdtools (YAML) |
| **chiptool** | SVD 単一 | chiptool (Rust) | YAML transforms |
| **modm** | CubeMX + SVD + CMSIS + PDF | DFG + lbuild (Python) | パイプライン内自動修正 + 手動リスト |
| **Kvasir** | SVD 単一 | 独自ジェネレータ | 不明 |
| **svd2cpp** | SVD 単一 | svd2cpp (C++) | なし |
| **regbits** | SVD 単一 | svd2regbits (Python) | なし |

### 8.3 操作 API の比較

| | Read | Write | Modify | 複数フィールド一括 |
|-|------|-------|--------|-------------------|
| **CMSIS** | `REG->FIELD & MASK` | `REG->FIELD = val` | `REG->FIELD \|= bit` | 手動マスク計算 |
| **Kvasir** | `read()` → Action | `write()` → Action | `set()/clear()` → Action | `apply()` で自動マージ |
| **svd2rust** | `.read().field()` | `.write(\|w\| w.field())` | `.modify(\|r, w\| ...)` | クロージャ内で複数記述 |
| **modm** | `Flags::get()` | `Flags::set()` | `Flags::update()` | `\|` 演算子で結合 |
| **umimmio** | `RegOps::read()` | `RegOps::write()` | `RegOps::modify()` | `Value` 結合 |

### 8.4 MCU 固有情報の配置

| | 配置場所 | 粒度 |
|-|---------|------|
| **CMSIS** | `stm32f407xx.h` (ベンダー提供ヘッダ) | MCU 型番ごと |
| **svd2rust** | PAC crate (e.g., `stm32f4`) | MCU ファミリごと |
| **modm** | lbuild 生成コード内 | MCU 型番ごと（マージ） |
| **Kvasir** | 生成ヘッダ | 不明 |
| **umimmio** | **未定義** — これが本調査の主題 | — |

---

## 9. 現行 umimmio との関係

### 9.1 umimmio が提供するもの（MMIO フレームワーク）

```
Device<Access, AllowedTransports...>     ← デバイスルート + Transport 制約
  └── Block<Parent, BaseAddr, Access>    ← メモリ領域
        └── Register = BitRegion<..., IsRegister=true>  ← レジスタ
              └── Field = BitRegion<..., IsRegister=false> ← フィールド
                    └── Value<Field, EnumVal>  ← 列挙値
```

+ RegOps<Derived> CRTP: `write()`, `read()`, `modify()`, `is()`, `flip()`
+ ByteAdapter: RegOps → `raw_read()`/`raw_write()` ブリッジ
+ DirectTransport: メモリマップド直接アクセス
+ I2cTransport / SpiTransport: バス経由アクセス

### 9.2 umimmio が提供しないもの（= RAL が必要）

- 「GPIOA のベースアドレスは 0x40020000」
- 「MODER レジスタはオフセット 0x00、32 ビット幅」
- 「MODER13 はビット 26-27、値 0b01 = 出力モード」
- これらの **MCU 固有データ**

### 9.3 現状の RAL 相当コード

`umiport/include/umiport/mcu/stm32f4/` に手書きで存在:

```cpp
// uart_output.hh 内の定義例
namespace stm32f4 {
    struct RCC : mm::Device<mm::RW, mm::DirectTransportTag> {
        static constexpr mm::Addr base_address = 0x40023800;
        struct AHB1ENR : mm::Register<RCC, 0x30, mm::bits32, 0, mm::bits32, mm::Inherit, 0> {
            struct GPIODEN : mm::Field<AHB1ENR, 3, 1, mm::Inherit, 0> {};
        };
    };
}
```

**問題:** これらの定義が各ドライバファイル内に散在しており、体系的な管理がされていない。

---

## 10. 設計判断のための論点整理

### 10.1 生成 vs 手書き

| 方針 | メリット | デメリット |
|------|---------|-----------|
| **完全手書き** | 即座に始められる、API 設計の自由度 | MCU サポート追加のたびに膨大な作業、エラーリスク |
| **SVD 完全自動生成** | 網羅的、保守容易 | SVD 品質問題、パッチインフラ構築コスト、生成コード量 |
| **ハイブリッド** | 使用するペリフェラルのみ手書き + 将来的に生成移行 | 一貫性の維持が課題 |

### 10.2 配置場所

| 選択肢 | 構造 | メリット | デメリット |
|--------|------|---------|-----------|
| **umiport 内** | `umiport/mcu/stm32f4/ral/` | BSP と同じパッケージ | umiport が肥大化 |
| **独立ライブラリ** | `umidevice-stm32f4/` | 関心の分離 | パッケージ数増加 |
| **umimmio 拡張** | `umimmio/devices/stm32f4/` | MMIO と統合 | umimmio の責務拡大 |

### 10.3 SVD パッチ戦略

| 戦略 | 実装コスト | データ品質 |
|------|----------|-----------|
| stm32-rs パッチを C++ 用に転用 | 中 | 高（コミュニティ維持） |
| modm 式複合データソース | 高 | 最高 |
| 手書きのみ（SVD 不使用） | 低（初期） | MCU 追加ごとに高コスト |
| SVD + 最小限パッチ | 低〜中 | 中（基本定義は十分） |

### 10.4 umimmio との統合方法

RAL は umimmio の型（`Device`, `Register`, `Field`, `Value`）を**インスタンス化する側**であり、umimmio 自体には依存方向として問題がない:

```
umimmio (フレームワーク) ← RAL (インスタンス化) ← umiport (ドライバ)
```

---

## 11. 主要な教訓

### 11.1 各エコシステムから学ぶべき点

| エコシステム | 教訓 |
|------------|------|
| **svd2rust** | クロージャベースの Write API は安全だが、C++ では冗長になりうる。Singleton パターンは embedded では不便（HAL でのペリフェラル分割に支障） |
| **chiptool** | Fieldset パターン（レジスタ値の保持）は I2C/SPI 外部デバイスで重要。umimmio の ByteAdapter と相性が良い |
| **modm** | GPIO connect<> の static_assert は UMI でも再現可能。複数データソースの品質向上は理想的だが実装コスト大 |
| **Kvasir** | `apply()` の遅延評価・マージ最適化は独自の価値がある。複数レジスタへの一括操作の最適化 |
| **mmio.hh** | Transport 統一（Direct + I2C + SPI）の設計は umimmio に継承済み。所有権システムは C++ では限界がある。svd2ral v1/v2 のパーサー・バリデータ・配列検出ロジックは再利用可能な資産 |

### 11.2 避けるべき失敗パターン

1. **SVD を無条件に信頼する** — パッチなしの SVD はバグだらけ
2. **完璧な生成パイプラインを先に作る** — 使えるペリフェラルが 0 の状態が長期間続く
3. **所有権チェックを C++ で無理に再現する** — Rust の borrow checker は言語レベルの機能であり、ライブラリでは代替不可能
4. **巨大な単一ヘッダ生成** — svd2rust の 932K LOC 問題。MCU 全体を一度に生成するとコンパイル時間が爆発

---

## 12. 参考文献

### 調査対象プロジェクト

| プロジェクト | URL |
|------------|------|
| svd2rust | https://github.com/rust-embedded/svd2rust |
| stm32-rs | https://github.com/stm32-rs/stm32-rs |
| svdtools | https://github.com/rust-embedded/svdtools |
| chiptool | https://github.com/embassy-rs/chiptool |
| modm | https://github.com/modm-io/modm |
| modm-devices | https://github.com/modm-io/modm-devices |
| Kvasir | https://github.com/kvasir-io/Kvasir |
| regbits | https://github.com/thanks4opensource/regbits |
| svd2cpp | https://github.com/czyzlukasz/svd2cpp |
| cmsis-svd | https://github.com/cmsis-svd/cmsis-svd |
| mdrivlib | https://github.com/4ms/mdrivlib |
| OpenMPTL | https://github.com/digint/openmptl |

### UMI 関連

| ファイル | 内容 |
|---------|------|
| `/Users/tekitou/work/umi_mmio/mmio/mmio.hh` | 過去の MMIO ライブラリ |
| `/Users/tekitou/work/umi_mmio/.archive/tools/svd2ral/` | SVD → C++ コード生成ツール v1 (Jinja2) |
| `/Users/tekitou/work/umi_mmio/.archive/tools/svd2ral_v2/` | SVD → C++ コード生成ツール v2 (直接生成) |
| `/Users/tekitou/work/umi_mmio/.archive/generated/ral/` | 生成済み RAL 出力（STM32F407） |
| `/Users/tekitou/work/umi_mmio/.archive/tools/patch_svd.py` | SVD パッチスクリプト |
| `/Users/tekitou/work/umi_mmio/.archive/tools/cmsis2svd.py` | CMSIS → SVD 変換ツール |
| `umimmio/include/umimmio/register.hh` | 現行 MMIO フレームワーク |
| `umiport/include/umiport/mcu/stm32f4/` | 現行の手書き RAL 相当コード |
