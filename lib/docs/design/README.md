# UMI 設計ドキュメント

本ディレクトリは、UMI の HAL / ドライバ / ビルドシステム設計に関する議論・調査・意思決定を集約する。

---

## ドメイン別構成

```
design/
├── foundations/          設計基盤 — 問題定義 → 比較分析 → アーキテクチャ
├── hal/                 HAL Concept 設計 — umihal の型契約定義
├── board/               ボード設定 — 設定局所化、継承、プロジェクト構成
├── pal/                 PAL (Peripheral Access Layer) — MCU 固有ハードウェア定義
├── pipeline/            HW データパイプライン — データソース統合と生成戦略
├── research/            フレームワーク個別調査 (参照資料)
├── archive/             設計プロセスの生データ・レビュー記録
│
│   ── 実装設計 (design/ 直下) ──
├── integration.md       ビルド・生成・ドライバ統合設計 — 3者の密結合を一箇所で定義 ★最初に読む
├── build_system.md      ビルドシステム設計 — xmake ルール構成、クロスコンパイル、ターゲット管理
├── codegen_pipeline.md  コード生成パイプライン — ツール選定、中間表現、生成物配置
├── testing_hw.md        ハードウェア層テスト戦略 — Mock/Renode/実機の4層テストピラミッド
├── implementation_roadmap.md  実装ロードマップ — Phase 0-5 の段階的実装計画
├── source_of_truth_unification.md  統合・圧縮プロセス — 離散情報を収束させる運用方法
├── init_sequence.md     初期化シーケンス — Reset_Handler → main() の実行フロー設計
└── interrupt_model.md   割り込み・イベントモデル — ディスパッチ、DMA 通知、NVIC 優先度
```

---

## 読み方ガイド

| やりたいこと | 読むべき文書 |
|-------------|-------------|
| 全体アーキテクチャを把握したい | [foundations/architecture.md](foundations/architecture.md) |
| 現行設計の問題を把握したい | [foundations/problem_statement.md](foundations/problem_statement.md) |
| フレームワーク横断比較を見たい | [foundations/comparative_analysis.md](foundations/comparative_analysis.md) |
| HAL Concept の設計を知りたい | [hal/concept_design.md](hal/concept_design.md) |
| ボード設定の仕組みを知りたい | [board/architecture.md](board/architecture.md) |
| ボード設定スキーマの比較根拠 | [board/config.md](board/config.md) + [board/config_analysis.md](board/config_analysis.md) |
| ユーザーのプロジェクト構成 | [board/project_structure.md](board/project_structure.md) |
| PAL の全体像を知りたい | [pal/00_OVERVIEW.md](pal/00_OVERVIEW.md) |
| HW データ生成パイプライン | [pipeline/hw_data_pipeline.md](pipeline/hw_data_pipeline.md) |
| ビルド・生成・ドライバの関係を理解したい | [integration.md](integration.md) |
| ビルドシステムの設計を知りたい | [build_system.md](build_system.md) |
| コード生成の実装方法を知りたい | [codegen_pipeline.md](codegen_pipeline.md) |
| HW 層のテスト方法を知りたい | [testing_hw.md](testing_hw.md) |
| 実装の優先順位を知りたい | [implementation_roadmap.md](implementation_roadmap.md) |
| 離散情報を統合しながら進めたい | [source_of_truth_unification.md](source_of_truth_unification.md) |
| 起動シーケンスを知りたい | [init_sequence.md](init_sequence.md) |
| 割り込み・DMA モデルを知りたい | [interrupt_model.md](interrupt_model.md) |
| 特定フレームワークの詳細 | [research/](research/) 内の個別ファイル |
| 議論の生データ・レビュー記録 | [archive/](archive/) |

---

## 設計領域と関係

