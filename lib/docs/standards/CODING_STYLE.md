# コーディングスタイルガイド

UMI プロジェクトのコーディングスタイルは `.clang-format`、`.clang-tidy`、`.clangd` で定義されています。

---

## フォーマット規則 (.clang-format)

### 基本設定

| 項目 | 値 | 説明 |
|------|-----|------|
| BasedOnStyle | LLVM | LLVMスタイルをベースに |
| Standard | Latest | 最新C++標準 |
| IndentWidth | 4 | インデント幅 |
| ColumnLimit | 120 | 1行の最大文字数 |
| MaxEmptyLinesToKeep | 1 | 連続空行は最大1行 |

### constexpr と inline

C++17以降、`constexpr` 変数・関数は暗黙的に `inline` であるため、`inline constexpr` は冗長です。**`constexpr` のみを使用**してください。

```cpp
// ✅ 正しい
constexpr int max_voices = 16;
constexpr float to_radians(float deg) { return deg * pi / 180.0f; }

// ❌ 間違い（冗長な inline）
inline constexpr int max_voices = 16;
inline constexpr float to_radians(float deg) { return deg * pi / 180.0f; }
```

### ポインタ・参照

```cpp
// ✅ 左寄せ（型に付ける）
int* ptr;
const std::string& ref;

// ❌ 右寄せ
int *ptr;
const std::string &ref;
```

### 関数引数・パラメータ

```cpp
// BinPackArguments: false, BinPackParameters: false
// 引数・パラメータが多い場合は1行1引数
void long_function(
    int first_arg,
    int second_arg,
    int third_arg
);

// AllowShortFunctionsOnASingleLine: Inline
// 短い関数はインライン可
int get() const { return value; }
```

### テンプレート・Concepts

```cpp
// AlwaysBreakTemplateDeclarations: Yes
template<typename T>
class MyClass {
    // ...
};

// RequiresClausePosition: OwnLine
// IndentRequiresClause: true
template<typename T>
    requires std::integral<T>
void func(T value);
```

### ラムダ式

```cpp
// LambdaBodyIndentation: Signature
auto callback = [this](int value) {
    process(value);  // ラムダのシグネチャに合わせてインデント
};
```

### インクルード順序

自動ソート・再グループ化（IncludeBlocks: Regroup, SortIncludes: CaseSensitive）：

```cpp
// Priority 1: C標準ヘッダ (^<.*\.h>)
#include <stdint.h>
#include <string.h>

// Priority 2: C++標準ヘッダ (^<.*>)
#include <algorithm>
#include <vector>

// Priority 3: プロジェクトヘッダ (^".*")
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

### アライメント設定

| 設定 | 値 | 説明 |
|------|-----|------|
| `AlignConsecutiveMacros` | `AcrossEmptyLines` | マクロ定義を空行を越えて整列 |
| `AlignOperands` | `Align` | 演算子オペランドを整列 |
| `AlignEscapedNewlines` | `Left` | エスケープ改行を左揃え |
| `AlignTrailingComments` | `true` | 行末コメントを整列 |

```cpp
// AlignConsecutiveMacros: AcrossEmptyLines
#define SHORT       1
#define MEDIUM      2
#define VERY_LONG   3

#define ANOTHER     4  // 空行を超えて整列

// AlignOperands: Align
int result = very_long_variable_name
           + another_long_variable
           - some_other_value;

// AlignTrailingComments: true
int x = 1;    // x の説明
int width = 10;    // 幅
```

### アクセス修飾子

```cpp
class Example {
public:
    void method();  // EmptyLineAfterAccessModifier: Never
    
private:            // EmptyLineBeforeAccessModifier: LogicalBlock
    int member;
};
```

---

## 命名規則 (.clang-tidy)

### 基本ルール

| 対象 | スタイル | 例 |
|------|----------|-----|
| 関数・メソッド | `lower_case` | `process_audio()`, `get_value()` |
| 変数・パラメータ | `lower_case` | `sample_rate`, `buffer_size` |
| メンバ変数 | `lower_case` | `gain`, `cutoff`, `phase` |
| グローバル変数 | `lower_case` | `g_instance` |
| 静的変数 | `lower_case` | `s_counter` |
| 定数 (constexpr) | `lower_case` | `max_voices`, `default_gain` |
| 型・クラス・構造体 | `CamelCase` | `AudioProcessor`, `MidiEvent` |
| 型テンプレート | `CamelCase` | `template<typename T>` |
| 型エイリアス | `CamelCase` | `using SampleType = float;` |
| コンセプト | `CamelCase` | `concept Numeric` |
| 列挙型 | `CamelCase` | `EventType`, `Priority` |
| 列挙値 | `UPPER_CASE` | `NOTE_ON`, `CONTROL_CHANGE` |
| スコープ付き列挙値 | `UPPER_CASE` | `WaveType::SINE` |
| 名前空間 | `lower_case` | `umi::dsp`, `umi::kernel` |

### メンバ変数の命名

**プレフィックス/サフィックスは使用しない**:

```cpp
// ✅ 正しい
class Filter {
private:
    float cutoff = 1000.0f;
    float resonance = 0.5f;
};

