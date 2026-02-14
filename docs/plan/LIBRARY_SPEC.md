# UMI ライブラリ構成 設計仕様書

**バージョン:** 1.4.0
**作成日:** 2026-02-14
**最終監査日:** 2026-02-14

本文書は UMI の理想的なライブラリ構成を定義する**設計仕様書**である。実装手順は [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) を参照。

---

## 1. 概要

### 1.1 設計思想

**「少数の高品質なライブラリ」 > 「多数の細分化されたモジュール」**

全ライブラリが LIBRARY_SPEC v2.0.0 + UMI Strict Profile（§8.1 参照）準拠のスタンドアロン構成として lib/ 直下に配置される。

```
lib/
├── umicore/      (Core Types & Concepts)
├── umihal/       (HAL Concepts)
├── umimmio/      (MMIO Register Access)
├── umiport/      (Platform Ports)
├── umidevice/    (Device Drivers)
├── umidsp/       (DSP Algorithms)
├── umidi/        (MIDI Protocol)
├── umiusb/       (USB Device Stack)
├── umios/        (OS Kernel + Services)
├── umitest/      (Test Framework)
├── umibench/     (Benchmark Framework)
├── umirtm/       (Real-Time Monitor)
├── umi/          (Bundle definitions only)
└── docs/         (共通標準・ガイド)
```

**ライブラリ総数: 12**

### 1.2 ライブラリ分割の基準

ライブラリは以下の4条件を**すべて**満たす単位で分割する:

| 条件 | 説明 |
|------|------|
| **独立デプロイ可能** | 他のUMIライブラリなしでも単独ビルド・テスト可能 |
| **明確な責務境界** | 「このライブラリは何をするか」を1文で説明できる |
| **安定したAPI** | 公開APIが頻繁に変わらない（内部実装の変更は自由） |
| **再利用価値** | UMI以外のプロジェクトでも利用できる汎用性 |

**分割しない判断基準:**
- 常に一緒に変更される2つのモジュール → 1ライブラリに統合
- 単独では意味をなさないモジュール → 親ライブラリの内部モジュールに
- ファイル数が極端に少ない (3ファイル以下) → 関連ライブラリに統合

---

## 2. レイヤーモデル

```
L5  Application        ← Processor 実装、アプリケーション
L4  System             ← umios (カーネル + サービス統合)
L3  Domain             ← umidsp, umidi, umiusb (ドメイン固有)
L2  Platform           ← umiport, umidevice (ハードウェア抽象化)
L1  Foundation         ← umicore, umihal, umimmio (型・概念・基盤)
L0  Infrastructure     ← umitest, umibench, umirtm (開発支援)
```

**レイヤー規則:**
- 依存は**同一レイヤーまたは下位レイヤーのみ**許可
- L0 は特殊: テスト・ベンチマーク・デバッグ用で、**プロダクションコードからは依存禁止**
- 上位レイヤーへの依存は**コンパイルエラー**で防止（将来的にCI検証）

---

## 3. 名前空間規約

全ライブラリは `umi::` ルート名前空間配下に統一する。

| ライブラリ | 名前空間 |
|-----------|---------|
| umicore | `umi::core`, `umi` (トップレベル Concept) |
| umihal | `umi::hal` |
| umimmio | `umi::mmio` |
| umiport | `umi::port`, `umi::board` (ボード固有), `umi::pal` (PAL 生成物) |
| umidevice | `umi::device` |
| umirtm | `umi::rtm` |
| umitest | `umi::test` |
| umibench | `umi::bench` |
| umidsp | `umi::dsp` |
| umidi | `umi::midi` |
| umiusb | `umi::usb` |
| umios | `umi::os`, `umi::os::service`, `umi::os::ipc` |

---

## 4. 依存関係

### 4.1 全体依存関係図

```
                         umios ─────────────────────────┐
                         (kernel, service, runtime)     │  L4
                         deps: umicore + umiport        │
                                                        │
                                                        │
     umidsp          umidi          umiusb              │  L3
     (filter,synth)  (parser,       (audio,midi)        │  (umios とは独立)
     deps: umicore   protocol)     deps: umicore        │
                     deps: umicore       + umidsp       │
         │              │              │                │
         │              │              └─── umidsp (ASRC)
         │              │                               │
         └──────────────┼───────────────────────────────┘
                        │                               │
                        │                               │
     umiport ───────────┼───────────────────────────────┘  L2
     (pal,driver,board)  │
     deps: umihal       │    umidevice
           + umimmio    │    (codecs, drivers)
                        │    deps: umihal + umimmio
         │              │         │
         └──────────────┼─────────┘
                        │
     umicore        umihal        umimmio                  L1
     (types,event,  (concepts)    (register,
      audio,error)                 transport)
     deps: なし     deps: なし    deps: なし


     umitest        umibench      umirtm                   L0 (テスト/デバッグ専用)
     deps: なし     deps: なし    deps: なし
```

**図の読み方:**
- `deps:` は直接依存 (`add_deps`) を表す。
- umios (L4) は umicore (L1) と umiport (L2) に直接依存するが、L3 には依存しない。
- L3 ライブラリ (umidsp, umidi, umiusb) は全て umicore (L1) に直接依存する。L2 には依存しない。umios とも独立しており、Application (L5) で初めて結合される。
- L0 は開発支援ツールであり、プロダクション依存グラフには参加しない。テスト時に各ライブラリの `tests/xmake.lua` から参照されるが、ライブラリ本体からの依存は禁止される。レイヤー番号の「0」は「最も基盤的」ではなく「プロダクション外」を意味する。

**L3 内の依存方向:** umiusb → umidsp（ASRC のため）。umidsp と umidi は互いに独立。

### 4.2 依存関係マトリクス（プロダクション依存のみ）

L0 (umitest, umibench, umirtm) はプロダクション依存グラフに参加しないため本マトリクスから除外する。

```
              core  hal  mmio  port  device  dsp  midi  usb  os
umicore        —    ×     ×     ×      ×      ×    ×     ×    ×
umihal         ×    —     ×     ×      ×      ×    ×     ×    ×
umimmio        ×    ×     —     ×      ×      ×    ×     ×    ×
umiport        ×    ○     ○     —      ×      ×    ×     ×    ×
umidevice      ×    ○     ○     ×      —      ×    ×     ×    ×
umidsp         ○    ×     ×     ×      ×      —    ×     ×    ×
umidi          ○    ×     ×     ×      ×      ×    —     ×    ×
umiusb         ○    ×     ×     ×      ×      ○    ×     —    ×
umios          ○    ×     ×     ○      ×      ×    ×     ×    —

○ = 直接依存あり   × = 依存なし
```

**テスト依存許可表（L0）:**

