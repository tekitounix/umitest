# ランタイム初期化シーケンス設計

**ステータス:** 設計中  **策定日:** 2026-02-10
**根拠:** [pal/categories/C13_LINKER_STARTUP.md](pal/categories/C13_LINKER_STARTUP.md), [hal/concept_design.md](hal/concept_design.md)

**関連文書:**
- [pal/categories/C13_LINKER_STARTUP.md](pal/categories/C13_LINKER_STARTUP.md) — リンカスクリプト・スタートアップの PAL カテゴリ
- [pal/categories/C08_CLOCK_TREE.md](pal/categories/C08_CLOCK_TREE.md) — クロックツリー定義
- [hal/concept_design.md](hal/concept_design.md) — HAL Concept (Platform の init() 契約)
- [interrupt_model.md](interrupt_model.md) — 割り込みモデル
- [board/architecture.md](board/architecture.md) — board.hh / platform.hh の構造

---

## 1. 目的

[C13_LINKER_STARTUP.md](pal/categories/C13_LINKER_STARTUP.md) はリンカスクリプトとスタートアップコードの **データ定義** を扱う。
本文書は **Reset_Handler からアプリケーション main() までの実行フロー全体** を設計する。

---

## 2. 初期化シーケンス全体像

### 2.1 ARM Cortex-M の場合

```
電源投入 / リセット
       │
       ▼
┌─────────────────────────────────────────────┐
│  Stage 0: Hardware Boot (CPU 自動実行)       │
│                                              │
│  1. ベクターテーブルから SP 初期値ロード       │
│  2. ベクターテーブルから Reset_Handler アドレス │
│  3. Reset_Handler へジャンプ                  │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Stage 1: C ランタイム初期化 (startup.cc)    │
│                                              │
│  1. .data セクション: Flash → RAM コピー      │
│  2. .bss セクション: ゼロ初期化               │
│  3. FPU 有効化 (Cortex-M4F の場合)           │
│  4. static コンストラクタ呼び出し             │
│     (.init_array セクション)                  │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Stage 2: クロック初期化                     │
│                                              │
│  1. Flash レイテンシ設定 (クロック上げる前)    │
│  2. HSE 発振器有効化 + 安定待ち              │
│  3. PLL 設定 (入力ソース, 逓倍/分周)         │
│  4. PLL 有効化 + ロック待ち                   │
│  5. バスプリスケーラ設定 (AHB, APB1, APB2)   │
│  6. システムクロック切り替え (PLL → SYSCLK)   │
│  7. SysTick 設定 (1ms ティック)              │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Stage 3: ペリフェラルクロック有効化          │
│                                              │
│  1. GPIO ポートクロック (RCC_AHB1ENR)        │
│  2. UART クロック (RCC_APB1ENR/APB2ENR)      │
│  3. I2C クロック (RCC_APB1ENR)               │
│  4. DMA クロック (RCC_AHB1ENR)               │
│  5. その他ペリフェラル                        │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Stage 4: ペリフェラル初期化                 │
│                                              │
│  1. GPIO ピンモード設定 (AF, 出力, 入力)      │
│  2. UART 初期化 (ボーレート, パリティ)        │
│  3. I2C 初期化 (クロック速度)                 │
│  4. DMA ストリーム設定                        │
│  5. NVIC 割り込み優先度 + 有効化              │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Stage 5: Platform 初期化                    │
│                                              │
│  1. Platform::init() 呼び出し                │
│     → OutputDevice (UART/RTT) 初期化         │
│     → Timer (DWT) 初期化                     │
│  2. umirtm 初期化 (出力先バインド)            │
│  3. 起動メッセージ出力                        │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Stage 6: アプリケーション                   │
│                                              │
│  main() / app_main() 呼び出し                │
│  → ユーザーコード実行                         │
└─────────────────────────────────────────────┘
```

---

## 3. 各ステージの責務と所在

### 3.1 責務マッピング

