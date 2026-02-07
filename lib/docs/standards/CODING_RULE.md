# コーディングスタイルガイド

UMI プロジェクトのコーディングスタイルは `.clang-format`、`.clang-tidy`、`.clangd` で機械的に強制されます。
このドキュメントは各設定ファイルの **正本 (single source of truth)** です。設定を変更する場合はまずこのドキュメントを更新し、設定ファイルを合わせてください。

---

## 1. .clang-format（全文）

```yaml
---
BasedOnStyle: LLVM
Language: Cpp
Standard: Latest

# Basic formatting
IndentWidth: 4
ColumnLimit: 120
PointerAlignment: Left
ReferenceAlignment: Left
BinPackArguments: false
BinPackParameters: false

# C++ specific
RequiresClausePosition: OwnLine
IndentRequiresClause: true
AllowShortFunctionsOnASingleLine: Inline
AlwaysBreakTemplateDeclarations: Yes

# Include sorting
IncludeBlocks: Regroup
SortIncludes: CaseSensitive
IncludeCategories:
  - Regex:    '^<.*\.h>'
    Priority: 1
  - Regex:    '^<.*>'
    Priority: 2
  - Regex:    '^".*"'
    Priority: 3

# Alignment
IndentPPDirectives: BeforeHash
AlignConsecutiveMacros: AcrossEmptyLines
AlignOperands: Align
AlignEscapedNewlines: Left
AlignTrailingComments: true

# Line breaks
MaxEmptyLinesToKeep: 1
EmptyLineAfterAccessModifier: Never
EmptyLineBeforeAccessModifier: LogicalBlock
LambdaBodyIndentation: Signature
```

### 設定理由

| 設定 | 値 | 理由 |
|------|-----|------|
| `BasedOnStyle` | `LLVM` | 広く採用されているスタイルをベースに最小限のカスタマイズ |
| `Language` | `Cpp` | C++ ソース対象 |
| `Standard` | `Latest` | C++23 の構文（`requires` 等）を正しく解析 |
| `IndentWidth` | `4` | 組込みコードの可読性を確保しつつ行幅を節約 |
| `ColumnLimit` | `120` | テンプレート重用コードで 80 桁は狭すぎるが、無制限は避ける |
| `PointerAlignment` | `Left` | 型に付ける (`int* ptr`)。C++ 慣習に従う |
| `ReferenceAlignment` | `Left` | ポインタと統一 (`const std::string& ref`) |
| `BinPackArguments` | `false` | 多引数は 1 行 1 引数で整列。差分が見やすい |
| `BinPackParameters` | `false` | 同上（宣言側） |
| `RequiresClausePosition` | `OwnLine` | `requires` 節を独立行にして可読性確保 |
| `IndentRequiresClause` | `true` | `requires` 節をインデントしてテンプレート宣言と区別 |
| `AllowShortFunctionsOnASingleLine` | `Inline` | ゲッター等の短い関数はインライン可。クラス外定義は展開 |
| `AlwaysBreakTemplateDeclarations` | `Yes` | `template<...>` は必ず独立行。定義と型パラメータを分離 |
| `IncludeBlocks` | `Regroup` | C → C++ → プロジェクトの 3 グループに自動再配置 |
| `SortIncludes` | `CaseSensitive` | 同一グループ内をアルファベット順に。大文字小文字区別 |
| `IncludeCategories` | (3段階) | C 標準 `.h` → C++ 標準 `<...>` → プロジェクト `"..."` |
| `IndentPPDirectives` | `BeforeHash` | `#if` ネスト内の `#include` / `#define` をインデント |
| `AlignConsecutiveMacros` | `AcrossEmptyLines` | マクロ定義値を空行越えで整列。レジスタマップの可読性向上 |
| `AlignOperands` | `Align` | 複数行演算子のオペランドを整列 |
| `AlignEscapedNewlines` | `Left` | マクロのバックスラッシュを左揃え |
| `AlignTrailingComments` | `true` | 行末コメントを整列 |
| `MaxEmptyLinesToKeep` | `1` | 連続空行を 1 行に制限。ファイルの無駄な膨張防止 |
| `EmptyLineAfterAccessModifier` | `Never` | `public:` の直後に空行を入れない。コンパクトに |
| `EmptyLineBeforeAccessModifier` | `LogicalBlock` | アクセス修飾子の前は論理ブロック境界でのみ空行 |
| `LambdaBodyIndentation` | `Signature` | ラムダ本体をシグネチャ基準でインデント |