| L0 ライブラリ | プロダクション依存 | テスト時に依存されるライブラリ |
|--------------|:-----------------:|---------------------------|
| umitest | なし | 各ライブラリの `tests/xmake.lua` から参照 |
| umibench | なし | 各ライブラリの `tests/xmake.lua` から参照 |
| umirtm | なし | 各ライブラリの `tests/xmake.lua` から参照 |

> L0 への `add_deps` はテストターゲット (`tests/xmake.lua`) 内に限定し、ライブラリ本体の `xmake.lua` から参照してはならない。

**注記:**
- **間接依存:** umios は umiport 経由で umihal, umimmio に間接依存するが、直接 `add_deps` はしない。同様に umiusb は umidsp 経由で umicore に間接依存する。マトリクスは `add_deps` に記載される直接依存のみを表す。
- **L3 内依存:** umiusb → umidsp は同一レイヤー内の許可された依存（ASRC 機能のため）。umidsp と umidi は互いに独立。

### 4.3 依存ルール

1. **L0 (test, bench, rtm) はプロダクション依存禁止** — `add_deps` は `{private = true}` + テストターゲット限定
2. **循環依存は絶対禁止** — グラフは DAG でなければならない
3. **umicore は依存ゼロ** — すべての基盤となる唯一の依存元
4. **umihal は依存ゼロ** — Concept 定義のみ。実装は umiport が持つ
5. **umimmio は依存ゼロ** — レジスタアクセス基盤
6. **umios は umicore + umiport に依存するが、L3 (umidsp/umidi/umiusb) には依存しない** — L3 と L4 は独立であり、Application 層 (L5) で初めて結合する

---

## 5. ライブラリ詳細設計

### L0: Infrastructure (開発支援ツール)

#### umitest — テストフレームワーク

```
lib/umitest/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umitest/
│   ├── test.hh              # umbrella header
│   ├── suite.hh             # Suite クラス
│   ├── context.hh           # TestContext (assert)
│   └── format.hh            # 値フォーマット
├── tests/
├── examples/
└── platforms/
    ├── host/
    └── wasm/
```

- **責務:** ベアメタル・WASM 対応の軽量テストフレームワーク
- **名前空間:** `umi::test`
- **依存:** なし

#### umibench — ベンチマークフレームワーク

- **責務:** サイクル精度のベンチマーク測定
- **名前空間:** `umi::bench`
- **依存:** なし

#### umirtm — リアルタイムモニタ

- **責務:** RTT/SWO ベースのデバッグ出力
- **名前空間:** `umi::rtm`
- **依存:** なし

---

### L1: Foundation (型・概念・基盤)

#### umicore — コア型・概念定義

```
lib/umicore/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umicore/
│   ├── core.hh              # umbrella header
│   ├── types.hh             # sample_t, port_id_t, constants
│   ├── error.hh             # Result<T>, Error enum
│   ├── event.hh             # Event, EventQueue<Cap>
│   ├── audio_context.hh     # AudioContext, StreamConfig
│   ├── processor.hh         # ProcessorLike concept
│   ├── shared_state.hh      # SharedParamState, etc.
│   ├── time.hh              # 時間変換ユーティリティ
│   ├── irq.hh               # 割り込み抽象 (backend-agnostic)
│   └── shell.hh             # シェル基盤ユーティリティ
├── tests/
└── examples/
```

- **責務:** 全プラットフォーム共通の型定義、Concept定義、エラー型
- **名前空間:** `umi::core`, `umi` (ProcessorLike 等のトップレベルConcept)
- **依存:** なし
- **設計判断:**
  - `AudioContext` はここに配置 — DSP や OS に依存せず、型定義のみ
  - `ProcessorLike` concept もここ — 実装は各ライブラリが提供
  - `Event` / `EventQueue` もここ — ルーティングロジックは umios に
  - `irq.hh` は既存 `lib/umi/core/irq.hh` を **移設** する。現行ファイルは backend-agnostic なインターフェース定義（`umi::irq::Handler`, `init()`, `set_handler()` 等）を持つ。移設時に API 差分を整理し、MCU 固有の実装（`lib/umi/port/common/irq.cc`）は umiport に残す
  - **irq.hh と PAL の関係:** umicore の `irq.hh` は backend-agnostic な割り込みインターフェースを定義し、PAL カテゴリ C4 (割り込みベクター) は MCU 固有の `IRQn` enum とベクターテーブルを生成する。この 2 層構造により、ドライバは umicore の抽象インターフェースのみに依存し、MCU 固有の割り込み番号は PAL 経由で解決する

#### umihal — HAL Concept 定義

```
lib/umihal/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umihal/
│   ├── hal.hh                # umbrella header
│   ├── arch.hh
│   ├── audio.hh
│   ├── board.hh
│   ├── codec.hh
│   ├── fault.hh
│   ├── gpio.hh
│   ├── i2c.hh
│   ├── i2s.hh
│   ├── interrupt.hh
│   ├── result.hh
│   ├── timer.hh
│   ├── uart.hh
│   └── concept/
│       ├── clock.hh
│       ├── codec.hh
│       ├── platform.hh
│       ├── transport.hh
│       ├── uart.hh
│       └── usb.hh            # USB HAL Concept (UsbDeviceLike 等)
├── tests/
│   ├── test_concepts.cc
│   └── compile_fail/
└── examples/
```

- **責務:** ハードウェア抽象化の Concept 定義のみ（実装なし）
- **名前空間:** `umi::hal`
- **依存:** なし
- **設計判断:** USB HAL Concept (`UsbDeviceLike`, `UsbEndpointLike` 等) もここに定義する。MCU 固有の USB 実装（STM32 OTG FS 等）は umiport が提供し、umiusb（L3）はプロトコル層のみを担当する。

#### umimmio — MMIO レジスタアクセス

- **責務:** メモリマップドI/O レジスタへの型安全なアクセス
- **名前空間:** `umi::mmio`
- **依存:** なし
- **PAL 生成基盤としての役割:** umimmio が提供する `Device<>`, `Register<>`, `Field<>` テンプレートは、PAL コード生成パイプライン（§6.4）の出力先として機能する。PAL は SVD データから umimmio テンプレートのインスタンス化コードを自動生成する:

```cpp
// umimmio が提供するテンプレート (lib/umimmio/)
template <mm::Addr BaseAddr>
struct Device { ... };

template <typename Parent, uint32_t Offset, uint32_t Bits, typename Access, uint32_t Reset>
struct Register { ... };

// PAL が生成するインスタンス (lib/umiport/include/umiport/pal/)
template <mm::Addr BaseAddr>
struct GPIOx : mm::Device<mm::RW, mm::DirectTransportTag> {
    struct MODER : mm::Register<GPIOx, 0x00, 32, mm::RW, 0xA800'0000> {
        struct MODER0 : mm::Field<MODER, 0, 2> {};
    };
};
using GPIOA = GPIOx<0x4002'0000>;
```

