# UMI ドキュメント完全インベントリ

**作成日:** 2026-02-14
**最終更新日:** 2026-02-14
**対象:** プロジェクト固有の全ドキュメント（.refs/, .venv/, xmake-repo/ 外部参照を除く）
**総ファイル数:** 約309ファイル
**前提仕様:** [LIBRARY_SPEC.md](../LIBRARY_SPEC.md) v1.3.0 / [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) v1.1.0

> **注意:** IMPLEMENTATION_PLAN v1.1.0 のクリーンスレート戦略により、`lib/` 配下のドキュメントは Phase 0 で `lib/_archive/` にアーカイブされ、新規12ライブラリの `docs/` として再構築される。本インベントリの lib/ セクションは**現行の配置**を記録しており、最終的なパスは CONSOLIDATION_PLAN v2.0.0 §5 を参照のこと。

---

## 凡例

| 記号 | 有用性 | 説明 |
|------|--------|------|
| ★ | 高 | 現在のコードベースと整合、アクティブに参照される |
| ◆ | 中 | 部分的に有用だが更新が必要、または参考資料として価値あり |
| ▽ | 低 | 古い/重複/断片的、統合または削除候補 |
| ✗ | 不要 | 完全に役割終了、コードとの不整合、または完全な重複 |

---

## 1. プロジェクトルート

| # | ファイル | 有用性 | 作成時期 | 要約 | 重複/関連 | 備考 |
|---|---------|--------|----------|------|-----------|------|
| 1 | README.md | ◆ | 2026-01 | プロジェクト概要、アーキテクチャ図、クイックスタート、ディレクトリ構成 | docs/README.md, docs/PROJECT_STRUCTURE.md | ディレクトリ構成が古い（lib/core/等）、リンク切れ多数 |
| 2 | CLAUDE.md | ★ | 2026-02 | Claude Code向けプロジェクトルール、ワークフロー、コードスタイル、ビルドコマンド | .github/copilot-instructions.md | CLAUDE.mdが正本 |
| 3 | RELEASE.md | ★ | 2026-02 | リリースポリシー、バージョニング、公開ライブラリ一覧、チェックリスト | lib/docs/guides/RELEASE_GUIDE.md | 内容は補完的 |
| 4 | .github/copilot-instructions.md | ◆ | 2026-02 | GitHub Copilot向け指示（CLAUDE.mdとほぼ同内容） | CLAUDE.md | 参照パスに壊れたリンクあり（docs/refs/specs/等） |

---

## 2. docs/ 直下 — 雑多なドキュメント群

| # | ファイル | 有用性 | 作成時期 | 要約 | 重複/関連 | 備考 |
|---|---------|--------|----------|------|-----------|------|
| 5 | docs/README.md | ▽ | 2025-01 | ドキュメント目次、ナビゲーション | — | **リンク25+箇所が壊れている**（specs/, guides/, reference/は存在しない）。要全面書き直し |
| 6 | docs/PROJECT_STRUCTURE.md | ◆ | 2026-02 | 全ファイルの役割一覧（538行、非常に詳細） | README.md, docs/structure.md | 情報量は多いが更新コスト高。コードと同期が取れなくなるリスク |
| 7 | docs/structure.md | ▽ | 2026-01 | UMI-OS構造のアウトライン（ツリー形式メモ） | docs/PROJECT_STRUCTURE.md, docs/MEMO.md | 断片的、PROJECT_STRUCTUREに完全に包含される |
| 8 | docs/NOMENCLATURE.md | ★ | 2025-01 | 命名体系、用語定義、プロジェクト階層図、名前空間規約 | — | v0.10.0。用語の正本として重要。ただし一部古い（lib/umi/port等のパス） |
| 9 | docs/MEMO.md | ▽ | 2026-01 | 開発メモ（定義、ライブラリ構成、メモリ設計メモ等） | docs/NOMENCLATURE.md, docs/MEMOMEMO.md | 非構造的メモ。一部はNOMENCLATURE.mdと重複。有用な断片はあるが整理が必要 |
| 10 | docs/MEMOMEMO.md | ▽ | 2026-01 | 検討メモ（タスク優先度、アーキテクチャ役割分担、ライセンス） | docs/MEMO.md | 4項目の短いメモ。MEMO.mdに統合可能 |
| 11 | docs/LISENCE_SERVER.md | ◆ | 2026-01 | ライセンス認証サーバー仕様（Cloudflare Workers/D1/R2、Ed25519） | — | 独立した設計文書。将来機能。typo: LISENCE → LICENSE |
| 12 | docs/CLANG_ARM_MULTILIB_WORKAROUND.md | ◆ | 2026-02 | clang-arm 21.x multilib.yaml互換性ワークアラウンド | docs/CLANG_TIDY_SETUP.md | 短いが実用的。CLANG_TIDY_SETUPと内容が一部重複 |
| 13 | docs/CLANG_TIDY_SETUP.md | ◆ | 2026-02 | clang-tidy セットアップガイド（multilib問題の3つの解決策） | docs/CLANG_ARM_MULTILIB_WORKAROUND.md | 上記と統合可能 |
| 14 | docs/clang_tooling_evaluation.md | ◆ | 2026-02 | .clang-format/.clang-tidy/.clangd設定の詳細評価と推奨修正 | lib/docs/guides/CODE_QUALITY_GUIDE.md | 387行の詳細分析。CODE_QUALITY_GUIDEとの関係を整理すべき |
| 15 | docs/STM32H7.md | ◆ | 2026-01 | Cortex-M7 DCache + DMAのワークアラウンド | — | STM32H7移植時の参考資料。将来のプラットフォーム拡張に有用 |
| 16 | docs/esp32-support-investigation.md | ◆ | 2026-02 | ESP32対応調査（ツールチェーン、HAL選択肢、統合方針） | — | 将来のプラットフォーム拡張向け調査レポート |

