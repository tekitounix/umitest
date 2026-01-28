# アプリケーションメモリ管理と Fault ハンドリング

## 概要

UMI-OS のアプリケーション保護とメモリ管理を改善する。

1. **APP_RAM の有効活用** - 固定スタックサイズを撤廃、残り領域を活用
2. **MPU による境界保護** - APP_RAM 外へのアクセスを検出
3. **ソフトウェアによるヒープ/スタック衝突検出** - 同一 RW 領域内の衝突
4. **Fault ハンドラの実装** - アプリ障害時の安全な復帰

## 現状

### メモリレイアウト (app.ld)

```
APP_RAM (48KB) = 0x2000C000 - 0x20017FFF
┌────────────────────────────────────┐
│ .data (初期化済みデータ)            │
│ .bss  (ゼロ初期化データ)            │
│ .stack (4KB 固定) ← 問題           │
│ .heap  (残り)                       │
└────────────────────────────────────┘
```

### 問題点

- スタックサイズが 4KB に固定されている
- ヒープを使わない場合でも領域が無駄になる
- MPU リージョンが AppData と AppStack で分離（やや複雑）

## 設計

### MPU の制約

**重要: 48KB は 2 のべき乗ではない**

Cortex-M の MPU は以下の制約がある:
- リージョンサイズは 2 のべき乗（32B, 64B, ..., 32KB, 64KB, ...）
- ベースアドレスはサイズにアラインされている必要あり

48KB を保護する方法:

| 方式 | 説明 |
|-----|-----|
| **A. 32KB + 16KB** | 2 リージョンで分割（シンプル） |
| **B. 64KB + サブリージョン** | 64KB リージョンの上位 16KB を無効化（1リージョン節約） |

**本実装では方式 A を採用**（シンプルで理解しやすく、リージョン数も十分）

### 新メモリレイアウト

```
                                        0x20018000 = _estack
┌────────────────────────────────────┐
│         スタック領域                │ ← MPU Region 3: 16KB (RW)
│            ↓ 成長                   │
│                                    │
│            ↑ 成長                   │
│         ヒープ領域                  │ ← コルーチン等 C++ 機能用
├────────────────────────────────────┤ 0x20014000
│ .bss  (ゼロ初期化データ)            │ ← MPU Region 2: 32KB (RW)
│ .data (初期化済みデータ)            │
└────────────────────────────────────┘ 0x2000C000

_sheap = _ebss (8バイトアライン)

MPU による境界保護:
- 48KB は 2 のべき乗ではないため、32KB + 16KB の 2 リージョンで保護
- Region 2 (32KB): 0x2000C000 - 0x20013FFF
- Region 3 (16KB): 0x20014000 - 0x20017FFF
- APP_RAM 外（0x2000C000 未満、0x20018000 以上）へのアクセスは Fault
- ヒープ/スタック衝突は同じ RW 領域内なので MPU では検出不可 → ソフトウェアで検出
```

### 衝突検出の整理

| 検出対象 | 方法 | 備考 |
|---------|------|------|
| **APP_RAM 外へのアクセス** | MPU Fault | 境界を超えた場合 |
| **ヒープ→スタック衝突** | ソフトウェア | `heap_alloc()` で SP と比較 |
| **スタック→ヒープ衝突** | 検出不可 | スタック消費は暗黙的、事前チェック不可 |

スタック消費は CPU が暗黙的に行うため、ソフトウェアでの事前チェックは不可能。
ウォーターマーク監視で事後的に使用量を確認する。

### メモリ使用量の追跡と衝突検出

デバッグ・監視用に**ヒープとスタックの使用量を追跡**、および**ヒープ割り当て時の衝突検出**を行う:

```cpp
// lib/umios/kernel/loader.hh の SharedMemory 内に配置

/// メモリ追跡情報
struct MemoryUsage {
    void* heap_base;      // _sheap
    void* heap_current;   // 現在のヒープポインタ
    void* stack_top;      // _estack
    void* stack_hwm;      // スタック最大使用（ハイウォーターマーク）

    /// ヒープ使用量
    size_t heap_used() const {
        return reinterpret_cast<uintptr_t>(heap_current)
             - reinterpret_cast<uintptr_t>(heap_base);
    }

    /// スタック最大使用量
    size_t stack_used() const {
        return reinterpret_cast<uintptr_t>(stack_top)
             - reinterpret_cast<uintptr_t>(stack_hwm);
    }
};
```

