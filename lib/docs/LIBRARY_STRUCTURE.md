# UMI ライブラリ構造規約

lib/ 以下に配置する各ライブラリの標準構造を定義する。

---

## 設計原則

1. **Single Source of Truth** — ドキュメントはコードコメントと README のみ。Sphinx/Doxygen 生成は使わない
2. **実行可能なサンプル** — サンプルはコンパイル可能なコード
3. **名前空間の明確化** — `#include <libname/...>` で所属ライブラリが常に分かる

---

## 標準構造

```
lib/<libname>/
├── README.md                  # [必須] ライブラリ概要
├── xmake.lua                  # [必須] ビルド定義
├── include/<libname>/         # [必須] 公開ヘッダ
│   ├── core/                  #   機能別サブディレクトリ（任意）
│   └── ...
├── src/                       # [任意] 実装ファイル（ヘッダオンリーなら不要）
│   └── *.cc
├── test/                      # [必須] テスト
│   ├── xmake.lua              #   テストビルド定義
│   └── test_*.cc
├── examples/                  # [推奨] 動作するサンプル
│   └── *.cc
└── docs/                      # [推奨] 設計文書（複雑なライブラリのみ）
    └── DESIGN.md
```

---

## 各要素の詳細

### README.md（必須）

30行以下に収める。以下のテンプレートに従う：

```markdown
# <libname> - 一行説明

簡潔な説明。

## 依存関係

- `umios/core` — AudioContext

## 主要API

- `TypeName` — 説明
- `concept ConceptName` — 説明

## クイックスタート

\`\`\`cpp
#include <libname/foo.hh>

// 最小限の動作例
\`\`\`

## ビルド・テスト

\`\`\`bash
xmake build test_libname
xmake run test_libname
\`\`\`
```

### xmake.lua（必須）

ライブラリのビルドターゲットを定義する。ヘッダオンリーの場合も `headeronly` ターゲットを定義し、依存関係と include パスを明示する。

```lua
-- ヘッダオンリーの例
target("libname")
    set_kind("headeronly")
    add_headerfiles("include/(libname/**.hh)")
    add_includedirs("include", { public = true })
```

### include/\<libname\>/（必須）

公開ヘッダの配置場所。利用側は以下の形式でインクルードする：

```cpp
#include <libname/foo.hh>
#include <libname/core/bar.hh>
```

規則：

- トップレベルに直接 `.hh` を置かない。必ず `include/<libname>/` 以下に配置する
- 機能が多い場合はサブディレクトリで分類する（`core/`, `codec/`, `filter/` 等）
- 内部専用ヘッダは `include/<libname>/detail/` に配置する

### src/（任意）

`.cc` 実装ファイルの配置場所。ヘッダオンリーライブラリでは不要。

- include と同じサブディレクトリ構造を対応させると見通しが良い
- テスト専用のユーティリティは `test/` に置く（ここには置かない）

### test/（必須）

- ファイル名は `test_<topic>.cc` とする
- 最小のライブラリでも最低1つのテストを持つこと
- テスト固有のビルド定義は `test/xmake.lua` に記述する
- テストフレームワークは `tests/test_common.hh` を使用する

### examples/（推奨）

コンパイル可能なサンプルコード。小規模なライブラリでは README.md 内のコード例で十分であり、不要。

```cpp
// examples/basic.cc
#include <umidi/core/ump.hh>
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

### docs/（推奨）

設計判断の記録や詳細な仕様が必要な場合に使用する。小規模なライブラリでは README.md で十分であり、不要。

---

## コメントルール

### 基本方針

- **`///` 一行のみ** — シンプルに保つ
- **型・クラスには書く** — 何であるかを説明
- **自明な関数には書かない** — `size()`, `empty()`, `clear()` 等
- **`@brief`, `@param`, `@return` は使わない** — 冗長

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

---

## サブディレクトリの分類指針

include/ 以下のサブディレクトリは、ライブラリの性質に応じて分類する：

| ライブラリの性質 | 分類基準 | 例 |
|-----------------|---------|-----|
| プロトコル実装 | プロトコル層・機能 | `core/`, `codec/`, `messages/`, `protocol/` |
| DSP/アルゴリズム | 処理カテゴリ | `core/`, `filter/`, `synth/`, `audio/` |
| ハードウェア抽象 | ハードウェア階層 | `arch/`, `mcu/`, `board/`, `device/` |
| 小規模ユーティリティ | サブディレクトリなし | 直接 `include/<libname>/` に配置 |

---

## 命名規則

| 対象 | 規則 | 例 |
|------|------|-----|
| ライブラリ名 | `umi` プレフィックス + 小文字 | `umidi`, `umidsp`, `umifs` |
| ヘッダファイル | `lower_case.hh` | `audio_context.hh` |
| 実装ファイル | `lower_case.cc` | `lfs_core.cc` |
| テストファイル | `test_<topic>.cc` | `test_codec.cc` |
| ビルドターゲット | ライブラリ名そのまま | `target("umidi")` |
| テストターゲット | `test_<libname>` | `target("test_umidi")` |

---

## チェックリスト

新規ライブラリ作成時、または既存ライブラリの整備時に確認する：

### 必須

- [ ] `README.md` がある（30行以下）
- [ ] `xmake.lua` がある
- [ ] 公開ヘッダが `include/<libname>/` 以下にある
- [ ] `#include <libname/...>` でインクルードできる
- [ ] `test/` にテストがある
- [ ] `xmake test` でテストが通る
- [ ] CODING_STYLE.md に準拠している

### 推奨

- [ ] `examples/` に動作するサンプルがある
- [ ] `docs/DESIGN.md` に設計思想がある（複雑なライブラリのみ）

### 作成しない

- `docs/conf.py`, `docs/Doxyfile` — 生成ツール設定
- `docs/API.md` — 手書き API リファレンス（コードコメントが Single Source of Truth）
- `docs/<MODULE>.md` — モジュール別ドキュメント
