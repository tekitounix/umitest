// SPDX-License-Identifier: MIT
// UMI-USB: STM32 OTG FS/HS HAL Backend
#pragma once

#include <cstdint>
#include <cstring>
#include "../hal.hh"

namespace umiusb {

// ============================================================================
// STM32 OTG Register Definitions
// ============================================================================

/// STM32 USB OTG FS/HS Register Map
/// Template parameter allows different base addresses (FS=0x50000000, HS=0x40040000)
template<uint32_t BaseAddr>
struct OtgRegs {
    static constexpr uint32_t BASE = BaseAddr;

    // Core Global Registers
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

    // Power and Clock Gating
    static constexpr uint32_t PCGCCTL = 0xE00;

    // Device Mode Registers
    static constexpr uint32_t DCFG = 0x800;
    static constexpr uint32_t DCTL = 0x804;
    static constexpr uint32_t DSTS = 0x808;
    static constexpr uint32_t DIEPMSK = 0x810;
    static constexpr uint32_t DOEPMSK = 0x814;
    static constexpr uint32_t DAINT = 0x818;
    static constexpr uint32_t DAINTMSK = 0x81C;
    static constexpr uint32_t DIEPEMPMSK = 0x834;

    // Endpoint Registers
    static constexpr uint32_t DIEPCTL(uint8_t ep) { return 0x900 + ep * 0x20; }
    static constexpr uint32_t DIEPINT(uint8_t ep) { return 0x908 + ep * 0x20; }
    static constexpr uint32_t DIEPTSIZ(uint8_t ep) { return 0x910 + ep * 0x20; }
    static constexpr uint32_t DTXFSTS(uint8_t ep) { return 0x918 + ep * 0x20; }
    static constexpr uint32_t DOEPCTL(uint8_t ep) { return 0xB00 + ep * 0x20; }
    static constexpr uint32_t DOEPINT(uint8_t ep) { return 0xB08 + ep * 0x20; }
    static constexpr uint32_t DOEPTSIZ(uint8_t ep) { return 0xB10 + ep * 0x20; }

    // TX FIFO Configuration
    static constexpr uint32_t DIEPTXF(uint8_t ep) { return 0x104 + (ep - 1) * 4; }

    // FIFO Address
    static constexpr uint32_t FIFO(uint8_t ep) { return 0x1000 + ep * 0x1000; }

    // Register access
    static volatile uint32_t& reg(uint32_t offset) {
        return *reinterpret_cast<volatile uint32_t*>(BASE + offset);
    }

