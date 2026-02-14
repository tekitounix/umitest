# Archive

このディレクトリは、正本に統合済みまたは役割終了したドキュメントのアーカイブです。
git 履歴で内容を参照できます。

## 保持ファイル

| サブディレクトリ | 内容 | 正本の参照先 |
|----------------|------|-------------|
| umi-kernel/ | カーネル設計文書の旧版（6ファイル + service/） | docs/umi-kernel/spec/ |
| umi-api/ | 旧 API 設計（AudioContext, Application, EventState） | docs/refs/ |
| umidi/ | MIDI イベントシステム設計、ジッター補償 | lib/umi/midi/docs/ |
| 現状.md | コードベース照合結果 | — |
| UMIOS_DESIGN_DECISIONS.md | OS 設計判断 | docs/umi-kernel/adr.md |
| DESIGN_CONTEXT_API.md | AudioContext API 設計 | docs/refs/ |
| STM32F4_KERNEL_FLOW.md | STM32F4 カーネルフロー | docs/umi-kernel/platform/stm32f4.md |
| OPTIMIZATION_PLAN.md | 最適化計画（LTO 等） | — |
| UMI_SYSTEM_ARCHITECTURE.md | システムアーキテクチャ全体像 | docs/umios-architecture/ |

## 削除済み（git 履歴に保存）

- UXMP_SPECIFICATION.md, UXMP_DATA_SPECIFICATION.md, UXMP提案書.md → SysEx に統合済み
- UMI_STATUS_PROTOCOL.md → 現行実装と不一致
- UMI_SYSEX_PROTOCOL.md → docs/umi-sysex/ に統合済み
- KERNEL_SCHEDULER_REDESIGN.md → 実装済み
- WEB_UI_REDESIGN.md → 反映済み
- PLAN_AUDIOCONTEXT_REFACTOR.md → 完了済み
- UMIOS_CONTENTS.md, LIBRARY_CONTENTS.md → PROJECT_STRUCTURE.md に包含
- umix/ → SysEx プロトコルに統合済み
