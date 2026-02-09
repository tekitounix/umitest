# 現状の問題点

本文書は、設計議論で特定された全ての問題を単一の問題文として整理したものである。
ソース資料は以下の4文書であり、7つのAIレビュー（ChatGPT, Gemini, Kimi, Claude Web, Opus 4.6, Claude Code, Kilo）の横断分析を含む。

| ソース文書 | 内容 |
|-----------|------|
| `../archive/review/ARCHITECTURE.md` | 元のアーキテクチャ設計（構造的課題を含む） |
| `../archive/review/compare.md` | 7レビューの横断比較（コンセンサス問題の抽出） |
| `../archive/HAL_INTERFACE_ANALYSIS.md` | HAL Concept設計の詳細分析 |
| `../archive/BSP_ANALYSIS.md` | BSPアーキテクチャのパターン・問題分析 |

補助資料:

| ソース文書 | 内容 |
|-----------|------|
| `../archive/XMAKE_RULE_ORDERING.md` | xmakeルールのライフサイクル順序問題 |
| `../archive/HARDWARE_DATA_CONSOLIDATION.md` | ハードウェアデータの二重管理問題 |
| `../archive/review/review_claudecode.md` | 実装コードとの突き合わせレビュー |

---

## 1. パッケージ構造の問題

### 1.1 パッケージ爆発リスク

**問題:** ARCHITECTURE.md の設計は、MCUシリーズごとに独立パッケージを作る方針を採っている。
5つのMCUファミリを追加するだけで10以上のパッケージが必要になる。

```
umiport-arch         (アーキテクチャ層)
umiport-stm32        (STM32ファミリ共通)
umiport-stm32f4      (STM32F4シリーズ)
umiport-stm32h7      (STM32H7シリーズ)
umiport-stm32f1      (STM32F1シリーズ)
umiport-nrf52        (Nordic nRF52)
umiport-rp2040       (RP2040)
...
```

**影響:** パッケージ数がMCUシリーズの数に比例して増加する。各パッケージに xmake.lua, include ディレクトリ, 依存関係定義が必要であり、管理コストが線形に増加する。

**検出元:** compare.md 問題点マトリクス — 4/7レビュー（Claude Web, Opus 4.6, Claude Code, Kilo）が指摘。Opus 4.6 は umiport 内部の3層サブディレクトリによる集約案を提案し、Kilo がこれを採用。

### 1.2 umiport と umiport-boards の偽りの分離

**問題:** ARCHITECTURE.md では `umiport`（共通インフラ）と `umiport-boards`（ボード定義）を別パッケージとして定義している。しかし実際の使用では、umiport-boards は umiport なしに使われることがなく、umiport もボード定義なしでは機能しない。常に一緒に使われる2つのパッケージを分離する意味がない。

**影響:** 新ボード追加時に2つのパッケージを跨いで編集する必要がある。依存関係の宣言が冗長になる（`add_deps("umiport", "umiport-boards")` が常にペアで出現）。

**検出元:** compare.md の Claude Web 指摘（umiport-boards単一パッケージ問題）。Claude Code レビュー（review_claudecode.md）で実コードの依存パターンを検証して確認。

### 1.3 startup.cc の配置矛盾

**問題:** umiport は ARCHITECTURE.md で「MCU固有コード禁止」と定義されている。しかし `umiport/src/stm32f4/startup.cc` は Cortex-M ベクタテーブル、FPU CPACR レジスタ（`0xE000ED88`）操作を含む完全にMCU固有のコードである。`linker.ld` も STM32F407VG 固有のメモリレイアウト（FLASH 1M, RAM 128K）をハードコードしている。

```cpp
// umiport/src/stm32f4/startup.cc — Cortex-M4 固有コード
__attribute__((section(".isr_vector"), used))
const std::array<const void*, 16> g_vector_table = { ... };  // Cortex-M ベクタテーブル
```

