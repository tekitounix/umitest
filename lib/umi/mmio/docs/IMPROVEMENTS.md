# umimmio 改善提案

このドキュメントは、umimmioライブラリのコードレビューに基づく改善提案をまとめたものです。

---

## 目次

1. [RO/WOレジスタのコンパイル時アクセス制御](#1-rowoレジスタのコンパイル時アクセス制御)
2. [Transportコンセプト定義](#2-transportコンセプト定義)
3. [エラー処理ポリシーの拡張](#3-エラー処理ポリシーの拡張)
4. [modify()の非atomic性ドキュメント](#4-modifyの非atomic性ドキュメント)
5. [テストコードの改善](#5-テストコードの改善)
6. [大規模レジスタセット対応](#6-大規模レジスタセット対応)
7. [スタイル・命名の問題](#7-スタイル命名の問題)
8. [バグと潜在的危険性](#8-バグと潜在的危険性)
9. [テストの問題点](#9-テストの問題点)
10. [テスト品質の改善提案](#10-テスト品質の改善提案)
11. [プロジェクトで必要なトランスポート分析](#11-プロジェクトで必要なトランスポート分析)

---

## 1. RO/WOレジスタのコンパイル時アクセス制御

### 現状の問題

`RO`（Read-Only）や `WO`（Write-Only）のパーミッションはテンプレート引数として定義されているが、
実際のアクセス制御は行われていない。

```cpp
// 現状: RO レジスタへの write がコンパイルを通ってしまう
struct STATUS : mm::Register<CS43L22, 0x2E, 8, mm::RO> {};
transport.write(CS43L22::STATUS::value(0));  // コンパイルエラーにならない
```

### 提案する解決策

`RegOps` の `write()` および `read()` に `static_assert` を追加：

```cpp
// mmio.hh の RegOps クラス内

template <typename Arg>
void write(Arg arg) const noexcept {
    using RegType = typename Arg::RegType;
    
    // コンパイル時アクセス制御
    static_assert(RegType::permission != RO, 
        "Cannot write to read-only register");
    
    // 既存の実装...
}

template <typename Reg>
[[nodiscard]] auto read(Reg reg) const noexcept -> typename Reg::RegValueType {
    using RegType = std::conditional_t<is_field<Reg>, typename Reg::RegType, Reg>;
    
    // コンパイル時アクセス制御
    static_assert(RegType::permission != WO,
        "Cannot read from write-only register");
    
    // 既存の実装...
}
```

### 実装の優先度

**高** - 型安全性の根幹に関わる機能

### 影響範囲

- `mmio.hh`: `RegOps::write()`, `RegOps::read()` の修正
- 既存のデバイス定義で RO レジスタに誤って write している箇所があればコンパイルエラーになる

---

## 2. Transportコンセプト定義

### 現状の問題

`RegOps<Derived>` に渡される `Derived` 型の要件が暗黙的。
Transport実装者がどのメソッドを実装すべきか不明確。

### 提案する解決策

C++20 コンセプトで明示的に定義：

```cpp
namespace mm {

/// DirectTransport が満たすべきコンセプト
template <typename T>
concept DirectTransportLike = requires(T& t) {
    typename T::TransportTag;
    requires std::same_as<typename T::TransportTag, DirectTransportTag>;
} && requires(T& t, auto reg, std::uint64_t val) {
    { t.reg_read(reg) } -> std::convertible_to<std::uint64_t>;
    { t.reg_write(reg, val) } -> std::same_as<void>;
};

/// reg_read/reg_write を持つTransport
template <typename T>
concept RegTransportLike = requires(T& t) {
    typename T::TransportTag;
} && requires(T& t, auto reg, std::uint64_t val) {
    { t.reg_read(reg) } -> std::convertible_to<std::uint64_t>;
    { t.reg_write(reg, val) } -> std::same_as<void>;
};

/// ByteAdapter派生Transport（I2C/SPI）が満たすべきコンセプト
template <typename T>
concept ByteTransportLike = requires(T& t) {
    typename T::TransportTag;
    requires (std::same_as<typename T::TransportTag, I2CTransportTag> ||
              std::same_as<typename T::TransportTag, SPITransportTag>);
} && requires(T& t, typename T::AddressType addr, void* data, std::size_t size) {
    { t.raw_read(addr, data, size) } -> std::same_as<void>;
    { t.raw_write(addr, data, size) } -> std::same_as<void>;
};

/// 汎用Transportコンセプト
template <typename T>
concept TransportLike = DirectTransportLike<T> || ByteTransportLike<T> || RegTransportLike<T>;

} // namespace mm
```

### RegOpsでの使用

```cpp
template <typename Derived, typename CheckPolicy = std::true_type>
class RegOps {
    template <typename Reg>
    static constexpr void check_transport_allowed() {
        static_assert(TransportLike<Derived>, "Derived must satisfy TransportLike");
        ...
    }
    ...
};
```

### 実装の優先度

**中** - コードの明確性向上、IDEサポート改善

### 影響範囲

- `mmio.hh`: コンセプト定義追加、`RegOps` テンプレート制約追加
- 既存のTransport実装: 変更不要（既に要件を満たしている）

---

## 3. エラー処理ポリシーの拡張

### 現状の問題

エラー処理は `assert()` のみ。組込み環境では以下のニーズがある：
- リリースビルドでも検出したい
- `Result<T>` で呼び出し側に通知したい
- カスタムエラーハンドラを呼びたい

### 提案する解決策

エラーポリシーをテンプレート引数で選択可能に：

```cpp
namespace mm {

/// デフォルト: assert（デバッグビルドのみ）
struct AssertOnError {
    static void on_range_error([[maybe_unused]] const char* msg) noexcept {
        assert(false && msg);
    }
};

/// リリースでも検出（trap命令）
struct TrapOnError {
    [[noreturn]] static void on_range_error([[maybe_unused]] const char* msg) noexcept {
        __builtin_trap();
    }
};

/// 無視（最速、検証済みコード用）
struct IgnoreError {
    static void on_range_error([[maybe_unused]] const char* msg) noexcept {}
};

/// カスタムハンドラ呼び出し
template <void (*Handler)(const char*)>
struct CustomErrorHandler {
    static void on_range_error(const char* msg) noexcept {
        Handler(msg);
    }
};

// 使用例
template <typename Derived, 
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError>
class RegOps { ... };

} // namespace mm
```

### 使用例

```cpp
// デバッグビルド（デフォルト）
DirectTransport<std::true_type, AssertOnError> mcu;

// リリースビルドでも安全
DirectTransport<std::true_type, TrapOnError> mcu_safe;

// パフォーマンス重視（検証済み）
DirectTransport<std::true_type, IgnoreError> mcu_fast;
```

### 実装の優先度

**低** - 現状のassertで多くのケースは対応可能

### 影響範囲

- `mmio.hh`: エラーポリシー型定義、`RegOps` テンプレート引数追加
- 既存コード: デフォルト引数により互換性維持

---

## 4. modify()の非atomic性ドキュメント

### 現状の問題

`modify()` は read-modify-write 操作であり、割り込みとの競合が発生しうる。
これがドキュメントで明示されていない。

### 提案する解決策

#### 4.1 コードコメントの追加

```cpp
/// Modify specific fields in a register (read-modify-write)
/// 
/// @warning This operation is NOT atomic. In interrupt contexts or
///          multi-threaded environments, wrap with appropriate
///          synchronization (e.g., disable_irq/enable_irq).
/// 
/// @code
/// // Safe usage in interrupt-sensitive code:
/// {
///     CriticalSection cs;  // or __disable_irq()
///     transport.modify(REG::FIELD::value(new_val));
/// }
/// @endcode
template <typename... Args>
void modify(Args... args) const noexcept { ... }
```

#### 4.2 USAGE.md への追記

```markdown
## スレッド安全性と割り込み

### modify() の注意点

`modify()` は以下の3ステップで動作します：

1. レジスタ全体を読み取り
2. 指定フィールドを変更
3. レジスタ全体を書き戻し

この操作は **atomic ではありません**。割り込みハンドラと共有するレジスタを
`modify()` する場合は、クリティカルセクションで保護してください：

```cpp
// 割り込みハンドラと共有するレジスタ
void update_config() {
    __disable_irq();
    transport.modify(PERIPH::CONFIG::ENABLE::Set{});
    __enable_irq();
}
```

### write() は安全？

単一の `write()` は通常atomic（32-bit aligned アクセスはCortex-Mでatomic）ですが、
複数フィールドを個別に `write()` する場合は同様の注意が必要です。
```

### 実装の優先度

**高** - バグの原因になりやすい重要な注意事項

### 影響範囲

- `mmio.hh`: コメント追加のみ
- `docs/USAGE.md`: セクション追加

---

## 5. テストコードの改善

### 5.1 reinterpret_cast の strict aliasing 問題

#### 現状の問題

`test_assert.cc` で `reinterpret_cast` を使用しており、strict aliasing violation の可能性：

```cpp
// 問題箇所
*reinterpret_cast<T*>(&mock_memory[(Reg::address - 0x1000) / 4]) = value;
```

#### 提案する解決策

`std::memcpy` または `std::bit_cast` を使用：

```cpp
template <typename Reg>
void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
    using T = typename Reg::RegValueType;
    auto idx = (Reg::address - 0x1000) / sizeof(T);
    
    // 方法1: memcpy（C++17以前でも動作）
    std::memcpy(&mock_memory[idx], &value, sizeof(T));
    
    // 方法2: bit_cast（C++20、コンパイル時評価可能）
    // mock_memory[idx] = std::bit_cast<std::uint32_t>(value);
}
```

### 5.2 SpiTransport の固定バッファサイズ

#### 現状の問題

```cpp
std::array<std::uint8_t, 9> tx_buf;  // cmd + up to 8 bytes
```

64-bit レジスタには不十分。

#### 提案する解決策

```cpp
template <typename Reg>
void raw_read(std::uint8_t reg_addr, void* data, std::size_t size) const noexcept {
    constexpr std::size_t max_size = 8;  // 64-bit まで対応
    assert(size <= max_size && "Register size exceeds maximum supported");
    
    std::array<std::uint8_t, 1 + max_size> tx_buf{};
    std::array<std::uint8_t, 1 + max_size> rx_buf{};
    // ...
}
```

または、テンプレートでサイズを静的に決定：

```cpp
template <std::size_t MaxRegSize = 8>
class SpiTransport { ... };
```

### 実装の優先度

**中** - テストコードだが、実Transport実装の参考になるため

---

## 6. 大規模レジスタセット対応

### 現状の問題

MCUの全ペリフェラル（STM32F4で数百レジスタ）を手動定義するのは非現実的。
SVD/CMSISファイルからの自動生成が必要。

### 提案する解決策

#### 6.1 生成スクリプト

Python スクリプトで SVD → umimmio 変換：

```
tools/
  svd2mmio.py       # SVD パーサー
  svd2mmio_test.py  # テスト
```

#### 6.2 生成コードの規約

```cpp
// 自動生成ヘッダの形式
// Generated from STM32F407.svd - DO NOT EDIT

#pragma once
#include <umimmio.hh>

namespace mcu::stm32f4 {

// NOLINTBEGIN(readability-identifier-naming)

struct USART1 : mm::Device<> {
    static constexpr std::uint32_t base_address = 0x40011000;
    
    struct SR : mm::Register<USART1, 0x00, 32, mm::RW, 0x00C00000> {
        struct PE   : mm::Field<SR, 0, 1> {};   // Parity error
        struct FE   : mm::Field<SR, 1, 1> {};   // Framing error
        // ...
    };
    // ...
};

// NOLINTEND(readability-identifier-naming)

} // namespace mcu::stm32f4
```

#### 6.3 ディレクトリ構造

```
lib/umiport/mcu/
  stm32f4/
    stm32f407.hh      # 自動生成（フルセット）
    stm32f407_gpio.hh # 自動生成（GPIOのみ）
    stm32f407_usart.hh
    ...
```

### 実装の優先度

**低** - 現状は手動定義で対応可能、将来的に検討

---

## 実装ロードマップ

| フェーズ | 項目 | 優先度 | 工数目安 |
|---------|------|--------|----------|
| Phase 1 | RO/WO アクセス制御 | 高 | 0.5日 |
| Phase 1 | modify() ドキュメント | 高 | 0.5日 |
| Phase 2 | Transport コンセプト | 中 | 1日 |
| Phase 2 | テストコード改善 | 中 | 0.5日 |
| Phase 3 | エラーポリシー拡張 | 低 | 1日 |
| Phase 3 | SVD自動生成 | 低 | 3日 |

---

## 7. スタイル・命名の問題

### 7.1 const noexcept の一貫性

`write()` と `modify()` で `const` の有無が不統一：

```cpp
// mmio.hh 現状
template <typename... Values>
void write(Values&&... values) noexcept { ... }  // non-const

template <typename... Values>
void modify(Values&&... values) const noexcept { ... }  // const
```

**問題**: 
- `write()` は論理的にもハードウェア状態を変更するが、Transport のメンバ変数は変更しない
- `modify()` は `const` なのに、内部で `reg_write()` を呼ぶ（これも const）

**提案**: 両方とも `const` にするか、両方とも non-const にする（一貫性）

```cpp
// 提案: 両方 const（Transport は状態を持たないため）
template <typename... Values>
void write(Values&&... values) const noexcept { ... }
```

### 7.2 命名: RegOps の可視性

`RegOps` が `protected` 継承で隠蔽されているが、Transport 実装者にとって分かりにくい：

```cpp
// 現状: friend で公開
class I2cTransport : public mm::ByteAdapter<I2cTransport<I2cBus, CheckPolicy>, CheckPolicy> {
    // ByteAdapter が RegOps を private 継承しているため、
    // using で公開する必要がある
};
```

**提案**: ドキュメントで Transport 実装パターンを明示（済）

### 7.3 一部の型エイリアスが冗長

```cpp
// mmio.hh 現状
using ParentRegType = std::conditional_t<IsRegister, void, Parent>;
// Field では別途定義
using ParentRegType = Reg;  // Required by RegOps
```

**問題**: `BitRegion` で定義した `ParentRegType` が Field で上書きされている

**修正案**: `BitRegion` の定義を修正し、Field での再定義を不要にする

```cpp
// BitRegion 内で正しく定義
using ParentRegType = std::conditional_t<IsRegister, void, Parent>;

// Field は BitRegion を継承するので、ParentRegType を再定義しない
// ただし、Field<Reg, ...> では Parent = Reg なので、
// IsRegister = false の場合、ParentRegType = Parent = Reg となり正しい

// 現状の Field での再定義は削除可能：
template <class Reg, std::size_t BitOffset, std::size_t BitWidth, class Access = Inherit>
struct Field : BitRegion<Reg, 0, Reg::reg_bits, BitOffset, BitWidth, Access, 0, false> {
    // using ParentRegType = Reg;  // ← 削除（BitRegion から継承される）
};
```

**影響**: コード整理のみ、動作に変更なし

---

## 8. バグと潜在的危険性

### 8.1 ~~【重大】~~ コンパイル時範囲チェック

**ステータス**: ✅ 対応完了（C++23 `if consteval` で実装）

**実装** (umimmio.hh):
```cpp
// 宣言のみ（定義なし）- constexpr評価で呼ばれるとコンパイルエラー
void mmio_compile_time_error_value_out_of_range();

template <std::integral T>
static constexpr auto value(T val) {
    if constexpr (bit_width < sizeof(T) * bits8) {
        constexpr auto max_value = (1ULL << bit_width) - 1;
        if consteval {
            if (static_cast<std::uint64_t>(val) > max_value) {
                mmio_compile_time_error_value_out_of_range();
            }
        }
    }
    // Runtime check deferred to write()/modify() via CheckPolicy + ErrorPolicy
    return DynamicValue<BitRegion, T>{val};
}
```

**仕組み**: 非constexpr関数は定数式で呼べないというC++仕様を利用。`-fno-exceptions` 環境でも動作。

**動作**:
- `constexpr auto v = Field::value(16);` → **コンパイルエラー**（4-bit fieldに16は範囲外）
- ランタイム呼び出しは `write()`/`modify()` 時に `CheckPolicy` + `ErrorPolicy` でチェック
```

### 8.2 【中】ByteAdapter のアドレス truncation

```cpp
// mmio.hh L472
self().raw_read(static_cast<std::uint8_t>(Reg::address), ...);
```

**問題**: `Reg::address` が 255 を超えると無言で切り捨てられる

**修正案**:

```cpp
static_assert(Reg::address <= 0xFF, "Register address exceeds 8-bit range for byte transport");
```

### 8.3 【低】flip() の RO/WO チェック順序

```cpp
template <typename Field>
void flip(Field /*field*/) const noexcept {
    static_assert(Field::bit_width == 1, "flip() only for 1-bit fields");
    static_assert(Field::AccessType::can_read && Field::AccessType::can_write, 
                 "flip() requires read-write access");
    check_transport_allowed<Field>();  // ★ Transport チェックが後
    ...
}
```

**問題**: `check_transport_allowed` が `static_assert` の後にあり、順序が他のメソッドと異なる

**修正案**: 他のメソッドと同じ順序に統一（Transport チェックを最初に）

```cpp
template <typename Field>
void flip(Field /*field*/) const noexcept {
    // 1. Transport チェック（他のメソッドと同じ順序）
    check_transport_allowed<Field>();
    
    // 2. 型制約チェック
    static_assert(Field::bit_width == 1, "flip() only for 1-bit fields");
    static_assert(Field::AccessType::can_read && Field::AccessType::can_write, 
                 "flip() requires read-write access");
    
    // 3. 実際の処理
    using ParentRegType = typename Field::ParentRegType;
    auto current = self().reg_read(ParentRegType{});
    current ^= Field::mask();
    self().reg_write(ParentRegType{}, current);
}
```

**影響**: エラーメッセージの順序が変わるのみ、動作に変更なし

---

## 9. テストの問題点

### 9.1 umitest の使い方の問題

#### 問題1: `run()` と `check_*()` の混在

```cpp
// test_register_value.cc - check_eq をトップレベルで使用
s.check_eq(static_cast<int>(transport.read(TestDevice::POWER_CTL{})), 0x01);
```

```cpp
// test.cc - run() 内で assert_* を使用
s.run("write with value()", [](umitest::TestContext& t) {
    t.assert_eq(mock_memory[0], 0x12345678u);
    return !t.failed;
});
```

**問題**: 
- `check_*()` はテスト名なしで結果だけ出力（失敗時にどのテストか分かりにくい）
- `run()` + `assert_*()` の方が推奨パターン

**修正案**: `check_*()` を使う場合は `section()` でグループ化するか、`run()` に統一

#### 問題2: test_value_get.cc の戻り値の型が非標準

```cpp
// test_value_get.cc
int main() {
    umitest::Suite s("mmio_value_get");
    constexpr auto min_val = TestDevice::CONFIG::MIN_VALUE::get();
    // ...
    s.check_eq(min_val, 0);  // ★ run() を使っていない
    return s.summary();
}
```

**問題**: テストがグループ化されておらず、失敗時の特定が困難

### 9.2 テストカバレッジの不足

| 項目 | 状態 | 備考 |
|------|------|------|
| 基本 write/read | ✅ | test.cc |
| フィールド操作 | ✅ | test.cc |
| Value 定数 | ✅ | test_register_value.cc, test_value_get.cc |
| I2C Transport | ✅ | test.cc |
| SPI Transport | ✅ | test.cc |
| 範囲チェック assert | ✅ | test_assert.cc |
| **RO レジスタへの write 禁止** | ❌ | コンパイルエラーテストなし |
| **WO レジスタからの read 禁止** | ❌ | コンパイルエラーテストなし |
| **Transport 制約** | ⚠️ | DirectOnlyDevice の正常系のみ |
| **複数フィールド同時 modify** | ✅ | test.cc |
| **64-bit レジスタ** | ❌ | テストなし |
| **負の値（signed）** | ❌ | テストなし |
| **境界値（0, max, max+1）** | ⚠️ | 一部のみ |
| **異なるレジスタ間の modify 禁止** | ❌ | テストなし |

### 9.3 追加すべきテスト

```cpp
// 1. RO/WO コンパイルエラーテスト（期待: コンパイル失敗）
// これはテストとして実行できないが、static_assert のコメントで明示

// 2. 64-bit レジスタテスト
struct Test64Device : mm::Device<> {
    struct REG64 : mm::Register<Test64Device, 0x00, 64> {
        struct FIELD32 : mm::Field<REG64, 0, 32> {};
        struct FIELD16 : mm::Field<REG64, 32, 16> {};
    };
};

// 3. 異なるレジスタ間の modify（実行時エラーになるべき）
// transport.modify(REG1::FIELD_A::Set{}, REG2::FIELD_B::Set{});  // 危険

// 4. signed 値のテスト
s.run("signed value handling", [](umitest::TestContext& t) {
    transport.write(SomeReg::SIGNED_FIELD::value(-1));  // 2の補数
    t.assert_eq(transport.read(SomeReg::SIGNED_FIELD{}), 0xFF);  // 期待値は？
    return !t.failed;
});
```

---

## 10. テスト品質の改善提案

### 10.1 テストの構造化

```cpp
// 改善例: test.cc
int main() {
    umitest::Suite s("mmio");
    
    // セットアップをラムダで共有
    auto setup = []() { mock_memory.fill(0); };
    
    s.section("Register Operations");
    s.run("write full register value", [&](umitest::TestContext& t) {
        setup();
        MockDirectTransport<> mcu;
        mcu.write(TestDevice::Block0::REG0::value(0x12345678));
        t.assert_eq(mock_memory[0], 0x12345678u, "register should contain written value");
        return !t.failed;
    });
    // ...
}
```

### 10.2 エッジケーステストの追加

```cpp
s.section("Edge Cases");
s.run("zero value write", [](umitest::TestContext& t) {
    mock_memory.fill(0xFF);
    MockDirectTransport<> mcu;
    mcu.write(TestDevice::Block0::REG0::value(0));
    t.assert_eq(mock_memory[0], 0u);
    return !t.failed;
});

s.run("max value for each bit width", [](umitest::TestContext& t) {
    // 8-bit max
    // 16-bit max
    // 32-bit max
    return !t.failed;
});
```

### 10.3 コンパイルエラーテストの文書化

```cpp
// test_compile_errors.cc（実行はしない、ドキュメント目的）
// 以下のコードはコンパイルエラーになることを確認済み

#if 0  // EXPECTED COMPILE ERROR: Cannot write to read-only register
void test_write_to_ro() {
    transport.write(ReadOnlyDevice::STATUS::value(0));
}
#endif

#if 0  // EXPECTED COMPILE ERROR: Cannot read from write-only register
void test_read_from_wo() {
    auto val = transport.read(WriteOnlyDevice::COMMAND{});
}
#endif
```

---

## 実装ロードマップ（更新）

| フェーズ | 項目 | 優先度 | 工数目安 |
|---------|------|--------|----------|
| **Phase 0** | **コンパイル時範囲チェック修正** | **緊急** | **0.5日** |
| **Phase 0** | **ByteAdapter アドレス検証** | **高** | **0.25日** |
| Phase 1 | RO/WO アクセス制御 | 高 | 0.5日 |
| Phase 1 | modify() ドキュメント | 高 | 0.5日 |
| Phase 1 | write() の const 統一 | 中 | 0.25日 |
| Phase 2 | Transport コンセプト | 中 | 1日 |
| Phase 2 | テストコード改善 | 中 | 1日 |
| Phase 2 | 64-bit レジスタテスト追加 | 中 | 0.5日 |
| Phase 3 | エラーポリシー拡張 | 低 | 1日 |
| Phase 3 | SVD自動生成 | 低 | 3日 |

---

## 11. プロジェクトで必要なトランスポート分析

### 11.1 現在実装済みのTransport

| Transport | ファイル | 用途 |
|-----------|----------|------|
| `DirectTransportTag` | `transport/direct.hh` | MCUペリフェラル（メモリマップドI/O） |
| `I2CTransportTag` | `transport/i2c.hh` | 外部デバイス（コーデック等） |
| `SPITransportTag` | `transport/spi.hh` | SPI接続デバイス |

### 11.2 MCUペリフェラル（DirectTransport）

現在プロジェクトで使用/定義されているMCUペリフェラル：

#### STM32F4系

| ペリフェラル | ファイル | 状態 | 用途 |
|--------------|----------|------|------|
| GPIO | `mcu/stm32f4/mcu/gpio.hh` | ✅ | LED、ボタン、コーデックリセット |
| I2C | `mcu/stm32f4/mcu/i2c.hh` | ✅ | CS43L22コーデック制御 |
| I2S | `mcu/stm32f4/mcu/i2s.hh` | ✅ | オーディオストリーム |
| RCC | `mcu/stm32f4/mcu/rcc.hh` | ✅ | クロック設定 |
| USB OTG | `mcu/stm32f4/mcu/usb_otg.hh` | ✅ | USB MIDI |
| EXTI | `mcu/stm32f4/mcu/exti.hh` | ✅ | 外部割り込み |
| Power | `mcu/stm32f4/mcu/power.hh` | ✅ | 電源管理 |
| **DMA** | なし | ❌ | オーディオDMA転送 |
| **TIM** | なし | ❌ | PWM、タイマー割り込み |
| **SPI** | なし | ❌ | 外部デバイス接続 |

#### STM32H7系（Daisy Seed）

| ペリフェラル | ファイル | 状態 | 用途 |
|--------------|----------|------|------|
| SAI | `mcu/stm32h7/mcu/sai.hh` | ✅ mmio | オーディオI/F (I2S互換) |
| DMA + DMAMUX | `mcu/stm32h7/mcu/dma.hh` | ✅ mmio | オーディオDMA |
| ADC | `mcu/stm32h7/mcu/adc.hh` | ✅ mmio | ノブ/CV入力 |
| QUADSPI | `mcu/stm32h7/mcu/qspi.hh` | ✅ mmio | 外部Flash |
| FMC | `mcu/stm32h7/mcu/fmc.hh` | ✅ mmio | 外部SDRAM |
| SDMMC | `mcu/stm32h7/mcu/sdmmc.hh` | ✅ mmio | SDカード |
| USART | `mcu/stm32h7/mcu/usart.hh` | ✅ mmio | MIDI UART |
| GPIO | `mcu/stm32h7/mcu/gpio.hh` | ✅ mmio | 汎用I/O |
| I2C | `mcu/stm32h7/mcu/i2c.hh` | ✅ mmio | コーデック制御 |
| RCC | `mcu/stm32h7/mcu/rcc.hh` | ✅ mmio | クロック |
| PWR | `mcu/stm32h7/mcu/pwr.hh` | ✅ mmio | 電源 |
| Flash | `mcu/stm32h7/mcu/flash.hh` | ✅ mmio | 内部Flash |
| **SPI** | なし | ❌ | 外部デバイス |
| **TIM** | なし | ❌ | PWM/タイマー |
| **DAC** | なし | ❌ | アナログ出力 |

### 11.3 外部デバイス（I2C/SPI Transport）

#### 現在サポート済みのデバイス

| デバイス | Transport | ファイル | 用途 |
|----------|-----------|----------|------|
| CS43L22 | I2C | `device/cs43l22/` | オーディオDAC (STM32F4-Discovery) |
| WM8731 | I2C | `device/wm8731/` | オーディオCODEC (Daisy Seed Rev5) |
| PCM3060 | I2C | `device/pcm3060/` | オーディオADC/DAC (Daisy Seed DFM) |
| AK4556 | GPIO only | `device/ak4556/` | オーディオCODEC (Daisy Seed Rev4) |

#### 今後必要になる可能性のあるデバイス

| デバイス | Transport | 用途 | 優先度 |
|----------|-----------|------|--------|
| **SSD1306/SH1106** | I2C/SPI | OLEDディスプレイ | 高 |
| **SSD1327** | I2C | グレースケールOLED | 中 |
| **IS25LP064** | QSPI | 外部Flash (Daisy) | ✅済 (QSPI) |
| **IS42S16400J** | FMC | 外部SDRAM (Daisy) | ✅済 (FMC) |
| **MCP4822** | SPI | DAC | 低 |
| **MCP3008** | SPI | ADC | 低 |
| **74HC595** | SPI | シフトレジスタ (LED) | 中 |
| **MAX7219** | SPI | LEDマトリクス | 中 |
| **TLC5940** | SPI | LEDドライバ(PWM) | 低 |
| **CAT24C** | I2C | EEPROM | 低 |
| **MPR121** | I2C | タッチセンサ | 中 |
| **AS5600** | I2C | 磁気エンコーダ | 中 |
| **BME280** | I2C/SPI | 環境センサ | 低 |

### 11.4 必要な新規Transport

#### 11.4.1 SPITransport（正式実装）

現在テストコードにのみ存在。正式なTransport実装が必要：

```cpp
// transport/spi.hh
template <typename SpiDriver>
class SpiTransport : public mm::ByteAdapter<SpiTransport<SpiDriver>> {
    SpiDriver& spi;
    GpioPin cs;
public:
    using TransportTag = mm::SPITransportTag;
    
    // 読み書き時にCS制御が必要
    void raw_write(std::uint8_t reg_addr, const void* data, std::size_t size);
    void raw_read(std::uint8_t reg_addr, void* data, std::size_t size);
};
```

**必要な機能:**
- CS (Chip Select) の自動制御
- 読み出しコマンドビット（MSB=1等）の自動付加
- 可変長データ対応

#### 11.4.2 WM8731専用Transport（9-bit レジスタ）

WM8731は特殊なI2Cプロトコル（7-bit addr + 9-bit data を16-bitで送信）：

```cpp
// umiport/device/wm8731/wm8731_transport.hh
template <typename I2cBus>
class Wm8731Transport {
public:
    // 16-bit write: (reg_addr << 9) | data
    template <typename Reg>
    void reg_write(Reg, typename Reg::RegValueType value) {
        std::uint16_t word = (Reg::address << 9) | (value & 0x1FF);
        std::uint8_t buf[2] = {
            static_cast<std::uint8_t>(word >> 8),
            static_cast<std::uint8_t>(word & 0xFF)
        };
        i2c.write(device_address, buf, 2);
    }
    
    // WM8731 は読み出し不可（Write-Only）
};
```

**問題:** umimmio の `ByteAdapter` は8-bitアドレス前提なので対応が必要

**解決策の選択肢:**

1. **ByteAdapter を使わない専用Transport（推奨）**

```cpp
// RegOps を直接継承し、reg_write のみ実装
template <typename I2cBus, typename CheckPolicy = std::true_type>
class Wm8731Transport : private mm::RegOps<Wm8731Transport<I2cBus, CheckPolicy>, CheckPolicy> {
    friend class mm::RegOps<Wm8731Transport<I2cBus, CheckPolicy>, CheckPolicy>;
    I2cBus& i2c;
    std::uint8_t device_address;

public:
    using TransportTag = mm::I2CTransportTag;
    using mm::RegOps<Wm8731Transport, CheckPolicy>::write;
    using mm::RegOps<Wm8731Transport, CheckPolicy>::modify;
    // read/is は WO デバイスなので公開しない

    Wm8731Transport(I2cBus& bus, std::uint8_t addr) : i2c(bus), device_address(addr) {}

    template <typename Reg>
    auto reg_read(Reg) const noexcept -> typename Reg::RegValueType {
        static_assert(sizeof(Reg) == 0, "WM8731 registers are write-only");
        return 0;
    }

    template <typename Reg>
    void reg_write(Reg, typename Reg::RegValueType value) const noexcept {
        // 7-bit register address + 9-bit data = 16-bit word
        std::uint16_t word = (static_cast<std::uint16_t>(Reg::address) << 9) 
                           | (value & 0x1FF);
        std::uint8_t buf[2] = {
            static_cast<std::uint8_t>(word >> 8),
            static_cast<std::uint8_t>(word & 0xFF)
        };
        i2c.write(device_address, buf, 2);
    }
};
```

2. **ByteAdapter にアドレス幅テンプレートを追加（将来検討）**

```cpp
// mmio.hh 拡張案
template <class Derived, typename CheckPolicy = std::true_type, 
          typename AddrType = std::uint8_t>  // アドレス型を可変に
class ByteAdapter : private RegOps<Derived, CheckPolicy> {
    // ...
};
```

**推奨**: 選択肢1（専用Transport）を採用。WM8731のような特殊プロトコルは
汎用フレームワークに組み込むより、専用実装の方が明確で保守しやすい

#### 11.4.3 BitBangTransport（低速/デバッグ用）

GPIO でプロトコルをエミュレート：

```cpp
template <typename GpioDriver>
class BitBangI2cTransport { ... };

template <typename GpioDriver>
class BitBangSpiTransport { ... };
```

**用途:**
- デバッグ時のロジアナ確認
- ピン制約時の代替手段
- 低速デバイス（EEPROM等）

### 11.5 特殊なMMIO操作パターン

#### 11.5.1 Atomic Bit-Band アクセス（Cortex-M3/M4）

```cpp
// STM32のBit-Bandリージョンを使ったアトミックビット操作
// 0x2200'0000 + (byte_offset * 32) + (bit_number * 4)
template <typename Reg, std::size_t BitPos>
struct BitBandAccess {
    static constexpr std::uintptr_t bit_band_addr = 
        0x2200'0000 + ((Reg::address - 0x2000'0000) * 32) + (BitPos * 4);
    
    static void set() {
        *reinterpret_cast<volatile std::uint32_t*>(bit_band_addr) = 1;
    }
    static void reset() {
        *reinterpret_cast<volatile std::uint32_t*>(bit_band_addr) = 0;
    }
};
```

#### 11.5.2 DMAバッファ配置制約

```cpp
// SRAM1 (D2) に配置が必要なDMAバッファ
// リンカスクリプトと連携
struct [[gnu::section(".dma_buffer")]] alignas(32) DmaBuffer {
    std::array<std::int32_t, 256> data;
};
```

#### 11.5.3 Memory-Mapped外部デバイス（QSPI/FMC）

```cpp
// QSPI Memory-mappedモードでの直接アクセス
// 0x9000'0000 からFlashが見える
constexpr std::uintptr_t QSPI_FLASH_BASE = 0x9000'0000;

// FMC SDRAMアクセス
// 0xC000'0000 からSDRAMが見える
constexpr std::uintptr_t SDRAM_BASE = 0xC000'0000;
```

### 11.6 優先実装ロードマップ

| 優先度 | 項目 | 理由 |
|--------|------|------|
| 🔴 高 | SPI Transport 正式実装 | OLEDディスプレイ、外部センサ対応 |
| 🔴 高 | WM8731 Transport | Daisy Seed Rev5 対応 |
| 🟡 中 | STM32F4 DMA レジスタ定義 | オーディオDMA |
| 🟡 中 | STM32F4/H7 TIM レジスタ定義 | PWM、エンコーダ |
| 🟢 低 | BitBang Transport | デバッグ用 |
| 🟢 低 | Bit-Band ヘルパー | 最適化 |

---

## 参考資料

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- [Embedded C++ Guidelines](https://www.embedded.com/)
- [ARM CMSIS SVD Format](https://arm-software.github.io/CMSIS_5/SVD/html/index.html)
