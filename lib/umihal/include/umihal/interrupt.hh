// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>
#include <cstdint>
#include <umihal/result.hh>

namespace umi::hal {
namespace interrupt {

// 割り込み優先度（相対的な値）
enum class Priority : std::uint8_t {
    LOWEST = 0,
    LOW = 1,
    MEDIUM = 2,
    HIGH = 3,
    HIGHEST = 4
};

// 割り込みハンドラ型
using Handler = void(*)();

} // namespace interrupt

// 割り込み管理インターフェース（共通操作）
template <typename T>
concept InterruptController = requires(T& intc,
                                     std::uint32_t irq_number,
                                     interrupt::Handler handler,
                                     interrupt::Priority priority) {
    // ハンドラ登録（必須）
    { intc.register_handler(irq_number, handler) } -> std::same_as<umi::hal::Result<void>>;

    // 割り込み制御（必須）
    { intc.enable_irq(irq_number) } -> std::same_as<umi::hal::Result<void>>;
    { intc.disable_irq(irq_number) } -> std::same_as<umi::hal::Result<void>>;

    // 優先度設定（オプション - NOT_SUPPORTEDを返してもよい）
    { intc.set_priority(irq_number, priority) } -> std::same_as<umi::hal::Result<void>>;

    // グローバル割り込み制御（必須）
    { intc.enable_global() } -> std::same_as<void>;
    { intc.disable_global() } -> std::same_as<void>;

    // 状態確認（必須）
    { intc.is_enabled(irq_number) } -> std::convertible_to<bool>;
    { intc.is_pending(irq_number) } -> std::convertible_to<bool>;

    // ペンディングクリア（オプション）
    { intc.clear_pending(irq_number) } -> std::same_as<umi::hal::Result<void>>;
};

// クリティカルセクション用RAII
template <InterruptController T>
class CriticalSection {
  public:
    explicit CriticalSection(T& controller) : controller(controller) {
        controller.disable_global();
    }

    ~CriticalSection() {
        controller.enable_global();
    }

    // コピー/ムーブ禁止
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
    CriticalSection(CriticalSection&&) = delete;
    CriticalSection& operator=(CriticalSection&&) = delete;

  private:
    T& controller;
};

} // namespace umi::hal
