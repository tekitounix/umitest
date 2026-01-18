// SPDX-License-Identifier: MIT
// STM32F4 USB OTG FS Device Driver
#pragma once

#include <cstdint>
#include <cstring>

namespace umi::stm32 {

/// USB OTG FS Core Registers
struct USB_OTG {
    static constexpr uint32_t BASE = 0x50000000;

    // Core global registers
    static constexpr uint32_t GOTGCTL = 0x000;
    static constexpr uint32_t GOTGINT = 0x004;
    static constexpr uint32_t GAHBCFG = 0x008;
    static constexpr uint32_t GUSBCFG = 0x00C;
    static constexpr uint32_t GRSTCTL = 0x010;
    static constexpr uint32_t GINTSTS = 0x014;
    static constexpr uint32_t GINTMSK = 0x018;
    static constexpr uint32_t GRXSTSR = 0x01C;
    static constexpr uint32_t GRXSTSP = 0x020;
    static constexpr uint32_t GRXFSIZ = 0x024;
    static constexpr uint32_t DIEPTXF0 = 0x028;
    static constexpr uint32_t GCCFG = 0x038;

    // Power and clock gating control register (offset 0xE00)
    static constexpr uint32_t PCGCCTL = 0xE00;
    static constexpr uint32_t PCGCCTL_STOPCLK = 1U << 0;
    static constexpr uint32_t PCGCCTL_GATECLK = 1U << 1;

    // Device mode registers (offset 0x800)
    static constexpr uint32_t DCFG = 0x800;
    static constexpr uint32_t DCTL = 0x804;
    static constexpr uint32_t DSTS = 0x808;
    static constexpr uint32_t DIEPMSK = 0x810;
    static constexpr uint32_t DOEPMSK = 0x814;
    static constexpr uint32_t DAINT = 0x818;
    static constexpr uint32_t DAINTMSK = 0x81C;

    // IN endpoint registers (0x900 + ep*0x20)
    static constexpr uint32_t DIEPCTL(uint8_t ep) { return 0x900 + ep * 0x20; }
    static constexpr uint32_t DIEPINT(uint8_t ep) { return 0x908 + ep * 0x20; }
    static constexpr uint32_t DIEPTSIZ(uint8_t ep) { return 0x910 + ep * 0x20; }
    static constexpr uint32_t DTXFSTS(uint8_t ep) { return 0x918 + ep * 0x20; }

    // OUT endpoint registers (0xB00 + ep*0x20)
    static constexpr uint32_t DOEPCTL(uint8_t ep) { return 0xB00 + ep * 0x20; }
    static constexpr uint32_t DOEPINT(uint8_t ep) { return 0xB08 + ep * 0x20; }
    static constexpr uint32_t DOEPTSIZ(uint8_t ep) { return 0xB10 + ep * 0x20; }

    // TX FIFO registers
    static constexpr uint32_t DIEPTXF(uint8_t ep) { return 0x104 + (ep - 1) * 4; }

    // FIFO addresses
    static constexpr uint32_t FIFO(uint8_t ep) { return 0x1000 + ep * 0x1000; }

    // GAHBCFG bits
    static constexpr uint32_t GAHBCFG_GINTMSK = 1U << 0;
    static constexpr uint32_t GAHBCFG_TXFELVL = 1U << 7;

    // GUSBCFG bits
    static constexpr uint32_t GUSBCFG_FDMOD = 1U << 30;  // Force device mode
    static constexpr uint32_t GUSBCFG_PHYSEL = 1U << 6;  // Full-speed PHY
    static constexpr uint32_t GUSBCFG_TRDT_MASK = 0xF << 10;
    static constexpr uint32_t GUSBCFG_TRDT(uint32_t v) { return (v & 0xF) << 10; }

    // GRSTCTL bits
    static constexpr uint32_t GRSTCTL_CSRST = 1U << 0;
    static constexpr uint32_t GRSTCTL_RXFFLSH = 1U << 4;
    static constexpr uint32_t GRSTCTL_TXFFLSH = 1U << 5;
    static constexpr uint32_t GRSTCTL_TXFNUM(uint8_t n) { return static_cast<uint32_t>(n) << 6; }
    static constexpr uint32_t GRSTCTL_AHBIDL = 1U << 31;

    // GINTSTS/GINTMSK bits
    static constexpr uint32_t GINTSTS_MMIS = 1U << 1;
    static constexpr uint32_t GINTSTS_OTGINT = 1U << 2;
    static constexpr uint32_t GINTSTS_SOF = 1U << 3;
    static constexpr uint32_t GINTSTS_RXFLVL = 1U << 4;
    static constexpr uint32_t GINTSTS_USBSUSP = 1U << 11;
    static constexpr uint32_t GINTSTS_USBRST = 1U << 12;
    static constexpr uint32_t GINTSTS_ENUMDNE = 1U << 13;
    static constexpr uint32_t GINTSTS_IEPINT = 1U << 18;
    static constexpr uint32_t GINTSTS_OEPINT = 1U << 19;
    static constexpr uint32_t GINTSTS_SRQINT = 1U << 30;
    static constexpr uint32_t GINTSTS_WKUPINT = 1U << 31;

