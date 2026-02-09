# ボード定義・継承・DB アーキテクチャ

**ステータス:** 確定版  **策定日:** 2026-02-09
**分割元:** 旧 03_ARCHITECTURE.md §5-§7, §9, §10.1, §10.4-§10.6

**関連文書:**
- [../foundations/architecture.md](../foundations/architecture.md) — 全体アーキテクチャ (設計原則、パッケージ構成)
- [../hal/concept_design.md](../hal/concept_design.md) — HAL Concept 設計
- [config.md](config.md) — ボード設定アーキテクチャの詳細 (設定の統一と局所化)
- [config_analysis.md](config_analysis.md) — ボード設定スキーマの比較分析
- [project_structure.md](project_structure.md) — ユーザープロジェクト構成

---

## 1. ボード定義の二重構造

ボード定義は **Lua 側**（ビルド構成）と **C++ 側**（型定義）の二重構造を持つ。Lua はコンパイル前に作用し（フラグ, リンカスクリプト）、C++ はコンパイル時に作用する（型検証）。

| 側 | ファイル | 役割 | 消費者 |
|----|---------|------|--------|
| Lua | `boards/<name>/board.lua` | MCU 選択, クロック, ピン, デバッグ | xmake ルール |
| C++ | `board/<name>/platform.hh` | HAL Concept 充足型, Output/Timer 結合 | コンパイラ |
| C++ | `board/<name>/board.hh` | constexpr 定数（ピン, クロック, メモリ） | platform.hh |

### platform.hh の具体例（stm32f4-disco）

```cpp
#pragma once
#include <umihal/concept/platform.hh>
#include <umiport/mcu/stm32f4/uart_output.hh>
#include <umiport/mcu/stm32f4/i2c.hh>
#include <umiport/arm/cortex-m/dwt.hh>
#include <umidevice/audio/cs43l22/cs43l22.hh>
#include <umiport/board/stm32f4-disco/board.hh>

namespace umi::port {
struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using I2C    = stm32f4::I2C1;
    using Codec  = umi::device::CS43L22Driver<I2C>;

    static constexpr const char* name() { return "stm32f4-disco"; }
    static void init() { Output::init(); }
};

static_assert(umi::hal::Platform<Platform>);
static_assert(umi::hal::PlatformWithTimer<Platform>);
static_assert(umi::hal::I2cTransport<Platform::I2C>);
} // namespace umi::port
```

### board.hh の具体例

```cpp
#pragma once
namespace umi::board {
struct Stm32f4Disco {
    static constexpr uint32_t hse_frequency = 8'000'000;
    static constexpr uint32_t system_clock  = 168'000'000;
    static constexpr uint32_t apb1_clock    = 42'000'000;
    struct Pin { static constexpr uint32_t console_tx = 9, console_rx = 10; };
    struct Memory {
        static constexpr uint32_t flash_base = 0x08000000, flash_size = 1024 * 1024;
        static constexpr uint32_t ram_base   = 0x20000000, ram_size   = 128 * 1024;
    };
};
} // namespace umi::board
```

---

## 2. ボード継承メカニズム

board.lua は `extends` で差分のみ記述できる。フォーク不要のカスタマイズ。

```lua
-- project/boards/my-custom-board/board.lua
return {
    extends = "stm32f4-disco",
    clock = { hse = 25000000 },          -- 水晶だけ変更
    pins = { audio_i2s_sck = "PB13" },   -- 1 ピンだけ変更
}
```

### deep_merge アルゴリズム

親テーブルの全フィールドを深いコピーし、子のフィールドで上書きする。テーブル同士は再帰マージ、それ以外は完全置換。

| 親の値 | 子の値 | 結果 |
|--------|--------|------|
| スカラー | スカラー | 子で上書き |
| `table` | `table` | 再帰マージ |
| `table` | 指定なし | 親を維持 |
| 任意 | `false` | 明示的に無効化 |

解決例: 親 `{ clock = { hse = 8M, sysclk = 168M } }` + 子 `{ clock = { hse = 25M } }` = `{ clock = { hse = 25M, sysclk = 168M } }`

ボード定義の検索順序:
1. `<project>/boards/<name>/board.lua` (プロジェクトローカル、最優先)
2. `umiport/boards/<name>/board.lua` (umiport 同梱)

---

## 3. MCU データベースと memory.ld 生成

### データベース構造

MCU データは umiport 内に Lua 形式で一元管理。arm-embedded パッケージとのインターフェイスは **`embedded.core`（コア名）1 つ** に集約。

