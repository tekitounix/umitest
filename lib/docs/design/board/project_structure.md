# ユーザープロジェクト構成

**ステータス:** 設計中
**関連文書:**
- [../foundations/architecture.md](../foundations/architecture.md) — パッケージ構成と依存関係
- [config.md](config.md) — ボード定義とコード生成

---

## 1. 本ドキュメントの目的

UMI を使ってソフトウェアを開発するユーザーの視点で、**プロジェクト構成の推奨パターン**を示す。対象シナリオは大きく 2 つ:

1. **既存 BSP をそのまま利用する場合** — umiport 同梱のボード定義を使い、アプリケーション開発に集中する
2. **カスタム BSP を書く場合** — 自作ボードや未対応ボードに UMI を移植する

---

## 2. ターゲットプラットフォーム

UMI は同一の Processor コードから複数ターゲットにビルドできる。ユーザーの開発フローはターゲットによって異なる。

| ターゲット | ビルド形式 | 実行方法 | 代表的な用途 |
|-----------|-----------|---------|-------------|
| **ARM 組込み** | `.elf` → `.bin`/`.hex` | pyOCD / ST-Link でフラッシュ | STM32F4, STM32H7, Daisy Pod |
| **WASM** | `.wasm` | ブラウザ / Node.js で実行 | Web シンセ、ヘッドレスシミュレーション |
| **ホスト** | ネイティブバイナリ | そのまま実行 | ユニットテスト、デスクトッププラグイン |
| **Renode** | `.elf` | Renode シミュレータ | CI 検証、HW なしデバッグ |

---

## 3. プロジェクト構成パターン

### 3.1 パターン A: 既存 BSP をそのまま利用

umiport 同梱のボード定義（例: `stm32f4-disco`, `stm32f4-renode`）をそのまま使う場合。ユーザーはアプリケーションコードのみ書けばよい。

#### ディレクトリ構成

```
my-project/
├── xmake.lua                  ← プロジェクト定義
├── src/
│   ├── main.cc                ← エントリポイント
│   └── my_synth.hh            ← Processor 実装
└── (build/)                   ← xmake が自動生成
    └── generated/
        ├── board.hh           ← constexpr 定数（生成）
        ├── clock_config.hh    ← PLL 係数（生成）
        ├── pin_config.hh      ← GPIO 初期化（生成）
        ├── platform.hh        ← HAL 型結合（生成）
        └── memory.ld          ← メモリマップ（生成）
```

#### xmake.lua

```lua
add_rules("mode.debug", "mode.release")

-- UMI ライブラリを依存として追加
add_requires("umiport", "umirtm", "umidevice")

target("my_app")
    set_kind("binary")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "stm32f4-disco")  -- 既存ボードを指定
    add_packages("umiport", "umirtm", "umidevice")
    add_files("src/*.cc")
```

#### ヘッダ生成によるデフォルト値の可視化

`xmake` を実行するだけで、選択したボードの全設定値が C++ constexpr として `build/generated/board.hh` に出力される。IDE の補完やジャンプ機能でそのまま確認できる。

```cpp
// build/generated/board.hh（自動生成）
namespace umi::board {
static constexpr uint32_t hse_frequency = 8'000'000;   // board.lua の clock.hse
static constexpr uint32_t system_clock  = 168'000'000; // board.lua の clock.sysclk
static constexpr uint32_t apb1_clock    = 42'000'000;  // MCU 定義 + PLL 計算結果

namespace pin::console {
static constexpr auto tx = GpioPin{ .port = 'A', .pin = 9, .af = 7 };
static constexpr auto rx = GpioPin{ .port = 'A', .pin = 10, .af = 7 };
}
} // namespace umi::board
```

**この設計の利点:**
- 設定を確認するために board.lua や MCU 定義ファイルを読む必要がない
- 生成されたヘッダがそのまま「ドキュメント」として機能する
- MCU 定義から導出された値（バスクロック、PLL 係数）もすべて可視化される
- `constexpr` なので、アプリケーションコードからも型安全にアクセスできる

他のフレームワークとの比較:

