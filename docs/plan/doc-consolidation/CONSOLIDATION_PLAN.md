# UMI ドキュメント統廃合計画

**バージョン:** 2.0.0
**作成日:** 2026-02-14
**前提仕様:** [LIBRARY_SPEC.md](../LIBRARY_SPEC.md) v1.3.0 / [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) v1.1.0
**関連:** [DOCUMENT_INVENTORY.md](DOCUMENT_INVENTORY.md)

---

## 1. 前提: クリーンスレート実装との関係

### 1.1 IMPLEMENTATION_PLAN v1.1.0 の影響

IMPLEMENTATION_PLAN v1.1.0 は以下のクリーンスレート戦略を定義している:

- **Phase 0:** 現行 `lib/` 全体を `lib/_archive/` に退避、`examples/` を `_archive/examples/` に退避
- **Phase 1-4:** LIBRARY_SPEC v1.3.0 準拠の12ライブラリを新規構築
- **Phase 4 (P4-5):** 全検証通過後に `lib/_archive/` と `_archive/examples/` を削除

**ドキュメント統廃合への影響:**

| 現行パス | Phase 0 後 | 最終状態 |
|---------|-----------|---------|
| `lib/umi/*/docs/` | `lib/_archive/umi/*/docs/` | **消滅** — 新ライブラリの `docs/` として再構築 |
| `lib/docs/` | `lib/_archive/docs/` | **消滅** — 新規 `lib/docs/` として再構築 |
| `lib/umibench/` 等 | `lib/_archive/standalone/` | **コピー＋修正** で新ライブラリに |
| `docs/` | **変更なし** | docs/ 配下の統廃合は独立して実施可能 |

### 1.2 統廃合の2フェーズ

ドキュメント統廃合は、実装計画のフェーズに同期して **2段階** で行う:

```
Stage A: docs/ 配下の統廃合（実装計画と独立、即時実行可能）
Stage B: lib/ 配下のドキュメント再構築（実装計画の各フェーズと同期）
```

**Stage A** は lib/ のクリーンスレートに依存せず、今すぐ実行できる。
**Stage B** は IMPLEMENTATION_PLAN の各 Phase で新ライブラリを構築する際に、ドキュメントも同時に構築する。

---

## 2. 現状の問題

### 2.1 数字で見る問題

