# umios カーネル最適化計画 v2.0

## 1. 概要

本文書は、umiosカーネルの性能・消費電力・ポータビリティを総合的に改善するための実装計画である。

### 1.1 設計目標

| 目標 | 指標 | 現状 | 目標値 |
|------|------|------|--------|
| **コンテキストスイッチ** | サイクル数 | ~200-400 | <150 |
| **スケジューラ選択** | 計算量 | O(n) | O(1) |
| **消費電力** | アイドル時 | 常時168MHz | 動的スケーリング |
| **ポータビリティ** | MPU依存 | 必須 | オプション |

### 1.2 設計原則

1. **スイッチ回数を減らす** - 単体高速化より効果大
2. **割り込み出口遅延を減らす** - tail-chaining最大活用
3. **キャッシュミスを減らす** - 静的メモリ、予測可能なアクセス
4. **消費電力を最小化** - Tickless + クロックスケーリング
5. **MPU隔離を維持** - セキュリティと安全性の確保

---

## 2. Phase計画の比較と統合

### 2.1 以前のPhase計画（性能特化）

| Phase | 内容 | 効果 |
|-------|------|------|
| Phase 1 | ビットマップO(1)スケジューラ、同期yield fast path | ~40-50%高速化 |
| Phase 2 | FPU所有権モデル、FPCCR明示設定 | ~20%高速化 |
| Phase 3 | BASEPRI統一、ISR優先度整理 | 予測可能性向上 |
| Phase 4 | DWT計測統合、ベンチマーク | 継続的改善基盤 |

### 2.2 新規提案（構造最適化）

| 項目 | 内容 | 効果 |
|------|------|------|
| スレッド数制限 | 固定4タスク + ワーカー | ready list操作削減 |
| 優先度固定 | 8-16優先度、ビットマップ | O(1)スケジューラ |
| Tickless | one-shot timer基本 | 無駄な割り込み削減 |
| 同期yield | Thread mode直接スイッチ | 例外オーバーヘッド削減 |
| FPU二段階 | 決め打ち方式 | 変動要因排除 |
| BASEPRI方式 | ロックレス + 最短クリティカル | 割り込み遅延削減 |
| SPSC標準 | 1:1通信パターン | ロック排除 |
| 静的メモリ | malloc/freeなし | 断片化・キャッシュ問題解消 |
| 計測組込み | DWT CYCCNT常時計測 | 継続的監視 |

### 2.3 統合評価

**両計画は補完関係にある：**

| 観点 | 以前のPhase | 新規提案 | 統合方針 |
|------|-------------|----------|----------|
| スケジューラ | ビットマップ化 | 優先度数制限 | **両方採用** |
| yield最適化 | fast path追加 | 同期スイッチ | **同一内容** |
| FPU管理 | 所有権モデル | 二段階方式 | **二段階が上位** |
| 割り込み | BASEPRI統一 | BASEPRI + ロックレス | **新規が詳細** |
| 計測 | DWT統合 | 計測組込み | **同一内容** |
| **追加項目** | - | Tickless、電力管理 | **新規採用** |

**結論**: 新規提案を基盤とし、以前のPhaseの具体的実装詳細を組み込む

---

## 3. 統合実装計画

### Phase 1: コア最適化（最大効果）

**目標**: コンテキストスイッチ40-50%高速化

#### 1.1 ビットマップO(1)スケジューラ