```lua
-- database/family/stm32f4.lua
return {
    core = "cortex-m4f",  vendor = "st",  src_dir = "stm32f4",
    openocd_target = "stm32f4x",  flash_tool = "pyocd",
    memory = {
        FLASH = { attr = "rx",  origin = 0x08000000 },
        SRAM  = { attr = "rwx", origin = 0x20000000 },
        CCM   = { attr = "rwx", origin = 0x10000000 },
    },
}
-- database/mcu/stm32f4/stm32f407vg.lua
local family = dofile(path.join(os.scriptdir(), "../../family/stm32f4.lua"))
family.device_name = "STM32F407VG"
family.memory.FLASH.length = "1M"
family.memory.SRAM.length  = "192K"
family.memory.CCM.length   = "64K"
return family
```

### memory.ld 自動生成

Lua DB から MEMORY 定義を自動生成。任意のメモリ領域数に対応（PlatformIO の 2 リージョン制限なし）。`sections.ld`（手書き、ファミリ共通）が `INCLUDE memory.ld` で取り込む。

| 合成パターン | MEMORY | SECTIONS | 用途 |
|-------------|--------|----------|------|
| A: 全自動 | DB から生成 | ファミリ共通 | ライブラリテスト |
| B: セクション追加 | DB から生成 | 標準 + `-T extra.ld` | 中規模アプリ |
| C: 完全カスタム | アプリ所有 | アプリ所有 | kernel, bootloader |

---

## 4. xmake ルール設計

`on_load` で MCU データベースを解決し `embedded.core` を設定。`on_config` でボード固有設定を適用する二段構成。

```lua
rule("umiport.board")
    on_load(function(target)   -- board.lua 読込 + extends 継承解決 + MCU DB -> embedded.core 設定
    end)
    on_config(function(target) -- includedirs, startup.cc, memory.ld 生成, ldflags フィルタ
    end)
```

使用側は 1 行で完結:

```lua
target("umirtm_stm32f4_renode")
    set_kind("binary")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "stm32f4-renode")
    add_deps("umirtm", "umiport")
    add_files("test_*.cc")
```

**ライフサイクル制約:** xmake の on_load は on_config より先に全ターゲットで完了する。`umiport.board` の on_load で設定した `embedded.core` が `embedded` ルールの on_load に遡及しない場合がある（ルール宣言順序依存）。回避策: `set_values("embedded.core", "cortex-m4f")` で順序非依存にする。ldflags フィルタ（`-T` の除去と再追加）は on_config 内で動作確認済み。

---

## 5. 論点と解決策

### 5.1 ボードデータの Single Source of Truth

**問題:** ハードウェア定数（HSE 周波数、ピン番号、メモリマップ等）が複数箇所に散在し、手動同期が必要。

**解決策:** ボード定義ファイル（board.lua）を唯一のソースとし、board.hh・memory.ld・clock_config.hh・platform.hh を全て生成する。MCU層とボード層を分離し、設定の統一と局所化を実現する。

詳細は **[config.md](config.md)** を参照。

### 5.2 デュアルコア MCU

**問題:** STM32H755（CM7 + CM4）のようなデュアルコア MCU の扱い。

**解決策:** デュアルコアはビルドターゲットとして分離し、各コアに独立した Platform を定義する。コア間共有は shared memory セクションで行う。

```cpp
// board/stm32h755-disco/platform_cm7.hh
namespace umi::port {
struct PlatformCM7 {
    using Output = stm32h7::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using Clock  = stm32h7::ClockTreeImpl;
    static constexpr const char* name() { return "stm32h755-cm7"; }
    static void init() { Output::init(); }
};
static_assert(umi::hal::PlatformFull<PlatformCM7>);
}

// board/stm32h755-disco/platform_cm4.hh
namespace umi::port {
struct PlatformCM4 {
    using Output = stm32h7::UartOutputCM4;
    using Timer  = cortex_m::DwtTimer;
    static constexpr const char* name() { return "stm32h755-cm4"; }
    static void init() { Output::init(); }
};
static_assert(umi::hal::Platform<PlatformCM4>);
}
```

```lua
-- boards/stm32h755-disco/board.lua
return {
    mcu = "stm32h755zi",
    cores = {
        cm7 = { platform = "platform_cm7.hh", entry = "_start_cm7" },
        cm4 = { platform = "platform_cm4.hh", entry = "_start_cm4" },
    },
    -- shared memory for IPC
    memory_extra = {
        SHARED = { attr = "rw", origin = 0x38000000, length = "64K" },
    },
}
```

```lua
-- xmake.lua での使用
target("app_cm7")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "stm32h755-disco")
    set_values("umiport.core", "cm7")  -- コア選択

target("app_cm4")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "stm32h755-disco")
    set_values("umiport.core", "cm4")
```

各コアは独立したビルドターゲット（独立した ELF）として扱う。`umiport.core` で platform.hh と startup の切り替えを行う。コア間通信は `SHARED` メモリセクションに配置するデータ構造で行い、HAL concept のスコープ外とする。