| 指標 | 現状 |
|------|------|
| 総ドキュメント数 | ~309ファイル |
| 不要/重複ファイル | ~75ファイル（24%） |
| 壊れたリンク | docs/README.md 25+箇所、copilot-instructions.md 5+箇所 |
| カーネル文書の重複箇所 | 3ディレクトリ（完全同一コピー5ファイル含む） |
| 分散したドキュメント領域 | docs/ ⇔ lib/docs/ ⇔ lib/umi/*/docs/ の3層 |

### 2.2 構造的問題

1. **正本が不明確** — 同じトピックが複数箇所に存在し、どれが最新・正本か判断不能
2. **archive の不完全な運用** — archiveに移動したファイルが元の場所にもコピーで残存
3. **docs/ のディレクトリ再編が繰り返され、その痕跡が残存** — refs/, dev/, umi-kernel/, umios-architecture/, archive/ が混在
4. **lib内ドキュメントと docs/ の役割分担が不明確** — USB, MIDI, Port 等のドメインドキュメントが両方に存在
5. **目次ファイル(README.md)が機能していない** — 壊れたリンクが修正されていない

---

## 3. 統廃合の方針

### 3.1 基本原則

| 原則 | 説明 |
|------|------|
| **Single Source of Truth** | 各トピックの正本は1箇所のみ |
| **Proximity to Code** | ライブラリ固有のドキュメントはlib/内に配置 |
| **Clear Hierarchy** | docs/は全体設計・仕様・ガイド、lib/docs/はライブラリ標準、lib/<libname>/docs/はライブラリ固有 |
| **Archive = 完全な移動** | archiveしたファイルは元の場所から必ず削除 |
| **Living Index** | 目次ファイルは自動生成またはCI検証で壊れたリンクを防ぐ |
| **クリーンスレートと同期** | lib/ 配下のドキュメントは実装計画の各フェーズで同時に新規構築する |

### 3.2 カテゴリ構成

統廃合後のドキュメントを以下の7カテゴリに分類する。

| カテゴリ | ID | 内容 | 配置先 | 詳細 |
|---------|-----|------|--------|------|
| **A. コア仕様** | CAT_A | アーキテクチャ、UMIP/UMIC/UMIM仕様、Concepts、命名体系、セキュリティ | docs/refs/ | [CAT_A_CORE_SPECS.md](CAT_A_CORE_SPECS.md) |
| **B. OS設計** | CAT_B | カーネル仕様、umios-architecture、ブートシーケンス、メモリ保護、サービス | docs/umi-kernel/ + docs/umios-architecture/ | [CAT_B_OS_DESIGN.md](CAT_B_OS_DESIGN.md) |
| **C. プロトコル** | CAT_C | SysExプロトコル、USB Audio設計、MIDIトランスポート | docs/umi-sysex/ | [CAT_C_PROTOCOLS.md](CAT_C_PROTOCOLS.md) |
| **D. 開発ガイド** | CAT_D | コーディング規約、ビルド、テスト、デバッグ、リリース、ツール設定 | lib/docs/ (再構築) + docs/dev/ | [CAT_D_DEV_GUIDES.md](CAT_D_DEV_GUIDES.md) |
| **E. HAL/ドライバ設計** | CAT_E | HAL Concept、PAL、ボード設定、ビルドシステム、コード生成 | lib/docs/design/ (再構築) | [CAT_E_HAL_DESIGN.md](CAT_E_HAL_DESIGN.md) |
| **F. DSP/技術資料** | CAT_F | TB-303 VCF/VCO解析、VAFilter、HW I/O処理設計 | docs/dsp/ + docs/hw_io/ (既存) | [CAT_F_DSP_TECHNICAL.md](CAT_F_DSP_TECHNICAL.md) |
| **G. ライブラリ固有** | CAT_G | 各ライブラリ(12ライブラリ)のドキュメント | lib/<libname>/docs/ (新規構築) | [CAT_G_LIBRARY_DOCS.md](CAT_G_LIBRARY_DOCS.md) |

---

## 4. Stage A: docs/ 配下の統廃合（即時実行可能）

### Phase A-1: 重複除去とクリーンアップ

#### A-1.1 完全同一コピーの削除

`docs/archive/umi-kernel/` と `docs/umi-kernel/` で完全同一のファイル:

| 削除対象 | 正本 |
|---------|------|
| docs/archive/umi-kernel/OVERVIEW.md | docs/umi-kernel/OVERVIEW.md |
| docs/archive/umi-kernel/DESIGN_DECISIONS.md | docs/umi-kernel/DESIGN_DECISIONS.md |
| docs/archive/umi-kernel/MEMORY.md | docs/umi-kernel/MEMORY.md |
| docs/archive/umi-kernel/service/FILESYSTEM.md | docs/umi-kernel/service/FILESYSTEM.md |
| docs/archive/umi-kernel/service/SHELL.md | docs/umi-kernel/service/SHELL.md |

**注意:** `docs/archive/umi-kernel/` 内の ARCHITECTURE.md, BOOT_SEQUENCE.md, IMPLEMENTATION_PLAN.md は **異なるバージョン** であり削除しない。旧バージョンとして archive に保持する。

#### A-1.2 移行済みファイルの削除

| 削除対象 | 理由 |
|---------|------|
| docs/archive/UXMP_SPECIFICATION.md | UMI-SysExに統合済み |
| docs/archive/UXMP_DATA_SPECIFICATION.md | 同上 |
| docs/archive/UXMP提案書.md | 同上（冒頭に「移行完了」記載） |
| docs/archive/UMI_STATUS_PROTOCOL.md | 現行実装と不一致 |
| docs/archive/KERNEL_SCHEDULER_REDESIGN.md | 実装済みで役割終了 |
| docs/archive/WEB_UI_REDESIGN.md | 反映済みで役割終了 |
| docs/archive/umix/ (2ファイル) | SysExプロトコルに統合済み |

#### A-1.3 メモの統合

| 統合元 | 統合先 | アクション |
|--------|--------|-----------|
| docs/MEMOMEMO.md | docs/MEMO.md | 4項目をMEMO.mdに追記後、MEMOMEMO.mdを削除 |
| docs/structure.md | — | PROJECT_STRUCTUREに完全に包含済み。削除 |
| docs/CLANG_ARM_MULTILIB_WORKAROUND.md | — | Phase A-4 で CLANG_SETUP.md に統合（ここでは削除しない） |

#### A-1.4 壊れたリンクの修正

| ファイル | アクション |
|---------|-----------|
| docs/README.md | 全面書き直し（新カテゴリ構成に合わせる） |
| .github/copilot-instructions.md | CLAUDE.mdと整合するようパスを修正 |
| docs/archive/README.md | 壊れたリンクを修正 |
| docs/LISENCE_SERVER.md | ファイル名typo修正（LISENCE → LICENSE） |

### Phase A-2: カーネル文書の一本化

`docs/umi-kernel/spec/` が正本として確立されているため:

| アクション | 対象 | 理由 |
|-----------|------|------|
| archive移動 | docs/umi-kernel/OVERVIEW.md | spec/kernel.mdに内容包含 |
| archive移動 | docs/umi-kernel/ARCHITECTURE.md | umios-architecture/02-kernel/が詳細版 |
| archive移動 | docs/umi-kernel/BOOT_SEQUENCE.md | umios-architecture/02-kernel/15-boot-sequence.mdが正本 |
| archive移動 | docs/umi-kernel/DESIGN_DECISIONS.md | adr.mdに役割移行 |
| archive移動 | docs/umi-kernel/IMPLEMENTATION_PLAN.md | docs/dev/IMPLEMENTATION_PLAN.mdが最新 |
| archive移動 | docs/umi-kernel/MEMORY.md | spec/memory-protection.mdが正本 |
| 保持 | docs/umi-kernel/spec/*.md | 正本（4ファイル） |
| 保持 | docs/umi-kernel/adr.md | ADR（設計判断記録） |
| 保持 | docs/umi-kernel/plan.md | メタ計画文書（参照パス要更新） |
| 保持 | docs/umi-kernel/platform/stm32f4.md | プラットフォーム固有 |

### Phase A-3: SysEx/プロトコル文書の整理

| アクション | 対象 | 理由 |
|-----------|------|------|
| 統合 | UMI_SYSEX_DATA.md → UMI_SYSEX_DATA_SPEC.md | DATA_SPEC.mdが詳細版、DATA.mdの内容を吸収 |
| 統合 | UMI_SYSEX_STATUS_IMPLEMENTATION_NOTES.md → UMI_SYSEX_STATUS.md | 実装メモをSTATUS.mdに統合 |
| archive移動 | docs/archive/UMI_SYSEX_PROTOCOL.md | umi-sysex/が正本 |

### Phase A-4: dev/ の整理

| アクション | 対象 → 先 | 理由 |
|-----------|-----------|------|
| 名前変更 | docs/dev/GUIDELINE.md → docs/dev/DESIGN_PATTERNS.md | 内容はC++設計パターン集であり「ガイドライン」ではない |
| 統合 | docs/clang_tooling_evaluation.md → docs/dev/CODE_QUALITY_NOTES.md | 独立した評価レポートとして保持（lib/docs/ は Stage B で再構築） |
| 統合 | docs/CLANG_TIDY_SETUP.md + docs/CLANG_ARM_MULTILIB_WORKAROUND.md → docs/dev/CLANG_SETUP.md | ツール設定を1ファイルに集約 |

### Phase A-5: USB/MIDIドキュメントの住み分け

| アクション | 理由 |
|-----------|------|
| docs/umi-usb/ → docs/archive/ へ移動 | lib/umi/usb/docs/の方が最新かつ詳細。ただし Stage B Phase 3 で新構造に移行するため、現時点では archive 移動のみ |
| docs/archive/umidi/ は保持 | lib/umi/midi/docs/と補完関係（設計思想の記録として価値） |

### Phase A-6: 目次・ナビゲーションの再構築

| アクション | 対象 |
|-----------|------|
| 全面書き直し | docs/README.md — 新カテゴリ構成に合わせた目次 |
| 更新 | README.md — ディレクトリ構成を現状に合わせる |
| 更新 | .github/copilot-instructions.md — パスを修正 |
| 更新 | CLAUDE.md — ドキュメント参照テーブルを新パスに合わせる |

---

## 5. Stage B: lib/ 配下のドキュメント再構築（実装計画と同期）

Stage B は IMPLEMENTATION_PLAN v1.1.0 の各フェーズと完全に同期して実行する。
各ライブラリの構築時に、UMI Strict Profile (LIBRARY_SPEC §8.1) 準拠のドキュメントを同時に作成する。

### 5.1 UMI Strict Profile ドキュメント要件

LIBRARY_SPEC v1.3.0 §8.1 により、全ライブラリに以下が必須:

| ファイル | 内容 |
|---------|------|
| README.md | リリースステータス、Why セクション、Quick Start、公開ヘッダ一覧、Documentation リンク |
| docs/DESIGN.md | 11セクション設計文書 |
| docs/INDEX.md | API リファレンスマップ |
| docs/TESTING.md | テスト戦略・品質ゲート |
| docs/ja/ | 日本語版（英語ドキュメントのミラー） |
| Doxyfile | Doxygen 生成設定 |
| examples/ | 最低1つの最小サンプル |

### 5.2 Phase 0 同期: lib/docs/ のアーカイブ

IMPLEMENTATION_PLAN Phase 0 で `lib/docs/` が `lib/_archive/docs/` に退避される。

**アクション:**
- `lib/_archive/docs/standards/` の 3 ファイル (CODING_RULE.md, LIBRARY_SPEC.md, API_COMMENT_RULE.md) は新 `lib/docs/standards/` にコピー
- `lib/_archive/docs/guides/` の 7 ファイルは新 `lib/docs/guides/` にコピー
- `lib/_archive/docs/design/` の 60+ ファイルは新 `lib/docs/design/` にコピー
- `lib/_archive/docs/INDEX.md` は新 `lib/docs/INDEX.md` にコピー

**理由:** lib/docs/ は共通標準・ガイドであり、ライブラリ実装とは独立。高品質で保持に値する (CAT_D 96%, CAT_E 100% の監査スコア)。

### 5.3 Phase 1 同期: L0 + L1 ライブラリ (6個)

| ライブラリ | ドキュメント方針 |
|-----------|---------------|
| umitest | コピー + INDEX.md, TESTING.md 新規追加 |
| umibench | コピー + INDEX.md, TESTING.md 新規追加（ゴールドリファレンス） |
| umirtm | コピー + INDEX.md, TESTING.md 新規追加 |
| umimmio | コピー + **分散5ファイル統合**（USAGE, EXAMPLES, GETTING_STARTED, INDEX, TESTING → DESIGN.md に統合し、新規 INDEX.md, TESTING.md を作成） |
| umicore | 全ドキュメント新規作成 |
| umihal | 全ドキュメント新規作成 |

**現行 lib/umi/*/docs/ の扱い:**

