# 比較分析

**ステータス:** 完了
**作成日:** 2026-02-09
**前提文書:** ../archive/BSP_ANALYSIS.md, ../archive/PLATFORM_COMPARISON.md, ../archive/HAL_INTERFACE_ANALYSIS.md, ../archive/review/compare.md, ../archive/review/compare_device.md
**目的:** 調査した 13+ のフレームワークとレビュー結果を横断的に比較し、UMI の設計判断の根拠を体系化する

---

## 1. BSP アーキテクチャの類型化

調査した 13 以上のシステムは、以下の 5 つのアーキタイプに分類される。

### 1.1 テンプレート/コード生成型 (Template / Code Generation)

**代表:** modm (lbuild + Jinja2), STM32CubeMX -- デバイス DB を入力としテンプレートエンジンが startup/vectors/linker script を生成する。modm は 4,557 デバイスをカバーする巨大 DB で手書きを極限まで排除。CubeMX は GUI ベースだが再生成でユーザー変更が消える問題がある。

### 1.2 オーバーレイ/デルタ型 (Overlay / Delta)

**代表:** Zephyr (DeviceTree overlay), Yocto -- ベース定義の上に差分を重ねる方式。Zephyr は DTS の 8 階層 include チェーンで段階的詳細化を実現。ベースを変更せずに拡張可能だが、DTS + Kconfig + CMake の三重構成は学習コストが高い。

### 1.3 継承型 (Inheritance)

**代表:** Mbed OS (targets.json `inherits`), CMSIS-Pack (4 階層 XML) -- OOP の継承に類似し、ベースターゲットのプロパティを子が引き継ぐ。Mbed の `_add`/`_remove` 構文は直感的だが targets.json は 10,000 行超に肥大化。CMSIS-Pack は family/subFamily/device/variant で属性を累積。

### 1.4 宣言的フラットファイル型 (Declarative Flat-file)

**代表:** PlatformIO (board.json), Arduino (boards.txt) -- 各ボードをフラットなデータファイルで記述。継承メカニズムなし。JSON 1 ファイルという低い参入障壁を持つが、同一 MCU のボード間でデータが大量重複する。

### 1.5 トレイト/コンセプト型 (Trait / Concept)

**代表:** Rust embedded-hal, Embassy -- ハードウェア抽象をコンパイル時の型制約で定義。PAC -> HAL -> BSP の層分離が最も明確で、vtable なしの静的ポリモーフィズムを実現。ただし BSP カバレッジが低く memory.x は手書き。

### 1.6 アーキタイプ別ユースケース対応表

3 つのユースケースに対する各アーキタイプの対応能力を評価する。

| アーキタイプ | 代表システム | UC1: 既存 BSP 使用 | UC2: BSP 継承/拡張 | UC3: 新規 BSP 作成 |
|------------|------------|:--:|:--:|:--:|
| テンプレート/生成 | modm, CubeMX | ◎ | ◎ (modm) / ○ (CubeMX) | △ |
| オーバーレイ/デルタ | Zephyr, Yocto | ◎ | ◎ | △ |
| 継承 | Mbed OS, CMSIS-Pack | ◎ | ◎ | ○ |
| フラットファイル | PlatformIO, Arduino | ◎ | x | ◎ |
| トレイト/コンセプト | Rust embedded-hal | ○ | ○ | ○ |

**重要な発見:** 3 つ全てのユースケースで ○ 以上を達成するシステムは **modm**, **Mbed OS**, **Zephyr** のみ。共通する特徴は、データの階層的継承メカニズムを持つこと、デバイス/ボードの分離が明確であること、何らかの生成機構を持つことの 3 点である。

---

## 2. HAL インターフェイス設計の比較

10 の主要 HAL を調査した結果、以下の設計軸でアプローチが分岐する。

### 2.1 モノリシック vs 階層的インターフェイス

