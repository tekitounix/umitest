// SPDX-License-Identifier: MIT
// Daisy Seed microSD card driver (SDMMC1, 4-bit bus)
// PC8=D0, PC9=D1, PC10=D2, PC11=D3, PC12=CLK, PD2=CMD (all AF12)
#pragma once

#include <cstdint>
#include <mcu/rcc.hh>
#include <mcu/gpio.hh>
#include <mcu/sdmmc.hh>
#include <transport/direct.hh>

namespace umi::daisy {

/// Initialize SDMMC1 GPIO pins (all AF12)
inline void init_sdcard_gpio() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // GPIO clocks (C, D) — likely already enabled
    t.modify(RCC::AHB4ENR::GPIOCEN::Set{});
    t.modify(RCC::AHB4ENR::GPIODEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB4ENR{});

    constexpr std::uint8_t AF12 = 12;

    auto cfg = [&](auto gpio, std::uint8_t pin) {
        gpio_configure_pin<decltype(gpio)>(t, pin, gpio_mode::ALTERNATE,
            gpio_otype::PUSH_PULL, gpio_speed::VERY_HIGH, gpio_pupd::PULL_UP);
        gpio_set_af<decltype(gpio)>(t, pin, AF12);
    };

    // PC8=D0, PC9=D1, PC10=D2, PC11=D3, PC12=CLK
    for (auto pin : {8, 9, 10, 11, 12}) {
        cfg(GPIOC{}, static_cast<std::uint8_t>(pin));
    }

    // PD2=CMD
    cfg(GPIOD{}, 2);
}

/// Send SDMMC1 command and wait for response
inline std::uint32_t sdmmc_send_cmd(std::uint32_t cmd_idx, std::uint32_t arg,
                                     std::uint32_t resp_type, bool data_cmd = false) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Clear all flags
    t.write(SDMMC1::ICR::value(0xFFFFFFFF));

    // Set argument
    t.write(SDMMC1::ARG::value(arg));

    // Build command register value
    std::uint32_t cmd_val = cmd_idx | (resp_type << 8) | (1U << 12);  // CPSMEN
    if (data_cmd) {
        cmd_val |= (1U << 6);  // CMDTRANS
    }
    t.write(SDMMC1::CMD::value(cmd_val));

    // Wait for command completion (with timeout)
    constexpr int max_wait = 500000;
    if (resp_type == sdmmc_resp::NONE) {
        for (int i = 0; i < max_wait; ++i) {
            if (t.read(SDMMC1::STA{}) & (1U << 7)) break;
        }
    } else {
        std::uint32_t sta = 0;
        for (int i = 0; i < max_wait; ++i) {
            sta = t.read(SDMMC1::STA{});
            if (sta & ((1U << 6) | (1U << 2) | (1U << 0))) break;
        }
        if (sta & (1U << 2)) return 0xFFFFFFFF;  // Timeout
    }

    t.write(SDMMC1::ICR::value(0xFFFFFFFF));
    return t.read(SDMMC1::RESP1{});
}