---

## 2. .clang-tidy（全文）

```yaml
Checks: >
  -*,
  bugprone-*,
  -bugprone-dynamic-static-initializers,
  -bugprone-easily-swappable-parameters,
  clang-analyzer-*,
  -clang-analyzer-core.FixedAddressDereference,
  performance-*,
  modernize-*,
  -modernize-use-trailing-return-type,
  -modernize-use-std-print,
  readability-*,
  -readability-magic-numbers,
  -readability-uppercase-literal-suffix,
  -readability-identifier-length,
  misc-*,
  -misc-non-private-member-variables-in-classes

WarningsAsErrors: 'bugprone-*,clang-analyzer-*,performance-move-const-arg'
HeaderFilterRegex: '(lib|examples|tests)/.*'
SystemHeaders: false

CheckOptions:
  # Functions/methods: lower_case
  - { key: readability-identifier-naming.FunctionCase,         value: lower_case }
  - { key: readability-identifier-naming.MethodCase,           value: lower_case }
  - { key: readability-identifier-naming.FunctionIgnoredRegexp, value: '^_.*|.*_Handler$|.*_IRQHandler$' }
  - { key: readability-identifier-naming.MethodIgnoredRegexp,  value: '^(operator|~).*' }

  # Variables: lower_case
  - { key: readability-identifier-naming.VariableCase,         value: lower_case }
  - { key: readability-identifier-naming.ParameterCase,        value: lower_case }
  - { key: readability-identifier-naming.MemberCase,           value: lower_case }
  - { key: readability-identifier-naming.ConstexprVariableCase, value: lower_case }
  - { key: readability-identifier-naming.GlobalVariableCase,   value: lower_case }
  - { key: readability-identifier-naming.StaticVariableCase,   value: lower_case }
  - { key: readability-identifier-naming.VariableIgnoredRegexp, value: '^[A-Z][A-Z0-9_]*$|^_.*' }
  - { key: readability-identifier-naming.StaticVariableIgnoredRegexp, value: '^[A-Z][A-Z0-9_]*$|^_.*' }
  - { key: readability-identifier-naming.GlobalVariableIgnoredRegexp, value: '^_.*' }

  # Types: CamelCase
  - { key: readability-identifier-naming.TypeCase,             value: CamelCase }
  - { key: readability-identifier-naming.ClassCase,            value: CamelCase }
  - { key: readability-identifier-naming.StructCase,           value: CamelCase }
  - { key: readability-identifier-naming.EnumCase,             value: CamelCase }
  - { key: readability-identifier-naming.TypeTemplateCase,     value: CamelCase }
  - { key: readability-identifier-naming.TypeAliasCase,        value: CamelCase }
  - { key: readability-identifier-naming.ConceptCase,          value: CamelCase }
  - { key: readability-identifier-naming.NamespaceCase,        value: lower_case }

  # ALL_CAPS exceptions (vendor names)
  - { key: readability-identifier-naming.TypeIgnoredRegexp,    value: '^[A-Z][A-Z0-9_]*$' }
  - { key: readability-identifier-naming.ClassIgnoredRegexp,   value: '^[A-Z][A-Z0-9_]*$' }
  - { key: readability-identifier-naming.StructIgnoredRegexp,  value: '^[A-Z][A-Z0-9_]*$' }
  - { key: readability-identifier-naming.TypeAliasIgnoredRegexp, value: '.*_t$|^[A-Z][A-Z0-9_]*$' }

  # Enum constants: UPPER_CASE
  - { key: readability-identifier-naming.EnumConstantCase,       value: UPPER_CASE }
  - { key: readability-identifier-naming.ScopedEnumConstantCase, value: UPPER_CASE }
```