---

### L2: Platform (ハードウェア抽象化)

#### umiport — ハードウェアポーティングキット

umiport は単なるドライバ集ではなく、**ハードウェアポーティングキット全体**を包含する:

| 構成要素 | 内容 | 種別 |
|---------|------|:----:|
| **PAL** | レジスタアクセス定義 (umimmio テンプレートのインスタンス化) | 生成 / 手書き |
| **ドライバ** | HAL Concept を充足する MCU 固有実装 (PAL を使用) | 手書き |
| **MCU データベース** | MCU スペック (メモリ、クロック、ペリフェラル一覧) | Lua |
| **ボード定義** | ボード設定 (HSE、ピン、ペリフェラル選択) | Lua + C++ (生成) |
| **コード生成ツール** | SVD → umimmio C++ の自動生成パイプライン | Python |
| **xmake ルール** | ボード選択・MCU DB 解決・PAL 生成タスク | Lua |
| **ビルド成果物** | startup.cc, memory.ld, clock_config.hh 等 | 生成 |

```
lib/umiport/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
│
├── include/umiport/
│   ├── port.hh                          # umbrella header
│   │
│   ├── pal/                             # PAL 生成物ルート (§6.4 参照)
│   │   ├── arch/arm/cortex_m/           #   L1: アーキテクチャ共通
│   │   │   ├── nvic.hh                  #     仕様固定 (手書き or 生成)
│   │   │   ├── scb.hh
│   │   │   └── systick.hh
│   │   ├── core/cortex_m4f/             #   L2: コアプロファイル固有
│   │   │   ├── dwt.hh                   #     DWT, FPU, MPU 等
│   │   │   └── fpu.hh
│   │   └── mcu/stm32f4/                #   L3: MCU ファミリ固有
│   │       └── periph/                  #     SVD から生成
│   │           ├── gpio.hh
│   │           ├── rcc.hh
│   │           ├── i2s.hh
│   │           └── usart.hh
│   │
│   ├── driver/                          # 手書きドライバ (PAL を #include)
│   │   ├── arch/arm/cortex_m/           #   アーキテクチャ共通ドライバ
│   │   │   └── dwt.hh                   #     サイクルカウンタ
│   │   └── mcu/stm32f4/                #   MCU 固有ドライバ
│   │       ├── gpio_driver.hh           #     GpioLike concept を充足
│   │       ├── uart_driver.hh           #     UartLike concept を充足
│   │       ├── i2s_driver.hh
│   │       ├── system_init.hh           #     クロック初期化 (PLL パラメータは board.lua から生成)
│   │       └── usb_otg.hh              #     USB OTG FS (UsbDeviceLike を充足)
│   │
│   ├── board/                           # ボード定義 (Lua + C++ 二重構造)
│   │   ├── host/                        #   ホスト環境
│   │   │   └── platform.hh
│   │   ├── wasm/                        #   WASM 環境
│   │   │   └── platform.hh
│   │   └── stm32f4_disco/              #   組込みボード
│   │       ├── platform.hh              #     生成: HAL Concept 充足型
│   │       ├── board.hh                 #     生成: constexpr 定数 (HSE, sysclk 等)
│   │       ├── pin_config.hh            #     生成: GPIO AF テーブル
│   │       └── clock_config.hh          #     生成: PLL M/N/P/Q (自動探索)
│   │
│   ├── platform/                        # 実行環境固有コード
│   │   ├── embedded/                    #   組込みカーネル環境 (syscall, protection)
│   │   └── wasm/                        #   WASM 実行環境
│   │
│   └── common/                          # 共通ユーティリティ
│       └── irq.hh                       #   MCU 固有割り込みディスパッチ
│
├── src/
│   ├── arch/cm4/handlers.cc
│   ├── common/irq.cc
│   ├── mcu/stm32f4/syscalls.cc
│   ├── host/write.cc
│   └── wasm/write.cc
│
├── database/                            # MCU/ボード定義データベース (§7.3 参照)
│   ├── mcu/
│   │   └── stm32f4/
│   │       └── stm32f407vg.lua          #   MCU スペック (メモリ, クロック, ペリフェラル)
│   └── boards/
│       └── stm32f4_disco/
│           └── board.lua                #   ボード設定 (HSE, ピン, 使用ペリフェラル)
│
├── gen/                                 # PAL コード生成ツール (§6.4, §7.3 参照)
│   ├── umipal-gen                       #   Python メインスクリプト
│   ├── parsers/
│   │   ├── svd_parser.py                #   SVD XML パーサー
│   │   └── cmsis_header_parser.py       #   CMSIS ヘッダパーサー
│   ├── models/
│   │   └── device_model.py              #   統一中間表現 (Unified Device Model)
│   ├── templates/
│   │   ├── peripheral.hh.j2             #   ペリフェラルレジスタテンプレート
│   │   ├── vectors.hh.j2               #   割り込みベクターテンプレート
│   │   └── memory.hh.j2                #   メモリマップテンプレート
│   └── data/
│       ├── svd/STM32F407.svd
│       └── patches/stm32f4.yaml         #   SVD パッチ (stm32-rs 形式)
│
├── rules/                               # xmake ビルドルール (§7.3 参照)
│   ├── board.lua                        #   ボード選択 → MCU 特定 → ツールチェイン決定
│   └── pal_generator.lua               #   PAL 再生成タスク定義
│
├── tests/
└── examples/
```

- **責務:** 全プラットフォームのハードウェアポーティング（PAL 生成、ドライバ、データベース、ビルドルール）
- **名前空間:** `umi::port` (ドライバ), `umi::board` (ボード固有), `umi::pal` (PAL 生成物)
- **依存:** umihal (L1), umimmio (L1)
- **設計判断:**
  - **pal/ と driver/ の分離:** pal/ は生成物（または手書きの生成相当品）、driver/ は PAL を使って HAL Concept を充足する手書きコード。この分離により、生成パイプライン導入前でも手書き PAL で動作し、段階的に自動生成に移行できる
  - **database/ の配置:** MCU DB と board.lua は umiport 内に配置する。xmake ビルドシステムと Python 生成ツールの両方から参照される共有データソースであり、umiport が「ハードウェアの真実のソース」として機能する
  - **gen/ の配置:** コード生成ツールは umiport 内に配置する。生成物の出力先 (pal/) と入力データ (database/) が同一ライブラリ内にあるため、ツール・データ・出力の一貫性が保たれる

#### umidevice — デバイスドライバ

