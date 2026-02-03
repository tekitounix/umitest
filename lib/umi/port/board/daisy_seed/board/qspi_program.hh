// SPDX-License-Identifier: MIT
// Daisy Seed QSPI Programming (Erase/Write)
// Requires memory-mapped mode to be disabled during programming
#pragma once

#include <cstdint>
#include <mcu/qspi.hh>
#include <transport/direct.hh>

#include "qspi.hh" // Includes flash_is25lp064a.hh

namespace umi::daisy {

/// Exit memory-mapped mode and enter indirect mode
inline void qspi_exit_memory_mapped() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Abort any ongoing operation
    auto cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 1))); // ABORT
    while (t.read(QUADSPI::CR{}) & (1U << 1)) {
    }

    // Clear all flags (TCF, SMF, TEF, TOF) to ensure clean state
    t.write(QUADSPI::FCR::value(0x1F));

    qspi_wait_busy();

    auto cr2 = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr2 & ~1U));
    t.write(QUADSPI::CCR::value(0));
    t.write(QUADSPI::AR::value(0));
    t.write(QUADSPI::DLR::value(0));
    t.write(QUADSPI::CR::value(cr2 | 1U));
}

/// Re-enter memory-mapped mode (call after programming)
inline void qspi_enter_memory_mapped() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Abort any ongoing operation first (like exit_memory_mapped does)
    auto cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 1))); // ABORT
    while (t.read(QUADSPI::CR{}) & (1U << 1)) {
    }

    // Clear all flags
    t.write(QUADSPI::FCR::value(0x1F));

    // Wait for flash to be ready (WIP=0) before entering memory-mapped mode
    // Use simple polling instead of auto-poll to avoid potential hang
    for (int retry = 0; retry < 1000000; ++retry) {
        std::uint8_t status = qspi_read_byte(Flash::READ_STATUS);
        if ((status & Flash::SR_WIP) == 0) {
            break;
        }
    }

    // Wait for QUADSPI controller to be not busy
    qspi_wait_busy();

    // Set Alternate Bytes value (0xA0 for continuous read mode)
    t.write(QUADSPI::ABR::value(0x000000A0));

    // Configure CCR for memory-mapped quad I/O read
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(Flash::QUAD_IO_FAST_READ) |
                                (qspi_mode::SINGLE << 8) | // IMODE: single line for instruction
                                (qspi_mode::QUAD << 10) |  // ADMODE: quad for address
                                (qspi_adsize::ADDR_24BIT << 12) |
                                (qspi_mode::QUAD << 14) |                      // ABMODE: quad for alternate bytes
                                (0U << 16) |                                   // ABSIZE: 8-bit
                                (8U << 18) |                                   // DCYC: 8 dummy cycles
                                (qspi_mode::QUAD << 24) |                      // DMODE: quad for data
                                (qspi_fmode::MEMORY_MAPPED << 26) | (1U << 28) // SIOO
                                ));

    // Invalidate both I-Cache and D-Cache for QSPI region
    qspi_invalidate_icache();
    qspi_invalidate_dcache();
}

/// Erase a 4KB sector at the given address
/// @param address Must be 4KB aligned (0x90000000 base)
/// @return true on success
inline bool qspi_erase_sector(std::uint32_t address) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Convert to flash-relative address
    address &= 0x00FFFFFF;

    qspi_exit_memory_mapped();

    // Enable write
    if (!qspi_write_enable()) {
        qspi_record_fail(1);
        return false;
    }

    {
        std::uint8_t sr = qspi_read_byte(Flash::READ_STATUS);
        if ((sr & Flash::SR_WEL) == 0) {
            qspi_record_fail(7);
            return false;
        }
    }

    qspi_wait_busy();

    // Send sector erase command with address
    // IMPORTANT: Register write order must be DLR → CCR → AR (per STM32 HAL)
    t.write(QUADSPI::DLR::value(0)); // No data
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(Flash::SECTOR_ERASE_ALT) |
                                (qspi_mode::SINGLE << 8) |                                  // IMODE
                                (qspi_mode::SINGLE << 10) |                                 // ADMODE
                                (qspi_adsize::ADDR_24BIT << 12) | (qspi_mode::NONE << 24) | // DMODE: no data
                                (qspi_fmode::INDIRECT_WRITE << 26)));
    t.write(QUADSPI::AR::value(address)); // AR must be AFTER CCR!

    // Wait for erase to complete (can take up to 400ms)
    qspi_wait_ready();

    qspi_enter_memory_mapped();
    return true;
}