    // GCCFG bits
    static constexpr uint32_t GCCFG_PWRDWN = 1U << 16;
    static constexpr uint32_t GCCFG_VBUSASEN = 1U << 18;   // VBUS sensing on PA9
    static constexpr uint32_t GCCFG_VBUSBSEN = 1U << 19;   // VBUS sensing on PB13
    static constexpr uint32_t GCCFG_SOFOUTEN = 1U << 20;   // SOF output enable
    static constexpr uint32_t GCCFG_NOVBUSSENS = 1U << 21; // Disable VBUS sensing

    // DCFG bits
    static constexpr uint32_t DCFG_DSPD_FS = 3U << 0;  // Full speed
    static constexpr uint32_t DCFG_DAD(uint8_t addr) { return static_cast<uint32_t>(addr) << 4; }

    // DCTL bits
    static constexpr uint32_t DCTL_RWUSIG = 1U << 0;  // Remote wakeup signaling
    static constexpr uint32_t DCTL_SDIS = 1U << 1;    // Soft disconnect
    static constexpr uint32_t DCTL_GINSTS = 1U << 2;  // Global IN NAK status (RO)
    static constexpr uint32_t DCTL_GONSTS = 1U << 3;  // Global OUT NAK status (RO)
    static constexpr uint32_t DCTL_SGINAK = 1U << 7;  // Set global IN NAK
    static constexpr uint32_t DCTL_CGINAK = 1U << 8;  // Clear global IN NAK
    static constexpr uint32_t DCTL_SGONAK = 1U << 9;  // Set global OUT NAK
    static constexpr uint32_t DCTL_CGONAK = 1U << 10; // Clear global OUT NAK

    // DIEPCTL/DOEPCTL bits
    static constexpr uint32_t DEPCTL_MPSIZ(uint32_t sz) { return sz & 0x7FF; }
    static constexpr uint32_t DEPCTL_USBAEP = 1U << 15;
    static constexpr uint32_t DEPCTL_NAKSTS = 1U << 17;
    static constexpr uint32_t DEPCTL_EPTYP(uint32_t t) { return (t & 3) << 18; }
    static constexpr uint32_t DEPCTL_STALL = 1U << 21;
    static constexpr uint32_t DEPCTL_TXFNUM(uint8_t n) { return static_cast<uint32_t>(n) << 22; }
    static constexpr uint32_t DEPCTL_CNAK = 1U << 26;
    static constexpr uint32_t DEPCTL_SNAK = 1U << 27;
    static constexpr uint32_t DEPCTL_SD0PID = 1U << 28;
    static constexpr uint32_t DEPCTL_EPENA = 1U << 31;
    static constexpr uint32_t DEPCTL_EPDIS = 1U << 30;

    // Endpoint types
    static constexpr uint32_t EPTYP_CONTROL = 0;
    static constexpr uint32_t EPTYP_ISOCHRONOUS = 1;
    static constexpr uint32_t EPTYP_BULK = 2;
    static constexpr uint32_t EPTYP_INTERRUPT = 3;

    // DIEPINT/DOEPINT bits
    static constexpr uint32_t DEPINT_XFRC = 1U << 0;   // Transfer complete
    static constexpr uint32_t DEPINT_EPDISD = 1U << 1;
    static constexpr uint32_t DEPINT_STUP = 1U << 3;   // Setup done (OUT only)
    static constexpr uint32_t DEPINT_OTEPDIS = 1U << 4;
    static constexpr uint32_t DEPINT_STSPHSRX = 1U << 5; // Status phase received (OUT)
    static constexpr uint32_t DEPINT_TXFE = 1U << 7;   // TX FIFO empty (IN only)
    static constexpr uint32_t DEPINT_NAK = 1U << 13;   // NAK interrupt

    // DOEPMSK bits (HAL values)
    static constexpr uint32_t DOEPMSK_XFRCM = 1U << 0;
    static constexpr uint32_t DOEPMSK_EPDM = 1U << 1;
    static constexpr uint32_t DOEPMSK_STUPM = 1U << 3;
    static constexpr uint32_t DOEPMSK_OTEPSPRM = 1U << 5;
    static constexpr uint32_t DOEPMSK_NAKM = 1U << 13;

    // DIEPMSK bits (HAL values)
    static constexpr uint32_t DIEPMSK_XFRCM = 1U << 0;
    static constexpr uint32_t DIEPMSK_EPDM = 1U << 1;
    static constexpr uint32_t DIEPMSK_TOM = 1U << 3;

