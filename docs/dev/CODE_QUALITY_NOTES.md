# UMIプロジェクト clang-tooling 設定評価

**対象ファイル**: `.clang-format`, `.clang-tidy`, `.clangd`

---

## 評価結果

| ファイル | 評価 | 備考 |
|---------|------|------|
| `.clang-format` | 良好 | `Standard: c++23` に固定すべき |
| `.clang-tidy` | 良好 | 現状で妥当 |
| `.clangd` | **要改善** | 命名規則除外パターンが不足 |

---

## 1. .clang-format

### 評価結果: 良好（細部の改善あり）

#### 1.1 基本設定

| 設定 | 現状 | 評価 | 備考 |
|-----|------|------|------|
| `BasedOnStyle: LLVM` | ✅ | 良好 | 業界標準ベース |
| `IndentWidth: 4` | ✅ | 良好 | CLAUDE.mdと一致 |
| `ColumnLimit: 120` | ✅ | 良好 | 現代的な制限 |
| `PointerAlignment: Left` | ✅ | 良好 | `int* ptr` - CLAUDE.md準拠 |
| `ReferenceAlignment: Left` | ✅ | 良好 | `int& ref` |

#### 1.2 C++23/現代C++対応

| 設定 | 現状 | 評価 | 備考 |
|-----|------|------|------|
| `Standard: Latest` | ⚠️ | 要修正 | `c++23`に固定すべき |
| `RequiresClausePosition: OwnLine` | ✅ | 良好 | Concepts対応 |
| `IndentRequiresClause: true` | ✅ | 良好 | |
| `AlwaysBreakTemplateDeclarations: Yes` | ✅ | 良好 | テンプレート可読性 |

**問題**: `Standard: Latest`は曖昧で再現性がない

```yaml
# 推奨
Standard: c++23
```

#### 1.3 組み込み特有パターン

プロジェクトの実際のコードとの比較：

```cpp
// asmブロック（handlers.cc）
extern "C" __attribute__((naked)) void PendSV_Handler() {
    asm volatile("   .syntax unified                 \n"
                 "   mrs     r0, psp                 \n"
                 ...
}

// constexpr変数（constants.hh）
constexpr float Pi = 3.14159265358979323846f;  // C++17以降、constexpr変数は暗黙的にinline

// Concepts（processor.hh）
template<typename P>
concept ProcessorLike = requires(P& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};

// 属性付き関数（basic_math.hh）
[[nodiscard]] [[gnu::always_inline]] constexpr F fast_sine_quarter(F x) noexcept;
```

| 設定 | 評価 | 備考 |
|-----|------|------|
| `AllowShortFunctionsOnASingleLine: Inline` | ✅ | 妥当。クラス内部の関数のみ1行許容 |
| `AlwaysBreakTemplateDeclarations: Yes` | ✅ | `template<typename T>\nvoid func()` 推奨 |

**評価**: `AllowShortFunctionsOnASingleLine: Inline` は妥当

- `Inline`: **クラス内部で定義された関数**を1行許容（空関数含む）
- クラス外部の関数は常に改行される

プロジェクトの実態と一致：
- クラス内部の短いメソッド（ゲッター/セッター）は1行で可読性向上
- クラス外部の自由関数は改行され、実装のある関数と明確に区別
- 修正は不要

#### 1.4 インクルードソート

| 設定 | 評価 | 備考 |
|-----|------|------|
| `IncludeBlocks: Regroup` | ✅ | カテゴリ別にグループ化 |
| `SortIncludes: CaseSensitive` | ✅ | ケースセンシティブ |
| `IncludeCategories` | ⚠️ | lib/umi特有の順序がない |

**改善案**: プロジェクト固有のインクルード順序を追加

```yaml
IncludeCategories:
  - Regex:    '^<.*\.h>'              # Cヘッダ
    Priority: 1
  - Regex:    '^<.*>'                  # 標準ライブラリ
    Priority: 2
  - Regex:    '^"umi/'                 # UMIコア
    Priority: 3
  - Regex:    '^"(umidsp|umidi)/'      # UMIサブライブラリ
    Priority: 4
  - Regex:    '^".*"'                  # その他
    Priority: 5
```

#### 1.5 プリプロセッサ/マクロ

プロジェクトの実態：
```cpp
// レジスタ定義（stm32f4）
#define GPIOA_BASE 0x40020000
#define RCC_BASE   0x40023800
```

| 設定 | 評価 | 備考 |
|-----|------|------|
| `IndentPPDirectives: BeforeHash` | ✅ | `#`をインデント |
| `AlignConsecutiveMacros: AcrossEmptyLines` | ✅ | マクロアラインメント |

#### 1.6 アラインメント

