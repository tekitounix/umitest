// SPDX-License-Identifier: MIT
// UMI-OS Fault Handler
// Application fault handling and logging

#pragma once

#include <common/fault.hh>
#include <cstdint>
#include <cstddef>
#include <atomic>

namespace umi::kernel {

// ============================================================================
// Fault Log Entry
// ============================================================================

/// Fault ログエントリ
struct FaultLogEntry {
    backend::cm::FaultInfo info;  ///< フォルト情報
    uint32_t timestamp_ms;        ///< 発生時刻 (ms)
};

// ============================================================================
// Fault Log (Ring Buffer)
// ============================================================================

/// Fault ログ（リングバッファ）
/// 最新 N 件のフォルト情報を保持
template<size_t N = 8>
class FaultLog {
public:
    /// エントリを追加
    void push(const FaultLogEntry& entry) noexcept {
        entries_[write_idx_ % N] = entry;
        write_idx_++;
    }

    /// 総エントリ数（オーバーフロー含む）
    [[nodiscard]] size_t count() const noexcept { return write_idx_; }

    /// 保持中のエントリ数
    [[nodiscard]] size_t size() const noexcept {
        return (write_idx_ > N) ? N : write_idx_;
    }

    /// インデックスでエントリを取得
    /// @param idx 0 = 最古、size()-1 = 最新
    /// @return エントリへのポインタ、範囲外なら nullptr
    [[nodiscard]] const FaultLogEntry* get(size_t idx) const noexcept {
        if (idx >= size()) return nullptr;
        size_t start = (write_idx_ > N) ? (write_idx_ - N) : 0;
        return &entries_[(start + idx) % N];
    }

    /// 最新のエントリを取得
    [[nodiscard]] const FaultLogEntry* latest() const noexcept {
        if (write_idx_ == 0) return nullptr;
        return &entries_[(write_idx_ - 1) % N];
    }

    /// ログをクリア
    void clear() noexcept { write_idx_ = 0; }

private:
    FaultLogEntry entries_[N]{};
    size_t write_idx_ = 0;
};

// ============================================================================
// Global Fault State
// ============================================================================

/// グローバル Fault ログ
///
/// **配置場所:** カーネルの .bss セクション（KERNEL_RAM 内）
/// - APP_RAM (0x2000C000-0x20017FFF) とは完全に分離
/// - アプリがクラッシュしても保持される
/// - MCU リセット時はゼロクリア
///
/// **SharedMemory との違い:**
/// - SharedMemory: カーネル/アプリ間の共有データ（アプリからアクセス可能）
/// - g_fault_log: カーネル専用（アプリからはアクセス不可、MPU で保護）
inline FaultLog<8> g_fault_log;

/// Fault 発生フラグ（ISR から設定、メインループで処理）
inline std::atomic<bool> g_fault_pending{false};

/// 一時保存された Fault 情報（ISR で設定、メインループで処理）
inline FaultLogEntry g_pending_fault{};

// ============================================================================
// Error LED Patterns
// ============================================================================

/// エラー LED パターン
enum class ErrorLedPattern : uint8_t {
    NONE = 0,
    STACK_OVERFLOW,      ///< 赤 点滅
    ACCESS_VIOLATION,    ///< 赤 2回点滅
    INVALID_INSTRUCTION, ///< 赤 3回点滅
    BUS_FAULT,           ///< 赤 4回点滅
    UNKNOWN,             ///< 赤 高速点滅
};

// ============================================================================
// Fault Handler Functions
// ============================================================================

/// ISR から呼ぶ: Fault 情報を一時保存してフラグを立てる
///
/// この関数は最小限の処理のみ行う（ISR 内で安全に呼べる）:
/// - g_pending_fault に FaultInfo をコピー
/// - g_fault_pending を true に設定
///
/// 実際のログ保存やアプリ終了は process_pending_fault() で行う
///
/// @param info キャプチャされた Fault 情報
/// @param timestamp_ms 発生時刻（ミリ秒）
inline void record_fault(const backend::cm::FaultInfo& info, uint32_t timestamp_ms) noexcept {
    g_pending_fault.info = info;
    g_pending_fault.timestamp_ms = timestamp_ms;
    g_fault_pending.store(true, std::memory_order_release);
}

/// メインループから呼ぶ: 保留中の Fault を処理
///
/// 以下の処理を行う:
/// - g_fault_log にエントリを追加
/// - g_fault_pending をクリア
///
/// @return true: Fault を処理した、false: 保留中の Fault なし
inline bool process_pending_fault() noexcept {
    if (!g_fault_pending.load(std::memory_order_acquire)) {
        return false;
    }

    // ログに追加（ISR では行わない）
    g_fault_log.push(g_pending_fault);

    // フラグをクリア
    g_fault_pending.store(false, std::memory_order_release);

    return true;
}

/// Fault 種別からエラーパターンを判定
///
/// @param info Fault 情報
/// @return 対応するエラー LED パターン
inline ErrorLedPattern classify_fault(const backend::cm::FaultInfo& info) noexcept {
    using namespace backend::cm;

    // MemManage Fault
    if (info.cfsr & cfsr::MSTKERR) {
        return ErrorLedPattern::STACK_OVERFLOW;
    }
    if (info.cfsr & (cfsr::IACCVIOL | cfsr::DACCVIOL)) {
        return ErrorLedPattern::ACCESS_VIOLATION;
    }

    // UsageFault
    if (info.cfsr & cfsr::UNDEFINSTR) {
        return ErrorLedPattern::INVALID_INSTRUCTION;
    }

    // BusFault
    if (info.cfsr & (cfsr::IBUSERR | cfsr::PRECISERR | cfsr::IMPRECISERR)) {
        return ErrorLedPattern::BUS_FAULT;
    }

    return ErrorLedPattern::UNKNOWN;
}

/// Fault が保留中かどうか
[[nodiscard]] inline bool has_pending_fault() noexcept {
    return g_fault_pending.load(std::memory_order_acquire);
}

/// 保留中の Fault 情報を取得
[[nodiscard]] inline const FaultLogEntry& pending_fault() noexcept {
    return g_pending_fault;
}

} // namespace umi::kernel