    // DIEPEMPMSK register (TX FIFO empty interrupt mask)
    static constexpr uint32_t DIEPEMPMSK = 0x834;

    // GRXSTSP packet status
    static constexpr uint32_t PKTSTS_OUT_NAK = 1;
    static constexpr uint32_t PKTSTS_OUT_DATA = 2;
    static constexpr uint32_t PKTSTS_OUT_COMPLETE = 3;
    static constexpr uint32_t PKTSTS_SETUP_COMPLETE = 4;
    static constexpr uint32_t PKTSTS_SETUP_DATA = 6;

    static volatile uint32_t& reg(uint32_t offset) {
        return *reinterpret_cast<volatile uint32_t*>(BASE + offset);
    }

    static volatile uint32_t& fifo(uint8_t ep) {
        return *reinterpret_cast<volatile uint32_t*>(BASE + FIFO(ep));
    }
};

/// USB Device State
struct USBDevice {
    static constexpr uint8_t MAX_ENDPOINTS = 4;
    static constexpr uint8_t MAX_PACKET_SIZE = 64;

    uint8_t address = 0;
    uint8_t pending_address = 0;  // Address to set after IN transfer completes
    bool configured = false;

    // Debug counters
    uint32_t dbg_usbrst_count = 0;
    uint32_t dbg_enumdne_count = 0;
    uint32_t dbg_rxflvl_count = 0;
    uint32_t dbg_last_pktsts = 0;
    uint32_t dbg_last_rxsts = 0;
    uint32_t dbg_setup_data_count = 0;
    uint32_t dbg_out_data_count = 0;
    uint8_t dbg_first_setup[8] = {0};  // First SETUP packet received
    uint32_t dbg_iepint_count = 0;
    uint32_t dbg_xfrc_count = 0;
    uint32_t dbg_ep_write_count = 0;
    uint32_t dbg_oepint_count = 0;
    uint32_t dbg_stup_int_count = 0;
    uint32_t dbg_pending_addr_set = 0;  // Value of pending_address when XFRC fires
    uint32_t dbg_dcfg_before_set = 0;   // DCFG value before setting address
    uint32_t dbg_dcfg_after_set = 0;    // DCFG value after setting address
    uint32_t dbg_dcfg_written = 0;      // Value we tried to write to DCFG
    // Log first 16 RXSTS values to see what packets are received
    uint32_t dbg_rxsts_log[16] = {0};
    uint32_t dbg_rxsts_log_idx = 0;
    // Debug: record GINTSTS at IRQ entry
    uint32_t dbg_irq_count = 0;
    uint32_t dbg_last_gintsts = 0;
    uint32_t dbg_last_gintmsk = 0;
    uint32_t dbg_last_gints = 0;  // masked result

    // EP0 setup buffer
    alignas(4) uint8_t setup_data[8];
    alignas(4) uint8_t ep0_buf[MAX_PACKET_SIZE];
    // EP1-3 RX buffers for bulk/interrupt endpoints
    alignas(4) uint8_t ep_rx_buf[MAX_ENDPOINTS][MAX_PACKET_SIZE];

    // Callbacks
    void (*on_setup)(uint8_t* data) = nullptr;
    void (*on_rx)(uint8_t ep, uint8_t* data, uint16_t len) = nullptr;

