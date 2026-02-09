# 07: ハードウェア定義データ統合パイプライン分析

## 概要

MCU のハードウェア定義（レジスタ、割り込みベクター、メモリマップ、GPIO AF マッピング等）を
完全に自動生成するために必要なデータソース、既存手法の分析、統合パイプライン設計案を示す。

**背景**: SVD ファイル単体では完全なハードウェア定義を得られない。
割り込みベクター定義、メモリレイアウト、コアペリフェラル、GPIO AF マッピング等は SVD に含まれず、
Rust のパッチ済み SVD にすら存在しない。
umi_mmio での CMSIS ヘッダ抽出の試みもケースの複雑さにより中断した。
modm や embassy のように複数ソースを統合するアプローチが不可欠である。

---

## 1. 必要なハードウェア定義の全体像

### 1.1 定義カテゴリと用途

| カテゴリ | 具体例 | 用途 |
|---------|--------|------|
| **レジスタ定義** | ペリフェラルレジスタ、ビットフィールド、アクセス属性 | umimmio RAL 生成 |
| **コアペリフェラル** | NVIC, SCB, SysTick, DWT, ITM, FPU, MPU, CoreDebug, TPI | コアレベル制御 |
| **ベクター定義** | IRQ 番号、ハンドラ名、コア例外 | 割り込みベクターテーブル |
| **メモリ定義** | Flash/SRAM/CCM/BKPSRAM の base/size | リンカスクリプト生成 |
| **GPIO AF** | ピン × シグナル × AF 番号 | GPIO ドライバ、ピン設定 |
| **クロックツリー** | PLL パラメータ、バスプリスケーラ、RCC イネーブルビット | クロック初期化 |
| **DMA マッピング** | ストリーム/チャネル × ペリフェラルリクエスト | DMA 設定 |
| **デバイスメタ** | コアタイプ、FPU/MPU 有無、NVIC 優先度ビット数 | コンパイル条件 |

### 1.2 各定義の変更頻度と規模

全カテゴリとも **write-once** — シリコンリビジョンでの微修正を除き不変。
ただしデバイスバリアント数が膨大（STM32 だけで 1,100 以上）であり、
手書きハードコードは非現実的。データベース＋コード生成が必須。

---

## 2. 利用可能なデータソース

### 2.1 SVD ファイル (CMSIS-SVD)

| 項目 | 内容 |
|------|------|
| **提供元** | ARM 仕様 / ベンダー提供 |
| **フォーマット** | XML (cmsis-svd.xsd) |
| **提供データ** | ペリフェラルレジスタ定義（ビットフィールドレベル）、ベースアドレス |
| **提供しないデータ** | コアペリフェラル（NVIC/SCB/DWT 等）、メモリマップ、GPIO AF、クロックツリー、DMA マッピング、割り込みベクターの完全定義 |
| **品質** | ベンダーにより大きなばらつき。ST の SVD は「精度が低い」(modm 評価) |
| **ライセンス** | ベンダー依存（ST は Apache-2.0 で modm-io/cmsis-svd-stm32 として公開） |

**致命的制約**: SVD 単体ではベアメタルシステムに必要な定義の **約 40%** しかカバーしない。

### 2.2 CMSIS ヘッダファイル

| 項目 | 内容 |
|------|------|
| **提供元** | ARM (core_cmX.h) + ベンダー (stm32f407xx.h) |
| **フォーマット** | C/C++ ヘッダ |
| **SVD にないデータ** | IRQn_Type 列挙（完全な割り込み番号）、コアペリフェラル構造体（NVIC_Type, SCB_Type 等）、メモリ BASE/SIZE マクロ、ビットバンドアドレス、デバイス構成マクロ |
| **品質** | SVD より高精度 (modm の評価: "parses CMSIS headers since SVD files are less accurate") |
| **ライセンス** | BSD-3-Clause / Apache-2.0 |

**強み**: SVD が欠落する領域（コアペリフェラル、割り込み番号）の唯一の機械可読ソース。
**弱み**: C プリプロセッサへの依存があり、抽出ツールの実装が複雑。

### 2.3 STM32CubeMX データベース

