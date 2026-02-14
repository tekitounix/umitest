# CAT_E: HAL/ドライバ設計 — 統合内容要約

**カテゴリ:** E. HAL/ドライバ設計
**配置先:** `lib/docs/design/`（既存構成維持）
**前提仕様:** [LIBRARY_SPEC.md](../LIBRARY_SPEC.md) v1.3.0 / [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) v1.1.0
**関連:** [CONSOLIDATION_PLAN.md](CONSOLIDATION_PLAN.md) | [DOCUMENT_INVENTORY.md](DOCUMENT_INVENTORY.md)

> **注意:** `lib/docs/design/` は Stage B Phase 0 で `lib/_archive/docs/design/` に退避後、新 `lib/docs/design/` にコピー再配置される（CONSOLIDATION_PLAN §5.2）。高品質（監査スコア 100%）なため内容はそのまま継承される。

---

## 1. カテゴリ概要

UMI の HAL Concept、PAL（Peripheral Access Layer）、ボード設定、ビルドシステム、コード生成パイプラインに関する設計文書群。既存の `lib/docs/design/` ディレクトリに体系的に整理されており、最も品質の高いカテゴリ。

**対象読者:** カーネル開発者、移植者、ドライバ開発者
**特徴:** 60+ ファイルの大規模設計文書群だが、ドメイン別に明確に構成されており、README.md がナビゲーションハブとして機能

---

## 2. 所属ドキュメント一覧

### 2.1 design/ 直下 — 実装設計（9ファイル）

| # | ファイル | 行数 | ステータス | 統廃合アクション |
|---|---------|------|----------|-----------------|
| 1 | README.md | ~213 | ★ 確定 | **保持** — ナビゲーションハブ |
| 2 | integration.md | ~374 | 設計中 | **保持** — ビルド・生成・ドライバの密結合点 |
| 3 | build_system.md | ~269 | 設計中 | **保持** — xmake 設計全体 |
| 4 | codegen_pipeline.md | ~296 | 設計中 | **保持** — コード生成の実装仕様 |
| 5 | testing_hw.md | ~294 | 設計中 | **保持** — HW層テスト戦略 |
| 6 | implementation_roadmap.md | ~216 | 設計中(90%) | **保持** — Phase 0-5 ロードマップ |
| 7 | source_of_truth_unification.md | ~200 | 設計中 | **保持** — 統合プロセス定義 |
| 8 | init_sequence.md | ~312 | 設計中 | **保持** — 6段階初期化シーケンス |
| 9 | interrupt_model.md | ~322 | 設計中 | **保持** — 割り込み・DMA モデル |

### 2.2 foundations/ — 設計基盤（3ファイル）

| # | ファイル | 行数 | ステータス | 統廃合アクション |
|---|---------|------|----------|-----------------|
| 10 | problem_statement.md | ~448 | ★ 完了 | **保持** — 問題診断の根拠 |
| 11 | comparative_analysis.md | ~341 | ★ 完了 | **保持** — 13+ FW 横断比較 |
| 12 | architecture.md | ~136 | ★ 確定 | **保持** — 5不変原則 |

### 2.3 hal/ — HAL Concept 設計（1ファイル）

| # | ファイル | 行数 | ステータス | 統廃合アクション |
|---|---------|------|----------|-----------------|
| 13 | concept_design.md | ~211 | ★ 確定 | **保持** — HAL Concept 仕様 |

### 2.4 board/ — ボード設定（4ファイル）

| # | ファイル | 行数 | ステータス | 統廃合アクション |
|---|---------|------|----------|-----------------|
| 14 | architecture.md | ~365 | ★ 確定 | **保持** — 二重構造設計 |
| 15 | config.md | ~200 | ★ | **保持** — 設定アーキテクチャ |
| 16 | config_analysis.md | ~200 | ★ | **保持** — 横断比較 |
| 17 | project_structure.md | ~150 | ★ | **保持** — プロジェクト構成 |

### 2.5 pal/ — PAL 設計（6 + 14カテゴリ）

| # | ファイル | 行数 | ステータス | 統廃合アクション |
|---|---------|------|----------|-----------------|
| 18 | 00_OVERVIEW.md | ~133 | ★ 確定 | **保持** — PAL 全体マップ |
| 19 | 01_LAYER_MODEL.md | ~150 | ★ | **保持** — 4層モデル |
| 20 | 02_CATEGORY_INDEX.md | ~200 | ★ | **保持** — C1-C14 索引 |
| 21 | 03_ARCHITECTURE.md | ~670 | 設計中 | **保持** — PAL 実装設計 |
| 22 | 04_ANALYSIS.md | ~200 | ★ | **保持** — 既存アプローチ分析 |
| 23 | 05_DATA_SOURCES.md | ~200 | ★ | **保持** — データソース参照 |
| 24 | categories/C01-C14 | 14ファイル | ★ | **保持** — 全カテゴリ詳細 |