**影響:** パッケージの責務定義と実装が矛盾している。MCU/アーキテクチャを追加するたびに「MCU固有コード禁止」の umiport に MCU 固有コードが増え続ける構造矛盾。RISC-V のような別アーキテクチャを追加する際、startup.cc の FPU 有効化やベクタテーブル構造が完全に Cortex-M 前提であるため、アーキテクチャ分離なしには対応不可能。

**検出元:** compare.md — **6/7レビューが指摘**（最多一致問題）。review_claudecode.md で具体的なコード行を特定。

### 1.4 ドキュメントと実態の乖離

**問題:** ARCHITECTURE.md は以下のパッケージが存在するかのように記述しているが、いずれも実装されていない。

| ARCHITECTURE.md の記載 | 実態 |
|------------------------|------|
| `umiport-arch/` | 存在しない |
| `umiport-stm32/` | 存在しない |
| `umiport-stm32f4/` | 存在しない |
| `umiport-stm32h7/` | 存在しない |

「将来ビジョン」と「現在の実装」が区別なく混在しており、読み手に「何が動いていて何が未実装か」が分からない。

**影響:** 新規参加者がドキュメントを信じてコードを探すが見つからない。ドキュメントへの信頼が失われ、結果として文書化の価値自体が毀損される。

**検出元:** compare.md — 4/7レビュー（Claude Web, Opus 4.6, Claude Code, Kilo）が指摘。特に Opus 4.6 は「実コードとの照合が最も精密」と評価され、乖離を網羅的に検出した。

---

## 2. 出力経路の固定化

### 2.1 `_write()` newlib syscall 依存

**問題:** ライブラリ（umirtm等）の出力が `_write()` newlib syscall を経由してボードの Output に到達する設計は、newlib が存在する環境（Cortex-M + arm-none-eabi-gcc）でしか機能しない。

```
rt::println("Hello {}", 42)
    -> rt::printf(fmt, args...)        <- umirtm（HW 非依存）
    -> ::_write(1, buf, len)           <- newlib syscall（umiport/src/*/syscalls.cc）
    -> umi::port::Platform::Output::putc(c)
```

`_write()` はnewlibのsyscallスタブであり、以下の環境では使えない。

| 環境 | 問題 |
|------|------|
| WASM | newlib が存在しない。`_write()` シンボルが未定義 |
| ESP-IDF | ESP-IDF の VFS (Virtual File System) が `_write()` を独自に定義しており衝突する |
| ホストプラグイン | ホストOSの libc の `_write()` と競合する |
| RISC-V (非newlib) | picolibc など newlib 以外のCライブラリでは `_write()` のシグネチャが異なる |

**影響:** マルチプラットフォーム展開（UMIの最大の差別化要因）の最大障壁。WASM ターゲットやデスクトッププラグインへのワンソース展開が、出力経路の設計で阻害される。

**検出元:** compare.md — **5/7レビュー**が指摘（ChatGPT, Claude Web, Opus 4.6, Claude Code, Kilo）。深刻度ランキング第2位。ChatGPT は weak symbol による sink 差し替え案、Claude Web / Kilo は link-time 注入（`extern void write_bytes()`）を提案。

### 2.2 ESP-IDF VFS 衝突

**問題:** ESP-IDF は `_write()` を VFS (Virtual File System) レイヤーで定義する。umiport の `syscalls.cc` が同じシンボルを定義すると多重定義エラーになる。ESP-IDF の VFS を無効化すればビルドは通るが、ESP-IDF の SPIFFS/FAT ファイルシステム機能が使えなくなる。

**影響:** ESP32 ファミリへの対応が出力経路の設計レベルで阻害される。

**検出元:** compare.md の ChatGPT レビューおよび Claude Web レビューで ESP-IDF の VFS との衝突が言及。

### 2.3 出力抽象層の不在

