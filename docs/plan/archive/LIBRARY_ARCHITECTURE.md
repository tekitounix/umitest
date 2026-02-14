# UMI ライブラリ構成 — 索引

**作成日:** 2026-02-14
**調査方法:** 9チーム並列調査 + 3チーム × 2ラウンド監査

## 主要文書

| 文書 | 内容 |
|------|------|
| **[LIBRARY_SPEC.md](LIBRARY_SPEC.md)** | 理想的なライブラリ構成の**設計仕様書**。12ライブラリの構造、レイヤーモデル、依存関係、名前空間規約、xmakeビルドシステム、品質基準を定義する。 |
| **[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)** | クリーンスレート**実装計画書**。現行コードをアーカイブし、5フェーズで12ライブラリを新規構築する手順を定義する。 |
| **[PAL_INTEGRATION_ANALYSIS.md](PAL_INTEGRATION_ANALYSIS.md)** | PAL/コード生成パイプラインと12ライブラリ構成の**統合分析**。LIBRARY_SPEC と IMPLEMENTATION_PLAN への反映方針を定義する。 |
| **[MIGRATION_PLAN.md](MIGRATION_PLAN.md)** | （参考）互換性を考慮した移行計画書。IMPLEMENTATION_PLAN.md が正本。 |

## アーカイブ（調査過程）

| 文書 | 内容 |
|------|------|
| [INVESTIGATION.md](archive/INVESTIGATION.md) | コードベース調査結果 |
| [ANALYSIS.md](archive/ANALYSIS.md) | 設計品質分析 |
| [PROPOSAL.md](archive/PROPOSAL.md) | 実行提案 |

## ドキュメント整理計画

| 文書 | 内容 |
|------|------|
| [CONSOLIDATION_PLAN.md](doc-consolidation/CONSOLIDATION_PLAN.md) | ドキュメント整理計画 |
| [DOCUMENT_INVENTORY.md](doc-consolidation/DOCUMENT_INVENTORY.md) | 既存ドキュメント棚卸し |
| [AUDIT_REPORT.md](doc-consolidation/AUDIT_REPORT.md) | 整理計画の監査結果 |
| [CAT_A〜G](doc-consolidation/) | カテゴリ別分析（7文書） |
