# UMI ライブラリ標準化実装計画

**Created**: 2026-02-07
**Updated**: 2026-02-07
**Phase**: Execution
**Scope**: umibench最終調整 → lib/docs統合 → umimmio/umitest/umirtm移行 → lib/umi独立化

---

## フェーズ概略

```
Phase 1          Phase 2          Phase 3               Phase 4
umibench         lib/docs         Migration              lib/umi 独立化
最終調整         統一標準化       umimmio/umitest/umirtm  dsp/midi/shell等
     │                │                │                      │
     ▼                ▼                ▼                      ▼
 ・tests/xmake.lua  ・INSTRUCTION更新 ・標準構造移行          ・構造分析
  ヘルパー関数化   ・LIBRARY_STRUCTURE ・standalone_repo対応   ・移行実施
 ・platform xmake    統合             ・テスト標準化          ・パッケージ化
  ヘルパー関数化                     ・CI追加
 ・xmake repo pkg                    ・xmake repo pkg
 ・ビルド検証
```

---

## Phase 1: umibench 最終調整 ✅ 完了

### 1.1 完了項目

- ✅ ディレクトリ構造（UMI_LIBRARY_STANDARD 準拠）
- ✅ 完全ドキュメント構成（docs/ + docs/ja/）
- ✅ CIワークフロー（ホスト/WASM/ARMビルド）
- ✅ compile_commands.autoupdate 設定
- ✅ xmake.lua standalone_repo 判定 + ヘルパー関数
- ✅ LICENSE, VERSION, CHANGELOG.md, RELEASE.md, Doxyfile
- ✅ platforms/ 構造（host/wasm/arm）
- ✅ tests/ + compile_fail テスト
- ✅ tests/xmake.lua ヘルパー関数統一 (`umibench_add_umitest_dep()`)
- ✅ platforms/arm xmake.lua ヘルパー関数統一
- ✅ platforms/wasm xmake.lua ヘルパー関数統一
- ✅ arm-embedded-xmake-repo パッケージ追加
- ✅ xmake v3 対応（`_G.` → 直接グローバル変数）
- ✅ ビルド・テスト検証パス

### 1.2 xmake v3 対応メモ

xmake v3 では `_G` テーブルが nil のため、`_G.xxx = val` は使えない。
直接グローバル変数に代入する: `UMIBENCH_STANDALONE_REPO = standalone_repo`

---

## Phase 2: lib/docs 統一標準化 ✅ 完了

### 2.1 完了項目

- ✅ UMI_LIBRARY_STANDARD.md: namespace対応表追加、xmake v3対応、umirtm追加
- ✅ INSTRUCTION.md: 入口ドキュメントとして再構成
- ✅ LIBRARY_STRUCTURE.md: UMI_LIBRARY_STANDARD.md へのリダイレクトに置換

### 2.2 lib/docs ファイル一覧と処遇

| ファイル | 処遇 |
|---------|------|
| UMI_LIBRARY_STANDARD.md | ✅ 統合標準仕様（最終化済み） |
| INSTRUCTION.md | ✅ 入口ドキュメント（再構成済み） |
| LIBRARY_STRUCTURE.md | ✅ リダイレクトに置換済み |
| IMPLEMENTATION_PLAN.md | ✅ 本計画（更新済み） |
| TESTING.md | 維持 |
| CODING_STYLE.md | 維持 |
| XMAKE.md | 維持 |
| CLANG_TOOLING.md | 維持 |
| DEBUG_GUIDE.md | 維持 |
| DOXYGEN_STYLE.md | 維持 |

---

## Phase 3: 移行・適用（umimmio / umitest / umirtm）

### 3.0 共通移行タスク（全ライブラリ共通）

各ライブラリで以下を実施：

| # | タスク | 詳細 |
|---|--------|------|
| 1 | ディレクトリ名統一 | `test/` → `tests/` リネーム |
| 2 | xmake.lua standalone_repo対応 | ヘルパー関数パターン導入 |
| 3 | tests/xmake.lua 標準化 | `add_rules("host.test")`, `add_tests("default")` |
| 4 | 必須ファイル追加 | LICENSE, VERSION, CHANGELOG.md, RELEASE.md, Doxyfile, .gitignore |
| 5 | docs/ 標準構成 | INDEX.md, DESIGN.md, TESTING.md 最低限 |
| 6 | README.md 英語化 | 英語版を主、日本語版は docs/ja/ に配置 |
| 7 | .github/workflows/ CI追加 | `<libname>-ci.yml` |
| 8 | arm-embedded-xmake-repo パッケージ追加 | `packages/u/<libname>/xmake.lua` |
| 9 | テスト実行・検証 | `xmake test` パス確認 |

