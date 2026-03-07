# モダンテストフレームワーク調査と umitest への示唆

**目的**: Rust, Go, Zig, Swift, C++ (Catch2 中心), Kotlin (Kotest) のテスト設計を調査し、
umitest ideal-v2 の設計判断を検証・改善するための知見を抽出する。

---

## 1. テストとは何か — 各言語の答え

### 1.1 Kent Beck (TDD の創始者)

テストとは**恐怖を排除する行為**。コードが正しいことを確認するのではなく、
**変更しても壊れないという確信を得る**ために書く。

- Red → Green → Refactor の 3 ステップ
- KISS / YAGNI — テストもプロダクションコードと同じ簡潔さが必要
- テストはドキュメントでもある — 「このコードは何をするか」の生きた仕様

### 1.2 Go の答え: テストは「ただのコード」

Go は意図的にアサーションライブラリを提供しない。

Go の設計哲学（Go Wiki: TestComments の趣旨を要約）:
テストが失敗したとき、どのテストが失敗し、なぜ失敗したかが即座にわかるべき。
アサーションライブラリは新しいサブ言語を作り出す — Go 自体を使えばいい。

- `t.Errorf("got %d, want %d", got, want)` — 普通の Go コード
- `t.Fatal` で即座停止、`t.Error` で継続 — 選択はユーザーの手に
- Table-driven tests — テストケースをデータとして定義し、ループで実行

**教訓**: フレームワークが提供すべきは**最小限の基盤**であり、DSL ではない。

### 1.3 Rust の答え: マクロによる自動診断

```rust
assert_eq!(1 + 1, 3);
// thread 'test' panicked at 'assertion failed: `(left == right)`
//   left: `2`,
//   right: `3`'
```

- `assert_eq!` はマクロ — 両辺の値を自動キャプチャし、失敗時に表示
- `#[test]` 属性でテスト関数をマーク — フレームワーク不要
- panic = 失敗。catch_unwind でテストハーネスが捕捉
- `#[should_panic]` — panic すべきコードのテスト
- カスタムメッセージは `assert_eq!(a, b, "msg: {}", detail)` で追加

**教訓**: **マクロ（コンパイル時コード生成）が使えるなら、式の両辺を自動キャプチャできる**。
C++ ではマクロかテンプレートで実現するしかない。

### 1.4 Zig の答え: 言語に組み込み、comptime で検証

```zig
test "addition" {
    try std.testing.expectEqual(@as(u32, 2), 1 + 1);
}
```

- `test` ブロックが言語構文 — フレームワーク不要
- `expect`, `expectEqual`, `expectEqualSlices` — 標準ライブラリの関数
- comptime で実行中なら `@compileError` を使い、runtime なら stderr に出力
- expected (第1引数) と actual (第2引数) の順序が明確

**教訓**: **テストは言語の第一級市民であるべき**。外部ツールではなく。

### 1.5 Swift Testing の答え: マクロで XCTAssert 族を統一

```swift
#expect(a == b)  // XCTAssertEqual(a, b) の後継
#require(try parse(input))  // nil なら即座にテスト中断
```

- `#expect` と `#require` の 2 つだけ — XCTest の数十の XCTAssert を置換
- `#expect` = 継続（CHECK 相当）、`#require` = 中断（REQUIRE 相当）
- マクロが式を分解し、失敗時に両辺の値を自動表示
- 数十の専用アサーション関数を覚える必要がない

**教訓**: **アサーション API は 2 つで十分** — 「継続」と「中断」。

### 1.6 Catch2 の答え: 式分解 + SECTION

```cpp
REQUIRE(a == b);  // 失敗時: "REQUIRE( 1 == 2 )" と両辺を表示
CHECK(a == b);    // 失敗しても継続
```

- **式分解 (Expression Decomposition)**: `REQUIRE(a == b)` を template operator overload で分解し、`a` と `b` の値を個別にキャプチャ
- `REQUIRE` = 失敗で中断、`CHECK` = 失敗でも継続
- `SECTION` — セットアップコードを共有し、各 SECTION は独立実行
- テスト名は文字列リテラル — 有効な識別子である必要なし

**教訓**: **式分解は C++ で最も強力な診断手法**。ただし `&&` / `||` は分解不可（短絡評価の意味論が壊れる）。

### 1.7 Kotest (Kotlin) の答え: Soft Assertions

```kotlin
assertSoftly {
    name shouldBe "John"
    age shouldBe 30
    email shouldContain "@"
}
// → 3つ全てを実行し、失敗をまとめて報告
```