```cpp
// lib/umios/kernel/umi_kernel.hh

template <std::size_t MaxTasks, std::size_t MaxTimers, class HW, std::size_t MaxCores = 2>
class Kernel {
    // 優先度ビットマップ（4優先度 = 4ビット使用）
    std::atomic<uint8_t> ready_bitmap_ {0};

    // 各優先度のタスクキュー（ラウンドロビン用）
    struct PriorityQueue {
        uint8_t head {0xFF};  // 先頭タスクID（0xFF = 空）
        uint8_t tail {0xFF};
        uint8_t count {0};
    };
    std::array<PriorityQueue, 4> priority_queues_ {};

    // O(1)タスク選択
    std::optional<TaskId> get_next_task_fast() noexcept {
        uint8_t bitmap = ready_bitmap_.load(std::memory_order_acquire);
        if (bitmap == 0) return std::nullopt;

        // CTZ: Count Trailing Zeros → 最高優先度（最小値）を1命令で取得
        uint8_t priority = __builtin_ctz(bitmap);
        auto& queue = priority_queues_[priority];

        if (queue.head == 0xFF) return std::nullopt;
        return TaskId{queue.head};
    }

    void mark_ready(TaskId id, Priority prio) noexcept {
        uint8_t p = static_cast<uint8_t>(prio);
        auto& queue = priority_queues_[p];

        // タスクをキューに追加
        if (queue.tail != 0xFF) {
            tasks[queue.tail].next = id.value;
        } else {
            queue.head = id.value;
        }
        queue.tail = id.value;
        tasks[id.value].next = 0xFF;
        queue.count++;

        // ビットマップ更新
        ready_bitmap_.fetch_or(1u << p, std::memory_order_release);
    }

    void mark_blocked(TaskId id, Priority prio) noexcept {
        uint8_t p = static_cast<uint8_t>(prio);
        auto& queue = priority_queues_[p];

        // タスクをキューから削除（簡略化：先頭のみ対応）
        if (queue.head == id.value) {
            queue.head = tasks[id.value].next;
            if (queue.head == 0xFF) {
                queue.tail = 0xFF;
                // キューが空になったらビットクリア
                ready_bitmap_.fetch_and(~(1u << p), std::memory_order_release);
            }
            queue.count--;
        }
    }
};
```

**効果**: スケジューラ選択 ~50サイクル → ~5サイクル

#### 1.2 同期yield fast path

```cpp
// lib/umios/kernel/port/cm4/fast_switch.hh

namespace umi::port::cm4 {

/// Thread-modeからの直接コンテキストスイッチ
/// ISRからは使用不可（PendSV経由）
[[gnu::naked, gnu::noinline]]
void yield_direct() noexcept {
    asm volatile(
        // ===== 現在のコンテキスト保存 =====
        "   mrs     r0, psp                 \n"
        "   tst     lr, #0x10               \n"  // FPU使用チェック
        "   it      eq                      \n"
        "   vstmdbeq r0!, {s16-s31}         \n"  // FPU使用時のみ保存
        "   stmdb   r0!, {r4-r11, lr}       \n"

        // 現在のTCBにスタック保存
        "   ldr     r1, =g_current_tcb      \n"
        "   ldr     r2, [r1]                \n"
        "   str     r0, [r2]                \n"

        // ===== 次タスク選択（クリティカルセクション） =====
        "   mov     r0, %[basepri]          \n"
        "   msr     basepri, r0             \n"
        "   dsb                             \n"
        "   isb                             \n"

        "   bl      select_next_task_asm    \n"  // r0 = next TCB

        "   mov     r1, #0                  \n"
        "   msr     basepri, r1             \n"

        // ===== 新コンテキスト復元 =====
        "   ldr     r1, =g_current_tcb      \n"
        "   str     r0, [r1]                \n"  // current = next
        "   ldr     r0, [r0]                \n"  // r0 = stack_ptr

        "   ldmia   r0!, {r4-r11, lr}       \n"
        "   tst     lr, #0x10               \n"
        "   it      eq                      \n"
        "   vldmiaeq r0!, {s16-s31}         \n"
        "   msr     psp, r0                 \n"
        "   bx      lr                      \n"
        :
        : [basepri] "i" (KERNEL_BASEPRI)
        : "memory"
    );
}

/// fast path使用可否判定
inline bool can_use_fast_yield() noexcept {
    uint32_t ipsr;
    asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr == 0;  // Thread modeのみ
}

} // namespace umi::port::cm4

// カーネルAPI
void Kernel::yield() {
    if (port::cm4::can_use_fast_yield()) {
        port::cm4::yield_direct();  // ~80サイクル
    } else {
        HW::request_context_switch();  // PendSV経由 ~200サイクル
    }
}
```

