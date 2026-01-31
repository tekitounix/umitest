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
template <uint32_t BaseAddr>
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
    static volatile uint32_t& reg(uint32_t offset) { return *reinterpret_cast<volatile uint32_t*>(BASE + offset); }

    static volatile uint32_t& fifo(uint8_t ep) { return *reinterpret_cast<volatile uint32_t*>(BASE + FIFO(ep)); }
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
inline constexpr uint32_t GUSBCFG_TRDT(uint32_t v) {
    return (v & 0xF) << 10;
}

// GRSTCTL
inline constexpr uint32_t GRSTCTL_CSRST = 1U << 0;
inline constexpr uint32_t GRSTCTL_RXFFLSH = 1U << 4;
inline constexpr uint32_t GRSTCTL_TXFFLSH = 1U << 5;
inline constexpr uint32_t GRSTCTL_TXFNUM(uint8_t n) {
    return static_cast<uint32_t>(n) << 6;
}
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
inline constexpr uint32_t GINTSTS_IISOIXFR = 1U << 20; // Incomplete isochronous IN transfer
inline constexpr uint32_t GINTSTS_WKUPINT = 1U << 31;

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
inline constexpr uint32_t DCFG_DAD(uint8_t a) {
    return static_cast<uint32_t>(a) << 4;
}

// DSTS
inline constexpr uint32_t DSTS_FNSOF_ODD = 1U << 8; // Frame number bit 0 (odd/even)

// DCTL
inline constexpr uint32_t DCTL_RWUSIG = 1U << 0;
inline constexpr uint32_t DCTL_SDIS = 1U << 1;
inline constexpr uint32_t DCTL_CGINAK = 1U << 8;
inline constexpr uint32_t DCTL_SGONAK = 1U << 9;
inline constexpr uint32_t DCTL_CGONAK = 1U << 10;

// DEPCTL (IN/OUT endpoint control)
inline constexpr uint32_t DEPCTL_MPSIZ(uint32_t sz) {
    return sz & 0x7FF;
}
inline constexpr uint32_t DEPCTL_USBAEP = 1U << 15;
inline constexpr uint32_t DEPCTL_NAKSTS = 1U << 17;
inline constexpr uint32_t DEPCTL_EPTYP(uint32_t t) {
    return (t & 3) << 18;
}
inline constexpr uint32_t DEPCTL_STALL = 1U << 21;
inline constexpr uint32_t DEPCTL_TXFNUM(uint8_t n) {
    return static_cast<uint32_t>(n) << 22;
}
inline constexpr uint32_t DEPCTL_CNAK = 1U << 26;
inline constexpr uint32_t DEPCTL_SNAK = 1U << 27;
inline constexpr uint32_t DEPCTL_SD0PID = 1U << 28;  // Set DATA0 PID (bulk/int) / Set even frame (iso)
inline constexpr uint32_t DEPCTL_SODDFRM = 1U << 29; // Set odd frame (isochronous)
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

} // namespace otg

// ============================================================================
// STM32 OTG HAL Implementation
// ============================================================================

