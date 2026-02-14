# CAT_D: 開発ガイド — 統合内容要約

**カテゴリ:** D. 開発ガイド
**配置先:** `lib/docs/`（ライブラリ標準・ガイド）+ `docs/dev/`
**前提仕様:** [LIBRARY_SPEC.md](../LIBRARY_SPEC.md) v1.3.0 / [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) v1.1.0
**関連:** [CONSOLIDATION_PLAN.md](CONSOLIDATION_PLAN.md) | [DOCUMENT_INVENTORY.md](DOCUMENT_INVENTORY.md)

> **注意:** `lib/docs/` は Stage B Phase 0 で `lib/_archive/docs/` に退避後、新 `lib/docs/` にコピー再配置される（CONSOLIDATION_PLAN §5.2）。高品質なため内容はほぼそのまま継承される。

---

## 1. カテゴリ概要

開発者の日常作業を支援する標準、ガイド、ツール設定文書群。コーディング規約、ビルド手順、テスト戦略、デバッグ手法、リリースプロセス、コード品質ツール設定を含む。

**対象読者:** 全開発者
**主要な問題:** コーディング関連文書（CODING_RULE, GUIDELINE, clang_tooling_evaluation, CODE_QUALITY_GUIDE）が4箇所に分散。clang 関連が3ファイルに散在。

---

## 2. 所属ドキュメント一覧

### 2.1 lib/docs/standards/ — 標準規約（3ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 1 | CODING_RULE.md | ~429 | ★ | **保持（正本）** CLAUDE.md から参照 |
| 2 | LIBRARY_SPEC.md | ~200 | ★ | **保持（正本）** ※ lib/docs/ 版。docs/plan/LIBRARY_SPEC.md v1.3.0 とは別文書 |
| 3 | API_COMMENT_RULE.md | ~150 | ★ | **保持（正本）** |

### 2.2 lib/docs/guides/ — ガイド群（7ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 4 | GETTING_STARTED.md | ~200 | ★ | **保持** — Phase 1-4 段階的作成ガイド |
| 5 | BUILD_GUIDE.md | ~150 | ★ | **保持** — xmake 全コマンドリファレンス |
| 6 | TESTING_GUIDE.md | ~200 | ★ | **保持** CLAUDE.md から参照 |
| 7 | DEBUGGING_GUIDE.md | ~847 | ★ | **保持** CLAUDE.md から参照 |
| 8 | RELEASE_GUIDE.md | ~150 | ★ | **保持** — RELEASE.md と補完関係 |
| 9 | CODE_QUALITY_GUIDE.md | ~200 | ★ | **保持** — clang 関連文書の統合先 |
| 10 | API_DOCS_GUIDE.md | ~150 | ★ | **保持** |

### 2.3 lib/docs/INDEX.md — ナビゲーション

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 11 | INDEX.md | ~42 | ★ | **保持** — 全ガイドへの導線 |

### 2.4 docs/dev/ — 開発者向け文書（5ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 12 | GUIDELINE.md | ~200 | ▽ | **名前変更** → DESIGN_PATTERNS.md |
| 13 | IMPLEMENTATION_PLAN.md | ~436 | ◆ | **保持** — Phase 0-7 実装計画 |
| 14 | SIMULATION.md | ~220 | ◆ | **保持** — docs/dev/ に現状維持 |
| 15 | RUST.md | ~128 | ◆ | **保持** — 言語比較参考資料 |
| 16 | DEBUG_VSCODE_CORTEX_DEBUG.md | ~182 | ★ | **保持** — DEBUGGING_GUIDE と補完 |

### 2.5 docs/ 直下 — ツール設定文書（3ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 17 | clang_tooling_evaluation.md | ~387 | ◆ | **統合** → CODE_QUALITY_GUIDE.md |
| 18 | CLANG_TIDY_SETUP.md | ~100 | ◆ | **統合** → CODE_QUALITY_GUIDE.md |
| 19 | CLANG_ARM_MULTILIB_WORKAROUND.md | ~50 | ◆ | **統合** → CODE_QUALITY_GUIDE.md |

### 2.6 ルート — リリース関連

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 20 | RELEASE.md | ~100 | ★ | **保持** — リリースポリシー |

---

## 3. ドキュメント別内容要約

### 3.1 CODING_RULE.md — コーディングスタイルガイド（正本）

**行数:** ~429行

.clang-format / .clang-tidy / .clangd の設定ファイルの**正本**。