| フレームワーク | 設定値の確認方法 | 可視性 |
|--------------|-----------------|--------|
| **Zephyr** | `menuconfig` / DTS ファイルを手動で追う | 低い（DTS の include チェーンが深い） |
| **ESP-IDF** | `menuconfig` / `sdkconfig` テキスト検索 | 中程度（フラットだが巨大） |
| **Arduino** | `boards.txt` のプロパティ一覧 | 低い（書式が特殊、構造がない） |
| **modm** | 生成された C++ ヘッダ | **高い**（UMI と同等のアプローチ） |
| **UMI** | 生成された constexpr ヘッダ + IDE 補完 | **高い**（ビルドするだけで全値が見える） |

modm も同様にヘッダを生成するが、UMI は xmake の `dofile()` による Lua 統合と constexpr の組み合わせにより、ビルドシステムと C++ コードの間でシームレスに値が流れる点が特徴的である。

#### ソースコード例

```cpp
// src/my_synth.hh — Processor 実装（全プラットフォーム共通）
#pragma once
#include <umi/processor.hh>

class MySynth {
public:
    void process(umi::AudioContext& ctx) {
        // オーディオ処理（リアルタイム安全）
        for (auto& event : ctx.input_events()) {
            // MIDI イベント処理
        }
        auto& out = ctx.output(0);
        for (size_t i = 0; i < out.size(); ++i) {
            out[i] = /* DSP 計算 */;
        }
    }
};
```

```cpp
// src/main.cc — エントリポイント（ターゲット固有）
#include "my_synth.hh"
#include <umiport/board/stm32f4-disco/platform.hh>

int main() {
    umi::port::Platform::init();
    MySynth synth;
    // ... カーネル起動、オーディオコールバック登録
}
```

#### ビルドと実行

```bash
xmake                              # ビルド（board.hh 等も自動生成）
xmake flash -t my_app              # フラッシュ書き込み

# 生成ファイルで設定値を確認
cat build/generated/board.hh       # 全設定値が constexpr で見える
```

---

### 3.2 パターン B: カスタム BSP を書く

自作ボードや、umiport に同梱されていないボードで UMI を使う場合。`boards/` ディレクトリにボード定義を追加する。

#### 3.2.1 既存ボードの派生（extends）

既存ボードと同じ MCU で、一部の設定（水晶、ピン配置）だけが異なる場合:

```
my-project/
├── xmake.lua
├── boards/
│   └── my-custom-board/
│       └── board.lua              ← 差分のみ記述
└── src/
    ├── main.cc
    └── my_synth.hh
```

```lua
-- boards/my-custom-board/board.lua
return {
    extends = "stm32f4-disco",         -- 既存ボードをベースに
    description = "My custom STM32F4 board",

    -- 変更点だけ記述（残りは親から継承）
    clock = {
        hse = 25000000,                -- 水晶が 25 MHz に変更
    },
    pin = {
        console = {
            tx = { port = "B", pin = 6, af = 7 },  -- USART1_TX を PB6 に変更
            rx = { port = "B", pin = 7, af = 7 },
        },
    },
    debug = {
        probe = "jlink",               -- ST-Link ではなく J-Link を使用
    },
}
```

```lua
-- xmake.lua
target("my_app")
    set_kind("binary")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "my-custom-board")  -- カスタムボードを指定
    add_files("src/*.cc")
```

**ポイント:**
- `extends` で親ボードの全設定を継承し、差分のみ上書きする
- 親ボードと同じ MCU なので、MCU 定義やスタートアップコードは共有される
- ビルドシステムは `<project>/boards/` を優先検索するため、umiport 内のボードと名前が衝突しない
- 生成される `board.hh` にはマージ後の最終値が出力されるので、継承の結果を容易に確認できる

#### 3.2.2 新規ボード定義（フルスクラッチ）

完全に新しいボード（異なる MCU ファミリー含む）を定義する場合:

```
my-project/
├── xmake.lua
├── boards/
│   └── my-nrf-board/
│       ├── board.lua              ← ボード定義
│       └── platform.hh           ← (platform.custom=true の場合のみ)
└── src/
    └── main.cc
```