    void init() {
        using R = USB_OTG;

        // Step 1: Select FS Embedded PHY (before reset, per HAL)
        R::reg(R::GUSBCFG) |= R::GUSBCFG_PHYSEL;

        // Step 2: Core soft reset (after PHY select)
        // Wait for AHB idle
        while (!(R::reg(R::GRSTCTL) & R::GRSTCTL_AHBIDL)) {}

        // Small delay before reset
        for (int i = 0; i < 10; ++i) { asm volatile(""); }

        // Core soft reset
        R::reg(R::GRSTCTL) |= R::GRSTCTL_CSRST;
        while (R::reg(R::GRSTCTL) & R::GRSTCTL_CSRST) {}

        // Wait for AHB idle again
        while (!(R::reg(R::GRSTCTL) & R::GRSTCTL_AHBIDL)) {}

        // Step 3: Activate USB Transceiver
        R::reg(R::GCCFG) |= R::GCCFG_PWRDWN;

        // Step 4: Force device mode and configure turnaround time
        R::reg(R::GUSBCFG) |= R::GUSBCFG_FDMOD | R::GUSBCFG_TRDT(6);

        // Wait for device mode (GINTSTS.CMOD = 0)
        for (int i = 0; i < 50000; ++i) { asm volatile(""); }

        // Step 5: Device init (per HAL USB_DevInit)
        // HAL: Clear all DIEPTXF registers first
        for (uint8_t i = 0; i < 15; ++i) {
            R::reg(R::DIEPTXF(i)) = 0;
        }

        // Soft disconnect during configuration
        R::reg(R::DCTL) |= R::DCTL_SDIS;

        // Disable VBUS sensing (HAL-style: clear VBUSASEN, VBUSBSEN, set NOVBUSSENS)
        R::reg(R::GCCFG) |= R::GCCFG_NOVBUSSENS;
        R::reg(R::GCCFG) &= ~R::GCCFG_VBUSBSEN;
        R::reg(R::GCCFG) &= ~R::GCCFG_VBUSASEN;

        // Restart the PHY clock
        R::reg(R::PCGCCTL) = 0;

        // Device configuration: full speed
        R::reg(R::DCFG) = R::DCFG_DSPD_FS;

        // Flush all FIFOs
        R::reg(R::GRSTCTL) = R::GRSTCTL_TXFFLSH | R::GRSTCTL_TXFNUM(0x10);
        while (R::reg(R::GRSTCTL) & R::GRSTCTL_TXFFLSH) {}
        R::reg(R::GRSTCTL) = R::GRSTCTL_RXFFLSH;
        while (R::reg(R::GRSTCTL) & R::GRSTCTL_RXFFLSH) {}

        // HAL: Clear all endpoint masks first
        R::reg(R::DIEPMSK) = 0;
        R::reg(R::DOEPMSK) = 0;
        R::reg(R::DAINTMSK) = 0;

        // HAL: Clear/disable all endpoints
        for (uint8_t i = 0; i < MAX_ENDPOINTS; ++i) {
            // IN endpoints
            if (R::reg(R::DIEPCTL(i)) & R::DEPCTL_EPENA) {
                if (i == 0) {
                    R::reg(R::DIEPCTL(i)) = R::DEPCTL_SNAK;
                } else {
                    R::reg(R::DIEPCTL(i)) = R::DEPCTL_EPDIS | R::DEPCTL_SNAK;
                }
            } else {
                R::reg(R::DIEPCTL(i)) = 0;
            }
            R::reg(R::DIEPTSIZ(i)) = 0;
            R::reg(R::DIEPINT(i)) = 0xFB7FU;

            // OUT endpoints
            if (R::reg(R::DOEPCTL(i)) & R::DEPCTL_EPENA) {
                if (i == 0) {
                    R::reg(R::DOEPCTL(i)) = R::DEPCTL_SNAK;
                } else {
                    R::reg(R::DOEPCTL(i)) = R::DEPCTL_EPDIS | R::DEPCTL_SNAK;
                }
            } else {
                R::reg(R::DOEPCTL(i)) = 0;
            }
            R::reg(R::DOEPTSIZ(i)) = 0;
            R::reg(R::DOEPINT(i)) = 0xFB7FU;
        }

        // HAL: Disable TXFIFO underrun interrupt mask
        R::reg(R::DIEPMSK) &= ~(1U << 8);  // TXFURM

        // Disable all interrupts first
        R::reg(R::GINTMSK) = 0;

        // Clear all pending interrupts
        R::reg(R::GINTSTS) = 0xBFFFFFFFU;

        // Configure FIFOs (total 320 words = 1280 bytes)
        // RX FIFO: 128 words
        R::reg(R::GRXFSIZ) = 128;

        // TX FIFO 0 (EP0): 64 words at offset 128
        R::reg(R::DIEPTXF0) = (64U << 16) | 128;

        // TX FIFO 1 (EP1): 64 words at offset 192
        R::reg(R::DIEPTXF(1)) = (64U << 16) | 192;

        // TX FIFO 2 (EP2): 64 words at offset 256
        R::reg(R::DIEPTXF(2)) = (64U << 16) | 256;

        // Enable interrupts (HAL order: RXFLVL first for non-DMA, then others)
        R::reg(R::GINTMSK) |= R::GINTSTS_RXFLVL;  // Non-DMA mode
        R::reg(R::GINTMSK) |= R::GINTSTS_USBSUSP | R::GINTSTS_USBRST |
                             R::GINTSTS_ENUMDNE | R::GINTSTS_IEPINT |
                             R::GINTSTS_OEPINT;

        // Enable global interrupt
        R::reg(R::GAHBCFG) = R::GAHBCFG_GINTMSK;

        // Setup EP0 IN endpoint (DIEPCTL0)
        // EP0 MPSIZ encoding for OTG FS: 0 = 64 bytes
        static constexpr uint32_t EP0_MPSIZ_64 = 0;
        R::reg(R::DIEPCTL(0)) = EP0_MPSIZ_64 |
                                R::DEPCTL_USBAEP |
                                R::DEPCTL_TXFNUM(0);

        // Enable EP0 interrupts (HAL: DAINTMSK |= 0x10001)
        R::reg(R::DAINTMSK) = (1U << 0) | (1U << 16);  // EP0 IN and OUT

        // Configure endpoint masks (HAL values)
        R::reg(R::DOEPMSK) = R::DOEPMSK_STUPM | R::DOEPMSK_XFRCM |
                             R::DOEPMSK_EPDM | R::DOEPMSK_OTEPSPRM | R::DOEPMSK_NAKM;
        R::reg(R::DIEPMSK) = R::DIEPMSK_TOM | R::DIEPMSK_XFRCM | R::DIEPMSK_EPDM;

        // Pre-configure EP0 OUT (DOEPTSIZ only, no DOEPCTL for non-DMA)
        configure_ep0();

        // Connect (USB_DevConnect in HAL)
        // In case phy is stopped, ensure to ungate and restore the phy CLK
        R::reg(R::PCGCCTL) &= ~(R::PCGCCTL_STOPCLK | R::PCGCCTL_GATECLK);
        R::reg(R::DCTL) &= ~R::DCTL_SDIS;
    }