    static volatile uint32_t& fifo(uint8_t ep) {
        return *reinterpret_cast<volatile uint32_t*>(BASE + FIFO(ep));
    }
};

// Register bit definitions (shared between FS/HS)
namespace otg {

// GAHBCFG
inline constexpr uint32_t GAHBCFG_GINTMSK = 1U << 0;
inline constexpr uint32_t GAHBCFG_TXFELVL = 1U << 7;

// GUSBCFG
inline constexpr uint32_t GUSBCFG_FDMOD = 1U << 30;
inline constexpr uint32_t GUSBCFG_PHYSEL = 1U << 6;
inline constexpr uint32_t GUSBCFG_TRDT_MASK = 0xFU << 10;
inline constexpr uint32_t GUSBCFG_TRDT(uint32_t v) { return (v & 0xF) << 10; }

// GRSTCTL
inline constexpr uint32_t GRSTCTL_CSRST = 1U << 0;
inline constexpr uint32_t GRSTCTL_RXFFLSH = 1U << 4;
inline constexpr uint32_t GRSTCTL_TXFFLSH = 1U << 5;
inline constexpr uint32_t GRSTCTL_TXFNUM(uint8_t n) { return static_cast<uint32_t>(n) << 6; }
inline constexpr uint32_t GRSTCTL_AHBIDL = 1U << 31;

// GINTSTS/GINTMSK
inline constexpr uint32_t GINTSTS_MMIS = 1U << 1;
inline constexpr uint32_t GINTSTS_OTGINT = 1U << 2;
inline constexpr uint32_t GINTSTS_SOF = 1U << 3;
inline constexpr uint32_t GINTSTS_RXFLVL = 1U << 4;
inline constexpr uint32_t GINTSTS_USBSUSP = 1U << 11;
inline constexpr uint32_t GINTSTS_USBRST = 1U << 12;
inline constexpr uint32_t GINTSTS_ENUMDNE = 1U << 13;
inline constexpr uint32_t GINTSTS_IEPINT = 1U << 18;
inline constexpr uint32_t GINTSTS_OEPINT = 1U << 19;

// GCCFG
inline constexpr uint32_t GCCFG_PWRDWN = 1U << 16;
inline constexpr uint32_t GCCFG_VBUSASEN = 1U << 18;
inline constexpr uint32_t GCCFG_VBUSBSEN = 1U << 19;
inline constexpr uint32_t GCCFG_NOVBUSSENS = 1U << 21;

// PCGCCTL
inline constexpr uint32_t PCGCCTL_STOPCLK = 1U << 0;
inline constexpr uint32_t PCGCCTL_GATECLK = 1U << 1;

// DCFG
inline constexpr uint32_t DCFG_DSPD_FS = 3U << 0;
inline constexpr uint32_t DCFG_DAD_MASK = 0x7FU << 4;
inline constexpr uint32_t DCFG_DAD(uint8_t a) { return static_cast<uint32_t>(a) << 4; }

// DCTL
inline constexpr uint32_t DCTL_RWUSIG = 1U << 0;
inline constexpr uint32_t DCTL_SDIS = 1U << 1;
inline constexpr uint32_t DCTL_CGINAK = 1U << 8;
inline constexpr uint32_t DCTL_SGONAK = 1U << 9;
inline constexpr uint32_t DCTL_CGONAK = 1U << 10;

// DEPCTL (IN/OUT endpoint control)
inline constexpr uint32_t DEPCTL_MPSIZ(uint32_t sz) { return sz & 0x7FF; }
inline constexpr uint32_t DEPCTL_USBAEP = 1U << 15;
inline constexpr uint32_t DEPCTL_NAKSTS = 1U << 17;
inline constexpr uint32_t DEPCTL_EPTYP(uint32_t t) { return (t & 3) << 18; }
inline constexpr uint32_t DEPCTL_STALL = 1U << 21;
inline constexpr uint32_t DEPCTL_TXFNUM(uint8_t n) { return static_cast<uint32_t>(n) << 22; }
inline constexpr uint32_t DEPCTL_CNAK = 1U << 26;
inline constexpr uint32_t DEPCTL_SNAK = 1U << 27;
inline constexpr uint32_t DEPCTL_SD0PID = 1U << 28;
inline constexpr uint32_t DEPCTL_EPDIS = 1U << 30;
inline constexpr uint32_t DEPCTL_EPENA = 1U << 31;

// DEPINT (IN/OUT endpoint interrupt)
inline constexpr uint32_t DEPINT_XFRC = 1U << 0;
inline constexpr uint32_t DEPINT_EPDISD = 1U << 1;
inline constexpr uint32_t DEPINT_STUP = 1U << 3;
inline constexpr uint32_t DEPINT_OTEPDIS = 1U << 4;
inline constexpr uint32_t DEPINT_STSPHSRX = 1U << 5;
inline constexpr uint32_t DEPINT_TXFE = 1U << 7;
inline constexpr uint32_t DEPINT_NAK = 1U << 13;

// DOEPMSK
inline constexpr uint32_t DOEPMSK_XFRCM = 1U << 0;
inline constexpr uint32_t DOEPMSK_EPDM = 1U << 1;
inline constexpr uint32_t DOEPMSK_STUPM = 1U << 3;
inline constexpr uint32_t DOEPMSK_OTEPSPRM = 1U << 5;
inline constexpr uint32_t DOEPMSK_NAKM = 1U << 13;

// DIEPMSK
inline constexpr uint32_t DIEPMSK_XFRCM = 1U << 0;
inline constexpr uint32_t DIEPMSK_EPDM = 1U << 1;
inline constexpr uint32_t DIEPMSK_TOM = 1U << 3;

// GRXSTSP packet status
inline constexpr uint32_t PKTSTS_OUT_NAK = 1;
inline constexpr uint32_t PKTSTS_OUT_DATA = 2;
inline constexpr uint32_t PKTSTS_OUT_COMPLETE = 3;
inline constexpr uint32_t PKTSTS_SETUP_COMPLETE = 4;
inline constexpr uint32_t PKTSTS_SETUP_DATA = 6;

// Endpoint types
inline constexpr uint32_t EPTYP_CONTROL = 0;
inline constexpr uint32_t EPTYP_ISOCHRONOUS = 1;
inline constexpr uint32_t EPTYP_BULK = 2;
inline constexpr uint32_t EPTYP_INTERRUPT = 3;

}  // namespace otg

// ============================================================================
// STM32 OTG HAL Implementation
// ============================================================================

/// STM32 OTG FS HAL Implementation
/// Satisfies the umiusb::Hal concept
template<uint32_t BaseAddr = 0x50000000, uint8_t MaxEndpoints = 4>
class Stm32OtgHal : public HalBase<Stm32OtgHal<BaseAddr, MaxEndpoints>> {
    using Regs = OtgRegs<BaseAddr>;
    using Base = HalBase<Stm32OtgHal<BaseAddr, MaxEndpoints>>;

public:
    static constexpr uint16_t EP0_SIZE = 64;
    static constexpr uint8_t MAX_EP = MaxEndpoints;

private:
    // Buffers
    alignas(4) uint8_t setup_buf_[8];
    alignas(4) uint8_t ep0_buf_[EP0_SIZE];
    alignas(4) uint8_t ep_rx_buf_[MAX_EP][64];

public:
    void init() {
        // Select FS embedded PHY
        Regs::reg(Regs::GUSBCFG) |= otg::GUSBCFG_PHYSEL;

        // Core soft reset
        wait_ahb_idle();
        Regs::reg(Regs::GRSTCTL) |= otg::GRSTCTL_CSRST;
        while (Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_CSRST) {}
        wait_ahb_idle();

        // Activate transceiver
        Regs::reg(Regs::GCCFG) |= otg::GCCFG_PWRDWN;

        // Force device mode
        Regs::reg(Regs::GUSBCFG) |= otg::GUSBCFG_FDMOD | otg::GUSBCFG_TRDT(6);
        delay(50000);  // Wait for mode switch

        // Soft disconnect
        Regs::reg(Regs::DCTL) |= otg::DCTL_SDIS;

        // Disable VBUS sensing
        Regs::reg(Regs::GCCFG) |= otg::GCCFG_NOVBUSSENS;
        Regs::reg(Regs::GCCFG) &= ~(otg::GCCFG_VBUSBSEN | otg::GCCFG_VBUSASEN);

        // Restart PHY clock
        Regs::reg(Regs::PCGCCTL) = 0;

        // Device config: full speed
        Regs::reg(Regs::DCFG) = otg::DCFG_DSPD_FS;

        // Flush FIFOs
        flush_tx_fifos(0x10);
        flush_rx_fifo();

        // Clear endpoint masks
        Regs::reg(Regs::DIEPMSK) = 0;
        Regs::reg(Regs::DOEPMSK) = 0;
        Regs::reg(Regs::DAINTMSK) = 0;

        // Disable all endpoints
        for (uint8_t i = 0; i < MAX_EP; ++i) {
            disable_endpoint(i, true);
            disable_endpoint(i, false);
            Regs::reg(Regs::DIEPINT(i)) = 0xFB7FU;
            Regs::reg(Regs::DOEPINT(i)) = 0xFB7FU;
        }

        // Configure FIFOs (320 words total for FS)
        Regs::reg(Regs::GRXFSIZ) = 128;             // RX FIFO: 128 words
        Regs::reg(Regs::DIEPTXF0) = (64U << 16) | 128;  // TX0: 64 words @ 128
        for (uint8_t i = 1; i < MAX_EP; ++i) {
            Regs::reg(Regs::DIEPTXF(i)) = (64U << 16) | (128 + i * 64);
        }

        // Clear pending interrupts
        Regs::reg(Regs::GINTSTS) = 0xBFFFFFFFU;

        // Enable interrupts
        Regs::reg(Regs::GINTMSK) = otg::GINTSTS_RXFLVL | otg::GINTSTS_USBSUSP |
                                   otg::GINTSTS_USBRST | otg::GINTSTS_ENUMDNE |
                                   otg::GINTSTS_IEPINT | otg::GINTSTS_OEPINT;
        Regs::reg(Regs::GAHBCFG) = otg::GAHBCFG_GINTMSK;

        // Configure EP0 IN
        Regs::reg(Regs::DIEPCTL(0)) = otg::DEPCTL_USBAEP | otg::DEPCTL_TXFNUM(0);

        // Enable EP0 interrupts
        Regs::reg(Regs::DAINTMSK) = (1U << 0) | (1U << 16);

        // Configure endpoint masks
        Regs::reg(Regs::DOEPMSK) = otg::DOEPMSK_STUPM | otg::DOEPMSK_XFRCM |
                                   otg::DOEPMSK_EPDM | otg::DOEPMSK_OTEPSPRM | otg::DOEPMSK_NAKM;
        Regs::reg(Regs::DIEPMSK) = otg::DIEPMSK_TOM | otg::DIEPMSK_XFRCM | otg::DIEPMSK_EPDM;

        // Prepare EP0 OUT for SETUP
        configure_ep0_out();
    }

