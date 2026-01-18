// SPDX-License-Identifier: MIT
// STM32F4-Discovery PDM Microphone Driver (MP45DT02)
// PDM input via I2S2 (SPI2) with DMA, includes CIC decimation filter
//
// Hardware connections on STM32F4-Discovery:
//   - PB10: I2S2_CK (AF5) - Clock to microphone
//   - PC3:  I2S2_SD (AF5) - PDM data from microphone
#pragma once

#include <cstdint>

namespace umi::stm32 {

/// CIC (Cascaded Integrator-Comb) decimation filter for PDM
/// 4-stage CIC with 64x decimation: 2.048MHz PDM -> 32kHz PCM
/// Note: Output is 32kHz, needs resampling to 48kHz if required
class CicDecimator {
    // 4-stage CIC filter state
    int32_t integrator_[4] = {0, 0, 0, 0};
    int32_t comb_delay_[4] = {0, 0, 0, 0};

    // Decimation counter
    uint32_t decim_count_ = 0;
    static constexpr uint32_t DECIMATION = 64;

    // Output scaling: CIC gain = R^N = 64^4 = 2^24
    // Right shift by 9 for 16-bit output with some headroom
    static constexpr uint32_t OUTPUT_SHIFT = 9;

    // DC offset removal (IIR high-pass)
    int32_t dc_estimate_ = 0;

public:
    void reset() {
        for (int i = 0; i < 4; ++i) {
            integrator_[i] = 0;
            comb_delay_[i] = 0;
        }
        decim_count_ = 0;
        dc_estimate_ = 0;
    }

    /// Process a 16-bit word of PDM data (16 bits at once)
    /// Returns true if a PCM sample is ready
    /// @param pdm_word 16 bits of PDM data (LSB first)
    /// @param out Output PCM sample (valid when returns true)
    bool process(uint16_t pdm_word, int16_t& out) {
        bool sample_ready = false;

        // Process each bit (LSB first, matching PDM standard)
        for (int bit = 0; bit < 16; ++bit) {
            // PDM bit: 0 -> -1, 1 -> +1
            int32_t input = ((pdm_word >> bit) & 1) ? 1 : -1;

            // Integrator stages (running at PDM rate)
            integrator_[0] += input;
            integrator_[1] += integrator_[0];
            integrator_[2] += integrator_[1];
            integrator_[3] += integrator_[2];

            // Decimation
            if (++decim_count_ >= DECIMATION) {
                decim_count_ = 0;

                // Comb stages (running at output rate)
                int32_t x = integrator_[3];

                for (int i = 0; i < 4; ++i) {
                    int32_t temp = x;
                    x = x - comb_delay_[i];
                    comb_delay_[i] = temp;
                }

                // High-pass filter for DC removal (simple IIR)
                // dc_estimate = dc_estimate * 0.999 + x * 0.001
                dc_estimate_ = dc_estimate_ - (dc_estimate_ >> 10) + (x >> 10);
                x = x - dc_estimate_;

                // Scale to 16-bit
                int32_t scaled = x >> OUTPUT_SHIFT;
                if (scaled > 32767) scaled = 32767;
                if (scaled < -32768) scaled = -32768;

                out = static_cast<int16_t>(scaled);
                sample_ready = true;
            }
        }

        return sample_ready;
    }

    /// Process a buffer of PDM data and output PCM samples
    /// @param pdm_buf Input PDM buffer (16-bit words)
    /// @param pdm_count Number of 16-bit words in PDM buffer
    /// @param pcm_buf Output PCM buffer
    /// @param pcm_max Maximum number of PCM samples to output
    /// @return Number of PCM samples produced
    uint32_t process_buffer(const uint16_t* pdm_buf, uint32_t pdm_count,
                           int16_t* pcm_buf, uint32_t pcm_max) {
        uint32_t pcm_count = 0;
        for (uint32_t i = 0; i < pdm_count && pcm_count < pcm_max; ++i) {
            int16_t sample;
            if (process(pdm_buf[i], sample)) {
                pcm_buf[pcm_count++] = sample;
            }
        }
        return pcm_count;
    }
};

/// Simple linear interpolation resampler (32kHz -> 48kHz)
/// Ratio: 48/32 = 3/2, so for every 2 input samples, output 3
class Resampler32to48 {
    int16_t prev_sample_ = 0;
    uint32_t phase_ = 0;  // 0 or 1

public:
    void reset() {
        prev_sample_ = 0;
        phase_ = 0;
    }