| Stage | 責務 | コード所在 | パッケージ |
|-------|------|-----------|-----------|
| 0 | ハードウェアブート | CPU 自動 | — |
| 1 | C ランタイム | src/stm32f4/startup.cc | umiport |
| 2 | クロック初期化 | mcu/stm32f4/rcc.hh | umiport |
| 3 | ペリフェラルクロック | mcu/stm32f4/rcc.hh | umiport |
| 4 | ペリフェラル初期化 | 各ドライバの init() | umiport |
| 5 | Platform 統合 | board/\<name\>/platform.hh | umiport (board) |
| 6 | アプリケーション | examples/ | アプリケーション |

### 3.2 初期化の依存関係グラフ

```
Stage 1: C ランタイム
  └─► FPU 有効化 (M4F)     ※ これ以降 float 演算可能

Stage 2: クロック
  ├─► Flash レイテンシ      ※ クロック上昇前に設定必須
  ├─► HSE                  ※ 外部発振器起動
  ├─► PLL                  ※ HSE が安定してから
  ├─► バスプリスケーラ       ※ PLL 切り替え前に設定
  └─► SYSCLK 切り替え       ※ 全部準備完了後

Stage 3: ペリフェラルクロック
  └─► 各ペリフェラルの RCC ENR ビット  ※ SYSCLK 確定後

Stage 4: ペリフェラル
  ├─► GPIO                 ※ ペリフェラルクロック有効化後
  ├─► UART                 ※ GPIO ピン設定後 (AF モード)
  ├─► I2C                  ※ GPIO ピン設定後 (AF + オープンドレイン)
  └─► NVIC                 ※ 対象ペリフェラル初期化後

Stage 5: Platform
  └─► Platform::init()     ※ 全ペリフェラル初期化完了後
```

**順序違反の典型的バグ:**
- PLL 設定前に Flash レイテンシ未設定 → ハードフォルト
- GPIO クロック未有効化で GPIO 設定 → レジスタ書き込み無視
- AF モード未設定で UART 送信 → 出力なし

---

## 4. 実装パターン

### 4.1 startup.cc テンプレート

```cpp
// src/stm32f4/startup.cc
#include <cstdint>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
extern void (*__init_array_start)();
extern void (*__init_array_end)();
extern int main();

extern "C" [[noreturn]] void Reset_Handler() {
    // Stage 1: C ランタイム初期化
    // .data コピー (Flash → RAM)
    auto* src = &_sidata;
    for (auto* dst = &_sdata; dst < &_edata; ) *dst++ = *src++;

    // .bss ゼロ初期化
    for (auto* dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    // FPU 有効化 (Cortex-M4F)
    // SCB->CPACR |= (0xF << 20);  // CP10, CP11 Full Access

    // static コンストラクタ
    for (auto* fn = &__init_array_start; fn < &__init_array_end; fn++) (*fn)();

    // Stage 2-5 は main() 内または Platform::init() で実行
    main();

    while (true) __asm volatile("wfi");
}
```

### 4.2 クロック初期化パターン

```cpp
// mcu/stm32f4/system_init.hh
namespace umi::port::stm32f4 {

template <typename BoardConfig>
constexpr void system_clock_init() {
    // Stage 2: クロック初期化
    // Flash レイテンシ (168MHz → 5WS)
    constexpr auto wait_states = calculate_wait_states(BoardConfig::system_clock);
    flash::set_latency(wait_states);

    // HSE 有効化
    rcc::enable_hse();
    while (!rcc::hse_ready()) {}

    // PLL 設定
    constexpr auto pll = calculate_pll_params(
        BoardConfig::hse_frequency,
        BoardConfig::system_clock,
        BoardConfig::apb1_clock
    );
    rcc::configure_pll(pll);
    rcc::enable_pll();
    while (!rcc::pll_ready()) {}

    // バスプリスケーラ
    rcc::set_ahb_prescaler(pll.ahb_div);
    rcc::set_apb1_prescaler(pll.apb1_div);
    rcc::set_apb2_prescaler(pll.apb2_div);

    // SYSCLK → PLL
    rcc::set_sysclk_source(rcc::SysClkSource::PLL);
    while (rcc::get_sysclk_source() != rcc::SysClkSource::PLL) {}
}

} // namespace umi::port::stm32f4
```