- **責務:** オーディオコーデック等の外部デバイスドライバ
- **名前空間:** `umi::device`
- **依存:** umihal (L1), umimmio (L1)

---

### L3: Domain (ドメイン固有ライブラリ)

#### umidsp — DSP アルゴリズム

```
lib/umidsp/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umidsp/
│   ├── dsp.hh               # umbrella header
│   ├── constants.hh         # DSP 定数
│   ├── core/
│   │   ├── phase.hh         # 位相管理
│   │   └── interpolate.hh   # 補間
│   ├── filter/
│   │   ├── biquad.hh        # Biquad (multi-stage)
│   │   ├── svf.hh           # State Variable Filter
│   │   ├── moog.hh          # Moog ラダーフィルタ
│   │   ├── k35.hh           # K35 フィルタ
│   │   └── moving_average.hh
│   ├── synth/
│   │   ├── oscillator.hh    # オシレータ群
│   │   └── envelope.hh      # ADSR エンベロープ
│   └── audio/
│       └── rate/
│           ├── asrc.hh      # 非同期サンプルレート変換
│           └── pi_controller.hh
├── tests/
├── examples/
└── filter_ref/               # リファレンス実装
```

- **責務:** オーディオ信号処理アルゴリズム（フィルタ、シンセシス、レート変換）
- **名前空間:** `umi::dsp`
- **依存:** umicore (L1)

#### umidi — MIDI プロトコル

```
lib/umidi/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umidi/
│   ├── midi.hh              # umbrella header
│   ├── core/
│   │   ├── ump.hh           # UMP32 型、エンコード/デコード
│   │   ├── parser.hh        # インクリメンタルパーサ
│   │   ├── sysex_assembler.hh
│   │   └── timestamp.hh
│   ├── messages/
│   │   ├── channel_voice.hh
│   │   ├── system.hh
│   │   └── sysex.hh
│   ├── cc/
│   │   ├── types.hh         # CC 列挙型
│   │   ├── standards.hh     # 標準 CC 定義
│   │   └── decoder.hh
│   ├── codec/
│   │   └── decoder.hh       # テンプレートベース静的デコーダ
│   └── protocol/
│       ├── umi_sysex.hh     # UMI SysEx プロトコル
│       ├── umi_auth.hh
│       ├── umi_bootloader.hh
│       ├── umi_firmware.hh
│       ├── umi_session.hh
│       ├── umi_state.hh
│       ├── umi_object.hh
│       └── umi_transport.hh
├── tests/
└── examples/
```

- **責務:** MIDI 1.0/2.0 プロトコル処理（パース、エンコード、UMI拡張プロトコル）
- **名前空間:** `umi::midi`
- **依存:** umicore (L1)

#### umiusb — USB デバイススタック

```
lib/umiusb/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umiusb/
│   ├── usb.hh               # umbrella header
│   ├── core/
│   │   ├── types.hh
│   │   ├── device.hh
│   │   └── descriptor.hh
│   ├── audio/
│   │   ├── audio_types.hh
│   │   ├── audio_interface.hh
│   │   └── audio_device.hh
│   └── midi/
│       ├── usb_midi_class.hh
│       └── umidi_adapter.hh
├── tests/
└── examples/
```

- **責務:** USB Audio/MIDI クラスデバイスのプロトコル層実装（HAL 非依存）
- **名前空間:** `umi::usb`
- **依存:** umicore (L1), umidsp (L3 — ASRC のため)
- **設計判断:** USB HAL Concept は umihal に、MCU 固有の USB ドライバ実装（STM32 OTG FS 等）は umiport に配置する。umiusb はプロトコル層のみを担当し、プラットフォーム固有コードを含まない。これにより L3 にプラットフォーム依存が侵入することを防ぎ、他の HAL 実装（GPIO, I2C 等）と同一のパターンで USB 対応を拡張できる。

---

### L4: System (OS / カーネル統合)

#### umios — OS カーネル + サービス

```
lib/umios/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umios/
│   ├── os.hh                # umbrella header
│   │
│   ├── kernel/              # カーネルコア
│   │   ├── kernel.hh        # Kernel<MaxTasks,MaxTimers,HW,MaxCores>
│   │   ├── startup.hh       # Bootstrap, LinkerSymbols
│   │   ├── monitor.hh       # StackMonitor, TaskProfiler
│   │   ├── metrics.hh       # KernelMetrics (opt-in)
│   │   ├── fault.hh         # FaultLog, FaultHandler
│   │   ├── driver.hh        # ドライバフレームワーク
│   │   ├── app_header.hh    # .umia バイナリフォーマット
│   │   ├── syscall.hh       # Syscall 番号・ハンドラ
│   │   └── concepts.hh      # AppLoaderLike 等の内部 Concept
│   │
│   ├── runtime/             # イベントルーティング
│   │   ├── event_router.hh  # EventRouter
│   │   ├── route_table.hh   # RouteTable
│   │   └── param_mapping.hh # ParamMapping, AppConfig
│   │
│   ├── service/             # カーネルサービス
│   │   ├── audio.hh         # AudioService
│   │   ├── midi.hh          # MidiService
│   │   ├── shell.hh         # Shell<HW,Kernel>
│   │   ├── loader.hh        # AppLoader
│   │   └── storage.hh       # StorageService
│   │
│   ├── ipc/                 # プロセス間通信
│   │   ├── spsc_queue.hh    # SpscQueue<T,Cap>
│   │   ├── triple_buffer.hh # TripleBuffer
│   │   └── notification.hh  # Notification<MaxTasks>
│   │
│   ├── app/                 # アプリケーション層
│   │   ├── syscall.hh       # ユーザー空間 syscall API
│   │   └── crt0.hh          # アプリケーション CRT0
│   │
│   └── adapter/             # プラットフォームアダプタ
│       ├── embedded.hh      # 組込みアダプタ
│       ├── wasm.hh          # WASM アダプタ
│       └── umim.hh          # UMIM プラグインアダプタ
│
├── src/
│   ├── service/
│   │   └── loader.cc
│   └── crypto/              # 暗号 (カーネル内部)
│       ├── sha256.cc
│       ├── sha512.cc
│       └── ed25519.cc
│
├── tests/
└── examples/
```

- **責務:** リアルタイムカーネル、サービス、IPC、アプリケーションフレームワーク
- **名前空間:** `umi::os` (kernel), `umi::os::service`, `umi::os::ipc`
- **依存:** umicore (L1), umiport (L2)

##### umios 内部アーキテクチャ