    void connect() {
        Regs::reg(Regs::PCGCCTL) &= ~(otg::PCGCCTL_STOPCLK | otg::PCGCCTL_GATECLK);
        Regs::reg(Regs::DCTL) &= ~otg::DCTL_SDIS;
        Base::connected_ = true;
    }

    void disconnect() {
        Regs::reg(Regs::DCTL) |= otg::DCTL_SDIS;
        Base::connected_ = false;
    }

    void set_address(uint8_t addr) {
        Base::address_ = addr;
        uint32_t dcfg = Regs::reg(Regs::DCFG);
        dcfg &= ~otg::DCFG_DAD_MASK;
        dcfg |= otg::DCFG_DAD(addr);
        Regs::reg(Regs::DCFG) = dcfg;
    }

    void ep_configure(const EndpointConfig& config) {
        uint8_t ep = config.number;
        uint32_t type = transfer_type_to_hw(config.type);

        if (config.direction == Direction::In) {
            Regs::reg(Regs::DIEPCTL(ep)) = otg::DEPCTL_MPSIZ(config.max_packet_size) |
                                           otg::DEPCTL_USBAEP |
                                           otg::DEPCTL_EPTYP(type) |
                                           otg::DEPCTL_TXFNUM(ep) |
                                           otg::DEPCTL_SD0PID;
            Regs::reg(Regs::DAINTMSK) |= (1U << ep);
        } else {
            Regs::reg(Regs::DOEPCTL(ep)) = otg::DEPCTL_MPSIZ(config.max_packet_size) |
                                           otg::DEPCTL_USBAEP |
                                           otg::DEPCTL_EPTYP(type) |
                                           otg::DEPCTL_SD0PID |
                                           otg::DEPCTL_EPENA |
                                           otg::DEPCTL_CNAK;
            Regs::reg(Regs::DOEPTSIZ(ep)) = (1U << 19) | config.max_packet_size;
            Regs::reg(Regs::DAINTMSK) |= (1U << (ep + 16));
        }
    }