### 4.3 Platform::init() パターン

```cpp
// board/stm32f4-disco/platform.hh
namespace umi::port {
struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;

    static void init() {
        // Stage 2: クロック初期化
        stm32f4::system_clock_init<umi::board::Stm32f4Disco>();

        // Stage 3: ペリフェラルクロック有効化
        stm32f4::rcc::enable_gpio(stm32f4::rcc::GpioPort::A);  // UART TX/RX
        stm32f4::rcc::enable_gpio(stm32f4::rcc::GpioPort::D);  // LED
        stm32f4::rcc::enable_uart(2);  // USART2

        // Stage 4: ペリフェラル初期化
        Output::init();
        Timer::init();

        // Stage 5: 起動メッセージ
        umi::rtm::println("UMI Platform: stm32f4-disco initialized");
    }
};
} // namespace umi::port
```

---

## 5. エラーリカバリ

### 5.1 初期化失敗時の挙動

| 障害 | 症状 | リカバリ |
|------|------|---------|
| HSE 起動失敗 | rcc::hse_ready() がタイムアウト | HSI にフォールバック + LED エラーパターン |
| PLL ロック失敗 | rcc::pll_ready() がタイムアウト | HSE 直接使用 (低クロック動作) |
| UART 初期化失敗 | init() が ErrorCode 返却 | RTT のみで動作 |
| I2C デバイス応答なし | NACK エラー | デバイスなしモードで続行 |
| ハードフォルト | HardFault_Handler | LED 高速点滅 + RTT にレジスタダンプ |

### 5.2 HardFault ハンドラ

```cpp
extern "C" void HardFault_Handler() {
    // スタックフレームからフォルト情報を取得
    // LED 高速点滅パターン (250ms 間隔) でハードフォルトを通知
    // RTT が初期化済みなら CFSR/HFSR/PC をダンプ
    // → デバッガ接続時はブレークポイントで停止
    __asm volatile("bkpt #0");
    while (true) {}
}
```

---

## 6. プラットフォーム別の差異

| Stage | ARM Cortex-M | WASM | Host |
|-------|-------------|------|------|
| 0 ブート | ベクターテーブル | Module instantiation | OS ローダー |
| 1 C ランタイム | startup.cc (手動) | emscripten 自動 | OS 自動 |
| 2 クロック | RCC レジスタ操作 | N/A | N/A |
| 3 ペリフェラルクロック | RCC ENR ビット | N/A | N/A |
| 4 ペリフェラル | レジスタ直接操作 | Web API バインド | OS API 呼び出し |
| 5 Platform | Platform::init() | Platform::init() | Platform::init() |
| 6 アプリケーション | main() | emscripten_set_main_loop | main() |

**設計原則:** Stage 5 (Platform::init()) が全プラットフォーム共通の抽象化ポイント。
アプリケーションは Platform::init() より前の詳細を知らない。

---

## 7. 未解決の設計課題

| # | 課題 | 選択肢 | 備考 |
|---|------|--------|------|
| 1 | Stage 2-4 の呼び出し場所 | Platform::init() 内 vs main() の先頭 vs startup.cc | 現設計は Platform::init() に統合 |
| 2 | HSE タイムアウト値 | ハードコード vs board.hh で定義 | MCU データシート準拠 |
| 3 | エラー LED パターン | ボード固有 vs 共通規約 | board.hh に error_led ピンを定義？ |
| 4 | static コンストラクタの排除 | 許容 vs 禁止 (ベアメタル) | サイズ・初期化順序の予測困難性 |
| 5 | RTOS 統合時のスタートアップ | ベアメタルと RTOS で分岐？ | FreeRTOS は独自のスタック初期化が必要 |