**問題:** ライブラリと実際の出力デバイスの間に、明示的な抽象レイヤーが存在しない。`_write()` syscall は newlib の実装詳細であり、設計されたインターフェースではない。ライブラリが「出力する」という操作を、特定のCランタイムのsyscallメカニズムに暗黙的に依存している。

**影響:** 出力先の差し替え（UART, RTT, USB CDC, ネットワーク等）がC言語のsyscallスタブの書き換えでしか実現できず、C++の型安全な仕組みを活用できない。テスト時のモック差し替えも困難。

**検出元:** compare.md のコンセンサス分析。7レビューの最大公約数として「`rt::detail::write_bytes()` の link-time 注入」が推奨された。

---

## 3. HAL Concept 設計の課題

### 3.1 モノリシック Concept

**問題:** 一部の concept が巨大すぎる。1つの concept に全機能を詰め込んでいる。

| Concept | メソッド数 | 対比: embedded-hal 相当 |
|---------|-----------|----------------------|
| `Uart` (uart.hh) | 18メソッド | embedded-hal の I2c: 1メソッド (`transaction`) |
| `Timer` | 18メソッド | embedded-hal の InputPin: 2メソッド |
| `GpioPin` | 9メソッド | embedded-hal の OutputPin: 2メソッド |

`Uart` concept の要求式リスト（uart.hh）:
`init`, `deinit`, `write_byte`, `read_byte`, `write`, `read`, `write_with_timeout`, `read_with_timeout`, `write_async`, `read_async`, `is_readable`, `is_writable`, `flush_tx`, `flush_rx`, `get_error`, `clear_error` 等。

**影響:** 実装負荷が高い。単純なポーリングUART実装でも、非同期メソッド（`write_async`, `read_async`）のスタブ実装が必要になる。

**検出元:** HAL_INTERFACE_ANALYSIS.md Section 6.3 問題2。compare.md — 5/7レビューが指摘。

### 3.2 「NOT_SUPPORTED は許容」エスケープハッチ

**問題:** 巨大 concept の結果、全メソッドを実装できない場合に「`NOT_SUPPORTED` を返してもよい」という逃げ道が暗黙的に許容されている。これは concept の哲学（コンパイル時に契約を保証する）を根本的に毀損する。

```cpp
// 意味的に壊れた例: concept を満たすが、機能しない
Result<void> write_async(std::span<const uint8_t> data, Callback cb) {
    return Result<void>{ErrorCode::NOT_SUPPORTED};  // 常に失敗
}
```

**影響:** concept 制約を満たす型が実際にはそのメソッドをサポートしない可能性があるため、型安全性の意義が失われる。ドライバ作者が「この Uart は非同期をサポートするか？」を型で判別できない。

**検出元:** HAL_INTERFACE_ANALYSIS.md Section 6.3 問題2。compare.md で 5/7 レビューが「HAL Concept の意味論不足」として指摘。

### 3.3 GPIO 入出力の型未分離

**問題:** `GpioPin` concept が `set_direction(Direction)` + `write(State)` + `read()` を全て含んでおり、入力ピンと出力ピンが型レベルで区別されない。

世界の主要HALが独立に「入出力は別型」に到達している:

| HAL | 分離方式 |
|-----|---------|
| Rust embedded-hal | 別trait: `InputPin` / `OutputPin` |
| Mbed OS | 別クラス: `DigitalIn` / `DigitalOut` |
| umihal（現在） | **統合concept: `GpioPin`** |

**影響:** 「出力ピンを読む」「入力ピンに書く」というバグがコンパイル時に検出されない。C++20 concepts で表現可能な型安全性を活用していない。

**検出元:** HAL_INTERFACE_ANALYSIS.md Section 6.3 問題3、Section 3.1 GPIO横断比較。compare.md で Opus 4.6 がConcept分割を提案。

### 3.4 同期・非同期の未分離

**問題:** blocking メソッドと async メソッドが同一 concept に同居している。