```
umios 内部の依存階層:

kernel/ (コア: スケジューラ, MPU, 割込み)
    ↑
    │  kernel は上位に依存しない (最下層)
    │
ipc/ (共有データ構造: SpscQueue, TripleBuffer)
    ↑
    │  ipc は kernel/service の両方から利用される中立層
    │
runtime/ (イベントルーティング: EventRouter, RouteTable)
    ↑
    │  runtime は kernel API を使用
    │
service/ (各サービス: Audio, MIDI, Shell, Loader, Storage)
    ↑
    │  service は kernel + runtime + ipc を使用
    │
adapter/ (プラットフォームアダプタ: embedded, wasm, umim)
    │  adapter は全層を結合し、エントリポイントを提供
```

kernel は service に依存しない。service が kernel を使用する。この方向を維持するため:
- SyscallHandler はテンプレートパラメータで StorageType, LoaderType を注入
- AppLoaderLike 等の Concept を kernel/concepts.hh に定義し、実装は service/ に配置

##### umios xmake ターゲット

umios 内部の依存方向（kernel → service の逆依存禁止）を**ビルドシステムレベルで強制する**ため、xmake ターゲットを内部層ごとに分割する:

```lua
-- lib/umios/xmake.lua

-- カーネルコア: umicore のみに依存（umiport にも依存しない）
target("umios.kernel")
    set_kind("headeronly")
    add_deps("umicore")
    add_headerfiles("include/(umios/kernel/**.hh)", "include/(umios/ipc/**.hh)")
    add_includedirs("include", { public = true })
target_end()

-- サービス層: kernel + umicore + umiport に依存
target("umios.runtime")
    set_kind("headeronly")
    add_deps("umios.kernel", "umicore")
    add_headerfiles("include/(umios/runtime/**.hh)", "include/(umios/service/**.hh)", "include/(umios/app/**.hh)")
    add_includedirs("include", { public = true })
target_end()

-- アダプタ層: 全層を結合（umiport が必要）
target("umios.adapter")
    set_kind("headeronly")
    add_deps("umios.runtime", "umiport")
    add_headerfiles("include/(umios/adapter/**.hh)")
    add_includedirs("include", { public = true })
target_end()

-- 統合ターゲット: 全層を束ねる便利ターゲット
target("umios")
    set_kind("headeronly")
    add_deps("umios.kernel", "umios.runtime", "umios.adapter")
target_end()

-- サービスローダー (static: .cc ファイルを含む)
target("umios.loader")
    set_kind("static")
    add_deps("umios.runtime")
    add_files("src/service/loader.cc")
target_end()

-- 暗号ライブラリ (static: .cc ファイルを含む)
target("umios.crypto")
    set_kind("static")
    add_deps("umios.kernel")
    add_files("src/crypto/sha256.cc", "src/crypto/sha512.cc", "src/crypto/ed25519.cc")
target_end()
```

> **設計判断:** `umios.kernel` は `umicore` のみに依存し、`umiport` には依存しない。これにより kernel/ から service/ や adapter/ への `#include` は**コンパイルエラー**になる。内部アーキテクチャの依存方向がコードレビューではなくビルドシステムで保証される。
>
> `loader.cc` はリンク時にアプリケーションターゲットから参照される実体を含むため、ヘッダオンリーではなく static ターゲットとして分離する。アプリケーションは `add_deps("umios.loader")` で明示的にリンクする。

---

## 6. プラットフォーム抽象化パターン

### 6.1 標準ディレクトリ構造

プラットフォーム固有のコードを持つライブラリは、以下の統一パターンに従う:

```
lib/<libname>/
├── include/<libname>/        # プラットフォーム非依存ヘッダ
├── platforms/
│   ├── common/               # 共通基底・トレイト
│   │   └── platform_base.hh
│   ├── host/
│   │   └── <libname>/
│   │       └── platform.hh
│   ├── wasm/
│   │   └── <libname>/
│   │       └── platform.hh
│   └── embedded/
│       └── <libname>/
│           └── platform.hh
└── xmake.lua                 # プラットフォーム選択ロジック
```

> **標準仕様との差分:** `lib/docs/standards/LIBRARY_SPEC.md` v2.0.0 では `platforms/arm/cortex-m/<board>/` を使用しているが、これは umiport 固有の構造（§6.3）に対応した表記である。一般ライブラリでは上記の `platforms/embedded/` を使用する。umiport のみが `pal/`, `driver/`, `board/`, `platform/` の種別軸と `arch/core/mcu` のスコープ軸を持つ特殊構造を取る。

### 6.2 プラットフォーム選択メカニズム

```lua
-- xmake.lua での標準パターン
if is_plat("wasm") then
    add_includedirs("platforms/wasm", { public = true })
elseif is_plat("cross") then
    add_includedirs("platforms/embedded", { public = true })
else
    add_includedirs("platforms/host", { public = true })
end
```

### 6.3 umiport の特殊構造

umiport のみ、ハードウェアの階層に対応した特殊構造を持つ。主要な分類軸は以下の2つ:

**スコープ軸 (PAL/ドライバ共通):**

```
arch/     → CPU アーキテクチャ (arm/cortex_m)
core/     → コアプロファイル (cortex_m4f, cortex_m7)
mcu/      → MCU ファミリ (stm32f4, stm32h7)
```

**種別軸:**

```
pal/      → 生成物: umimmio テンプレートのインスタンス化 (レジスタ定義)
driver/   → 手書き: PAL を使って HAL Concept を充足するドライバ実装
board/    → ボード固有定義 (Lua + C++ 二重構造)
platform/ → 実行環境固有 (embedded, wasm)
```

これらは xmake の `board.lua` ルール (§7.3) で動的に選択される。

### 6.4 HAL Concept → PAL → ドライバの 3 層パターン

組込みプラットフォームにおけるハードウェア抽象化は、以下の 3 層で構成される:

```
Layer 1: HAL Concept (umihal)
    定義のみ。実装なし。
    例: GpioLike concept — set_mode(), write(), read()

         │ concept を充足
         ▼

Layer 3: ドライバ (umiport/driver/)
    手書き。PAL レジスタ定義を使って HAL Concept を実装。
    例: GpioDriver<GPIOA> — PAL の MODER, ODR, BSRR を操作して GpioLike を充足

         │ PAL を #include
         ▼

Layer 2: PAL 生成物 (umiport/pal/)
    生成 (or 手書き)。umimmio テンプレートのインスタンス化。
    例: GPIOx<0x4002'0000>::MODER::MODER0 — SVD から自動生成
```

**具体例:**

