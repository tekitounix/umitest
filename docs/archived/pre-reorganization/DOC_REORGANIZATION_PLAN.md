# ドキュメント整理計画

**作成日:** 2026-01-21
**ステータス:** 計画段階（未実行）

---

## 現状分析

### 現在のドキュメント一覧（archived除く）

| ファイル | 行数 | 内容 | 問題点 |
|----------|------|------|--------|
| API.md | 98 | APIインデックス | 適切 |
| API_APPLICATION.md | 236 | アプリAPI | 適切 |
| API_UI.md | 847 | UI/BSP API | 肥大化、BSP部分が大きい |
| API_DSP.md | 180 | DSPモジュール | 適切 |
| API_KERNEL.md | 73 | Kernel API | 薄い、詳細不足 |
| ARCHITECTURE.md | 474 | アーキテクチャ | 包括的だが一部古い記述 |
| UMIP_SPEC.md | 478 | Processor仕様 | ARCHITECTURE.mdと重複 |
| UMIC_SPEC.md | 235 | Controller仕様 | ARCHITECTURE.mdと重複 |
| UMIM_SPEC.md | 435 | バイナリ形式仕様 | 適切 |
| USE_CASES.md | 362 | ユースケース例 | 適切 |
| TEST_STRATEGY.md | 212 | テスト戦略 | 適切 |
| LIBRARY_PACKAGING.md | 178 | ライブラリ構造 | 適切 |
| SIMULATION_BACKENDS.md | 220 | シミュレーション | 適切 |
| SECURITY_ANALYSIS.md | 1114 | セキュリティ | 詳細だが長い |
| CODING_STYLE.md | 299 | コーディングスタイル | 適切 |

**合計: 15ファイル、約5400行**

---

## 発見した問題点

### 1. 内容の重複

- **ARCHITECTURE.md** と **UMIP_SPEC.md/UMIC_SPEC.md** で同じ概念を別々に説明
- 「統一main()モデル」「Processor/Control Task」の説明が3箇所に存在
- アプリケーション構造の図が複数ファイルに散在

### 2. 粒度の不均一

- **API_UI.md (847行)**: BSP I/O型定義が肥大化、本来は別ファイルが適切
- **API_KERNEL.md (73行)**: 他APIドキュメントに比べて極端に薄い
- **SECURITY_ANALYSIS.md (1114行)**: 1ファイルとしては長すぎる

### 3. 対象読者の混在

- **開発者向け**（API、アーキテクチャ）と**ユーザー向け**（チュートリアル的内容）が混在
- 仕様書（UMIP/UMIC/UMIM）と実装ガイドの境界が曖昧

### 4. ナビゲーションの困難さ

- どこから読み始めればよいか不明
- 関連ドキュメント間のリンクが不十分

---

## 整理方針

### 基本原則

1. **Single Source of Truth** - 同じ概念は1箇所でのみ説明
2. **対象読者別分離** - 入門/リファレンス/仕様を明確に分離
3. **適切な粒度** - 1ファイル200-500行を目安
4. **ナビゲーション強化** - READMEからの導線を明確に

### 対象読者の定義

| 読者 | 目的 | 必要なドキュメント |
|------|------|-------------------|
| **新規開発者** | UMIを学ぶ | クイックスタート、アーキテクチャ概要 |
| **アプリ開発者** | アプリを作る | API、ユースケース |
| **BSP開発者** | ボード対応 | BSP仕様、Kernel API |
| **コントリビュータ** | UMI自体を開発 | 仕様書、コーディングスタイル |

---

## 提案する新構成