| 方式 | 採用 HAL | 特徴 |
|------|---------|------|
| **モノリシック** (全機能を 1 インターフェイス) | CMSIS-Driver, 旧 umihal | 1 ドライバに Init/Send/Receive/Async/Status 全メソッド。NOT_SUPPORTED の逃げ道が型安全性を損なう |
| **階層的** (段階的 refinement) | Rust embedded-hal, umihal concept/ | `UartBasic` -> `UartBuffered` -> `UartAsync` のように能力に応じて段階的に精緻化 |
| **関数群分離** | Zephyr | Polling / IRQ-driven / Async(DMA) を同一ペリフェラルの別 API 関数群として提供 |
| **クラス分離** | Mbed OS | `BufferedSerial` / `UnbufferedSerial`、`DigitalIn` / `DigitalOut` を別クラス |

umihal の concept/codec.hh (`CodecBasic` -> `CodecWithVolume` -> `AudioCodec`) は既にこの階層化パターンの模範的実装である。

### 2.2 同期/非同期/DMA の分離戦略

全ての成熟した HAL が「ブロッキング/非同期/DMA は同一インターフェイスに混ぜない」という結論に到達している。

| HAL | 分離方法 | コンテキスト安全性の保証 |
|-----|---------|----------------------|
| **Rust embedded-hal** | 別クレート (embedded-hal / embedded-hal-async / embedded-hal-nb) | 型システム + Send/Sync trait で保証 |
| **Zephyr** | 別 API 関数群 (Polling / IRQ-driven / Async DMA) | ドキュメント + Kconfig の排他制御 |
| **CMSIS-Driver** | 単一 API + SignalEvent callback | コールバック有無で判別（型では非保証） |
| **Mbed OS** | 別クラス (BufferedSerial / UnbufferedSerial) | クラス選択で暗黙的に保証 |
| **ESP-IDF** | 別関数群 | ドキュメントのみ |

分離の本質的理由は「実装の詳細」ではなく「呼び出しコンテキストの制約」にある。ブロッキング関数は process() から呼べず、非同期コールバックは ISR コンテキストで呼ばれるため、型で実行モデルを区別しなければリアルタイム違反を防げない。

### 2.3 エラーハンドリング方式

| 方式 | 採用 HAL | 特徴 |
|------|---------|------|
| `Result<T, E>` (型安全) | Rust embedded-hal, umihal | 全メソッドがフォールブル。エラーの詳細分類は impl 固有 |
| 負の int (POSIX errno) | Zephyr | 0: 成功, 負: エラー。型安全性なし |
| `int32_t` 定数 | CMSIS-Driver | `ARM_DRIVER_ERROR_xxx` 定数群 |
| `esp_err_t` | ESP-IDF | 独自エラーコード体系 |
| エラーなし | Mbed (GPIO), Arduino | 戻り値を返さない |

umihal の `Result<T>` = `std::expected<T, ErrorCode>` は embedded-hal と同等の設計品質であり、この方針を維持すべきである。

### 2.4 型安全性（GPIO の入出力分離）

GPIO の入力と出力を型レベルで区別するのは、Rust embedded-hal と Mbed OS が独立に到達した結論である。

| HAL | 入出力分離 | 実現方式 |
|-----|:--------:|---------|
| Rust embedded-hal | ○ | `InputPin` / `OutputPin` 別 trait |
| Mbed OS | ○ | `DigitalIn` / `DigitalOut` 別クラス |
| Zephyr | x | `gpio_flags_t` のランタイムフラグ |
| CMSIS-Driver | x | 統合 API |
| ESP-IDF | x | config 構造体 |
| umihal (現状) | x | 統合 concept |

コンパイル時に「出力ピンを読む」「入力ピンに書く」というバグを防げるため、umihal も `GpioInput` / `GpioOutput` への分離が推奨される。

### 2.5 トランザクション vs 操作ベース API

| 方式 | 採用 HAL | 特徴 |
|------|---------|------|
| **トランザクションベース** | embedded-hal (I2C `transaction()`, SPI `SpiDevice`) | 任意の操作シーケンスを 1 トランザクションとして表現。REPEATED START を正しく処理 |
| **操作ベース** | Mbed, Zephyr, CMSIS, umihal | `write()` / `read()` を個別メソッドとして提供。複合操作は `write_read()` を別途追加 |

