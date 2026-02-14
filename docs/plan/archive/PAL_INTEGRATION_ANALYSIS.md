# PAL/コード生成パイプラインと12ライブラリ構成の統合分析

**作成日:** 2026-02-14
**入力文書:**
- `docs/plan/LIBRARY_SPEC.md` v1.2.0
- `docs/plan/IMPLEMENTATION_PLAN.md` v1.0.0
- `lib/docs/design/` 配下の設計文書群（integration.md, codegen_pipeline.md, pal/*, board/*, pipeline/*, foundations/*, hal/*, init_sequence.md, interrupt_model.md, implementation_roadmap.md）

---

## 1. 問題の本質

`lib/docs/design/` には、MCU のハードウェア定義を **データベースから全自動生成** し、ビルドシステム・ドライバ・コード生成の **3者を循環的に統合** する設計が詳細に文書化されている。

一方 `docs/plan/LIBRARY_SPEC.md` と `IMPLEMENTATION_PLAN.md` は、この仕組みを **一切考慮していない**。umiport を「手書きの HAL ドライバ集」として扱っており、以下が欠落している:

| 欠落要素 | 設計文書での定義箇所 |
|---------|------------------|
| PAL 4 層スコーピングモデル (L1〜L4) | pal/01_LAYER_MODEL.md |
| コード生成パイプライン (Parse → Merge → Generate → Output) | codegen_pipeline.md |
| MCU データベーススキーマ (Lua) | integration.md §3 |
| board.lua + C++ の二重構造 | board/architecture.md |
| 生成物の配置先 (`umiport/include/umiport/pal/`) | integration.md §2 |
| ビルド・生成・ドライバの3者同時決定 | integration.md §1 |
| 既存ツール資産 (svd2ral v1/v2, cmsis-dev-extractor 等) | pipeline/hw_data_pipeline.md §3 |
| 初期化シーケンス (Stage 0〜6) | init_sequence.md |
| 割り込みディスパッチモデル | interrupt_model.md |
| PLL 自動探索 | board/config.md §5.5 |

---

## 2. 影響を受けるライブラリ

### 2.1 umiport — 最大の影響、構造の再定義が必要

**現在の LIBRARY_SPEC における umiport:**
```
lib/umiport/
├── include/umiport/
│   ├── arch/cm4/, cm7/
│   ├── mcu/stm32f4/
│   ├── board/stm32f4_disco/
│   ├── platform/embedded/, wasm/
│   └── common/
├── src/
└── ...
```

**設計文書が定義する umiport の実態:**
```
lib/umiport/
├── include/umiport/
│   ├── pal/                          ← [欠落] 生成物ルート
│   │   ├── arch/arm/cortex_m/        ←   L1: アーキテクチャ共通 (NVIC, SCB, SysTick)
│   │   ├── core/cortex_m4f/          ←   L2: コアプロファイル固有 (DWT, FPU, MPU)
│   │   └── mcu/stm32f4/             ←   L3: MCU ファミリ固有 (GPIO, UART, RCC 等)
│   │       └── periph/gpio.hh       ←     SVD から生成される umimmio テンプレートインスタンス
│   │
│   ├── mcu/stm32f4/                  ← 手書きドライバ (PAL を #include する)
│   │   ├── gpio_driver.hh
│   │   ├── uart_driver.hh
│   │   └── system_init.hh           ← クロック初期化 (PLL パラメータは board.lua から生成)
│   │
│   ├── arch/arm/cortex-m/            ← 手書きアーキテクチャ共通ドライバ
│   │   └── dwt.hh
│   │
│   └── board/                        ← ボード定義 (Lua + C++ 二重構造)
│       └── stm32f4-disco/
│           ├── platform.hh           ← 生成: HAL Concept 充足型
│           ├── board.hh              ← 生成: constexpr 定数 (HSE, sysclk 等)
│           ├── pin_config.hh         ← 生成: GPIO AF テーブル
│           └── clock_config.hh       ← 生成: PLL M/N/P/Q (自動探索)
│
├── database/                         ← [欠落] MCU/ボード定義 DB
│   ├── mcu/
│   │   └── stm32f407vg.lua           ← MCU スペック (メモリ、クロック範囲、ペリフェラル一覧)
│   └── boards/
│       └── stm32f4-disco/
│           └── board.lua             ← ボード設定 (HSE, ピン割り当て, 使用ペリフェラル)
│
├── gen/                              ← [欠落] コード生成ツール
│   ├── umipal-gen                    ← Python メインスクリプト
│   ├── parsers/                      ← SVD, CMSIS ヘッダ等のパーサー
│   ├── templates/                    ← Jinja2 テンプレート
│   └── data/                         ← SVD ファイル、パッチ YAML
│
├── src/
│   ├── common/irq.cc
│   ├── mcu/stm32f4/syscalls.cc
│   └── hal/stm32_otg.cc             ← USB OTG FS 実装
│
├── rules/                            ← [欠落] xmake ビルドルール
│   ├── board.lua                     ← ボード選択ルール (MCU DB 読み込み → ツールチェイン決定)
│   └── pal_generator.lua            ← PAL 再生成タスク定義
│
└── ...
```

**umiport は単なるドライバ集ではなく、「ハードウェアポーティングキット」全体** である:
1. PAL (生成されたレジスタ定義)
2. ドライバ (PAL を使用する手書き HAL 実装)
3. MCU データベース (Lua)
4. ボード定義 (Lua + C++)
5. コード生成ツール (Python)
6. xmake ルール (board.lua)
7. ビルド成果物 (startup.cc, memory.ld)

### 2.2 umimmio — 間接的な影響

umimmio 自体の構造は変わらないが、PAL の生成物は umimmio の型テンプレートのインスタンス化である:

```cpp
// umimmio が提供するテンプレート
template <mm::Addr BaseAddr>
struct Device { ... };

template <typename Parent, uint32_t Offset, uint32_t Bits, typename Access, uint32_t Reset>
struct Register { ... };

template <typename Register, uint32_t BitOffset, uint32_t BitWidth>
struct Field { ... };

// PAL が生成するインスタンス (SVD → Jinja2 → C++)
template <mm::Addr BaseAddr>
struct GPIOx : mm::Device<mm::RW, mm::DirectTransportTag> {
    struct MODER : mm::Register<GPIOx, 0x00, 32, mm::RW, 0xA800'0000> {
        struct MODER0 : mm::Field<MODER, 0, 2> {};
        // ...
    };
};
using GPIOA = GPIOx<0x4002'0000>;
```

**影響:** LIBRARY_SPEC §5 の umimmio の説明に「PAL の生成基盤」としての役割を明記すべき。

### 2.3 umicore — irq.hh との関連

PAL の C4 (ベクター定義) は umicore の `irq.hh` (割り込み抽象) と直接関連する:
- umicore: backend-agnostic な `umi::irq::Handler`, `init()`, `set_handler()`
- PAL C4: MCU 固有の `IRQn` enum、weak シンボルのベクターテーブル

この2層構造は LIBRARY_SPEC で明示すべき。

### 2.4 umihal — Concept と PAL 実装の関係

umihal が定義する Concept:
```cpp
template <typename T>
concept GpioLike = requires(T) { T::set_mode(...); T::write(...); };
```

umiport の手書きドライバが PAL 生成物を使って充足:
```cpp
// PAL 生成: GPIOx<BaseAddr>::MODER, ::ODR, ::BSRR
// 手書きドライバ: GpioDriver<GPIOA> がこれらを使って GpioLike を充足
```

この関係は LIBRARY_SPEC の §6 (プラットフォーム抽象化) に組み込むべき。

---

## 3. 再構築すべき箇所の判定

### 3.1 再構築が必要（構造的に不足）

| 対象 | 現状 | あるべき姿 |
|------|------|-----------|
| **LIBRARY_SPEC §5 umiport のツリー** | 4 層 (arch/mcu/board/platform) | pal/ + drivers/ + database/ + gen/ + rules/ を含む完全構造 |
| **LIBRARY_SPEC §7 xmake ビルドシステム** | ライブラリの `add_deps` のみ | `xmake pal-gen` タスク、board.lua ルール、MCU DB 解決チェーンを含む |
| **IMPLEMENTATION_PLAN Phase 2 (umiport)** | 「4 層構造で新規構築」 | PAL 生成パイプライン構築 + MCU DB 移設 + ボード定義移設を含むサブフェーズ分割 |

### 3.2 追記で対応可能（情報の補足）

| 対象 | 追記内容 |
|------|---------|
| **LIBRARY_SPEC §5 umimmio** | 「PAL の生成基盤としての役割」を注記 |
| **LIBRARY_SPEC §5 umicore** | irq.hh と PAL C4 の2層関係を注記 |
| **LIBRARY_SPEC §6 プラットフォーム抽象化** | HAL Concept → PAL 生成物 → 手書きドライバの3層パターンを明示 |
| **LIBRARY_SPEC §11 将来拡張** | PAL の Phase 2〜5 (GPIO MUX, クロックツリー, 非 ARM) を将来拡張として記載 |

### 3.3 変更不要

| 対象 | 理由 |
|------|------|
| umitest, umibench, umirtm (L0) | PAL とは無関係 |
| umidsp, umidi, umiusb (L3) | プラットフォーム非依存。PAL の影響なし |
| umios (L4) | kernel 内部は PAL を直接参照しない。adapter 層が umiport 経由で間接的に使う |
| umidevice (L2) | デバイスドライバは umihal Concept に依存し、PAL とは直接結合しない |
| 依存マトリクス (§4.2) | ライブラリ間の `add_deps` は変わらない |

---

## 4. PAL 統合後の umiport Phase 設計（IMPLEMENTATION_PLAN への提案）

現在の IMPLEMENTATION_PLAN Phase 2 は umiport を一枚岩で構築するが、設計文書を考慮すると**3サブフェーズに分割**すべき:

### Phase 2a: umiport 骨格 + 手書き PAL（最小動作スライス）

**目的:** PAL 生成パイプラインなしで最小限のビルドを成立させる。

```
lib/umiport/
├── include/umiport/
│   ├── pal/                          ← 手書き (Phase 2c で生成物に置換)
│   │   ├── arch/arm/cortex_m/
│   │   │   ├── nvic.hh              ← 手書き: 仕様固定 (有限8バリアント)
│   │   │   └── systick.hh
│   │   └── mcu/stm32f4/
│   │       └── periph/gpio.hh       ← 手書き: 最小限のレジスタ定義
│   ├── mcu/stm32f4/
│   │   ├── gpio_driver.hh           ← 手書き: PAL gpio.hh を使用
│   │   └── system_init.hh           ← 手書き: PLL パラメータは定数
│   ├── board/stm32f4-disco/
│   │   ├── platform.hh              ← 手書き
│   │   └── board.hh                 ← 手書き
│   └── common/
├── src/
│   ├── common/irq.cc
│   └── mcu/stm32f4/syscalls.cc
└── xmake.lua                        ← board.lua ルール (手動MCU選択)
```

**成功基準:**
- `xmake build stm32f4_kernel` がこの手書き PAL で成功する
- umihal Concept が充足される
- 既存 `lib/_archive/umi/port/` のコードを参照して構築

### Phase 2b: MCU データベース + ボード定義の Lua 化

**目的:** board.lua → C++ 生成チェーンを確立する。

```
lib/umiport/
├── database/
│   ├── mcu/stm32f407vg.lua           ← 新規: MCU スペック
│   └── boards/stm32f4-disco/
│       └── board.lua                 ← 新規: ボード設定
├── rules/
│   └── board.lua                     ← xmake-repo/synthernet の board.lua を参照して構築
└── ...
```

**生成チェーン:**
1. `board.lua` → `board.hh` (constexpr 定数)
2. `board.lua` → `platform.hh` (HAL 型解決)
3. `board.lua` + `mcu/*.lua` → `memory.ld` (リンカスクリプト)
4. `board.lua` + `mcu/*.lua` → `clock_config.hh` (PLL M/N/P/Q 自動探索)

**成功基準:**
- `xmake build stm32f4_kernel --board=stm32f4-disco` でボード定義から自動解決
- 手書き `board.hh` / `platform.hh` を生成物に置換

### Phase 2c: PAL コード生成パイプライン

**目的:** SVD → umimmio C++ ヘッダの自動生成を確立する。

```
lib/umiport/
├── gen/
│   ├── umipal-gen                    ← Python メインスクリプト
│   ├── parsers/
│   │   ├── svd_parser.py            ← SVD XML パーサー (cmsis-svd ベース)
│   │   └── cmsis_header_parser.py   ← CMSIS ヘッダパーサー (C4: IRQn 抽出)
│   ├── models/
│   │   └── device_model.py          ← 統一中間表現 (Unified Device Model)
│   ├── templates/
│   │   ├── peripheral.hh.j2         ← C6: ペリフェラルレジスタ
│   │   ├── vectors.hh.j2            ← C4: 割り込みベクター
│   │   └── memory.hh.j2             ← C5: メモリマップ
│   └── data/
│       ├── svd/STM32F407.svd
│       └── patches/stm32f4.yaml     ← stm32-rs パッチ形式
└── ...
```

**生成対象 (Phase 1 スコープ):**
| カテゴリ | 入力 | 出力 | 優先度 |
|---------|------|------|:------:|
| C6 ペリフェラルレジスタ | SVD + パッチ | `pal/mcu/stm32f4/periph/*.hh` | 1 |
| C4 割り込みベクター | CMSIS ヘッダ | `pal/mcu/stm32f4/vectors.hh` | 2 |
| C5 メモリマップ | MCU DB (Lua) | `pal/mcu/stm32f4/memory.hh` | 2 |

**成功基準:**
- `xmake pal-gen --family stm32f4` で C6, C4, C5 が生成される
- 生成物がPhase 2a の手書き PAL と同等以上の品質
- 生成物で `xmake build stm32f4_kernel` が成功する

**除外 (将来フェーズ):**
- C7 GPIO MUX (Open Pin Data パーサーが必要)
- C8 クロックツリー (部分的に手動)
- C9〜C14 (データソースが限定的)
- 非 STM32 プラットフォーム (RP2040, ESP32)

### Phase 2 の前提: 既存ツール資産の活用

`lib/_archive/` の umimmio 関連ツールを参照:

| ツール | 場所 (アーカイブ後) | 再利用度 | 用途 |
|--------|------------------|:--------:|------|
| svd2ral v1 | `_archive/umi/mmio/.archive/tools/svd2ral/` | ★★★★ | SVD → C++ 変換のロジック。umimmio API 用にリファクタ |
| svd2ral v2 | 同上 | ★★★ | グループ検出ロジックを再利用 |
| cmsis-dev-extractor | 同上 | ★★★★ | C5 メモリマップ + C3 デバイスメタの抽出 |
| svd2cpp_vec | 同上 | ★★★★ | C4 割り込みベクター生成 |

---

## 5. LIBRARY_SPEC への具体的な修正提案

### 5.1 §5 umiport の構造を再定義

**現在の §5 umiport:**
- arch/mcu/board/platform の 4 層ヘッダ構造
- src/ に irq.cc, syscalls.cc

**修正後:**
- pal/ (生成物ルート、PAL L1〜L3 スコープ) を追加
- database/ (MCU DB + ボード定義 Lua) を追加
- gen/ (コード生成ツール) を追加
- rules/ (xmake ビルドルール) を追加
- pal/ と mcu/ (手書きドライバ) の責務分離を明示

### 5.2 §6 に PAL 生成パターンを追加

**現在の §6:** プラットフォーム選択メカニズム (`is_plat()` のみ)

**追加すべき内容:**
- HAL Concept (umihal) → PAL 生成物 (umiport/pal/) → 手書きドライバ (umiport/mcu/) の3層パターン
- board.lua → C++ 生成チェーン
- `xmake pal-gen` タスクの位置付け

### 5.3 §7 にビルド統合セクションを追加

**追加すべき内容:**
- MCU DB スキーマ (integration.md §3)
- ボード選択 → MCU 特定 → PAL 生成物選択の解決チェーン
- `xmake pal-gen` タスク定義
- 生成タイミング: 事前生成 (git 管理下) vs ビルド時の分離

### 5.4 §11 将来拡張に PAL Phase 2〜5 を追記

| フェーズ | カテゴリ | 内容 | 条件 |
|---------|---------|------|------|
| PAL Phase 2 | C7 + C8 | GPIO MUX + クロックツリー | Open Pin Data パーサー完成 |
| PAL Phase 3 | C9〜C14 | DMA, 電力, セキュリティ等 | 主要ドライバが安定 |
| PAL Phase 4 | — | RP2040 対応 | ARM 以外のアーキテクチャ |
| PAL Phase 5 | — | ESP32 対応 | RISC-V + Xtensa |

---

## 6. 「再構築すべき」の判定まとめ

### 6.1 umiport は再構築すべき — 設計文書をベースに

現在の LIBRARY_SPEC/IMPLEMENTATION_PLAN の umiport 定義は、`lib/docs/design/` の統合設計に対して **構造的に不足** している。手書きドライバのみのポーティング層として記述されているが、実際にはデータベース、コード生成、ビルドルールを包含する「ハードウェアポーティングキット」である。

**結論:** LIBRARY_SPEC §5 の umiport セクションと IMPLEMENTATION_PLAN Phase 2 を、設計文書の統合設計をベースに**再構築**する。

### 6.2 他のライブラリは追記で十分 — 構造は変わらない

umimmio, umicore, umihal は PAL との関係を注記すれば十分。L3/L4/L0 は影響なし。12ライブラリ構成自体は変更不要。依存マトリクスも変更不要。

### 6.3 IMPLEMENTATION_PLAN の Phase 2 はサブフェーズ分割

Phase 2 を 2a (手書き PAL) → 2b (MCU DB + Lua 化) → 2c (PAL 生成パイプライン) に分割する。これにより、生成パイプラインがなくても umiport のビルドを先行させ、段階的に自動化を進められる。

---

## 付録: 設計文書の参照マップ

```
lib/docs/design/
├── foundations/architecture.md      ← パッケージ構成の基本原則
├── hal/concept_design.md            ← HAL Concept 設計
├── pal/
│   ├── 00_OVERVIEW.md               ← PAL の目的と設計原則
│   ├── 01_LAYER_MODEL.md            ← 4 層スコーピングモデル
│   ├── 02_CATEGORY_INDEX.md         ← C1〜C14 カテゴリ全体像
│   ├── 03_ARCHITECTURE.md           ← 生成ヘッダの型構造
│   ├── 04_ANALYSIS.md               ← 各カテゴリの詳細分析
│   ├── 05_DATA_SOURCES.md           ← データソース (SVD, CMSIS 等)
│   └── categories/C01〜C14.md       ← 各カテゴリの詳細設計
├── pipeline/hw_data_pipeline.md     ← データ統合パイプライン分析
├── codegen_pipeline.md              ← コード生成実装設計
├── integration.md                   ← ビルド・生成・ドライバ統合設計
├── build_system.md                  ← ビルドシステム設計
├── board/
│   ├── architecture.md              ← ボード定義の二重構造
│   ├── config.md                    ← 設定フォーマット詳細
│   └── config_analysis.md           ← 設定方式の比較分析
├── init_sequence.md                 ← 初期化シーケンス (Stage 0〜6)
├── interrupt_model.md               ← 割り込みディスパッチモデル
└── implementation_roadmap.md        ← 実装ロードマップ
```