### 2.6 pipeline/ — HW データパイプライン（1ファイル）

| # | ファイル | 行数 | ステータス | 統廃合アクション |
|---|---------|------|----------|-----------------|
| 25 | hw_data_pipeline.md | ~651 | ★ 完了 | **保持** — データ統合パイプライン設計 |

### 2.7 research/ — フレームワーク個別調査（14ファイル）

| # | ファイル | 有用性 | 統廃合アクション |
|---|---------|--------|-----------------|
| 26-39 | zephyr, modm, esp_idf, rust_embedded, cmsis_pack, mbed_os, platformio, arduino, hal_interfaces, other_frameworks, board_config_* | ◆ | **保持** — 参考資料 |

### 2.8 archive/ — 設計プロセスの記録（32ファイル）

| # | ファイル | 有用性 | 統廃合アクション |
|---|---------|--------|-----------------|
| 40-52 | archive/ 直下 | ▽ | **保持** — foundations/ の根拠資料 |
| 53-71 | archive/review/ | ▽ | **保持** — AIレビュー記録 |

---

## 3. 主要ドキュメント内容要約

### 3.1 foundations/architecture.md — 設計不変原則

**ステータス:** 確定版 | **全設計の根拠**

5つの不変原則:
1. **ライブラリはハードウェアを知らない** — 直接依存禁止
2. **統合は board 層のみ** — platform.hh での合成
3. **#ifdef 禁止** — Concept + テンプレートで解決
4. **出力経路は link-time 注入** — コンパイル時依存なし
5. **ビルドシステムがドライバを選択** — MCU DB 駆動

7パッケージ構成: umihal, umiport, umidevice, umimmio, umiboard（board rule）+ アプリ層

---

### 3.2 integration.md — 統合設計（★最初に読む）

**ステータス:** 設計中(70%)

ビルド・コード生成・ドライバの3層の密結合ポイント。

**5つの統合ポイント:**
1. ディレクトリ構造とインクルードパス規約
2. MCU DB スキーマ（`family` フィールドがキー）
3. ボード → MCU → PAL の解決チェーン
4. 生成タイミングと整合性
5. 命名規約の統一

**未解決課題:** 5個（Phase 3 以降で決定）

---

### 3.3 hal/concept_design.md — HAL Concept 設計

**ステータス:** 確定版

C++23 Concept による HAL 契約定義の確定仕様。

**主要内容:**
- **階層化パターン** — Basic → Extended → Full（例: CodecBasic → CodecWithVolume → AudioCodec）
- **sync/async 分離** — 同期・非同期を Concept レベルで分離
- **エラー型** — `Result<T>` ベース
- **Platform Concept** — init(), configure_clock() 等の完全仕様
- **Transport Concept** — I2C/SPI/DMA の詳細設計
- **GPIO** — InputPin/OutputPin に分離（型安全）

---

### 3.4 board/architecture.md — ボード設定アーキテクチャ

**ステータス:** 確定版

ボード定義の二重構造設計。

**主要内容:**
- **Lua 側** — ビルド設定、MCU 指定、ツールチェイン選択
- **C++ 側** — board.hh/platform.hh による型定義
- **継承メカニズム** — `extends` による deep_merge（差分記述）
- **MCU DB** — Single Source of Truth、memory.ld 自動生成
- **xmake ルール** — umiport.board rule のライフサイクル

---

### 3.5 pal/03_ARCHITECTURE.md — PAL アーキテクチャ

**ステータス:** 設計中(80%) | **行数:** ~670行

PAL 実装の中核設計。

**主要内容:**
- **umimmio と PAL の責務分離** — umimmio = 型安全レジスタ基盤、PAL = MCU 固有定義
- **段階的アプローチ** — Phase 1: 手書き → Phase 2: SVD 生成
- **インスタンス化パターン** — `using GPIOA = GpioPort<0x4002'0000>;`
- **3つの列挙値パターン** — namespace enum, typed enum, bit field
- **ドライバからの利用パターン** — PAL 定義 → ドライバテンプレート → ボード選択

---

### 3.6 pipeline/hw_data_pipeline.md — HW データパイプライン

**ステータス:** 完了 | **行数:** ~651行

SVD/CMSIS/CubeMX 等の複数ソースからの統合パイプライン設計。

**主要内容:**
- **8カテゴリの必要データ** — レジスタ、ベクター、メモリ、GPIO AF、クロック、DMA、コアペリフェラル、デバイスメタ
- **データソース分析** — SVD は全定義の約40%のみ。CMSIS ヘッダ + CubeMX + CMSIS-Pack で補完
- **modm 方式の発見** — 「コンパイルして実行」による CMSIS ヘッダ抽出（テキスト処理の限界を超える）
- **embassy stm32-data** — 最も成熟した複数ソース統合モデル
- **Unified Device Model** — JSON 中間表現の設計
- **AI エージェント活用** — SVD パッチ生成、コアペリフェラル DB 構築の半自動化