embedded-hal 1.0 は `transaction()` を基本にし `write`/`read` はデフォルト実装にしたが、C++ ではライフタイム管理の複雑さから 3 メソッド方式がより実用的である。

### 2.6 HAL 特性比較マトリクス

| 特性 | embedded-hal | Mbed OS | Zephyr | CMSIS-Driver | ESP-IDF | Arduino |
|------|:-----------:|:------:|:------:|:-----------:|:------:|:------:|
| 多態方式 | trait (静的) | vtable (動的) | 関数テーブル | Access Struct | ハンドル | クラス |
| 実行モデル分離 | 別クレート | 別クラス | 別 API 群 | コールバック | 混在 | なし |
| 初期化 | スコープ外 | コンストラクタ | DeviceTree | Init/Uninit | ハンドル | begin() |
| エラーハンドリング | Result<T,E> | なし/int | int (errno) | int32_t | esp_err_t | なし |
| GPIO 方向型安全 | ○ | ○ | x | x | x | x |
| Bus/Device 分離 | ○ (SPI) | x | x | x | ○ (I2C) | x |
| トランザクション | ○ | x | x | x | x | x |
| vtable オーバーヘッド | なし | あり | あり | あり | なし | なし |

---

## 3. データ管理と継承メカニズムの比較

フレームワークがハードウェアデータをどのように管理し、継承するかを比較する。

### 3.1 データ継承方式の分類

| 方式 | 採用システム | 特徴 |
|------|------------|------|
| **DTS include チェーン** | Zephyr | 8 階層の `#include` でデータ継承。下位が上位のノードを追加/オーバーライド |
| **JSON inherits** | Mbed OS | `inherits` + `_add`/`_remove` で差分管理。10,000 行のモノリシック JSON |
| **XML 4 階層累積** | CMSIS-Pack | family/subFamily/device/variant で属性を段階的に累積 |
| **マージ + フィルタ** | modm | 類似デバイスを 1 ファイルにマージ。`device-*` 属性で差分のみ修飾 |
| **パターンマッチ継承** | libopencm3 | `stm32f407?g*` のようなワイルドカードで親子関係を定義 |
| **Cargo feature flags** | Rust Embedded | `Cargo.toml` の feature で条件付きコンパイル。データ継承ではなくコード選択 |
| **フラットデータ** | PlatformIO, Arduino | 継承メカニズムなし。ボード間でデータ重複 |

### 3.2 生成 vs 手書き

| 生成レベル | 採用システム | 生成対象 |
|-----------|------------|---------|
| **完全生成** | modm | startup, vectors, linker script, レジスタアクセスコード全て |
| **リンカ + セクション生成** | Zephyr, ESP-IDF (ldgen) | リンカスクリプト全体をテンプレートから生成 |
| **メモリ部分のみ生成** | PlatformIO, libopencm3 | FLASH + RAM のメモリ定義部分を生成（PlatformIO は 2 リージョン限定） |
| **手書き** | NuttX, Arduino | ボードごとに個別のリンカスクリプト |

**発見:** データ継承とコード生成は排他的ではない。最も成功しているシステム (modm, Zephyr) は両方を活用している。

### 3.3 データ管理能力の比較表

