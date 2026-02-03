// SPDX-License-Identifier: MIT
// Daisy Seed QSPI Programming (Erase/Write)
// Requires memory-mapped mode to be disabled during programming
#pragma once

#include <cstdint>
#include <mcu/qspi.hh>
#include <transport/direct.hh>

#include "qspi.hh"

namespace umi::daisy {

// Additional IS25LP064A constants for programming (extends qspi.hh)
namespace is25lp {
constexpr std::uint8_t BLOCK_ERASE = 0xD8; // 64KB Block Erase

constexpr std::uint32_t PAGE_SIZE = 256;    // 256 bytes per page
constexpr std::uint32_t SECTOR_SIZE = 4096; // 4KB sector
constexpr std::uint32_t BLOCK_SIZE = 65536; // 64KB block
} // namespace is25lp

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
        std::uint8_t status = qspi_read_byte(is25lp::READ_STATUS);
        if ((status & is25lp::SR_WIP) == 0) {
            break;
        }
    }

    // Wait for QUADSPI controller to be not busy
    qspi_wait_busy();

    // Set Alternate Bytes value (0xA0 for continuous read mode)
    t.write(QUADSPI::ABR::value(0x000000A0));

    // Configure CCR for memory-mapped quad I/O read
    t.write(QUADSPI::CCR::value(
        static_cast<std::uint32_t>(is25lp::QUAD_READ) | (qspi_mode::SINGLE << 8) | // IMODE: single line for instruction
        (qspi_mode::QUAD << 10) |                                                  // ADMODE: quad for address
        (qspi_adsize::ADDR_24BIT << 12) | (qspi_mode::QUAD << 14) |                // ABMODE: quad for alternate bytes
        (0U << 16) |                                                               // ABSIZE: 8-bit
        (6U << 18) |                                                               // DCYC: 6 dummy cycles
        (qspi_mode::QUAD << 24) |                                                  // DMODE: quad for data
        (qspi_fmode::MEMORY_MAPPED << 26) | (1U << 28)                             // SIOO
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
    qspi_write_enable();

    qspi_wait_busy();

    // Send sector erase command with address
    // IMPORTANT: Register write order must be DLR → CCR → AR (per STM32 HAL)
    t.write(QUADSPI::DLR::value(0)); // No data
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(is25lp::SECTOR_ERASE) | (qspi_mode::SINGLE << 8) | // IMODE
                                (qspi_mode::SINGLE << 10) |                                                   // ADMODE
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
    std::uint32_t sector_start = start & ~(is25lp::SECTOR_SIZE - 1);
    std::uint32_t end = start + size;

    while (sector_start < end) {
        if (!qspi_erase_sector(sector_start)) {
            return false;
        }
        sector_start += is25lp::SECTOR_SIZE;
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
    qspi_write_enable();

    qspi_wait_busy();

    // Send sector erase command with address
    // IMPORTANT: Register write order must be DLR → CCR → AR (per STM32 HAL)
    t.write(QUADSPI::DLR::value(0)); // No data
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(is25lp::SECTOR_ERASE) | (qspi_mode::SINGLE << 8) | // IMODE
                                (qspi_mode::SINGLE << 10) |                                                   // ADMODE
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
    std::uint32_t sector_start = (start & 0x00FFFFFF) & ~(is25lp::SECTOR_SIZE - 1);
    std::uint32_t end = (start & 0x00FFFFFF) + size;

    while (sector_start < end) {
        if (!qspi_erase_sector_no_remap(sector_start)) {
            qspi_enter_memory_mapped();
            return false;
        }

        // Call poll function between sector erases
        poll_fn();

        sector_start += is25lp::SECTOR_SIZE;
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

    if (len == 0 || len > is25lp::PAGE_SIZE) {
        return false;
    }

    // Convert to flash-relative address
    address &= 0x00FFFFFF;

    if (reset_mode) {
        qspi_exit_memory_mapped();
    }

    // Enable write
    qspi_write_enable();

    qspi_wait_busy();

    // IMPORTANT: Register write order must be DLR → CCR → AR (per STM32 HAL)
    // Set data length
    t.write(QUADSPI::DLR::value(len - 1));

    // Configure for page program (single line) - CCR must be BEFORE AR!
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(is25lp::PAGE_PROGRAM) | (qspi_mode::SINGLE << 8) | // IMODE
                                (qspi_mode::SINGLE << 10) |                                                   // ADMODE
                                (qspi_adsize::ADDR_24BIT << 12) |
                                (qspi_mode::SINGLE << 24) | // DMODE: single line for data
                                (qspi_fmode::INDIRECT_WRITE << 26)));

    // Set address - AR must be AFTER CCR!
    t.write(QUADSPI::AR::value(address));

    // Write data to FIFO (libDaisy HAL compatible)
    // DR register must be accessed as byte for proper FIFO operation
    auto* const dr_byte = reinterpret_cast<volatile std::uint8_t*>(0x52005020);
    
    for (std::uint32_t i = 0; i < len; ++i) {
        // Wait for FIFO Threshold Flag (FTF) - indicates FIFO has room
        // FTF is set when FIFO level <= FTHRES
        while (!(t.read(QUADSPI::SR{}) & (1U << 2))) {
            // Timeout check (simple counter)
        }
        // Write byte directly to DR (like HAL does)
        *dr_byte = data[i];
    }

    // Wait for transfer complete (TCF)
    while (!(t.read(QUADSPI::SR{}) & (1U << 1))) {
    }
    t.write(QUADSPI::FCR::value(1U << 1)); // Clear TCF

    // Wait for programming to complete
    qspi_wait_ready();

    if (reset_mode) {
        qspi_enter_memory_mapped();
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

    // Exit memory-mapped mode once at the start (like libDaisy)
    qspi_exit_memory_mapped();

    while (len > 0) {
        // Calculate bytes to write in this page
        std::uint32_t page_offset = flash_addr % is25lp::PAGE_SIZE;
        std::uint32_t page_remain = is25lp::PAGE_SIZE - page_offset;
        std::uint32_t chunk = (len < page_remain) ? len : page_remain;

        // reset_mode=false: don't exit/enter memory-mapped mode per page
        if (!qspi_program_page(flash_addr | 0x90000000, data, chunk, false)) {
            qspi_enter_memory_mapped();
            return false;
        }

        flash_addr += chunk;
        data += chunk;
        len -= chunk;
    }

    // Re-enter memory-mapped mode at the end (like libDaisy)
    qspi_enter_memory_mapped();
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