**効果**: yield ~200サイクル → ~80サイクル

---

### Phase 2: FPU戦略の決め打ち

**目標**: FPUコンテキスト処理の変動排除

#### 2.1 FPUポリシー定義

```cpp
// lib/umios/kernel/umi_kernel.hh

/// FPU使用ポリシー
enum class FpuPolicy : uint8_t {
    /// FPU使用禁止（非FPUタスク）
    /// コンテキストスイッチ時にFPU保存/復元なし
    Forbidden = 0,

    /// FPU独占（オーディオタスク用）
    /// 他タスクはFPU禁止のため、保存不要
    Exclusive = 1,

    /// 遅延保存（従来方式、将来の拡張用）
    LazyStack = 2,
};

struct TaskConfig {
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
    Priority prio = Priority::User;
    FpuPolicy fpu_policy = FpuPolicy::Forbidden;  // デフォルト: FPU禁止
    uint8_t core_affinity = static_cast<uint8_t>(Core::Any);
};
```

#### 2.2 FPU所有権モデル

```cpp
// lib/umios/kernel/fpu_owner.hh

namespace umi::kernel {

/// FPU所有権管理
/// 単一タスクがFPUを「所有」し、他タスクはFPU使用禁止
class FpuOwnership {
    std::optional<TaskId> owner_ {};

public:
    /// FPU所有権を設定（起動時に1回のみ）
    bool set_owner(TaskId id) noexcept {
        if (owner_.has_value()) return false;
        owner_ = id;
        return true;
    }

    /// 現在のタスクがFPU所有者か
    bool is_owner(TaskId id) const noexcept {
        return owner_.has_value() && owner_->value == id.value;
    }

    /// FPU保存が必要か判定
    /// owner → non-owner: 保存不要（non-ownerはFPU使わない）
    /// non-owner → owner: 復元不要（ownerの状態は保持されている）
    bool needs_save(TaskId from, TaskId to) const noexcept {
        // 同一タスク間: 不要
        if (from.value == to.value) return false;
        // owner間の切り替えはない（ownerは1つ）
        // owner → non-owner: 不要
        // non-owner → owner: 不要
        return false;  // 常に不要！
    }
};

} // namespace
```

#### 2.3 FPCCR明示設定

```cpp
// lib/umios/backend/cm/cortex_m4.hh

namespace umi::backend::cm4 {

/// FPU Lazy Stackingの明示設定
inline void configure_fpu() noexcept {
    // FPU有効化
    SCB->CPACR |= (0xF << 20);  // CP10, CP11 full access

    // FPCCR設定
    // ASPEN=1: 自動状態保存有効
    // LSPEN=1: 遅延状態保存有効（ただしExclusiveモードでは実質無効）
    FPU->FPCCR = FPU_FPCCR_ASPEN_Msk | FPU_FPCCR_LSPEN_Msk;

    __DSB();
    __ISB();
}

/// FPU例外フレームが保留中か
inline bool is_lazy_state_pending() noexcept {
    return (FPU->FPCCR & FPU_FPCCR_LSPACT_Msk) != 0;
}

} // namespace
```

**効果**: FPUスイッチ ~100サイクル → 0サイクル（Exclusiveモード時）

---

### Phase 3: Tickless + 電力管理

**目標**: アイドル時消費電力最小化

#### 3.1 Tickless基盤