/// Erase sectors covering the given range
/// @param start Start address (0x90000000 base)
/// @param size Number of bytes to erase (will be rounded up to sector boundary)
/// @return true on success
inline bool qspi_erase_range(std::uint32_t start, std::uint32_t size) {
    // Align to sector boundaries
    std::uint32_t sector_start = start & ~(Flash::SECTOR_SIZE - 1);
    std::uint32_t end = start + size;

    while (sector_start < end) {
        if (!qspi_erase_sector(sector_start)) {
            return false;
        }
        sector_start += Flash::SECTOR_SIZE;
    }
    return true;
}

/// Erase a single sector without re-entering memory-mapped mode
/// (For use in batch erase operations)
/// @param address Must be 4KB aligned (flash-relative, i.e. already masked to 0x00FFFFFF)
/// @return true on success
inline bool qspi_erase_sector_no_remap(std::uint32_t address) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Enable write
    if (!qspi_write_enable()) {
        return false;
    }

    qspi_wait_busy();

    // Send sector erase command with address
    // IMPORTANT: Register write order must be DLR → CCR → AR (per STM32 HAL)
    t.write(QUADSPI::DLR::value(0)); // No data
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(Flash::SECTOR_ERASE_ALT) |
                                (qspi_mode::SINGLE << 8) |                                  // IMODE
                                (qspi_mode::SINGLE << 10) |                                 // ADMODE
                                (qspi_adsize::ADDR_24BIT << 12) | (qspi_mode::NONE << 24) | // DMODE: no data
                                (qspi_fmode::INDIRECT_WRITE << 26)));
    t.write(QUADSPI::AR::value(address)); // AR must be AFTER CCR!

    // Wait for erase to complete (can take up to 400ms)
    qspi_wait_ready();

    return true;
}

/// Erase sectors with callback between sectors (allows USB polling)
/// @param start Start address (0x90000000 base)
/// @param size Number of bytes to erase
/// @param poll_fn Callback called after each sector erase (for USB polling)
/// @return true on success
template <typename PollFn>
inline bool qspi_erase_range_with_poll(std::uint32_t start, std::uint32_t size, PollFn&& poll_fn) {
    // Exit memory-mapped mode once at the start
    qspi_exit_memory_mapped();

    // Align to sector boundaries
    std::uint32_t sector_start = (start & 0x00FFFFFF) & ~(Flash::SECTOR_SIZE - 1);
    std::uint32_t end = (start & 0x00FFFFFF) + size;

    while (sector_start < end) {
        if (!qspi_erase_sector_no_remap(sector_start)) {
            qspi_enter_memory_mapped();
            return false;
        }

        // Call poll function between sector erases
        poll_fn();

        sector_start += Flash::SECTOR_SIZE;
    }

    // Re-enter memory-mapped mode at the end
    qspi_enter_memory_mapped();
    return true;
}

