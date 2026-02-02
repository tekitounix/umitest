# 00: 実装計画

> 各設計ドキュメント (01〜06) の実装タスクを抽出した ToDo リスト。
> 各タスクの設計詳細は関連ドキュメントを参照。

---

## 実装ルール (CLAUDE.md より)

**全フェーズ共通で遵守すること。**

### ワークフロー

| ルール | 内容 |
|--------|------|
| **計画と実装を分離** | 計画・調査・設計フェーズではコード変更しない。確認を得てから実装に移る |
| **ビルド成功だけで完了にしない** | ファームウェアタスクは **build → flash → デバッガ検証** の全工程を経て完了 |
| **既存コードを先に読む** | 変更前に現在の実装を理解する。盲目的な書き換え禁止 |
| **古い実装への巻き戻し禁止** | ロールバックによる「修正」は行わない |
| **変更後テスト実行** | ライブラリコード変更後は `xmake test` を実行。失敗状態でコミットしない |

### 各フェーズの実装手順テンプレート

```
1. 関連ドキュメント・既存コードを読む
2. 対象ファイル・API を特定
3. build/flash/test パスを確認
4. 完了基準を定義（何をどう検証するか）
5. → 確認を得てから実装開始
6. インクリメンタルに実装（1変更1コミット単位を推奨）
7. xmake test でホストテスト通過
8. xmake build stm32f4_kernel で ARM ビルド通過
9. xmake flash-kernel で書き込み
10. デバッガ (pyOCD/GDB) で動作検証
11. → 検証完了をもってタスク完了
```

### コードスタイル

| 項目 | 規則 |
|------|------|
| 規格 | C++23 |
| フォーマッタ | clang-format (LLVM base, 4-space indent, 120 char) |
| 関数/メソッド/変数/constexpr | `lower_case` |
| 型/クラス/concept | `CamelCase` |
| enum 値 | `UPPER_CASE` |
| 名前空間 | `lower_case` |
| メンバ変数 | プレフィックス/サフィックスなし。`m_` 禁止、`_` サフィックス禁止。必要なら `this->` |
| ポインタ/参照 | 左寄せ: `int* ptr` ✓ |
| エラー処理 | `Result<T>` またはエラーコード。カーネル/オーディオパスで例外禁止 |
| constexpr | `constexpr` のみ。冗長な `inline` を付けない (C++17 以降は暗黙的に inline) |

### リアルタイム安全性 (ISR / audio callback / process())

**ハード制約 — 違反はUBまたはオーディオグリッチを引き起こす:**

- ヒープ確保禁止 (`new`, `malloc`, `std::vector` の growth)
- ブロッキング同期禁止 (`mutex`, `semaphore`)
- 例外禁止 (`throw`)
- stdio 禁止 (`printf`, `cout`)

### デバッグアダプタ注意事項

- アダプタ無応答は USB の問題ではない
- `pgrep -fl pyocd`, `pgrep -fl openocd` で孤立プロセスを確認
- 特定 PID のみ kill — 広範なパターンでの kill 禁止

---

## ライブラリ構成ルール

`lib/` 以下の各ライブラリは独立した構成を持つ。

### ディレクトリ構成テンプレート

```
lib/<library>/
├── xmake.lua         # ライブラリターゲット定義
├── include/          # または直接ヘッダ配置（umiport は特殊構成）
├── docs/             # 設計ドキュメント
│   └── design/       # 設計ドキュメント（ナンバリング）
└── test/
    ├── xmake.lua     # テストターゲット定義（独立）
    ├── test_*.cc     # ホスト単体テスト
    └── stub_*.hh     # テスト用スタブ
```

### ライブラリ構成

```
lib/
├── umimmio/          ← レジスタ抽象 (umi_mmio コア移植)
│   ├── xmake.lua
│   ├── include/mmio/
│   │   ├── mmio.hh
│   │   └── transport/
│   │       ├── direct.hh
│   │       └── i2c.hh
│   ├── docs/
│   └── test/
│       ├── xmake.lua
│       └── test_*.cc
│
├── umiport/          ← HAL + Driver + Device
│   ├── xmake.lua     ← umi-port ルール定義 + ターゲット定義
│   ├── concepts/     ← HAL concepts (umi_mmio hal/ 由来 + umiport固有)
│   ├── device/       ← 外部デバイスドライバ (transport非依存)
│   ├── common/       ← Cortex-M共通 (NVIC, SCB, SysTick, DWT)
│   ├── arch/         ← CPUコア固有 (cm4, cm7)
│   ├── mcu/          ← MCUペリフェラル (stm32f4, stm32h7)
│   ├── board/        ← ボード固有ドライバ (stm32f4_disco, daisy_seed)
│   ├── platform/     ← 実行環境 (embedded, wasm)
│   ├── docs/design/  ← 本ドキュメント群
│   └── test/
│       ├── xmake.lua
│       └── test_*.cc
│
├── umiusb/           ← USBスタック (ミドルウェア)
├── umifs/            ← ファイルシステム
├── umidsp/           ← DSP
└── ...
```

---

## テスト戦略

### テストディレクトリ構成

`lib/umiusb/test/` の構造に倣い、`lib/umiport/test/` に全テストを集約する。