```cpp
// lib/umios/kernel/tickless.hh

namespace umi::kernel {

/// Ticklessタイマー管理
template <class HW>
class TicklessTimer {
    usec next_wakeup_ {UINT64_MAX};  // 次の起床時刻
    bool audio_active_ {false};       // オーディオ処理中フラグ

public:
    /// 次のタイムアウトを設定
    void schedule_wakeup(usec target) noexcept {
        if (target < next_wakeup_) {
            next_wakeup_ = target;
            HW::set_timer_absolute(target);
        }
    }

    /// タイマー割り込みハンドラ
    void on_timer_irq() noexcept {
        usec now = HW::monotonic_time_usecs();
        next_wakeup_ = UINT64_MAX;

        // 次のタイムアウトを再計算
        // ...
    }

    /// アイドル時の処理
    /// オーディオ非アクティブ時のみ深いスリープ可能
    void enter_idle() noexcept {
        if (!audio_active_ && next_wakeup_ == UINT64_MAX) {
            // 深いスリープ（Stop mode等）
            HW::enter_deep_sleep();
        } else {
            // 軽いスリープ（WFI）
            HW::wfi();
        }
    }
};

} // namespace
```

#### 3.2 クロックスケーリング

```cpp
// lib/umios/backend/cm/stm32f4/power.hh

namespace umi::backend::stm32f4 {

/// クロックプロファイル
enum class ClockProfile : uint8_t {
    Full,      // 168MHz - オーディオ処理中
    Medium,    // 84MHz  - 軽負荷
    Low,       // 24MHz  - アイドル
    Sleep,     // HSI 16MHz + Stop mode
};

/// 電力管理
class PowerManager {
    ClockProfile current_ {ClockProfile::Full};

public:
    /// クロックプロファイル変更
    void set_profile(ClockProfile profile) noexcept {
        if (profile == current_) return;

        switch (profile) {
        case ClockProfile::Full:
            // PLL有効、168MHz
            RCC::switch_to_pll_168mhz();
            break;

        case ClockProfile::Medium:
            // PLL有効、84MHz（分周変更）
            RCC::set_ahb_prescaler(2);
            break;

        case ClockProfile::Low:
            // HSE直接、24MHz
            RCC::switch_to_hse();
            break;

        case ClockProfile::Sleep:
            // Stop mode準備
            prepare_stop_mode();
            break;
        }

        current_ = profile;
        update_systick_reload();  // SysTickリロード値更新
    }

    /// オーディオ処理開始時に呼び出し
    void on_audio_start() noexcept {
        set_profile(ClockProfile::Full);
    }

    /// アイドル突入時に呼び出し
    void on_idle_enter() noexcept {
        // オーディオ非アクティブならクロックダウン
        if (!audio_active_) {
            set_profile(ClockProfile::Low);
        }
    }

private:
    void prepare_stop_mode() noexcept {
        // フラッシュ電力ダウン有効
        // 電圧レギュレータ低電力モード
        // WFI後にStop mode突入
    }

    void update_systick_reload() noexcept {
        // クロック変更に合わせてSysTickリロード値を更新
        // Ticklessでは通常不要だが、フォールバック用
    }
};

} // namespace
```

#### 3.3 アイドルタスク改善

```cpp
// Idle Task実装

void idle_task_entry(void*) {
    auto& kernel = get_kernel();
    auto& power = get_power_manager();

    while (true) {
        // 次の起床時刻を取得
        auto next = kernel.timers.next_expiry(kernel.time_us);

        if (next.has_value()) {
            // タイムアウトあり: one-shotタイマー設定
            HW::set_timer_absolute(*next);
        }

        // 電力プロファイル調整
        power.on_idle_enter();

        // スリープ（WFI or Stop mode）
        if (power.can_deep_sleep()) {
            HW::enter_stop_mode();
        } else {
            HW::wfi();
        }

        // 起床後、フルクロックに戻す（オーディオ用）
        power.on_wakeup();
    }
}
```

**効果**: アイドル時消費電力 ~50-80%削減（プロファイル依存）

---

### Phase 4: MPU抽象化レイヤー

**目標**: MPU有無に関わらず同一APIで動作

#### 4.1 Protection抽象化