    /// Resample from 32kHz to 48kHz
    /// Input: 32 samples -> Output: 48 samples (ratio 3:2)
    /// @param in Input buffer (32kHz)
    /// @param in_count Number of input samples
    /// @param out Output buffer (48kHz) - must have space for in_count * 3 / 2
    /// @return Number of output samples produced
    uint32_t process(const int16_t* in, uint32_t in_count, int16_t* out) {
        uint32_t out_count = 0;

        for (uint32_t i = 0; i < in_count; ++i) {
            int16_t curr = in[i];

            // For 3:2 ratio, every 2 input samples produce 3 output samples
            // Phase 0: output prev, interpolate(prev,curr,1/3), interpolate(prev,curr,2/3)
            // Phase 1: output curr
            // This simplifies to alternating patterns

            if (phase_ == 0) {
                // Output 2 samples for this input
                out[out_count++] = prev_sample_;
                // Interpolate at 2/3 position
                out[out_count++] = static_cast<int16_t>((prev_sample_ + 2 * curr) / 3);
            } else {
                // Output 1 sample for this input
                // Interpolate at 1/3 position
                out[out_count++] = static_cast<int16_t>((2 * prev_sample_ + curr) / 3);
            }

            prev_sample_ = curr;
            phase_ = 1 - phase_;
        }

        return out_count;
    }
};

/// PDM Microphone driver using I2S2 (SPI2)
/// STM32F4-Discovery: MP45DT02 MEMS microphone
///   - PB10: I2S2_CK (AF5) - Clock output to mic
///   - PC3:  I2S2_SD (AF5) - PDM data input from mic
struct PdmMic {
    // SPI2/I2S2 base address
    static constexpr uint32_t SPI2_BASE = 0x40003800;

    // Register offsets
    static constexpr uint32_t CR1 = 0x00;
    static constexpr uint32_t CR2 = 0x04;
    static constexpr uint32_t SR = 0x08;
    static constexpr uint32_t DR = 0x0C;
    static constexpr uint32_t I2SCFGR = 0x1C;
    static constexpr uint32_t I2SPR = 0x20;

    // I2SCFGR bits
    static constexpr uint32_t I2SCFGR_I2SMOD = 1U << 11;  // I2S mode enable
    static constexpr uint32_t I2SCFGR_I2SE = 1U << 10;    // I2S enable
    static constexpr uint32_t I2SCFGR_I2SCFG_MASK = 3U << 8;
    static constexpr uint32_t I2SCFGR_I2SCFG_MASTER_RX = 3U << 8;  // Master receive
    static constexpr uint32_t I2SCFGR_I2SSTD_LSB = 2U << 4;  // LSB justified
    static constexpr uint32_t I2SCFGR_CKPOL = 1U << 3;  // Clock polarity
    static constexpr uint32_t I2SCFGR_DATLEN_16 = 0U << 1;
    static constexpr uint32_t I2SCFGR_CHLEN_16 = 0U << 0;

    // I2SPR bits
    static constexpr uint32_t I2SPR_MCKOE = 1U << 9;  // Master clock output enable
    static constexpr uint32_t I2SPR_ODD = 1U << 8;    // Odd factor

    // CR2 bits
    static constexpr uint32_t CR2_RXDMAEN = 1U << 0;  // RX DMA enable

    // SR bits
    static constexpr uint32_t SR_RXNE = 1U << 0;  // RX not empty

    volatile uint32_t& reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(SPI2_BASE + offset);
    }

    /// Initialize I2S2 for PDM microphone input
    /// Target clock: ~2.048MHz for 64x decimation to 32kHz
    /// With PLLI2S = 86MHz: 86MHz / (2 * 21) ≈ 2.05MHz
    void init() {
        // Disable I2S first
        reg(I2SCFGR) = 0;

        // Wait for disable
        while (reg(I2SCFGR) & I2SCFGR_I2SE) {}

        // Configure: I2S mode, Master RX, LSB justified, 16-bit, clock polarity high
        reg(I2SCFGR) =
            I2SCFGR_I2SMOD |
            I2SCFGR_I2SCFG_MASTER_RX |
            I2SCFGR_I2SSTD_LSB |
            I2SCFGR_CKPOL |  // Clock polarity high (idle high)
            I2SCFGR_DATLEN_16 |
            I2SCFGR_CHLEN_16;

        // I2S prescaler for ~2MHz clock
        // For 32kHz output with 64x decimation: 32kHz * 64 = 2.048MHz
        // PLLI2S = 86MHz, I2SDIV = 21, ODD = 0
        // I2S_CLK = PLLI2S / (2 * I2SDIV) = 86MHz / 42 ≈ 2.05MHz
        reg(I2SPR) = 21;  // No ODD, no MCLK output
    }

    void enable() {
        reg(I2SCFGR) |= I2SCFGR_I2SE;
    }

    void disable() {
        reg(I2SCFGR) &= ~I2SCFGR_I2SE;
    }

    void enable_dma() {
        reg(CR2) |= CR2_RXDMAEN;
    }

