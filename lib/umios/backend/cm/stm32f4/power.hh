// SPDX-License-Identifier: MIT
// STM32F4 Power Management and Tickless Support
#pragma once

#include <cstdint>

namespace umi::stm32 {

// PWR (Power Controller) registers
struct PWR {
    static constexpr std::uint32_t BASE = 0x40007000;

    static constexpr std::uint32_t CR = 0x00;
    static constexpr std::uint32_t CSR = 0x04;

    static volatile std::uint32_t& reg(std::uint32_t offset) {
        return *reinterpret_cast<volatile std::uint32_t*>(BASE + offset);
    }

    // CR bits
    static constexpr std::uint32_t CR_LPDS = 1U << 0;   // Low-power deepsleep
    static constexpr std::uint32_t CR_PDDS = 1U << 1;   // Power down deepsleep
    static constexpr std::uint32_t CR_CWUF = 1U << 2;   // Clear wakeup flag
    static constexpr std::uint32_t CR_CSBF = 1U << 3;   // Clear standby flag
    static constexpr std::uint32_t CR_PVDE = 1U << 4;   // Power voltage detector enable
    static constexpr std::uint32_t CR_DBP = 1U << 8;    // Disable backup protection
    static constexpr std::uint32_t CR_FPDS = 1U << 9;   // Flash power down in stop
    static constexpr std::uint32_t CR_VOS_MASK = 3U << 14;
    static constexpr std::uint32_t CR_VOS_SCALE1 = 3U << 14;  // 168MHz
    static constexpr std::uint32_t CR_VOS_SCALE2 = 2U << 14;  // 144MHz max
    static constexpr std::uint32_t CR_VOS_SCALE3 = 1U << 14;  // 120MHz max

    // CSR bits
    static constexpr std::uint32_t CSR_WUF = 1U << 0;   // Wakeup flag
    static constexpr std::uint32_t CSR_SBF = 1U << 1;   // Standby flag
    static constexpr std::uint32_t CSR_PVDO = 1U << 2;  // PVD output
    static constexpr std::uint32_t CSR_BRR = 1U << 3;   // Backup regulator ready
    static constexpr std::uint32_t CSR_EWUP = 1U << 8;  // Enable WKUP pin
    static constexpr std::uint32_t CSR_BRE = 1U << 9;   // Backup regulator enable
    static constexpr std::uint32_t CSR_VOSRDY = 1U << 14; // Regulator voltage scaling ready
};

// SCB (System Control Block) for sleep modes
struct SCB {
    static constexpr std::uint32_t BASE = 0xE000ED00;

    static constexpr std::uint32_t SCR = 0x10;  // System Control Register

    static volatile std::uint32_t& reg(std::uint32_t offset) {
        return *reinterpret_cast<volatile std::uint32_t*>(BASE + offset);
    }

    // SCR bits
    static constexpr std::uint32_t SCR_SLEEPONEXIT = 1U << 1;
    static constexpr std::uint32_t SCR_SLEEPDEEP = 1U << 2;
    static constexpr std::uint32_t SCR_SEVONPEND = 1U << 4;
};

/// Clock profile for power management
enum class ClockProfile : std::uint8_t {
    Full,      ///< 168MHz - Audio processing active
    Medium,    ///< 84MHz  - Light load
    Low,       ///< 24MHz  - Idle, minimal processing
    Sleep,     ///< Stop mode - minimal power
};

/// Sleep mode depth
enum class SleepMode : std::uint8_t {
    WFI,       ///< Wait For Interrupt - quick wakeup (~1us)
    WFE,       ///< Wait For Event
    Stop,      ///< Stop mode - slower wakeup (~5us), lower power
    Standby,   ///< Standby mode - RAM lost, lowest power
};

/// Power management for STM32F4
class PowerManager {
public:
    /// Enter sleep mode (WFI - Wait For Interrupt)
    /// Quick wakeup, minimal power savings
    static void wfi() {
        asm volatile("wfi" ::: "memory");
    }

    /// Enter sleep mode (WFE - Wait For Event)
    static void wfe() {
        asm volatile("wfe" ::: "memory");
    }