// ❌ 間違い（アンダースコアサフィックス）
class Filter {
private:
    float cutoff_ = 1000.0f;
    float resonance_ = 0.5f;
};

// ❌ 間違い（m_ プレフィックス）
class Filter {
private:
    float m_cutoff = 1000.0f;
    float m_resonance = 0.5f;
};
```

**パラメータとの名前衝突時は `this->` を使用**:

```cpp
class Oscillator {
public:
    void set_frequency(float frequency) {
        this->frequency = frequency;  // ✅ this-> で解決
    }
    
private:
    float frequency = 440.0f;
};
```

### コード例

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
inline float midi_to_freq(int note) {
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

// 定数: lower_case（inline は付けない — C++17以降 constexpr は暗黙的に inline）
constexpr float default_gain = 1.0f;
constexpr int max_voices = 16;

}  // namespace umi::dsp
```

### 例外パターン

以下のパターンは命名チェックから除外されます：

```cpp
// ベンダー定義の ALL_CAPS 型名
GPIO_TypeDef* gpio;
DMA_HandleTypeDef dma;

// _t サフィックス（POSIX慣習）
using sample_t = float;

// アンダースコア始まり（内部/予約）
void _internal_init();

// 演算子オーバーロード
bool operator==(const Event& other) const;
```

---

## 静的解析 (.clang-tidy)

### 有効なチェック

| カテゴリ | 説明 |
|----------|------|
| `bugprone-*` | バグを誘発しやすいパターン |
| `clang-analyzer-*` | Clang静的解析 |
| `performance-*` | パフォーマンス問題 |
| `modernize-*` | モダンC++への移行 |
| `readability-*` | 可読性向上 |
| `misc-*` | その他 |

### 無効化されたチェック

| チェック | 無効化理由 |
|----------|------------|
| `bugprone-dynamic-static-initializers` | `static constexpr` での誤検出 |
| `bugprone-easily-swappable-parameters` | DSP/組込みでは同型引数が頻出（ADSR、座標等） |
| `modernize-use-trailing-return-type` | 後置戻り値型を強制しない |
| `readability-magic-numbers` | 組込みでは数値リテラルが頻出 |
| `readability-uppercase-literal-suffix` | リテラルサフィックスの大文字強制は不要 |
| `readability-identifier-length` | 組込み/DSPでは短い変数名が一般的 |

### エラー扱いの警告

以下は警告ではなくエラーとして報告されます：

- `bugprone-*` 全般
- `clang-analyzer-*` 全般
- `performance-move-const-arg`

---

## clangd 設定

### コンパイルデータベース

```yaml
CompileFlags:
  Add:
    - "--query-driver=/Applications/ArmGNUToolchain/.../arm-none-eabi-g++"
    - "-std=c++23"
```

xmake が `compile_commands.json` をプロジェクトルートに生成。`--query-driver` でARM GCCの組み込みヘッダパスを解決。

### ARM クロスコンパイル対応

ARM GCC 固有のフラグは clangd では除外：

```yaml
Remove:
  - -mfpu=*
  - -mfloat-abi=*
  - -mcpu=*
  - -mthumb
  - --specs=*
  - -fno-exceptions
  - -fno-rtti
```

### 抑制された警告

```yaml
Diagnostics:
  Suppress:
    - pp_file_not_found  # クロスコンパイル環境でのヘッダ不足
```

### clang-tidy 連携

clangd 内で clang-tidy を有効化：

```yaml
Diagnostics:
  ClangTidy:
    Add:
      - bugprone-*
      - clang-analyzer-*
      - performance-*
      - modernize-*
      - readability-*
      - misc-*
    Remove:
      - bugprone-dynamic-static-initializers
      - bugprone-easily-swappable-parameters
      - modernize-use-trailing-return-type
      - readability-magic-numbers
      - readability-uppercase-literal-suffix
      - readability-identifier-length
    CheckOptions:
      readability-identifier-naming.FunctionCase: lower_case
      readability-identifier-naming.VariableCase: lower_case
      readability-identifier-naming.TypeCase: CamelCase
      # ... 他の命名規則
```

---

## ツールの使用方法

### フォーマット適用

```bash
# 単一ファイル
clang-format -i src/main.cc

# ディレクトリ全体
find lib -name "*.cc" -o -name "*.hh" | xargs clang-format -i
```

### 静的解析実行

```bash
# clang-tidy 実行（プロジェクトルートの compile_commands.json を使用）
clang-tidy -p . src/main.cc
```

---

## 関連ドキュメント

- [LIBRARY_STRUCTURE.md](LIBRARY_STRUCTURE.md) - ライブラリ構造規約
- [TESTING.md](TESTING.md) - テスト戦略