| 項目 | 内容 |
|------|------|
| **提供元** | STMicroelectronics |
| **フォーマット** | XML (families.xml, MCU 定義, IP Modes.xml) |
| **ユニークデータ** | **GPIO AF マッピング**（PRIMARY ソース）、DMA チャネル割り当て、クロックツリー定義、ペリフェラル IP バージョン |
| **提供しないデータ** | レジスタビットフィールド定義、割り込みベクター位置（IRQ 番号なし） |
| **ライセンス** | **ST SLA (再配布不可)** — 但し STM32_open_pin_data は BSD-3-Clause |

**代替**: [STM32_open_pin_data](https://github.com/STMicroelectronics/STM32_open_pin_data) (BSD-3-Clause)
が GPIO AF データのサブセットを再配布可能な形で提供。

### 2.4 CMSIS-Pack (.pdsc)

| 項目 | 内容 |
|------|------|
| **提供元** | ARM 仕様 / ベンダー提供 |
| **フォーマット** | XML (PACK.xsd) |
| **SVD にないデータ** | メモリマップ（Flash/RAM の origin/size/access）、フラッシュアルゴリズム（FLM）、プロセッサ特性（FPU/MPU/TZ）、デバッグシーケンス |
| **ライセンス** | ベンダー依存 |

**UMI での活用**: メモリマップとプロセッサ特性の取得に最適。
`cmsis-pack-manager` (Python) でプログラマティックにアクセス可能。

### 2.5 データソース対応マトリクス

```
必要な定義          最適ソース          補完ソース          umi_mmio 既存ツール
─────────────────────────────────────────────────────────────────────────────
レジスタ定義        SVD                 CMSIS ヘッダ        svd2ral v1/v2 ✓
コアペリフェラル    CMSIS ヘッダ        (手書き)            cmsis2svd ✓ (部分的)
ベクター定義        CMSIS ヘッダ        SVD (ペリフェラル側) svd2cpp_vec ✓
メモリ定義          CMSIS-Pack/CMSIS hdr CubeMX             cmsis-dev-extractor ✓
GPIO AF             CubeMX DB           Open Pin Data       (未実装)
クロックツリー      CubeMX DB           (手書き)            (未実装)
DMA マッピング      CubeMX DB           (手書き)            (未実装)
デバイスメタ        CMSIS ヘッダ        CMSIS-Pack          cmsis-dev-extractor ✓
```

---

## 3. 既存手法の詳細分析

### 3.1 umi_mmio の過去の試み

**リポジトリ**: `/Users/tekitou/work/umi_mmio/.archive/tools/`

#### 3.1.1 svd2ral v1 (Jinja2 テンプレート方式)

```
構成: cli.py / parser.py / models.py / validator.py
      codegen/{generator.py, backends.py, naming.py}
      config/{schema.py, loader.py}
生成物: .archive/generated/ral/stm32f407.hh
```

- SVD → パース → バリデーション → Jinja2 テンプレート → C++ コード
- 3 つの出力モード: single_file / grouped / individual
- バックエンド自動選択（アドレス範囲で Direct/I2C/SPI を判定）
- YAML 設定ファイルでペリフェラルフィルタリング
- STM32F407 の全ペリフェラル生成に成功

#### 3.1.2 svd2ral v2 (直接生成方式)

```
構成: __main__.py / parser.py / models.py / generator.py
生成物: .archive/generated/ral/stm32f407_v2.hh
```

- テンプレートなし、Python で直接 C++ コード文字列を構築
- ペリフェラルグループ自動検出（GPIOA/B/C → `template<uint32_t Addr> struct GPIO`）
- `dim/dimIncrement` レジスタ配列展開
- フィールド配列テンプレート生成

#### 3.1.3 CMSIS 抽出ツール群

| ツール | 入力 | 出力 | 課題 |
|--------|------|------|------|
| `cmsis2svd.py` (588行) | CMSIS ヘッダ | SVD XML | typedef struct パース、IRQn_Type 抽出、コアペリフェラルは不完全 |
| `cmsis2svd_improved.py` (843行) | CMSIS ヘッダ | SVD XML | ネストブレース対応、ITM PORT union、複数 struct パターン対応。**ケースの複雑さにより中断** |
| `cmsis-dev-extractor.py` (306行) | CMSIS ヘッダ | C++ constexpr | メモリレイアウト、ビットバンド、デバイス構成、クロック定数。**動作確認済み** |
| `svd2cpp_vec.py` (300行) | SVD | C++ ベクターテーブル | Cortex-M4 コア例外（ハードコード）+ デバイス割り込み + IRQn enum。**動作確認済み** |
| `device-yaml2cpp.py` | YAML | C++ constexpr | メモリレイアウト namespace 生成 |
| `validate_svd.py` | SVD | 検証結果 | lxml XML Schema バリデーション |

#### 3.1.4 生成済み出力

```
.archive/generated/
├── ral/
│   ├── stm32f407.hh           # v1 生成 (SVD → レジスタ定義)
│   ├── stm32f407_v2.hh        # v2 生成 (同上、テンプレート方式)
│   ├── cortex_m4.hh           # CMSIS ヘッダ → コアペリフェラル
│   └── cmsis_stm32f407_memory.hh  # メモリレイアウト定義
├── interrupt/
│   └── stm32f407_vectors.hh   # ベクターテーブル + IRQn enum
└── mmio/
    └── cortex_m4_core.hh      # コアペリフェラル (ITM, DWT, SysTick, NVIC, SCB, MPU, CoreDebug, FPU, TPI)
```

#### 3.1.5 中断の原因分析

CMSIS ヘッダ抽出（`cmsis2svd_improved.py`）が中断した主要因:

1. **typedef struct パースの複雑さ**: ネストブレース、union、条件付きコンパイル、コメント内の struct
2. **命名規則の不統一**: `_TypeDef`, `_Type`, プレーン struct が混在
3. **コアペリフェラルの特殊性**: ITM の PORT union 配列、SCB の CPACR CP10/CP11 特殊フィールド
4. **ベースアドレスの間接参照**: `#define PERIPH_BASE 0x40000000` → `#define APB1_BASE (PERIPH_BASE + 0x0)` → `#define TIM2_BASE (APB1_BASE + 0x0)` の多段参照
5. **フォールバックの限界**: パースに失敗したコアペリフェラルのアドレスをハードコードする方式では汎用性がない

**教訓**: CMSIS ヘッダの C プリプロセッサをテキスト処理で再現するのは本質的に脆弱。
modm の `stm_header.py` が採用する **「コンパイルして実行」** アプローチが正解。

### 3.2 modm DFG パイプライン

**リポジトリ**: [modm-io/modm-devices](https://github.com/modm-io/modm-devices)

#### 3.2.1 アーキテクチャ

```
[ベンダー生データ]
    │
    ▼
[raw-data-extractor] ── make extract-data-stm32
    │                    stm32_cubemx_extractor.py (CubeMX DB → コピー＋パッチ)
    ▼
[raw-device-data/]     抽出済みベンダーデータ
    │
    ▼
[DFG フロントエンド]
    │  stm_device_tree.py  (CubeMX XML パース)
    │  stm_header.py       (CMSIS ヘッダ: コンパイル＋実行で抽出)
    │  stm_peripherals.py  (ペリフェラルグルーピング)
    ▼
[Python DeviceTree]    各デバイスごとのツリー表現
    │
    ▼
[DeviceMerger]         ロスレスツリーマージ
    │                    構造的一致 → デバイス ID セット拡張
    ▼
[devices/stm32/*.xml]  106 ファイル (1,171 デバイスを圧縮)
    │
    ▼
[lbuild + Jinja2]      modm 本体でコード生成
    ▼
[C++ HAL / vectors.c / linkerscript.ld / GPIO クラス]
```

#### 3.2.2 データソースと抽出方法

| ソース | 抽出方法 | 取得データ |
|--------|---------|-----------|
| CubeMX DB | XPath クエリ (`stm_device_tree.py`) | ペリフェラル一覧、IP バージョン、GPIO AF、ピン定義 |
| CMSIS ヘッダ | **コンパイル＋実行** (`stm_header.py`) | IRQn 列挙、メモリサイズ、レジスタマップ、プリプロセッサ定義 |
| Keil DFP (.pdsc) | XML パース | デバイスバリアント、メモリレイアウト、コンパイル定義 |
| 手動パッチ | `patches/stm32.patch` | ベンダーデータの既知エラー修正 |

**核心的発見**: modm の `stm_header.py` は CMSIS ヘッダをテキスト処理するのではなく、
**C++ テンプレートを生成 → g++ でコンパイル → 実行して #define 値を取得** する。
これにより C プリプロセッサの多段マクロ展開、条件付きコンパイル等を正確に処理する。

#### 3.2.3 ロスレスマージの仕組み

```python
# DeviceTree.merge() の核心
def merge(self, other):
    if self == other:         # name + attributes のみ比較（デバイスIDは無視）
        self.ids.extend(other.ids)  # デバイスセットを拡張
        self._merge(other)          # 子ノードを再帰マージ
```

- 構造的メタデータ（name, attributes）とデバイス適用性（ids）を分離
- デバイス間の差異は `device-size="8"` 等のプレフィックス属性で表現
- 1,171 デバイスが 106 ファイルに圧縮（約 11 倍のデータ削減）

#### 3.2.4 レジスタ定義の扱い

**重要**: modm は SVD からレジスタ定義を独自生成 **しない**。
CMSIS ヘッダファイルをそのまま `modm/platform/device.hpp` 経由でラップして使用する。
これは UMI の umimmio アプローチ（型安全な RAL 生成）とは根本的に異なる。

#### 3.2.5 制限

- CubeMX DB の再配布不可（ユーザーが自分でインストール＋抽出が必要）
- クロックツリー抽出は「概念実証段階」で完全自動化に至っていない
- STM32F1 のリマップ方式は特殊処理が必要
- XML スキーマバリデーションとセマンティックチェッカーが未実装
- マルチベンダー対応で NXP の機械可読データソースが不足

### 3.3 Rust エコシステム (stm32-rs / embassy)

#### 3.3.1 stm32-rs / svdtools (SVD パッチ方式)

```
[ベンダー SVD] → [YAML パッチ (svdtools)] → [パッチ済み SVD] → [svd2rust] → [PAC クレート]
```

**YAML パッチの内容**:
- フィールド名の修正・統一
- 欠落レジスタ/フィールドの追加
- アクセス属性の修正
- enumerated values の追加
- derivedFrom 関係の修正
- **コアペリフェラルの追加はしない**（cortex-m クレートが別途提供）

**svd2rust が生成しないもの**: 割り込みベクター（cortex-m-rt が提供）、メモリマップ、
GPIO AF、クロックツリー、DMA マッピング。PAC は純粋にレジスタアクセスのみ。

**SVD の根本的問題** (stm32-rs/meta#4):
SVD は粒度が粗すぎる — 1つの SVD が MCU ファミリー全体を記述するが、
ファミリー内でもペリフェラルの実装バージョン（IP version）が異なる。
CubeMX DB の IP バージョン情報を使えばより正確な PAC が生成可能。

#### 3.3.2 cortex-m / cortex-m-rt

- **cortex-m クレート**: コアペリフェラル定義を **手書き** で提供
  - `cortex_m::peripheral::{NVIC, SCB, SysTick, DWT, ITM, ...}`
  - ARM Architecture Reference Manual に基づく Rust 構造体
- **cortex-m-rt**: ベクターテーブルを `#[interrupt]` 属性マクロで構築
  - コア例外は cortex-m-rt 内にハードコード
  - デバイス固有割り込みは PAC の `Interrupt` enum から取得

#### 3.3.3 embassy stm32-data (最も成熟した統合パイプライン)

```
入力ソース:
├── STM32Cube Database (XML) → MCU リスト、IP バージョン、GPIO AF、メモリサイズ
├── STM32Cubeprog Database  → Flash/RAM 仕様
├── STM32 HAL Headers       → 割り込み定義、ベースアドレス
└── stm32-rs SVD files      → レジスタブロック定義

処理パイプライン:
1. ソースダウンロード → sources/
2. JSON 生成 (stm32-data-gen) → build/data/chips/*.json
3. PAC 生成 (stm32-metapac-gen) → Rust クレート (stm32-metapac)

stm32-metapac の特徴:
- PeripheralRccRegister 構造体: RCC イネーブル/リセットビットのレジスタオフセット＋ビット位置
- ClockGen: 多段クロックマルチプレクサの自動生成
- IP バージョン単位のレジスタブロック再利用（SVD の MCU ファミリー単位より精密）
```

**UMI への示唆**: embassy の stm32-data は「SVD + CubeMX + HAL ヘッダの統合」を
Rust エコシステムで実現しており、C++ での同等パイプラインの参考モデルとして最適。

---

## 4. 統合パイプライン設計案

### 4.1 設計原則

1. **ベンダー公開データの直接変換を最大化** — 手作業を最小化
2. **データソースの優先順位** — 同じデータが複数ソースにある場合の arbitration ルール
3. **中間表現の確立** — 全ソースを一旦共通フォーマットに正規化してから生成
4. **段階的実装** — 最も必要なもの（レジスタ、ベクター、メモリ）から着手
5. **AI エージェント活用** — 手動パッチが必要な部分は AI で半自動化

### 4.2 パイプラインアーキテクチャ

```
Phase 1: データ取得
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
[SVD]  [CMSIS Headers]  [CMSIS-Pack]  [Open Pin Data]
  │          │                │              │
  ▼          ▼                ▼              ▼

Phase 2: ソース別パーサ
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
svd_parser   header_extractor  pdsc_parser  pindata_parser
(既存v2拡張)  (modm方式採用)    (新規)       (新規)
  │          │                │              │
  ▼          ▼                ▼              ▼

Phase 3: 中間表現 (Unified Device Model)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
              ┌──────────────────────┐
              │   device.json/yaml   │
              │                      │
              │ ├── meta{}           │  コアタイプ、FPU/MPU、NVIC bits
              │ ├── memory[]         │  Flash/RAM/CCM 領域
              │ ├── interrupts[]     │  IRQ 番号、ハンドラ名
              │ ├── peripherals{}    │  レジスタ定義 (SVD 由来)
              │ ├── core_peripherals{}│ NVIC/SCB/DWT/ITM 定義
              │ ├── gpio_af{}        │  ピン×シグナル×AF 番号
              │ ├── clock_tree{}     │  PLL/バス/プリスケーラ
              │ └── dma_mapping{}    │  ストリーム/チャネル割当
              └──────────────────────┘
                        │
                        ▼

Phase 4: コード生成
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
┌─────────────┬──────────────┬──────────────┬────────────┐
│ ral/*.hh    │ vectors.hh   │ memory.hh    │ gpio_af.hh │
│ レジスタ定義 │ ベクター      │ メモリマップ  │ AF マップ  │
│ (umimmio)   │ テーブル      │ constexpr    │            │
└─────────────┴──────────────┴──────────────┴────────────┘
```

### 4.3 ソース別パーサの設計

#### 4.3.1 SVD パーサ (svd_parser) — 既存資産の拡張

**ベース**: svd2ral v2 の parser.py + models.py

**拡張項目**:
- svdtools 形式の YAML パッチ適用機能（stm32-rs のパッチを直接利用可能に）
- IP バージョン単位のペリフェラル共有検出（CubeMX DB の IP version 情報と連携）
- 出力を中間表現 JSON に変更（C++ 直接生成ではなく）

#### 4.3.2 CMSIS ヘッダ抽出器 (header_extractor) — modm 方式の採用

**umi_mmio のテキスト処理方式を廃止し、modm の「コンパイルして実行」方式を採用する。**

```python
# 方式: C++ テンプレート生成 → g++ コンパイル → 実行 → stdout から値取得
#
# 1. ヘッダから #define 一覧を抽出するテンプレートを生成
# 2. g++ -E (プリプロセッサ展開) または コンパイル＋実行
# 3. IRQn_Type enum、メモリマクロ、デバイス構成を取得

class HeaderExtractor:
    def extract(self, device_header: str, include_paths: list[str]):
        # C++ ソースを生成
        source = self._generate_extraction_template(device_header)
        # コンパイル＋実行
        result = self._compile_and_run(source, include_paths)
        # パース
        return {
            "interrupts": self._parse_interrupts(result),
            "memory_sizes": self._parse_memory(result),
            "defines": self._parse_defines(result),
        }
```

**利点**:
- C プリプロセッサの全機能を正確に処理（多段マクロ、条件付きコンパイル）
- umi_mmio の cmsis2svd で問題になった typedef struct パースが不要
- modm で実績のある方式

**前提条件**:
- ホストに arm-none-eabi-gcc または同等のクロスコンパイラが必要
- CMSIS ヘッダの入手（BSD-3-Clause で公開されている）

#### 4.3.3 CMSIS-Pack パーサ (pdsc_parser) — 新規

```python
# .pdsc XML からメモリマップとプロセッサ特性を抽出
# cmsis-pack-manager を利用してパックの取得・キャッシュを自動化

class PdscParser:
    def parse(self, pdsc_path: str):
        return {
            "memory_map": self._extract_memory(pdsc_path),
            "processor": self._extract_processor(pdsc_path),
            "flash_algorithms": self._extract_flash(pdsc_path),
        }
```

#### 4.3.4 Open Pin Data パーサ (pindata_parser) — 新規

```python
# STM32_open_pin_data の GPIO Modes XML から AF マッピングを抽出
# BSD-3-Clause ライセンスのため再配布可能

class PinDataParser:
    def parse(self, gpio_modes_xml: str):
        return {
            "gpio_af": self._extract_af_mapping(gpio_modes_xml),
            "dma_requests": self._extract_dma(dma_modes_xml),
        }
```

### 4.4 中間表現 (Unified Device Model)

JSON ベースの中間表現。全ソースからのデータを正規化して統合する。

```json
{
  "device": "STM32F407VG",
  "meta": {
    "core": "cortex-m4",
    "fpu": true,
    "mpu": true,
    "nvic_prio_bits": 4,
    "max_frequency": 168000000
  },
  "memory": [
    {"name": "flash", "base": "0x08000000", "size": 1048576, "access": "rx"},
    {"name": "sram1", "base": "0x20000000", "size": 114688, "access": "rwx"},
    {"name": "sram2", "base": "0x2001C000", "size": 16384, "access": "rwx"},
    {"name": "ccm",   "base": "0x10000000", "size": 65536, "access": "rw"}
  ],
  "interrupts": [
    {"name": "WWDG", "irqn": 0, "description": "Window Watchdog"},
    {"name": "PVD",  "irqn": 1, "description": "PVD through EXTI"}
  ],
  "peripherals": {
    "GPIOA": {
      "base_address": "0x40020000",
      "ip_version": "gpio_v2_0",
      "registers": {}
    }
  },
  "gpio_af": {
    "PA0": [
      {"signal": "TIM2_CH1", "af": 1},
      {"signal": "USART2_CTS", "af": 7}
    ]
  }
}
```

### 4.5 データソース優先順位 (Arbitration Rules)

同じデータが複数ソースに存在する場合の優先順位:

| データ | 第1優先 | 第2優先 | 理由 |
|--------|---------|---------|------|
| レジスタ定義 | SVD (パッチ済み) | CMSIS ヘッダ | ビットフィールドレベルの精度 |
| IRQ 番号 | CMSIS ヘッダ | SVD | ヘッダが正規ソース |
| メモリマップ | CMSIS-Pack | CMSIS ヘッダ | 構造化データ |
| GPIO AF | Open Pin Data | CubeMX DB | ライセンスの自由度 |
| コアペリフェラル | CMSIS ヘッダ (core_cmX.h) | — | 唯一のソース |
| デバイスメタ | CMSIS ヘッダ | CMSIS-Pack | 直接的なマクロ定義 |

### 4.6 コード生成器

既存の svd2ral v1/v2 の生成器を拡張し、中間表現からの生成に変更。

```
中間表現 (JSON)
    │
    ├── ral_generator.py      → ral/<device>.hh     (レジスタ定義、umimmio 形式)
    ├── vector_generator.py   → vectors/<device>.hh  (ベクターテーブル + IRQn enum)
    ├── memory_generator.py   → memory/<device>.hh   (メモリレイアウト constexpr)
    ├── core_generator.py     → core/<arch>.hh       (コアペリフェラル定義)
    ├── gpio_generator.py     → gpio/<device>.hh     (GPIO AF マップ)
    └── linker_generator.py   → <device>.ld          (リンカスクリプト)
```

---

## 5. AI エージェント活用戦略

### 5.1 AI が効果的な領域

| 領域 | 作業内容 | 自動化度 |
|------|---------|----------|
| SVD パッチ作成 | リファレンスマニュアル PDF を読み YAML パッチを生成 | 半自動 |
| コアペリフェラルDB 初期構築 | ARM Architecture Reference Manual からレジスタ定義を抽出 | 半自動 |
| 新 MCU 対応 | 既存デバイスをテンプレートに新デバイスの差分を反映 | 半自動 |
| バリデーション | 生成コードとリファレンスマニュアルの照合 | 自動 |
| クロックツリー記述 | RCC レジスタとクロック図から制約モデルを構築 | 手動＋AI 支援 |

### 5.2 ワークフロー

```
1. 自動抽出可能なデータ（SVD, CMSIS-Pack, Open Pin Data）をパイプラインで処理
2. 欠落・不正確なデータを AI エージェントが検出
3. AI がリファレンスマニュアルを参照してパッチ/追加データを提案
4. 人間がレビュー＋承認
5. パッチをリポジトリにコミット
```

### 5.3 コアペリフェラルの扱い

コアペリフェラル（Cortex-M0/M3/M4/M7/M33 等）はアーキテクチャごとに固定。
**AI で一度正確に記述すれば、全デバイスで再利用可能。**

アーキテクチャ数は限定的:
- Cortex-M0/M0+ (ARMv6-M)
- Cortex-M3 (ARMv7-M)
- Cortex-M4/M4F (ARMv7E-M)
- Cortex-M7 (ARMv7E-M)
- Cortex-M23 (ARMv8-M Baseline)
- Cortex-M33 (ARMv8-M Mainline)
- Cortex-M55 (ARMv8.1-M)
- Cortex-M85 (ARMv8.1-M)

既存の `cortex_m4_core.hh` を参考に、AI エージェントで全アーキテクチャ分を生成。
ARM Architecture Reference Manual をソースとし、CMSIS ヘッダで検証する。

---

## 6. 実装ロードマップ

### Phase 1: 基盤構築 (SVD + CMSIS ヘッダ)

**目標**: レジスタ定義 + ベクターテーブル + メモリマップの自動生成

1. 中間表現フォーマットの確定
2. svd2ral v2 パーサを中間表現出力に変更
3. modm 方式の CMSIS ヘッダ抽出器の実装
4. IRQ マージロジック（SVD のペリフェラル割り込み + ヘッダの IRQn 列挙）
5. STM32F407 で end-to-end 動作検証

**既存資産の再利用**:
- svd2ral v2: parser.py, models.py → 中間表現出力に変更
- svd2cpp_vec.py の IRQ 抽出ロジック
- cmsis-dev-extractor.py のメモリレイアウト抽出ロジック

### Phase 2: 拡張 (CMSIS-Pack + Open Pin Data)

**目標**: メモリマップの正確化 + GPIO AF マッピング

1. CMSIS-Pack パーサの実装（cmsis-pack-manager 活用）
2. STM32_open_pin_data パーサの実装
3. GPIO AF 中間表現の設計と生成器の実装
4. リンカスクリプト生成の統合

### Phase 3: AI 支援データベース拡充

**目標**: マルチデバイス対応 + コアペリフェラル完全カバー

1. 全 Cortex-M アーキテクチャのコアペリフェラル DB 構築
2. svdtools YAML パッチの AI 生成ワークフロー確立
3. STM32 主要ファミリー (F0/F1/F3/F4/F7/H7/L4/G4) の中間表現生成
4. クロックツリーデータの段階的追加

### Phase 4: マルチベンダー拡張

**目標**: NXP, Nordic, RP2040 等への対応

1. ベンダー別フロントエンドの設計（modm の構造を参考）
2. NXP MCUXpresso Config Tools / ATDF 等のデータソース調査
3. ベンダー共通の中間表現による統一生成

---

## 7. 既存プロジェクトとの比較

### 7.1 アプローチ比較

| 項目 | UMI (提案) | modm | embassy stm32-data | stm32-rs |
|------|-----------|------|-------------------|---------|
| レジスタ定義 | 型安全 RAL (umimmio) | CMSIS ヘッダ直接利用 | IP バージョン単位 PAC | SVD + パッチ |
| コアペリフェラル | AI 生成 DB | CMSIS ヘッダラップ | 手書き cortex-m | 手書き cortex-m |
| ベクター | 中間表現→生成 | DeviceTree→vectors.c | JSON→metapac | cortex-m-rt |
| GPIO AF | Open Pin Data | CubeMX DB | CubeMX DB | 対象外 |
| 中間表現 | JSON | 独自 XML | JSON | パッチ済み SVD |
| マルチベンダー | 設計段階 | STM32/AVR/SAM/nRF | STM32 のみ | STM32 のみ |

### 7.2 UMI の差別化ポイント

1. **型安全な RAL**: modm の CMSIS ヘッダ直接利用や stm32-rs の PAC と異なり、
   umimmio の constexpr 型システムでコンパイル時に型安全性を保証
2. **AI ファーストのデータ拡充**: 手動パッチ（stm32-rs）やベンダーツール依存（modm）ではなく、
   AI エージェントを第一級の手段としてデータベース構築に活用
3. **ライセンスクリーンなパイプライン**: BSD/Apache ソースのみを使用し、
   CubeMX DB の ST SLA 制約を Open Pin Data で回避

---

## 8. 参照

### 8.1 umi_mmio 既存資産

| パス | 内容 |
|------|------|
| `.archive/tools/svd2ral/` | v1 生成器 (Jinja2) |
| `.archive/tools/svd2ral_v2/` | v2 生成器 (直接生成) |
| `.archive/tools/cmsis2svd.py` | CMSIS→SVD 変換 |
| `.archive/tools/cmsis2svd_improved.py` | 改良版 (中断) |
| `.archive/tools/cmsis-dev-extractor.py` | メモリ/デバイス構成抽出 |
| `.archive/tools/svd2cpp_vec.py` | ベクターテーブル生成 |
| `.archive/tools/device-yaml2cpp.py` | YAML→C++ |
| `.archive/generated/` | 生成済み出力一式 |

### 8.2 外部プロジェクト

| プロジェクト | URL | 参考ポイント |
|-------------|-----|-------------|
| modm-devices | [github.com/modm-io/modm-devices](https://github.com/modm-io/modm-devices) | DFG パイプライン、ロスレスマージ |
| modm-data | [github.com/modm-io/modm-data](https://github.com/modm-io/modm-data) | PDF データ抽出 |
| stm32-data | [github.com/embassy-rs/stm32-data](https://github.com/embassy-rs/stm32-data) | 複数ソース統合パイプライン |
| stm32-rs | [github.com/stm32-rs/stm32-rs](https://github.com/stm32-rs/stm32-rs) | SVD パッチ YAML 形式 |
| svdtools | [github.com/stm32-rs/svdtools](https://github.com/stm32-rs/svdtools) | SVD パッチエンジン |
| STM32_open_pin_data | [github.com/STMicroelectronics/STM32_open_pin_data](https://github.com/STMicroelectronics/STM32_open_pin_data) | BSD-3 GPIO AF データ |
| cmsis-svd-stm32 | [github.com/modm-io/cmsis-svd-stm32](https://github.com/modm-io/cmsis-svd-stm32) | Apache-2.0 SVD ファイル |
| cmsis-header-stm32 | [github.com/modm-io/cmsis-header-stm32](https://github.com/modm-io/cmsis-header-stm32) | CMSIS ヘッダ集 |
| cmsis-pack-manager | [github.com/pyocd/cmsis-pack-manager](https://github.com/pyocd/cmsis-pack-manager) | CMSIS-Pack アクセスツール |
| Salkinium Master Thesis | [salkinium.com/master.pdf](https://salkinium.com/master.pdf) | PDF データ抽出研究 |
