# arm-embedded-xmake-repo 改善 実行計画

## TL;DR

標準xmakeワークフローに合わせて、プラグイン/ルール/タスク構成を整理し、重複実装を削減する。Phase 1は`format/check`の標準化と`xmake test`自動検出対応。Phase 2以降でFlash統合、ツール検出の共通化、エミュレータ/サイズ/デプロイ整備を行う。

---

## コンテキスト

- 既存計画書: `docs/ARM_EMBEDDED_XMAKE_REPO_IMPROVEMENT_PLAN.md`
- 標準フロー基準: `docs/XMAKE.md`
- arm-embedded repo: `.refs/arm-embedded-xmake-repo`
- 現行xmake設定: `xmake.lua`

---

## スコープ

### IN
- `coding-rules` に `xmake format` と `xmake check` を追加
- 既存テストターゲットの `set_group("test")` を付与し `xmake test` 自動検出に移行
- Flash/ツール検出/エミュレータ関連の共通化設計と移行
- ドキュメント更新

### OUT
- 実際のビルド成果物やARMボードへの書き込み実行
- 既存テストの内容変更（テスト追加/削除）
- 機能要件やAPI仕様の変更

---

## 依存関係

- 変更対象: `.refs/arm-embedded-xmake-repo` と ルート `xmake.lua`
- 既存プラグイン/ルールを尊重し、後方互換を維持する

---

## 実行フェーズとタスク

### Wave 1 (Phase 1: Quick Wins)

- [x] **1) `xmake format` プラグイン追加**

**目的**: `xmake coding format` と同等機能を標準コマンドで提供。

**参照**:
- `.refs/arm-embedded-xmake-repo/packages/c/coding-rules/xmake.lua`
- `.refs/arm-embedded-xmake-repo/packages/c/coding-rules/plugins/format/xmake.lua`

**作業**:
- `coding-rules`にトップレベル `format` タスクを追加
- 既存の`coding format`と同じ実装を呼び出す

**受け入れ条件**:
- `xmake format` が `xmake coding format` と同等の挙動

**QA (コマンド)**:
- `xmake format` 実行でフォーマットが適用される

---

- [x] **2) `xmake check` プラグイン追加**

**目的**: CI向けに短い標準コマンドを提供。

**参照**:
- `.refs/arm-embedded-xmake-repo/packages/c/coding-rules/xmake.lua`
- `.refs/arm-embedded-xmake-repo/packages/c/coding-rules/plugins/check/xmake.lua`

**作業**:
- `coding-rules`にトップレベル `check` タスクを追加
- 既存の`coding check`と同じ実装を呼び出す

**受け入れ条件**:
- `xmake check` が `xmake coding check` と同等の挙動

**QA (コマンド)**:
- `xmake check` 実行でチェックが走る

---

- [x] **3) `xmake test` 自動検出移行**

**目的**: `set_group("test")`で標準テスト検出に移行。

**参照**:
- `xmake.lua` 内の `target("test_*" ...)` 定義

**作業**:
- 既存のテストターゲットに `set_group("test")` を追加
- 必要に応じて `set_default(false)` を見直し

**受け入れ条件**:
- `xmake test` で全テストが自動検出される

**QA (コマンド)**:
- `xmake test` 実行でテスト一覧が走る

---

- [x] **4) テスト標準化のドキュメント更新**

**目的**: 新しいテスト追加ルールを周知。

**参照**:
- `docs/XMAKE.md`
- `docs/ARM_EMBEDDED_XMAKE_REPO_IMPROVEMENT_PLAN.md`

**作業**:
- `set_group("test")` での自動検出ルールを明記
- `xmake test -g / -p` の使い方を追記

**受け入れ条件**:
- 新規テスト追加手順が明確に記載される

**QA (コマンド)**:
- `rg "set_group\(\"test\"\)" docs/XMAKE.md` で記載を確認

---

### Wave 2 (Phase 2: Core Improvements)

- [x] **5) Flash統合プラグインの整備**

**目的**: 5つのFlashタスクを統合し、MCU/アドレス指定を標準化。

**参照**:
- `.refs/arm-embedded-xmake-repo/packages/a/arm-embedded/plugins/flash/xmake.lua`
- プロジェクト内の`flash-*`タスク（検索で特定）

**作業**:
- 既存Flashプラグインに `--address` などの拡張オプションを追加
- プロジェクト側の`flash-*`タスクを `xmake flash -t <target>` に移行
- 旧タスクはエイリアスとして残す（移行期間）