**配置場所:** `SharedMemory` 構造体内（カーネルとアプリで共有）

```cpp
// lib/umios/kernel/loader.hh
struct SharedMemory {
    // ... 既存フィールド ...

    // メモリ追跡（アプリが更新、カーネルが監視）
    MemoryUsage app_memory;
};
```

**初期化:** カーネルがアプリをロードする際 (`AppLoader::load()`) に初期化する。
アプリの `_start()` が呼ばれる前に完了している必要がある。

```cpp
// AppLoader::load() 内で初期化
shared_->app_memory.heap_base    = reinterpret_cast<void*>(&_sheap);
shared_->app_memory.heap_current = shared_->app_memory.heap_base;
shared_->app_memory.stack_top    = reinterpret_cast<void*>(&_estack);
shared_->app_memory.stack_hwm    = shared_->app_memory.stack_top;
```

**ヒープ割り当て（crt0.cc で使用）:**

```cpp
// lib/umios/app/crt0.cc

extern SharedMemory* g_shared;  // カーネルから渡される

void* operator new(size_t size) {
    size = (size + 7) & ~size_t(7);  // 8バイトアライン

    auto& mem = g_shared->app_memory;
    auto* new_end = static_cast<uint8_t*>(mem.heap_current) + size;

    // 現在のスタックポインタを取得して衝突チェック
    void* sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));

    // マージン 64B を確保（関数呼び出しのスタック消費分）
    if (reinterpret_cast<uintptr_t>(new_end) + 64 >
        reinterpret_cast<uintptr_t>(sp)) {
        // OOM - スタックに衝突
        umi::syscall::panic("heap/stack collision");
    }

    void* result = mem.heap_current;
    mem.heap_current = new_end;
    return result;
}
```

**追跡タイミング:**

| タイミング | 更新内容 | 実装場所 |
|-----------|---------|---------|
| アプリロード時 | 全フィールド初期化 | `AppLoader::load()` (Phase 3) |
| `operator new` | `heap_current` を更新 + 衝突チェック | `crt0.cc` (Phase 4) |
| コンテキスト切替時 | `stack_hwm` を更新 | カーネルスケジューラ |

**シェルからの確認:**

```
> mem
Heap:  1.2 KB used
Stack: 0.8 KB max used (watermark)
```

**オーバーヘッド:** ~5 cycles（ヒープ割り当て自体が数十〜数百 cycle なので無視できる）

### Fault ログの配置

Fault 発生時のログは **カーネル RAM（KERNEL_RAM）の .bss セクション** に配置する。

| データ | 配置場所 | アプリからのアクセス |
|--------|---------|-------------------|
| `MemoryUsage` | SharedMemory 内 | 可能（読み書き） |
| `g_fault_log` | カーネル .bss | 不可（MPU で保護） |
| `g_fault_pending` | カーネル .bss | 不可（MPU で保護） |

**SharedMemory と Fault ログの違い:**
- **SharedMemory**: カーネル/アプリ間の共有領域。アプリからアクセス可能（MPU で RW 許可）
- **Fault ログ**: カーネル専用。アプリがクラッシュしても破壊されない。MCU リセット時はゼロクリア

### Fault ハンドリングフロー

```
アプリ Fault 発生 (MemManage / BusFault / UsageFault)
       ↓
┌─────────────────────────────────────┐
│ 1. フォルト情報をキャプチャ          │
│    - CFSR, HFSR, MMFAR, BFAR        │
│    - EXC_RETURN で MSP/PSP を判定    │  ← 重要: 常に PSP とは限らない
│    - スタックフレームから PC, LR     │
└─────────────────────────────────────┘
       ↓
┌─────────────────────────────────────┐
│ 2. 情報を一時保存 + フラグ設定       │  ← ISR はここまで（最小限）
│    - g_pending_fault に FaultInfo    │
│    - g_fault_pending = true          │
│    - terminate() は呼ばない          │
└─────────────────────────────────────┘
       ↓
┌─────────────────────────────────────┐
│ 3. 安全なコンテキストに復帰          │
│    - PendSV でコンテキスト切替       │
│    - または直接カーネルタスクへ       │
└─────────────────────────────────────┘
       ↓
┌─────────────────────────────────────┐
│ 4. メインループで後処理              │  ← ログ保存はここで行う
│    - g_fault_log.push()             │
│    - loader.terminate(-1)           │
│    - エラー LED 表示                 │
│    - シェル有効化 or 待機            │
└─────────────────────────────────────┘
```

