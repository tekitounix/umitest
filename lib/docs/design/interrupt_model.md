# 割り込み・イベントモデル設計

**ステータス:** 設計中  **策定日:** 2026-02-10
**根拠:** [hal/concept_design.md](hal/concept_design.md), [pal/categories/C04_VECTORS.md](pal/categories/C04_VECTORS.md)

**関連文書:**
- [hal/concept_design.md](hal/concept_design.md) — sync/async 分離、UartAsync 等の定義
- [pal/categories/C04_VECTORS.md](pal/categories/C04_VECTORS.md) — 割り込みベクターテーブル
- [pal/categories/C01_CORE_PERIPHERALS.md](pal/categories/C01_CORE_PERIPHERALS.md) — NVIC 定義
- [pal/categories/C09_DMA_MAPPING.md](pal/categories/C09_DMA_MAPPING.md) — DMA マッピング
- [init_sequence.md](init_sequence.md) — NVIC 初期化のタイミング

---

## 1. 目的

HAL Concept で sync/async は分離されているが、
**割り込みがどのようにドライバコードに到達し、ユーザーに通知されるか** の設計が未定義。
本文書では以下を設計する:

1. ベクターテーブルからドライバハンドラへのディスパッチ
2. DMA 完了通知モデル
3. NVIC 優先度割り当てポリシー
4. リアルタイムオーディオパスとの整合

---

## 2. 割り込みディスパッチモデル

### 2.1 呼び出しチェーン

```
ハードウェア割り込み発生
       │
       ▼
┌─────────────────────────────────────────────┐
│  Layer 1: ベクターテーブル                    │
│                                              │
│  vectors.cc で定義される関数ポインタ配列       │
│  → IRQ 番号に対応するハンドラを呼び出す        │
│                                              │
│  extern "C" void USART2_IRQHandler();        │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Layer 2: ドライバ IRQ ハンドラ              │
│                                              │
│  ペリフェラルのステータスレジスタを読み取り     │
│  → イベント種別を判定                         │
│  → データ転送 or フラグ設定                    │
│  → ユーザーコールバック呼び出し (登録済みの場合)│
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Layer 3: ユーザーコールバック / 通知          │
│                                              │
│  a) コールバック関数 (ISR コンテキスト)        │
│  b) フラグ/セマフォ設定 (メインループでポーリング)│
│  c) リングバッファへのデータ投入               │
└─────────────────────────────────────────────┘
```

### 2.2 ハンドラ登録方式

#### 方式比較