Phase 0 で `lib/_archive/umi/*/docs/` にアーカイブされる。新ライブラリ構築時に参照元として活用するが、直接コピーはしない。新ライブラリのドキュメントは UMI Strict Profile に完全準拠して新規作成する。

### 5.4 Phase 2 同期: L2 ライブラリ (2個)

| ライブラリ | ドキュメント方針 |
|-----------|---------------|
| umiport | 全ドキュメント新規作成。`lib/_archive/umi/port/docs/` (9ファイル) を参照して DESIGN.md を作成。PAL 設計は `lib/_archive/docs/design/pal/` から内容を統合 |
| umidevice | 全ドキュメント新規作成 |

**lib/docs/design/ との関係:**

現行 `lib/docs/design/` の HAL/PAL 設計資料 (60+ ファイル) は、umiport の DESIGN.md と `lib/docs/design/` の両方に適切に配分する:
- **lib/docs/design/**: フレームワーク全体に関わる設計資料（foundations/, hal/, research/ 等）
- **lib/umiport/docs/**: umiport 固有の実装設計（PAL パイプライン、MCU DB、board.lua 等）

### 5.5 Phase 3 同期: L3 ライブラリ (3個)

| ライブラリ | ドキュメント方針 |
|-----------|---------------|
| umidsp | `lib/_archive/umi/dsp/README.md` + `docs/dsp/` (CAT_F) を参照。docs/dsp/ は独立技術資料として保持（移動しない） |
| umidi | `lib/_archive/umi/midi/docs/` (3ファイル) を参照して新規作成。docs/umi-sysex/ (CAT_C) は独立プロトコル仕様として保持 |
| umiusb | `lib/_archive/umi/usb/docs/` (14ファイル) を参照して新規作成。設計的価値の高い資料は新 docs/ に統合 |

### 5.6 Phase 4 同期: L4 ライブラリ + 統合

| ライブラリ | ドキュメント方針 |
|-----------|---------------|
| umios | 全ドキュメント新規作成。`lib/_archive/umi/kernel/` 等および `docs/umios-architecture/` (CAT_B) を参照 |

**Phase 4 完了時の docs/ 再構成:**
- docs/umios-architecture/ と umios の DESIGN.md の関係を明確化
- umios-architecture/ は **外部設計仕様書** として保持、DESIGN.md は **実装設計** として位置づけ

### 5.7 現行 lib/umi/*/docs/ の最終処理