    void ep_write(uint8_t ep, const uint8_t* data, uint16_t len) {
        constexpr uint16_t mps = 64;

        // Set transfer size
        if (len == 0) {
            Regs::reg(Regs::DIEPTSIZ(ep)) = (1U << 19);  // PKTCNT=1, XFRSIZ=0
        } else {
            uint16_t pktcnt = (len + mps - 1) / mps;
            Regs::reg(Regs::DIEPTSIZ(ep)) = (static_cast<uint32_t>(pktcnt) << 19) | len;
        }

        // Enable endpoint
        Regs::reg(Regs::DIEPCTL(ep)) |= otg::DEPCTL_CNAK | otg::DEPCTL_EPENA;

        // Write to FIFO
        if (len > 0 && data) {
            uint32_t words = (len + 3) / 4;
            while ((Regs::reg(Regs::DTXFSTS(ep)) & 0xFFFF) < words) {}

            volatile uint32_t& fifo_reg = Regs::fifo(ep);
            const uint32_t* src = reinterpret_cast<const uint32_t*>(data);
            for (uint32_t i = 0; i < words; ++i) {
                fifo_reg = src[i];
            }
        }
    }

    void ep_stall(uint8_t ep, bool in) {
        if (in) {
            Regs::reg(Regs::DIEPCTL(ep)) |= otg::DEPCTL_STALL;
        } else {
            Regs::reg(Regs::DOEPCTL(ep)) |= otg::DEPCTL_STALL;
        }
    }

