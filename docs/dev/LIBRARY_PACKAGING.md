# UMI-OS ライブラリパッケージングガイド

UMI-OSライブラリの標準構造を定義する。

## 設計原則

1. **Single Source of Truth** - ドキュメントはコードコメントとREADMEのみ
2. **実行可能なサンプル** - サンプルはコンパイル可能なコード
3. **生成ツール不要** - Sphinx/Doxygen生成は使わない（必要になったら追加）

## ディレクトリ構造

```
lib/<name>/
├── README.md              # クイックスタート
├── xmake.lua              # ビルド設定
├── include/               # 公開ヘッダ（コメント付き）
│   └── *.hh               # シンプルな場合は直下
│   └── <category>/        # 必要ならカテゴリ分け
├── test/                  # ユニットテスト
│   └── test_*.cc
├── examples/              # 動作するサンプル
│   └── *.cc
└── docs/                  # オプション
    └── design.md          # 設計思想（複雑なライブラリのみ）
```

### includeの構造

- **シンプルなライブラリ**: `include/*.hh` 直下に配置
- **カテゴリ分けが必要な場合**: `include/<category>/*.hh`
- **ライブラリ名のフォルダは不要**: `include/<name>/` は冗長

## コメントルール

### 基本方針

- **`///` 一行のみ** - シンプルに保つ
- **型・クラスには書く** - 何であるかを説明
- **自明な関数には書かない** - `size()`, `empty()`, `clear()` 等
- **`@brief`, `@param`, `@return` は使わない** - 冗長

### 良い例

```cpp
/// 32-bit Universal MIDI Packet.
struct UMP32 {
    uint32_t word = 0;

    /// Create Note On message.
    static constexpr UMP32 note_on(uint8_t ch, uint8_t note, uint8_t vel) noexcept;

    /// Check if this is a Note On (velocity > 0).
    bool is_note_on() const noexcept;

    // コメント不要 - 名前で自明
    uint8_t channel() const noexcept;
    uint8_t note() const noexcept;
    uint8_t velocity() const noexcept;
};

/// MIDI byte stream parser.
class Parser {
public:
    /// Parse one byte, returns true when complete message ready.
    bool parse(uint8_t byte, UMP32& out) noexcept;

    // コメント不要
    void reset() noexcept;
};
```

### 悪い例

```cpp
// ❌ 冗長 - @記法は使わない
/// @brief Create Note On message.
/// @param ch MIDI channel (0-15)
/// @param note Note number (0-127)
/// @param vel Velocity (1-127)
/// @return UMP32 packet
static constexpr UMP32 note_on(uint8_t ch, uint8_t note, uint8_t vel) noexcept;

// ❌ 自明な関数にコメント
/// Get the channel.
uint8_t channel() const noexcept;

// ❌ 複数行で冗長
/// 32-bit Universal MIDI Packet.
///
/// This struct represents a MIDI message in UMP format.
/// It stores the data as a single uint32_t for efficiency.
struct UMP32 { ... };
```

### 複雑なAPIのみ詳細を書く

```cpp
/// Parse SysEx message with timeout.
/// Returns partial result if timeout before complete message.
/// Call repeatedly until returns Ok or Error.
Result<SysExData> parse_sysex(uint8_t byte, uint32_t timeout_ms) noexcept;
```

## README.md

30行以下に収める：

```markdown
# <name> - 一行説明

簡潔な説明。

## 特徴

- 機能1
- 機能2

## クイックスタート

\`\`\`cpp
#include <<name>.hh>

// 最小限の動作例
\`\`\`

## ビルド

\`\`\`bash
xmake build <name>_test
xmake run <name>_test
\`\`\`
```

## examples/

コンパイル可能なサンプル：

```cpp
// examples/basic.cc
#include <umidi.hh>
#include <cstdio>

int main() {
    umidi::Parser parser;
    umidi::UMP32 ump;

    uint8_t bytes[] = {0x90, 60, 100};
    for (uint8_t b : bytes) {
        if (parser.parse(b, ump) && ump.is_note_on()) {
            printf("Note: %d\n", ump.note());
        }
    }
}
```

## チェックリスト

### 必須

- [ ] `README.md` - クイックスタート
- [ ] `include/` - コメント付きヘッダ
- [ ] `test/` - ユニットテスト
- [ ] `examples/` - 動作するサンプル
- [ ] `xmake.lua` - ビルド設定

### オプション

- [ ] `docs/design.md` - 設計思想（複雑なライブラリのみ）
- [ ] `docs/PROTOCOL.md` - プロトコル仕様（必要な場合）

### 作成しない

- `docs/conf.py`, `docs/Doxyfile` - 生成ツール設定
- `docs/API.md` - 手書きAPIリファレンス
- `docs/<MODULE>.md` - モジュール別ドキュメント
- `docs/ja/` - 翻訳