```
lib/umiport/test/
├── xmake.lua                      # テストターゲット定義
│
│  ── ホスト単体テスト ──
├── test_concept_compliance.cc     # 全 Concept の static_assert 検証
├── test_hal_stm32f4.cc            # STM32F4 HAL のスタブ使用テスト
├── test_hal_stm32h7.cc            # STM32H7 HAL のスタブ使用テスト
├── test_board_spec.cc             # BoardSpec/McuInit の定数検証
├── test_audio_driver.cc           # AudioDriver Concept 実装のロジックテスト
├── test_device_codec.cc           # 外部デバイスドライバ（mmio経由のI2Cテスト）
├── stub_registers.hh              # レジスタメモリをRAM上にエミュレート
│
│  ── Renode シミュレータテスト ──
├── renode_port_test.cc            # 最小起動・ペリフェラル初期化検証
├── port_test.resc                 # Renode スクリプト
└── port_test.robot                # Robot Framework テスト
```

### テストレベル

| レベル | 場所 | 実行方法 | 対象 |
|--------|------|---------|------|
| **L1: ホスト単体テスト** | `lib/umiport/test/test_*.cc` | `xmake run test_port_*` | Concept充足、HALロジック、ボード定数 |
| **L2: Renode シミュレータ** | `lib/umiport/test/renode_*.cc` | `xmake renode-port-test` | ペリフェラル初期化、クロック設定 |
| **L3: 実機テスト** | STM32F4-Discovery / Daisy Seed | `xmake flash-kernel` + pyOCD | 実際のHW動作、オーディオ、USB |

### L1: ホスト単体テスト

スタブレジスタまたはmmioのモックTransportを使ってHWなしでロジックをテストする。

```cpp
// mmioのモックTransportを使用したテスト例
// umimmioのtest_transport.hhにMockI2cBus, MockSpiBus等が用意されている
#include <mmio/test/test_transport.hh>

// Concept充足テスト
static_assert(concepts::GpioPin<Stm32f4Gpio<PA5>>);
static_assert(concepts::I2cMaster<Stm32f4I2c<1>>);
static_assert(concepts::AudioCodec<device::Cs43l22Driver<MockI2cBus>>);
```

テスト対象:
- **Concept充足**: 全レイヤーの全型が対応するConceptを `static_assert` で満たすことを検証
- **HALロジック**: クロック設定計算、GPIO AF設定、DMA設定のレジスタ書き込み値を検証
- **デバイスドライバ**: mmio I2cTransport経由のレジスタ操作をモックI2Cで検証
- **BoardSpec**: 定数値（クロック周波数、バッファサイズ等）の妥当性を検証

### xmake.lua テンプレート (lib/umiport/test/xmake.lua)

```lua
-- lib/umiport/test/xmake.lua
local test_dir = os.scriptdir()
local umiport_dir = path.directory(test_dir)
local lib_dir = path.directory(umiport_dir)
local root_dir = path.directory(lib_dir)

-- Concept 充足テスト
target("test_port_concepts")
    add_rules("host.test")
    set_default(true)
    add_deps("umi.mmio")
    add_files(path.join(test_dir, "test_concept_compliance.cc"))
    add_includedirs(path.join(root_dir, "tests"))
    add_includedirs(path.join(umiport_dir, "concepts"))
    -- F4ターゲットのインクルードパスを設定してテスト
    add_includedirs(path.join(umiport_dir, "common"))
    add_includedirs(path.join(umiport_dir, "arch/cm4"))
    add_includedirs(path.join(umiport_dir, "mcu/stm32f4"))
    add_includedirs(path.join(umiport_dir, "board/stm32f4_disco"))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- ... 他のテストターゲットも同様
```

### xmake.lua テンプレート (lib/umiport/xmake.lua)

```lua
-- lib/umiport/xmake.lua
-- UMI-Port: Hardware Abstraction & Driver Layer

-- umi-port ルール: common/ と concepts/ を自動付与
rule("umi-port")
    on_load(function (target)
        local umiport_dir = path.join(os.projectdir(), "lib/umiport")
        target:add("includedirs", path.join(umiport_dir, "common"))
        target:add("includedirs", path.join(umiport_dir, "concepts"))
    end)
rule_end()

-- テストをインクルード
includes("test")
```

---

## 目標

Daisy Pod カーネルの実装を最終目標とし、
UMIプロジェクト全体の HAL / Driver / Middleware 構造を根本からリファクタリングする。

## 前提

- **libDaisy 完全非依存** — レジスタ直接操作による独自実装
- **マクロ排除** — `#ifdef` による切り替えは一切使わない
- **Concept駆動** — レイヤー契約を C++23 Concept で形式定義
- **mmio統一** — MCU内蔵レジスタと外部デバイスを同一mmio APIで操作
- libDaisy は `.refs/libDaisy/` にリファレンスとしてのみ配置

---

## フェーズ一覧