| システム | 唯一源泉 | 継承 | リンカ生成 | スタートアップ生成 | メモリ定義 |
|---------|:-------:|:----:|:--------:|:--------------:|:--------:|
| **Zephyr** | ○ (DTS) | ◎ (8 階層) | ◎ (テンプレート) | ○ | DTS memory ノード |
| **modm** | ◎ (modm-devices) | ◎ (マージ+フィルタ) | ◎ (テンプレート) | ◎ (テンプレート) | DB から生成 |
| **Mbed OS** | ○ (targets.json) | ◎ (inherits) | ○ (テンプレート+マクロ) | x | JSON 内の値 |
| **CMSIS-Pack** | ◎ (DFP) | ◎ (4 階層) | ○ (Pack 内提供) | ○ | XML memory 要素 |
| **ESP-IDF** | ◎ (soc_caps.h) | △ (パス切替) | ◎ (ldgen) | ○ | memory.ld.in |
| **PlatformIO** | △ (board.json) | x | △ (2 リージョン限定) | x | JSON upload.* |
| **Arduino** | △ (boards.txt) | x | x (手書き) | x | boards.txt upload.* |
| **Rust Embedded** | ○ (各クレート) | x (独立) | x (手書き memory.x) | ○ (cortex-m-rt) | memory.x |
| **libopencm3** | ◎ (devices.data) | ○ (パターンマッチ) | ◎ (genlink) | x | devices.data |
| **NuttX** | △ (Kconfig) | △ (依存関係) | x (手書き) | x | defconfig |

### 3.4 三重管理の実態

同一のハードウェア事実を複数箇所に記述する「三重管理負担」の程度を比較する。

| システム | 管理箇所数 | 箇所 |
|---------|:--------:|------|
| Zephyr | 3 | DTS + Kconfig + CMake |
| ESP-IDF | 4 | soc_caps.h + Kconfig + CMake + ldgen |
| Rust | 2 | memory.x (手書き) + BSP コード内の定数 |
| UMI (旧) | 2 | mcu-database.json + linker.ld |
| **UMI (新設計)** | **1** | **Lua DB のみ (memory.ld は自動生成)** |
| **modm** | **1** | **modm-devices DB のみ (全て自動生成)** |

---

## 4. 共通問題パターン

調査した全システムに横断的に現れる 5 つの問題パターンを分析する。

### 4.1 「80% 完成」問題

開発ボードでは即座に動作するが、カスタムボード（量産基板）に移行すると、HSE 周波数の違い、デバッグプローブの変更、ピン割り当ての変更といった差異で想定外の工数が発生する。

| システム | 深刻度 | 理由 |
|---------|:-----:|------|
| PlatformIO | 高 | board.json に継承なし。全フィールドを手動コピー |
| Arduino | 高 | variant コピーが標準手法 |
| Rust | 中 | memory.x の手書きと BSP の再構築が必要 |
| Zephyr | 中 | DT overlay で差分記述可能だが 5+ ファイル必要 |
| Mbed OS | 低 | `inherits` で差分のみ指定 |
| modm | 低 | `extends` で既存ボードを継承 |

**示唆:** UC2（既存 BSP の継承/拡張）を第一級市民として設計すべき。開発ボードから量産基板への移行は最も頻繁に発生するユースケースである。

### 4.2 Fork-and-Modify アンチパターン

継承メカニズムが存在しない/不十分な場合、既存 BSP をコピーして改変せざるを得ず、以後上流の改善を取り込めなくなる。PlatformIO の board.json コピー、Arduino の variant コピー、NuttX のディレクトリコピーがこれに該当する。Zephyr (DT overlay + BOARD_ROOT)、Mbed OS (`inherits`)、modm (`extends`) はこの問題を構造的に回避している。

### 4.3 設定-コードギャップ

回路図からファームウェア設定への手動変換で不整合が生じやすい問題。CubeMX は GUI 自動生成で、Zephyr は DTS の表現力でギャップを最小化する。UMI の board.hh `constexpr` パターンは物理定数を型システムで検証可能にする良い設計であり、`static_assert` で更に縮小できる。

### 4.4 三重管理負担

同一のハードウェア事実を 3+ 箇所に記述する必要がある問題。Zephyr は DTS + Kconfig + CMake の 3 箇所、ESP-IDF は 4 箇所。modm と UMI (新設計) のみが 1 箇所への集約を達成している。

### 4.5 フラットファイル爆発 vs モノリシックファイル肥大化

フラットファイルは件数が爆発し（PlatformIO: 291 board.json）、モノリシックファイルは肥大化する（Mbed: targets.json 10,000+ 行）。この二律背反はファイル分割 + 継承メカニズムでのみ回避できる。modm と libopencm3 がこの解法を実践している。