```
docs/
├── README.md                    # 新規: ドキュメント目次
│
├── getting-started/             # 入門（新規ディレクトリ）
│   ├── QUICKSTART.md            # 新規: 5分で始めるUMI
│   └── CONCEPTS.md              # 新規: 基本概念（Processor/Controlタスク）
│
├── guides/                      # ガイド（新規ディレクトリ）
│   ├── APPLICATION.md           # USE_CASES.mdをリネーム・拡充
│   ├── DSP.md                   # API_DSP.mdをリネーム
│   ├── UI_IO.md                 # API_UI.mdからUI部分を分離
│   └── TESTING.md               # TEST_STRATEGY.mdをリネーム
│
├── reference/                   # リファレンス（新規ディレクトリ）
│   ├── API_APPLICATION.md       # 既存を移動
│   ├── API_KERNEL.md            # 既存を拡充して移動
│   └── API_BSP.md               # 新規: API_UI.mdからBSP部分を分離
│
├── specs/                       # 仕様書（新規ディレクトリ）
│   ├── ARCHITECTURE.md          # 既存を簡潔化して移動
│   ├── UMIP.md                  # UMIP_SPEC.mdをリネーム・簡潔化
│   ├── UMIC.md                  # UMIC_SPEC.mdをリネーム・簡潔化
│   ├── UMIM.md                  # UMIM_SPEC.mdをリネーム
│   └── SECURITY.md              # SECURITY_ANALYSIS.mdを分割・移動
│
├── development/                 # 開発者向け（新規ディレクトリ）
│   ├── CODING_STYLE.md          # 既存を移動
│   ├── LIBRARY_PACKAGING.md     # 既存を移動
│   └── SIMULATION.md            # SIMULATION_BACKENDS.mdをリネーム
│
└── archived/                    # 既存
```

---

## 具体的な作業計画

### Phase 1: ディレクトリ作成と移動（破壊なし）

1. 新ディレクトリ構造を作成
2. 既存ファイルをコピー（移動ではない）
3. 新しい目次README.mdを作成

### Phase 2: 重複の統合

| 統合元 | 統合先 | 内容 |
|--------|--------|------|
| UMIP_SPEC.md の概念説明部分 | ARCHITECTURE.md | Processorモデル |
| UMIC_SPEC.md の概念説明部分 | ARCHITECTURE.md | Controllerモデル |
| API_UI.md のBSP部分 | API_BSP.md（新規） | I/O型、ファクトリ関数 |

### Phase 3: 新規ドキュメント作成

| ファイル | 内容 | ソース |
|----------|------|--------|
| QUICKSTART.md | 最小アプリ作成手順 | API.mdの例を拡充 |
| CONCEPTS.md | Processor/Control, 共有メモリ | ARCHITECTURE.mdから抽出 |

### Phase 4: 既存ファイルのクリーンアップ

1. 旧ファイルから統合済み内容を削除
2. 相互参照リンクを更新
3. 旧ファイルをarchivedに移動

---

## 移行後の行数目安

| ファイル | 現在 | 目標 | 備考 |
|----------|------|------|------|
| README.md (docs) | - | 50 | 目次のみ |
| QUICKSTART.md | - | 150 | 最小限の例 |
| CONCEPTS.md | - | 200 | 図を多用 |
| APPLICATION.md | 362 | 400 | 例を追加 |
| DSP.md | 180 | 200 | 維持 |
| UI_IO.md | - | 300 | UI部分のみ |
| API_APPLICATION.md | 236 | 250 | 維持 |
| API_KERNEL.md | 73 | 200 | 拡充 |
| API_BSP.md | - | 400 | 新規分離 |
| ARCHITECTURE.md | 474 | 300 | 簡潔化 |
| UMIP.md | 478 | 200 | 簡潔化 |
| UMIC.md | 235 | 150 | 簡潔化 |
| UMIM.md | 435 | 350 | 維持 |
| SECURITY.md | 1114 | 600 | 簡潔化 |

---

## 優先順位

### 高優先度（すぐに着手すべき）

1. **API_UI.md の分割** - BSP部分を API_BSP.md に分離（現在肥大化）
2. **docs/README.md の作成** - ナビゲーション改善

### 中優先度（余裕があれば）

3. **QUICKSTART.md の作成** - 新規開発者向け入口
4. **UMIP/UMIC の簡潔化** - ARCHITECTURE.mdとの重複解消

### 低優先度（将来的に）

5. **SECURITY_ANALYSIS.md の分割** - セクション別に分離
6. **ディレクトリ構造の変更** - 全ファイルの移動

---

## 注意事項

- **既存リンクへの影響**: ファイル移動時は旧パスからのリダイレクト（シンボリックリンク）を検討
- **段階的移行**: 一度に全て変更せず、1セクションずつ移行
- **レビュー**: 大きな変更前に内容を確認

---

## 実行待ち

この計画はレビュー後に実行します。現時点では整理は行いません。
