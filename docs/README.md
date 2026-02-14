# UMI ドキュメント

UMI (Universal Musical Instruments) プラットフォームのドキュメントです。

---

## クイックナビゲーション

| 読みたい人 | 推奨ドキュメント |
|------------|-----------------|
| **UMI を初めて理解したい** | [refs/ARCHITECTURE.md](refs/ARCHITECTURE.md) |
| **アプリを作りたい** | [refs/API_APPLICATION.md](refs/API_APPLICATION.md) |
| **コードを書く** | [lib/docs/standards/CODING_RULE.md](../lib/docs/standards/CODING_RULE.md) |
| **ライブラリを作る** | [lib/docs/guides/GETTING_STARTED.md](../lib/docs/guides/GETTING_STARTED.md) |
| **テストを書く** | [lib/docs/guides/TESTING_GUIDE.md](../lib/docs/guides/TESTING_GUIDE.md) |
| **デバッグする** | [lib/docs/guides/DEBUGGING_GUIDE.md](../lib/docs/guides/DEBUGGING_GUIDE.md) |
| **カーネルを理解する** | [umi-kernel/spec/kernel.md](umi-kernel/spec/kernel.md) |

---

## ドキュメントカテゴリ

### A. コア仕様 — [refs/](refs/)

UMI のアーキテクチャ、Concepts 設計、UMIP/UMIC/UMIM 仕様、API リファレンス。

| ドキュメント | 内容 |
|-------------|------|
| [ARCHITECTURE.md](refs/ARCHITECTURE.md) | 全体アーキテクチャ（統一 main() モデル、Processor/Controller） |
| [CONCEPTS.md](refs/CONCEPTS.md) | C++20 Concepts 設計 |
| [UMIP.md](refs/UMIP.md) / [UMIC.md](refs/UMIC.md) / [UMIM.md](refs/UMIM.md) | コア仕様群 |
| [SECURITY.md](refs/SECURITY.md) | セキュリティリスク分析、MPU 保護 |
| [API_APPLICATION.md](refs/API_APPLICATION.md) | アプリケーション API |
| [API_KERNEL.md](refs/API_KERNEL.md) | カーネル API |
| [API_DSP.md](refs/API_DSP.md) | DSP API |
| [UMIDSP_GUIDE.md](refs/UMIDSP_GUIDE.md) | DSP ライブラリガイド |

### B. OS 設計 — [umi-kernel/](umi-kernel/) + [umios-architecture/](umios-architecture/)

カーネル仕様、OS アーキテクチャ設計。

| ドキュメント | 内容 |
|-------------|------|
| [umi-kernel/spec/](umi-kernel/spec/) | カーネル仕様正本（kernel, application, memory-protection, system-services） |
| [umi-kernel/adr.md](umi-kernel/adr.md) | Architecture Decision Records |
| [umi-kernel/platform/stm32f4.md](umi-kernel/platform/stm32f4.md) | STM32F4 固有実装仕様 |
| [umios-architecture/](umios-architecture/) | OS 設計仕様（41 ファイル） |

### C. プロトコル — [umi-sysex/](umi-sysex/)

MIDI SysEx ベースのデバイス間通信プロトコル。

| ドキュメント | 内容 |
|-------------|------|
| [UMI_SYSEX_OVERVIEW.md](umi-sysex/UMI_SYSEX_OVERVIEW.md) | プロトコル群概要 |
| [UMI_SYSEX_TRANSPORT.md](umi-sysex/UMI_SYSEX_TRANSPORT.md) | トランスポート共通仕様 |
| [UMI_SYSEX_STATUS.md](umi-sysex/UMI_SYSEX_STATUS.md) | 状態取得・パラメータ・メーター |
| [UMI_SYSEX_DATA_SPEC.md](umi-sysex/UMI_SYSEX_DATA_SPEC.md) | データ交換詳細仕様 |
| [UMI_SYSEX_CONCEPT_MODEL.md](umi-sysex/UMI_SYSEX_CONCEPT_MODEL.md) | 概念モデル |

### D. 開発ガイド — [../lib/docs/](../lib/docs/) + [dev/](dev/)

コーディング規約、ビルド、テスト、デバッグ、リリース。

| ドキュメント | 内容 |
|-------------|------|
| [CODING_RULE.md](../lib/docs/standards/CODING_RULE.md) | コーディングスタイル正本 |
| [LIBRARY_SPEC.md](../lib/docs/standards/LIBRARY_SPEC.md) | ライブラリ構造規約 |
| [API_COMMENT_RULE.md](../lib/docs/standards/API_COMMENT_RULE.md) | Doxygen コメント規約 |
| [BUILD_GUIDE.md](../lib/docs/guides/BUILD_GUIDE.md) | ビルド・テスト・デプロイ |
| [TESTING_GUIDE.md](../lib/docs/guides/TESTING_GUIDE.md) | テスト戦略 |
| [DEBUGGING_GUIDE.md](../lib/docs/guides/DEBUGGING_GUIDE.md) | デバッグ手法 |
| [dev/DESIGN_PATTERNS.md](dev/DESIGN_PATTERNS.md) | C++ 設計パターン集 |
| [dev/CLANG_SETUP.md](dev/CLANG_SETUP.md) | clang ツール設定 |

### E. HAL/ドライバ設計 — [../lib/docs/design/](../lib/docs/design/)

HAL Concept、PAL、ボード設定、コード生成パイプライン。60+ ファイルの設計資料群。

→ [lib/docs/design/README.md](../lib/docs/design/README.md) を参照

### F. DSP/技術資料 — [dsp/](dsp/) + [hw_io/](hw_io/)

DSP アルゴリズム解析、ハードウェア I/O 処理設計。

| サブディレクトリ | 内容 |
|-----------------|------|
| [dsp/tb303/vcf/](dsp/tb303/vcf/) | TB-303 VCF フィルター完全解析（5 ファイル） |
| [dsp/tb303/vco/](dsp/tb303/vco/) | TB-303 VCO Waveshaper（3 ファイル） |
| [dsp/vafilter/](dsp/vafilter/) | Virtual Analog フィルター設計 |
| [hw_io/](hw_io/) | ボタン・エンコーダ・ADC・MIDI UART 処理設計 |

---

## その他のドキュメント

| ファイル | 内容 |
|---------|------|
| [NOMENCLATURE.md](NOMENCLATURE.md) | 命名体系・用語定義 |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | プロジェクト構成 |
| [LICENSE_SERVER.md](LICENSE_SERVER.md) | ライセンス認証サーバー仕様 |
| [STM32H7.md](STM32H7.md) | Cortex-M7 DCache ワークアラウンド |
| [esp32-support-investigation.md](esp32-support-investigation.md) | ESP32 対応調査 |

---

## 計画文書

| ファイル | 内容 |
|---------|------|
| [plan/LIBRARY_SPEC.md](plan/LIBRARY_SPEC.md) | ライブラリ構成 設計仕様書 v1.3.0 |
| [plan/IMPLEMENTATION_PLAN.md](plan/IMPLEMENTATION_PLAN.md) | クリーンスレート実装計画書 v1.1.0 |
| [plan/doc-consolidation/](plan/doc-consolidation/) | ドキュメント統廃合計画 |
