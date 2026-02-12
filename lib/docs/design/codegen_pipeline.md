# コード生成パイプライン実装設計

**ステータス:** 設計中  **策定日:** 2026-02-10
**根拠:** [pipeline/hw_data_pipeline.md](pipeline/hw_data_pipeline.md), [pal/05_DATA_SOURCES.md](pal/05_DATA_SOURCES.md)

**関連文書:**
- [integration.md](integration.md) — **ビルド・生成・ドライバの統合設計 (本文書と密結合)**
- [pipeline/hw_data_pipeline.md](pipeline/hw_data_pipeline.md) — データソース分析 (何を生成するか)
- [pal/02_CATEGORY_INDEX.md](pal/02_CATEGORY_INDEX.md) — PAL カテゴリ一覧 (C1-C14)
- [pal/03_ARCHITECTURE.md](pal/03_ARCHITECTURE.md) — PAL アーキテクチャ (生成物の型構造)
- [build_system.md](build_system.md) — ビルドシステムとの統合

---

## 1. 目的

[pipeline/hw_data_pipeline.md](pipeline/hw_data_pipeline.md) は「何を・どこから」の分析を完了した。
本文書は **「どうやって生成するか」** の実装設計を扱う。

| 観点 | hw_data_pipeline.md | 本文書 |
|------|---------------------|--------|
| データソース | 詳細分析済み | 参照のみ |
| 生成対象カテゴリ | C1-C14 定義済み | 参照のみ |
| ツール選定 | 未定 | **本文書で決定** |
| パイプライン実装 | 概念のみ | **本文書で設計** |
| ビルド統合 | 未定 | **本文書で設計** |
| 差分更新戦略 | 未定 | **本文書で設計** |

---

## 2. パイプライン全体像

```
            ┌─────────────────────────────────────────────────┐
            │              Data Sources (入力)                 │
            │                                                 │
            │  D1: CMSIS-SVD (.svd)  ─── ペリフェラルレジスタ  │
            │  D2: CMSIS Headers     ─── コアペリフェラル, IRQ │
            │  D3: STM32_open_pin_data── GPIO AF マッピング    │
            │  D4: modm-devices      ─── 統合 MCU データ       │
            │  D5: Vendor SDK Headers ── ベンダー固有           │
            └────────────┬────────────────────────────────────┘
                         │
            ┌────────────▼────────────────────────────────────┐
            │           Stage 1: Parse (パーサー)              │
            │                                                  │
            │  svd_parser     → SVD XML → 中間表現             │
            │  header_parser  → C ヘッダ → 中間表現             │
            │  pindata_parser → GPIO XML → 中間表現             │
            │  modm_importer  → modm DB → 中間表現              │
            └────────────┬────────────────────────────────────┘
                         │
            ┌────────────▼────────────────────────────────────┐
            │           Stage 2: Merge (統合)                  │
            │                                                  │
            │  各パーサーの出力を MCU 単位で統合                  │
            │  → 統一中間表現 (Unified Device Model)            │
            │  → カテゴリ別に C1-C14 へ分類                      │
            │  → バリデーション (矛盾検出, 欠損警告)             │
            └────────────┬────────────────────────────────────┘
                         │
            ┌────────────▼────────────────────────────────────┐
            │           Stage 3: Generate (コード生成)          │
            │                                                  │
            │  テンプレートエンジン → C++ ヘッダ生成              │
            │  各カテゴリ (C1-C14) ごとにテンプレートを適用       │
            │  → umimmio 型テンプレートのインスタンス化           │
            │  → constexpr 定数テーブル                          │
            │  → リンカスクリプトフラグメント                     │
            └────────────┬────────────────────────────────────┘
                         │
            ┌────────────▼────────────────────────────────────┐
            │           Stage 4: Output (出力)                 │
            │                                                  │
            │  生成ファイル配置 + メタデータ生成                  │
            │  → ファイルハッシュによる差分検出                   │
            │  → 変更があったファイルのみ書き出し                 │
            └─────────────────────────────────────────────────┘
```

---

## 3. ツール選定

### 3.1 生成ツール言語

| 選択肢 | 利点 | 欠点 | 評価 |
|--------|------|------|------|
| **Python** | SVD パーサーの既存実装豊富 (cmsis-svd-data, svdtools)、テンプレートエンジン (Jinja2) 成熟 | xmake 連携に外部ランタイム必要 | **推奨** |
| Lua (xmake 内) | xmake とネイティブ統合、追加依存なし | XML パーサー貧弱、テンプレートエンジン不在 | 複雑なパースには不向き |
| Rust | 型安全、svd-parser クレート存在 | ビルド依存追加、学習コスト | オーバーエンジニアリング |