```cpp
// lib/umios/kernel/protection.hh

namespace umi::kernel {

/// メモリ保護ポリシー
enum class ProtectionMode : uint8_t {
    /// MPU有効、非特権モードで実行
    /// セキュリティ最大、オーバーヘッドあり
    Full,

    /// MPU無効、特権モードで実行
    /// MPUなしMCU向け、または開発時
    Privileged,

    /// MPU有効、特権モードで実行（デバッグ用）
    PrivilegedWithMpu,
};

/// メモリ保護インターフェース
template <class HW, ProtectionMode Mode = ProtectionMode::Full>
class Protection {
public:
    /// 初期化
    static void init() noexcept {
        if constexpr (Mode == ProtectionMode::Full ||
                      Mode == ProtectionMode::PrivilegedWithMpu) {
            if (HW::mpu_available()) {
                configure_mpu();
            }
        }
    }

    /// アプリケーション領域設定
    static void configure_app(const AppRuntime& runtime,
                              void* shared_base,
                              size_t shared_size) noexcept {
        if constexpr (Mode == ProtectionMode::Full ||
                      Mode == ProtectionMode::PrivilegedWithMpu) {
            if (HW::mpu_available()) {
                mpu::configure_app_regions(runtime, shared_base, shared_size,
                                           kernel_base(), kernel_size());
            }
        }
        // Privilegedモードでは何もしない
    }

    /// アプリケーション開始
    [[noreturn]] static void start_app(void* stack_top, void (*entry)()) noexcept {
        if constexpr (Mode == ProtectionMode::Full) {
            // 非特権モードで実行
            HW::enter_user_mode(reinterpret_cast<uint32_t>(stack_top), entry);
        } else {
            // 特権モードで実行
            HW::set_psp(reinterpret_cast<uint32_t>(stack_top));
            entry();
            while (true) { HW::wfi(); }
        }
    }

    /// Syscallが必要か
    static constexpr bool needs_syscall() noexcept {
        return Mode == ProtectionMode::Full;
    }

private:
    static void configure_mpu() noexcept {
        // Region 0: Kernel (privileged only)
        // Region 1: App .text (RX)
        // Region 2: App .data (RW)
        // Region 3: App stack (RW)
        // Region 4: Shared (RW)
        // Region 5: Peripherals (privileged only)
    }
};

} // namespace
```

#### 4.2 コンパイル時選択

```cpp
// lib/umios/kernel/config.hh

namespace umi::kernel {

/// プラットフォーム検出
#if defined(__ARM_ARCH) && defined(UMIOS_USE_MPU)
    constexpr ProtectionMode DEFAULT_PROTECTION = ProtectionMode::Full;
#elif defined(__ARM_ARCH)
    constexpr ProtectionMode DEFAULT_PROTECTION = ProtectionMode::Privileged;
#else
    constexpr ProtectionMode DEFAULT_PROTECTION = ProtectionMode::Privileged;
#endif

/// カーネル型定義
template <std::size_t MaxTasks = 8,
          std::size_t MaxTimers = 16,
          ProtectionMode Mode = DEFAULT_PROTECTION>
using KernelType = Kernel<MaxTasks, MaxTimers, PlatformHW, 2, Mode>;

} // namespace
```

#### 4.3 Syscall条件分岐

```cpp
// lib/umios/app/umi_app.hh

namespace umi {

/// プロセッサ登録（MPUモードに応じて実装切替）
template <typename Processor>
int register_processor(Processor& proc) {
#if defined(UMIOS_PRIVILEGED_MODE)
    // 特権モード: 直接カーネル関数呼び出し
    return kernel::register_processor_direct(&proc);
#else
    // 非特権モード: Syscall経由
    return syscall::call(syscall::nr::RegisterProc,
                         reinterpret_cast<uint32_t>(&proc));
#endif
}

/// イベント待機
inline uint32_t wait_event(uint32_t mask, uint32_t timeout_us = 0) {
#if defined(UMIOS_PRIVILEGED_MODE)
    return kernel::wait_event_direct(mask, timeout_us);
#else
    return syscall::call(syscall::nr::WaitEvent, mask, timeout_us);
#endif
}

} // namespace
```

**効果**: MPUなしMCUでも同一アプリが動作、開発時のデバッグ容易化

---