```lua
-- boards/my-nrf-board/board.lua
return {
    schema_version = 1,
    name = "my-nrf-board",
    description = "Custom nRF52840 board with audio codec",
    mcu = "nrf52840",                  -- MCU 定義ファイルを参照

    clock = {
        hfclk = "external",           -- HFXO 使用
        lfclk = "xtal",               -- 外部 32.768 kHz 水晶
    },

    pin = {
        console = {
            tx = { port = 0, pin = 6 },    -- nRF は PSEL レジスタ方式
            rx = { port = 0, pin = 8 },
        },
        audio = {
            i2c_sda = { port = 0, pin = 26 },
            i2c_scl = { port = 0, pin = 27 },
        },
    },

    peripherals = { "UARTE0", "TWIM0", "I2S" },

    debug = {
        probe = "jlink",
        rtt = true,
    },

    platform = {
        output = "rtt",
        timer = "systick",
    },

    -- ファミリー固有セクション
    nrf = {
        softdevice = "s140",
        softdevice_flash = 0x27000,
        softdevice_ram = 0x2800,
    },
}
```

**ポイント:**
- `extends` を使わず、全フィールドを自分で定義する
- MCU 定義ファイル（`database/mcu/nrf52840.lua`）が umiport に存在する必要がある
- ファミリー固有セクション（`nrf = { ... }`）で SoftDevice 等の特殊要件を宣言
- `platform.custom = true` を設定すれば、`platform.hh` の自動生成をスキップして手書きのものを使える

#### 3.2.3 platform.hh の手書きが必要なケース

標準的なボードでは `platform.hh` は board.lua の `platform.output` / `platform.timer` から自動生成される。しかし、以下のケースでは手書きが必要:

- 外部デバイスの電源投入シーケンスに順序制約がある
- 複数ペリフェラルの初期化に特殊な手順がある
- HAL concept を満たすカスタムドライバが必要

```lua
-- board.lua
return {
    -- ...
    platform = {
        custom = true,  -- 自動生成をスキップ
    },
}
```

```cpp
// boards/my-custom-board/platform.hh（手書き）
#pragma once
#include <umihal/concept/platform.hh>
#include <umiport/mcu/stm32f4/uart_output.hh>
#include <umiport/arm/cortex-m/dwt.hh>
#include <umidevice/audio/pcm3060/pcm3060.hh>

namespace umi::port {
struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using Codec  = umi::device::PCM3060Driver<stm32f4::I2C1>;

    static void init() {
        Output::init();
        // PCM3060 は RESET ピンを Low→High にしてから 1ms 待つ必要がある
        GpioD::set_low(Pin::codec_reset);
        delay_ms(1);
        GpioD::set_high(Pin::codec_reset);
        delay_ms(1);
    }
};
static_assert(umi::hal::PlatformFull<Platform>);
} // namespace umi::port
```

---

## 4. マルチターゲット構成

同一の Processor 実装を複数ターゲットに同時にビルドする構成。UMI の「One-source multi-target」の典型例。

```
my-project/
├── xmake.lua
├── boards/
│   └── my-custom-board/
│       └── board.lua
├── src/
│   ├── my_synth.hh            ← Processor 実装（共通）
│   ├── main_embedded.cc       ← 組込みエントリポイント
│   └── main_wasm.cc           ← WASM エントリポイント
└── web/
    └── index.html             ← Web UI
```

```lua
-- xmake.lua
add_rules("mode.debug", "mode.release")

-- 組込みターゲット
target("firmware")
    set_kind("binary")
    set_group("firmware")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "my-custom-board")
    add_files("src/main_embedded.cc")

-- WASM ターゲット
target("wasm_synth")
    set_kind("binary")
    set_group("wasm")
    set_toolchains("emcc")
    add_files("src/main_wasm.cc")
    add_ldflags("-sWASM=1", "-sALLOW_MEMORY_GROWTH=0", { force = true })

-- ホストテスト
target("test_synth")
    set_kind("binary")
    set_group("test")
    add_files("src/test_synth.cc")
    add_deps("umitest")
```

**ポイント:**
- `my_synth.hh` は全ターゲットで共有される（`#include` するだけ）
- プラットフォーム依存は `main_*.cc` に閉じ込める
- `xmake build firmware` / `xmake build wasm_synth` / `xmake test` で個別ビルド可能

---

## 5. カーネル + アプリケーション構成

UMI の組込みターゲットでは、**カーネル**（OS 機能 + デバイスドライバ）と**アプリケーション**（DSP 処理）を分離するアーキテクチャを採用している。

