# 20 — Diagnostics

## 概要

ランタイムの統計収集、Fault ログ、パフォーマンスメトリクスの管理。
Shell の `show` コマンドおよびプログラマティックアクセスで利用する。

| 項目 | 状態 |
|------|------|
| KernelMetrics（DWT ベース） | 実装済み |
| FaultLog（リングバッファ） | 実装済み |
| エラー LED パターン | 実装済み |
| MemoryUsage モニタリング | 実装済み |

---

## データフロー

```
データ収集元                   カーネル RAM                  表示

AudioTask ──────────→ KernelMetrics.audio ──→ show cpu/audio
DWT サイクルカウンタ ─→ KernelMetrics.context_switch ──→ show cpu
Fault ISR ───────────→ FaultLog (ring buffer) ──→ show errors
ヒープ allocator ────→ MemoryUsage ──→ show memory
USB ドライバ ────────→ USB 統計 ──→ show usb

                     カーネル RAM に保持
                     │
                     ▼
                     正常時のみ要約を SharedMemory にコピー
                     （Fault 時は SharedMemory を信頼しない）
```

---

## KernelMetrics

DWT（Data Watchpoint and Trace）ベースのパフォーマンスメトリクス。

### DWT サイクルカウンタ

```cpp
namespace dwt {
    void enable();           // DWT_CTRL.CYCCNTENA = 1
    void disable();
    uint32_t cycles();       // DWT_CYCCNT 読み取り
    void reset();            // DWT_CYCCNT = 0
    bool is_available();     // DWT 利用可能か
}
```

### メトリクス構造体

```cpp
struct KernelMetrics {
    struct ContextSwitch {
        uint32_t count;                // スイッチ回数
        uint32_t cycles_min;           // 最小サイクル
        uint32_t cycles_max;           // 最大サイクル
        uint64_t cycles_sum;           // 累計サイクル
        uint32_t average() const;      // 平均サイクル
    } context_switch;

    struct IsrLatency {
        uint32_t audio_dma_max;        // Audio DMA ISR 最大レイテンシ
        uint32_t usb_max;              // USB ISR 最大レイテンシ
        uint32_t systick_max;          // SysTick ISR 最大レイテンシ
        uint32_t timer_max;            // Timer ISR 最大レイテンシ
    } isr_latency;

    struct Audio {
        uint32_t cycles_last;          // 直前の process() サイクル
        uint32_t cycles_max;           // process() 最大サイクル
        uint32_t overruns;             // オーバーラン回数
        uint32_t underruns;            // アンダーラン回数
    } audio;

    struct Scheduler {
        uint32_t preemptions;          // プリエンプション回数
        uint32_t yields;               // Yield 回数
        uint32_t idle_entries;         // Idle 遷移回数
        uint32_t task_switches;        // タスクスイッチ総数
    } scheduler;

    struct Power {
        uint32_t wfi_count;            // WFI 実行回数
        uint32_t stop_mode_count;      // Stop モード遷移回数
        uint64_t idle_cycles;          // Idle サイクル累計
    } power;
};
```

### RAII 計測ヘルパー

```cpp
// 関数実行サイクルの計測
class ScopedCycles {
    uint32_t start;
    uint32_t& dest;
public:
    ScopedCycles(uint32_t& dest) : start(dwt::cycles()), dest(dest) {}
    ~ScopedCycles() { dest = dwt::cycles() - start; }
};

// コンテキストスイッチ計測
class ScopedSwitchMeasure { /* ... */ };
```

マクロ版: `UMI_MEASURE_START`, `UMI_MEASURE_END`, `UMI_MEASURE_SWITCH()`

---

## Fault ログ

### FaultLogEntry

```cpp
struct FaultLogEntry {
    backend::cm::FaultInfo info;   // 例外レジスタ情報
    uint32_t timestamp_ms;         // Fault 発生時刻
};
```

`FaultInfo` には以下が含まれる:

| フィールド | 内容 |
|-----------|------|
| PC | Fault 発生アドレス |
| LR | リンクレジスタ |
| SP | スタックポインタ |
| CFSR | Configurable Fault Status Register |
| MMFAR | MemManage Fault Address Register |
| BFAR | BusFault Address Register |