**受け入れ条件**:
- `xmake flash -t <target>` で従来と同等動作
- 旧タスクも当面動作し、非推奨メッセージを出す

**QA (コマンド)**:
- `xmake flash -t <target> --help` で新オプションが表示される

---

- [x] **6) ツール検出の共通化 (レジストリ導入)**

**目的**: Renode/pyOCD/ARM GCC検出を一元化。

**参照**:
- `.refs/arm-embedded-xmake-repo/packages/a/arm-embedded/plugins/flash/xmake.lua`
- `.refs/arm-embedded-xmake-repo/packages/a/arm-embedded/plugins/debug/xmake.lua`
- `.refs/arm-embedded-xmake-repo/packages/a/arm-embedded/plugins/debugger/xmake.lua`

**作業**:
- `arm-embedded`内にツール検出ユーティリティを追加
- 既存プラグインで共通ユーティリティを使用

**受け入れ条件**:
- 主要プラグインが共通検出ロジックを使用
- 検出失敗時のメッセージが統一される

**QA (コマンド)**:
- `xmake flash --help` でツール検出メッセージが統一される

---

### Wave 3 (Phase 3: Advanced Features)

- [x] **7) エミュレータプラグイン族**

**目的**: `renode`/`renode-test`/`robot` を標準化。

**参照**:
- `xmake.lua` の `task("renode")`, `task("renode-test")`, `task("robot")`

**作業**:
- `arm-embedded`に `emulator.run/test/robot` を追加
- 既存タスクをエイリアス化

**受け入れ条件**:
- `xmake emulator run/test/robot` で従来同等動作

**QA (コマンド)**:
- `xmake emulator --help` でサブコマンドが表示される

---

- [x] **8) サイズ分析プラグイン**

**目的**: `fs-check`のサイズ分析部分を汎用化。

**参照**:
- `xmake.lua` の `task("fs-check")`

**作業**:
- `xmake size -t <target>` の設計と実装
- 結果出力フォーマットを統一

**受け入れ条件**:
- `xmake size -t <target>` でサイズが表示される

**QA (コマンド)**:
- `xmake size --help` で使い方が表示される

---

- [ ] **9) デプロイプラグイン**

**目的**: `webhost`や`waveshaper-py`の配布処理を統一。

**参照**:
- `xmake.lua` の `task("webhost")`, `task("webhost-serve")`, `task("waveshaper-py")`

**作業**:
- `xmake deploy -t <target> --dest <dir>` を追加
- 旧タスクはエイリアス化

**受け入れ条件**:
- `xmake deploy -t headless_webhost --dest examples/headless_webhost/web` が可能

**QA (コマンド)**:
- `xmake deploy --help` で使い方が表示される

---

### Wave 4 (Phase 4: Polish)

- [ ] **10) `xmake show` の拡張**

**目的**: `xmake show` に組み込み向け情報を表示。

**参照**:
- `.refs/arm-embedded-xmake-repo/packages/a/arm-embedded/rules/embedded/xmake.lua`
- `xmake.lua` の `task("info")`

**作業**:
- embeddedルールに`on_show`相当の情報出力を追加
- `info`タスクは非推奨化

**受け入れ条件**:
- `xmake show` にMCU/Toolchain/Memory情報が表示される

**QA (コマンド)**:
- `xmake show` で組み込み情報が出る

---

- [ ] **11) ドキュメント統合**

**目的**: 全ドキュメントに新コマンド体系を反映。

**参照**:
- `docs/XMAKE.md`
- `.refs/arm-embedded-xmake-repo/README.md`

**作業**:
- 旧コマンドとの対応表を記載
- 旧タスクは非推奨と明記

**受け入れ条件**:
- 主要ドキュメントが新コマンド表記に統一

**QA (コマンド)**:
- `rg "xmake format|xmake check|xmake flash -t" docs` で記載確認

---

## 進行ルール

- 後方互換性を保つ（旧コマンドはエイリアスとして残す）
- Phaseごとに確認・段階的に移行
- 実行前に `xmake --version` と環境依存ツールの存在を確認

---

## 最終確認（Success Criteria）

- `xmake format/check/test/flash` の標準コマンドが一貫して使える
- 旧タスクは非推奨として残り、移行方法がドキュメント化されている
- ドキュメントと実装が一致している