| 現行パス | 新パス | 処理 |
|---------|--------|------|
| lib/umi/fs/docs/ (6ファイル) | lib/umios/ 内部参照 | umios の StorageService 設計に統合。参照元として _archive に保持 |
| lib/umi/usb/docs/ (14ファイル) | lib/umiusb/docs/ + lib/umiport/docs/ | USB プロトコル → umiusb、USB HAL → umiport に分配 |
| lib/umi/midi/docs/ (3ファイル) | lib/umidi/docs/ | umidi の設計資料として新規作成時に参照 |
| lib/umi/port/docs/ (9ファイル) | lib/umiport/docs/ | umiport の DESIGN.md 作成時に参照 |
| lib/umi/mmio/docs/ (3ファイル) | lib/umimmio/docs/ | DESIGN.md に統合（IMPROVEMENTS.md の未解決項目は新 Issue として管理） |
| lib/umi/dsp/README.md | lib/umidsp/docs/ | 新規 DESIGN.md の基礎 |
| lib/umi/bench_old/ (2ファイル) | — | **削除**（umibench に完全置換済み） |
| lib/umi/ref/README.md | — | 保持（規約検証用リファレンス） |

---

## 6. 統廃合後の目標構成

```
docs/
├── README.md                    # 目次（全カテゴリへのナビゲーション）
├── NOMENCLATURE.md              # 命名体系・用語定義（正本）
├── PROJECT_STRUCTURE.md         # プロジェクト構成（正本）
├── MEMO.md                      # 開発メモ（統合済み）
│
├── refs/                        # [CAT_A] コア仕様
│   ├── ARCHITECTURE.md          # アーキテクチャ
│   ├── CONCEPTS.md              # C++20 Concepts
│   ├── SECURITY.md              # セキュリティ
│   ├── UMIP.md / UMIC.md / UMIM.md  # コア仕様群
│   ├── UMIM_NATIVE_SPEC.md
│   ├── API_*.md                 # API仕様群
│   └── UMIDSP_GUIDE.md
│
├── umi-kernel/                  # [CAT_B] OS設計（整理後）
│   ├── spec/                    # 仕様正本（4ファイル）
│   ├── adr.md                   # ADR
│   ├── plan.md                  # メタ計画
│   └── platform/stm32f4.md
│
├── umios-architecture/          # [CAT_B] OS設計仕様（41ファイル、既存構成維持）
│
├── umi-sysex/                   # [CAT_C] SysExプロトコル（統合後5ファイル）
│
├── dev/                         # [CAT_D] 開発者向け（整理後）
│   ├── DESIGN_PATTERNS.md       # 旧GUIDELINE.md
│   ├── IMPLEMENTATION_PLAN.md
│   ├── CLANG_SETUP.md           # CLANG_TIDY_SETUP + CLANG_ARM_MULTILIB 統合
│   ├── CODE_QUALITY_NOTES.md    # 旧clang_tooling_evaluation.md（評価レポート）
│   ├── SIMULATION.md            # シミュレーションバックエンド比較
│   ├── RUST.md
│   └── DEBUG_VSCODE_CORTEX_DEBUG.md
│
├── dsp/                         # [CAT_F] DSP技術資料（変更なし）
├── hw_io/                       # [CAT_F] HW I/O処理設計（変更なし）
├── web/                         # Web UI設計
│
├── LICENSE_SERVER.md            # ライセンスサーバー（typo修正済み）
├── STM32H7.md                   # DCache技術資料
├── esp32-support-investigation.md
│
├── archive/                     # アーカイブ（整理後）
│   ├── README.md
│   ├── umi-kernel/              # 旧カーネル文書（差分があるもののみ）
│   ├── umi-api/                 # 旧API設計
│   ├── umi-usb/                 # docs/umi-usb/ から移動
│   ├── umidi/                   # 旧MIDI設計（補完資料として保持）
│   └── (その他 archive 文書)
│
└── plan/                        # 計画文書
    ├── LIBRARY_SPEC.md          # 設計仕様書 v1.3.0
    ├── IMPLEMENTATION_PLAN.md   # 実装計画書 v1.1.0
    ├── archive/                 # 計画の旧版
    └── doc-consolidation/       # 本統廃合計画

lib/
├── docs/                        # [CAT_D, CAT_E] 共通標準・ガイド（Stage B Phase 0 で再構築）
│   ├── standards/               # CODING_RULE, LIBRARY_SPEC, API_COMMENT_RULE
│   ├── guides/                  # 7ガイドファイル
│   ├── design/                  # HAL/ドライバ設計 60+ ファイル
│   └── INDEX.md
│
├── umicore/docs/                # [CAT_G] 新規作成（Phase 1）
├── umihal/docs/                 # [CAT_G] 新規作成（Phase 1）
├── umimmio/docs/                # [CAT_G] コピー＋統合（Phase 1）
├── umitest/docs/                # [CAT_G] コピー＋追加（Phase 1）
├── umibench/docs/               # [CAT_G] コピー＋追加（Phase 1）
├── umirtm/docs/                 # [CAT_G] コピー＋追加（Phase 1）
├── umiport/docs/                # [CAT_G] 新規作成（Phase 2）
├── umidevice/docs/              # [CAT_G] 新規作成（Phase 2）
├── umidsp/docs/                 # [CAT_G] 新規作成（Phase 3）
├── umidi/docs/                  # [CAT_G] 新規作成（Phase 3）
├── umiusb/docs/                 # [CAT_G] 新規作成（Phase 3）
└── umios/docs/                  # [CAT_G] 新規作成（Phase 4）
```