/// Program a single page (up to 256 bytes)
/// @param address Flash address (0x90000000 base)
/// @param data Pointer to data
/// @param len Number of bytes (max 256)
/// @param reset_mode If true, exits and re-enters memory-mapped mode (default: true)
/// @return true on success
inline bool
qspi_program_page(std::uint32_t address, const std::uint8_t* data, std::uint32_t len, bool reset_mode = true) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    if (len == 0 || len > Flash::PAGE_SIZE) {
        return false;
    }

    // Convert to flash-relative address
    if (address >= 0x90000000) {
        address -= 0x90000000;
    }

    if (reset_mode) {
        qspi_set_mode(QspiMode::INDIRECT_POLLING);
    }

    t.write(QUADSPI::FCR::value(0x1F));

    // Enable write
    if (!qspi_write_enable()) {
        qspi_record_fail(1);
        return false;
    }

    qspi_wait_busy();

    // IMPORTANT: Register write order must be DLR → CCR → AR (per STM32 HAL)
    // Set data length
    t.write(QUADSPI::DLR::value(len - 1));

    // Configure for page program (single line) - CCR must be BEFORE AR!
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(Flash::PAGE_PROGRAM) | (qspi_mode::SINGLE << 8) | // IMODE
                                (qspi_mode::SINGLE << 10) |                                                  // ADMODE
                                (qspi_adsize::ADDR_24BIT << 12) |
                                (qspi_mode::SINGLE << 24) | // DMODE: single line for data
                                (qspi_fmode::INDIRECT_WRITE << 26)));

    // Set address - AR must be AFTER CCR!
    t.write(QUADSPI::AR::value(address));

    // Write data to FIFO using HAL-like abstraction
    for (std::uint32_t i = 0; i < len; ++i) {
        // Wait for FIFO Threshold Flag (FTF) - indicates FIFO has room
        // FTF is set when FIFO level <= FTHRES
        std::uint32_t timeout = 100000;
        while (!(t.read(QUADSPI::SR{}) & (1U << 2))) {
            if (--timeout == 0) {
                t.write(QUADSPI::CR::value(t.read(QUADSPI::CR{}) | (1U << 1)));
                while (t.read(QUADSPI::CR{}) & (1U << 1)) {
                }
                t.write(QUADSPI::FCR::value(0x1F));
                qspi_record_fail(2);
                return false;
            }
        }
        qspi_write_dr_byte(data[i]);
    }

    std::uint32_t tc_timeout = 100000;
    while (tc_timeout > 0) {
        std::uint32_t sr = t.read(QUADSPI::SR{});
        if (sr & (1U << 1)) {
            break;
        }
        if (sr & (1U << 0)) {
            t.write(QUADSPI::CR::value(t.read(QUADSPI::CR{}) | (1U << 1)));
            while (t.read(QUADSPI::CR{}) & (1U << 1)) {
            }
            t.write(QUADSPI::FCR::value(0x1F));
            qspi_record_fail(3);
            return false;
        }
        if (sr & (1U << 4)) {
            t.write(QUADSPI::CR::value(t.read(QUADSPI::CR{}) | (1U << 1)));
            while (t.read(QUADSPI::CR{}) & (1U << 1)) {
            }
            t.write(QUADSPI::FCR::value(0x1F));
            qspi_record_fail(4);
            return false;
        }
        --tc_timeout;
    }
    if (tc_timeout == 0) {
        t.write(QUADSPI::CR::value(t.read(QUADSPI::CR{}) | (1U << 1)));
        while (t.read(QUADSPI::CR{}) & (1U << 1)) {
        }
        t.write(QUADSPI::FCR::value(0x1F));
        qspi_record_fail(5);
        return false;
    }
    t.write(QUADSPI::FCR::value(1U << 1));

    // Wait for programming to complete
    if (!qspi_wait_ready()) {
        qspi_record_fail(6);
        return false;
    }

    if (reset_mode) {
        qspi_set_mode(QspiMode::MEMORY_MAPPED);
    }
    return true;
}

/// Program data to QSPI flash
/// @param address Flash address (0x90000000 base)
/// @param data Pointer to data
/// @param len Number of bytes
/// @return true on success
inline bool qspi_program(std::uint32_t address, const std::uint8_t* data, std::uint32_t len) {
    // Convert to flash-relative address for calculations
    std::uint32_t flash_addr = address & 0x00FFFFFF;

    // Set indirect polling mode once at the start (like libDaisy SetMode)
    qspi_set_mode(QspiMode::INDIRECT_POLLING);

    while (len > 0) {
        // Calculate bytes to write in this page
        std::uint32_t page_offset = flash_addr % Flash::PAGE_SIZE;
        std::uint32_t page_remain = Flash::PAGE_SIZE - page_offset;
        std::uint32_t chunk = (len < page_remain) ? len : page_remain;

        // Mode already set to INDIRECT_POLLING, no need to switch per page
        if (!qspi_program_page(flash_addr | 0x90000000, data, chunk, false)) {
            qspi_set_mode(QspiMode::MEMORY_MAPPED);
            return false;
        }

        flash_addr += chunk;
        data += chunk;
        len -= chunk;
    }

    // Return to memory-mapped mode at the end (like libDaisy SetMode)
    qspi_set_mode(QspiMode::MEMORY_MAPPED);
    return true;
}

/// Verify data in QSPI flash
/// @param address Flash address (0x90000000 base)
/// @param data Expected data
/// @param len Number of bytes
/// @return true if data matches
inline bool qspi_verify(std::uint32_t address, const std::uint8_t* data, std::uint32_t len) {
    const auto* flash = reinterpret_cast<const std::uint8_t*>(address);
    for (std::uint32_t i = 0; i < len; ++i) {
        if (flash[i] != data[i]) {
            return false;
        }
    }
    return true;
}

} // namespace umi::daisy