- **Soft Assertions**: ブロック内の全アサーションを実行し、最後にまとめて報告
- 通常のアサーション (`shouldBe`) は最初の失敗で例外送出
- `assertSoftly` スコープ内では失敗を蓄積

**教訓**: **「テストケース内の全命題を評価してからまとめて報告」は強力なパターン**。

---

## 2. 設計上の共通パターンと分岐点

### 2.1 失敗時の動作: 中断 vs 継続

| フレームワーク | 中断 | 継続 | デフォルト |
|---------------|------|------|-----------|
| Rust | `assert!` (panic) | なし（全 assert が panic） | 中断 |
| Go | `t.Fatal` | `t.Error` | 継続 |
| Zig | `try expect` (return error) | なし | 中断 |
| Swift | `#require` | `#expect` | 継続 |
| Catch2 | `REQUIRE` | `CHECK` | 使い分け |
| Kotest | `shouldBe` | `assertSoftly { }` | 中断 |

**観察**: 6 言語中 4 言語が「中断」と「継続」の両方を提供。これは偶然ではない。

**umitest への示唆**: 現在の v2 は TestContext の check が全て「継続」。
中断モードがない。これは Rust/Zig に比べて情報が少ない設計ではなく、
**Kotest の assertSoftly と同じ「全部評価してからまとめて報告」パターン**に相当する。

ただし、前提条件が崩れた後の check は無意味（null ポインタの先を検証する等）。
**中断モードも提供すべきか？**

### 2.2 診断メッセージの生成方法

| 手法 | 使用言語 | 利点 | 欠点 |
|------|---------|------|------|
| マクロで式キャプチャ | Rust, Swift, Zig | 両辺の値を自動表示。ユーザーの記述が最小 | マクロ依存 |
| 式分解 (template) | Catch2, doctest | マクロ＋テンプレートで両辺キャプチャ | `&&`/`||` 非対応。複雑な実装 |
| 明示的 2 引数 | Go, umitest | 実装が単純。embedded 向き | ユーザーが両辺を明示的に渡す必要 |
| DSL (shouldBe) | Kotest | 読みやすい | 独自言語化 |

**umitest への示唆**: umitest のテストは主にホストで実行するが、
フレームワーク自体は embedded (Cortex-M) でも動作可能でなければならない (P5)。
コア API に式分解を含めると、embedded ターゲットでのコンパイル時に
テンプレート膨張とバイナリサイズ増加が発生する。

**現在の「明示的 2 引数」方式 (`ctx.eq(a, b)`) はコア API として最適解**。
ただし、ホスト専用のオプショナルなマクロ層として式分解を追加する余地は残すべき。
これはコア API に影響せず、P5 を維持したまま診断の充実を図れる。

### 2.3 テストの構造化

| パターン | 使用 | 説明 |
|---------|------|------|
| 関数単位 | Rust, Go, Zig | 1 関数 = 1 テスト |
| SECTION/Subcase | Catch2, doctest | ネストしたセクションでセットアップ共有 |
| Table-driven | Go (文化), Rust (マクロ) | データ駆動テスト |
| BDD (Given/When/Then) | Catch2, Kotest | ビジネスロジック向け |

**umitest への示唆**: `suite.run("name", fn)` は「関数単位」に最も近い。
SECTION に相当する機能はない。これは**意図的な省略として正しい** —
SECTION はテスト間の暗黙の依存を生む可能性があり、
embedded テストでは単純さが重要。

### 2.4 集計と報告の分離

| フレームワーク | 集計単位 | 報告のカスタマイズ |
|---------------|---------|------------------|
| Rust (libtest) | テスト関数 | カスタムハーネスで可能 |
| Go | テスト関数 | `-v` フラグ, `-json` 出力 |
| Catch2 | TEST_CASE | Reporter インターフェース |
| Zig | test ブロック | 標準 test runner |

**大半のフレームワークがテスト関数/テストケース単位で最終結果を集計する**。
ただし Catch2 の reporter は assertions, sections, test cases を別イベントとして扱い、
passing assertions の報告も可能（JUnit reporter 等）。つまり「集計単位」と「報告粒度」は別概念。

**umitest v2 の D14（run() のみで集計）は妥当な設計判断**。
根拠は「全 FW がそうだから」ではなく、集計単位の一貫性と summary の意味の明確さ。

---

## 3. umitest v2 設計への具体的フィードバック

### 3.1 検証済み: v2 の設計判断が正しいもの

