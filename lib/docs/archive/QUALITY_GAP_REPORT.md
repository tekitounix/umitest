# UMI ライブラリ品質ギャップ分析レポート

**Created**: 2026-02-07
**Updated**: 2026-02-07
**Scope**: umitest, umimmio, umirtm vs umibench (リファレンス)
**Purpose**: 品質・網羅性の差分を定量的に分析し、改善ロードマップを策定する

---

## 1. エグゼクティブサマリー

umibench をリファレンス（品質 100%）として、3 ライブラリの各品質軸を評価した。

| 品質軸 | umibench | umitest | umimmio | umirtm |
|--------|:--------:|:-------:|:-------:|:------:|
| **実装品質** | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ← ✅ 全ライブラリ品質基準達成
| **Doxygen コメント** | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ← ✅ 全ヘッダ 10%+ 達成
| **テスト網羅性** | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ← ✅ 全ライブラリ test/impl ≥1.0x, 277+テスト
| **compile-fail テスト** | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ← ✅ 全ライブラリに compile-fail テスト追加
| **examples** | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ← ✅ 各3件作成済
| **docs 充実度** | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ← ✅ DESIGN 11章構成, INDEX API参照, TESTING品質ゲート
| **日英ドキュメント** | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ← ✅ 全docs/ja/ 英語版と同等内容に更新完了
| **テスト構造化** | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ← ✅ マルチファイル化済
| **CI** | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ← ✅ ルートCI刷新(test/format/tidy/WASM), ライブラリCI改善(format-check追加)
| **著作権規約** | — | ✅ | ✅ | ✅ | ← ✅ CLAUDE.md + UMI_LIBRARY_STANDARD 5.5

---

## 2. 定量比較

### 2.1 コード行数

| メトリクス | umibench | umitest | umimmio | umirtm |
|-----------|:--------:|:-------:|:-------:|:------:|
| **ヘッダ実装** | 454 行 | 495 行 | 1,332 行 | 1,851 行 |
| **テスト** | 688 行 | 667 行 | 1,372 行 | 1,997 行 |
| **テスト/実装比率** | ✅ 1.52x | ✅ 1.35x | ✅ 1.03x | ✅ 1.08x |

### 2.2 Doxygen コメント密度

| ライブラリ | Doxygen行数 | 総行数 | 密度 | 目標 |
|-----------|:----------:|:------:|:----:|:----:|
| **umibench** | 95 | 454 | ✅ **20.9%** | ≥10% |
| **umimmio** | 191 | 1,332 | ✅ **14.3%** | ≥10% |
| **umitest** | 60 | 495 | ✅ **12.1%** | ≥10% |
| **umirtm** | 217 | 1,851 | ✅ **11.7%** | ≥10% |

### 2.3 テスト構造

| 項目 | umibench | umitest | umimmio | umirtm |
|------|:--------:|:-------:|:-------:|:------:|
| テストファイル数 | 5 (.cc) + 1 (.hh) | ✅ 4 (.cc) | ✅ 4 (.cc) + 1 (.hh) | ✅ 7 (.cc) + 1 (.hh) |
| テストケース数 | 34 | ✅ 35 | ✅ 49 | ✅ 159 |
| compile-fail テスト | ✅ 1件 | ✅ 1件 | ✅ 2件 | ✅ 2件 |
| 境界値テスト | あり | ✅ あり | ✅ あり | ✅ あり |
| エッジケーステスト | あり | ✅ あり | ✅ あり | ✅ あり |

### 2.4 docs 構成

| ドキュメント | umibench | umitest | umimmio | umirtm |
|-------------|:--------:|:-------:|:-------:|:------:|
| INDEX.md | ✅ 47行 | ✅ 14行 | ✅ 14行 | ✅ 14行 |
| DESIGN.md | ✅ 413行 | ✅ 70行 | ✅ 37行 | ✅ 40行 |
| TESTING.md | ✅ 60行 | ✅ 40行 | ✅ 31行 | ✅ 27行 |
| GETTING_STARTED.md | ✅ 60行 | ❌ | ❌ | ❌ |
| USAGE.md | ✅ 45行 | ❌ | ❌ | ❌ |
| EXAMPLES.md | ✅ 24行 | ❌ | ❌ | ❌ |
| PLATFORMS.md | ✅ 34行 | ❌ (該当なし) | ❌ (該当なし) | ❌ (該当なし) |
| ja/ 日本語版 | ✅ 全7ファイル | ✅ 全7ファイル | ✅ 全7ファイル | ✅ 全7ファイル |