---

## 3. docs/dev/ — 開発ガイド

| # | ファイル | 有用性 | 作成時期 | 要約 | 重複/関連 | 備考 |
|---|---------|--------|----------|------|-----------|------|
| 17 | docs/dev/GUIDELINE.md | ▽ | 2026-01 | C++実装仕様（Data/State分離、ダブルバッファリング、Arena、SPSC） | lib/docs/standards/CODING_RULE.md | 「コーディングガイドライン」ではなく設計パターン集。名前が誤解を招く。CODING_RULEとは異なる内容 |
| 18 | docs/dev/IMPLEMENTATION_PLAN.md | ◆ | 2026-02 | umios-architecture実装計画（Phase 0-7、syscall統一からController APIまで） | docs/umios-architecture/99-proposals/implementation-plan.md | 436行の詳細実装計画。proposalと重複する可能性 |
| 19 | docs/dev/SIMULATION.md | ◆ | 2026-01 | シミュレーションバックエンド比較（WASM/Renode/Cortex-M Web） | — | 220行。バックエンド比較とAPI統一の設計。実用的 |
| 20 | docs/dev/RUST.md | ◆ | 2026-01 | Rust vs C++技術選定レポート | — | 128行。言語比較の考察文書。参考資料として保持 |
| 21 | docs/dev/DEBUG_VSCODE_CORTEX_DEBUG.md | ★ | 2026-02 | VSCode + Cortex-Debug設定ガイド（2系統の生成経路、MCU DB、SVD） | lib/docs/guides/DEBUGGING_GUIDE.md | 182行。実用的で詳細。DEBUGGING_GUIDEとは補完関係 |

---

## 4. docs/refs/ — APIリファレンス・仕様書