```cpp
// i2c.hh — 現状: ブロッキングと非同期が混在
concept I2cMaster = requires(T& i2c, ...) {
    { i2c.write(address, tx_data) } -> std::same_as<Result<void>>;       // blocking
    { i2c.write_async(address, tx_data, callback) } -> std::same_as<Result<void>>;  // async
};
```

これはUMIのリアルタイムオーディオ制約と直接衝突する。`process()` コールバック（リアルタイムコンテキスト）からブロッキング関数を呼ぶとオーディオグリッチが発生するが、concept のシグネチャからは「この関数はブロックするのか？」が読めない。

I2S ではさらに深刻で、3つの異なる実行モデルが1つの concept に同居している:

```cpp
concept I2sMaster = requires(T& i2s, ...) {
    { i2s.transmit(tx_data) }                            // blocking
    { i2s.transmit_async(tx_data, callback) }            // async (DMA)
    { i2s.start_continuous_transmit(tx_data, buf_cb) }   // continuous (streaming)
};
```

実際のオーディオシステムが使うのは continuous のみ。にもかかわらず全実装が blocking/async のスタブも強制される。

**影響:** リアルタイム違反の見逃し（型で防げない）、バッファ寿命の事故（async のバッファ所有権がシグネチャから不明）、不要なスタブ実装の強制。

**検出元:** HAL_INTERFACE_ANALYSIS.md Section 5（実行モデルの分離に関する詳細分析）。compare.md — 5/7レビューが指摘。Rust embedded-hal が blocking/async/nb を別crateに完全分離しているのが対照的。

### 3.5 欠落 Concept

**問題:** 設計上必要だが未定義の concept が存在する。

| 欠落 Concept | 必要な理由 | 指摘元 |
|-------------|-----------|--------|
| `ClockTree` | クロック設定の統一的抽象化。PLL設定はMCUごとに異なるが、「目標周波数を指定して設定」という操作は共通 | Kimi + Kilo (compare.md) |
| `Platform` (concept版) | ボード定義が concept を満たすことをコンパイル時に検証する。`static_assert(Platform<MyBoard>)` | Claude Web (compare.md) |
| `SpiBus` | SPI バス自体の concept。`SpiTransport` はドライバ向け視点で存在するが、HALレベルのSPIバス concept がない | HAL_INTERFACE_ANALYSIS.md Section 6.3 問題5 |

**影響:** ClockTree 不在により各ボードのクロック初期化がアドホックな実装になる。Platform concept 不在により新ボード追加時に「何を実装すべきか」がコンパイラエラーで示されない。

**検出元:** compare.md (Kimi: ClockTree, Claude Web: Platform), HAL_INTERFACE_ANALYSIS.md (SpiBus)。

### 3.6 Concept 移行の不完全さ

**問題:** 旧来のフラットな巨大 concept ファイル（`uart.hh`, `gpio.hh` 等）と、新しい階層的 concept（`concept/codec.hh`, `concept/uart.hh`）が混在している。

```
umihal/include/umihal/
├── uart.hh           <- 旧: 18メソッドの巨大 Uart concept
├── gpio.hh           <- 旧: 入出力未分離の GpioPin concept
├── concept/
│   ├── codec.hh      <- 新: CodecBasic -> CodecWithVolume -> AudioCodec (模範的)
│   ├── uart.hh       <- 新: UartBasic / UartAsync (正しい階層)
│   └── platform.hh   <- 新: OutputDevice, Platform
```

`concept/` ディレクトリ内の設計パターンが正解であるにもかかわらず、全面展開されていない。`concept/codec.hh` の `CodecBasic -> CodecWithVolume -> AudioCodec` が模範パターンだが、これが他の concept に横展開されていない。

**影響:** どの concept 定義を使うべきかが不明確。旧 `uart.hh` と新 `concept/uart.hh` のどちらが正なのかがコードからは分からない。

**検出元:** HAL_INTERFACE_ANALYSIS.md Section 6.3 問題1（二重定義）, 問題6（移行不完全）。