---

## 3. ライブラリ別詳細分析

### 3.1 umitest

#### 3.1.1 実装品質

**良い点**:
- 単一ヘッダで完結（配布・統合が容易）
- `std::source_location` ベースの行番号自動取得
- 包括的なアサーション群（eq/ne/lt/le/gt/ge/near/true）
- `Suite::check_*` のインライン検証パターン
- Section による論理グルーピング
- 列挙型フォーマット対応

**改善すべき点**:
1. **format_value の網羅性不足** — `std::string_view`, `nullptr_t`, `std::optional` 等の一般的な型が未対応
2. **assert_false 未実装** — `assert_true(!cond)` で代用可能だがAPI一貫性のため必要
3. **assert_contains / assert_throws 等の高度なアサーション未実装**
4. **テスト失敗時のメッセージ品質** — `assert_eq` 等で「expected vs got」の表現が不統一
5. **色出力の制御** — 環境変数 `NO_COLOR` への対応なし（プリプロセッサのみ）
6. **ファイルを分割すべき** — 378行の単一ファイルは管理困難。概念的に分離可能:
   - `umitest/format.hh` — 値フォーマッタ
   - `umitest/context.hh` — TestContext
   - `umitest/suite.hh` — Suite
   - `umitest/test.hh` — 統合ヘッダ

#### 3.1.2 Doxygen コメント

**現状: Doxygen コメント 0 件** — lib 内最悪。

**必要な対応**:
- `/// @file` + `/// @brief` — ファイル先頭
- `format_value` の全 `if constexpr` 分岐に `@brief`
- `TestContext` の全アサーションに `@brief`, `@param`, `@return`
- `Suite` のメンバ関数に `@brief`, `@param`
- `record_fail`, `record_fail_cmp` に `@brief`
- データメンバに `///< `

#### 3.1.3 テスト網羅性

**不足しているテスト**:

| カテゴリ | 必要なテスト |
|---------|-------------|
| **失敗パス** | assert_eq失敗時のメッセージ出力確認 |
| **境界値** | 整数最大値/最小値の比較 |
| **浮動小数点** | NaN, Inf, -0.0, subnormal での assert_near |
| **型ミックス** | signed vs unsigned 比較 |
| **ポインタ** | nullptr 比較、ポインタフォーマット |
| **文字列** | 長い文字列のフォーマット切り詰め |
| **check API** | check_* 全メソッドの失敗パス |
| **Suite 統計** | passed/failed カウント正確性 |
| **Section** | セクション出力確認 |
| **色制御** | `UMI_TEST_NO_COLOR` 定義時のフォーマット |

#### 3.1.4 examples 必要量

| example | 説明 |
|---------|------|
| `minimal.cc` | 最小テスト（1 suite, 1 test） |
| `assertions.cc` | 全アサーションのデモ |
| `sections.cc` | セクション分けの実践例 |
| `check_style.cc` | インライン check スタイル |

---

### 3.2 umimmio

#### 3.2.1 実装品質

**良い点**:
- register.hh は高品質な型安全設計（608行）
- Doxygen コメントが 40 箇所存在（3ライブラリ中最多）
- 多様なトランスポート対応（Direct/I2C/SPI/Bitbang）
- アクセスポリシーの静的検証（RW/RO/WO）

**改善すべき点**:
1. **transport/ ヘッダの Doxygen 不足** — register.hh に比べてコメントが薄い
2. **ホスト側テスト基盤なし** — MMIO は volatile アクセスだがモック可能な設計にすべき
3. **Region/Field/Value のコンパイル時検証テスト不足** — static_assert テスト
4. **transport 間の API 一貫性** — 各トランスポートのインターフェースが統一されているか要検証

#### 3.2.2 Doxygen コメント

**現状: register.hh に 40 件あるが、他は不足**