### 3.1 umitest 移行（優先度: 最高）

他ライブラリの依存先のため最優先。

**現状との差分**:

| 項目 | 現在 | 変更後 |
|------|------|--------|
| xmake.lua | 5行、モノレポのみ | standalone_repo対応 |
| test/ | `test/` (単数形) | `tests/` (複数形) |
| test/xmake.lua | `set_kind("binary")` のみ | `host.test` ルール + `add_tests` |
| LICENSE | なし | MIT |
| VERSION | なし | 0.1.0 |
| CHANGELOG.md | なし | 初期版 |
| RELEASE.md | なし | 標準版 |
| Doxyfile | なし | 標準版 |
| .gitignore | なし | 標準版 |
| docs/ | DESIGN.md のみ | INDEX.md, TESTING.md 追加 |
| README.md | 日本語のみ | 英語版 + docs/ja/README.md |
| .github/ | なし | umitest-ci.yml |
| include/ | `umitest/test.hh` | ✅ 変更不要 |

**特記**: umitestは依存なし。standalone時も `add_requires` 不要。

### 3.2 umimmio 移行

**現状との差分**:

| 項目 | 現在 | 変更後 |
|------|------|--------|
| xmake.lua | 4行、モノレポのみ | standalone_repo対応 |
| test/ | 空ディレクトリ | テスト作成 + `tests/` リネーム |
| examples/ | 空ディレクトリ | 削除（空なので） |
| docs/ | 空ディレクトリ | INDEX.md, DESIGN.md, TESTING.md |
| README.md | 日本語のみ | 英語版 + docs/ja/README.md |

**特記**: テストファイルが存在しない。最低限のコンパイルテストを作成する。

### 3.3 umirtm 移行

**現状との差分**:

| 項目 | 現在 | 変更後 |
|------|------|--------|
| xmake.lua | 7行、モノレポのみ | standalone_repo対応 |
| test/ | `test/` (単数形) | `tests/` (複数形) |
| test/xmake.lua | `host.test` + `add_tests` あり | ✅ ほぼ変更不要（パス修正のみ） |
| docs/ | なし | INDEX.md, DESIGN.md, TESTING.md |
| README.md | 日本語のみ | 英語版 + docs/ja/README.md |

**特記**: umirtmはumitestに依存。standalone時は `add_requires("umitest")` が必要。

### 3.4 Phase 3 完了基準

- [ ] umitest 標準化完了・テストパス
- [ ] umimmio 標準化完了・テストパス
- [ ] umirtm 標準化完了・テストパス
- [ ] arm-embedded-xmake-repo 全パッケージ追加
- [ ] ルート xmake.lua 統合ビルド確認

---

## Phase 4: lib/umi 独立化（将来）

### 4.1 独立候補分析

| モジュール | namespace | 依存 | 独立可能性 |
|-----------|-----------|------|:-:|
| umi.dsp | `umi::dsp` | なし | ✅ |
| umi.midi | `umi::midi` | なし | ✅ |
| umi.shell | `umi::shell` | なし | ✅ |
| umi.crypto | `umi::crypto` | 要調査 | ⚠️ |
| umi.boot | `umi::boot` | kernel | ❌ |
| umi.synth | `umi::synth` | dsp | ⚠️ |
| umi.usb | `umi::usb` | dsp(ASRC) | ⚠️ |
| umi.core + kernel | `umi::core/kernel` | port, adapter | ❌ 密結合 |
| umi.port | `umi::port` | core | ❌ 密結合 |

### 4.2 Phase 4 は Phase 3 完了後に詳細計画を策定

---

## リスクと対策

| リスク | 影響 | 対策 |
|--------|------|------|
| `host.test` ルールの提供元 | standalone buildで動作不可 | arm-embedded パッケージが提供（確認済み） |
| umimmio テスト不在 | Phase 3 工数増 | 最小限のコンパイルテストで対応 |
| xmake v3 での `_G` nil化 | グローバル変数共有不可 | 直接グローバル変数代入で対応（検証済み） |
| CI環境差異 | ローカルでは動くがCIで失敗 | 早期にCI検証 |

---

## 実行順序

```
Step 1: Phase 1 - umibench最終調整           ✅ 完了
Step 2: Phase 2 - lib/docs標準化             ✅ 完了
Step 3: Phase 3 - umitest移行                ← 現在
Step 4: Phase 3 - umimmio移行
Step 5: Phase 3 - umirtm移行
Step 6: 全体統合検証
```