## 実装計画

### Phase 1: app.ld の変更

**ファイル:** `lib/umios/app/app.ld`

```ld
/* 変更前 */
.stack (NOLOAD) : {
    . = ALIGN(8);
    _sstack = .;
    . = . + 4K;     /* 4KB stack (configurable) */
    _estack = .;
} > APP_RAM

.heap (NOLOAD) : {
    . = ALIGN(8);
    _sheap = .;
    _eheap = ORIGIN(APP_RAM) + LENGTH(APP_RAM) - 4K;
} > APP_RAM

/* 変更後 */
/* .bss 直後からヒープ開始（コルーチン等の C++ 機能で使用） */
_sheap = ALIGN(_ebss, 8);

/* スタックトップ = RAM 終端 */
_estack = ORIGIN(APP_RAM) + LENGTH(APP_RAM);

/* ヒープとスタックは同じ領域を共有（衝突に注意） */
/* ヒープは下から上へ、スタックは上から下へ成長 */
/*
    ┌─────────────┐ _estack (RAM終端)
    │   Stack     │ ↓ 成長
    │     ↓       │
    │             │
    │     ↑       │
    │   Heap      │ ↑ 成長
    └─────────────┘ _sheap (= _ebss aligned)
*/
```

**注意:**
- `_estack` は [vector_table.hh](lib/umios/backend/cm/common/vector_table.hh) と [irq.hh](lib/umios/backend/cm/common/irq.hh) で参照されているため維持が必要
- 現在 crt0.cc にはコルーチン用 4KB バンプアロケータがハードコード（`g_heap[4096]`）。これを SharedMemory ベースに変更

### Phase 2: MPU 設定の変更

**ファイル:** `lib/umios/kernel/mpu_config.hh`

```cpp
inline void configure_app_regions(
    void* app_ram_base,
    size_t app_ram_size,
    const AppRuntime& runtime,
    void* shared_base,
    size_t shared_size,
    void* kernel_base,
    size_t kernel_size
) noexcept {
    disable();

    // Region 0: Kernel (privileged only)
    configure_region(Region::Kernel, { ... });

    // Region 1: Application .text (read-only, executable)
    configure_region(Region::AppText, { ... });

    // Region 2: APP_RAM 下位 32KB (data + bss + heap の一部)
    configure_region(Region::AppData, {
        .base = app_ram_base,  // 0x2000C000
        .size = 32 * 1024,
        .readable = true,
        .writable = true,
        .executable = false,
        .privileged_only = false,
        .device_memory = false,
    });

    // Region 3: APP_RAM 上位 16KB (heap の続き + stack)
    configure_region(Region::AppStack, {
        .base = static_cast<uint8_t*>(app_ram_base) + 32 * 1024,  // 0x20014000
        .size = 16 * 1024,
        .readable = true,
        .writable = true,
        .executable = false,
        .privileged_only = false,
        .device_memory = false,
    });

    // Region 4: Shared memory
    configure_region(Region::Shared, { ... });

    // 注: ヒープ/スタック衝突はソフトウェアで検出（同じ RW 領域内）

    enable(true);
}
```

### Phase 3: SharedMemory に MemoryUsage 追加

**ファイル:** `lib/umios/kernel/loader.hh`

```cpp
struct SharedMemory {
    // ... 既存フィールド ...

    // メモリ追跡情報（アプリが更新、カーネルが監視）
    MemoryUsage app_memory;
};
```

**初期化タイミング:** カーネルがアプリをロードする際に `AppLoader::load()` 内で初期化

```cpp
// AppLoader::load() 内
shared_->app_memory.heap_base = reinterpret_cast<void*>(&_sheap);
shared_->app_memory.heap_current = shared_->app_memory.heap_base;
shared_->app_memory.stack_top = reinterpret_cast<void*>(&_estack);
shared_->app_memory.stack_hwm = shared_->app_memory.stack_top;
```

### Phase 4: crt0.cc の変更

**ファイル:** `lib/umios/app/crt0.cc`