**主要内容:**
- **.clang-format 全文** — LLVM ベース、4-space indent、120 char limit、PointerAlignment: Left
- **.clang-tidy 設定** — 有効化/無効化チェック一覧
- **.clangd 設定** — LSP サーバー設定
- **命名規約** — lower_case(関数/変数)、CamelCase(型)、UPPER_CASE(enum値)
- **設計原則** — constexpr 優先、inline 冗長回避、メンバ変数プレフィックスなし

**CLAUDE.md との関係:** CLAUDE.md のコードスタイルセクションはこの文書のサブセット

---

### 3.2 LIBRARY_SPEC.md — ライブラリ標準仕様

**バージョン:** 2.0.0

全ライブラリ共通の構造・規約・著作権表記。

**主要内容:**
- **設計原則** — ソースツリー最小、共通事項集約、バージョンは git タグ
- **標準ディレクトリ構造** — include/, src/, test/, docs/, xmake.lua
- **必須ファイル** — README.md, docs/DESIGN.md
- **推奨ファイル** — docs/ja/README.md, docs/INDEX.md, docs/TESTING.md
- **著作権表記** — MIT ライセンス、ヘッダテンプレート

---

### 3.3 API_COMMENT_RULE.md — API コメント規約

Doxygen コメントの書き方ルール。

**主要内容:**
- **必須タグ** — @file, @brief, @tparam, @param, @return
- **推奨タグ** — @warning, @note, @pre, @post
- **コード例** — テンプレート関数、クラス、名前空間のコメントパターン

---

### 3.4 GETTING_STARTED.md — 新規ライブラリ作成ガイド

Phase 1-4 の段階的作成手順。

**主要内容:**
- **Phase 1** — 最小構成（README.md + include/ + xmake.lua）
- **Phase 2** — テスト追加（test/ + TESTING.md）
- **Phase 3** — ドキュメント整備（DESIGN.md + ja/README.md + INDEX.md）
- **Phase 4** — 品質基準達成（umibench レベル）

---

### 3.5 BUILD_GUIDE.md — ビルドガイド

xmake の全コマンドリファレンス。

**主要内容:**
- **基本フロー** — configure → build → run → test → clean
- **コード品質** — xmake format（フォーマッタ）
- **クロスコンパイル** — ARM 組込みターゲット設定
- **WASM ビルド** — headless_webhost

---

### 3.6 TESTING_GUIDE.md — テスト戦略

テストの書き方と実行方法。CLAUDE.md から参照。

---

### 3.7 DEBUGGING_GUIDE.md — デバッグガイド

**行数:** ~847行の大規模ガイド

pyOCD, GDB, RTT を使ったデバッグ手法。CLAUDE.md から参照。

**主要内容:**
- **pyOCD セットアップ** — インストール、接続、フラッシュ
- **GDB デバッグ** — ブレークポイント、ステップ実行、変数表示
- **RTT 出力** — Segger RTT 互換の umirtm 出力
- **VSCode 統合** — launch.json 設定

**docs/dev/DEBUG_VSCODE_CORTEX_DEBUG.md との関係:** 補完関係。DEBUGGING_GUIDE は pyOCD ベース、Cortex-Debug は VSCode 拡張ベース

---

### 3.8 GUIDELINE.md → DESIGN_PATTERNS.md（名前変更）

C++ 実装パターン集。名前が「ガイドライン」で誤解を招く。

**主要内容:**
- **Data/State 分離** — 共有データと処理状態の明確な分離
- **ダブルバッファリング** — リアルタイム安全なデータ交換
- **Arena アロケーション** — ヒープを避ける固定メモリ割当
- **SPSC 通信** — Lock-free 単一生産者/単一消費者キュー

**CODING_RULE.md との関係:** 異なる内容。CODING_RULE = スタイル規約、GUIDELINE = 設計パターン

---

### 3.9 clang 関連3ファイル（統合対象）

| ファイル | 内容 | 行数 |
|---------|------|------|
| clang_tooling_evaluation.md | .clang-format/.clang-tidy/.clangd の詳細評価と推奨修正 | ~387 |
| CLANG_TIDY_SETUP.md | clang-tidy セットアップ、multilib 問題の3解決策 | ~100 |
| CLANG_ARM_MULTILIB_WORKAROUND.md | clang-arm 21.x multilib.yaml 互換性ワークアラウンド | ~50 |

**統合先:** lib/docs/guides/CODE_QUALITY_GUIDE.md — ツール設定を1箇所に集約

---

### 3.10 RELEASE.md — リリースポリシー

**主要内容:**
- **バージョニング** — セマンティックバージョニング
- **公開ライブラリ一覧** — umibench, umimmio, umitest, umirtm
- **リリースチェックリスト** — テスト通過、ドキュメント更新、タグ作成

**RELEASE_GUIDE.md との関係:** RELEASE.md = ポリシー（What）、RELEASE_GUIDE.md = 手順（How）