    void ep_unstall(uint8_t ep, bool in) {
        if (in) {
            Regs::reg(Regs::DIEPCTL(ep)) &= ~otg::DEPCTL_STALL;
            Regs::reg(Regs::DIEPCTL(ep)) |= otg::DEPCTL_SD0PID;
        } else {
            Regs::reg(Regs::DOEPCTL(ep)) &= ~otg::DEPCTL_STALL;
            Regs::reg(Regs::DOEPCTL(ep)) |= otg::DEPCTL_SD0PID;
        }
    }

    void poll() {
        uint32_t gintsts = Regs::reg(Regs::GINTSTS);
        uint32_t gintmsk = Regs::reg(Regs::GINTMSK);
        uint32_t gints = gintsts & gintmsk;

        // USB Reset
        if (gints & otg::GINTSTS_USBRST) {
            handle_reset();
            Regs::reg(Regs::GINTSTS) = otg::GINTSTS_USBRST;
        }

        // Enumeration done
        if (gints & otg::GINTSTS_ENUMDNE) {
            Regs::reg(Regs::DIEPCTL(0)) &= ~0x7FFU;
            Regs::reg(Regs::DCTL) |= otg::DCTL_CGINAK;
            Regs::reg(Regs::GUSBCFG) = (Regs::reg(Regs::GUSBCFG) & ~otg::GUSBCFG_TRDT_MASK) |
                                       otg::GUSBCFG_TRDT(6);
            Regs::reg(Regs::GINTSTS) = otg::GINTSTS_ENUMDNE;
        }

        // Suspend
        if (gints & otg::GINTSTS_USBSUSP) {
            Regs::reg(Regs::GINTSTS) = otg::GINTSTS_USBSUSP;
            Base::notify_suspend();
        }

        // RX FIFO not empty
        if (gintsts & otg::GINTSTS_RXFLVL) {
            Regs::reg(Regs::GINTMSK) &= ~otg::GINTSTS_RXFLVL;
            handle_rxflvl();
            Regs::reg(Regs::GINTMSK) |= otg::GINTSTS_RXFLVL;
        }

        // IN endpoint interrupt
        if (gints & otg::GINTSTS_IEPINT) {
            handle_iepint();
        }

        // OUT endpoint interrupt
        if (gints & otg::GINTSTS_OEPINT) {
            handle_oepint();
        }
    }

private:
    void handle_reset() {
        Base::address_ = 0;
        Regs::reg(Regs::DCTL) &= ~otg::DCTL_RWUSIG;
        flush_tx_fifos(0x10);

        for (uint8_t i = 0; i < MAX_EP; ++i) {
            Regs::reg(Regs::DIEPINT(i)) = 0xFB7FU;
            Regs::reg(Regs::DIEPCTL(i)) &= ~otg::DEPCTL_STALL;
            Regs::reg(Regs::DOEPINT(i)) = 0xFB7FU;
            Regs::reg(Regs::DOEPCTL(i)) &= ~otg::DEPCTL_STALL;
            Regs::reg(Regs::DOEPCTL(i)) |= otg::DEPCTL_SNAK;
        }

        Regs::reg(Regs::DAINTMSK) |= 0x10001U;
        Regs::reg(Regs::DOEPMSK) = otg::DOEPMSK_STUPM | otg::DOEPMSK_XFRCM |
                                   otg::DOEPMSK_EPDM | otg::DOEPMSK_OTEPSPRM | otg::DOEPMSK_NAKM;
        Regs::reg(Regs::DIEPMSK) = otg::DIEPMSK_TOM | otg::DIEPMSK_XFRCM | otg::DIEPMSK_EPDM;
        Regs::reg(Regs::DCFG) &= ~otg::DCFG_DAD_MASK;

        configure_ep0_out();
        Base::notify_reset();
    }