| Phase | 内容 | 状態 | 破壊的変更 |
|-------|------|------|-----------|
| 0a | port/ リファクタリング（既存STM32F4） | ✅ 完了 | include パス全変更 |
| 0b | umimmio 移植 + concepts/ 整備 | ✅ 完了 | なし |
| 1 | STM32H7 HAL + 最小起動 | ✅ 完了 | なし |
| 2 | オーディオ出力 | ✅ 完了 | なし |
| 3 | 全二重オーディオ + カーネル | ✅ 完了 | なし |
| 4 | USB + Pod HID | ✅ 完了 | なし |
| 5 | QSPI + SDRAM | ✅ 完了 | なし |
| 6 | umios統合 + 全I/O | ✅ 完了 | なし |
| 7 | umi::Kernel移行 + USB Audio双方向 | ✅ 完了 | main.cc大幅書き換え |
| 8 | アプリケーション完全分離 | ✅ 完了 | syscall/MPU/リンカ変更 |
| 9 | 品質・安定化（D-Cache、テスト） | 未着手 | なし |

---

## Phase 0a: port/ リファクタリング（既存STM32F4）

> 詳細: [01](01-principles.md), [02](02-port-architecture.md), [05](05-migration.md)

既存の散在した `backend/` + `kernel/port/` を新しい `lib/umiport/` 構造に移行する。
**既存ターゲット（STM32F4-Discovery）が壊れないことを保証しつつ**構造を整理。

### ライブラリ初期構築

- [x] `lib/umiport/` ディレクトリを作成
- [x] `lib/umiport/xmake.lua` を作成（`umi-port` ルール定義）
- [x] `lib/umiport/test/xmake.lua` を作成
- [x] `lib/umiport/docs/design/` に設計ドキュメント群を配置

### ファイル移行

- [x] 既存ファイルを [05: 移行マッピング](05-migration.md) に従いコピー
- [x] コピー先ファイルの `#include` パスを `<layer/header.hh>` 形式に全て更新
- [x] xmake.lua に `umi.port.embedded.stm32f4_disco`, `umi.port.wasm` ターゲット追加
- [x] `umi.embedded`, `umi.wasm` にumiportの依存を追加（レガシーパス並行）
- [x] consumer側（examples, kernel）の `#include` パスを新形式に移行
- [x] `lib/umi/xmake.lua` のレガシーパスを削除（backend/cm, backend/wasm）
- [x] `stm32_otg.hh`: MCUレジスタ定義は `mcu/usb_otg.hh` として移行済み。umiusb HAL実装（`Stm32FsHal`等）はumiusbに残す
- [x] `lib/bsp/`, `lib/hal/` のレガシーパスを削除 — syscalls.cc/linker.ldをumiport/mcu/stm32f4/に移動

### HW 分離

- [ ] カーネルの HW 直接依存を Concept 経由に書き換え

### テスト・検証

**L1 (ホスト単体):**
- [x] `xmake test` パス（test_kernel 88/88）

**L3 (実機):**
- [x] `xmake build stm32f4_kernel` パス
- [x] `xmake build headless_webhost` パス
- [ ] `xmake flash-kernel` → pyOCD で動作検証（リグレッションなし）

### クリーンアップ

- [x] 旧 `lib/umios/backend/cm/`, `backend/wasm/` を削除
- [x] 旧 `lib/umios/kernel/port/` を削除
- [x] kernel内の相対パス参照を `<layer/header.hh>` 形式に修正

**完了条件**: `xmake test` + `xmake build stm32f4_kernel` + `xmake build headless_webhost` 全パス

---

## Phase 0b: umimmio 移植 + concepts/ 整備

> 詳細: [06](06-mmio-integration.md), [03](03-concept-contracts.md)

umi_mmioのレジスタ抽象コアを`lib/umimmio/`に移植し、
HAL conceptsを`lib/umiport/concepts/`に配置する。

### umimmio ライブラリ移植

- [x] `lib/umimmio/` ディレクトリ作成
- [x] `lib/umimmio/xmake.lua` 作成（`umi.mmio` ターゲット）
- [x] `lib/umimmio/include/mmio/mmio.hh` — コアヘッダ移植
- [x] `lib/umimmio/include/mmio/transport/direct.hh` — DirectTransport
- [x] `lib/umimmio/include/mmio/transport/i2c.hh` — I2cTransport
- [x] `lib/umimmio/test/xmake.lua` 作成
- [x] `lib/umimmio/test/test_mmio.cc` — 基本テスト移植（全10グループ通過）
- [x] `lib/umimmio/test/test_transport.hh` — モックTransport移植
- [x] `lib/umimmio/test/test_assert.cc` — アサーションテスト移植（debug buildで通過）
- [x] `lib/umimmio/test/test_register_value.cc` — RegisterValue テスト移植
- [x] `lib/umimmio/test/test_value_get.cc` — Value::get()テスト移植（Device→Register修正済み）
- [x] `lib/umi/xmake.lua` に `umi.mmio` ターゲット追加

### HAL Concepts 配置 (umi_mmio hal/ → umiport/concepts/hal/)

- [x] `concepts/hal/result.hh` — `Result<T, ErrorCode>` 共通エラー型
- [x] `concepts/hal/gpio.hh` — GpioPin, GpioPort
- [x] `concepts/hal/i2c.hh` — I2cMaster
- [x] `concepts/hal/uart.hh` — Uart
- [x] `concepts/hal/timer.hh` — Timer, DelayTimer, PwmTimer
- [x] `concepts/hal/i2s.hh` — I2sMaster
- [x] `concepts/hal/interrupt.hh` — InterruptController
- [x] `concepts/hal/audio.hh` — AudioDevice