---

## 5. 設計レビューの合意事項

7 人の独立したレビュアー（ChatGPT, Gemini, Kimi, Claude Web, Opus 4.6, Claude Code, Kilo）が UMI のアーキテクチャをレビューした結果、以下の合意が得られた。

### 5.1 維持すべき設計原則（7/7 一致）

| 原則 | 支持 |
|------|:----:|
| **ライブラリは HW を知らない** -- umirtm/umibench/umimmio の 0 依存 | 7/7 |
| **統合は board 層のみで行う** | 7/7 |
| **`#ifdef` 禁止、同名ヘッダ + includedirs による実装切り替え** | 7/7 |
| **出力経路の一本化** -- ライブラリが出力先を知らない | 7/7 |

### 5.2 最も深刻な問題（指摘数順）

| 順位 | 問題 | 指摘数 | 内容 |
|:----:|------|:------:|------|
| 1 | **startup/syscalls の配置問題** | 6/7 | MCU 固有コードが「MCU 固有コード禁止」の層に存在する構造矛盾 |
| 2 | **`_write()` syscall 依存の固定化** | 5/7 | WASM/ESP-IDF/ホストで機能せず、マルチプラットフォーム展開の最大障壁 |
| 3 | **HAL Concept の設計不足** | 5/7 | NOT_SUPPORTED escape hatch、ClockTree 不在、必須/拡張未分離 |
| 4 | **同名ヘッダの IDE/スケール問題** | 5/7 | clangd 混乱、ビルドエラーの暗号化 |
| 5 | **ドキュメントと実装の乖離** | 4/7 | 存在しないパッケージを記述 |

### 5.3 到達した設計決定

レビュー横断比較とデバイスドライバ配置分析から、以下の設計決定に到達した。

**パッケージ構成の決定:**

| 決定 | 根拠 | 支持 |
|------|------|:----:|
| umiport と umiport-boards を統合 | 単独で意味を持たないものを分離する必然性がない | 2/3 |
| umidevice は独立パッケージ | IC に帰属する知識は MCU にもボードにも属さない | 3/3 |
| umiport 内部は `arm/` `mcu/` `board/` `src/` で構造化 | ディレクトリで責務分離を維持 | 2/3 |

**出力経路の決定:**

`_write()` syscall を廃止し、`rt::detail::write_bytes()` の link-time 注入に変更する。`_write()` は Cortex-M + newlib 向けの一実装に格下げする。

**HAL Concept の方向性:**

| 改善策 | 提案者 |
|--------|--------|
| Concept 必須/拡張分離 (`UartBasic` / `UartAsync`) | Opus 4.6 |
| ClockTree Concept 追加 | Kimi |
| Platform Concept + `static_assert` 検証 | Claude Web |

これら 3 つの改善策は互いに補完的であり、全て採用する。

---

## 6. UMI に最適なアプローチの導出

単一のアーキタイプでは 3 ユースケース全てを満たせない。調査結果から導出される UMI の最適解は **ハイブリッドアーキテクチャ** である。

### 6.1 各層で採用するアーキタイプ

| 層 | 採用アーキタイプ | 参照元 | UMI での実現 |
|----|-----------------|--------|-------------|
| **C++ 型システム** | トレイト/コンセプト型 | Rust embedded-hal | C++23 concept = Rust trait。ゼロオーバーヘッド静的ポリモーフィズム |
| **ビルド/Lua 層** | 継承 + オーバーレイ混成 | Mbed inherits + Zephyr overlay | Lua dofile() 継承 + xmake set_values() overlay。単一言語で三重構成を回避 |
| **リンカスクリプト** | テンプレート/生成型 | modm, libopencm3 | Lua DB -> memory.ld 自動生成。唯一源泉の原則 |
| **ボード定義** | 合成型 (Composition) | Rust BSP, umiport 設計 | platform.hh が MCU + Device + Config を合成 |

### 6.2 各フレームワークから採用する要素