```cpp
// [Layer 1] umihal: Concept 定義 (lib/umihal/)
template <typename T>
concept GpioLike = requires(T gpio, uint8_t pin, GpioMode mode) {
    gpio.set_mode(pin, mode);
    gpio.write(pin, true);
    { gpio.read(pin) } -> std::same_as<bool>;
};

// [Layer 2] PAL: レジスタ定義 (lib/umiport/include/umiport/pal/mcu/stm32f4/)
//   SVD から自動生成、または手書き
template <mm::Addr BaseAddr>
struct GPIOx : mm::Device<mm::RW, mm::DirectTransportTag> {
    struct MODER : mm::Register<GPIOx, 0x00, 32, mm::RW, 0xA800'0000> { ... };
    struct ODR   : mm::Register<GPIOx, 0x14, 32, mm::RW, 0x0000'0000> { ... };
    struct BSRR  : mm::Register<GPIOx, 0x18, 32, mm::WO, 0x0000'0000> { ... };
};
using GPIOA = GPIOx<0x4002'0000>;

// [Layer 3] ドライバ: HAL Concept を充足 (lib/umiport/include/umiport/driver/mcu/stm32f4/)
template <typename GpioRegs>
struct GpioDriver {
    constexpr void set_mode(uint8_t pin, GpioMode mode) {
        GpioRegs::MODER::write_field(pin * 2, 2, static_cast<uint32_t>(mode));
    }
    constexpr void write(uint8_t pin, bool value) { ... }
    constexpr bool read(uint8_t pin) { ... }
};
static_assert(GpioLike<GpioDriver<GPIOA>>);  // Concept 充足を保証
```

**この 3 層分離の利点:**
- PAL は生成パイプラインの導入前でも手書きで動作する
- ドライバは PAL の実装方法（手書き / 生成）に依存しない
- HAL Concept は全プラットフォームで共通であり、ドライバの差し替えが型安全

### 6.5 board.lua → C++ 生成チェーン

ボード定義は **Lua (データ定義) + C++ (コンパイル時定数)** の二重構造を持つ:

```
database/boards/stm32f4_disco/board.lua    (入力: ボード設定)
database/mcu/stm32f4/stm32f407vg.lua       (入力: MCU スペック)
        │
        │  xmake rules/board.lua が解決
        ▼
include/umiport/board/stm32f4_disco/
├── board.hh            ← constexpr 定数 (HSE_FREQ, SYSCLK 等)
├── platform.hh         ← HAL Concept 充足型の typedef
├── clock_config.hh     ← PLL M/N/P/Q (HSE + target sysclk から自動探索)
├── pin_config.hh       ← GPIO AF テーブル
└── memory.ld           ← リンカスクリプト (FLASH/RAM origin, length)
```

**PLL 自動探索:** board.lua に `hse_freq` と `target_sysclk` を指定すると、MCU DB のクロック制約から有効な PLL M/N/P/Q パラメータを自動探索し `clock_config.hh` に出力する。

**生成タイミング:** board 定義の C++ ファイルは `xmake build` 時に自動生成される（PAL コード生成とは異なり、ビルドの一部として実行）。

---

## 7. xmake ビルドシステム

### 7.1 ルート xmake.lua

```lua
set_project("umi")
set_version("0.3.0")
set_xmakever("2.8.0")

-- L0: Infrastructure
includes("lib/umitest")
includes("lib/umibench")
includes("lib/umirtm")

-- L1: Foundation
includes("lib/umicore")
includes("lib/umihal")
includes("lib/umimmio")

-- L2: Platform
includes("lib/umiport")
includes("lib/umidevice")

-- L3: Domain
includes("lib/umidsp")
includes("lib/umidi")
includes("lib/umiusb")

-- L4: System
includes("lib/umios")

-- Bundles
includes("lib/umi")

-- Applications
includes("examples/stm32f4_os")
includes("examples/headless_webhost")

-- Tools
includes("tools/release.lua")
includes("tools/dev-sync.lua")
```

### 7.2 各ライブラリの xmake.lua パターン

```lua
-- 標準パターン (headeronly)
target("<libname>")
    set_kind("headeronly")
    add_headerfiles("include/(<libname>/**.hh)")
    add_includedirs("include", { public = true })
    add_deps("<dependency1>", "<dependency2>")  -- 必要な場合のみ
target_end()

-- テスト (別ファイル推奨)
includes("tests")
```

### 7.3 umiport のビルド統合

umiport は通常のライブラリビルドに加え、MCU データベースの解決と PAL コード生成の2つの追加ビルドメカニズムを持つ。

#### 7.3.1 MCU DB 解決チェーン

ボード選択から MCU スペック、ツールチェイン決定までの解決フロー:

```
ユーザー指定: --board=stm32f4_disco
     │
     ▼
database/boards/stm32f4_disco/board.lua    ← ボード定義
     │  mcu = "stm32f407vg"
     ▼
database/mcu/stm32f4/stm32f407vg.lua       ← MCU スペック
     │  core = "cortex-m4f"
     │  vendor = "st"
     │  family = "stm32f4"
     ▼
rules/board.lua                            ← xmake ルール
     ├── ツールチェイン → arm-none-eabi-gcc
     ├── includedirs   → pal/mcu/stm32f4/, driver/mcu/stm32f4/, board/stm32f4_disco/
     ├── memory.ld     → database から生成
     └── defines       → HSE_VALUE, SYSCLK 等
```

**MCU DB スキーマ (Lua):**

```lua
-- database/mcu/stm32f4/stm32f407vg.lua
return {
    core     = "cortex-m4f",
    vendor   = "st",
    family   = "stm32f4",
    memory   = {
        FLASH  = { origin = 0x08000000, length = "1M"  },
        SRAM   = { origin = 0x20000000, length = "128K" },
        CCM    = { origin = 0x10000000, length = "64K"  },
    },
    clock    = {
        hse_range  = { 4e6, 26e6 },
        sysclk_max = 168e6,
        pll_src    = { "HSI", "HSE" },
    },
    svd_file = "STM32F407.svd",
}
```

**スキーマバリデーション:**

`rules/board.lua` は MCU DB と board.lua の読み込み時に必須フィールドを検証する。不足・型不一致はビルドエラーとする:

```lua
-- rules/validate.lua (board.lua から呼び出し)
local required_mcu_fields = {
    { "core",     "string" },
    { "vendor",   "string" },
    { "family",   "string" },
    { "memory",   "table"  },
    { "clock",    "table"  },
}

local required_memory_fields = {
    { "origin", "number" },
    { "length", "string" },
}

function validate_mcu_db(mcu, path)
    for _, field in ipairs(required_mcu_fields) do
        local name, expected_type = field[1], field[2]
        assert(mcu[name] ~= nil,
            format("MCU DB '%s': missing required field '%s'", path, name))
        assert(type(mcu[name]) == expected_type,
            format("MCU DB '%s': field '%s' must be %s, got %s",
                   path, name, expected_type, type(mcu[name])))
    end
    -- memory 各リージョンの検証
    for region, spec in pairs(mcu.memory) do
        for _, field in ipairs(required_memory_fields) do
            assert(spec[field[1]] ~= nil,
                format("MCU DB '%s': memory.%s missing '%s'", path, region, field[1]))
        end
    end
end
```