    void handle_rxflvl() {
        uint32_t rxsts = Regs::reg(Regs::GRXSTSP);
        uint8_t ep = rxsts & 0xF;
        uint16_t bcnt = (rxsts >> 4) & 0x7FF;
        uint8_t pktsts = (rxsts >> 17) & 0xF;

        if (pktsts == otg::PKTSTS_SETUP_DATA) {
            read_packet(setup_buf_, 8);
        } else if (pktsts == otg::PKTSTS_OUT_DATA && bcnt > 0) {
            uint8_t* buf = (ep == 0) ? ep0_buf_ : ep_rx_buf_[ep];
            read_packet(buf, bcnt);
            if (ep == 0) {
                Base::notify_ep0_rx(buf, bcnt);
            } else {
                Base::notify_rx(ep, buf, bcnt);
            }
        }
    }

    void handle_iepint() {
        uint32_t epint = Regs::reg(Regs::DAINT) & Regs::reg(Regs::DAINTMSK) & 0xFFFF;
        for (uint8_t ep = 0; ep < MAX_EP && epint; ++ep) {
            if (epint & (1U << ep)) {
                uint32_t epints = Regs::reg(Regs::DIEPINT(ep));
                if (epints & otg::DEPINT_XFRC) {
                    Regs::reg(Regs::DIEPEMPMSK) &= ~(1U << ep);
                    Regs::reg(Regs::DIEPINT(ep)) = otg::DEPINT_XFRC;
                    if (ep == 0) {
                        // Prepare for next SETUP
                        Regs::reg(Regs::DOEPTSIZ(0)) = (3U << 29) | (1U << 19) | EP0_SIZE;
                        Regs::reg(Regs::DOEPCTL(0)) |= otg::DEPCTL_EPENA | otg::DEPCTL_CNAK;
                    }
                    Base::notify_tx_complete(ep);
                }
            }
        }
    }