---

## 7. 削除ファイル一覧

### Stage A 削除/移動（即時実行可能、合計 ~21 ファイル）

| ファイル | 理由 |
|---------|------|
| docs/structure.md | PROJECT_STRUCTUREに包含 |
| docs/MEMOMEMO.md | MEMO.mdに統合 |
| docs/CLANG_ARM_MULTILIB_WORKAROUND.md | CLANG_SETUP.mdに統合 |
| docs/CLANG_TIDY_SETUP.md | CLANG_SETUP.mdに統合 |
| docs/clang_tooling_evaluation.md | docs/dev/CODE_QUALITY_NOTES.md に移動（名前変更） |
| docs/umi-usb/ (2ファイル) | archive に移動 |
| docs/archive/UXMP_SPECIFICATION.md | SysExに統合済み |
| docs/archive/UXMP_DATA_SPECIFICATION.md | SysExに統合済み |
| docs/archive/UXMP提案書.md | SysExに統合済み |
| docs/archive/UMI_STATUS_PROTOCOL.md | 現行不一致 |
| docs/archive/KERNEL_SCHEDULER_REDESIGN.md | 実装済み |
| docs/archive/WEB_UI_REDESIGN.md | 反映済み |
| docs/archive/umix/ (2ファイル) | SysExに統合済み |
| docs/archive/umi-kernel/ (5同一ファイル) | 重複 |
| docs/archive/PLAN_AUDIOCONTEXT_REFACTOR.md | 完了済み |
| docs/archive/UMIOS_CONTENTS.md | PROJECT_STRUCTUREに包含 |
| docs/archive/LIBRARY_CONTENTS.md | PROJECT_STRUCTUREに包含 |
| docs/umi-sysex/UMI_SYSEX_DATA.md | DATA_SPECに統合 |
| docs/umi-sysex/UMI_SYSEX_STATUS_IMPLEMENTATION_NOTES.md | STATUSに統合 |