| 方式 | 利点 | 欠点 | UMI での採用 |
|------|------|------|-------------|
| **静的ベクターテーブル** | ゼロオーバーヘッド、コンパイル時解決 | 動的変更不可 | **主方式** |
| 関数ポインタテーブル (RAM) | 動的変更可能 | RAM 消費、間接呼び出し | オプション |
| 仮想関数ディスパッチ | OOP 的に自然 | vtable オーバーヘッド | **不採用** (設計原則 #4) |

#### 静的ハンドラの実装パターン

```cpp
// vectors.cc — リンカが配置するベクターテーブル
// weak シンボルでデフォルトハンドラを定義、ドライバが上書き
extern "C" {
    [[gnu::weak]] void USART2_IRQHandler()   { Default_Handler(); }
    [[gnu::weak]] void DMA1_Stream5_IRQHandler() { Default_Handler(); }
    [[gnu::weak]] void I2C1_EV_IRQHandler()  { Default_Handler(); }
}

// ドライバが実体を提供して weak を上書き
// umiport/mcu/stm32f4/uart_irq.cc
extern "C" void USART2_IRQHandler() {
    umi::port::stm32f4::uart2_irq_handler();
}
```

### 2.3 ドライバ内の IRQ ハンドラ設計

```cpp
namespace umi::port::stm32f4 {

// UART ドライバ — 割り込みハンドリング
template <typename Config>
class UartDriver {
    // ISR から呼ばれるコールバック (constexpr 関数ポインタ or テンプレート)
    using Callback = void(*)(uint8_t byte);
    static inline Callback rx_callback = nullptr;

public:
    static void enable_rx_interrupt(Callback cb) {
        rx_callback = cb;
        // USART_CR1 の RXNEIE ビットを設定
        // NVIC_EnableIRQ(USART2_IRQn)
    }

    // ドライバ内部の IRQ ハンドラ
    static void handle_irq() {
        auto sr = usart2::read_sr();

        if (sr & USART_SR_RXNE) {
            auto byte = usart2::read_dr();
            if (rx_callback) rx_callback(static_cast<uint8_t>(byte));
        }

        if (sr & USART_SR_TC) {
            // 送信完了処理
        }

        if (sr & USART_SR_ORE) {
            // オーバーラン — DR を読んでクリア
            [[maybe_unused]] auto _ = usart2::read_dr();
        }
    }
};

} // namespace umi::port::stm32f4
```

---

## 3. DMA 完了通知モデル

### 3.1 DMA の役割

オーディオパイプラインでは DMA がペリフェラル ↔ メモリ間のデータ転送を担う。
CPU は DMA 完了割り込みでのみ介入する。

```
             DMA Transfer
Audio Buffer ◄──────────── I2S ペリフェラル
(メモリ)        (自動)        (DAC/ADC)
     │
     │ DMA 半完了割り込み (Half Transfer)
     │ DMA 全完了割り込み (Transfer Complete)
     ▼
process() 呼び出し ← ダブルバッファリング
```

### 3.2 ダブルバッファリングパターン

```cpp
namespace umi::port::stm32f4 {

template <size_t BufferSize>
class I2sDmaDriver {
    // ダブルバッファ — DMA が一方に書き込み中、CPU は他方を処理
    static inline std::array<int16_t, BufferSize * 2> dma_buffer{};

    // process コールバック — ISR コンテキストで呼ばれる
    using ProcessCallback = void(*)(std::span<int16_t> buffer);
    static inline ProcessCallback process_cb = nullptr;

public:
    static void start(ProcessCallback cb) {
        process_cb = cb;
        // DMA 設定: Circular mode, Half Transfer + Transfer Complete 割り込み有効
        // DMA_SxCR: CIRC=1, HTIE=1, TCIE=1
    }

    static void handle_dma_irq() {
        if (dma_half_transfer()) {
            // 前半バッファが DMA 完了 → CPU が前半を処理
            if (process_cb) process_cb({dma_buffer.data(), BufferSize});
            clear_half_transfer_flag();
        }
        if (dma_transfer_complete()) {
            // 後半バッファが DMA 完了 → CPU が後半を処理
            if (process_cb) process_cb({dma_buffer.data() + BufferSize, BufferSize});
            clear_transfer_complete_flag();
        }
    }
};

} // namespace umi::port::stm32f4
```

### 3.3 通知モデル比較

| モデル | レイテンシ | CPU 負荷 | 適用先 |
|--------|-----------|---------|--------|
| **ISR コールバック** | 最小 | ISR 内で完結 | オーディオ process() |
| フラグ + ポーリング | ループ周期依存 | 低 | 非リアルタイム転送 |
| セマフォ (RTOS) | タスクスイッチ依存 | 中 | RTOS 環境 |

**オーディオパス:** ISR コールバック (ダブルバッファ) を採用。
process() 内のリアルタイム制約 (ヒープ不使用, ブロッキング不使用) が適用される。

---

## 4. NVIC 優先度設計

### 4.1 優先度割り当てポリシー

STM32F4 は 4 ビット優先度 (0-15, 0 が最高)。
グループ優先度とサブ優先度に分割可能。

```
UMI 推奨: NVIC_PriorityGroup_4 (全ビットをプリエンプション優先度に使用)

優先度マップ:
┌────────────────────────────────────────────┐
│  0-1   最高  フォルト (HardFault, NMI)      │  ← プリエンプト不可
│  2-3   高    オーディオ DMA (I2S)            │  ← リアルタイム
│  4-5   中高  タイマー割り込み                 │
│  6-7   中    通信ペリフェラル (UART, I2C)     │
│  8-9   中低  GPIO 外部割り込み               │
│ 10-11  低    DMA (非オーディオ)               │
│ 12-13  最低  SysTick, ソフトウェア割り込み    │
│ 14-15  予約  将来の RTOS 用                   │
└────────────────────────────────────────────┘
```

### 4.2 設計根拠

- **オーディオ DMA が最高 (フォルト除く):** オーディオバッファのアンダーラン防止。
  DMA 半完了/完了割り込みは一定間隔で発生し、遅延はグリッチに直結。
- **UART は中優先度:** データ損失は ORE (オーバーラン) フラグで検出可能。
  RTT 出力はデバッグ用であり、リアルタイム性は不要。
- **SysTick は低優先度:** ミリ秒カウンタの遅延は許容範囲。

### 4.3 board.hh での優先度定義

```cpp
namespace umi::board {
struct Stm32f4Disco {
    // 割り込み優先度 (board 固有のカスタマイズポイント)
    struct IrqPriority {
        static constexpr uint8_t audio_dma  = 2;
        static constexpr uint8_t timer      = 5;
        static constexpr uint8_t uart       = 7;
        static constexpr uint8_t i2c        = 7;
        static constexpr uint8_t gpio_exti  = 9;
        static constexpr uint8_t systick    = 13;
    };
};
} // namespace umi::board
```

---

## 5. リアルタイムオーディオパスとの整合

### 5.1 制約の再確認

[CLAUDE.md のリアルタイム安全ルール](../../CLAUDE.md) より:
- ヒープ割り当て禁止
- ブロッキング同期禁止
- 例外送出禁止
- stdio 禁止

### 5.2 オーディオ割り込みフロー

```
DMA Half Transfer 割り込み (優先度 2)
  │
  ├─ ステータスフラグクリア
  ├─ process() 呼び出し ← ユーザーの AudioProcessor
  │   ├─ 入力バッファ読み取り
  │   ├─ DSP 処理 (constexpr パラメータ)
  │   └─ 出力バッファ書き込み
  └─ リターン (DMA が次のバッファに切り替え済み)

所要時間制約:
  バッファサイズ 256 samples / 48kHz = 5.33ms
  → process() は 5.33ms 以内に完了しなければならない
  → 実測で 50% 以下 (2.67ms) を推奨マージン
```

### 5.3 優先度逆転の防止

```
問題: UART 割り込み (優先度 7) が process() 実行中にプリエンプト？
解答: process() は DMA 割り込み (優先度 2) 内で実行されるため、
      優先度 7 の UART はプリエンプトできない。安全。

問題: process() 内から UART 送信を呼びたい場合？
解答: 禁止。process() はリアルタイム制約下にある。
      ログが必要なら、フラグを設定してメインループで出力。
```

---

## 6. プラットフォーム別の差異

| 項目 | ARM Cortex-M | WASM | Host |
|------|-------------|------|------|
| 割り込みモデル | NVIC ハードウェア割り込み | N/A | OS シグナル / スレッド |
| DMA | ハードウェア DMA | N/A | N/A |
| オーディオコールバック | DMA 割り込み | AudioWorklet | PortAudio コールバック |
| 優先度制御 | NVIC 優先度レジスタ | N/A | スレッド優先度 |

**抽象化ポイント:** HAL Concept の async インターフェースが、
プラットフォーム固有の通知メカニズムを隠蔽する。

---

## 7. 未解決の設計課題

| # | 課題 | 選択肢 | 備考 |
|---|------|--------|------|
| 1 | コールバック型 | 関数ポインタ vs テンプレートパラメータ vs std::function | std::function はヒープ使用の可能性 |
| 2 | 割り込み内の処理時間制限 | ハードリミット vs ソフトリミット (ウォッチドッグ) | オーディオバッファサイズ依存 |
| 3 | ネスト割り込みポリシー | 許可 (デフォルト) vs 禁止 (クリティカルセクション) | オーディオ DMA 内での他割り込み |
| 4 | RTOS タスク通知との統合 | ISR → タスク通知 vs ISR 内完結 | FreeRTOS 統合時に再検討 |
| 5 | エラー割り込みの伝搬 | ErrorCode 返却 vs コールバック vs グローバルフラグ | バスエラー、DMA エラー等 |