### umiport 固有 Concepts

- [ ] `concepts/codec.hh` — AudioCodec（外部コーデック抽象）
- [ ] `concepts/board.hh` — BoardSpec, McuInit
- [ ] `concepts/fault.hh` — FaultReport
- [ ] `concepts/arch.hh` — CacheOps, FpuOps, ContextSwitch, ArchTraits

### device/ レイヤー基盤

- [x] `device/` ディレクトリ構造を作成
- [x] `device/cs43l22/cs43l22_regs.hh` — mmio Register定義（STM32F4-Disco用コーデック）
- [x] `device/cs43l22/cs43l22.hh` — Transport非依存ドライバ
- [ ] 既存の `board/stm32f4_disco/board/cs43l22.hh` をdevice/に移行

### テスト・検証

**L1 (ホスト単体):**
- [x] `test_mmio.cc` — mmioコアのホストテスト通過
- [ ] `test_concept_compliance.cc` — 全Conceptのstatic_assert検証
- [ ] `test_device_codec.cc` — CS43L22ドライバのモックI2Cテスト
- [x] `xmake test` パス（既存テスト全通過）

**完了条件**: umimmioテスト通過 ✓ + Concept充足テスト（未実施）+ 既存テスト全パス ✓

---

## Phase 1: STM32H7 HAL + 最小起動（LED 点滅）

> 詳細: [02](02-port-architecture.md), [03](03-concept-contracts.md)

STM32H750 の最小 HAL を `lib/umiport/mcu/stm32h7/` に実装。
mmio Device/Register 定義を使用し、DirectTransport でアクセス。

### HAL 実装 (mmio形式)

- [x] `lib/umiport/mcu/stm32h7/mcu/rcc.hh` — mmio Register定義 + RCC操作
    - HSE 16MHz → PLL1 → 480MHz (boost mode)、PWR VOS0
    - HSI48 は CR bit12/13（CRRCRではない。CRRCR offset=0x08 は読み取り専用キャリブレーション）
    - AHB1ENR::ADC12EN (bit5)、D3CCIPR::ADCSEL (bits 17:16) 追加済み
- [x] `lib/umiport/mcu/stm32h7/mcu/pwr.hh` — 電源設定（VOS1→VOS0 boost）
- [x] `lib/umiport/mcu/stm32h7/mcu/gpio.hh` — mmio Register定義 + GPIO操作
- [x] `lib/umiport/mcu/stm32h7/mcu/flash.hh` — Flash wait state設定

### PAL 実装

- [x] `lib/umiport/arch/cm7/arch/cache.hh` — D-Cache / I-Cache 有効化
- [x] `lib/umiport/arch/cm7/arch/fpu.hh` — FPU有効化（CPACR）
- [x] `lib/umiport/arch/cm7/arch/context.hh` — コンテキストスイッチ（init_task_stack, EXC_RETURN）
- [x] `lib/umiport/arch/cm7/arch/handlers.hh` — SVC_Handler, PendSV_Handler, start_first_task
- [x] `lib/umiport/arch/cm7/arch/switch.hh` — request_context_switch (PendSV trigger)

### Driver 実装

- [x] `lib/umiport/board/daisy_seed/board/bsp.hh` — Seed LED (PC7)、基本クロック定数
- [x] `lib/umiport/board/daisy_seed/board/mcu_init.hh` — `init_clocks()`, `init_led()`, `toggle_led()`

### カーネル

- [x] `examples/daisy_pod_kernel/kernel.ld` — リンカスクリプト（内蔵Flash実行、DTCM BSS対応）
- [x] `examples/daisy_pod_kernel/src/main.cc` — Reset_Handler、4タスクRTOS
- [x] xmake.lua に daisy_pod_kernel ターゲット追加

### 実装メモ

- g_vector_table を DTCM に配置（D-Cache coherency問題を回避）
- Reset_Handler で DTCM BSS をゼロ初期化（VectorTableRAM::initialized_ が未初期化だと init() がスキップされる）

### テスト・検証

**L1 (ホスト単体):**
- [ ] `test_hal_stm32h7.cc` — RCC PLL計算、GPIO設定の検証
- [ ] `test_concept_compliance.cc` に H7/CM7 のConcept充足追加
- [ ] `xmake test` パス

**L3 (実機 Daisy Seed):**
- [x] `xmake build daisy_pod_kernel` パス
- [x] pyOCD flash → LED (PC7) 点滅確認
- [x] デバッガ (pyOCD + GDB) 接続確認、control_task_entry 到達確認

---

## Phase 2: オーディオ出力

> 詳細: [03](03-concept-contracts.md), [04](04-hw-separation.md)

SAI1 経由でオーディオ出力。外部コーデックは device/ レイヤーで実装。

### HAL 実装 (mmio形式)

- [x] `lib/umiport/mcu/stm32h7/mcu/sai.hh` — SAI1 Block A/B: Master TX + Slave RX
- [x] `lib/umiport/mcu/stm32h7/mcu/dma.hh` — DMA + DMAMUX、Circular mode
- [x] `lib/umiport/mcu/stm32h7/mcu/i2c.hh` — I2Cドライバ（ポーリング）