| # | ファイル | 有用性 | 作成時期 | 要約 | 重複/関連 | 備考 |
|---|---------|--------|----------|------|-----------|------|
| 22 | docs/refs/ARCHITECTURE.md | ★ | 2025-01 | UMI全体アーキテクチャ（統一main()モデル、Processor/Controller、プラットフォーム対応） | — | v0.10.0。CLAUDE.mdから参照される正本 |
| 23 | docs/refs/CONCEPTS.md | ★ | 2025-01 | C++20 Concepts設計（ProcessorLike, HasPorts等） | — | コードの型制約の正本 |
| 24 | docs/refs/SECURITY.md | ★ | 2025-01 | セキュリティリスク分析（MPU保護、Ed25519署名、脅威モデル） | docs/umios-architecture/05-binary/14-security.md | 1113行の詳細文書 |
| 25 | docs/refs/UMIP.md | ★ | 2025-01 | UMI-Processor仕様 | — | コア仕様 |
| 26 | docs/refs/UMIC.md | ★ | 2025-01 | UMI-Controller仕様 | — | コア仕様 |
| 27 | docs/refs/UMIM.md | ★ | 2025-01 | UMI-Module バイナリ形式仕様 | docs/refs/UMIM_NATIVE_SPEC.md | コア仕様 |
| 28 | docs/refs/UMIM_NATIVE_SPEC.md | ★ | 2025-01 | UMIMネイティブバイナリ仕様 | docs/refs/UMIM.md | UMIM.mdの補完 |
| 29 | docs/refs/API_APPLICATION.md | ★ | 2025-01 | アプリケーションAPI（process(), register_processor()等） | — | CLAUDE.mdから参照 |
| 30 | docs/refs/API_KERNEL.md | ★ | 2025-01 | カーネルAPI（Syscall、IRQ、MPU） | — | |
| 31 | docs/refs/API_DSP.md | ★ | 2025-01 | DSP API（Oscillator, Filter, Envelope） | — | |
| 32 | docs/refs/API_BSP.md | ◆ | 2025-01 | BSP API（HwType, ValueType, Curve） | — | 将来仕様マーク |
| 33 | docs/refs/API_UI.md | ◆ | 2025-01 | UI API（Input, Output, Canvas） | — | 将来仕様マーク |
| 34 | docs/refs/UMIDSP_GUIDE.md | ★ | 2025-01 | DSPライブラリ使用ガイド | — | 1002行の詳細ガイド |

---

## 5. docs/umi-kernel/ — カーネル設計文書