**必要な対応**:
- transport/*.hh 全ファイルに `@file` + `@brief`
- I2C/SPI の `read`/`write` に `@param`, `@return`, `@pre`
- bitbang 実装に `@note` でクロック要件説明
- mmio.hh の各 IWYU pragma export に機能説明を追加

#### 3.2.3 テスト網羅性

**現状: 3 テストケース, 57 行** — 実装 1,610 行に対して 3.5%。

**不足しているテスト**:

| カテゴリ | 必要なテスト |
|---------|-------------|
| **Region** | 型パラメータの検証、constexpr 評価 |
| **Field** | ビットマスク生成、set/clear/toggle |
| **Value** | 列挙値のフィールド適用 |
| **DynamicValue** | 動的値のフィールド適用 |
| **アクセスポリシー** | 書き込み禁止(RO)のコンパイルエラー確認 (compile-fail) |
| **ビット操作** | 隣接/重複フィールド、全幅フィールド |
| **Transport mock** | ホスト上でのレジスタ R/W シミュレーション |
| **static_assert** | コンパイル時制約の検証 |

#### 3.2.4 examples 必要量

| example | 説明 |
|---------|------|
| `minimal.cc` | 基本的な Region/Field 定義 |
| `register_map.cc` | 実際のペリフェラル風レジスタマップ定義 |
| `transport_mock.cc` | トランスポートモックでのテストパターン |

---

### 3.3 umirtm

#### 3.3.1 実装品質

**良い点**:
- 完全な printf 実装（1,133行）— 組み込み向けとして十分な機能
- `{}` フォーマット記法の print ヘルパー
- テンプレートベースの Monitor 設計（コンパイル時サイズ決定）
- NoBlockSkip/NoBlockTrim/BlockIfFull の 3 モード

**改善すべき点**:
1. **printf.hh が巨大（1,133行）** — 内部ヘルパーの分割を検討
2. **rtm.hh のリングバッファ実装** — 境界条件のテスト不足
3. **ホスト側ヘルパー `rtm_host.hh`** の設計意図が不明瞭
4. **print.hh** のフォーマット仕様ドキュメントなし

#### 3.3.2 Doxygen コメント

**現状: 全 4 ヘッダで Doxygen コメント 0 件** — umitest と同レベル。

**必要な対応**:
- 全ファイルに `@file` + `@brief`
- `Monitor` クラステンプレートに `@tparam` 全パラメータ
- `write<N>(...)` に `@brief`, `@param`, `@return`, `@pre`
- `Mode` 列挙に `@brief` + 各値の `///<`
- printf の `%d`, `%x`, `%s` 等の対応フォーマット仕様ドキュメント
- print の `{}` フォーマット仕様ドキュメント
- `rtm_host.hh` の意図と使い方

#### 3.3.3 テスト網羅性

**現状: 3 テストケース, 71 行** — 実装 1,680 行に対して 4.2%。

**不足しているテスト**:

| カテゴリ | 必要なテスト |
|---------|-------------|
| **Monitor init** | 初期化後の全チャネル状態 |
| **Monitor read** | 書き込み→読み取りのラウンドトリップ |
| **バッファ境界** | ラップアラウンド、ちょうどフル |
| **NoBlockTrim** | バッファ超過時のトリム動作 |
| **BlockIfFull** | フル時のブロック動作（ホスト上でのシミュレーション） |
| **マルチチャネル** | 複数 up/down チャネルの独立性 |
| **printf 書式** | `%d`, `%u`, `%x`, `%o`, `%s`, `%c`, `%p`, `%%` |
| **printf フラグ** | `#`, `0`, `-`, `+`, 幅指定, 精度 |
| **printf 境界** | INT_MIN, INT_MAX, 空文字列, NULL ポインタ |
| **printf バッファ** | バッファサイズ不足時の truncation |
| **print `{}`** | 基本型、16進数、パディング |
| **print 複合** | 複数引数、エスケープ `{{` `}}` |

#### 3.3.4 examples 必要量

| example | 説明 |
|---------|------|
| `minimal.cc` | Monitor 初期化 + write |
| `printf_demo.cc` | printf フォーマット全機能デモ |
| `print_demo.cc` | `{}` フォーマットデモ |
| `host_debug.cc` | rtm_host.hh を使ったホストデバッグ例 |

---

## 4. 共通改善ロードマップ

### 4.1 優先度マトリクス

| 優先度 | 作業 | 理由 | 状態 |
|:------:|------|------|:----:|
| ~~P0~~ | ~~Doxygen コメント追加（全ヘッダ）~~ | ~~0% → DOXYGEN_STYLE準拠に~~ | ✅ 完了 |
| ~~P0~~ | ~~テスト大幅拡充~~ | ~~比率 0.03-0.31 → 目標 1.0+~~ | ✅ 完了 |
| ~~P1~~ | ~~examples 作成~~ | ~~0 → 各ライブラリ 3 件~~ | ✅ 完了 |
| ~~P1~~ | ~~docs 充実（GETTING_STARTED, USAGE）~~ | ~~INDEX + DESIGN のみ → 完全構成~~ | ✅ 完了 |
| ~~P1~~ | ~~著作権・著者規約策定~~ | ~~LICENSE/@author の使い分け~~ | ✅ 完了 |
| ~~P2~~ | ~~docs 日英対訳~~ | ~~README.md のみ → 全 docs~~ | ✅ 完了 |
| ~~P2~~ | ~~compile-fail テスト（全ライブラリ）~~ | ~~コンパイル時制約の検証~~ | ✅ 完了 (umibench:1, umimmio:2, umirtm:2, umitest:1) |
| ~~P2~~ | ~~lib/docs 再構築~~ | ~~standards/ guides/ archive/~~ | ✅ 完了 |
| ~~P3~~ | ~~umitest ファイル分割~~ | ~~435行単一ファイル → format.hh + context.hh + suite.hh + test.hh~~ | ✅ 完了 |

### 4.2 目標メトリクス

| メトリクス | 初期値 | 現状値 | 目標 | 達成 |
|-----------|---------|---------|------|:----:|
| テスト/実装比率 | 0.03-0.31 | 1.03x-1.52x (全ライブラリ) | ≥ 1.0 | ✅ |
| Doxygen 密度 | 0-6.6% | 11.7%-20.9% (全ライブラリ) | ≥ 10% | ✅ |
| テストケース数 | 3-10 | 34-159 | ≥ 20 | ✅ |
| compile-fail テスト | 0-1 | 1-2 (全ライブラリ) | ≥ 1 | ✅ |
| example 数 | 0 | 各3件 | ≥ 3 | ✅ |
| docs ファイル数 | 3-4 | 6+ | ≥ 6 | ✅ |
| 日英対訳 | 1/6 | 7/7 全ファイル英語版同等 | 7/7 | ✅ |

### 4.3 推定工数

| ライブラリ | Doxygen | テスト | examples | docs | 合計 |
|-----------|:-------:|:-----:|:--------:|:----:|:----:|
| umitest | 1h | 3h | 1h | 2h | **7h** |
| umimmio | 2h | 4h | 1h | 2h | **9h** |
| umirtm | 3h | 5h | 1.5h | 2h | **11.5h** |
| **合計** | 6h | 12h | 3.5h | 6h | **27.5h** |

---

## 5. lib/docs 再構築計画

### 5.1 現状の問題

1. **役割の曖昧さ** — 「ライブラリ共通ルール」と「実装計画」が混在
2. **LIBRARY_STRUCTURE.md** — リダイレクトのみ（7行）で体裁が悪い
3. **INSTRUCTION.md** — 入口ドキュメントだが更新が追いついていない
4. **IMPLEMENTATION_PLAN.md** — 実行中の計画書で、完了後は価値が低下
5. **ドキュメント間の依存関係が不明瞭** — どれから読むべきかわからない
6. **対象読者の区分なし** — 初心者向けと上級者向けが混在

### 5.2 再構築の原則

- **lib/docs = ライブラリ共通の標準・ガイド・リファレンス**
- 一時的な計画書は別ディレクトリ（archive/）に移動
- 読む順序を明確化
- 対象読者を明記

### 5.3 新しい構成案

```
lib/docs/
├── INDEX.md                    # → 入口。読む順序ガイド（INSTRUCTION.mdを統合）
│
├── standards/                  # === 標準・規約 ===
│   ├── LIBRARY_STANDARD.md     # ← 現 UMI_LIBRARY_STANDARD.md（リネーム）
│   ├── CODING_STYLE.md         # ← 移動
│   └── DOXYGEN_STYLE.md        # ← 移動
│
├── guides/                     # === 開発ガイド ===
│   ├── GETTING_STARTED.md      # 新規: 初めてのライブラリ作成
│   ├── TESTING.md              # ← 移動 + 拡充
│   ├── XMAKE.md                # ← 移動
│   ├── CLANG_TOOLING.md        # ← 移動
│   └── DEBUG_GUIDE.md          # ← 移動
│
├── archive/                    # === 完了/一時ドキュメント ===
│   ├── IMPLEMENTATION_PLAN.md  # ← 完了したら移動
│   └── LIBRARY_STRUCTURE.md    # ← リダイレクト、将来削除
│
└── ja/                         # === 日本語版 ===
    ├── INDEX.md
    ├── standards/
    │   └── LIBRARY_STANDARD.md
    └── guides/
        └── GETTING_STARTED.md
```

### 5.4 INDEX.md（入口）設計

```markdown
# UMI Library Development Guide

## For New Contributors
1. [Getting Started](guides/GETTING_STARTED.md) — 最初に読む
2. [Coding Style](standards/CODING_STYLE.md) — コードの書き方
3. [Doxygen Style](standards/DOXYGEN_STYLE.md) — コメントの書き方

## Standards & Rules
- [Library Standard](standards/LIBRARY_STANDARD.md) — 全ライブラリ共通の構造・規約

## Development Guides
- [Testing](guides/TESTING.md) — テスト戦略
- [xmake](guides/XMAKE.md) — ビルドシステム
- [Clang Tooling](guides/CLANG_TOOLING.md) — 静的解析
- [Debugging](guides/DEBUG_GUIDE.md) — デバッグ手法

## Reference Implementation
- [umibench](../umibench/) — 完全準拠リファレンス
```

### 5.5 削除/統合するファイル

| ファイル | 処遇 | 理由 |
|---------|------|------|
| INSTRUCTION.md | INDEX.md に統合後削除 | 役割重複 |
| LIBRARY_STRUCTURE.md | archive/ に移動 | リダイレクトのみ |
| UMI_LIBRARY_STANDARD.md | standards/ にリネーム移動 | 分類整理 |
| IMPLEMENTATION_PLAN.md | 完了まで維持、完了後 archive/ | 一時的文書 |

### 5.6 新規作成するファイル

| ファイル | 内容 |
|---------|------|
| `INDEX.md` | 入口・ナビゲーション（現 INSTRUCTION.md を拡張） |
| `guides/GETTING_STARTED.md` | 新規ライブラリ作成の step-by-step ガイド |
| `ja/INDEX.md` | 日本語版入口 |
| `ja/standards/LIBRARY_STANDARD.md` | 日本語版標準 |

### 5.7 実施順序

1. ディレクトリ作成 (`standards/`, `guides/`, `archive/`, `ja/`)
2. 既存ファイルの移動
3. INDEX.md 新規作成
4. GETTING_STARTED.md 新規作成
5. INSTRUCTION.md 削除
6. 各ファイル内のリンク更新
7. 日本語版作成

---

## 6. 全体実施スケジュール

```
Week 1: Doxygen コメント追加
  Day 1-2: umitest/test.hh
  Day 3-4: umirtm/rtm.hh, printf.hh, print.hh, rtm_host.hh
  Day 5:   umimmio/transport/*.hh

Week 2: テスト拡充
  Day 1-2: umitest テスト（失敗パス、境界値、型ミックス）
  Day 3-4: umirtm テスト（printf全書式、Monitor境界、マルチチャネル）
  Day 5:   umimmio テスト（Region/Field/Value、compile-fail）

Week 3: examples + docs
  Day 1:   全ライブラリ examples 作成
  Day 2-3: docs 充実（GETTING_STARTED, USAGE, EXAMPLES）
  Day 4:   lib/docs 再構築
  Day 5:   日英対訳、最終レビュー
```

---

## 7. 承認事項

本レポートの内容に基づき、以下の順序で実装を進める承認を求めます：

1. lib/docs 再構築（構造変更は早期に）
2. Doxygen コメント追加（最優先の品質改善）
3. テスト大幅拡充
4. examples 作成
5. docs 充実
