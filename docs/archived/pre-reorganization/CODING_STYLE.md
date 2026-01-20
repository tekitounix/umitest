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

### ポインタ・参照

```cpp
// ✅ 左寄せ（型に付ける）
int* ptr;
const std::string& ref;

// ❌ 右寄せ
int *ptr;
const std::string &ref;
```

### 関数引数

```cpp
// BinPackArguments: false - 引数が多い場合は1行1引数
void long_function(
    int first_arg,
    int second_arg,
    int third_arg
);

// 短い関数はインライン可
int get() const { return value_; }
```

### テンプレート

```cpp
// AlwaysBreakTemplateDeclarations: Yes
template<typename T>
class MyClass {
    // ...
};

// requires節は独立行
template<typename T>
requires std::integral<T>
void func(T value);
```

### インクルード順序

自動ソート（CaseSensitive）、カテゴリ順：

```cpp
// 1. C標準ヘッダ
#include <stdint.h>
#include <string.h>

// 2. C++標準ヘッダ
#include <algorithm>
#include <vector>

// 3. プロジェクトヘッダ
#include "my_header.hh"
#include "umidsp/oscillator.hh"
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

### マクロ整列

```cpp
// AlignConsecutiveMacros: AcrossEmptyLines
#define SHORT       1
#define MEDIUM      2
#define VERY_LONG   3

#define ANOTHER     4  // 空行を超えて整列
```

### アクセス修飾子

```cpp
class Example {
public:
    void method();  // EmptyLineAfterAccessModifier: Never
    
private:            // EmptyLineBeforeAccessModifier: LogicalBlock
    int member_;
};
```

---

## 命名規則 (.clang-tidy)

### 基本ルール

| 対象 | スタイル | 例 |
|------|----------|-----|
| 関数・メソッド | `lower_case` | `process_audio()`, `get_value()` |
| 変数・パラメータ | `lower_case` | `sample_rate`, `buffer_size` |
| メンバ変数 | `lower_case` | `gain_`, `cutoff` |
| 定数 (constexpr) | `lower_case` | `max_voices`, `default_gain` |
| 型・クラス・構造体 | `CamelCase` | `AudioProcessor`, `MidiEvent` |
| 列挙型 | `CamelCase` | `EventType`, `Priority` |
| 列挙値 | `UPPER_CASE` | `NOTE_ON`, `CONTROL_CHANGE` |
| 名前空間 | `lower_case` | `umi::dsp`, `umi::kernel` |

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
    // メンバ: lower_case
    float phase_ = 0.0f;
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

// 定数: lower_case
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
  CompilationDatabase: .build/
```

xmake が生成する `compile_commands.json` を参照。

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
# clang-tidy 実行
clang-tidy src/main.cc -- -std=c++20

# compile_commands.json がある場合
clang-tidy -p .build src/main.cc
```

### VS Code 統合

`.vscode/settings.json`:

```json
{
    "editor.formatOnSave": true,
    "C_Cpp.clang_format_path": "/usr/bin/clang-format",
    "clangd.arguments": [
        "--compile-commands-dir=.build",
        "--clang-tidy"
    ]
}
```

---

## 関連ドキュメント

- [API.md](API.md) - API リファレンス
- [ARCHITECTURE.md](ARCHITECTURE.md) - アーキテクチャ概要