    void handle_oepint() {
        uint32_t epint = (Regs::reg(Regs::DAINT) & Regs::reg(Regs::DAINTMSK)) >> 16;
        for (uint8_t ep = 0; ep < MAX_EP && epint; ++ep) {
            if (epint & (1U << ep)) {
                uint32_t epints = Regs::reg(Regs::DOEPINT(ep));

                if (epints & otg::DEPINT_XFRC) {
                    Regs::reg(Regs::DOEPINT(ep)) = otg::DEPINT_XFRC;
                    if (ep != 0) {
                        // Re-enable OUT endpoint
                        Regs::reg(Regs::DOEPTSIZ(ep)) = (1U << 19) | 64;
                        Regs::reg(Regs::DOEPCTL(ep)) |= otg::DEPCTL_EPENA | otg::DEPCTL_CNAK;
                    }
                }

                if (epints & otg::DEPINT_STUP) {
                    Regs::reg(Regs::DOEPINT(ep)) = otg::DEPINT_STUP;
                    if (ep == 0) {
                        auto* setup = reinterpret_cast<SetupPacket*>(setup_buf_);
                        Base::notify_setup(*setup);
                    }
                }

                if (epints & otg::DEPINT_OTEPDIS) {
                    Regs::reg(Regs::DOEPINT(ep)) = otg::DEPINT_OTEPDIS;
                }

                if (epints & otg::DEPINT_STSPHSRX) {
                    Regs::reg(Regs::DOEPINT(ep)) = otg::DEPINT_STSPHSRX;
                }

                if (epints & otg::DEPINT_NAK) {
                    Regs::reg(Regs::DOEPINT(ep)) = otg::DEPINT_NAK;
                }
            }
        }
    }

    void configure_ep0_out() {
        if (Regs::reg(Regs::DOEPCTL(0)) & otg::DEPCTL_EPENA) return;
        Regs::reg(Regs::DOEPTSIZ(0)) = (1U << 19) | (3U * 8U) | (3U << 29);
    }

    void read_packet(uint8_t* buf, uint16_t len) {
        volatile uint32_t& fifo_reg = Regs::fifo(0);
        uint32_t words = len / 4;
        uint32_t* dst = reinterpret_cast<uint32_t*>(buf);

        for (uint32_t i = 0; i < words; ++i) {
            dst[i] = fifo_reg;
        }

        uint16_t remaining = len % 4;
        if (remaining) {
            uint32_t temp = fifo_reg;
            uint8_t* dst_byte = buf + (words * 4);
            for (uint16_t i = 0; i < remaining; ++i) {
                dst_byte[i] = static_cast<uint8_t>(temp >> (8 * i));
            }
        }
    }

    void disable_endpoint(uint8_t ep, bool in) {
        auto& ctl = in ? Regs::reg(Regs::DIEPCTL(ep)) : Regs::reg(Regs::DOEPCTL(ep));
        if (ctl & otg::DEPCTL_EPENA) {
            ctl = (ep == 0) ? otg::DEPCTL_SNAK : (otg::DEPCTL_EPDIS | otg::DEPCTL_SNAK);
        } else {
            ctl = 0;
        }
        auto& tsiz = in ? Regs::reg(Regs::DIEPTSIZ(ep)) : Regs::reg(Regs::DOEPTSIZ(ep));
        tsiz = 0;
    }

    void flush_tx_fifos(uint8_t fifo_num) {
        Regs::reg(Regs::GRSTCTL) = otg::GRSTCTL_TXFFLSH | otg::GRSTCTL_TXFNUM(fifo_num);
        while (Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_TXFFLSH) {}
    }

    void flush_rx_fifo() {
        Regs::reg(Regs::GRSTCTL) = otg::GRSTCTL_RXFFLSH;
        while (Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_RXFFLSH) {}
    }

    void wait_ahb_idle() {
        while (!(Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_AHBIDL)) {}
    }

    static void delay(uint32_t count) {
        for (volatile uint32_t i = 0; i < count; ++i) {}
    }

    static constexpr uint32_t transfer_type_to_hw(TransferType type) {
        switch (type) {
            case TransferType::Control: return otg::EPTYP_CONTROL;
            case TransferType::Isochronous: return otg::EPTYP_ISOCHRONOUS;
            case TransferType::Bulk: return otg::EPTYP_BULK;
            case TransferType::Interrupt: return otg::EPTYP_INTERRUPT;
            default: return otg::EPTYP_BULK;
        }
    }
};

// Type aliases for common configurations
using Stm32FsHal = Stm32OtgHal<0x50000000, 4>;  // USB OTG FS
// using Stm32HsHal = Stm32OtgHal<0x40040000, 6>;  // USB OTG HS (future)

}  // namespace umiusb