| v2 の決定 | 裏付ける言語/FW | 根拠 |
|-----------|---------------|------|
| D14: run() のみで集計 | Go, Rust, Zig | 集計単位の一貫性と summary の意味の明確さ（Catch2 は assertion 単位の報告も可能だが、最終集計は TEST_CASE 単位） |
| D1: check を free function に | Zig (std.testing) | 判定を独立関数にする設計は Zig と同じ |
| D15: reporter 注入 | Catch2 (Reporter) | 報告のカスタマイズは Catch2 で確立済み |
| D6: 色を reporter に | 全言語 | 色はフォーマッタではなく出力層の責務 |
| 短縮 API (`ctx.eq`) | Swift (`#expect`) | API 数を最小にする傾向 |

### 3.2 再考すべき: v2 に欠けているもの

#### A. 中断モード (fatal check)

**根拠**: Rust (assert!), Go (t.Fatal), Swift (#require), Catch2 (REQUIRE) の 4 言語が提供。

前提条件が崩れた場合、後続の check は無意味またはクラッシュする。
例:

```cpp
s.run("parse and use", [](auto& ctx) {
    auto result = parse(input);
    ctx.is_true(result.has_value());  // これが失敗したら…
    ctx.eq(result->name, "test");     // ← ここでクラッシュ
});
```

**提案**: `ctx.require_true()` / `ctx.require_eq()` — 失敗時に即座に return。

実装: `check_*` free function は変更不要。TestContext に `require_*` メソッドを追加し、
失敗時に early return するだけ。

```cpp
// ctx.require_eq は check_eq を呼び、失敗なら return
template <typename A, typename B>
bool require_eq(const A& a, const B& b,
                std::source_location loc = std::source_location::current()) {
    auto r = check_eq(a, b, loc);
    record(r);
    return r.passed;  // 呼び出し側: if (!ctx.require_eq(a, b)) return;
}
```

ただし `if (!ctx.require_eq(...)) return;` はボイラープレート。
マクロなしでは解決困難。**これを許容するか、マクロを導入するかは設計判断**。

#### B. カスタムメッセージ

**根拠**: Rust (`assert_eq!(a, b, "context: {}", detail)`), Go (`t.Errorf("...")`),
Catch2 (`INFO("...")` + `REQUIRE`), Swift (`#expect(a == b, "reason")`).

全フレームワークが失敗時のカスタムメッセージを何らかの形で提供。
現在の v2 は `is_true(cond, msg)` のみカスタムメッセージ対応。
`eq`, `ne` 等にはカスタムメッセージ引数がない。

**提案**: 比較系 check にもオプショナルなメッセージ引数を追加するか、
Catch2 の `INFO` のようなコンテキスト蓄積メカニズムを提供する。

#### C. 式分解の可能性（将来拡張）

Catch2 と doctest が証明した通り、C++ でも template operator overload で
式分解は可能。umitest のテストは主にホストで実行するため、
式分解自体がホスト環境で使えないわけではない。

ただし、umitest のコア API は embedded でもコンパイル可能でなければならない (P5)。
式分解をコア API に含めると、embedded ターゲットでコンパイルされた際に
テンプレート膨張とバイナリサイズ増加が発生する。
コア API と式分解を分離すれば、P5 を維持したまま診断を充実できる。

**提案**: v2 のコア設計は「明示的 2 引数」を維持しつつ、
ホスト専用のオプショナルなマクロ層（例: `expr_check.hh`）を
将来追加できるように check.hh の設計を閉じておく
（check 関数が constexpr bool を返す設計はマクロラッパーとの互換性が高い）。

---

## 4. テストの本質 — 調査から得た結論

### 4.1 テストの目的（全言語に共通）

1. **変更への確信**: コードを変更しても壊れないことを確認する（Kent Beck）
2. **仕様の明文化**: テストは生きたドキュメント（全言語共通）
3. **素早いフィードバック**: 失敗を早く知り、原因を特定する（全 CI/CD 文化）

### 4.2 良いテストフレームワークの条件（全言語に共通）

| 条件 | 説明 | 実現例 |
|------|------|--------|
| **最小の儀式** | テストを書くのに必要なボイラープレートが最小 | Zig: `test "name" { }`, Go: `func TestX(t *testing.T) { }` |
| **明確な診断** | 失敗時に「何が」「なぜ」失敗したかがわかる | Rust: 両辺の値表示, Catch2: 式分解 |
| **独立性** | テスト間に依存がない | 全言語共通 |
| **速度** | テストは頻繁に実行するため高速でなければならない | Go: 並列実行, Zig: incremental |
| **外部依存なし** | フレームワーク自体が軽量 | Zig: 標準ライブラリ, Go: 標準ライブラリ |

### 4.3 必要十分なテスト API

調査から導かれる「必要十分」な API セット:

| カテゴリ | 必須 | 理由 |
|---------|------|------|
| 等値比較 | `eq(a, b)` | 最も基本的なテスト |
| 不等値比較 | `ne(a, b)` | 否定条件のテスト |
| 順序比較 | `lt`, `le`, `gt`, `ge` | 数値・範囲のテスト |
| 近似比較 | `near(a, b, eps)` | 浮動小数点テスト |
| 真偽判定 | `is_true(cond)` | 複合条件のテスト |
| **中断判定** | `require(cond)` | 前提条件の検証 (Swift #require, Catch2 REQUIRE 相当) |

**不要なもの**:
- 文字列マッチ (`contains`, `starts_with`) — `is_true(sv.contains(...))` で代替
- 例外テスト — umitest は例外を使わない環境向け
- BDD 記法 — embedded テストには不要な複雑さ
- Table-driven テスト支援 — C++ のループで自然に実現可能

---

## 5. ideal-v2 への改訂提案まとめ

| # | 提案 | 優先度 | 根拠 |
|---|------|--------|------|
| S1 | `require_eq` / `require_true` 中断モード追加 | High | 4/6 言語が提供。前提条件崩壊時の安全性 |
| S2 | 比較系 check にカスタムメッセージ引数追加 | Medium | 全言語が提供。診断情報の充実 |
| S3 | 式分解はホスト専用オプショナル層として将来追加可能と明記。コア API は変更なし | Low | Catch2 で実証済み。コア API は P5 (embedded 対応) のため明示的 2 引数を維持し、ホスト専用層で診断を充実 |
| S4 | v2 の残り設計（D1-D15 のうち S1-S3 対象外のもの）は妥当 | — | 本調査で主要な設計判断が他言語で裏付け済み。ただし S1-S3 の追加を推奨 |

---

## Sources

### Rust
- [How to Write Tests - The Rust Programming Language](https://doc.rust-lang.org/book/ch11-01-writing-tests.html)
- [assert_eq! - Rust std](https://doc.rust-lang.org/std/macro.assert_eq.html)
- [Custom Test Frameworks RFC](https://rust-lang.github.io/rfcs/2318-custom-test-frameworks.html)
- [Testing - Writing an OS in Rust](https://os.phil-opp.com/testing/)

### Go
- [Go Wiki: TableDrivenTests — 公式](https://go.dev/wiki/TableDrivenTests)
- [Go Wiki: TestComments — 公式](https://go.dev/wiki/TestComments)
- [Go testing package — 公式](https://pkg.go.dev/testing)
- [Go Unit Testing Best Practices 2025](https://www.glukhov.org/post/2025/11/unit-tests-in-go/) (二次資料)

### Zig
- [std/testing.zig source](https://github.com/ziglang/zig/blob/master/lib/std/testing.zig)
- [Zig Documentation - Testing](https://ziglang.org/documentation/master/)
- [Thoughts on Zig Testing](https://nathancraddock.com/blog/thoughts-on-zig-test/)

### Swift
- [Swift Testing GitHub — 公式](https://github.com/swiftlang/swift-testing)
- [Using #expect macro — SwiftLee](https://www.avanderlee.com/swift-testing/expect-macro/) (二次資料)
- [#require macro — SwiftLee](https://www.avanderlee.com/swift-testing/require-macro/) (二次資料)

### C++ (Catch2 中心)
- [Catch2 Assertions — 公式 docs](https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md)
- [Catch2 Test Cases and Sections — 公式 docs](https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md)
- [Catch2 GitHub — 公式](https://github.com/catchorg/Catch2)
- [Expression Decomposition 解説 — Fekir's Blog](https://fekir.info/post/decomposing-an-expression/) (二次資料)

**注**: doctest は Catch2 と同系統の設計（SECTION → Subcase, REQUIRE/CHECK 同等）だが、
本調査では一次資料に基づく独立した分析は行っていない。doctest 固有の設計判断については
[doctest 公式 docs](https://github.com/doctest/doctest/blob/master/doc/markdown/readme.md) を参照。

### Kotlin (Kotest)
- [Kotest Soft Assertions](https://kotest.io/docs/assertions/soft-assertions.html)
- [Kotest Assertions](https://kotest.io/docs/assertions/assertions.html)

### General
- [Canon TDD - Kent Beck](https://tidyfirst.substack.com/p/canon-tdd)
- [TDD on Martin Fowler's Bliki](https://martinfowler.com/bliki/TestDrivenDevelopment.html)