### Device 実装

- [x] `lib/umiport/device/ak4556/ak4556.hh` — AK4556 リセットピン制御（レジスタなし）
- [x] `lib/umiport/device/wm8731/wm8731.hh` — WM8731 I2C制御
- [x] `lib/umiport/device/pcm3060/pcm3060.hh` — PCM3060 I2C制御

### Driver 実装

- [x] `lib/umiport/board/daisy_seed/board/audio.hh` — SAI1構成 + コーデック自動検出 + DMA
- [x] DMAバッファを `.dma_buffer` セクション (SRAM_D1) に配置

### テスト・検証

**L1 (ホスト単体):**
- [ ] `test_device_codec.cc` にWM8731テスト追加（モックI2C経由）
- [ ] `test_audio_driver.cc` — AudioDriver Concept のstart/stop/configureシーケンス
- [ ] `xmake test` パス

**L3 (実機 Daisy Seed):**
- [ ] ヘッドフォンからサイン波出力確認（DMA動作中、要検証）

---

## Phase 3: 全二重オーディオ + カーネル

> 詳細: [04](04-hw-separation.md)

- [x] SAI1 Block B (Slave RX) 追加 — 全二重構成済み（audio.hh）
- [x] WM8731 I2C 制御
- [x] コーデック自動検出（AK4556 / WM8731 / PCM3060、GPIO ADC読み取り）
- [x] RTOS 起動（4タスク: Audio/System/Control/Idle）
- [x] PendSV/SVC コンテキストスイッチ
- [ ] AudioContext マッピング
- [ ] syscall

### テスト・検証

**L1 (ホスト単体):**
- [ ] `xmake test` パス

**L3 (実機 Daisy Seed):**
- [x] RTOS起動、control_task_entry到達確認（pyOCD/GDB）
- [ ] パススルー（入力→出力）動作

---

## Phase 4: USB + Pod HID

- [x] `lib/umiport/board/daisy_seed/board/usb.hh` — USB OTG HS GPIO/クロック初期化（PB14/PB15 AF12, HSI48）
- [x] USB MIDI（umiusb Stm32HsHal + UsbMidiClass → AudioClass統合済み）
    - H7 VBUS sensing修正: `GOTGCTL BVALOEN/BVALOVAL` + `GCCFG &= ~VBDEN`（F4のNOVBUSSENSとは異なる）
- [x] `lib/umiport/mcu/stm32h7/mcu/adc.hh` — ADC1 mmioレジスタ定義 + 定数群
    - ADCクロック: per_ck (HSI 64MHz) / DIV4 = 16MHz、BOOST=0b10
    - D3CCIPR ADCSEL=10 (per_ck) をmain.ccで設定
- [x] `lib/umiport/board/daisy_pod/board/bsp.hh` — Pod固有ピン定義（libDaisy daisy_pod.cpp準拠）
- [x] `lib/umiport/board/daisy_pod/board/hid.hh` — Pod HIDドライバ
    - Button: 8ビットシフトレジスタデバウンス（libDaisy Switch準拠）
    - Encoder: 2ビット遷移検出（libDaisy Encoder準拠）
    - SoftPwmLed/RgbLed: ソフトウェアPWM 120Hz（libDaisy Led準拠、inverted polarity）
    - Knobs: ADC1 DMA circular、16bit、32xオーバーサンプリング、ワンポールフィルタ2ms
- [x] USB Audio（umiusb AudioClass統合）
    - `AudioFullDuplexMidi48k` で Audio IN/OUT + MIDI 統合
    - DMA ISR → audio task → `read_audio()` / `write_audio_in()` でリングバッファ接続
    - macOSで "Daisy Pod Audio" として認識（2ch IN/OUT, 48kHz）
    - USB未接続時はシンセ出力またはパススルー（RX→TX）にフォールバック
- [x] MIDI→シンセ発音（8ボイスポリ、ノコギリ波、AR エンベロープ）
    - MIDI Note On/Off → lock-free EventQueue → audio task でボイス割り当て
    - knob1 → マスターボリューム制御
- [x] HID → Event Queue 基盤（SPSC lock-free ring buffer、EventType定義）
    - エンコーダクリック/回転、ボタン、ノブ変更をEvent化
- [x] HID → UMI Event 統合 — EventRouter.receive_input() でエンコーダ/ノブをルーティング

### テスト・検証

**L1 (ホスト単体):**
- [ ] `xmake test` パス

**L3 (実機 Daisy Pod):**
- [x] USB MIDI デバイス認識確認（macOS: "Daisy Pod MIDI" → "Daisy Pod Audio", VID:0x1209, PID:0x000B）
- [x] USB Audio デバイス認識確認（macOS Audio MIDI Setup: 2ch IN/OUT, 48kHz）
- [x] ADCキャリブレーション通過、HID初期化完了確認（GDB: control_task_entry到達）
- [x] ノブ→LED デモ動作（knob1→LED1赤、knob2→LED2青、エンコーダクリック→Seed LEDトグル）
- [x] USB MIDI 受信→シンセ発音、ノブでパラメータ変更（knob1→volume）
- [x] パススルー動作（USB未接続 + シンセ無音時: SAI RX→TX コピー）