| 設定 | 評価 | 備考 |
|-----|------|------|
| `AlignOperands: Align` | ✅ | 演算子オペランドをアライン |
| `AlignTrailingComments: true` | ✅ | 行末コメントをアライン |
| `AlignEscapedNewlines: Left` | ✅ | 行継続を左揃え |

### 推奨修正

```yaml
# 1. Standardを固定
Standard: c++23

# 2. インクルード順序をプロジェクトに最適化（任意）
IncludeCategories:
  - Regex:    '^<.*\.h>'
    Priority: 1
  - Regex:    '^<.*>'
    Priority: 2
  - Regex:    '^"umi/'
    Priority: 3
  - Regex:    '^"(umidsp|umidi|umisynth)/'
    Priority: 4
  - Regex:    '^".*"'
    Priority: 5
```

---

## 2. .clang-tidy

### 現状の評価

現在の設定は妥当。除外パターンは適切：

| 除外設定 | 評価 |
|---------|------|
| `bugprone-easily-swappable-parameters` | DSP関数で誤検出するため適切 |
| `readability-magic-numbers` | オーディオ定数が多いため適切 |
| `performance-no-int-to-ptr` | 組み込みレジスタアクセスで必要 |

### 推奨アプローチ

**HeaderFilterRegexはグローバルに設定せず、局所的な.clang-tidyファイルを使用すべき**

理由：
- ハードコードされたパスはメンテナンス性が低い
- ライブラリごとに異なるチェック要件がある
- NOLINTコメントは例外を許容するため使用しない

**推奨構成**:

```yaml
# .clang-tidy（ルート）- 基本設定
Checks: >
  -*,
  bugprone-*,-bugprone-dynamic-static-initializers,-bugprone-easily-swappable-parameters,
  clang-analyzer-*,-clang-analyzer-core.FixedAddressDereference,
  performance-*,
  modernize-*,-modernize-use-trailing-return-type,-modernize-use-std-print,
  readability-*,-readability-magic-numbers,-readability-uppercase-literal-suffix,-readability-identifier-length,
  misc-*,-misc-non-private-member-variables-in-classes

CheckOptions:
  # 命名規則（プロジェクト全体共通）
  - { key: readability-identifier-naming.FunctionCase, value: lower_case }
  - { key: readability-identifier-naming.MethodCase, value: lower_case }
  - { key: readability-identifier-naming.FunctionIgnoredRegexp, value: '^_.*|.*_Handler$|.*_IRQHandler$' }
  - { key: readability-identifier-naming.VariableCase, value: lower_case }
  - { key: readability-identifier-naming.TypeCase, value: CamelCase }

---

# lib/umi/port/.clang-tidy - 組み込み層特有の除外
Checks: >
  -*,
  bugprone-*,-bugprone-dynamic-static-initializers,-bugprone-easily-swappable-parameters,
  clang-analyzer-*,-clang-analyzer-core.FixedAddressDereference,
  performance-*,-performance-no-int-to-ptr,
  modernize-*,-modernize-use-trailing-return-type,-modernize-use-std-print,
  readability-*,-readability-magic-numbers,-readability-uppercase-literal-suffix,-readability-identifier-length,
  misc-*,-misc-non-private-member-variables-in-classes

CheckOptions:
  # 命名規則（組み込み層特有の例外）
  - { key: readability-identifier-naming.TypeIgnoredRegexp, value: '^[A-Z][A-Z0-9_]*$' }
  - { key: readability-identifier-naming.ClassIgnoredRegexp, value: '^[A-Z][A-Z0-9_]*$' }
  - { key: readability-identifier-naming.StructIgnoredRegexp, value: '^[A-Z][A-Z0-9_]*$' }
  - { key: readability-identifier-naming.TypeAliasIgnoredRegexp, value: '.*_t$|^[A-Z][A-Z0-9_]*$' }
  # performance-no-int-to-ptr: 組み込みレジスタアクセスで必要なため除外
```

**配置場所と役割**:

| ファイル | 役割 |
|---------|------|
| `.clang-tidy`（ルート） | 基本命名規則、performance-no-int-to-ptrはチェック対象 |
| `lib/umi/port/.clang-tidy` | ALL_CAPS型名除外、performance-no-int-to-ptr除外 |
| `third_party/` | HeaderFilterRegexで除外（グローバル） |

---

## 3. .clangd ⚠️ 主要な問題

### 問題：命名規則の不整合

プロジェクトで使用されるALL_CAPS型名：
```cpp
struct GPIO { };      // lib/umi/port/mcu/stm32f4/mcu/gpio.hh
struct RCC { };       // lib/umi/port/mcu/stm32f4/mcu/rcc.hh
struct DMAStream { }; // lib/umi/port/mcu/stm32h7/mcu/dma.hh
struct USART1 { };    // lib/umi/port/mcu/stm32h7/mcu/usart.hh
// ... その他多数
```