**決定**: Python を主軸とし、xmake からの呼び出しは Lua ラッパーで行う。

### 3.2 主要依存ライブラリ

| 用途 | ライブラリ | 備考 |
|------|-----------|------|
| SVD パース | `cmsis-svd` (Python) | CMSIS-SVD XML の標準パーサー |
| テンプレート | `Jinja2` | C++ コード生成テンプレート |
| XML パース | `lxml` | STM32_open_pin_data の GPIO AF データ |
| スキーマ検証 | `pydantic` | 中間表現のバリデーション |
| CLI | `click` or `argparse` | コマンドラインインターフェース |

---

## 4. 中間表現 (Unified Device Model)

### 4.1 スキーマ概要

全パーサーの出力を統一する中間表現。MCU 1 デバイスにつき 1 つの JSON/dict。

```python
@dataclass
class DeviceModel:
    """統一デバイスモデル — 1 MCU = 1 インスタンス"""
    meta: DeviceMeta           # C12: デバイスメタ情報
    core: CoreInfo             # C1/C2/C3: コア情報
    vectors: list[Vector]      # C4: 割り込みベクター
    memory: MemoryMap          # C5: メモリマップ
    peripherals: list[Peripheral]  # C6: ペリフェラルレジスタ
    gpio_mux: GpioMuxTable     # C7: GPIO AF マッピング
    clock_tree: ClockTree      # C8: クロックツリー
    dma_mapping: DmaMapping    # C9: DMA マッピング
    power: PowerDomains        # C10: 電力管理
    security: SecurityConfig   # C11: セキュリティ
    linker: LinkerInfo         # C13: リンカ情報
    debug: DebugConfig         # C14: デバッグ/トレース
```

### 4.2 データソースとカテゴリの対応

| カテゴリ | 主データソース | 補助データソース |
|---------|--------------|----------------|
| C1 コアペリフェラル | CMSIS ヘッダ (core_cm4.h) | — |
| C2 コアイントリンシクス | CMSIS ヘッダ | — |
| C3 コアシステム | CMSIS-Pack (.pdsc) | modm-devices |
| C4 ベクター | CMSIS ヘッダ (IRQn_Type) | SVD (部分) |
| C5 メモリマップ | CMSIS ヘッダ + リファレンスマニュアル | modm-devices |
| C6 ペリフェラルレジスタ | **SVD** (主) | CMSIS ヘッダ (補正) |
| C7 GPIO MUX | **STM32_open_pin_data** | CubeMX DB |
| C8 クロックツリー | CubeMX DB / modm-devices | — |
| C9 DMA マッピング | CubeMX DB / modm-devices | SVD (部分) |
| C10 電力管理 | SVD + リファレンスマニュアル | — |
| C11 セキュリティ | SVD + CMSIS ヘッダ | — |
| C12 デバイスメタ | CMSIS-Pack (.pdsc) | modm-devices |
| C13 リンカ/スタートアップ | MCU DB + テンプレート | — |
| C14 デバッグ/トレース | CMSIS ヘッダ + SVD | — |

---

## 5. 生成物の配置

### 5.1 方針: ソースツリーにコミット

生成物は **ソースツリーにコミットする** (ビルドディレクトリではない)。

| 方式 | 利点 | 欠点 |
|------|------|------|
| **ソースツリーにコミット** | ビルド時に生成ツール不要、差分レビュー可能、IDE 補完が効く | リポジトリサイズ増大 |
| ビルドディレクトリに生成 | リポジトリ軽量 | ビルド依存増、IDE 補完困難 |

根拠: modm, Rust PAC (svd2rust) も同様にコミットする方式を採用。
ユーザーが生成ツールをインストールせずにビルドできることを優先。

### 5.2 生成ファイル配置

```
lib/umiport/
├── include/umiport/
│   ├── pal/                        # 生成物ルートディレクトリ
│   │   ├── arm/cortex-m/           # L1/L2: アーキテクチャ・コアプロファイル
│   │   │   ├── nvic.hh             # C1: NVIC レジスタ定義
│   │   │   ├── scb.hh              # C1: SCB レジスタ定義
│   │   │   ├── systick.hh          # C1: SysTick レジスタ定義
│   │   │   ├── intrinsics.hh       # C2: コアイントリンシクス
│   │   │   └── core_config.hh      # C3: コアシステム定数
│   │   └── stm32f4/                # L3/L4: MCU ファミリ・バリアント
│   │       ├── vectors.hh          # C4: 割り込みベクター
│   │       ├── memory_map.hh       # C5: メモリマップ
│   │       ├── periph/             # C6: ペリフェラルレジスタ
│   │       │   ├── gpio.hh
│   │       │   ├── rcc.hh
│   │       │   ├── uart.hh
│   │       │   └── ...
│   │       ├── gpio_mux.hh         # C7: GPIO AF テーブル
│   │       ├── clock_tree.hh       # C8: クロックツリー定数
│   │       ├── dma_map.hh          # C9: DMA マッピング
│   │       ├── power.hh            # C10: 電力管理定数
│   │       ├── security.hh         # C11: セキュリティ定数
│   │       ├── device_meta.hh      # C12: デバイスメタ
│   │       └── debug.hh            # C14: デバッグ/トレース
│   └── ...
└── gen/                            # 生成ツール本体
    ├── umipal-gen                  # メイン生成スクリプト
    ├── parsers/                    # Stage 1: パーサー群
    ├── merger/                     # Stage 2: 統合ロジック
    ├── templates/                  # Stage 3: Jinja2 テンプレート
    └── data/                       # 入力データ (SVD, ピンデータ等)
```