---

## Phase 5: QSPI + SDRAM（オプション）

- [x] `lib/umiport/mcu/stm32h7/mcu/qspi.hh` — QUADSPI メモリマップドモード
- [x] `lib/umiport/mcu/stm32h7/mcu/fmc.hh` — FMC SDRAM初期化 *(base_address 0x52000000→0x52004000 修正済み)*
- [x] `lib/umiport/board/daisy_seed/board/sdram.hh` — IS42S16160J SDRAM初期化
- [x] `lib/umiport/board/daisy_seed/board/qspi.hh` — IS25LP064A QSPI初期化

### テスト・検証

**L3 (実機 Daisy Seed):**
- [x] SDRAM初期化通過（ハング無し）
- [x] QSPI初期化通過（APMS修正済み）
- [x] SDRAM 読み書き検証 — 0xC0000000 に 0xDEADBEEF 書き込み/読み出し成功
    - 原因1: FMC_BCR1.FMCEN (bit31) 未設定 → `__FMC_ENABLE()` 相当を追加
    - 原因2: MPU Region設定なし → D-cache有効時にSDRAM領域のメモリ属性不正 → MPU Region 1 (Cacheable+Bufferable) 追加
- [x] QSPI XIP 実行検証 — 0x90000000 読み取り成功 (BusFaultなし、byte0=0x00)
- [x] SAI DMA動作確認 — DMA1 Stream0 EN=1, NDTR循環中

---

## Phase 6: umios統合 + 全I/O対応

- [x] umios AudioContext / EventRouter / SharedState 統合
    - float audio buffers、DMA int32↔float変換
    - AudioContext構築 → 登録済みProcessor呼び出し → フォールバック(シンセ/パススルー)
    - EventRouter: MIDI callback → RawInput → audio queue / shared state
- [x] `lib/umiport/mcu/stm32h7/mcu/usart.hh` — USART1 mmioレジスタ定義
- [x] `lib/umiport/board/daisy_seed/board/midi_uart.hh` — MIDI UART ドライバ
    - PB6=TX AF7, PB7=RX AF7, 31250 baud, 8N1
    - RXNE割り込み → MidiUartParser → on_midi_message
    - 送信: blocking TXE待ち
- [x] `lib/umiport/mcu/stm32h7/mcu/sdmmc.hh` — SDMMC1 mmioレジスタ定義
- [x] `lib/umiport/board/daisy_seed/board/sdcard.hh` — microSD ドライバ
    - PC8-12=D0-D3+CLK, PD2=CMD, AF12
    - 4-bit bus, SDHC対応, blocking FIFO polling
    - タイムアウト付きコマンド送信
- [ ] microSD実機検証 — 要: D1CCIPR.SDMMCSEL クロックソース設定

### テスト・検証

**L3 (実機 Daisy Pod):**
- [x] CFSR=0（フォールトなし）、スケジューラ稼動
- [x] SDRAM検証 — sdram_result=1 (PASS), sdram_read=0xDEADBEEF
- [x] MIDI UART初期化完了（USART1 IRQ登録済み, IRQ37）
- [ ] MIDI UART送受信検証（外部MIDIデバイス接続時）
- [ ] microSD読み書き検証（カード挿入 + クロック設定後）

### 対応済み全I/O一覧

| I/O | 状態 | 備考 |
|-----|------|------|
| RGB LED ×2 | ✅ | ソフトウェアPWM 120Hz |
| ボタン ×2 | ✅ | 8bit シフトレジスタデバウンス |
| ノブ ×2 | ✅ | ADC1 DMA + ワンポールフィルタ |
| プッシュエンコーダ | ✅ | 2bit 遷移検出 |
| オーディオ IN/OUT | ✅ | SAI DMA全二重 48kHz/24bit |
| USB Audio+MIDI | ✅ | HS内蔵PHY, macOS認識済み |
| MIDI UART IN/OUT | ✅ | USART1 31250baud, IRQ RX |
| microSD | ⚠️ | ドライバ完成、クロック設定未完 |

---

## Phase 7: umi::Kernel移行 + USB Audio双方向

カスタムmini-RTOSを`umi::Kernel`ベースに移行。USB Audio IN/OUTを追加。

### umi::Kernel 移行

- [x] `examples/daisy_pod_kernel/src/arch.hh` — CM7用archレイヤー
    - TaskContext、コールバック型定義、init_task、yield、start_scheduler
- [x] `examples/daisy_pod_kernel/src/arch.cc` — PendSV/SVC/SysTick ハンドラ
    - `umi_cm7_current_tcb` / `umi_cm7_switch_context` extern "C"シンボル
    - BASEPRI=0x60 ガード付きPendSV、svc_handler_cディスパッチ
- [x] main.cc書き換え — `umi::Kernel<8, 4, Stm32H7Hw, 1>`
    - Stm32H7Hw: BASEPRI critical section、DWT cycle count (480MHz)、PendSV trigger
    - SpscQueue<AudioBuffer, 4> for DMA ISR→audio task
    - 3タスク: audio (REALTIME), control (USER), idle
    - FPUポリシー: compile-time解決（audio+control=FPU保存）