/// Initialize SDMMC1 for SD card (4-bit, slow clock for init)
/// Returns true if card detected and initialized
inline bool init_sdcard() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Enable SDMMC1 clock
    t.modify(RCC::AHB3ENR::SDMMC1EN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB3ENR{});

    init_sdcard_gpio();

    // Power off → on sequence
    t.write(SDMMC1::POWER::value(0));
    for (int i = 0; i < 10000; ++i) { asm volatile("" ::: "memory"); }

    // Set slow clock for initialization (~400 kHz)
    // SDMMC1 clock = 240 MHz (HCLK), divider = 250 → ~960 kHz (close enough)
    t.write(SDMMC1::CLKCR::value(250));

    // Power on
    t.write(SDMMC1::POWER::value(0x03));
    for (int i = 0; i < 100000; ++i) { asm volatile("" ::: "memory"); }

    // CMD0: GO_IDLE_STATE (no response)
    sdmmc_send_cmd(sd_cmd::GO_IDLE_STATE, 0, sdmmc_resp::NONE);

    // CMD8: SEND_IF_COND (voltage check, 0x1AA pattern)
    auto r = sdmmc_send_cmd(sd_cmd::SEND_IF_COND, 0x1AA, sdmmc_resp::SHORT);
    if (r == 0xFFFFFFFF) return false;

    // ACMD41 loop: SD_SEND_OP_COND (HCS=1 for SDHC)
    for (int retry = 0; retry < 1000; ++retry) {
        sdmmc_send_cmd(sd_cmd::APP_CMD, 0, sdmmc_resp::SHORT);
        r = sdmmc_send_cmd(sd_cmd::SD_SEND_OP_COND, 0x40FF8000, sdmmc_resp::SHORT);
        if (r & (1U << 31)) break;  // Card ready (busy bit cleared)
        for (int i = 0; i < 10000; ++i) { asm volatile("" ::: "memory"); }
    }

    if (!(r & (1U << 31))) return false;  // Card not ready

    bool is_sdhc = (r & (1U << 30)) != 0;

    // CMD2: ALL_SEND_CID (get card identification)
    sdmmc_send_cmd(2, 0, sdmmc_resp::LONG);

    // CMD3: SEND_RELATIVE_ADDR (get RCA)
    auto rca_resp = sdmmc_send_cmd(3, 0, sdmmc_resp::SHORT);
    std::uint32_t rca = rca_resp & 0xFFFF0000;

    // CMD7: SELECT_CARD (transfer state)
    sdmmc_send_cmd(7, rca, sdmmc_resp::SHORT);

    // Switch to 4-bit bus: ACMD6
    sdmmc_send_cmd(sd_cmd::APP_CMD, rca, sdmmc_resp::SHORT);
    sdmmc_send_cmd(6, 0x02, sdmmc_resp::SHORT);  // 4-bit bus width

    // Speed up clock: divider = 4 → ~60 MHz (HS mode)
    t.write(SDMMC1::CLKCR::value(4 | (0b01 << 14)));  // 4-bit bus

    // Set block length to 512
    if (!is_sdhc) {
        sdmmc_send_cmd(sd_cmd::SET_BLOCKLEN, 512, sdmmc_resp::SHORT);
    }

    (void)is_sdhc;
    return true;
}

/// Read a single 512-byte block from SD card (blocking, FIFO polling)
/// @param block_addr Block address (sector number for SDHC, byte address for SD)
/// @param buf Output buffer (must be 512 bytes)
/// @return true on success
inline bool sdcard_read_block(std::uint32_t block_addr, std::uint8_t* buf) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Setup data transfer
    t.write(SDMMC1::DTIMER::value(0xFFFFFFFF));
    t.write(SDMMC1::DLEN::value(512));
    t.write(SDMMC1::DCTRL::value(
        (1U << 0) |   // DTEN
        (1U << 1) |   // DTDIR = from card
        (9U << 4)     // DBLOCKSIZE = 512 bytes (2^9)
    ));

    // CMD17: READ_SINGLE_BLOCK
    sdmmc_send_cmd(sd_cmd::READ_SINGLE, block_addr, sdmmc_resp::SHORT, true);

    // Read data via FIFO polling
    std::uint32_t* dst = reinterpret_cast<std::uint32_t*>(buf);
    std::uint32_t count = 0;

    for (int wait = 0; count < 128 && wait < 1000000; ++wait) {
        auto sta = t.read(SDMMC1::STA{});
        if (sta & ((1U << 1) | (1U << 3) | (1U << 5))) return false;  // Error
        if (sta & (1U << 15)) {  // RXFIFONE
            dst[count++] = t.read(SDMMC1::FIFO{});
        }
        if (sta & (1U << 8)) break;  // DATAEND
    }

    // Read remaining FIFO data
    while ((t.read(SDMMC1::STA{}) & (1U << 15)) && count < 128) {
        dst[count++] = t.read(SDMMC1::FIFO{});
    }

    t.write(SDMMC1::ICR::value(0xFFFFFFFF));
    return count == 128;
}

} // namespace umi::daisy