```
my-project/
├── xmake.lua
├── kernel/
│   ├── xmake.lua              ← カーネルターゲット
│   ├── kernel.ld              ← カスタムリンカスクリプト
│   └── src/
│       ├── main.cc            ← カーネルエントリ
│       ├── kernel.cc          ← タスクスケジューリング、ISR
│       ├── arch.cc            ← アーキテクチャ固有（IRQ, MPU）
│       ├── mcu.cc             ← MCU 初期化（クロック, ペリフェラル）
│       └── bsp.hh             ← ボード固有型結合
├── app/
│   ├── xmake.lua              ← アプリケーションターゲット
│   └── src/
│       ├── main.cc            ← アプリエントリ
│       └── my_synth.hh        ← Processor 実装
└── boards/
    └── my-board/
        └── board.lua
```

| 層 | 責務 | Flash 配置 |
|----|------|-----------|
| **カーネル** | デバイス初期化、オーディオ ISR、タスク管理 | `0x08000000` (Flash 先頭) |
| **アプリケーション** | DSP 処理（Processor 実装） | `0x08060000` (Flash 後半) |

```bash
xmake flash -t kernel                      # カーネルをフラッシュ
xmake flash -t app -a 0x08060000           # アプリを別アドレスにフラッシュ
```

**利点:**
- カーネルを変更せずにアプリだけ更新できる（開発イテレーション高速化）
- カーネルは安定版を固定し、アプリだけ頻繁に更新する運用が可能
- アプリは `.umia` 形式（ヘッダ付きバイナリ）で配布可能

---

## 6. ボード定義の検索順序

ビルドシステムは以下の順序でボード定義を検索する:

1. `<project>/boards/<name>/board.lua` — プロジェクトローカル（最優先）
2. `umiport/boards/<name>/board.lua` — umiport 同梱

プロジェクトローカルのボード定義が umiport 同梱のボードと同名の場合、プロジェクトローカルが優先される。これにより、umiport のボード定義をフォークせずにプロジェクト固有のカスタマイズが可能。

---

## 7. 新規ボード追加の手順

### 7.1 既存 MCU ファミリー + 新規ボード

MCU が umiport のデータベースに既に登録されている場合:

1. `boards/<name>/board.lua` を作成
2. `xmake.lua` で `set_values("umiport.board", "<name>")` を設定
3. `xmake` でビルド → `board.hh` が生成され、設定値を確認
4. バリデーションエラーがあれば修正

**触るファイル: board.lua の 1 ファイルのみ。**

### 7.2 新規 MCU + 新規ボード

MCU が umiport のデータベースに未登録の場合:

1. `database/mcu/<family>/<device>.lua` に MCU 定義を追加
2. 必要なら `database/family/<family>.lua` にファミリー共通定義を追加
3. `include/umiport/mcu/<family>/` に MCU 固有ドライバを実装
4. `src/<family>/` にスタートアップコードと sections.ld を追加
5. `boards/<name>/board.lua` を作成
6. ビルドして動作確認

**MCU 定義は一度作れば、同じ MCU を使う全ボードで再利用される。**

---

## 8. 未解決の論点

### 8.1 プロジェクトテンプレート / スキャフォールディング

`xmake create` 的なコマンドでプロジェクトの雛形を生成する仕組み。ボード選択を対話的に行い、必要なファイルを自動生成する。

```bash
# 案: コマンド例
umi new my-project --board stm32f4-disco
umi new my-project --board custom --mcu stm32f407vg
```

### 8.2 外部プロジェクトからの UMI 利用方法

UMI を xmake パッケージとして `add_requires("umi")` で取得する方法。現在は git submodule または直接 includes で使用しているが、パッケージ化により利用が簡単になる。

### 8.3 WASM ビルドの標準化

現在の WASM ビルドは `examples/headless_webhost` 内で個別に設定しているが、`embedded` ルールに相当する `wasm` ルールの標準化が望ましい。

```lua
-- 案: wasm ルールによる簡素化
target("my_wasm")
    set_kind("binary")
    add_rules("umi.wasm")        -- WASM 向け標準ルール
    add_files("src/main_wasm.cc")
```