---

## 4. ボード定義・データ管理の問題

### 4.1 ハードウェアデータの二重管理

**問題:** ハードウェアに関する情報が、ビルドシステムのパッケージ（arm-embedded）とプロジェクト内ライブラリ（umiport）に分裂している。

```
arm-embedded パッケージ (xmake-repo/synthernet/)
├── database/mcu-database.json      <- MCU メモリサイズ・アドレス
├── plugins/flash/database/flash-targets.json  <- PyOCD デバイスパック
└── linker/common.ld                <- 汎用リンカスクリプト

umiport (lib/umiport/)
├── src/stm32f4/linker.ld           <- MCU 固有リンカスクリプト
└── include/umiport/board/*/        <- ボード定義
```

具体的な二重管理の例:

| データ | arm-embedded 側 | umiport 側 |
|--------|-----------------|-----------|
| Flash/RAMサイズ | `mcu-database.json` の `"flash": "1M"` | `linker.ld` の `FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1M` |
| リンカシンボル名 | `common.ld` は `_data_start` / `_data_end` | umiport の `linker.ld` は `_sdata` / `_edata` |
| 逆参照 | `mcu-database.json` が `"renode_repl": "lib/umiport/renode/..."` とプロジェクトパスを参照 | -- |

**影響:** 新MCU追加時に `mcu-database.json`（パッケージ内）と `umiport/src/`（プロジェクト内）の**両方**を変更し、`xmake dev-sync` でパッケージを同期する必要がある。データの重複により不整合が発生しやすい。シンボル名の不一致はリンクエラーの原因になる。

**検出元:** HARDWARE_DATA_CONSOLIDATION.md Section 1。BSP_ANALYSIS.md Section 3.4（三重管理負担パターン）。compare.md のドキュメント乖離指摘と関連。

### 4.2 ボード継承メカニズムの不在

**問題:** ボード定義に継承の仕組みがない。各ボードが完全に独立しており、同一MCUでも全設定を個別に記述する必要がある。

BSP_ANALYSIS.md の調査では、3つの全ユースケースで良好な評価を得るシステム（modm, Mbed OS, Zephyr）は全て継承メカニズムを持つ。一方、継承のないシステム（PlatformIO, Arduino）はカスタムボード対応（UC2）で致命的な欠陥を持つ。

```
開発ボード (STM32F4-Discovery) で動作確認
    -> カスタム基板に移行
    -> HSE周波数が異なる、ピン割り当てが異なる、デバッグプローブが異なる
    -> 全設定をコピーして手動変更 (Fork-and-Modify アンチパターン)
```

**影響:** 「開発ボードから量産基板へ」という最も頻繁なユースケースで、上流BSPの変更を取り込めなくなるFork-and-Modify問題が発生する。

**検出元:** BSP_ANALYSIS.md Section 3.1（80%完成問題）, Section 3.2（Fork-and-Modify アンチパターン）。

### 4.3 memory.ld 手書き問題

**問題:** リンカスクリプトのメモリ定義が手書きであり、Lua データベース（将来設計）や JSON データベース（現在の `mcu-database.json`）と二重管理になっている。MCUバリアント（STM32F401: 256K Flash vs STM32F407: 1M Flash）のメモリサイズが、JSONとリンカスクリプトの両方に記述されている。

```
// mcu-database.json
"stm32f407vg": { "flash": "1M", "ram": "128K" }

// linker.ld (手書き — 同じ情報の重複)
MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1M
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}
```

成熟したシステム（modm: 4,557デバイスを階層DBで管理、Zephyr: DeviceTreeから生成）はメモリ定義を**生成**している。手書きは2リージョン（Flash+RAM）程度なら問題ないが、STM32H7の8リージョン（FLASH + DTCM + AXI_SRAM + SRAM1 + SRAM2 + SRAM4 + ITCM + BACKUP）では手書きの保守性が急速に劣化する。

