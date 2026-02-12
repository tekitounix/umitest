# 離散情報収束ノート（プロジェクト全体）

**ステータス:** 運用中（正本固定なし）
**最終更新:** 2026-02-10
**対象:** `lib/docs/design` / `lib/umi` / `docs` / `xmake.lua`

---

## 0. 立場

- 「唯一の正本」は前提にしない。
- 代わりに、`事実(Fact)` と `矛盾(Contradiction)` を継続更新し、判断を収束させる。
- ドキュメントは仕様そのものではなく、**判断履歴と実行順序を管理する道具**とする。

---

## 1. System Map（圧縮版）

### 1.1 実体レイヤ

- **Build 実体（root）**: `xmake.lua` は分割ライブラリ群（`umihal/umiport/umimmio/umitest/umidevice/umibench/umirtm`）を include。
- **Build 実体（lib/umi）**: `lib/umi/xmake.lua` は `umi.core` など統合ターゲット群を持つ。
- **Runtime 実体**: `lib/umi/*` に kernel/app/service/midi/dsp/usb 等が存在。
- **Design 実体**: `lib/docs/design/*` は HAL/board/PAL/codegen/build 統合設計。
- **Spec 実体**: `docs/umios-architecture/*`, `docs/umi-kernel/*`, `docs/refs/*` に API/OS 仕様。

### 1.2 依存の芯

```
design(理想) -> build設定(xmake) -> code(lib/umi, split libs) -> test実行結果
                      ^
                      |
                 docs(仕様/解説)
```

---

## 2. 収束スナップショット（2026-02-10）

### 2.1 Fact Ledger（確認済み事実）

- ファイル規模: `lib/docs/design` 84 / `docs` 190 / `lib/umi` 513 / 対象Markdown合計 271。
- root `xmake show -l targets` は分割ライブラリ系ターゲットのみを列挙。
- `xmake show -P lib/umi -l targets` では `umi.core` 等の統合ターゲット群を列挙。
- `xmake show -P lib/umi -l targets` で `includes("lib/umi/bench")` 警告（実ディレクトリは `bench_old`）。
- root `xmake test` は 18/18 pass（分割ライブラリ系テスト）。
- `docs/specs`, `docs/reference`, `docs/development`, `docs/guides`, `docs/rust` は存在しない（`docs/README.md` 記述と不一致）。
- root `README.md` が示す `doc/`, `lib/core`, `lib/dsp`, `port/` 等は現構成と不一致。

### 2.2 Constraint Ledger（制約）

- 単独開発のため、並行する複数の説明体系は維持コストが高い。
- リアルタイム領域（kernel/audio/irq）は「理想設計」より「現在の実動検証」を優先すべき。
- 仕様・計画・コードが同時更新されない前提で、矛盾管理を常設する必要がある。

---

## 3. Contradiction Ledger（優先管理）

| ID | 矛盾 | 影響 | 優先 |
|---|---|---|---|
| C-01 | `docs/README.md` の参照先と実ディレクトリが不一致 | 新規参入時の導線崩壊 | High |
| C-02 | root `README.md` の構成図が現実装と不一致 | 全体理解の誤誘導 | High |
| C-03 | `docs/umios-architecture/index.md` は「実装済み」中心、`99-proposals/implementation-plan.md` は未実装前提 | 進捗判断の二重基準化 | High |
| C-04 | root build は split libs 中心、`lib/umi` は統合スタック中心 | どのテスト結果を「全体健全性」とみなすか曖昧 | High |
| C-05 | `lib/umi/xmake.lua` が `lib/umi/bench` を参照、実体は `bench_old` | 統合ビルドの信頼性低下 | Medium |
| C-06 | 設計文書群（2026-02）と旧仕様文書群（2025系）が同レベルに見える | 判断時系列が不明瞭 | Medium |

---

## 4. Decision Queue（今判断すべきこと）

1. **Build の主軸をどちらに置くか**
   - A: root split-libs を主軸（`lib/umi` は実験）
   - B: `lib/umi` 統合を主軸（root は基盤テスト）
2. **ドキュメントの層役割を固定するか**
   - `lib/docs/design` = 設計・将来像
   - `docs/umios-architecture` = 実装追従仕様
   - `docs/README.md` = 現実導線のみ
3. **実装状況ラベルを統一するか**
   - `実装済み / 部分実装 / 設計済み / 提案` の4種に固定
4. **古い導線を削除するか残すか**
   - 旧構成記述を即削除するか、`legacy` 明示で段階撤去するか
5. **全体健全性テストの最小定義**
   - root 18 tests + `lib/umi`の代表ターゲット検証を「収束判定」にするか

---

## 5. Next Execution Slice（最小1サイクル）

**目的:** 「どれを見れば現在地が分かるか」を1回で成立させる。

1. `docs/README.md` のリンク先を現実ディレクトリへ修正（`refs`, `dev` ベース）。
2. `docs/umios-architecture/index.md` のステータス表記を `implementation-plan.md` と同じ粒度に合わせる。
3. `lib/umi/xmake.lua` の `bench` 参照を実体に合わせて修正（または意図を注記）。
4. 完了条件を満たしたら、このファイルの Fact/Contradiction を更新して次サイクルへ進む。

**完了条件:**

- リンク切れがなく、初見で「現実の入口」に到達できる。
- `実装済み` 表記がコード実体と矛盾しない。
- ビルド主軸の判断材料（root / lib/umi）が比較可能になる。

---

## 6. 軽量運用ルール（単独開発向け）

- 1回で全体最適化しない。毎回「1スライス」だけ収束。
- 各サイクルで残すのは以下のみ:
  - `F` 事実 3件
  - `C` 制約 2件
  - `X` 矛盾 3件
  - `Q` 判断待ち 3件
- 新規文書を増やすより、このファイルを更新して圧縮を維持する。