| # | ファイル | 有用性 | 作成時期 | 要約 | 重複/関連 | 備考 |
|---|---------|--------|----------|------|-----------|------|
| 35 | docs/umi-kernel/OVERVIEW.md | ◆ | 2026-01 | カーネル概要（4タスク構成、メモリレイアウト） | docs/archive/umi-kernel/OVERVIEW.md (**同一**) | archiveと完全同一コピー |
| 36 | docs/umi-kernel/ARCHITECTURE.md | ◆ | 2026-01 | カーネルアーキテクチャ詳細 | docs/archive/umi-kernel/ARCHITECTURE.md (異なる), docs/umios-architecture/02-kernel/ | 3箇所に分散。umios-architectureが最新正本 |
| 37 | docs/umi-kernel/BOOT_SEQUENCE.md | ◆ | 2026-01 | ブートシーケンス | docs/archive/umi-kernel/BOOT_SEQUENCE.md (異なる) | umios-architecture/02-kernel/15-boot-sequence.mdが正本 |
| 38 | docs/umi-kernel/DESIGN_DECISIONS.md | ▽ | 2026-01 | 設計判断 | docs/archive/umi-kernel/DESIGN_DECISIONS.md (**同一**) | archiveと完全同一。adr.mdへ統合計画あり |
| 39 | docs/umi-kernel/IMPLEMENTATION_PLAN.md | ▽ | 2026-01 | 実装計画 | docs/archive/umi-kernel/IMPLEMENTATION_PLAN.md (異なる) | docs/dev/IMPLEMENTATION_PLAN.mdの方が最新 |
| 40 | docs/umi-kernel/MEMORY.md | ▽ | 2026-01 | メモリマップ | docs/archive/umi-kernel/MEMORY.md (**同一**) | archiveと完全同一 |
| 41 | docs/umi-kernel/adr.md | ★ | 2026-02 | Architecture Decision Records（5件） | — | 正本。簡潔で価値あり |
| 42 | docs/umi-kernel/plan.md | ★ | 2026-02 | カーネルドキュメント再構成計画 | — | メタドキュメント。本インベントリと関連 |
| 43 | docs/umi-kernel/platform/stm32f4.md | ★ | 2026-02 | STM32F4固有の実装仕様 | — | プラットフォーム固有情報の正本 |
| 44-47 | docs/umi-kernel/spec/*.md | ★ | 2026-02 | カーネル仕様正本（kernel, application, memory-protection, system-services） | — | plan.mdに基づき再構成済み。最新の正本 |
| 48-49 | docs/umi-kernel/service/*.md | ▽ | 2026-01 | ファイルシステム、シェル | docs/archive/umi-kernel/service/ (**同一**) | archiveと完全同一 |

---

## 6. docs/umios-architecture/ — OS設計仕様（全41ファイル）

| # | サブディレクトリ | ファイル数 | 有用性 | 要約 | 備考 |
|---|-----------------|-----------|--------|------|------|
| 50-52 | 00-fundamentals/ | 3+index | ★ | コア概念（AudioContext, Processor/Controller） | 実装済み |
| 53-58 | 01-application/ | 6+index | ★ | アプリ層（Events, Params, MIDI, Backend, SharedMemory） | 実装済み |
| 59-62 | 02-kernel/ | 4+index | ★ | カーネル（Scheduler, MPU, Boot, AppLoader） | 実装済み |
| 63-64 | 03-port/ | 2+index | ★ | ポート層（Syscall, Memory） | 実装済み |
| 65-69 | 04-services/ | 5+index | ★ | サービス（Shell, Updater, Storage, Diagnostics） | 実装済み |
| 70-71 | 05-binary/ | 2+index | ★ | バイナリ形式・セキュリティ | 実装済み |
| 72-75 | 06-09 dsp/usb/gfx/midi/ | 4 index | ▽ | プレースホルダのみ（将来統合予定） | 空のindex |
| 76-81 | 99-proposals/ | 5+index | ◆ | 設計提案（io-port v1-v4, syscall-redesign, span-rangef） | 一部は実装済みで役割終了 |
| 82-83 | README.md, index.md | 2 | ◆ | ナビゲーション | ほぼ同内容で重複 |

---

## 7. docs/umi-sysex/ — SysExプロトコル（7ファイル）

| # | ファイル | 有用性 | 作成時期 | 要約 | 重複/関連 | 備考 |
|---|---------|--------|----------|------|-----------|------|
| 84 | UMI_SYSEX_OVERVIEW.md | ★ | 2026-01 | SysExプロトコル概要 | docs/archive/UMI_SYSEX_PROTOCOL.md | archiveは旧版 |
| 85 | UMI_SYSEX_CONCEPT_MODEL.md | ★ | 2026-01 | コンセプトモデル | docs/archive/UXMP提案書.md | UXMP→SysExへ統合済み |
| 86 | UMI_SYSEX_DATA.md | ◆ | 2026-01 | データ仕様 | UMI_SYSEX_DATA_SPEC.md | DATA.mdとDATA_SPEC.mdが並存 |
| 87 | UMI_SYSEX_DATA_SPEC.md | ★ | 2026-01 | データ仕様（詳細版、1916行） | UMI_SYSEX_DATA.md | こちらが詳細版 |
| 88 | UMI_SYSEX_STATUS.md | ◆ | 2026-01 | ステータスプロトコル | UMI_SYSEX_STATUS_IMPLEMENTATION_NOTES.md, docs/archive/UMI_STATUS_PROTOCOL.md | |
| 89 | UMI_SYSEX_STATUS_IMPLEMENTATION_NOTES.md | ◆ | 2026-01 | ステータス実装メモ | UMI_SYSEX_STATUS.md | STATUS.mdに統合可能 |
| 90 | UMI_SYSEX_TRANSPORT.md | ★ | 2026-01 | トランスポート層 | — | |

---

## 8. docs/umi-usb/ — USB Audio（2ファイル）

| # | ファイル | 有用性 | 作成時期 | 要約 | 重複/関連 | 備考 |
|---|---------|--------|----------|------|-----------|------|
| 91 | USB_AUDIO.md | ◆ | 2026-01 | USB Audio設計 | lib/umi/usb/docs/ | libにもUSBドキュメント群がある |
| 92 | USB_AUDIO_REDESIGN_PLAN.md | ◆ | 2026-01 | USB Audio再設計計画 | lib/umi/usb/docs/design/ | libのdesign/が最新 |

---

## 9. docs/dsp/ — DSP設計資料（10ファイル）

| # | ファイル | 有用性 | 作成時期 | 要約 | 重複/関連 | 備考 |
|---|---------|--------|----------|------|-----------|------|
| 93-97 | dsp/tb303/vcf/*.md | ★ | 2026-01 | TB-303 VCFフィルタ設計・解析（5ファイル、合計5000+行） | — | 回路解析の貴重な技術資料 |
| 98-100 | dsp/tb303/vco/*.md | ★ | 2026-01 | TB-303 VCO Waveshaper（3ファイル + test/README.md） | — | トランジスタ解析含む |
| 101 | dsp/vafilter/VAFILTER_DESIGN.md | ★ | 2026-01 | Virtual Analogフィルタ設計（1076行） | — | 独立した技術資料 |

---

## 10. docs/hw_io/ — ハードウェアI/O処理設計（5ファイル）

| # | ファイル | 有用性 | 作成時期 | 要約 | 重複/関連 | 備考 |
|---|---------|--------|----------|------|-----------|------|
| 102 | hw_io/README.md | ★ | 2026-02 | 共通方針（ISR/処理ループ分離、周期設計） | — | |
| 103-106 | hw_io/button,encoder,potentiometer,midi_uart.md | ★ | 2026-02 | 各入力デバイスの処理設計 | — | 統一フォーマットで整備済み |

---

## 11. docs/web/ — Web UI設計（1ファイル）

| # | ファイル | 有用性 | 作成時期 | 要約 | 重複/関連 | 備考 |
|---|---------|--------|----------|------|-----------|------|
| 107 | web/UI.md | ◆ | 2026-01 | Web UI設計（コンポーネント構成、AudioWorklet） | docs/archive/WEB_UI_REDESIGN.md | 735行 |

---

## 12. docs/archive/ — アーカイブ済みドキュメント（36ファイル）

| # | サブカテゴリ | ファイル | 有用性 | 要約 | 備考 |
|---|-------------|---------|--------|------|------|
| 108 | README.md | 1 | ▽ | アーカイブ目次。リンクが壊れている | |
| 109-114 | umi-kernel/ | 7+2 service | ✗ | カーネル設計文書の旧版 | docs/umi-kernel/と4ファイルが完全同一。残りはumi-kernel/の方が更新版 |
| 115-117 | umi-api/ | 3 | ◆ | API設計（AudioContext, Application, EventState） | 旧APIモデルだが設計思想の参照に有用 |
| 118-122 | umidi/ | 5 | ◆ | MIDIイベントシステム設計、ジッター補償、トランスポート | 詳細な設計資料。lib/umi/midi/docs/と補完関係 |
| 123-124 | umix/ | 2 | ▽ | UMIXトランスポート・SysExの旧設計 | SysExプロトコルに統合済み |
| 125 | 現状.md | ◆ | 2026-02 | コードベース照合による分類結果 | 有用な棚卸し結果 |
| 126-128 | UXMP*.md, UXMP提案書.md | 3 | ✗ | UXMPデータ仕様（SysExに統合済み） | 提案書自体に「移行完了」の記載あり |
| 129 | UMI_SYSTEM_ARCHITECTURE.md | ◆ | 2026-01 | システムアーキテクチャ全体像 | CLAUDE.mdから参照。991行の大規模文書 |
| 130 | UMI_SYSEX_PROTOCOL.md | ▽ | 2026-01 | SysExプロトコル旧版 | docs/umi-sysex/に統合済み |
| 131 | UMI_STATUS_PROTOCOL.md | ✗ | 2026-01 | ステータスプロトコル旧版 | 現行実装と不一致 |
| 132 | UMIOS_DESIGN_DECISIONS.md | ◆ | 2026-01 | OS設計判断 | syscall番号体系は現行と一致 |
| 133 | UMIOS_CONTENTS.md | ▽ | 2026-01 | UMIOS構成説明 | PROJECT_STRUCTURE.mdに包含 |
| 134 | DESIGN_CONTEXT_API.md | ◆ | 2026-01 | AudioContext API設計 | CLAUDE.mdから参照 |
| 135 | PLAN_AUDIOCONTEXT_REFACTOR.md | ▽ | 2026-01 | AudioContextリファクタリング計画 | 完了済み |
| 136 | KERNEL_SCHEDULER_REDESIGN.md | ✗ | 2026-01 | スケジューラ再設計計画 | 実装済みで役割終了 |
| 137 | OPTIMIZATION_PLAN.md | ◆ | 2026-01 | 最適化計画（LTO等） | 検討ログとして有用 |
| 138 | STM32F4_KERNEL_FLOW.md | ◆ | 2026-01 | STM32F4カーネルフロー | 現行実装と整合 |
| 139 | WEB_UI_REDESIGN.md | ✗ | 2026-01 | Web UI再設計 | 反映済みで役割終了 |
| 140 | LIBRARY_CONTENTS.md | ▽ | 2026-01 | ライブラリ内容一覧 | PROJECT_STRUCTURE.mdに包含 |

---

## 13. lib/docs/ — ライブラリ共通標準・ガイド

### 13.1 lib/docs/standards/（3ファイル）

| # | ファイル | 有用性 | 要約 | 備考 |
|---|---------|--------|------|------|
| 141 | CODING_RULE.md | ★ | C++23コーディング規約（429行） | 正本。CLAUDE.mdから参照 |
| 142 | LIBRARY_SPEC.md | ★ | ライブラリ構造規約 | 正本。CLAUDE.mdから参照 |
| 143 | API_COMMENT_RULE.md | ★ | APIコメント規約（Doxygen） | 正本。CLAUDE.mdから参照 |

### 13.2 lib/docs/guides/（7ファイル）

| # | ファイル | 有用性 | 要約 | 備考 |
|---|---------|--------|------|------|
| 144 | GETTING_STARTED.md | ★ | 新規ライブラリ作成ガイド（Phase 1-4） | |
| 145 | BUILD_GUIDE.md | ★ | ビルド・テスト・デプロイ | |
| 146 | TESTING_GUIDE.md | ★ | テスト戦略 | CLAUDE.mdから参照 |
| 147 | DEBUGGING_GUIDE.md | ★ | デバッグ手法（pyOCD, GDB, RTT） | CLAUDE.mdから参照。847行 |
| 148 | RELEASE_GUIDE.md | ★ | リリース・パッケージ配布 | RELEASE.mdと補完関係 |
| 149 | CODE_QUALITY_GUIDE.md | ★ | コード品質ツール設定 | |
| 150 | API_DOCS_GUIDE.md | ★ | APIドキュメント生成・運用 | |

### 13.3 lib/docs/INDEX.md

| # | ファイル | 有用性 | 要約 | 備考 |
|---|---------|--------|------|------|
| 151 | INDEX.md | ★ | lib/docs全体のナビゲーション、準拠ライブラリ一覧 | |

---

## 14. lib/docs/design/ — HAL/ドライバ設計（60+ファイル）

### 14.1 design/ 直下（実装設計 9ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 152 | README.md | ★ | 設計ドキュメント全体マップ・読み方ガイド |
| 153 | integration.md | ★ | ビルド・生成・ドライバ統合設計 |
| 154 | build_system.md | ★ | ビルドシステム設計 |
| 155 | codegen_pipeline.md | ★ | コード生成パイプライン |
| 156 | testing_hw.md | ★ | ハードウェア層テスト戦略 |
| 157 | implementation_roadmap.md | ★ | Phase 0-5実装ロードマップ |
| 158 | source_of_truth_unification.md | ★ | 離散情報の統合プロセス |
| 159 | init_sequence.md | ★ | 初期化シーケンス設計 |
| 160 | interrupt_model.md | ★ | 割り込み・イベントモデル |

### 14.2 design/foundations/（3ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 161 | problem_statement.md | ★ | 現行課題整理と設計要件 |
| 162 | comparative_analysis.md | ★ | 13+フレームワーク横断比較 |
| 163 | architecture.md | ★ | UMI理想アーキテクチャ確定版 |

### 14.3 design/hal/（1ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 164 | concept_design.md | ★ | HAL Concept設計（GPIO/UART/Transport/Platform） |

### 14.4 design/board/（4ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 165-168 | architecture, config, config_analysis, project_structure | ★ | ボード設定アーキテクチャ |

### 14.5 design/pal/ + categories/（6+14ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 169-174 | 00_OVERVIEW - 05_DATA_SOURCES | ★ | PAL設計ドキュメント（概要、レイヤーモデル、カテゴリ索引、アーキテクチャ、分析、データソース） |
| 175-188 | categories/C01-C14 | ★ | 14カテゴリ別詳細（コアペリフェラル〜デバッグトレース） |

### 14.6 design/pipeline/（1ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 189 | hw_data_pipeline.md | ★ | HWデータ統合パイプライン |

### 14.7 design/research/（14ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 190-203 | arduino, zephyr, modm, esp_idf, rust_embedded, cmsis_pack, mbed_os, platformio, other_frameworks, hal_interfaces, board_config_* | ◆ | 各フレームワーク個別調査。参考資料として保持 |

### 14.8 design/archive/ + review/（13+19ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 204-216 | archive/ 直下 | ▽ | 旧設計文書（RAL, BSP, HAL分析等）。foundations/の根拠資料 |
| 217-235 | archive/review/ | ▽ | AIレビュー記録（Claude, ChatGPT, Gemini, Kimi等）。設計プロセスの記録 |

---

## 15. lib/umi/ — 各ライブラリ内ドキュメント

### 15.1 lib/umi/fs/docs/（6ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 236-241 | README, DESIGN, AUDIT, CLEANROOM_PLAN, SLIM_STORAGE_PLAN, TEST_REPORT | ◆ | ファイルシステム設計・監査。内容は有用だが整理が必要 |

### 15.2 lib/umi/midi/docs/（2ファイル）+ README

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 242-244 | README, PROTOCOL, design | ◆ | MIDIプロトコル設計 |

### 15.3 lib/umi/mmio/docs/（3ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 245-247 | IMPROVEMENTS, NAMING, USAGE | ◆ | MMIO改善計画、命名規約、使用例。umimmioのDESIGN.mdとの関係を整理すべき |

### 15.4 lib/umi/port/docs/（2+7ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 248-249 | BACKEND_SWITCHING, DAISY_POD_PLAN | ◆ | バックエンド切替、Daisy Pod計画 |
| 250-256 | design/00-06 | ★ | ポート設計文書（実装計画、原則、アーキテクチャ、Concept、HW分離、移行、MMIO統合） |

### 15.5 lib/umi/usb/docs/（6+8ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 257-262 | ASRC_DESIGN, IMPLEMENTATION_ANALYSIS, SPEED_SUPPORT, UAC2_DUPLEX, UAC_SPEC_REF, UMIUSB_REF | ◆ | USB設計資料 |
| 263-270 | design/00-07 | ★ | USB設計文書（実装計画、速度対応、HAL/WinUSB/WebUSB、APIアーキテクチャ、ISR分離、MIDI統合/分離、UAC機能） |

### 15.6 lib/umi/bench_old/（2ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 271-272 | README, KNOWN_ISSUES | ✗ | 旧ベンチマーク。umibenchに置換済み |

---

## 16. 公開ライブラリドキュメント

### 16.1 umibench（3ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 273 | README.md | ★ | ★★★★★品質リファレンス |
| 274 | docs/DESIGN.md | ★ | 11セクション設計文書 |
| 275 | docs/ja/README.md | ★ | 日本語版 |

### 16.2 umimmio（8ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 276-283 | README, DESIGN, EXAMPLES, GETTING_STARTED, INDEX, TESTING, USAGE, ja/README | ★ | 完全な標準準拠ドキュメント |

### 16.3 umiport（1ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 284 | README.md | ◆ | WIPライブラリ |

### 16.4 umirtm（3ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 285-287 | README, DESIGN, ja/README | ★ | 標準準拠 |

### 16.5 umitest（3ファイル）

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 288-290 | README, DESIGN, ja/README | ★ | 標準準拠 |

### 16.6 その他

| # | ファイル | 有用性 | 要約 |
|---|---------|--------|------|
| 291 | lib/umi/dsp/README.md | ◆ | DSPライブラリ概要 |
| 292 | lib/umi/ref/README.md | ◆ | リファレンスライブラリ |
| 293 | lib/umi/test/umitest/README.md | ▽ | 旧パスのREADME |
| 294 | examples/embedded/README.md | ◆ | 組み込みサンプル説明 |
| 295 | examples/headless_webhost/README.md | ◆ | WASMサンプル説明 |

---

## 17. docs/plan/ — 計画文書

### 17.1 docs/plan/ 直下

| # | ファイル | 有用性 | 要約 | 備考 |
|---|---------|--------|------|------|
| 296 | LIBRARY_SPEC.md | ★ | UMI ライブラリ構成 設計仕様書 v1.3.0（12ライブラリ、5レイヤー、PAL統合） | **権威ある正本** |
| 297 | IMPLEMENTATION_PLAN.md | ★ | クリーンスレート実装計画書 v1.1.0（Phase 0-4、2a/2b/2c サブフェーズ） | **権威ある正本** |

### 17.2 docs/plan/archive/ (6ファイル)

| # | ファイル | 有用性 | 要約 | 備考 |
|---|---------|--------|------|------|
| 298 | INVESTIGATION.md | ◆ | 初期調査・分析結果 | 旧版計画文書 |
| 299 | ANALYSIS.md | ◆ | 構造分析 | 旧版計画文書 |
| 300 | PROPOSAL.md | ◆ | 統合提案 | 旧版計画文書 |
| 301 | PAL_INTEGRATION_ANALYSIS.md | ◆ | PAL統合分析 | LIBRARY_SPEC v1.3.0 に反映済み |
| 302 | MIGRATION_PLAN.md | ◆ | 移行計画 | IMPLEMENTATION_PLAN v1.1.0 に統合済み |
| 303 | LIBRARY_ARCHITECTURE.md | ◆ | ライブラリアーキテクチャ | LIBRARY_SPEC v1.3.0 に統合済み |

### 17.3 docs/plan/doc-consolidation/ (10ファイル)

| # | ファイル | 有用性 | 要約 | 備考 |
|---|---------|--------|------|------|
| 304 | CONSOLIDATION_PLAN.md | ★ | ドキュメント統廃合計画 v2.0.0 | 本セット |
| 305 | DOCUMENT_INVENTORY.md | ★ | 完全ドキュメントインベントリ | 本ファイル |
| 306 | AUDIT_REPORT.md | ★ | 監査レポート v2.0 | 本セット |
| 307 | CAT_A_CORE_SPECS.md | ★ | コア仕様カテゴリ要約 | |
| 308 | CAT_B_OS_DESIGN.md | ★ | OS設計カテゴリ要約 | |
| 309 | CAT_C_PROTOCOLS.md | ★ | プロトコルカテゴリ要約 | |
| 310 | CAT_D_DEV_GUIDES.md | ★ | 開発ガイドカテゴリ要約 | |
| 311 | CAT_E_HAL_DESIGN.md | ★ | HAL/ドライバ設計カテゴリ要約 | |
| 312 | CAT_F_DSP_TECHNICAL.md | ★ | DSP/技術資料カテゴリ要約 | |
| 313 | CAT_G_LIBRARY_DOCS.md | ★ | ライブラリドキュメントカテゴリ要約 v2.0 | 12ライブラリ構成に改訂済み |

---

## 統計サマリ

| 有用性 | ファイル数 | 割合 |
|--------|-----------|------|
| ★ 高 | ~150 | 48% |
| ◆ 中 | ~90 | 29% |
| ▽ 低 | ~35 | 11% |
| ✗ 不要 | ~34 | 11% |

### 主要な問題パターン

1. **カーネル文書の3重管理** — docs/umi-kernel/, docs/archive/umi-kernel/, docs/umios-architecture/02-kernel/ に同種内容が分散（うち5ファイルは完全同一コピー）
2. **docs/README.md のリンク壊滅** — 25+のリンクが壊れている（旧ディレクトリ構造を参照）
3. **USB/MIDI/SysExの分散** — 同ドメインのドキュメントがdocs/, docs/archive/, lib/umi/*/docs/に分散
4. **コーディング規約系の散在** — CODING_RULE, GUIDELINE, clang_tooling_evaluation, CODE_QUALITY_GUIDEが分散
5. **構造説明の重複** — PROJECT_STRUCTURE, structure.md, README.mdが重複情報を持つ
6. **UXMP→SysEx移行の残骸** — 3ファイルが「移行済み」にも関わらず残存
7. **archiveの不完全性** — archiveされたファイルが非archiveディレクトリにも完全コピーで残存
8. **copilot-instructions.mdの壊れたパス** — 存在しないdocs/refs/specs/等を参照
9. **lib/ クリーンスレートとの不整合** — IMPLEMENTATION_PLAN v1.1.0 により lib/ 全体がアーカイブ・再構築されるが、現行の lib/umi/*/docs/ のインベントリはそのまま残存