### 2.1 チェックカテゴリ

| カテゴリ | 理由 |
|----------|------|
| `bugprone-*` | バグを誘発しやすいパターンを検出。エラー扱い |
| `clang-analyzer-*` | Clang 静的解析。パスセンシティブなバグ検出。エラー扱い |
| `performance-*` | 不要コピー、move 漏れ等のパフォーマンス問題検出 |
| `modernize-*` | レガシー記法から C++17/20/23 への移行を促進 |
| `readability-*` | 命名規則・可読性の機械的強制 |
| `misc-*` | 上記に分類されないが有用なチェック |

### 2.2 無効化されたチェックと理由

| チェック | 理由 |
|----------|------|
| `bugprone-dynamic-static-initializers` | `static constexpr` での誤検出が頻発 |
| `bugprone-easily-swappable-parameters` | DSP/組込みでは同型引数が頻出（ADSR パラメータ、座標値等） |
| `clang-analyzer-core.FixedAddressDereference` | MMIO の固定アドレスレジスタアクセスで必ず誤検出 |
| `modernize-use-trailing-return-type` | 後置戻り値型の強制は可読性を下げるケースが多い |
| `modernize-use-std-print` | 組込み向け独自 printf 実装 (umirtm) があり `std::print` は使わない |
| `readability-magic-numbers` | 組込みではレジスタオフセット・ビットマスク等の数値リテラルが頻出 |
| `readability-uppercase-literal-suffix` | `1.0f` vs `1.0F` は些末。強制する価値がない |
| `readability-identifier-length` | 組込み/DSP では `x`, `fs`, `sr` 等の短い変数名が業界標準 |
| `misc-non-private-member-variables-in-classes` | POD 的な構造体・レジスタマップ定義で public メンバが必須 |

### 2.3 エラー扱いの警告

```yaml
WarningsAsErrors: 'bugprone-*,clang-analyzer-*,performance-move-const-arg'
```

| 対象 | 理由 |
|------|------|
| `bugprone-*` | バグの可能性が高く、警告のまま放置すべきでない |
| `clang-analyzer-*` | 静的解析の検出結果は原則すべて修正対象 |
| `performance-move-const-arg` | `const` 引数への `std::move` は意図と異なるコピーを発生させる |

### 2.4 ヘッダフィルタ

```yaml
HeaderFilterRegex: '(lib|examples|tests)/.*'
SystemHeaders: false
```

| 設定 | 理由 |
|------|------|
| `HeaderFilterRegex` | プロジェクトの `lib/`、`examples/`、`tests/` 配下のみ解析。ベンダーヘッダ (`.refs/`) は対象外 |
| `SystemHeaders: false` | システムヘッダの警告は抑制。修正対象外のノイズを排除 |

### 2.5 命名規則

| 対象 | スタイル | 例 |
|------|----------|-----|
| 関数 | `lower_case` | `process_audio()`, `get_value()` |
| メソッド | `lower_case` | `tick()`, `reset()` |
| 変数・パラメータ | `lower_case` | `sample_rate`, `buffer_size` |
| メンバ変数 | `lower_case` | `gain`, `cutoff`, `phase` |
| constexpr 変数 | `lower_case` | `max_voices`, `default_gain` |
| グローバル変数 | `lower_case` | `g_instance` |
| 静的変数 | `lower_case` | `s_counter` |
| 型・クラス・構造体 | `CamelCase` | `AudioProcessor`, `MidiEvent` |
| 列挙型 | `CamelCase` | `EventType`, `Priority` |
| 型テンプレート | `CamelCase` | `template<typename T>` |
| 型エイリアス | `CamelCase` | `using SampleType = float;` |
| コンセプト | `CamelCase` | `concept Numeric` |
| 名前空間 | `lower_case` | `umi::dsp`, `umi::kernel` |
| 列挙値 | `UPPER_CASE` | `NOTE_ON`, `CONTROL_CHANGE` |
| スコープ付き列挙値 | `UPPER_CASE` | `WaveType::SINE` |