**影響:** MCUバリアント追加のたびに手書きリンカスクリプトが必要。メモリサイズの転記ミスがリンクエラーやランタイムクラッシュの原因になる。

**検出元:** HARDWARE_DATA_CONSOLIDATION.md Section 1, Section 8。BSP_ANALYSIS.md Section 3.4（modmやlibopencm3との比較）。

### 4.4 同名ヘッダ戦略のIDE混乱

**問題:** `platform.hh` という同名ファイルが複数のディレクトリに存在し、xmake の includedirs で切り替える方式は、IDE（特にclangd）が正しい定義にジャンプできない問題を引き起こす。

さらに、umibenchでは `umi::port::Platform`（起動・syscall用）と `umi::bench::Platform`（ベンチマーク用）という**2つの全く異なる型**が `platform.hh` という同名ファイルで定義されている。

| ファイル | 定義する型 | 目的 |
|---------|-----------|------|
| `umiport/board/stm32f4-renode/platform.hh` | `umi::port::Platform` | 起動・syscall用 |
| `umibench/tests/stm32f4-renode/umibench/platform.hh` | `umi::bench::Platform` | ベンチマーク用 |

**影響:** clangd が `#include "platform.hh"` の解決先を特定できない。ビルドエラーの原因追跡が困難（どの platform.hh が使われているか分からない）。新ボード追加時に umibench 側にも別の platform.hh を作る必要があるが、この暗黙知は文書化されていない。

**検出元:** compare.md — 5/7レビューが指摘（ChatGPT, Claude Web, Opus 4.6, Claude Code, Kilo）。review_claudecode.md で umibench の二重 Platform 問題が詳細に分析された。

---

## 5. ビルドシステムの制約

### 5.1 xmake ルールのライフサイクル順序問題

**問題:** `umiport.board` ルールが `on_config` フェーズで設定した値を、`embedded` ルールの `on_load` フェーズが読み取れない。xmake のビルドフェーズ順序は以下の通りであり、遡及しない。

```
Phase 0: ターゲット定義パース (set_values等が即時評価)
Phase 1: on_load()   <- embedded ルールがここでリンカスクリプトを読む
Phase 2: after_load()
Phase 3: on_config() <- umiport.board がここで値を設定する（手遅れ）
Phase 4: before_build() -> コンパイル -> after_link()
```

```
期待した動作:
  umiport.board が embedded.linker_script を設定 -> embedded がその値を使用

実際の動作:
  embedded:on_load() が linker_script=nil を読む -> common.ld をフォールバック適用
  umiport.board:on_config() が値を設定 -> 誰も読まない（手遅れ）
```

現在の回避策は ldflags のフィルタリングによる書き換え:

```lua
-- on_config 内で embedded が設定した -T フラグを除去して差し替え
local old_flags = target:get("ldflags") or {}
local new_flags = {}
for _, flag in ipairs(old_flags) do
    if not flag:find("^%-T") then
        table.insert(new_flags, flag)
    end
end
target:set("ldflags", new_flags)
target:add("ldflags", "-T" .. ld_path, {force = true})
```

**影響:** embedded ルールの ldflags 内部構造に依存する脆い実装。embedded ルールが `-T` フラグの形式を変更すると壊れる。「なぜフィルタが必要なのか」がコードから自明でない。

**検出元:** XMAKE_RULE_ORDERING.md（全文がこの問題の分析）。

### 5.2 ルール間依存宣言の不在

**問題:** xmake にはルール間の依存関係を宣言するメカニズムが存在しない。ターゲットには `add_deps()` があるが、ルールには相当する機能がない。

```lua
-- ターゲット間: 依存宣言が可能
target("app")
    add_deps("lib")

-- ルール間: 依存宣言が不可能
rule("umiport.board")
    -- add_deps("embedded") は存在しない
```