ISRハンドラ名：
```cpp
void PendSV_Handler() { }   // arch/handlers.cc
void TIM2_IRQHandler() { }  // 各ボード実装
```

### .clang-tidy（適切に設定済み）

```yaml
readability-identifier-naming.TypeIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
readability-identifier-naming.ClassIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
readability-identifier-naming.StructIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
readability-identifier-naming.FunctionIgnoredRegexp: '^_.*|.*_Handler$'
```

### .clangd（推奨アプローチ）

**基本的な命名規則（lower_case等）はルートで設定し、例外パターンは局所的に設定すべき**

理由：
- STM32レジスタ型名（GPIO, RCC等）は`lib/umi/port/mcu`固有の例外
- グローバルに例外を設定すると意図しない誤検出を許容する

**推奨構成**:

```yaml
# .clangd（ルート）- 基本命名規則のみ
Diagnostics:
  ClangTidy:
    CheckOptions:
      # 関数・変数はプロジェクト全体で統一
      readability-identifier-naming.FunctionCase: lower_case
      readability-identifier-naming.MethodCase: lower_case
      readability-identifier-naming.FunctionIgnoredRegexp: '^_.*|.*_Handler$|.*_IRQHandler$'
      readability-identifier-naming.MethodIgnoredRegexp: '^(operator|~).*'
      readability-identifier-naming.VariableCase: lower_case
      readability-identifier-naming.ParameterCase: lower_case
      readability-identifier-naming.MemberCase: lower_case
      readability-identifier-naming.TypeCase: CamelCase
      readability-identifier-naming.ClassCase: CamelCase
      readability-identifier-naming.StructCase: CamelCase
      # ...（例外パターンは設定しない）

# lib/umi/port/mcu/.clangd - 組み込み固有の例外
Diagnostics:
  ClangTidy:
    CheckOptions:
      # STM32レジスタ型名を除外
      readability-identifier-naming.TypeIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      readability-identifier-naming.ClassIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      readability-identifier-naming.StructIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      readability-identifier-naming.TypeAliasIgnoredRegexp: '.*_t$|^[A-Z][A-Z0-9_]*$'
```

---

## 推奨修正

### .clangd（ルート）

```yaml
CompileFlags:
  # -std=c++23 はcompile_commands.jsonから推論されるため不要
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
      # 関数・変数はプロジェクト全体で統一
      readability-identifier-naming.FunctionCase: lower_case
      readability-identifier-naming.MethodCase: lower_case
      readability-identifier-naming.FunctionIgnoredRegexp: '^_.*|.*_Handler$|.*_IRQHandler$'
      readability-identifier-naming.MethodIgnoredRegexp: '^(operator|~).*'
      readability-identifier-naming.VariableCase: lower_case
      readability-identifier-naming.ParameterCase: lower_case
      readability-identifier-naming.MemberCase: lower_case
      readability-identifier-naming.TypeCase: CamelCase
      readability-identifier-naming.ClassCase: CamelCase
      readability-identifier-naming.StructCase: CamelCase
      # 例外パターン（TypeIgnoredRegexp等）は局所的に設定
```

### .clangd（lib/umi/port/mcu/）

```yaml
# MCU固有コード用：STM32レジスタ型名を除外
Diagnostics:
  ClangTidy:
    CheckOptions:
      readability-identifier-naming.TypeIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      readability-identifier-naming.ClassIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      readability-identifier-naming.StructIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      readability-identifier-naming.TypeAliasIgnoredRegexp: '.*_t$|^[A-Z][A-Z0-9_]*$'
```

### .clang-format

```yaml
# Standard: Latest から変更
Standard: c++23
```

---

## まとめ

### 対応優先度

1. **High**: .clangdの命名規則設定
   - **ルート**: 基本命名規則（関数・変数をlower_case等）
   - **lib/umi/port/mcu/**: 局所的な例外（STM32型名のALL_CAPS許容）

2. **Low**: .clang-formatの標準固定
   - `Standard: c++23`

3. **Optional**: 必要に応じて局所的な.clang-tidy/.clang-d配置
   - 特定ライブラリで緩和/強化したい場合、そのディレクトリに配置
   - 例: `lib/umi/port/mcu/.clangd`

### 効果

- ISRハンドラ名（`*_Handler`, `*_IRQHandler`）でのIDE警告が消滅
- STM32レジスタ型名（GPIO, RCC等）での警告は`lib/umi/port/mcu/`配下のみで除外され、他の場所では検出される