    void configure_ep0() {
        using R = USB_OTG;

        // HAL USB_EP0_OutStart for non-DMA mode
        // IMPORTANT: For non-DMA mode, HAL does NOT set DOEPCTL (no EPENA/CNAK)!
        // It only sets DOEPTSIZ.

        // Check if EP0 is already enabled (like HAL does for core > 300A)
        // Skip if already enabled
        if ((R::reg(R::DOEPCTL(0)) & R::DEPCTL_EPENA) != 0) {
            return;
        }

        // DOEPTSIZ: HAL sets it step by step
        R::reg(R::DOEPTSIZ(0)) = 0;  // Clear first
        R::reg(R::DOEPTSIZ(0)) |= (1U << 19);    // PKTCNT = 1
        R::reg(R::DOEPTSIZ(0)) |= (3U * 8U);     // XFRSIZ = 24 bytes (3 SETUP packets)
        R::reg(R::DOEPTSIZ(0)) |= (3U << 29);    // STUPCNT = 3

        // NON-DMA MODE: Do NOT set DOEPCTL here!
        // HAL's USB_EP0_OutStart only sets DOEPCTL for DMA mode (dma == 1)
        // The hardware will automatically handle SETUP packet reception
    }

    void configure_endpoint(uint8_t ep, uint8_t type, uint16_t max_pkt, bool in) {
        using R = USB_OTG;

        if (in) {
            R::reg(R::DIEPCTL(ep)) = R::DEPCTL_MPSIZ(max_pkt) |
                                    R::DEPCTL_USBAEP |
                                    R::DEPCTL_EPTYP(type) |
                                    R::DEPCTL_TXFNUM(ep) |
                                    R::DEPCTL_SD0PID;
            R::reg(R::DAINTMSK) |= (1U << ep);
        } else {
            R::reg(R::DOEPCTL(ep)) = R::DEPCTL_MPSIZ(max_pkt) |
                                    R::DEPCTL_USBAEP |
                                    R::DEPCTL_EPTYP(type) |
                                    R::DEPCTL_SD0PID |
                                    R::DEPCTL_EPENA |
                                    R::DEPCTL_CNAK;
            R::reg(R::DOEPTSIZ(ep)) = (1U << 19) | max_pkt;
            R::reg(R::DAINTMSK) |= (1U << (ep + 16));
        }
    }

    void ep_write(uint8_t ep, const uint8_t* data, uint16_t len) {
        using R = USB_OTG;
        dbg_ep_write_count++;

        // Max packet size (EP0 = 64, bulk endpoints = 64)
        constexpr uint16_t mps = 64;

        // HAL USB_EPStartXfer for IN endpoint (non-DMA mode)
        // Step 1: Set DIEPTSIZ - HAL clears fields then sets them
        if (len == 0) {
            // ZLP path (HAL: ep->xfer_len == 0)
            R::reg(R::DIEPTSIZ(ep)) &= ~(0x7FFFFU);      // Clear XFRSIZ
            R::reg(R::DIEPTSIZ(ep)) &= ~(0x3FFU << 19);  // Clear PKTCNT (10 bits for non-EP0)
            R::reg(R::DIEPTSIZ(ep)) |= (1U << 19);       // PKTCNT = 1
            // XFRSIZ stays 0 for ZLP
        } else {
            // Calculate number of packets: (len + mps - 1) / mps
            uint16_t pktcnt = (len + mps - 1) / mps;

            // Non-ZLP path
            R::reg(R::DIEPTSIZ(ep)) &= ~(0x7FFFFU);      // Clear XFRSIZ
            R::reg(R::DIEPTSIZ(ep)) &= ~(0x3FFU << 19);  // Clear PKTCNT (10 bits)
            R::reg(R::DIEPTSIZ(ep)) |= (static_cast<uint32_t>(pktcnt) << 19);  // PKTCNT
            R::reg(R::DIEPTSIZ(ep)) |= (len & 0x7FFFFU); // XFRSIZ = len
        }

        // Step 2: Enable endpoint (HAL does this BEFORE writing FIFO for non-ISOC)
        R::reg(R::DIEPCTL(ep)) |= R::DEPCTL_CNAK | R::DEPCTL_EPENA;

        // Step 3: Write data to FIFO (for non-ISOC, HAL uses TXFE interrupt,
        // but synchronous write should also work)
        if (len > 0 && data != nullptr) {
            // Wait for TX FIFO space (like HAL PCD_WriteEmptyTxFifo)
            uint32_t words_needed = (len + 3) / 4;
            while ((R::reg(R::DTXFSTS(ep)) & 0xFFFF) < words_needed) {}

            // Write to FIFO
            volatile uint32_t& fifo = R::fifo(ep);
            const uint32_t* src = reinterpret_cast<const uint32_t*>(data);
            for (uint32_t i = 0; i < words_needed; ++i) {
                fifo = src[i];
            }
        }
    }