```
┌─────────────────────────────────────────────────────────┐
│                    Build System (xmake)                  │
│  umiport.board rule / MCU DB / memory.ld generation     │
│                                                         │
│   「どのドライバ実装を選択・コンパイルするか」              │
└────────────┬───────────────────────────┬────────────────┘
             │ selects                   │ configures
             ▼                           ▼
┌──────────────────────┐    ┌──────────────────────────┐
│   HAL (umihal)       │    │   Driver (umiport)       │
│                      │    │                           │
│ C++23 concepts で    │◄───│ MCU 固有のドライバ実装    │
│ HW 契約を定義        │    │ HAL concepts を satisfy   │
│ (実装なし・型のみ)   │    │                           │
└──────────────────────┘    └──────────────────────────┘
  defines contracts           provides implementations

                             ┌──────────────────────────┐
                             │   Device (umidevice)     │
                             │                           │
                             │ MCU 外部デバイス定義      │
                             │ (I2C/SPI センサ、DAC 等)  │
                             └──────────────────────────┘
                               external device drivers
```

**依存の流れ:** アプリケーション → Driver (umiport) → HAL (concepts)
**選択の流れ:** Build System → Driver 実装を選択 → HAL contracts を satisfy

---

## ドキュメント一覧

### foundations/ — 設計基盤

| 文書 | 概要 |
|------|------|
| [problem_statement.md](foundations/problem_statement.md) | 現行/旧アーキテクチャの課題整理と設計要件の定義 |
| [comparative_analysis.md](foundations/comparative_analysis.md) | 13+ フレームワークの横断比較、パターン分類・トレードオフ比較 |
| [architecture.md](foundations/architecture.md) | 全調査を統合した UMI の理想アーキテクチャ (確定版) |

### hal/ — HAL Concept 設計

| 文書 | 概要 |
|------|------|
| [concept_design.md](hal/concept_design.md) | HAL Concept の設計方針、エラー型、GPIO/UART/Transport/Platform の定義 |

### board/ — ボード設定

| 文書 | 概要 |
|------|------|
| [architecture.md](board/architecture.md) | ボード定義二重構造、継承メカニズム、MCU DB、xmake ルール設計 |
| [config.md](board/config.md) | ボード設定アーキテクチャの詳細 — 設定の統一と局所化 |
| [config_analysis.md](board/config_analysis.md) | 全フレームワークのボード設定スキーマ横断比較 |
| [project_structure.md](board/project_structure.md) | ユーザープロジェクト構成の推奨パターン |

### pal/ — PAL (Peripheral Access Layer)

| 文書 | 概要 |
|------|------|
| [00_OVERVIEW.md](pal/00_OVERVIEW.md) | PAL ドキュメント全体マップ、設計原則、対象プラットフォーム |
| [01_LAYER_MODEL.md](pal/01_LAYER_MODEL.md) | 4 層モデル — ハードウェア定義のスコープ分類 |
| [02_CATEGORY_INDEX.md](pal/02_CATEGORY_INDEX.md) | カテゴリ一覧 — C1–C14 の全体マトリクスと完全性チェックリスト |
| [03_ARCHITECTURE.md](pal/03_ARCHITECTURE.md) | PAL アーキテクチャ提案 — コード構造と API 設計 |
| [04_ANALYSIS.md](pal/04_ANALYSIS.md) | 既存 PAL アプローチの横断分析 |
| [05_DATA_SOURCES.md](pal/05_DATA_SOURCES.md) | データソース完全リファレンス — カテゴリ × プラットフォーム別 |
| [categories/](pal/categories/) | C01–C14 カテゴリ別詳細 + 生成ヘッダのコード例 |

### pipeline/ — HW データパイプライン

| 文書 | 概要 |
|------|------|
| [hw_data_pipeline.md](pipeline/hw_data_pipeline.md) | SVD/CMSIS/CubeMX 等の複数ソース統合パイプライン設計 |

### 実装設計 (design/ 直下)