- [x] handlers.cc依存除去 — arch.ccにハンドラ含む

### D-Cache / メモリ配置修正

- [x] midi_event_queue: D2 SRAM → BSS（D-Cacheコヒーレンシ問題回避）
- [x] d2_dbg[16]: D2 SRAM → DTCM（pyOCDから直接可視）
- [x] スタックサイズ: 512 → 2048 words (audio/control)
- [x] D-Cache再有効化 — post_data_init()分離で.data init後に有効化、CCR=0x00070200確認済み

### USB Audio 双方向

- [x] USB Audio IN: `write_audio_in(out, AUDIO_BLOCK_SIZE)` — シンセ出力をUSB経由でホストへ
- [x] USB Audio OUT: `read_audio(out, AUDIO_BLOCK_SIZE)` — ホスト音声をSAI TX（PCM3060）にパススルー
- [x] macOS "Daisy Pod Audio" として2ch IN/OUT認識

### テスト・検証（全自動化）

**L3 (実機 Daisy Pod):**
- [x] ビルド: `xmake build daisy_pod_kernel` パス (24.8KB Flash, 41% RAM)
- [x] フラッシュ: pyOCD flash成功
- [x] デバッガ: d2_dbg[0] (audio task count) 増加中、HardFaultなし
- [x] USB MIDI受信: `python3 mido` → Note On C4 vel=100 → d2_dbg[7]=0x03903C64
- [x] シンセ発音: ~260Hz音声出力確認（ユーザー確認）
- [x] USB Audio IN: `sounddevice.rec()` → Peak=9607, RMS=2913（MIDI Note送信中）
- [x] USB Audio OUT: `sounddevice.play()` → 440Hz sine再生コマンド正常完了

### 既知の課題

- D-Cache無効のまま（パフォーマンス影響）
- デバッグ計測用 d2_dbg[16] が残っている（クリーンアップ未実施）
- synth_volume のknobフォールバック（0.01未満→0.5f）は暫定対策

---

## Phase 8: アプリケーション完全分離

OS（カーネル）とアプリケーション（.umia）を完全に分離し、
STM32F4カーネルと同じOS/App二分割アーキテクチャをH7で実現する。

### 目標

- カーネル（OS）: ハードウェア初期化、タスクスケジューリング、ドライバ、USB/MIDI処理
- アプリ（.umia）: Processor実装のみ。`process(AudioContext&)` を提供
- アプリはsyscall経由でOSサービスにアクセス（MIDI送受信、パラメータ、時刻取得等）
- MPU保護: アプリはユーザーモード、OS特権モード

### カーネル側の変更

- [x] main.cc → kernel.cc/kernel.hh 分離 — モノリシックなmain.ccをカーネルモジュール+エントリに分割
- [x] カーネルからシンセ実装を分離 — `synth.hh` (DaisySynth) としてフォールバック用に独立
- [x] AppLoader + SharedMemory統合 — `umios/kernel/loader.hh` + `loader_stub.cc`
- [x] syscallディスパッチ実装 — exit, yield, register_proc, wait_event, get_time, get_shared, sleep, log
- [x] AudioContext構築 — アプリRUNNING時: int32→float変換→`g_loader.call_process()`→int32変換、未ロード時: フォールバックシンセ
- [x] 共有メモリ（`_shared_start`） — kernel.ldに.sharedセクション追加 (0x24070000, 64KB)
- [x] MPU設定 — 8リージョン (カーネルSRAM, QSPI, App RAM, Shared, D2 DMA, Peripherals, Flash, DTCM)
    - Note: D1 SRAMリージョンはShareableビットなし（ldrex/strex互換性のため）
- [x] QSPIガード — `qspi_accessible()` でRCC_AHB3ENR確認、未初期化時はload_appスキップ

### アプリ側

- [x] `examples/daisy_pod_synth_h7/` — H7用ミニマルシンセアプリ (Flash 372B, RAM 132B)
- [x] `app.ld` — QSPI XIP (0x90000080) + APP_RAM (0x24040000) リンカスクリプト
- [x] `crt0.cc` — .data/.bssの初期化 → main()呼び出し
- [x] `main.cc` — SVC syscallでregister_processor → yieldループ
- [x] xmake.lua に `daisy_pod_synth_h7` ターゲット追加

### リンカスクリプト変更

- [x] カーネル用: .shared (0x24070000, 64KB)、_app_image_start, _app_ram_start/size シンボル
- [x] アプリ用: APP_FLASH (0x90000080, 8MB-128B) + APP_RAM (0x24040000, 192KB)

### ビルドタスク

- [x] `xmake flash-h7-kernel` — pyOCDでカーネルフラッシュ
- [x] `xmake flash-h7-app` — pyOCDでQSPIにアプリフラッシュ
    - Note: pyocd ターゲット名は `stm32h750xx`（xmake.luaの `stm32h750xbh6` は要修正）

### テスト・検証

**L1 (ホスト単体):**
- [x] syscallディスパッチのユニットテスト (`test_syscall_context` 7/7パス)
- [x] AudioContext構築→Processor呼び出しのテスト (`test_syscall_context` に含む)

**L3 (実機 Daisy Pod):**
- [x] カーネル単体起動（アプリなし）→ フォールバックシンセ動作確認
    - d2_dbg[0] カウンタ増加中、HardFaultなし