### Stage B 削除（Phase 0 でアーカイブ、Phase 4 (P4-5) で削除）

| ファイル | 理由 |
|---------|------|
| lib/umi/bench_old/ (2ファイル) | umibenchに完全置換済み |
| lib/_archive/ 全体 | Phase 4 (P4-5) 全検証通過後に削除 |
| _archive/examples/ 全体 | Phase 4 (P4-5) 全検証通過後に削除 |

---

## 8. 実行順序とリスク

### 実行順序

```
Stage A（docs/ 配下、即時実行可能）:
  A-1（即座） — 重複除去、typo修正、メモ統合
  A-2（短期） — カーネル文書一本化
  A-3（短期） — SysEx統合
  A-4（中期） — dev/ と clang 系の整理
  A-5（中期） — USB/MIDI の住み分け
  A-6（最終） — 目次再構築

Stage B（lib/ 配下、実装計画と同期）:
  B-0 = IMPL Phase 0 — lib/docs/ のコピー再配置
  B-1 = IMPL Phase 1 — L0+L1 ライブラリドキュメント（6個）
  B-2 = IMPL Phase 2 — L2 ライブラリドキュメント（2個）
  B-3 = IMPL Phase 3 — L3 ライブラリドキュメント（3個）
  B-4 = IMPL Phase 4 — L4 ライブラリドキュメント + 統合検証
```