これにより、新しい MCU やボードを追加する際にフィールド忘れが**ビルド時に即座に検出**される。

#### 7.3.2 PAL コード生成タスク

PAL 生成は通常ビルドとは**分離された明示的タスク**として実行する:

```bash
# PAL 生成 (ソースツリーに出力、git 管理下)
xmake pal-gen --family stm32f4

# 通常ビルド (生成済み PAL ヘッダを使用)
xmake build stm32f4_os
```

**分離の理由:**
- PAL 生成は SVD ファイルと Python ツールに依存する（通常ビルドとは異なるツールチェイン）
- 生成物は git 管理下に置き、レビュー可能にする（差分ベース更新）
- 開発者は PAL を再生成せずにドライバ開発を進められる

```lua
-- rules/pal_generator.lua (xmake タスク定義)
task("pal-gen")
    set_menu({
        usage = "xmake pal-gen [options]",
        description = "Generate PAL headers from SVD/CMSIS data",
        options = {
            {nil, "family", "v", nil, "MCU family (e.g., stm32f4)"},
            {nil, "dry-run", "k", nil, "Show what would be generated"},
        }
    })
    on_run(function ()
        -- gen/umipal-gen を呼び出し
        -- 出力先: include/umiport/pal/mcu/<family>/periph/
        os.exec("python3 gen/umipal-gen --family %s", option.get("family"))
    end)
task_end()
```

#### 7.3.3 ボード定義の C++ 生成

board.lua → C++ ヘッダの生成はビルド時に自動実行される（PAL 生成とは異なり `xmake build` の一部）:

```lua
-- rules/board.lua (ビルド時生成)
rule("umiport.board")
    on_load(function (target)
        local board = target:values("umiport.board")
        local board_lua = path.join("database/boards", board, "board.lua")
        local mcu_lua   = resolve_mcu(board_lua)  -- MCU DB からスペック取得

        -- board.hh, platform.hh, clock_config.hh, memory.ld を生成
        generate_board_headers(target, board_lua, mcu_lua)
    end)
rule_end()
```

### 7.4 便利バンドル

```lua
-- lib/umi/xmake.lua
target("umi.base")
    set_kind("headeronly")
    add_deps("umicore", "umidsp", "umidi")
target_end()

target("umi.wasm.full")
    set_kind("headeronly")
    add_deps("umicore", "umidsp", "umidi")
target_end()

target("umi.embedded.full")
    set_kind("headeronly")
    add_deps("umicore", "umidsp", "umidi", "umiusb", "umios")
target_end()
```

---

## 8. 品質基準

### 8.1 全ライブラリ共通（UMI Strict Profile）

本プロジェクトは LIBRARY_SPEC v2.0.0 を基盤としつつ、以下の **UMI Strict Profile** を上乗せする。
v2.0.0 で任意とされている項目のうち、UMI では必須に格上げしたものを明示する。

| 項目 | v2.0.0 | UMI Strict Profile | 差分理由 |
|------|:------:|:-----------------:|---------|
| README.md | 必須 | 必須 | — |
| DESIGN.md | 必須 | 必須（11セクション） | UMI 標準テンプレートに拡張 |
| INDEX.md | — | **必須** | API リファレンスマップとして全ライブラリに要求 |
| TESTING.md | — | **必須** | テスト戦略・品質ゲートの明文化 |
| docs/ja/ | **任意** | **必須** | 日英バイリンガル開発チーム対応 |
| Doxyfile | 必須 | 必須 | — |
| compile_fail/ | **任意** | **条件付き必須** | Concept 定義ライブラリのみ必須（§8.2 参照） |
| examples/ | 任意 | **必須** | 最低1つの minimal example |

> **正本関係:** `lib/docs/standards/LIBRARY_SPEC.md` が v2.0.0 の正本。本文書の UMI Strict Profile は v2.0.0 を拡張するものであり、矛盾する場合は本文書（UMI Strict Profile）が優先する。

### 8.2 テストレベル

| レベル | 対象 | 要件 |
|--------|------|------|
| **ユニットテスト** | 全ライブラリ | 公開 API の主要パスをカバー |
| **compile-fail テスト** | Concept を定義するライブラリ (umihal, umimmio, umitest 等) | 不正な型がコンパイルエラーになることを検証 |
| **統合テスト** | L2 以上のライブラリ (umiport, umidevice, umios) | レイヤーを跨ぐ最低1つの統合シナリオ |

**統合テスト例:**
- umiport + umidevice: I2C 経由でのオーディオコーデック初期化シーケンス
- umios + umiport: カーネル起動 → SysTick → タスクスケジュール
- umiusb + umidsp: USB Audio ストリーム + ASRC パイプライン

### 8.3 目標スコア

全ライブラリが **★★★★☆ (4/5) 以上** を達成すること。

---

## 9. 検証基準

### 9.1 構造的正当性

- [ ] 全ライブラリが lib/ 直下にスタンドアロンとして存在する
- [ ] lib/umi/ にはバンドル定義のみ残る
- [ ] 依存関係グラフが DAG である（循環なし）
- [ ] レイヤー規則（下位のみ依存）が守られている
- [ ] 全ライブラリが `xmake test` で独立テスト可能

### 9.2 ドキュメント完成度

- [ ] 全ライブラリに README.md, DESIGN.md, INDEX.md, TESTING.md が存在する
- [ ] docs/ja/ が全ライブラリに存在する
- [ ] 全ライブラリに Doxyfile が存在する

### 9.3 テスト完成度

- [ ] 全ライブラリにユニットテストが存在する
- [ ] Concept 定義ライブラリ (umihal, umimmio, umitest) に compile-fail テストが存在する
- [ ] L2 以上のライブラリに最低1つの統合テストシナリオが存在する

### 9.4 レイヤー規則検証

- [ ] 各ライブラリの `add_deps` が §4.2 マトリクスと一致する
- [ ] L0 ライブラリへの `add_deps` がテストターゲット外に存在しない
- [ ] L3 内の依存方向が umiusb → umidsp のみ（逆方向なし）

### 9.5 PAL / コード生成検証