---

## 6. 差分更新・再生成戦略

### 6.1 再生成トリガー

| トリガー | 操作 |
|---------|------|
| 新 MCU ファミリ追加 | `umipal-gen add-family stm32f7` |
| 新デバイスバリアント追加 | `umipal-gen add-device stm32f407vg` |
| データソース更新 | `umipal-gen update --source svd` |
| テンプレート修正 | `umipal-gen regenerate --all` |

### 6.2 差分検出方式

```
1. 生成前に既存ファイルの SHA-256 ハッシュを記録
2. 全ファイルをメモリ上で生成
3. ハッシュ比較 → 変更があったファイルのみディスクに書き出し
4. 変更サマリーを標準出力に表示

→ git diff で差分レビュー可能
→ 不要な差分ノイズを防止
```

### 6.3 手動パッチの保持

原則: **生成物に手動パッチを当てない**。
修正が必要な場合は、以下のいずれかで対応:

1. データソース側にパッチファイル (SVD パッチ、modm 方式) を用意
2. テンプレート側にカスタマイズポイントを追加
3. 生成物とは別の手書きオーバーライドヘッダを用意

---

## 7. xmake との統合

### 7.1 ビルドフロー内の位置

通常ビルドでは生成済みファイルをそのまま使用し、生成ツールは実行しない。

```
通常ビルド:  xmake build <target>
  → 生成済み pal/ ヘッダを include して通常コンパイル
  → Python 不要

PAL 再生成: xmake pal-gen [--family stm32f4]
  → Python スクリプト実行
  → 生成物をソースツリーに書き出し
  → git diff で確認 → git commit
```

### 7.2 xmake カスタムタスク

```lua
-- xmake.lua
task("pal-gen")
    on_run(function ()
        os.execv("python3", {"lib/umiport/gen/umipal-gen", "generate", "--all"})
    end)
    set_menu {
        usage = "xmake pal-gen [options]",
        description = "Regenerate PAL hardware definition headers",
        options = {
            {"f", "family", "kv", nil, "Target MCU family (e.g. stm32f4)"},
            {"d", "device", "kv", nil, "Target device (e.g. stm32f407vg)"},
        }
    }
```

---

## 8. 段階的実装計画

| Phase | 対象 | 入力 | 出力 |
|-------|------|------|------|
| **Phase 1** | C6 ペリフェラルレジスタ (STM32F4) | SVD | umimmio 型定義ヘッダ |
| **Phase 2** | C4 ベクター + C5 メモリマップ | CMSIS ヘッダ | constexpr テーブル + リンカ定数 |
| **Phase 3** | C7 GPIO MUX + C8 クロックツリー | STM32_open_pin_data + CubeMX | AF テーブル + クロック定数 |
| **Phase 4** | C1-C3 コア + C9-C14 残り | 複合ソース | 全カテゴリ生成完了 |
| **Phase 5** | 他プラットフォーム (RP2040, ESP32) | 各ベンダー SDK | マルチプラットフォーム対応 |

---

## 9. 未解決の設計課題

| # | 課題 | 選択肢 | 備考 |
|---|------|--------|------|
| 1 | SVD パッチの管理方式 | YAML パッチ (svdtools 方式) vs JSON パッチ | stm32-rs/stm32-rs のパッチ資産を流用可能か |
| 2 | modm-devices の活用範囲 | データソースとして直接利用 vs 参考のみ | ライセンス: MPL-2.0 |
| 3 | 生成テンプレートのテスト | 生成物のコンパイルテスト vs ゴールデンファイル比較 | CI での回帰検出方法 |
| 4 | C++ コード生成のフォーマット | 生成時に clang-format 適用 vs テンプレートで整形済み | フォーマッタ依存を避けたい |
| 5 | マルチプラットフォーム共通化 | 全プラットフォーム統一パーサー vs プラットフォーム別 | 入力フォーマットが根本的に異なる |