    // HAL USB_ReadPacket - only reads from FIFO, does NOT touch DOEPCTL/DOEPTSIZ
    static void read_packet(uint8_t* data, uint16_t len) {
        using R = USB_OTG;

        volatile uint32_t& fifo_reg = R::fifo(0);  // All FIFOs read from address 0
        uint32_t words = len / 4;
        uint32_t* dst = reinterpret_cast<uint32_t*>(data);

        // Read full words
        for (uint32_t i = 0; i < words; ++i) {
            dst[i] = fifo_reg;
        }

        // Handle remaining bytes (HAL style)
        uint16_t remaining = len % 4;
        if (remaining != 0) {
            uint32_t temp = fifo_reg;
            uint8_t* dst_byte = data + (words * 4);
            for (uint16_t i = 0; i < remaining; ++i) {
                dst_byte[i] = static_cast<uint8_t>(temp >> (8 * i));
            }
        }
    }

    // Prepare EP0 OUT to receive data (like HAL_PCD_EP_Receive -> USB_EPStartXfer for EP0)
    void ep0_prepare_receive() {
        using R = USB_OTG;

        // EP0: xfer_size = maxpacket, pktcnt = 1
        R::reg(R::DOEPTSIZ(0)) &= ~(0x7FFFFU);  // Clear XFRSIZ
        R::reg(R::DOEPTSIZ(0)) &= ~(0x3U << 19); // Clear PKTCNT (EP0 is only 2 bits)
        R::reg(R::DOEPTSIZ(0)) |= MAX_PACKET_SIZE;  // XFRSIZ = 64
        R::reg(R::DOEPTSIZ(0)) |= (1U << 19);       // PKTCNT = 1

        // Enable EP
        R::reg(R::DOEPCTL(0)) |= R::DEPCTL_CNAK | R::DEPCTL_EPENA;
    }

    void set_address(uint8_t addr) {
        using R = USB_OTG;
        address = addr;
        // HAL: USB_SetDevAddress
        // Clear DAD field (bits 10:4), then set new address
        uint32_t dcfg = R::reg(R::DCFG);
        dcfg &= ~(0x7FU << 4);  // Clear DAD (must use U suffix!)
        dcfg |= (static_cast<uint32_t>(addr) << 4) & (0x7FU << 4);
        R::reg(R::DCFG) = dcfg;
    }

    void ep_stall(uint8_t ep, bool in) {
        using R = USB_OTG;
        if (in) {
            R::reg(R::DIEPCTL(ep)) |= R::DEPCTL_STALL;
        } else {
            R::reg(R::DOEPCTL(ep)) |= R::DEPCTL_STALL;
        }
    }