---

## 4. カテゴリ内の関連性マップ

```
lib/docs/ ← 集約場所
├── INDEX.md ← ナビゲーション（全ガイドへの導線）
│
├── standards/ ← 規約（What）
│   ├── CODING_RULE.md ← コードスタイル正本
│   ├── LIBRARY_SPEC.md ← ディレクトリ構造・必須ファイル
│   └── API_COMMENT_RULE.md ← Doxygen コメント
│
└── guides/ ← ガイド（How）
    ├── GETTING_STARTED.md ← 新規ライブラリ作成
    ├── BUILD_GUIDE.md ← ビルド・テスト・デプロイ
    ├── TESTING_GUIDE.md ← テスト戦略
    ├── DEBUGGING_GUIDE.md ← デバッグ手法
    │     └── docs/dev/DEBUG_VSCODE_CORTEX_DEBUG.md（補完）
    ├── RELEASE_GUIDE.md ← リリース手順
    │     └── RELEASE.md（補完：ポリシー）
    ├── CODE_QUALITY_GUIDE.md ← ツール設定
    └── API_DOCS_GUIDE.md ← ドキュメント生成

docs/dev/ ← 開発者向け（設計・計画）
├── DESIGN_PATTERNS.md ← 旧 GUIDELINE.md（名前変更）
├── IMPLEMENTATION_PLAN.md ← 実装計画
├── CLANG_SETUP.md ← CLANG_TIDY_SETUP + CLANG_ARM_MULTILIB_WORKAROUND 統合
├── CODE_QUALITY_NOTES.md ← 旧 clang_tooling_evaluation.md
├── SIMULATION.md ← 保持
└── RUST.md ← 言語比較
```

---

## 5. 統廃合アクション

### Phase A-4 実行項目（dev/ と clang 系の整理）

| ステップ | アクション | 対象 |
|---------|-----------|------|
| A-4.1 | GUIDELINE.md → DESIGN_PATTERNS.md に名前変更 | docs/dev/ |
| A-4.2 | clang 関連3ファイルの内容を docs/dev/CLANG_SETUP.md に統合 | docs/ 直下 → docs/dev/ |
| A-4.3 | clang_tooling_evaluation.md → docs/dev/CODE_QUALITY_NOTES.md として保持 | docs/ 直下 → docs/dev/ |
| A-4.4 | 統合元の2ファイル（CLANG_TIDY_SETUP.md, CLANG_ARM_MULTILIB_WORKAROUND.md）を削除 | docs/ 直下 |

### 統合の詳細

#### A-4.2 clang 関連統合の手順

1. `docs/dev/CLANG_SETUP.md` を新規作成し以下を統合:
   - clang-tidy セットアップ手順（CLANG_TIDY_SETUP.md 全文）
   - ARM multilib ワークアラウンド（CLANG_ARM_MULTILIB_WORKAROUND.md 全文）
2. `clang_tooling_evaluation.md` → `docs/dev/CODE_QUALITY_NOTES.md` に移動（独立評価レポートとして保持）
3. 重複する情報は CODING_RULE.md を正本として、各ファイルからリンク
4. 統合元の2ファイルを削除

### Phase A-6 で必要な更新

| 対象 | 更新内容 |
|------|---------|
| CLAUDE.md | ドキュメント参照テーブルのパス確認（現在は正しい） |
| docs/README.md | 新カテゴリ構成に合わせた目次更新 |

---

## 6. 品質評価

| 観点 | 評価 | コメント |
|------|------|---------|
| 網羅性 | ★★★★★ | ビルド/テスト/デバッグ/リリース/品質の全側面をカバー |
| 一貫性 | ★★★★☆ | lib/docs/ は統一フォーマット。docs/ 直下の散在が問題 |
| 更新頻度 | ★★★★★ | 2026-02 に活発に更新 |
| 読みやすさ | ★★★★★ | INDEX.md からの明快な導線。コード例豊富 |
| コードとの整合 | ★★★★★ | .clang-format 等の設定ファイルの正本として機能 |

---

## 7. 推奨事項

1. **clang 関連の即時統合** — docs/dev/CLANG_SETUP.md に集約、clang_tooling_evaluation.md は CODE_QUALITY_NOTES.md に移動
2. **GUIDELINE.md の名前変更** — 「ガイドライン」という名前で CODING_RULE.md と混同される
3. **lib/docs/ の完成度は高い** — 大規模な変更は不要。Stage B Phase 0 でコピー再配置後もほぼそのまま継承
4. **CLAUDE.md の参照パスは現状正しい** — Phase A-4 実行後も変更不要