/// STM32 OTG FS HAL Implementation
/// Satisfies the umiusb::Hal concept
template <uint32_t BaseAddr = 0x50000000, uint8_t MaxEndpoints = 4>
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
    alignas(4) uint8_t ep_rx_buf_[MAX_EP][640]; // 24-bit/96kHz Audio OUT needs up to 582 bytes

    // Endpoint configuration cache (for re-enabling after XFRC)
    std::array<uint16_t, MAX_EP> out_ep_mps_{};
    std::array<TransferType, MAX_EP> out_ep_type_{};
    std::array<TransferType, MAX_EP> in_ep_type_{};
    std::array<uint16_t, MAX_EP> last_in_len_{}; // Last IN transfer length for IISOIXFR retry

    // Feedback EP transmission control (ref: STM32F401_USB_AUDIO_DAC)
    // tx_flag prevents overlapping feedback transmissions.
    // fnsof captured at IsoINIncomplete is used for parity check before sending.
    bool fb_tx_flag_ = false;           // true while feedback EP transfer is in progress
    uint16_t fb_fnsof_at_iisoixfr_ = 0; // FNSOF captured at last IsoINIncomplete
    uint8_t fb_ep_ = 0;                 // Feedback endpoint number (set by AudioInterface)

    // EP0 IN multi-packet transfer state
    const uint8_t* ep0_tx_ptr_ = nullptr;
    uint16_t ep0_tx_remaining_ = 0;

  public:
    // Debug counters (non-volatile for increment, read from debugger)
    uint32_t dbg_setup_count_ = 0;
    uint32_t dbg_ep0_xfrc_count_ = 0;
    uint32_t dbg_ep0_stall_count_ = 0;
    uint32_t dbg_poll_count_ = 0; // Total poll() calls
    uint32_t dbg_sof_count_ = 0;
    uint32_t dbg_last_gintsts_ = 0;          // GINTSTS at last SOF
    uint32_t dbg_last_gintmsk_ = 0;          // GINTMSK at last SOF
    uint32_t dbg_last_gints_ = 0;            // gints at last SOF
    uint32_t dbg_sof_after_clear_ = 0;       // SOF flag after clear (should be 0)
    uint32_t dbg_sof_callback_count_ = 0;    // Actual SOF callback invocations (deduplicated)
    uint32_t dbg_sof_skip_count_ = 0;        // SOF skipped due to same frame
    uint16_t last_sof_frame_ = 0xFFFF;       // Last processed frame number for deduplication
    uint32_t dbg_ep1_out_count_ = 0;         // Audio OUT packet count
    uint32_t dbg_ep1_out_bytes_ = 0;         // Audio OUT total bytes
    int16_t dbg_ep1_rx_sample0_ = 0;         // First sample in Audio OUT packet
    int16_t dbg_ep1_rx_sample1_ = 0;         // Second sample in Audio OUT packet
    uint32_t dbg_ep1_fifo_word_ = 0;         // First word from EP1 packet
    uint32_t dbg_ep2_fb_count_ = 0;          // Feedback EP write count
    uint32_t dbg_ep2_fb_xfrc_ = 0;           // Feedback EP XFRC count
    uint32_t dbg_ep2_int_count_ = 0;         // Feedback EP IN interrupt count
    uint32_t dbg_ep2_int_last_ = 0;          // Last DIEPINT(2) snapshot
    uint32_t dbg_ep2_last_fb_ = 0;           // Last feedback value sent
    uint32_t dbg_ep2_diepctl_ = 0;           // DIEPCTL(2) value
    uint32_t dbg_ep2_diepint_ = 0;           // DIEPINT(2) value
    uint32_t dbg_ep2_fifo_before_ = 0;       // DTXFSTS(2) before FIFO write
    uint32_t dbg_ep2_fifo_after_ = 0;        // DTXFSTS(2) after FIFO write
    uint32_t dbg_ep3_in_count_ = 0;          // Audio IN packet count (EP3)
    uint32_t dbg_ep3_xfrc_count_ = 0;        // Audio IN XFRC count (EP3)
    uint32_t dbg_ep3_epena_busy_ = 0;        // Audio IN EPENA still set count (EP3)
    uint32_t dbg_ep1_in_count_ = 0;          // Audio IN packet count (EP1, for AudioInOnly)
    uint32_t dbg_ep1_xfrc_count_ = 0;        // Audio IN XFRC count (EP1, for AudioInOnly)
    uint32_t dbg_ep1_epena_busy_ = 0;        // Audio IN EPENA still set count (EP1)
    uint32_t dbg_ep1_last_len_ = 0;          // Last len passed to ep_write for EP1
    uint32_t dbg_ep1_fifo_before_ = 0;       // DTXFSTS before FIFO write
    uint32_t dbg_ep1_fifo_after_ = 0;        // DTXFSTS after FIFO write
    uint32_t dbg_ep1_txf_cfg_ = 0;           // DIEPTXF(1) value
    uint32_t dbg_ep1_diepctl_ = 0;           // DIEPCTL(1) value
    uint32_t dbg_ep1_diepint_ = 0;           // DIEPINT(1) value
    uint32_t dbg_iepint_count_ = 0;          // IEPINT handler call count
    uint32_t dbg_last_daint_ = 0;            // Last DAINT value in IEPINT
    uint32_t dbg_last_daintmsk_ = 0;         // Last DAINTMSK value
    uint32_t dbg_gintsts_iepint_count_ = 0;  // GINTSTS IEPINT flag count
    uint32_t dbg_ep1_fifo_at_xfrc_ = 0;      // FIFO space at XFRC
    uint32_t dbg_ep1_dsts_at_write_ = 0;     // DSTS at ep_write (frame number)
    uint32_t dbg_ep1_dsts_at_sof_ = 0;       // DSTS at SOF callback
    uint32_t dbg_ep1_parity_mismatch_ = 0;   // Times parity didn't match SOF
    uint32_t dbg_iisoixfr_count_ = 0;        // Incomplete isochronous IN transfer count
    uint32_t dbg_ep1_first_word_ = 0;        // First word written to EP1 FIFO
    uint32_t dbg_ep1_second_word_ = 0;       // Second word written to EP1 FIFO
    uint32_t dbg_fifo_first_word_ = 0;       // First word read from RX FIFO
    uint32_t dbg_ep1_doepint_ = 0;           // DOEPINT(1) last snapshot
    uint32_t dbg_ep1_doepint_err_count_ = 0; // Unhandled DOEPINT(1) bits count
    uint32_t dbg_ep1_doepint_err_last_ = 0;  // Last unhandled DOEPINT(1) bits
    uint32_t dbg_ep1_doepint_xfrc_count_ = 0;
    uint32_t dbg_ep1_doepint_stup_count_ = 0;
    uint32_t dbg_ep1_doepint_otepdis_count_ = 0;
    uint32_t dbg_ep1_doepint_stsphsrx_count_ = 0;
    uint32_t dbg_ep1_doepint_nak_count_ = 0;
    uint32_t dbg_rxflvl_count_ = 0;             // RXFLVL events
    uint32_t dbg_rxflvl_out_count_ = 0;         // RXFLVL OUT_DATA events
    uint32_t dbg_rxflvl_setup_count_ = 0;       // RXFLVL SETUP_DATA events
    uint32_t dbg_rxflvl_other_count_ = 0;       // RXFLVL other events
    uint32_t dbg_rxflvl_pktsts_count_[16] = {}; // RXFLVL pktsts histogram
    uint32_t dbg_ep1_out_bcnt_last_ = 0;        // EP1 OUT last byte count
    uint32_t dbg_ep1_out_bcnt_min_ = 0;         // EP1 OUT min byte count
    uint32_t dbg_ep1_out_bcnt_max_ = 0;         // EP1 OUT max byte count
    uint32_t dbg_ep1_out_zero_count_ = 0;       // EP1 OUT zero-length count
    uint32_t dbg_ep1_out_zero_outdata_ = 0;
    uint32_t dbg_ep1_out_zero_setup_ = 0;
    uint32_t dbg_ep1_out_zero_other_ = 0;
    uint32_t dbg_ep1_out_zero_rxsts_last_ = 0;
    uint32_t dbg_ep1_pktsts_last_ = 0;    // EP1 last RX pktsts
    uint32_t dbg_ep1_out_first_word_ = 0; // EP1 OUT first word
    uint32_t dbg_ep1_out_last_word_ = 0;  // EP1 OUT last word
    uint8_t dbg_last_brequest_ = 0;
    uint16_t dbg_last_wvalue_ = 0;
    uint16_t dbg_last_wlength_ = 0;
    uint32_t dbg_ep_configure_out_count_ = 0;
    uint32_t dbg_ep1_configure_count_ = 0;
    uint32_t dbg_ep1_doepctl_after_cfg_ = 0;

    void rearm_out_ep(uint8_t ep) {
        uint16_t mps = out_ep_mps_[ep];
        if (mps == 0) {
            mps = 64;
        }

        Regs::reg(Regs::DOEPTSIZ(ep)) &= ~(0x7FFFFU);      // Clear XFRSIZ
        Regs::reg(Regs::DOEPTSIZ(ep)) &= ~(0x3FFU << 19);  // Clear PKTCNT
        Regs::reg(Regs::DOEPTSIZ(ep)) |= (1U << 19) | mps; // PKTCNT=1, XFRSIZ=mps

        if (out_ep_type_[ep] == TransferType::Isochronous) {
            if ((Regs::reg(Regs::DSTS) & otg::DSTS_FNSOF_ODD) == 0) {
                Regs::reg(Regs::DOEPCTL(ep)) |= otg::DEPCTL_SODDFRM;
            } else {
                Regs::reg(Regs::DOEPCTL(ep)) |= otg::DEPCTL_SD0PID;
            }
        }

        Regs::reg(Regs::DOEPCTL(ep)) |= otg::DEPCTL_CNAK | otg::DEPCTL_EPENA;
    }

  public:
    /// Configure feedback endpoint number for tx_flag / parity control.
    void set_feedback_ep(uint8_t ep) { fb_ep_ = ep; }

    /// Check if feedback EP is ready to transmit (previous transfer done + parity match).
    /// Reference: STM32F401_USB_AUDIO_DAC SOF handler.
    [[nodiscard]] bool is_feedback_tx_ready() const {
        if (fb_tx_flag_) {
            return false; // Previous transfer still in flight
        }
        // Parity check: only transmit when current FNSOF parity matches the one
        // captured at last IsoINIncomplete. This avoids sending feedback on the
        // same frame that triggered the incomplete transfer.
        uint32_t odd_now = (Regs::reg(Regs::DSTS) & otg::DSTS_FNSOF_ODD) ? 1 : 0;
        return (fb_fnsof_at_iisoixfr_ & 1) == odd_now;
    }

    /// Mark feedback EP as in-flight. Called after ep_write for feedback.
    void set_feedback_tx_flag() { fb_tx_flag_ = true; }

    // Ensure isochronous OUT EP has correct frame parity for next frame.
    // Call from SOF handler to prevent incomplete ISO OUT transfers.
    void update_iso_out_ep(uint8_t ep) {
        if (out_ep_type_[ep] != TransferType::Isochronous) {
            return;
        }

        uint32_t doepctl = Regs::reg(Regs::DOEPCTL(ep));
        if ((doepctl & otg::DEPCTL_EPENA) == 0) {
            // EP disabled — full re-arm with correct parity
            rearm_out_ep(ep);
        } else {
            // EP enabled but may have wrong parity — update parity bits
            // Current frame odd → next frame even → SD0PID (even frame)
            // Current frame even → next frame odd → SODDFRM
            if ((Regs::reg(Regs::DSTS) & otg::DSTS_FNSOF_ODD) == 0) {
                Regs::reg(Regs::DOEPCTL(ep)) |= otg::DEPCTL_SODDFRM;
            } else {
                Regs::reg(Regs::DOEPCTL(ep)) |= otg::DEPCTL_SD0PID;
            }
        }
    }

    void init() {
        // Select FS embedded PHY (must be set before core reset for proper PHY selection)
        Regs::reg(Regs::GUSBCFG) |= otg::GUSBCFG_PHYSEL;

        // Core soft reset
        wait_ahb_idle();
        Regs::reg(Regs::GRSTCTL) |= otg::GRSTCTL_CSRST;
        while (Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_CSRST) {
        }
        wait_ahb_idle();

        // Activate transceiver
        Regs::reg(Regs::GCCFG) |= otg::GCCFG_PWRDWN;

        // Force device mode and re-select FS PHY (PHYSEL is cleared by core reset)
        Regs::reg(Regs::GUSBCFG) |= otg::GUSBCFG_PHYSEL | otg::GUSBCFG_FDMOD | otg::GUSBCFG_TRDT(6);
        delay(50000); // Wait for mode switch

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
        // 96kHz/24-bit/stereo OUT: 97 frames × 6 bytes = 582 bytes = 146 words
        // RX FIFO needs: max_packet_size/4 + 1 + setup_packets(2×8bytes) + status_info(10)
        // For 582 bytes: 146 + 1 + 4 + 10 = 161 words minimum, use 176 for safety
        // Budget: RX 176 + TX0 24 + TX1 16 + TX2 8 + TX3 96 = 320 words
        Regs::reg(Regs::GRXFSIZ) = 176;                  // RX FIFO: 176 words @ 0
        Regs::reg(Regs::DIEPTXF0) = (24U << 16) | 176;   // TX0: 24 words @ 176 (EP0 control)
        Regs::reg(Regs::DIEPTXF(1)) = (16U << 16) | 200; // TX1: 16 words @ 200 (MIDI/INT IN)
        Regs::reg(Regs::DIEPTXF(2)) = (8U << 16) | 216;  // TX2: 8 words @ 216 (Feedback)
        Regs::reg(Regs::DIEPTXF(3)) = (96U << 16) | 224; // TX3: 96 words @ 224 (Audio IN)

        // Clear pending interrupts
        Regs::reg(Regs::GINTSTS) = 0xBFFFFFFFU;

        // Enable interrupts (SOF required for Async feedback + Audio IN timing)
        Regs::reg(Regs::GINTMSK) = otg::GINTSTS_RXFLVL | otg::GINTSTS_USBRST | otg::GINTSTS_ENUMDNE |
                                   otg::GINTSTS_IEPINT | otg::GINTSTS_OEPINT | otg::GINTSTS_SOF;
        Regs::reg(Regs::GAHBCFG) = otg::GAHBCFG_GINTMSK;

        // Configure EP0 IN
        Regs::reg(Regs::DIEPCTL(0)) = otg::DEPCTL_USBAEP | otg::DEPCTL_TXFNUM(0);

        // Enable EP0 interrupts
        Regs::reg(Regs::DAINTMSK) = (1U << 0) | (1U << 16);

        // Configure endpoint masks
        Regs::reg(Regs::DOEPMSK) =
            otg::DOEPMSK_STUPM | otg::DOEPMSK_XFRCM | otg::DOEPMSK_EPDM | otg::DOEPMSK_OTEPSPRM | otg::DOEPMSK_NAKM;
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
            // Cache IN endpoint type for frame parity in ep_write
            in_ep_type_[ep] = config.type;

            uint32_t diepctl = otg::DEPCTL_MPSIZ(config.max_packet_size) | otg::DEPCTL_EPTYP(type) |
                               otg::DEPCTL_TXFNUM(ep) | otg::DEPCTL_SD0PID | otg::DEPCTL_USBAEP;
            Regs::reg(Regs::DIEPCTL(ep)) = diepctl;
            Regs::reg(Regs::DAINTMSK) |= (1U << ep);
        } else {
            // Cache OUT endpoint configuration for re-enabling
            out_ep_mps_[ep] = config.max_packet_size;
            out_ep_type_[ep] = config.type;

            ++dbg_ep_configure_out_count_;
            if (ep == 1) {
                ++dbg_ep1_configure_count_;
            }

            Regs::reg(Regs::DAINTMSK) |= (1U << (ep + 16));
            Regs::reg(Regs::DOEPCTL(ep)) = otg::DEPCTL_MPSIZ(config.max_packet_size) | otg::DEPCTL_EPTYP(type) |
                                           otg::DEPCTL_SD0PID | otg::DEPCTL_USBAEP;

            // Setup transfer size
            Regs::reg(Regs::DOEPTSIZ(ep)) = (1U << 19) | config.max_packet_size;

            // Enable endpoint
            Regs::reg(Regs::DOEPCTL(ep)) |= otg::DEPCTL_CNAK | otg::DEPCTL_EPENA;

            if (ep == 1) {
                dbg_ep1_doepctl_after_cfg_ = Regs::reg(Regs::DOEPCTL(ep));
            }
        }
    }

    /// Check if IN endpoint is busy (previous transfer not yet complete)
    [[nodiscard]] bool is_ep_busy(uint8_t ep) const {
        if (ep == 0 || ep >= MAX_EP)
            return false;
        if (in_ep_type_[ep] == TransferType::Isochronous) {
            return (Regs::reg(Regs::DIEPCTL(ep)) & otg::DEPCTL_EPENA) != 0;
        }
        return false;
    }

    void ep_write(uint8_t ep, const uint8_t* data, uint16_t len) {
        if (ep == 0) {
            // EP0: Use packet-by-packet transfer for multi-packet Control IN
            // STM32 OTG FS doesn't reliably handle multi-packet Control transfers
            // when all data is written to FIFO at once
            ep0_tx_ptr_ = data;
            ep0_tx_remaining_ = len;
            ep0_send_packet();
        } else {
            // Non-EP0: Single transfer
            // For isochronous IN: STM32Cube HAL compatible approach
            if (in_ep_type_[ep] == TransferType::Isochronous) {
                // If endpoint is still enabled, previous transfer hasn't completed.
                // Avoid reprogramming ISO IN EP while busy (prevents feedback overwrite).
                if (Regs::reg(Regs::DIEPCTL(ep)) & otg::DEPCTL_EPENA) {
                    if (ep == 1)
                        ++dbg_ep1_epena_busy_;
                    else if (ep == 3)
                        ++dbg_ep3_epena_busy_;
                    return;
                }

                uint32_t words = (len + 3) / 4;
                uint32_t fifo_space = Regs::reg(Regs::DTXFSTS(ep)) & 0xFFFF;

                // Debug: record state before transfer
                if (ep == 1) {
                    dbg_ep1_fifo_before_ = fifo_space;
                    dbg_ep1_txf_cfg_ = Regs::reg(Regs::DIEPTXF(1));
                    dbg_ep1_dsts_at_write_ = Regs::reg(Regs::DSTS);
                }
                if (ep == 2) {
                    dbg_ep2_fifo_before_ = fifo_space;
                }

                // If FIFO doesn't have enough space, flush it first
                if (fifo_space < words) {
                    // Wait for AHB idle before flush (as per HAL)
                    uint32_t timeout = 1000;
                    while (!(Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_AHBIDL) && --timeout > 0) {
                    }
                    // Flush this endpoint's TX FIFO
                    Regs::reg(Regs::GRSTCTL) = otg::GRSTCTL_TXFFLSH | otg::GRSTCTL_TXFNUM(ep);
                    timeout = 1000;
                    while ((Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_TXFFLSH) && --timeout > 0) {
                    }
                    // Update fifo_space after flush
                    fifo_space = Regs::reg(Regs::DTXFSTS(ep)) & 0xFFFF;
                    if (ep == 1)
                        dbg_ep1_fifo_before_ = fifo_space;
                }

                // Save length for potential IISOIXFR retry
                last_in_len_[ep] = len;

                // Setup DIEPTSIZ: MULCNT=1, PKTCNT=1, XFRSIZ=len
                Regs::reg(Regs::DIEPTSIZ(ep)) = (1U << 29) | (1U << 19) | len;

                // Set parity to match CURRENT frame (packet sent in this frame).
                // FNSOF reflects the current frame number at SOF callback time.
                // Odd FNSOF -> set SODDFRM; Even FNSOF -> set SD0PID/SEVNFRM.
                uint32_t diepctl = Regs::reg(Regs::DIEPCTL(ep));
                diepctl |= otg::DEPCTL_CNAK | otg::DEPCTL_EPENA;
                if ((Regs::reg(Regs::DSTS) & otg::DSTS_FNSOF_ODD) != 0) {
                    // Current frame is odd -> set SODDFRM
                    diepctl |= otg::DEPCTL_SODDFRM;
                } else {
                    // Current frame is even -> set SD0PID/SEVNFRM
                    diepctl |= otg::DEPCTL_SD0PID;
                }
                Regs::reg(Regs::DIEPCTL(ep)) = diepctl;

                // Write data to FIFO AFTER enabling endpoint (TinyUSB slave mode order)
                if (len > 0 && data != nullptr) {
                    volatile uint32_t& fifo_reg = Regs::fifo(ep);
                    const uint32_t* src = reinterpret_cast<const uint32_t*>(data);
                    // Debug: record first two words for EP1
                    if (ep == 1 && words >= 2) {
                        dbg_ep1_first_word_ = src[0];
                        dbg_ep1_second_word_ = src[1];
                    }
                    for (uint32_t i = 0; i < words; ++i) {
                        fifo_reg = src[i];
                    }
                }

                // Debug: record state after transfer setup
                if (ep == 1) {
                    ++dbg_ep1_in_count_;
                    dbg_ep1_last_len_ = len;
                    dbg_ep1_fifo_after_ = Regs::reg(Regs::DTXFSTS(ep)) & 0xFFFF;
                    dbg_ep1_diepctl_ = Regs::reg(Regs::DIEPCTL(1));
                    dbg_ep1_diepint_ |= Regs::reg(Regs::DIEPINT(1));
                }
                if (ep == 2) {
                    ++dbg_ep2_fb_count_;
                    // Record feedback value (3 bytes packed into uint32)
                    if (len >= 3 && data != nullptr) {
                        dbg_ep2_last_fb_ = data[0] | (data[1] << 8) | (data[2] << 16);
                    }
                    dbg_ep2_fifo_after_ = Regs::reg(Regs::DTXFSTS(2)) & 0xFFFF;
                    dbg_ep2_diepctl_ = Regs::reg(Regs::DIEPCTL(2));
                    dbg_ep2_diepint_ |= Regs::reg(Regs::DIEPINT(2));
                }
                if (ep == 3)
                    ++dbg_ep3_in_count_;
            } else {
                // Non-isochronous (Bulk/Interrupt): wait for previous transfer to complete
                // Check if EPENA is still set from previous transfer
                uint32_t timeout_wait = 100000;
                while ((Regs::reg(Regs::DIEPCTL(ep)) & otg::DEPCTL_EPENA) && --timeout_wait > 0) {
                    // Previous transfer still in progress - spin wait
                }

                constexpr uint16_t mps = 64;
                uint16_t pktcnt = len == 0 ? 1 : (len + mps - 1) / mps;
                Regs::reg(Regs::DIEPTSIZ(ep)) = (static_cast<uint32_t>(pktcnt) << 19) | len;
                Regs::reg(Regs::DIEPCTL(ep)) |= otg::DEPCTL_CNAK | otg::DEPCTL_EPENA;

                if (ep == 3)
                    ++dbg_ep3_in_count_;

                if (len > 0 && data != nullptr) {
                    uint32_t words = (len + 3) / 4;
                    uint32_t timeout = 10000;
                    while ((Regs::reg(Regs::DTXFSTS(ep)) & 0xFFFF) < words && --timeout > 0) {
                    }

                    volatile uint32_t& fifo_reg = Regs::fifo(ep);
                    const uint32_t* src = reinterpret_cast<const uint32_t*>(data);
                    for (uint32_t i = 0; i < words; ++i) {
                        fifo_reg = src[i];
                    }
                }
            }
        }
    }

  private:
    /// Send one packet on EP0 IN (called from ep_write and XFRC handler)
    void ep0_send_packet() {
        uint16_t chunk = (ep0_tx_remaining_ > EP0_SIZE) ? EP0_SIZE : ep0_tx_remaining_;

        // Setup: PKTCNT=1, XFRSIZ=chunk
        Regs::reg(Regs::DIEPTSIZ(0)) = (1U << 19) | chunk;
        Regs::reg(Regs::DIEPCTL(0)) |= otg::DEPCTL_CNAK | otg::DEPCTL_EPENA;

        if (chunk > 0 && ep0_tx_ptr_ != nullptr) {
            uint32_t words = (chunk + 3) / 4;
            uint32_t timeout = 10000;
            while ((Regs::reg(Regs::DTXFSTS(0)) & 0xFFFF) < words && --timeout > 0) {
            }

            volatile uint32_t& fifo_reg = Regs::fifo(0);
            const uint32_t* src = reinterpret_cast<const uint32_t*>(ep0_tx_ptr_);
            for (uint32_t i = 0; i < words; ++i) {
                fifo_reg = src[i];
            }
            ep0_tx_ptr_ += chunk;
        }
        ep0_tx_remaining_ -= chunk;
    }

  public:
    /// Prepare EP0 to receive data (for control OUT data stage)
    void ep0_prepare_rx(uint16_t len) {
        // Setup transfer size for EP0 OUT
        // STUPCNT=3, PKTCNT=1, XFRSIZ=len
        Regs::reg(Regs::DOEPTSIZ(0)) = (3U << 29) | (1U << 19) | len;
        Regs::reg(Regs::DOEPCTL(0)) |= otg::DEPCTL_CNAK | otg::DEPCTL_EPENA;
    }

    void ep_stall(uint8_t ep, bool in) {
        if (ep == 0)
            ++dbg_ep0_stall_count_;
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
        ++dbg_poll_count_;
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
            Regs::reg(Regs::GUSBCFG) = (Regs::reg(Regs::GUSBCFG) & ~otg::GUSBCFG_TRDT_MASK) | otg::GUSBCFG_TRDT(6);
            Regs::reg(Regs::GINTSTS) = otg::GINTSTS_ENUMDNE;
        }

        // Suspend
        if (gints & otg::GINTSTS_USBSUSP) {
            Regs::reg(Regs::GINTSTS) = otg::GINTSTS_USBSUSP;
            // Enable wakeup interrupt, enter low power mode
            Regs::reg(Regs::GINTMSK) |= otg::GINTSTS_WKUPINT;
            Regs::reg(Regs::PCGCCTL) |= otg::PCGCCTL_STOPCLK;
            Base::notify_suspend();
        }

        // Wakeup / Remote wakeup
        if (gints & otg::GINTSTS_WKUPINT) {
            Regs::reg(Regs::GINTSTS) = otg::GINTSTS_WKUPINT;
            // Exit low power mode, disable wakeup interrupt
            Regs::reg(Regs::PCGCCTL) &= ~otg::PCGCCTL_STOPCLK;
            Regs::reg(Regs::GINTMSK) &= ~otg::GINTSTS_WKUPINT;
            Base::notify_resume();
        }

        // SOF (Start of Frame) - for isochronous timing
        // Deduplicate by frame number: ISR may fire multiple times per frame due to
        // other interrupts (RXFLVL, IEPINT). Only process SOF once per frame.
        if (gints & otg::GINTSTS_SOF) {
            Regs::reg(Regs::GINTSTS) = otg::GINTSTS_SOF; // Clear flag
            ++dbg_sof_count_;

            uint32_t dsts = Regs::reg(Regs::DSTS);
            uint16_t frame = static_cast<uint16_t>((dsts >> 8) & 0x3FFF);
            dbg_last_gintsts_ = gintsts;
            dbg_last_gintmsk_ = gintmsk;
            dbg_last_gints_ = gints;
            dbg_sof_after_clear_ = Regs::reg(Regs::GINTSTS) & otg::GINTSTS_SOF;
            dbg_ep1_dsts_at_sof_ = dsts;

            if (frame != last_sof_frame_) {
                last_sof_frame_ = frame;
                ++dbg_sof_callback_count_;
                dbg_ep1_diepint_ |= Regs::reg(Regs::DIEPINT(1));
                if (Base::callbacks.on_sof != nullptr) {
                    Base::callbacks.on_sof(Base::callbacks.context);
                }
            } else {
                ++dbg_sof_skip_count_;
            }
        }

        // RX FIFO not empty
        if (gintsts & otg::GINTSTS_RXFLVL) {
            Regs::reg(Regs::GINTMSK) &= ~otg::GINTSTS_RXFLVL;
            handle_rxflvl();
            Regs::reg(Regs::GINTMSK) |= otg::GINTSTS_RXFLVL;
        }

        // IN endpoint interrupt
        if (gints & otg::GINTSTS_IEPINT) {
            ++dbg_gintsts_iepint_count_;
            handle_iepint();
        }

        // OUT endpoint interrupt
        if (gints & otg::GINTSTS_OEPINT) {
            handle_oepint();
        }

        // Incomplete isochronous IN transfer - TinyUSB-style retry
        if (gints & otg::GINTSTS_IISOIXFR) {
            Regs::reg(Regs::GINTSTS) = otg::GINTSTS_IISOIXFR;
            ++dbg_iisoixfr_count_;

            // Capture FNSOF for feedback parity check (ref: STM32F401_USB_AUDIO_DAC)
            fb_fnsof_at_iisoixfr_ = static_cast<uint16_t>((Regs::reg(Regs::DSTS) >> 8) & 0x3FFF);
            // If feedback EP missed, clear tx_flag so SOF can retry
            if (fb_ep_ > 0 && (Regs::reg(Regs::DIEPCTL(fb_ep_)) & otg::DEPCTL_EPENA) != 0) {
                fb_tx_flag_ = false;
                Regs::reg(Regs::GRSTCTL) = otg::GRSTCTL_TXFFLSH | otg::GRSTCTL_TXFNUM(fb_ep_);
                uint32_t timeout = 1000;
                while ((Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_TXFFLSH) && --timeout > 0) {
                }
            }

            // Check all isochronous IN endpoints and retry if they missed the transfer
            const uint32_t odd_now = (Regs::reg(Regs::DSTS) & otg::DSTS_FNSOF_ODD) ? 1 : 0;

            for (uint8_t ep : {1, 3}) {
                if (in_ep_type_[ep] != TransferType::Isochronous) {
                    continue;
                }

                uint32_t diepctl = Regs::reg(Regs::DIEPCTL(ep));
                // Check if enabled and parity matches current frame (meaning it missed)
                // DPID bit (bit16) indicates odd(1) or even(0) frame target
                const uint32_t dpid_odd = (diepctl >> 16) & 1;

                if ((diepctl & otg::DEPCTL_EPENA) != 0 && dpid_odd == odd_now) {
                    // This endpoint missed its transfer - retry with opposite parity
                    // Re-setup DIEPTSIZ with same size (TinyUSB style)
                    // Use last_in_len_ which was saved during ep_write
                    uint16_t len = last_in_len_[ep];
                    Regs::reg(Regs::DIEPTSIZ(ep)) = (1U << 29) | (1U << 19) | len;

                    // Re-enable with correct parity for next frame
                    diepctl |= otg::DEPCTL_CNAK | otg::DEPCTL_EPENA;
                    if (odd_now != 0) {
                        // Current frame is odd -> target even frame
                        diepctl |= otg::DEPCTL_SD0PID;
                    } else {
                        // Current frame is even -> target odd frame
                        diepctl |= otg::DEPCTL_SODDFRM;
                    }
                    Regs::reg(Regs::DIEPCTL(ep)) = diepctl;
                }
            }
        }
    }

  private:
    void handle_reset() {
        Base::address_ = 0;
        // Clear any pending EP0 transfer state
        ep0_tx_ptr_ = nullptr;
        ep0_tx_remaining_ = 0;
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
        Regs::reg(Regs::DOEPMSK) =
            otg::DOEPMSK_STUPM | otg::DOEPMSK_XFRCM | otg::DOEPMSK_EPDM | otg::DOEPMSK_OTEPSPRM | otg::DOEPMSK_NAKM;
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
        ++dbg_rxflvl_count_;
        ++dbg_rxflvl_pktsts_count_[pktsts & 0xF];

        if (pktsts == otg::PKTSTS_SETUP_DATA) {
            ++dbg_rxflvl_setup_count_;
            read_packet(setup_buf_, 8);
            if (ep == 1 && bcnt == 0) {
                ++dbg_ep1_out_zero_count_;
                ++dbg_ep1_out_zero_setup_;
                dbg_ep1_out_zero_rxsts_last_ = rxsts;
            }
        } else if (pktsts == otg::PKTSTS_OUT_DATA) {
            ++dbg_rxflvl_out_count_;
            uint8_t* buf = (ep == 0) ? ep0_buf_ : ep_rx_buf_[ep];
            if (bcnt > 0) {
                read_packet(buf, bcnt);
            }
            if (ep == 0) {
                if (bcnt > 0) {
                    Base::notify_ep0_rx(buf, bcnt);
                }
            } else {
                // Debug: count Audio OUT packets (EP1)
                if (ep == 1) {
                    ++dbg_ep1_out_count_;
                    dbg_ep1_out_bytes_ += bcnt;
                    dbg_ep1_out_bcnt_last_ = bcnt;
                    if (dbg_ep1_out_bcnt_min_ == 0 || bcnt < dbg_ep1_out_bcnt_min_) {
                        dbg_ep1_out_bcnt_min_ = bcnt;
                    }
                    if (bcnt > dbg_ep1_out_bcnt_max_) {
                        dbg_ep1_out_bcnt_max_ = bcnt;
                    }
                    if (bcnt == 0) {
                        ++dbg_ep1_out_zero_count_;
                        ++dbg_ep1_out_zero_outdata_;
                        dbg_ep1_out_zero_rxsts_last_ = rxsts;
                    }
                    dbg_ep1_pktsts_last_ = pktsts;
                    if (bcnt >= 4) {
                        dbg_ep1_out_first_word_ = *reinterpret_cast<uint32_t*>(buf);
                        dbg_ep1_out_last_word_ = *reinterpret_cast<uint32_t*>(buf + bcnt - 4);
                    }
                    // Capture first two samples from received audio data
                    if (bcnt >= 4) {
                        dbg_ep1_rx_sample0_ = static_cast<int16_t>(buf[0] | (buf[1] << 8));
                        dbg_ep1_rx_sample1_ = static_cast<int16_t>(buf[2] | (buf[3] << 8));
                        // Also capture raw first word
                        dbg_ep1_fifo_word_ = *reinterpret_cast<uint32_t*>(buf);
                    }
                }
                if (bcnt > 0) {
                    Base::notify_rx(ep, buf, bcnt);
                }
            }
        } else {
            ++dbg_rxflvl_other_count_;
            if (ep == 1) {
                dbg_ep1_pktsts_last_ = pktsts;
                if (bcnt == 0) {
                    ++dbg_ep1_out_zero_count_;
                    ++dbg_ep1_out_zero_other_;
                    dbg_ep1_out_zero_rxsts_last_ = rxsts;
                }
            }
        }
    }

    void handle_iepint() {
        ++dbg_iepint_count_;
        dbg_last_daint_ = Regs::reg(Regs::DAINT);
        dbg_last_daintmsk_ = Regs::reg(Regs::DAINTMSK);
        uint32_t epint = dbg_last_daint_ & dbg_last_daintmsk_ & 0xFFFF;
        for (uint8_t ep = 0; ep < MAX_EP && epint; ++ep) {
            if (epint & (1U << ep)) {
                uint32_t epints = Regs::reg(Regs::DIEPINT(ep));
                // Record EP1 DIEPINT for debugging
                if (ep == 1) {
                    dbg_ep1_diepint_ = epints;
                } else if (ep == 2) {
                    ++dbg_ep2_int_count_;
                    dbg_ep2_int_last_ = epints;
                    dbg_ep2_diepint_ = epints;
                }
                if (epints & otg::DEPINT_XFRC) {
                    Regs::reg(Regs::DIEPEMPMSK) &= ~(1U << ep);
                    Regs::reg(Regs::DIEPINT(ep)) = otg::DEPINT_XFRC;
                    if (ep == 0) {
                        ++dbg_ep0_xfrc_count_;
                        // EP0 XFRC: Check if more data to send (multi-packet transfer)
                        if (ep0_tx_remaining_ > 0) {
                            // Send next packet
                            ep0_send_packet();
                        } else {
                            // Transfer complete - prepare for Status OUT stage
                            Regs::reg(Regs::DOEPTSIZ(0)) = (3U << 29) | (1U << 19) | EP0_SIZE;
                            Regs::reg(Regs::DOEPCTL(0)) |= otg::DEPCTL_EPENA | otg::DEPCTL_CNAK;
                        }
                    } else if (ep == 1) {
                        ++dbg_ep1_xfrc_count_;
                        dbg_ep1_fifo_at_xfrc_ = Regs::reg(Regs::DTXFSTS(1)) & 0xFFFF;
                    } else if (ep == 2) {
                        ++dbg_ep2_fb_xfrc_;
                        fb_tx_flag_ = false; // Feedback transfer complete
                    } else if (ep == 3) {
                        ++dbg_ep3_xfrc_count_;
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
                bool rearm = false;
                if (ep == 1) {
                    dbg_ep1_doepint_ = epints;
                    constexpr uint32_t kHandled = otg::DEPINT_XFRC | otg::DEPINT_STUP | otg::DEPINT_OTEPDIS |
                                                  otg::DEPINT_STSPHSRX | otg::DEPINT_NAK;
                    uint32_t unhandled = epints & ~kHandled;
                    if (unhandled != 0) {
                        ++dbg_ep1_doepint_err_count_;
                        dbg_ep1_doepint_err_last_ = unhandled;
                    }
                    if (epints & otg::DEPINT_XFRC) {
                        ++dbg_ep1_doepint_xfrc_count_;
                    }
                    if (epints & otg::DEPINT_STUP) {
                        ++dbg_ep1_doepint_stup_count_;
                    }
                    if (epints & otg::DEPINT_OTEPDIS) {
                        ++dbg_ep1_doepint_otepdis_count_;
                    }
                    if (epints & otg::DEPINT_STSPHSRX) {
                        ++dbg_ep1_doepint_stsphsrx_count_;
                    }
                    if (epints & otg::DEPINT_NAK) {
                        ++dbg_ep1_doepint_nak_count_;
                    }
                }

                if (epints & otg::DEPINT_XFRC) {
                    Regs::reg(Regs::DOEPINT(ep)) = otg::DEPINT_XFRC;
                    if (ep != 0) {
                        rearm = true;
                    }
                }

                if (epints & otg::DEPINT_STUP) {
                    Regs::reg(Regs::DOEPINT(ep)) = otg::DEPINT_STUP;
                    if (ep == 0) {
                        ++dbg_setup_count_;

                        // Save request info for debugging
                        auto* setup = reinterpret_cast<SetupPacket*>(setup_buf_);
                        dbg_last_brequest_ = setup->bRequest;
                        dbg_last_wvalue_ = setup->wValue;
                        dbg_last_wlength_ = setup->wLength;

                        // Clear any STALL condition on EP0
                        Regs::reg(Regs::DIEPCTL(0)) &= ~otg::DEPCTL_STALL;
                        Regs::reg(Regs::DOEPCTL(0)) &= ~otg::DEPCTL_STALL;

                        // Abort any ongoing EP0 IN transfer
                        ep0_tx_ptr_ = nullptr;
                        ep0_tx_remaining_ = 0;

                        // Re-configure EP0 OUT for next SETUP/DATA packet
                        configure_ep0_out();

                        Base::notify_setup(*setup);
                    }
                }

                if (epints & otg::DEPINT_OTEPDIS) {
                    Regs::reg(Regs::DOEPINT(ep)) = otg::DEPINT_OTEPDIS;
                    if (ep != 0) {
                        rearm = true;
                    }
                }

                if (epints & otg::DEPINT_STSPHSRX) {
                    Regs::reg(Regs::DOEPINT(ep)) = otg::DEPINT_STSPHSRX;
                }

                if (epints & otg::DEPINT_NAK) {
                    Regs::reg(Regs::DOEPINT(ep)) = otg::DEPINT_NAK;
                }

                if (rearm && ep != 0) {
                    rearm_out_ep(ep);
                }
            }
        }
    }

    void configure_ep0_out() {
        if (Regs::reg(Regs::DOEPCTL(0)) & otg::DEPCTL_EPENA)
            return;
        Regs::reg(Regs::DOEPTSIZ(0)) = (1U << 19) | (3U * 8U) | (3U << 29);
        Regs::reg(Regs::DOEPCTL(0)) |= otg::DEPCTL_EPENA | otg::DEPCTL_CNAK;
    }

    void read_packet(uint8_t* buf, uint16_t len) {
        volatile uint32_t& fifo_reg = Regs::fifo(0);
        uint32_t words = len / 4;
        uint32_t* dst = reinterpret_cast<uint32_t*>(buf);

        for (uint32_t i = 0; i < words; ++i) {
            uint32_t word = fifo_reg;
            dst[i] = word;
            // Save first word for debugging
            if (i == 0)
                dbg_fifo_first_word_ = word;
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
        while (Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_TXFFLSH) {
        }
    }

    void flush_rx_fifo() {
        Regs::reg(Regs::GRSTCTL) = otg::GRSTCTL_RXFFLSH;
        while (Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_RXFFLSH) {
        }
    }

    void wait_ahb_idle() {
        while (!(Regs::reg(Regs::GRSTCTL) & otg::GRSTCTL_AHBIDL)) {
        }
    }

    static void delay(uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            asm volatile("" ::: "memory");
        }
    }

    static constexpr uint32_t transfer_type_to_hw(TransferType type) {
        switch (type) {
        case TransferType::Control:
            return otg::EPTYP_CONTROL;
        case TransferType::Isochronous:
            return otg::EPTYP_ISOCHRONOUS;
        case TransferType::Bulk:
            return otg::EPTYP_BULK;
        case TransferType::Interrupt:
            return otg::EPTYP_INTERRUPT;
        default:
            return otg::EPTYP_BULK;
        }
    }
};

// Type aliases for common configurations
using Stm32FsHal = Stm32OtgHal<0x50000000, 4>; // USB OTG FS
// using Stm32HsHal = Stm32OtgHal<0x40040000, 6>;  // USB OTG HS (future)

} // namespace umiusb