    uint16_t read() {
        while (!(reg(SR) & SR_RXNE)) {}
        return static_cast<uint16_t>(reg(DR));
    }

    uint32_t dr_addr() const { return SPI2_BASE + DR; }
};

/// DMA for PDM microphone input
/// DMA1 Stream 3, Channel 0 for SPI2_RX
struct DmaPdm {
    static constexpr uint32_t DMA1_BASE = 0x40026000;
    static constexpr uint32_t STREAM3_OFFSET = 0x10 + 0x18 * 3;  // Stream 3

    // DMA register offsets (from stream base)
    static constexpr uint32_t CR = 0x00;
    static constexpr uint32_t NDTR = 0x04;
    static constexpr uint32_t PAR = 0x08;
    static constexpr uint32_t M0AR = 0x0C;
    static constexpr uint32_t M1AR = 0x10;
    static constexpr uint32_t FCR = 0x14;

    // Low interrupt status/clear registers (for Streams 0-3)
    static constexpr uint32_t LISR = 0x00;
    static constexpr uint32_t LIFCR = 0x08;

    // CR bits
    static constexpr uint32_t CR_EN = 1U << 0;
    static constexpr uint32_t CR_DMEIE = 1U << 1;
    static constexpr uint32_t CR_TEIE = 1U << 2;
    static constexpr uint32_t CR_HTIE = 1U << 3;
    static constexpr uint32_t CR_TCIE = 1U << 4;
    static constexpr uint32_t CR_DIR_P2M = 0U << 6;   // Peripheral to memory
    static constexpr uint32_t CR_CIRC = 1U << 8;      // Circular mode
    static constexpr uint32_t CR_MINC = 1U << 10;     // Memory increment
    static constexpr uint32_t CR_PSIZE_16 = 1U << 11; // Peripheral 16-bit
    static constexpr uint32_t CR_MSIZE_16 = 1U << 13; // Memory 16-bit
    static constexpr uint32_t CR_PL_HIGH = 2U << 16;  // High priority
    static constexpr uint32_t CR_DBM = 1U << 18;      // Double buffer mode
    static constexpr uint32_t CR_CT = 1U << 19;       // Current target (read-only)
    static constexpr uint32_t CR_CHSEL_0 = 0U << 25;  // Channel 0

    // LISR/LIFCR bits for Stream 3
    static constexpr uint32_t TCIF3 = 1U << 27;
    static constexpr uint32_t HTIF3 = 1U << 26;
    static constexpr uint32_t TEIF3 = 1U << 25;
    static constexpr uint32_t DMEIF3 = 1U << 24;
    static constexpr uint32_t FEIF3 = 1U << 22;

    uint32_t stream_base;

    DmaPdm() : stream_base(DMA1_BASE + STREAM3_OFFSET) {}

    volatile uint32_t& reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(stream_base + offset);
    }

    volatile uint32_t& dma_reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(DMA1_BASE + offset);
    }

    /// Initialize DMA for double-buffered PDM input
    /// @param buf0 First buffer
    /// @param buf1 Second buffer
    /// @param count Number of 16-bit words per buffer
    /// @param peripheral_addr SPI2 DR address
    void init(uint16_t* buf0, uint16_t* buf1, uint32_t count, uint32_t peripheral_addr) {
        // Disable stream
        reg(CR) = 0;
        while (reg(CR) & CR_EN) {}

        // Clear all interrupt flags for stream 3
        dma_reg(LIFCR) = TCIF3 | HTIF3 | TEIF3 | DMEIF3 | FEIF3;

        // Configure stream
        reg(PAR) = peripheral_addr;
        reg(M0AR) = reinterpret_cast<uintptr_t>(buf0);
        reg(M1AR) = reinterpret_cast<uintptr_t>(buf1);
        reg(NDTR) = count;

        // CR: P2M, circular, memory increment, 16-bit, high priority, double buffer, channel 0
        reg(CR) =
            CR_CHSEL_0 |
            CR_PL_HIGH |
            CR_MSIZE_16 |
            CR_PSIZE_16 |
            CR_MINC |
            CR_CIRC |
            CR_DIR_P2M |
            CR_DBM |
            CR_TCIE;  // Transfer complete interrupt

        // Disable FIFO (direct mode)
        reg(FCR) = 0;
    }

    void enable() {
        reg(CR) |= CR_EN;
    }

    void disable() {
        reg(CR) &= ~CR_EN;
    }

    bool transfer_complete() const {
        return (dma_reg(LISR) & TCIF3) != 0;
    }

    void clear_tc() {
        dma_reg(LIFCR) = TCIF3;
    }

    /// Returns which buffer is currently being filled (0 or 1)
    uint8_t current_buffer() const {
        return (reg(CR) & CR_CT) ? 1 : 0;
    }
};

}  // namespace umi::stm32