    void handle_irq() {
        using R = USB_OTG;

        dbg_irq_count++;
        dbg_last_gintsts = R::reg(R::GINTSTS);
        dbg_last_gintmsk = R::reg(R::GINTMSK);
        uint32_t gints = dbg_last_gintsts & dbg_last_gintmsk;
        dbg_last_gints = gints;

        // USB Reset (HAL-style handling)
        if ((gints & R::GINTSTS_USBRST) != 0) {
            dbg_usbrst_count++;
            address = 0;
            pending_address = 0;
            configured = false;

            // HAL: Clear remote wakeup signaling
            R::reg(R::DCTL) &= ~R::DCTL_RWUSIG;

            // HAL: Flush all TX FIFOs
            R::reg(R::GRSTCTL) = R::GRSTCTL_TXFFLSH | R::GRSTCTL_TXFNUM(0x10);
            while (R::reg(R::GRSTCTL) & R::GRSTCTL_TXFFLSH) {}

            // HAL: Clear endpoint interrupts and STALL for all endpoints
            for (uint8_t i = 0; i < MAX_ENDPOINTS; ++i) {
                R::reg(R::DIEPINT(i)) = 0xFB7FU;
                R::reg(R::DIEPCTL(i)) &= ~R::DEPCTL_STALL;
                R::reg(R::DOEPINT(i)) = 0xFB7FU;
                R::reg(R::DOEPCTL(i)) &= ~R::DEPCTL_STALL;
                R::reg(R::DOEPCTL(i)) |= R::DEPCTL_SNAK;
            }

            // HAL: Enable EP0 interrupts
            R::reg(R::DAINTMSK) |= 0x10001U;

            // HAL: Configure endpoint masks (full HAL values)
            R::reg(R::DOEPMSK) = R::DOEPMSK_STUPM | R::DOEPMSK_XFRCM |
                                 R::DOEPMSK_EPDM | R::DOEPMSK_OTEPSPRM | R::DOEPMSK_NAKM;
            R::reg(R::DIEPMSK) = R::DIEPMSK_TOM | R::DIEPMSK_XFRCM | R::DIEPMSK_EPDM;

            // HAL: Clear device address
            R::reg(R::DCFG) &= ~(0x7FU << 4);

            // HAL: Setup EP0 (USB_EP0_OutStart for non-DMA)
            configure_ep0();

            // Clear interrupt flag AFTER handling (HAL order)
            R::reg(R::GINTSTS) = R::GINTSTS_USBRST;
        }

        // Enumeration done (HAL order: process first, clear flag last)
        if ((gints & R::GINTSTS_ENUMDNE) != 0) {
            dbg_enumdne_count++;

            // HAL: USB_ActivateSetup - Clear IN EP0 MPSIZ
            R::reg(R::DIEPCTL(0)) &= ~0x7FFU;
            // HAL: USB_ActivateSetup - Clear global IN NAK
            R::reg(R::DCTL) |= R::DCTL_CGINAK;

            // HAL: USB_SetTurnaroundTime - for 168MHz HCLK, TRDT = 6
            R::reg(R::GUSBCFG) = (R::reg(R::GUSBCFG) & ~R::GUSBCFG_TRDT_MASK) |
                                  R::GUSBCFG_TRDT(6);

            // Clear interrupt flag AFTER handling (HAL order)
            R::reg(R::GINTSTS) = R::GINTSTS_ENUMDNE;
        }

        // USB Suspend - must clear flag to avoid infinite IRQ loop
        if ((gints & R::GINTSTS_USBSUSP) != 0) {
            R::reg(R::GINTSTS) = R::GINTSTS_USBSUSP;
            // Note: Suspend handling could be added here if needed
        }

        // RX FIFO not empty - HAL-style processing with mask/unmask
        // Note: Check raw GINTSTS, not masked gints, as RXFLVL is a status bit
        if ((dbg_last_gintsts & R::GINTSTS_RXFLVL) != 0) {
            // Mask RXFLVL interrupt while processing (HAL: USB_MASK_INTERRUPT)
            R::reg(R::GINTMSK) &= ~R::GINTSTS_RXFLVL;

            dbg_rxflvl_count++;
            uint32_t rxsts = R::reg(R::GRXSTSP);
            dbg_last_rxsts = rxsts;
            // Log RXSTS for debugging
            if (dbg_rxsts_log_idx < 16) {
                dbg_rxsts_log[dbg_rxsts_log_idx++] = rxsts;
            }
            uint8_t ep = rxsts & 0xF;
            uint16_t bcnt = (rxsts >> 4) & 0x7FF;
            uint8_t pktsts = (rxsts >> 17) & 0xF;
            dbg_last_pktsts = pktsts;

            // HAL-style RXFLVL handling (only reads data, no EP re-enable here)
            if (pktsts == R::PKTSTS_SETUP_DATA) {
                dbg_setup_data_count++;
                // Read SETUP data from FIFO (8 bytes) - HAL: USB_ReadPacket
                read_packet(setup_data, 8);
                // Save first SETUP packet for debugging
                if (dbg_setup_data_count == 1) {
                    for (int i = 0; i < 8; ++i) {
                        dbg_first_setup[i] = setup_data[i];
                    }
                }
                // NOTE: Do NOT call on_setup() here!
                // Setup handling is done in OEPINT when DOEPINT_STUP is set
            } else if (pktsts == R::PKTSTS_OUT_DATA) {
                dbg_out_data_count++;
                if (bcnt > 0) {
                    // HAL: USB_ReadPacket - only reads, no EP re-enable
                    // Use appropriate buffer for each endpoint
                    uint8_t* buf = (ep == 0) ? ep0_buf : ep_rx_buf[ep];
                    read_packet(buf, bcnt);
                    if (on_rx != nullptr) {
                        on_rx(ep, buf, bcnt);
                    }
                }
            }
            // HAL: SETUP_COMPLETE and OUT_NAK are handled with empty else block

            // Unmask RXFLVL interrupt after processing (HAL: USB_UNMASK_INTERRUPT)
            R::reg(R::GINTMSK) |= R::GINTSTS_RXFLVL;
        }

        // IN endpoint interrupts (HAL style)
        if ((gints & R::GINTSTS_IEPINT) != 0) {
            dbg_iepint_count++;
            uint32_t epint = R::reg(R::DAINT) & R::reg(R::DAINTMSK) & 0xFFFF;
            for (uint8_t ep = 0; ep < MAX_ENDPOINTS && epint != 0; ++ep) {
                if ((epint & (1U << ep)) != 0) {
                    uint32_t epints = R::reg(R::DIEPINT(ep));
                    if ((epints & R::DEPINT_XFRC) != 0) {
                        dbg_xfrc_count++;
                        // HAL: Clear DIEPEMPMSK for this EP (line 1218-1219)
                        uint32_t fifoemptymsk = 1U << ep;
                        R::reg(R::DIEPEMPMSK) &= ~fifoemptymsk;

                        R::reg(R::DIEPINT(ep)) = R::DEPINT_XFRC;

                        // EP0 IN complete - HAL: DataInStageCallback
                        if (ep == 0) {
                            // Apply pending address if any (SET_ADDRESS completion)
                            dbg_pending_addr_set = pending_address;
                            if (pending_address != 0) {
                                dbg_dcfg_before_set = R::reg(R::DCFG);
                                R::reg(R::DCFG) &= ~(0x7FU << 4);  // Clear DAD
                                uint32_t dad_val = (static_cast<uint32_t>(pending_address) << 4) & (0x7FU << 4);
                                dbg_dcfg_written = R::reg(R::DCFG) | dad_val;
                                R::reg(R::DCFG) |= dad_val;  // Set DAD
                                dbg_dcfg_after_set = R::reg(R::DCFG);
                                address = pending_address;
                                pending_address = 0;
                            }
                            // Prepare OUT EP0 for status stage (like HAL USBD_LL_PrepareReceive)
                            R::reg(R::DOEPTSIZ(0)) = (3U << 29) |  // STUPCNT
                                                     (1U << 19) |  // PKTCNT
                                                     MAX_PACKET_SIZE;
                            R::reg(R::DOEPCTL(0)) |= R::DEPCTL_EPENA | R::DEPCTL_CNAK;
                        }
                    }
                }
            }
        }

        // OUT endpoint interrupts (HAL style)
        if ((gints & R::GINTSTS_OEPINT) != 0) {
            dbg_oepint_count++;
            uint32_t epint = (R::reg(R::DAINT) & R::reg(R::DAINTMSK)) >> 16;
            for (uint8_t ep = 0; ep < MAX_ENDPOINTS && epint != 0; ++ep) {
                if ((epint & (1U << ep)) != 0) {
                    uint32_t epints = R::reg(R::DOEPINT(ep));

                    // XFRC: Transfer complete
                    if ((epints & R::DEPINT_XFRC) != 0) {
                        R::reg(R::DOEPINT(ep)) = R::DEPINT_XFRC;
                        // HAL: PCD_EP_OutXfrComplete_int - calls DataOutStageCallback
                        // Re-enable OUT endpoint for next transfer (non-EP0 only)
                        if (ep != 0) {
                            R::reg(R::DOEPTSIZ(ep)) = (1U << 19) | MAX_PACKET_SIZE;  // PKTCNT=1, XFRSIZ=64
                            R::reg(R::DOEPCTL(ep)) |= R::DEPCTL_EPENA | R::DEPCTL_CNAK;
                        }
                    }

                    // STUP: Setup phase done
                    if ((epints & R::DEPINT_STUP) != 0) {
                        dbg_stup_int_count++;
                        R::reg(R::DOEPINT(ep)) = R::DEPINT_STUP;
                        // HAL: PCD_EP_OutSetupPacket_int - just calls SetupStageCallback
                        // NO EP re-enable here for non-DMA mode!
                        if (ep == 0 && on_setup != nullptr) {
                            on_setup(setup_data);
                        }
                    }

                    // OTEPDIS: OUT Token Received When Endpoint Disabled
                    if ((epints & R::DEPINT_OTEPDIS) != 0) {
                        R::reg(R::DOEPINT(ep)) = R::DEPINT_OTEPDIS;
                    }

                    // OTEPSPR: Status Phase Received
                    if ((epints & R::DEPINT_STSPHSRX) != 0) {
                        R::reg(R::DOEPINT(ep)) = R::DEPINT_STSPHSRX;
                    }

                    // NAK interrupt
                    if ((epints & R::DEPINT_NAK) != 0) {
                        R::reg(R::DOEPINT(ep)) = R::DEPINT_NAK;
                    }
                }
            }
        }
    }
};

}  // namespace umi::stm32