```cpp
// 変更前: ハードコードされたヒープ
namespace {
    alignas(8) char g_heap[4096];
    char* g_heap_ptr = g_heap;
}

// 変更後: SharedMemory を使用
extern "C" {
    extern uint32_t _sheap;
    extern uint32_t _estack;
}

void* operator new(size_t size) {
    size = (size + 7) & ~size_t(7);

    auto& mem = umi::syscall::get_shared_memory()->app_memory;
    auto* new_end = static_cast<uint8_t*>(mem.heap_current) + size;

    void* sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));

    if (reinterpret_cast<uintptr_t>(new_end) + 64 >
        reinterpret_cast<uintptr_t>(sp)) {
        umi::syscall::panic("heap/stack collision");
    }

    void* result = mem.heap_current;
    mem.heap_current = new_end;
    return result;
}
```

### Phase 5: Fault ハンドラの実装

**新規ファイル:** `lib/umios/kernel/fault_handler.hh`

```cpp
#pragma once

#include "../backend/cm/common/fault.hh"
#include <cstdint>
#include <atomic>

namespace umi::kernel {

/// Fault ログエントリ
struct FaultLogEntry {
    backend::cm::FaultInfo info;
    uint32_t timestamp_ms;
};

/// Fault ログ（リングバッファ）
template<size_t N = 8>
class FaultLog {
public:
    void push(const FaultLogEntry& entry) {
        entries_[write_idx_ % N] = entry;
        write_idx_++;
    }

    size_t count() const { return write_idx_; }

    const FaultLogEntry* get(size_t idx) const {
        if (idx >= count() || idx >= N) return nullptr;
        size_t start = (write_idx_ > N) ? (write_idx_ - N) : 0;
        return &entries_[(start + idx) % N];
    }

private:
    FaultLogEntry entries_[N];
    size_t write_idx_ = 0;
};

/// グローバル Fault ログ
///
/// **配置場所:** カーネルの .bss セクション（KERNEL_RAM 内）
/// - APP_RAM (0x2000C000-0x20017FFF) とは完全に分離
/// - アプリがクラッシュしても保持される
/// - MCU リセット時はゼロクリア（電源断耐性が必要な場合は RTC バックアップ RAM や Flash に保存）
///
/// **SharedMemory との違い:**
/// - SharedMemory: カーネル/アプリ間の共有データ（アプリからアクセス可能）
/// - g_fault_log: カーネル専用（アプリからはアクセス不可、MPU で保護）
inline FaultLog<8> g_fault_log;

/// Fault 発生フラグ（ISR から設定、メインループで処理）
inline std::atomic<bool> g_fault_pending{false};
inline FaultLogEntry g_pending_fault{};

/// エラー LED パターン
enum class ErrorLedPattern : uint8_t {
    None = 0,
    StackOverflow,      // 赤 点滅
    AccessViolation,    // 赤 2回点滅
    InvalidInstruction, // 赤 3回点滅
    Unknown,            // 赤 高速点滅
};

/// ISR から呼ぶ: Fault 情報を一時保存してフラグを立てる（軽量）
/// 注: g_fault_log への push は process_pending_fault() で行う（ISR 内では最小限の処理）
inline void record_fault(const backend::cm::FaultInfo& info, uint32_t timestamp) {
    g_pending_fault = {info, timestamp};
    g_fault_pending.store(true, std::memory_order_release);
}

/// メインループから呼ぶ: Fault を処理
/// - g_fault_log に追加
/// - g_fault_pending をクリア
/// @return true if fault was processed
bool process_pending_fault();

/// エラー状態を設定
void set_error_state(ErrorLedPattern pattern);

/// Fault 種別からエラーパターンを判定
ErrorLedPattern classify_fault(const backend::cm::FaultInfo& info);

} // namespace umi::kernel
```

### Phase 6: Fault ハンドラ（ISR 側）

**ファイル:** `examples/stm32f4_kernel/src/main.cc`

**対象例外ハンドラ:**
- `MemManage_Handler` - MPU 違反（APP_RAM 外へのアクセス等）
- `BusFault_Handler` - バスエラー（無効なアドレスへのアクセス等）
- `UsageFault_Handler` - 使用法エラー（未定義命令、0除算等）

これらは全て同じ `common_fault_handler()` にルーティングする。HardFault は上記が無効時のエスカレーション先なので、個別に有効化すれば HardFault には到達しない。