- [ ] `xmake pal-gen --family stm32f4` が正常完了する
- [ ] 生成された PAL ヘッダが umimmio テンプレート (`Device<>`, `Register<>`, `Field<>`) を正しくインスタンス化する
- [ ] PAL 生成物と手書きドライバの分離が維持されている（pal/ 内にドライバロジックが混入していない）
- [ ] MCU DB (`database/mcu/`) のスキーマが §7.3.1 に準拠する
- [ ] `--board=<board>` 指定でビルドが完結する（MCU → arch → ツールチェイン自動解決）
- [ ] board.lua → C++ 生成チェーン（board.hh, clock_config.hh, memory.ld）が正常動作する
- [ ] 生成物が git 管理下にあり、差分レビュー可能である

---

## 10. 設計判断の根拠

§1.2 の分割基準 4 条件に対する各設計判断の照合:

| 判断 | 独立デプロイ | 責務境界 | 安定API | 再利用性 | 結論 |
|------|:----------:|:-------:|:------:|:-------:|------|
| synth を dsp に吸収 | ✗ (2ファイル、単独では不足) | △ (DSP と密結合) | ○ | △ | **吸収**: 分割しない基準「3ファイル以下」に該当 |
| crypto を umios 内部に | ✗ (カーネル署名検証専用) | ○ | ○ | △ (汎用だが UMI 固有用途) | **内部**: 常にカーネルと一緒に変更される |
| fs を umios 内部に | ✗ (StorageService と密結合) | ○ | △ | △ | **内部**: 単独では意味をなさない。ただし WASM 環境で非カーネルファイルシステム利用が必要になった場合は再分離を検討 |
| shell を umicore に吸収 | ✗ (2ファイル) | △ (基盤ユーティリティ) | ○ | △ | **吸収**: 分割しない基準「3ファイル以下」に該当 |
| umiport に umi::board 名前空間 | — | ○ (arch/mcu と board の責務分離) | ○ | — | **採用**: ボード固有定義は port 層の一部だが名前空間で区別 |

### 10.1 ADR: umios 内部ターゲット分割

**背景:** 初期調査（archive/INVESTIGATION.md, archive/ANALYSIS.md）では「kernel は umicore のみに依存」を高評価した。v1.3.0 ではコードレビューによる依存方向の担保を想定していたが、レビューでは**設計意図がコードとして表現されない**ため、ビルドシステムレベルでの強制に方針を変更した。

**検討した案:**

| 案 | 構造 | 利点 | 欠点 |
|---|------|------|------|
| **A: 単一ターゲット** | umios → umicore + umiport | ターゲット構成が単純 | kernel→service の逆依存をコードレビューでしか防げない |
| **B: 内部分割（採用）** | umios.kernel → umicore のみ、umios.adapter → umiport | 依存方向がコンパイルエラーで保証される、kernel 単体テストが容易 | xmake ターゲットが増加（6ターゲット） |

**採用理由:**
1. kernel → service の逆依存は「組込みリアルタイムOS として致命的な設計欠陥」であり、コードレビューではなくビルドで防ぐべき
2. `umios.kernel` は `umicore` のみに依存するため、umiport のスタブなしで kernel 単体テストが可能
3. `umios.adapter` のみが `umiport` に依存する構造は、プラットフォームポーティングの範囲を明確にする
4. ターゲット数の増加（3→6）は、依存方向の保証に対して十分に小さいコスト

---

## 11. 将来拡張候補

### 11.1 ライブラリ昇格候補

以下のモジュールは現時点では12ライブラリ構成に含めないが、将来的にスタンドアロンライブラリへの昇格を検討する。

| モジュール | 現行パス | 候補名 | 想定レイヤー | 昇格条件 |
|-----------|---------|--------|:----------:|---------|
| コルーチン | lib/umi/coro/ | — | — | 使用実績を確認。未使用ならアーカイブ。使用中なら umicore に吸収（1ファイル510行） |
| グラフィックス | lib/umi/gfx/ + lib/umi/ui/ | umigfx | L3 (Domain) | UI/描画の需要が確定し、12ファイル1,159行を超える規模に成長した時点 |

**移行期間中の扱い:**
- coro: lib/umi/coro/ に保持。IMPLEMENTATION_PLAN Phase 4 で判断
- gfx/ui: lib/umi/gfx/ および lib/umi/ui/ に保持。移行対象外

### 11.2 PAL コード生成の段階的拡張

PAL コード生成パイプライン (§6.4, §7.3.2) は初期リリースでは最小スコープ (C4, C5, C6) に限定する。以下のフェーズで段階的に拡張する:

| フェーズ | カテゴリ | 内容 | 前提条件 |
|---------|---------|------|---------|
| **PAL Phase 1** (初期) | C4, C5, C6 | 割り込みベクター、メモリマップ、ペリフェラルレジスタ | SVD パーサー + CMSIS ヘッダパーサー |
| **PAL Phase 2** | C7 + C8 | GPIO MUX (AF テーブル) + クロックツリー | Open Pin Data パーサー完成 |
| **PAL Phase 3** | C9〜C14 | DMA チャネル、電力管理、セキュリティ等 | 主要ドライバが安定、データソース整備 |
| **PAL Phase 4** | — | RP2040 対応 | ARM 以外のアーキテクチャ対応 |
| **PAL Phase 5** | — | ESP32 対応 (RISC-V + Xtensa) | マルチ ISA 対応の gen/ 拡張 |

**カテゴリ参照:** PAL カテゴリ C1〜C14 の詳細は `lib/docs/design/pal/02_CATEGORY_INDEX.md` を参照。

---

## 用語集

| 用語 | 定義 |
|------|------|
| **モジュール** | lib/umi/ 配下のディレクトリ単位 (例: lib/umi/dsp/) |
| **ライブラリ** | lib/ 直下のスタンドアロン単位。LIBRARY_SPEC v2.0.0 に準拠する (例: lib/umidsp/) |
| **バンドル** | 複数ライブラリをまとめて依存追加する便利ターゲット (例: umi.base) |
| **DAG** | 有向非巡回グラフ。ライブラリ依存関係の循環を禁止する制約 |
| **PAL** | Peripheral Access Layer。umimmio テンプレートのインスタンス化によるレジスタアクセス定義。SVD データから自動生成、または手書き。`umiport/include/umiport/pal/` に配置 |
| **MCU DB** | MCU データベース。MCU スペック (メモリ, クロック, ペリフェラル) を Lua テーブルで定義したファイル群。`umiport/database/mcu/` に配置 |
| **Unified Device Model** | PAL コード生成パイプラインの中間表現。SVD/CMSIS 等の異なるデータソースを統一的に表現する Python データ構造 |
| **board.lua** | ボード定義ファイル (Lua)。MCU 選択、HSE 周波数、ピン割り当て等を記述。xmake ビルドルールと C++ ヘッダ生成の入力となる |
| **SVD** | System View Description。ARM が定義する MCU ペリフェラル記述 XML フォーマット。PAL 生成の主要入力 |