---

### 3.7 implementation_roadmap.md — 実装ロードマップ

**ステータス:** 設計中(90%)

垂直スライス優先の段階的実装計画。

| Phase | 目標 | 内容 |
|-------|------|------|
| 0 | 基盤確立 | umimmio + 手書き PAL + board.lua |
| 1 | 垂直スライス | STM32F4-Discovery で LED + UART 動作 |
| 2 | テスト自動化 | 4層テストピラミッド構築 |
| 3 | 生成パイプライン | SVD → PAL コード生成 |
| 4 | 実用ドライバ | SPI/I2C/Timer/ADC |
| 5 | マルチプラットフォーム | STM32H7, NXP, RP2040 |

---

## 4. カテゴリ内の関連性マップ

```
foundations/ ← 設計の根拠（Why）
├── problem_statement.md ← 現行の問題分析
├── comparative_analysis.md ← 13+ FW 比較
└── architecture.md ← 5 不変原則（全文書の前提）
    │
    ├── hal/concept_design.md ← HAL 契約（What）
    │
    ├── board/architecture.md ← ボード定義設計（How）
    │
    ├── pal/ ← ペリフェラル定義（What + How）
    │   ├── 00_OVERVIEW → 01_LAYER → 02_INDEX
    │   ├── 03_ARCHITECTURE ← PAL 実装設計
    │   ├── 04_ANALYSIS, 05_DATA_SOURCES
    │   └── categories/C01-C14 ← 14カテゴリ詳細
    │
    └── pipeline/hw_data_pipeline.md ← データ統合（Source）
        │
design/ (直下) ← 実装統合設計（How）
├── integration.md ← ★ 3層の密結合点（最初に読む）
├── build_system.md ← xmake 設計
├── codegen_pipeline.md ← コード生成実装
├── testing_hw.md ← テスト戦略
├── implementation_roadmap.md ← Phase 0-5 計画
├── init_sequence.md ← 起動シーケンス
├── interrupt_model.md ← 割り込みモデル
└── source_of_truth_unification.md ← 統合プロセス

research/ ← 参考資料（Reference）
└── 14個のフレームワーク調査

archive/ ← 設計プロセス記録（History）
├── 旧設計文書
└── AI レビュー記録
```

---

## 5. 統廃合アクション

### 変更不要（そのまま保持）

**本カテゴリは大規模な統廃合が不要。** ドメイン別に明確に構成されており、README.md が適切なナビゲーションを提供している。

| 領域 | ファイル数 | 状態 |
|------|-----------|------|
| design/ 直下 | 9 | 保持 |
| foundations/ | 3 | 保持 |
| hal/ | 1 | 保持 |
| board/ | 4 | 保持 |
| pal/ | 20 | 保持 |
| pipeline/ | 1 | 保持 |
| research/ | 14 | 保持（参考） |
| archive/ | 32 | 保持（記録） |

### 推奨する軽微な改善

| 改善項目 | 対象 | 内容 |
|---------|------|------|
| 「設計中」文書のステータス更新 | 8ファイル | Phase 完了時に確定版に昇格 |
| archive/review/ の整理 | 19ファイル | 有用なレビュー結果を foundations/ に統合検討 |

---

## 6. 品質評価

| 観点 | 評価 | コメント |
|------|------|---------|
| 網羅性 | ★★★★★ | HAL/PAL/Board/Build/Test の全側面をカバー |
| 一貫性 | ★★★★★ | ドメイン別構成、統一フォーマット |
| 更新頻度 | ★★★★★ | 2026-02 に集中的に作成・更新 |
| 読みやすさ | ★★★★★ | README.md の目的別ナビゲーション、ASCII 図、コード例 |
| コードとの整合 | ★★★★☆ | 多くが設計段階。実装進行に伴い検証が必要 |
| 相互参照 | ★★★★★ | 文書間のリンクが充実 |

---

## 7. 推奨事項

1. **本カテゴリは最高品質** — 大規模な統廃合は不要。現在の構成を維持
2. **Phase 進行に伴うステータス更新** — 各 Phase 完了時に「設計中」→「確定版」への昇格
3. **未解決課題の追跡** — integration.md(5個), build_system.md(5個), init_sequence.md(5個) 等の未解決課題を Phase 別に解決
4. **入門ドキュメントの検討** — `lib/docs/design/GETTING_STARTED.md` の新規作成で、5分で全体像を把握できる導線を追加
5. **archive/review/ の選別的統合** — 有用な AI レビュー結果は foundations/ への統合を検討（ただし優先度は低い）