- [x] USB認識: "Daisy Pod Audio" (VID:1209 PID:000B) — Audio 2ch IN/OUT 48kHz + MIDI
- [x] MIDI→シンセ発音: Peak -14.2 dBFS (Note On C4, MPU有効状態)
- [x] MPU有効化: 8リージョン設定、ldrex/strex正常動作
- [ ] QSPIアプリロード → アプリシンセ発音（QSPI初期化追加が必要）
- [ ] MPU違反検出 → アプリ停止、カーネル継続

---

## Phase 9: 品質・安定化

### D-Cache再有効化

- [x] D2 SRAM DMAバッファのコヒーレンシ解決
    - 採用: 選択肢B — MPU Region 4でD2 SRAMをDevice属性（Strongly-Ordered）に設定済み
    - D-Cacheはdevice属性領域をキャッシュしないため、明示的なinvalidate不要
    - TX側のDCCMVACは冗長だが害なし（D1 SRAMはWrite-Through属性）
- [x] D-Cache有効化コード追加 — `enable_dcache()` をReset_Handlerに追加（MPU設定後）
- [x] D-Cache有効化後の全機能リグレッションテスト — CFSR=0, CCR=0x00070200 (IC+DC+BP有効) 確認済み

### デバッグ計測クリーンアップ

- [x] d2_dbg[16]: `UMI_DEBUG`コンパイルスイッチで条件付きコンパイル済み（release buildでは無効）
- [x] HardFault_Handler: リリース時はLED点灯のみに簡素化（`UMI_DEBUG`でデバッグダンプ条件付き）

### ホストテスト整備

- [x] `xmake test` 全パス確認 (test_kernel 88/88, test_dsp 79/79, test_audio PASS, test_midi 152/152)
- [ ] umiport concept充足テスト
- [ ] デバイスドライバのモックテスト

---

## フェーズ間の依存

```
Phase 0a (port/リファクタリング)
  │
  ├→ Phase 0b (umimmio移植 + concepts/整備)
  │    │
  │    ├→ Phase 1 (H7 HAL + LED点滅)
  │    │    │
  │    │    └→ Phase 2 (オーディオ出力)
  │    │         │
  │    │         └→ Phase 3 (全二重 + カーネル)
  │    │              │
  │    │              └→ Phase 4 (USB + Pod HID)
  │    │                   │
  │    │                   └→ Phase 5 (QSPI + SDRAM)
  │    │                        │
  │    │                        └→ Phase 6 (umios統合 + 全I/O)
  │    │                             │
  │    │                             └→ Phase 7 (umi::Kernel + USB Audio) ✅
  │    │                                  │
  │    │                                  ├→ Phase 8 (アプリ完全分離) ✅
  │    │                                  │
  │    │                                  └→ Phase 9 (品質・安定化)
  │    │
  │    └→ 既存mcu/のmmio化（Phase 1以降で段階的に）
  │
  └→ 既存ターゲット（F4, WASM）は Phase 0a 完了時点で動作保証
```

## mmio化の段階的移行

Phase 0b以降、MCUレジスタ定義を段階的にmmio形式に移行する:

```
Phase 0a: 生volatile操作のまま移行（現在の状態）
Phase 0b: umimmio移植、device/cs43l22をmmio化（実証）
Phase 1 : H7 HALを最初からmmio形式で実装
Phase 2+: F4の既存mcu/も必要に応じてmmio化
```

H7 HALは最初からmmio形式で書くため、F4からの移行は必須ではない。
F4のmmio化は必要に応じて段階的に行う。

## リスク

| リスク | 影響 | 対策 |
|--------|------|------|
| Phase 0a で既存ビルドが壊れる | 全体ブロック | レガシーパス並行、段階的移行 |
| H7 キャッシュコヒーレンシ | オーディオ破損 | SRAM_D2 non-cacheable が最安全策 |
| VOS0 boost 電源シーケンス | 起動失敗 | libDaisy の初期化順を参照 |
| コーデック版検出 | 音が出ない | GPIO ADC 読み取りで判定（libDaisy 参照） |
| USB HS vs FS レジスタ差異 | USB 不動 | F4 OTG 実装をベースに H7 差分を反映 |
| umimmio移植時の互換性 | テスト失敗 | umi_mmioのテストを全て移植して検証 |

## 関連ドキュメント

| # | ドキュメント | 内容 |
|---|-------------|------|
| 01 | [基本原則](01-principles.md) | マクロ排除、同名ヘッダ、xmake制御のルール |
| 02 | [port/ アーキテクチャ](02-port-architecture.md) | レイヤー構成、HAL/Driver/Middleware関係、派生ボード |
| 03 | [Concept 契約](03-concept-contracts.md) | 各レイヤーの Concept 定義と検証パターン |
| 04 | [HW 分離原則](04-hw-separation.md) | カーネル・ミドルウェアからの HW 漏出排除 |
| 05 | [移行マッピング](05-migration.md) | 現行ファイル→新構成の対応表と手順 |
| 06 | [umi_mmio 統合計画](06-mmio-integration.md) | mmioライブラリ統合、device/レイヤー、Concept配置 |