### リスクと緩和策

| リスク | 緩和策 |
|--------|--------|
| CLAUDE.md の参照パスが壊れる | Phase A-6 で CLAUDE.md, copilot-instructions.md を同時更新 |
| archive 削除で情報が失われる | `trash` コマンドを使用。git 履歴にも残る |
| 進行中の作業との競合 | 専用ブランチ `docs/consolidation` で実施 |
| Stage A と Stage B のタイミング不整合 | Stage A は docs/ のみ対象のため lib/ には影響しない。Stage B は実装フェーズ内で完結 |
| lib/docs/design/ の所在 | Phase 0 でコピー再配置し、新旧の参照を維持 |

---

## 9. 成果物チェックリスト

### Stage A

- [ ] 不要ファイル ~21 件の削除/archive 移動
- [ ] カーネル文書の正本一本化
- [ ] SysEx プロトコル文書の統合
- [ ] dev/ の名前変更・移動
- [ ] 壊れたリンクの全修正
- [ ] docs/README.md の全面書き直し
- [ ] CLAUDE.md の参照テーブル更新
- [ ] copilot-instructions.md のパス修正

### Stage B

- [ ] lib/docs/ の再配置（Phase 0 同期）
- [ ] L0+L1 6 ライブラリの UMI Strict Profile ドキュメント完成（Phase 1）
- [ ] L2 2 ライブラリの UMI Strict Profile ドキュメント完成（Phase 2）
- [ ] L3 3 ライブラリの UMI Strict Profile ドキュメント完成（Phase 3）
- [ ] L4 1 ライブラリの UMI Strict Profile ドキュメント完成（Phase 4）
- [ ] 全12ライブラリの Doxygen 生成成功
- [ ] lib/_archive/ 削除完了