    /// Enter stop mode (low power, slower wakeup)
    /// Preserves RAM, wakes on any EXTI or interrupt
    static void enter_stop_mode() {
        // Clear wakeup flag
        PWR::reg(PWR::CR) |= PWR::CR_CWUF;

        // Configure stop mode: Low-power regulator, stop mode
        PWR::reg(PWR::CR) = (PWR::reg(PWR::CR) & ~PWR::CR_PDDS) | PWR::CR_LPDS;

        // Set SLEEPDEEP bit
        SCB::reg(SCB::SCR) |= SCB::SCR_SLEEPDEEP;

        // Data and instruction sync barriers
        asm volatile("dsb" ::: "memory");
        asm volatile("isb" ::: "memory");

        // Enter stop mode
        asm volatile("wfi" ::: "memory");

        // After wakeup: clear SLEEPDEEP
        SCB::reg(SCB::SCR) &= ~SCB::SCR_SLEEPDEEP;
    }

    /// Check if system woke from stop mode
    /// If so, reconfigure clocks (HSI is running after stop)
    static bool woke_from_stop() {
        // After stop mode, system runs on HSI
        // Check if PLL is not ready (indicating we came from stop)
        constexpr std::uint32_t RCC_CR = 0x40023800;
        constexpr std::uint32_t CR_PLLRDY = 1U << 25;
        auto cr = *reinterpret_cast<volatile std::uint32_t*>(RCC_CR);
        return (cr & CR_PLLRDY) == 0;
    }

    /// Prepare for idle (called when no tasks ready)
    /// Returns recommended sleep mode based on next wakeup time
    static SleepMode recommend_sleep_mode(std::uint64_t next_wakeup_us,
                                           std::uint64_t now_us,
                                           bool audio_active) {
        // If audio is active, only light sleep
        if (audio_active) {
            return SleepMode::WFI;
        }

        // Calculate time until next wakeup
        if (next_wakeup_us <= now_us) {
            return SleepMode::WFI;  // Immediate wakeup needed
        }

        auto sleep_time_us = next_wakeup_us - now_us;

        // Stop mode only worthwhile if sleeping > 100us
        // (wakeup latency ~5us, clock stabilization ~10us)
        if (sleep_time_us > 100) {
            return SleepMode::Stop;
        }

        return SleepMode::WFI;
    }

    /// Execute recommended sleep
    static void enter_sleep(SleepMode mode) {
        switch (mode) {
            case SleepMode::WFI:
                wfi();
                break;
            case SleepMode::WFE:
                wfe();
                break;
            case SleepMode::Stop:
                enter_stop_mode();
                break;
            case SleepMode::Standby:
                // Not implemented - RAM would be lost
                wfi();
                break;
        }
    }
};

/// Tickless idle support
/// Manages timer and sleep mode selection for optimal power
template <class HW>
class TicklessIdle {
    bool audio_active_ {false};
    std::uint64_t last_wakeup_ {0};

public:
    /// Set audio activity status
    /// When audio is active, only WFI sleep is allowed
    void set_audio_active(bool active) {
        audio_active_ = active;
    }

    /// Check if audio is active
    bool is_audio_active() const {
        return audio_active_;
    }

    /// Enter idle state with optimal sleep mode
    /// @param next_timer_expiry Absolute time of next timer (UINT64_MAX if none)
    void enter_idle(std::uint64_t next_timer_expiry) {
        auto now = HW::monotonic_time_usecs();

        auto mode = PowerManager::recommend_sleep_mode(
            next_timer_expiry, now, audio_active_);

        // If using stop mode, need to reconfigure clocks after wakeup
        bool use_stop = (mode == SleepMode::Stop);

        PowerManager::enter_sleep(mode);

        // After wakeup
        if (use_stop && PowerManager::woke_from_stop()) {
            // Reconfigure system clocks (PLL, etc.)
            // This would call RCC::init_168mhz() or similar
            // Left to platform-specific init
        }

        last_wakeup_ = HW::monotonic_time_usecs();
    }

    /// Get time of last wakeup
    std::uint64_t last_wakeup_time() const {
        return last_wakeup_;
    }
};

}  // namespace umi::stm32