```cpp
/// 共通 Fault ハンドラ（3つの例外から呼ばれる）
static void common_fault_handler() {
    using namespace umi::backend::cm;
    using namespace umi::kernel;

    // 1. フォルト情報をキャプチャ
    FaultInfo info;
    info.capture();

    // 2. EXC_RETURN から使用スタックを判定
    uint32_t exc_return;
    __asm__ volatile("mov %0, lr" : "=r"(exc_return));

    uint32_t* sp;
    if (exc_return & 0x4) {
        // PSP を使用していた（アプリコンテキスト）
        __asm__ volatile("mrs %0, psp" : "=r"(sp));
    } else {
        // MSP を使用していた（カーネルコンテキスト）
        __asm__ volatile("mrs %0, msp" : "=r"(sp));
    }

    // スタックフレームから PC/LR を取得
    info.pc = sp[6];
    info.lr = sp[5];

    // 3. Fault 情報を記録（軽量処理のみ）
    record_fault(info, get_time_ms());

    // 4. 安全なコンテキストに復帰
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

/// 各例外ハンドラから共通処理を呼び出す
extern "C" void MemManage_Handler() { common_fault_handler(); }
extern "C" void BusFault_Handler()  { common_fault_handler(); }
extern "C" void UsageFault_Handler(){ common_fault_handler(); }
```

**例外の有効化（起動時に設定）:**

```cpp
// SCB->SHCSR で個別例外を有効化
SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk   // MemManage 有効
           |  SCB_SHCSR_BUSFAULTENA_Msk   // BusFault 有効
           |  SCB_SHCSR_USGFAULTENA_Msk;  // UsageFault 有効
```

### Phase 7: メインループでの後処理

```cpp
// process_pending_fault() の実装
bool process_pending_fault() {
    if (!g_fault_pending.load(std::memory_order_acquire)) {
        return false;
    }

    // ログに追加（ISR では行わない）
    g_fault_log.push(g_pending_fault);

    // フラグをクリア
    g_fault_pending.store(false, std::memory_order_release);

    return true;
}

void idle_task() {
    while (true) {
        if (umi::kernel::process_pending_fault()) {
            g_loader.terminate(-1);

            auto pattern = umi::kernel::classify_fault(g_pending_fault.info);
            umi::kernel::set_error_state(pattern);

#if UMIOS_DEBUG
            shell::enable();
#endif
        }

        __WFI();
    }
}
```

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `lib/umios/app/app.ld` | スタック/ヒープ領域の変更 |
| `lib/umios/app/crt0.cc` | `operator new` を SharedMemory ベースに変更 |
| `lib/umios/kernel/loader.hh` | SharedMemory に MemoryUsage 追加 |
| `lib/umios/kernel/fault_handler.hh` | 新規: Fault ログ（カーネル RAM に配置）、フラグ、軽量記録 |
| `lib/umios/kernel/fault_handler.cc` | 新規: 後処理実装 |
| `examples/stm32f4_kernel/src/main.cc` | Fault ハンドラ（MemManage/BusFault/UsageFault）、例外有効化、メモリ監視 |

## 検証項目

- [ ] app.ld 変更後にアプリがビルド・実行できる
- [ ] `_estack`, `_sheap` シンボルが正しく参照される
- [ ] スタックが正しく RAM 終端から成長する
- [ ] MPU 設定で APP_RAM (32KB + 16KB) が保護される
- [ ] APP_RAM 外へのアクセスで MemManage Fault が発生する
- [ ] Fault 後にカーネルが正常に動作を継続する
- [ ] Fault ログがシェルから確認できる
- [ ] Fault ログの保存が ISR ではなくメインループで行われる（`process_pending_fault()`）
- [ ] EXC_RETURN 判定が正しく動作する（PSP/MSP）
- [ ] ヒープ/スタック使用量がシェルから確認できる
- [ ] ヒープ割り当て時の衝突チェックで OOM/panic が発生する

## リスク・注意点

1. **MPU リージョン数**: Cortex-M4 は通常 8 リージョン。現状で十分
2. **アライメント**: MPU リージョンのベースアドレスはサイズにアラインが必要
3. **Fault 中の再入**: `record_fault()` は再入安全である必要（atomic 使用）
4. **スタック破壊時**: スタックが完全に破壊された場合、スタックフレーム取得も失敗する可能性
5. **スタック→ヒープ衝突**: スタック消費は暗黙的なため事前検出不可。ウォーターマーク監視で対応

## 関連ドキュメント

- [PLAN_AUDIOCONTEXT_REFACTOR.md](PLAN_AUDIOCONTEXT_REFACTOR.md) - AudioContext リファクタリング