| 採用要素 | 参照元 | UMI での実現 |
|---------|--------|-------------|
| データの唯一源泉 | modm (modm-devices) | Lua DB (database/*.lua) がメモリの唯一源泉 |
| memory.x のシンプルさ | Rust Embedded | 生成される memory.ld が同等の簡潔さ |
| sections.ld の分離 | ESP-IDF, Zephyr | ファミリ共通 sections.ld を手書きで維持 |
| `_add`/`_remove` 差分管理 | Mbed OS | Lua テーブル操作で同等の表現 |
| IP バージョンベースの共有 | libopencm3 | mcu/common/ に IP バージョン別ソースを配置可能 |
| overlay の非侵入性 | Zephyr (DT overlay) | `umiport.board_root` でプロジェクトローカルなボードを非侵入的に追加 |
| HAL スコープの限定 | embedded-hal | 初期化は concept に含めない（RAII に委ねる） |
| soc_caps.h の宣言的パターン | ESP-IDF | board.hh の constexpr + `if constexpr` でゼロオーバーヘッド |
| ペリフェラル別 3 層 API | Zephyr (UART) | concept 階層化 (UartBasic / UartBuffered / UartAsync) |
| JSON 1 ファイルの参入障壁 | PlatformIO | platform.hh + board.hh の 2 ファイルで新ボード追加完了 |

### 6.3 ハイブリッドアーキテクチャの全体像

| 層 | 内容 |
|----|------|
| **C++ Concept (umihal)** | Platform, OutputDevice, UartBasic, AudioCodec -- 契約定義 + static_assert 検証 |
| **合成 (platform.hh)** | `using Output = stm32f4::UartOutput; using Codec = CS43L22Driver<I2C>;` -- MCU x Device の唯一の統合点 |
| **Lua 継承 + Overlay (xmake)** | family.lua -> mcu.lua の dofile() 継承 + set_values() overlay |
| **テンプレート生成 (xmake rule)** | Lua DB -> memory.ld 自動生成。sections.ld はファミリ共通手書き |
| **ビルドメカニズム (arm-embedded)** | embedded.core -> コンパイラフラグ。アーキテクチャの普遍的知識 |

### 6.4 不採用としたアプローチ

| アプローチ | 不採用理由 |
|-----------|-----------|
| DeviceTree / YAML / Kconfig (Zephyr 方式) | UMI の規模にオーバーエンジニアリング。xmake Lua で十分 |
| GUI ベースコード生成 (CubeMX 方式) | 再生成でユーザー変更が消える問題 |
| 巨大モノリシック JSON (Mbed 方式) | 10,000 行超は保守性が著しく低下 |
| ランタイム Capabilities (CMSIS 方式) | ゼロオーバーヘッド要件と矛盾。コンパイル時解決すべき |

### 6.5 ユースケース検証

| UC | 手順 | ファイル数 |
|----|------|:--------:|
| UC1: 既存 BSP 使用 | xmake.lua に `set_values("umiport.board", "stm32f4-disco")` の 1 行 | 0 |
| UC2: BSP 継承/拡張 | platform.hh + board.hh をプロジェクトローカルに作成。startup/linker は同一 MCU で共有 | 2 |
| UC3: 新規 BSP 作成 | Lua DB + src/ (startup/linker) + board/ (platform.hh + board.hh)。`static_assert` で不足を検出 | 5+ |

---

## 参照文書

| 文書 | 内容 |
|------|------|
| ../archive/BSP_ANALYSIS.md | 5 アーキタイプ分類、3 ユースケース評価、共通問題パターン |
| ../archive/PLATFORM_COMPARISON.md | 13 システムの詳細調査・横断比較表 |
| ../archive/HAL_INTERFACE_ANALYSIS.md | 10 HAL のインターフェイス比較、根源的設計原理 |
| ../archive/review/compare.md | 7 レビュアーの合意分析、改善優先度 |
| ../archive/review/compare_device.md | デバイスドライバ配置分析、パッケージ構成決定 |