| 文書 | 概要 |
|------|------|
| [integration.md](integration.md) | ビルド・生成・ドライバの密結合ポイント定義 (パス規約、MCU DB スキーマ、解決チェーン) |
| [build_system.md](build_system.md) | xmake ルール構成、クロスコンパイル、ターゲット管理、synthernet 連携 |
| [codegen_pipeline.md](codegen_pipeline.md) | PAL コード生成ツール選定、中間表現、生成物配置、差分更新戦略 |
| [testing_hw.md](testing_hw.md) | 4層テストピラミッド (static/mock/Renode/実機)、HAL コンプライアンステスト |
| [implementation_roadmap.md](implementation_roadmap.md) | Phase 0-5 の段階的実装計画、完了定義、リスクと緩和策 |
| [source_of_truth_unification.md](source_of_truth_unification.md) | 離散情報の統合・圧縮ループ、矛盾管理、次スライス選定の運用方法 |
| [init_sequence.md](init_sequence.md) | Reset_Handler → main() の6段階初期化シーケンス設計 |
| [interrupt_model.md](interrupt_model.md) | 割り込みディスパッチ、DMA ダブルバッファ、NVIC 優先度ポリシー |

### research/ — フレームワーク個別調査

| 文書 | 概要 |
|------|------|
| [zephyr.md](research/zephyr.md) | Zephyr RTOS 調査 |
| [board_config_zephyr.md](research/board_config_zephyr.md) | Zephyr DTS/binding/board.yml 詳細 |
| [modm.md](research/modm.md) | modm フレームワーク調査 |
| [board_config_modm_cmsis_mbed_rust.md](research/board_config_modm_cmsis_mbed_rust.md) | modm/CMSIS-Pack/Mbed OS/Rust 比較 |
| [board_config_esp_platformio_arduino.md](research/board_config_esp_platformio_arduino.md) | ESP-IDF/PlatformIO/Arduino 調査 |
| [board_config_mcu_families.md](research/board_config_mcu_families.md) | MCU ファミリー間設定差異 |
| [esp_idf.md](research/esp_idf.md) | ESP-IDF 調査 |
| [rust_embedded.md](research/rust_embedded.md) | Rust embedded エコシステム |
| [cmsis_pack.md](research/cmsis_pack.md) | CMSIS-Pack 標準 |
| [mbed_os.md](research/mbed_os.md) | Mbed OS 調査 |
| [platformio.md](research/platformio.md) | PlatformIO 調査 |
| [arduino.md](research/arduino.md) | Arduino 調査 |
| [hal_interfaces.md](research/hal_interfaces.md) | HAL インターフェース設計パターン |
| [other_frameworks.md](research/other_frameworks.md) | その他フレームワーク |

### archive/ — アーカイブ

設計プロセスで生成された生データ・レビュー記録・中間成果物。
`foundations/` の根拠資料であり、通常は参照不要。

---

## 設計領域ごとの主要な問い

### HAL (umihal)

- concept 粒度: モノリシック vs コンポーザブル → [hal/concept_design.md](hal/concept_design.md)
- sync / async の分離方法
- GPIO の入出力型安全性
- エラーモデル (`Result<T>` の設計)

### Driver (umiport)

- パッケージ粒度: MCU / board / device の分割 → [foundations/architecture.md](foundations/architecture.md)
- ボード定義フォーマット: Lua + C++ デュアル構造 → [board/architecture.md](board/architecture.md)
- データ継承モデル (`extends` によるボード派生)
- startup / linker script の配置

### Device (umidevice)

- MCU 外部デバイスの定義 (I2C/SPI センサ、DAC 等)
- MCU と同列のレイヤ配置 (ドライバではなくデバイス定義)

### Build System (xmake)

- ビルドシステム全体設計 → [build_system.md](build_system.md)
- rule ライフサイクルの実行順序 → [board/architecture.md](board/architecture.md) §4
- ハードウェアデータの single source of truth
- ボード継承の Lua `extends` 実装
- PAL コード生成パイプライン → [codegen_pipeline.md](codegen_pipeline.md)

### Runtime (初期化・割り込み)

- Reset → main() の初期化シーケンス → [init_sequence.md](init_sequence.md)
- 割り込みディスパッチ・DMA 通知モデル → [interrupt_model.md](interrupt_model.md)
- NVIC 優先度ポリシー → [interrupt_model.md](interrupt_model.md) §4

### 実装計画

- 段階的実装ロードマップ (Phase 0-5) → [implementation_roadmap.md](implementation_roadmap.md)
- ハードウェア層テスト戦略 → [testing_hw.md](testing_hw.md)
