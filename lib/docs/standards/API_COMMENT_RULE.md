# API コメント規約

この文書は、過剰なコメントを避けつつ実用的な API ドキュメントを生成するための最小ルールです。
全 UMI ライブラリに適用されます。

## 1. 目的

- コメントは短く正確に書く。
- 実装の逐語説明ではなく、API 契約を記述する。
- 利用者と保守者が Doxygen 出力だけで主要な使い方を追える状態にする。

## 2. 必須ルール

### 2.1 ファイル先頭

すべての C++ ファイル（`.hh`/`.cc`）に以下の順序でヘッダを記述する:

```cpp
// SPDX-License-Identifier: MIT
// Copyright (c) <year>, tekitounix
/// @file
/// @brief Benchmark runner and baseline calibration API.
/// @author Shota Moriguchi @tekitounix
```

| 要素 | 必須 | 説明 |
|------|:----:|------|
| `SPDX-License-Identifier` | 必須 | ライセンス識別子 |
| `Copyright` | 必須 | 著作権表記 |
| `@file` | 必須 | Doxygen ファイルマーカー |
| `@brief` | 必須 | 責務を1文で |
| `@author` | 推奨 | 主要著者（新規ファイル作成時） |

### 2.2 公開 API（主に `include/`）

- 公開の型/関数/コンセプトに `@brief` を付ける。
- 振る舞いに影響する引数は `@param` を付ける。
- `void` 以外は `@return` を付ける。
- 意味が自明でないテンプレート引数は `@tparam` を付ける。

### 2.3 公開データ構造

- API として使う構造体メンバは `///<` で用途を短く書く。

### 2.4 条件・注意

必要な場合のみ使う。

- `@pre`: 呼び出し前提条件
- `@note`: 重要な補足
- `@warning`: 危険性（ブロッキング、ハード依存、誤用リスク）

## 3. 適用範囲

全ライブラリ (`lib/<libname>/`) の以下のディレクトリに適用:

| ディレクトリ | コメント密度 |
|-------------|-------------|
| `include/` | API 契約を十分に記述 |
| `platforms/` | ファイル責務と非自明なハード依存挙動 |
| `examples/` | ファイル説明と主要ヘルパーのみ |
| `tests/` | フィクスチャ/テスト登録関数の意図のみ |

## 4. やらないこと

- 自明な処理を行単位で説明しない。
- 単純ラッパーに長文を付けない。
- ローカル変数すべてに説明を付けない。

## 5. 推奨スタイル

- 基本は `///` のインライン形式。
- `@brief` は1文。
- `@param` は「何を渡すか」より「挙動への影響」を優先して記述。

関数テンプレート例:

```cpp
/// @brief Measure callable execution once.
/// @tparam Timer Timer implementation that satisfies TimerLike.
/// @tparam Func Callable type to execute.
/// @param func Callable to benchmark.
/// @return Elapsed counter value reported by Timer.
template<TimerLike Timer, typename Func>
typename Timer::Counter measure(Func&& func);
```

## Reference

- [Coding Rule](CODING_RULE.md) — 命名規則・スタイル規約
- [Library Spec §5.5](LIBRARY_SPEC.md) — 著作権・著者表記の詳細規約