### Phase 5: 計測・監視基盤

**目標**: 継続的性能監視と最適化の基盤

#### 5.1 DWT統合

```cpp
// lib/umios/kernel/metrics.hh

#if defined(UMI_ENABLE_METRICS)

namespace umi::kernel {

/// カーネルメトリクス
struct Metrics {
    // コンテキストスイッチ
    struct {
        uint32_t count {0};
        uint32_t cycles_min {UINT32_MAX};
        uint32_t cycles_max {0};
        uint64_t cycles_sum {0};
    } context_switch;

    // ISRレイテンシ
    struct {
        uint32_t dma_audio_max {0};
        uint32_t usb_max {0};
        uint32_t systick_max {0};
    } isr_latency;

    // オーディオ処理
    struct {
        uint32_t cycles_last {0};
        uint32_t cycles_max {0};
        uint32_t overruns {0};
        uint32_t underruns {0};
    } audio;

    // スケジューラ
    struct {
        uint32_t preemptions {0};
        uint32_t yields {0};
        uint32_t idle_entries {0};
    } scheduler;

    /// 統計リセット
    void reset() noexcept {
        *this = Metrics{};
    }

    /// 平均コンテキストスイッチサイクル
    uint32_t avg_switch_cycles() const noexcept {
        if (context_switch.count == 0) return 0;
        return context_switch.cycles_sum / context_switch.count;
    }
};

inline Metrics g_metrics;

/// サイクル計測RAII
class ScopedCycles {
    uint32_t start_;
    uint32_t* target_;
public:
    ScopedCycles(uint32_t* target) noexcept
        : start_(DWT->CYCCNT), target_(target) {}
    ~ScopedCycles() noexcept {
        *target_ = DWT->CYCCNT - start_;
    }
};

/// コンテキストスイッチ計測
inline void record_switch_cycles(uint32_t cycles) noexcept {
    auto& cs = g_metrics.context_switch;
    cs.count++;
    cs.cycles_sum += cycles;
    if (cycles < cs.cycles_min) cs.cycles_min = cycles;
    if (cycles > cs.cycles_max) cs.cycles_max = cycles;
}

} // namespace

#define UMI_MEASURE_SWITCH(var) \
    uint32_t var##_start = DWT->CYCCNT
#define UMI_RECORD_SWITCH(var) \
    umi::kernel::record_switch_cycles(DWT->CYCCNT - var##_start)

#else

#define UMI_MEASURE_SWITCH(var) ((void)0)
#define UMI_RECORD_SWITCH(var) ((void)0)

#endif
```

#### 5.2 シェルコマンド拡張

```cpp
// lib/umios/kernel/shell_commands.hh

// show cpu コマンド拡張
void cmd_show_cpu(ShellContext& ctx) {
    auto& m = kernel::g_metrics;

    ctx.println("=== CPU Metrics ===");
    ctx.printf("Context switches: %lu\n", m.context_switch.count);
    ctx.printf("  Min cycles: %lu\n", m.context_switch.cycles_min);
    ctx.printf("  Max cycles: %lu\n", m.context_switch.cycles_max);
    ctx.printf("  Avg cycles: %lu\n", m.avg_switch_cycles());
    ctx.println("");
    ctx.printf("Audio processing:\n");
    ctx.printf("  Last cycles: %lu\n", m.audio.cycles_last);
    ctx.printf("  Max cycles:  %lu\n", m.audio.cycles_max);
    ctx.printf("  Overruns:    %lu\n", m.audio.overruns);
    ctx.printf("  Underruns:   %lu\n", m.audio.underruns);
    ctx.println("");
    ctx.printf("Scheduler:\n");
    ctx.printf("  Preemptions: %lu\n", m.scheduler.preemptions);
    ctx.printf("  Yields:      %lu\n", m.scheduler.yields);
    ctx.printf("  Idle entries: %lu\n", m.scheduler.idle_entries);
}
```

---

## 4. 実装ロードマップ