### 5.3 非 ARM アーキテクチャ

**問題:** RISC-V (ESP32-C3)、Xtensa (ESP32) は startup/linker パターンが ARM Cortex-M と根本的に異なる。

**解決策:** アーキテクチャごとにパッケージを分離し、MCU DB の `core` フィールドで対応パッケージを自動選択する。

```
xmake-repo/synthernet/packages/
├── a/arm-embedded/       # 既存: Cortex-M 用
├── r/riscv-embedded/     # 新規: RISC-V 用
└── x/xtensa-embedded/    # 新規: Xtensa 用（ESP-IDF 統合）
```

```lua
-- database/family/esp32c3.lua
return {
    core = "riscv32-imc",  -- arm-embedded ではなく riscv-embedded が処理
    vendor = "espressif",
    toolchain = "riscv-none-elf",
    src_dir = "esp32c3",
    -- ESP-IDF は独自のリンカスクリプトとスタートアップを持つ
    startup = "esp-idf",   -- umiport の startup.cc を使わない
    linker = "esp-idf",    -- umiport の memory.ld/sections.ld を使わない
}
```

```lua
-- rules/board.lua の on_load 内
local core = mcu_config.core
if core:match("^cortex") then
    target:add("packages", "arm-embedded")
elseif core:match("^riscv") then
    target:add("packages", "riscv-embedded")
elseif core:match("^xtensa") then
    target:add("packages", "xtensa-embedded")
end
target:set("values", "embedded.core", core)
```

umiport 内部の変更:

```
lib/umiport/
├── include/umiport/
│   ├── arm/cortex-m/         # 既存
│   ├── riscv/                # 新規: RISC-V 共通コード
│   └── mcu/
│       ├── stm32f4/          # 既存
│       └── esp32c3/          # 新規
├── src/
│   ├── stm32f4/              # 既存: startup.cc + syscalls.cc + sections.ld
│   ├── esp32c3/              # 新規: write.cc のみ（startup/linker は ESP-IDF 管理）
│   └── host/                 # 既存
```

重要な設計判断:
- ESP-IDF 統合の場合、startup/linker は ESP-IDF が管理する。umiport は `write_bytes()` の実装のみ提供。`_write()` syscall は不要（newlib を使わない）
- `write_bytes()` の link-time 注入パターンは全アーキテクチャで統一。これが唯一のアーキテクチャ横断インターフェイス
- umihal concept は全アーキテクチャ共通。`GpioOutput` を満たす型が ARM でも RISC-V でも同じ concept で検証される

### 5.4 ボード定義バージョニング

**問題:** umiport のボード定義フォーマットが変更された場合、`extends` で拡張しているプロジェクトローカルボードが影響を受ける。

**解決策:** board.lua に `schema_version` フィールドを導入し、互換性チェックを board_loader.lua で行う。

```lua
-- boards/stm32f4-disco/board.lua
return {
    schema_version = 1,  -- 現行フォーマット
    mcu = "stm32f407vg",
    clock = { hse = 8000000, sysclk = 168000000 },
    -- ...
}
```

```lua
-- rules/board_loader.lua
local CURRENT_SCHEMA = 1

local function load_board(umiport_dir, board_name, target)
    local board = dofile(board_path)

    -- スキーマバージョンチェック
    local version = board.schema_version or 0
    if version > CURRENT_SCHEMA then
        raise("board '%s' requires schema_version %d, but umiport supports up to %d. "
              .. "Update umiport to use this board.", board_name, version, CURRENT_SCHEMA)
    end
    if version < CURRENT_SCHEMA then
        -- 将来: マイグレーション関数を呼ぶ
        cprint("${yellow}warning: board '%s' uses schema_version %d (current: %d). "
               .. "Consider updating the board definition.${reset}", board_name, version, CURRENT_SCHEMA)
    end

    -- extends 解決
    if board.extends then
        local parent = load_board(umiport_dir, board.extends, target)
        board = deep_merge(parent, board)
    end
    return board
end
```

バージョニングポリシー:
- **schema_version 未指定 = v0**: 旧フォーマット。警告表示のみ、動作継続
- **マイナー変更** (フィールド追加): version 据え置き。新フィールドにはデフォルト値を設定
- **破壊的変更** (フィールド改名/削除): version をインクリメント。マイグレーション関数を提供
- **UMI が 1.0 に到達するまで**: schema_version = 1 のまま運用し、破壊的変更はリリースノートで通知

```lua
-- マイグレーション例（将来）
local migrations = {
    [0] = function(board)
        -- v0 -> v1: "uart_tx" を "pins.console_tx" に移動
        if board.uart_tx then
            board.pins = board.pins or {}
            board.pins.console_tx = board.uart_tx
            board.uart_tx = nil
        end
        return board
    end,
}
```