**メンバ変数にプレフィックス/サフィックスは使用しない** (`m_` も `_` 末尾も禁止)。パラメータとの衝突時は `this->` で解決:

```cpp
class Oscillator {
public:
    void set_frequency(float frequency) {
        this->frequency = frequency;
    }
private:
    float frequency = 440.0f;
};
```

**constexpr と inline**: C++17 以降 `constexpr` は暗黙的に `inline` なので `inline constexpr` は冗長。`constexpr` のみ使用:

```cpp
constexpr int max_voices = 16;       // OK
inline constexpr int max_voices = 16; // NG: 冗長
```

### 2.6 命名チェックの例外パターン（IgnoredRegexp）

| 対象 | パターン | 理由 |
|------|----------|------|
| Function | `^_.*\|.*_Handler$\|.*_IRQHandler$` | 内部関数 (`_start`)、CMSIS 割り込みハンドラ (`SysTick_Handler`) |
| Method | `^(operator\|~).*` | 演算子オーバーロード、デストラクタ |
| Variable / StaticVariable | `^[A-Z][A-Z0-9_]*$\|^_.*` | ベンダー定義のマクロ風変数 (`DMA_STREAM`)、リンカスクリプトシンボル (`_estack`) |
| GlobalVariable | `^_.*` | リンカスクリプト定義シンボル (`_shared_start` 等) |
| Type / Class / Struct | `^[A-Z][A-Z0-9_]*$` | ベンダー定義の ALL_CAPS 型名 (`GPIO_TypeDef`, `DMA_HandleTypeDef`) |
| TypeAlias | `.*_t$\|^[A-Z][A-Z0-9_]*$` | POSIX `_t` 慣習 (`size_t`)、ベンダー型 |

---

## 3. .clangd（全文）

```yaml
CompileFlags:
  Add:
    - "-std=c++23"
  Remove:
    - -mfpu=*
    - -mfloat-abi=*
    - -mcpu=*
    - -mthumb
    - --specs=*
    - -fno-exceptions
    - -fno-rtti

Diagnostics:
  Suppress:
    - pp_file_not_found
```

clang-tidy のチェック・命名規則は `.clangd` には書かない。clangd は `.clang-tidy` を自動的に読み込むため、
重複記載すると更新漏れや上書きによる不一致が発生する。CI (`clang-tidy`) と IDE (`clangd`) の挙動を完全に一致させるため、設定は `.clang-tidy` に一本化する。

### 3.1 設定理由

**CompileFlags:**

| 設定 | 理由 |
|------|------|
| `Add: -std=c++23` | ヘッダファイル等 compile_commands.json にエントリがないファイルの fallback |
| `Remove: -mfpu=*` | ARM GCC 固有フラグ。clangd (LLVM) が gcc-arm の compile_commands.json を読む際にエラーになる |
| `Remove: -mfloat-abi=*` | 同上 |
| `Remove: -mcpu=*` | 同上 |
| `Remove: -mthumb` | 同上 |
| `Remove: --specs=*` | GCC 専用リンカ指定 (`nano.specs` 等)。clangd は解釈不能 |
| `Remove: -fno-exceptions` | 全ターゲットで付与されるが、clangd の型推論・補完精度のために有効化。例外禁止は `.clang-tidy` と コードレビューで担保 |
| `Remove: -fno-rtti` | 同上。RTTI 無効下では `dynamic_cast` 等の推論が不正確になる |