**影響:** `add_rules("embedded", "umiport.board")` と `add_rules("umiport.board", "embedded")` で on_load の実行順序が変わり、異なる結果になる。ルール宣言順序への依存は設計として不適切だが、xmake の仕様上避けられない。

**検出元:** XMAKE_RULE_ORDERING.md Section 2.3（保証されていないこと）。

### 5.3 ボイラープレートの増殖

**問題:** 各ライブラリの `tests/xmake.lua` に同一のボード設定パターンが繰り返されている。

```lua
-- umirtm/tests/xmake.lua, umibench/tests/xmake.lua,
-- umimmio/tests/xmake.lua, umitest/tests/xmake.lua
-- 全てに以下の同一パターン:
target("xxx_stm32f4_renode")
    set_kind("binary")
    set_default(false)
    add_rules("embedded", "umiport.board")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    set_values("umiport.board", "stm32f4-renode")
    add_files("test_*.cc")
    add_deps("xxx", "umiport")
```

このパターンは現在4つのライブラリで繰り返されており、新しいボード（STM32H7, RP2040等）を追加するたびに**全ライブラリの tests/xmake.lua を手動で修正**する必要がある。

**影響:** ボード数 x ライブラリ数で増加し、スケールしない。1つのボード設定の変更が全ライブラリに波及する。変更漏れが発生しやすい。

**検出元:** review_claudecode.md で実コードの比較から検出。compare.md — Claude Codeのみが指摘した実務的問題だが、xmakeルールベースのボード選択で解決可能。

---

## 問題の深刻度サマリ

compare.md の7レビュー横断分析に基づく、指摘数と深刻度の重み付けランキング:

| 順位 | 問題 | 深刻度 | 指摘数 | 根拠 |
|------|------|--------|:------:|------|
| **1** | startup/syscalls の配置矛盾 (1.3) | 最高 | 6/7 | MCU追加のたびに「MCU固有コード禁止」のumiportが肥大化する構造矛盾 |
| **2** | `_write()` syscall 依存 (2.1) | 最高 | 5/7 | WASM/ESP-IDF/ホストで機能せず、マルチプラットフォーム展開の最大障壁 |
| **3** | HAL Concept の設計不足 (3.1-3.4) | 高 | 5/7 | NOT_SUPPORTED escape hatch, sync/async混在, GPIO入出力未分離 |
| **4** | 同名ヘッダのスケール/IDE問題 (4.4) | 高 | 5/7 | clangd混乱, ビルドエラー暗号化, 二重Platform |
| **5** | ドキュメントと実態の乖離 (1.4) | 高 | 4/7 | 存在しないパッケージを「ある」かのように記述 |
| **6** | パッケージ粒度の過剰分割 (1.1) | 中 | 4/7 | MCU増加時のパッケージ爆発リスク |
| **7** | umiport の責務過多 (1.3) | 中 | 4/7 | 原則と実装の矛盾 |
| **8** | ハードウェアデータの二重管理 (4.1) | 中 | -- | 新MCU追加時の2箇所編集 + dev-sync 必須 |
| **9** | xmake ルール順序問題 (5.1) | 中 | -- | ldflags フィルタによる脆い回避策 |
| **10** | ボイラープレート増殖 (5.3) | 低 | 1/7 | ボード数 x ライブラリ数で線形増加 |

---

## 維持すべき設計原則

全7レビューが一致して高く評価し、「維持すべき」と明示的に記述した4原則（compare.md Section 2）:

| # | 原則 | 支持 |
|---|------|:----:|
| 1 | **ライブラリはHWを知らない**（umirtm/umibench/umimmioの0依存） | 7/7 |
| 2 | **統合はboard層のみで行う** | 7/7 |
| 3 | **`#ifdef` 禁止、同名ヘッダ + includedirs による実装切り替え** | 7/7 |
| 4 | **出力経路の一本化**（ライブラリが出力先を知らない） | 7/7 |

これらは設計の「不変核」であり、問題解決においてもこれらの原則を毀損してはならない。