### FaultLog（リングバッファ）

```cpp
template <size_t N = 8>
class FaultLog {
    void push(const FaultLogEntry& entry);
    const FaultLogEntry* get(size_t idx) const;
    const FaultLogEntry* latest() const;
    void clear();
    size_t count() const;
private:
    FaultLogEntry entries[N];
    size_t write_idx = 0;
    size_t total_count = 0;
};
```

- 最大 8 エントリ保持
- **カーネル RAM に配置**（SharedMemory ではない）
- Fault 時に SharedMemory は破壊される可能性があるため信頼しない

### グローバル Fault 状態

```cpp
inline FaultLog<8> g_fault_log;                     // カーネル .bss
inline std::atomic<bool> g_fault_pending{false};    // Pending フラグ
inline FaultLogEntry g_pending_fault{};             // 一時保存
```

### Fault 処理フロー

```
Fault ISR (HardFault / MemManage / BusFault / UsageFault)
  │
  ├─ record_fault(info, timestamp)
  │   ├─ g_pending_fault = entry
  │   └─ g_fault_pending = true
  │
  └─ (ISR 終了 → PendSV → SystemTask に切り替わる)

SystemTask (次の wait_block 復帰時)
  │
  ├─ process_pending_fault()
  │   ├─ g_fault_log.push(g_pending_fault)
  │   └─ g_fault_pending = false
  │
  ├─ AppLoader::terminate()
  │   └─ Processor 無効化、AppState = Terminated
  │
  ├─ LED パターン変更（エラー表示）
  │
  └─ Shell 有効化（デバッグ可能に）
```

---

## エラー LED パターン

Fault の種類に応じた LED パターンで視覚的にエラーを通知する:

```cpp
enum class ErrorLedPattern : uint8_t {
    NONE = 0,             // Fault なし
    STACK_OVERFLOW,       // Red 1 回点滅
    ACCESS_VIOLATION,     // Red 2 回点滅
    INVALID_INSTRUCTION,  // Red 3 回点滅
    BUS_FAULT,            // Red 4 回点滅
    UNKNOWN,              // Red 高速点滅
};
```

```cpp
ErrorLedPattern classify_fault(const backend::cm::FaultInfo& info);
```

CFSR の各ビットから Fault 種別を判定:
- MMFSR (MemManage) → AccessViolation / StackOverflow
- BFSR (BusFault) → BusFault
- UFSR (UsageFault) → InvalidInstruction

---

## MemoryUsage

カーネルが定期的にヒープ/スタック使用量を収集する:

| 項目 | 内容 |
|------|------|
| heap_used | 現在のヒープ使用量 |
| heap_peak | ヒープ使用量のピーク値 |
| stack_used | 現在のスタック使用量（_estack - SP） |
| stack_peak | スタック使用量のピーク値 |

- **カーネル RAM に保持**
- UI 表示に必要な要約のみ、正常時に SharedMemory へコピー
- ヒープ/スタック衝突検出の詳細は [12-memory-protection.md](12-memory-protection.md) を参照

---

## デバッグカウンタ

GDB で直接参照可能なグローバル変数:

| 変数名 | 内容 |
|--------|------|
| `dbg_i2s_isr_count` | I2S DMA ISR 呼び出し回数 |
| `dbg_underrun` | オーディオアンダーラン回数 |
| `dbg_overrun` | オーディオオーバーラン回数 |
| `dbg_usb_rx_count` | USB 受信パケット数 |
| `dbg_sysex_processed` | SysEx 処理済み数 |

---

## 実装ファイル

| ファイル | 内容 |
|---------|------|
| `lib/umios/kernel/metrics.hh` | KernelMetrics、DWT ヘルパー、RAII 計測 |
| `lib/umios/kernel/fault_handler.hh` | FaultLog、FaultLogEntry、LED パターン |

---

## 関連ドキュメント

- [11-scheduler.md](11-scheduler.md) — コンテキストスイッチ計測
- [12-memory-protection.md](12-memory-protection.md) — Fault 処理、ヒープ/スタック衝突検出
- [13-system-services.md](13-system-services.md) — SystemTask でのディスパッチ
- [17-shell.md](17-shell.md) — `show` コマンドの表示