**Diagnostics:**

| 設定 | 理由 |
|------|------|
| `Suppress: pp_file_not_found` | ARM クロスコンパイル対象ファイルを開いた際、ホストに存在しないヘッダ (`<stm32f4xx.h>` 等) のエラーを抑制 |

### 3.2 ローカル .clangd オーバーライド

MMIO レジスタ定義ディレクトリでは、ハードウェア慣習の ALL_CAPS 命名を許可するローカル `.clangd` を配置:

- `lib/umi/port/device/.clangd` — デバイスレジスタ定義
- `lib/umi/port/mcu/.clangd` — MCU レジスタ定義

これらは `TypeIgnoredRegexp`, `MemberIgnoredRegexp` 等で `^[A-Z][A-Z0-9_]*$` パターンを追加許可する。
対応する `.clang-tidy` オーバーライドも同ディレクトリに配置し、CI でも同じ例外が適用される。

---

## 4. コード例

```cpp
namespace umi::dsp {

// 型: CamelCase
class Oscillator {
public:
    // メソッド: lower_case
    float tick(float freq_norm);
    void reset();

private:
    // メンバ: lower_case（サフィックスなし）
    float phase = 0.0f;
};

// 列挙型: CamelCase, 列挙値: UPPER_CASE
enum class WaveType {
    SINE,
    SAW,
    SQUARE,
    TRIANGLE,
};

// 関数: lower_case
float midi_to_freq(int note) {
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

// 定数: lower_case（inline は付けない）
constexpr float default_gain = 1.0f;
constexpr int max_voices = 16;

}  // namespace umi::dsp
```

### インクルード順序

```cpp
// Priority 1: C標準ヘッダ
#include <stdint.h>
#include <string.h>

// Priority 2: C++標準ヘッダ
#include <algorithm>
#include <vector>

// Priority 3: プロジェクトヘッダ
#include "my_header.hh"
#include "oscillator.hh"
```

### プリプロセッサ

```cpp
// IndentPPDirectives: BeforeHash
#if defined(STM32)
    #include <stm32f4xx.h>
    #define BUFFER_SIZE 256
#else
    #include <mock_hal.hh>
    #define BUFFER_SIZE 1024
#endif
```

---

## 5. ツールの使用方法

### coding-rules パッケージ（推奨）

`coding-rules` パッケージが設定ファイルの生成・管理を自動化します:

```bash
# .clangd, .clang-tidy, .clang-format を生成・更新
xmake coding init

# プロジェクト全体をフォーマット
xmake coding format
```

### 手動実行

```bash
# 単一ファイルのフォーマット
clang-format -i src/main.cc

# clang-tidy 実行（プロジェクトルートの compile_commands.json を使用）
clang-tidy -p . src/main.cc
```

---

## 6. エラーハンドリング

- `Result<T>` またはエラーコードを優先する。
- カーネル/オーディオパスでは例外を使用しない。
- 外部入力（ユーザー入力、外部 API）のバウンダリでのみバリデーションを行う。
- 内部コードやフレームワーク保証は信頼する。

---

## 7. リアルタイム安全性

`process()` やオーディオコールバック内では以下を**厳守**する（違反は未定義動作やオーディオグリッチの原因）:

| 禁止事項 | 理由 |
|----------|------|
| ヒープ割り当て (`new`, `malloc`, `std::vector` growth) | レイテンシ不定 |
| ブロッキング同期 (`mutex`, `semaphore`) | デッドロック・レイテンシ |
| 例外 (`throw`) | スタックアンワインド不可 |
| stdio (`printf`, `cout`) | ブロッキング I/O |

---

## 関連ドキュメント

- [Library Spec](LIBRARY_SPEC.md) - ライブラリ構造規約
- [Testing Guide](../guides/TESTING_GUIDE.md) - テスト戦略
- [API Comment Rule](API_COMMENT_RULE.md) - コメント規約