```
Week 1-2: Phase 1（コア最適化）
├── Day 1-2: ビットマップスケジューラ実装
├── Day 3-4: 同期yield fast path実装
├── Day 5-6: テスト・ベンチマーク
└── Day 7: ドキュメント更新

Week 3: Phase 2（FPU戦略）
├── Day 1-2: FPUポリシー実装
├── Day 3: FPCCR設定
├── Day 4-5: テスト
└── Day 6-7: 既存コードとの統合

Week 4: Phase 3（電力管理）
├── Day 1-2: Tickless基盤実装
├── Day 3-4: クロックスケーリング実装
├── Day 5: アイドルタスク改善
└── Day 6-7: 消費電力測定・調整

Week 5: Phase 4（MPU抽象化）
├── Day 1-2: Protection抽象化レイヤー
├── Day 3: コンパイル時選択機構
├── Day 4-5: Syscall条件分岐
└── Day 6-7: MPUなし環境でのテスト

Week 6: Phase 5（計測基盤）
├── Day 1-2: DWT統合
├── Day 3: シェルコマンド拡張
├── Day 4-5: ベンチマークスイート
└── Day 6-7: 最終調整・ドキュメント
```

---

## 5. 期待される改善効果

### 5.1 性能指標

| 指標 | 現状 | Phase 1後 | Phase 2後 | 最終目標 |
|------|------|-----------|-----------|----------|
| **コンテキストスイッチ** | 200-400cyc | 100-200cyc | 80-150cyc | <150cyc |
| **スケジューラ選択** | O(n) 50cyc | O(1) 5cyc | 同左 | O(1) |
| **タスクyield** | 200cyc | 80cyc | 同左 | <100cyc |
| **FPUスイッチ追加** | 100cyc | 同左 | 0cyc | 0cyc |

### 5.2 消費電力（概算）

| 状態 | 現状 | Phase 3後 |
|------|------|-----------|
| **オーディオ処理中** | 168MHz常時 | 168MHz（変更なし）|
| **軽負荷** | 168MHz | 84MHz（~40%削減）|
| **アイドル** | 168MHz + WFI | 24MHz + WFI（~80%削減）|
| **深いスリープ** | 未対応 | Stop mode（~95%削減）|

### 5.3 ポータビリティ

| プラットフォーム | 現状 | Phase 4後 |
|------------------|------|-----------|
| **STM32F4（MPUあり）** | ✅ 完全対応 | ✅ 完全対応 |
| **STM32F1（MPUなし）** | ❌ 非対応 | ✅ 特権モードで対応 |
| **WASM** | ✅ スタブ | ✅ 同左 |
| **RISC-V** | ❌ 未対応 | ⚠️ 将来対応基盤 |

---

## 6. リスクと対策

| リスク | 影響 | 対策 |
|--------|------|------|
| fast path導入によるバグ | 高 | 段階的導入、広範なテスト |
| クロックスケーリングによるオーディオ途切れ | 高 | オーディオ中は常にFull |
| MPU無効化によるセキュリティ低下 | 中 | 本番はFull必須、開発のみPrivileged |
| 計測オーバーヘッド | 低 | コンパイル時無効化 |

---

## 7. 参考文献

- [ARM Application Note 298: Cortex-M4(F) Lazy Stacking](https://developer.arm.com/documentation/dai0298/a/)
- [Wang & Saksena: Preemption Threshold Scheduling (IEEE, 1999)](https://ieeexplore.ieee.org/document/811269/)
- [Memfault: ARM Cortex-M RTOS Context Switching](https://interrupt.memfault.com/blog/cortex-m-rtos-context-switching)
- [SEGGER embOS Performance](https://www.segger.com/products/rtos/embos/technology/performance/)
- [PX5 RTOS](https://px5rtos.com/fastest-rtos/)
- [Zephyr ARM Cortex-M Context Switch Optimization](https://github.com/zephyrproject-rtos/zephyr/issues/79069)

---

*Document Version: 2.0*
*Created: 2025-01-25*
*Author: Claude Code*
