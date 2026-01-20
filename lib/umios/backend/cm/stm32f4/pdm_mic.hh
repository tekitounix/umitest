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

/// 4-stage CIC decimation filter (sinc^4) with 64x decimation
/// For 2nd-order sigma-delta modulator, use N=L+1=3 minimum, N=4 for good balance
class CicDecimator {
    static constexpr int CIC_STAGES = 4;
    static constexpr uint32_t CIC_DECIMATION = 64;
    // CIC gain = R^N = 64^4 = 2^24
    // With 64x decimation: 2.048MHz PDM / 64 = 32kHz PCM output
    // Then need 32kHz -> 48kHz resampling (3:2)
    // Lower shift = finer resolution but needs lower gain to avoid clipping
    // Shift 6: output range ~±2^18, need gain ~0.125 (1/8)
    static constexpr int32_t OUTPUT_SHIFT = 6;

    // Using int32_t for Cortex-M4 performance (64-bit ops are slow)
    // CIC gain = R^N = 64^4 = 2^24, with input ±1, max accumulation < 2^30
    int32_t integrator_[CIC_STAGES] = {0, 0, 0, 0};
    int32_t comb_delay_[CIC_STAGES] = {0, 0, 0, 0};
    uint32_t cic_count_ = 0;

    // DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]
    // This is a simple first-order high-pass filter with cutoff ~10Hz at 32kHz
    // R = 0.995 = 32604/32768 in Q15 for fast response, or
    // R = 0.999 = 32735/32768 for slower response but less ripple
    static constexpr int32_t kDcBlockerR = 32604;  // 0.995 * 32768, ~10Hz cutoff at 32kHz
    int32_t dc_prev_in_ = 0;   // x[n-1]
    int32_t dc_prev_out_ = 0;  // y[n-1]

    int32_t gain_ = 2;  // Gain to compensate for OUTPUT_SHIFT=6

    // Warmup counter to allow DC filter to stabilize
    uint32_t warmup_samples_ = 0;
    static constexpr uint32_t WARMUP_SAMPLES = 1600;  // ~50ms at 32kHz (faster with new filter)

public:
    void reset() {
        for (int i = 0; i < CIC_STAGES; ++i) {
            integrator_[i] = 0;
            comb_delay_[i] = 0;
        }
        cic_count_ = 0;
        dc_prev_in_ = 0;
        dc_prev_out_ = 0;
        warmup_samples_ = 0;
    }

    void set_gain(int32_t gain) { gain_ = gain; }

    /// Process a 16-bit word of PDM data (16 bits at once)
    /// Returns true if a PCM sample is ready
    bool process(uint16_t pdm_word, int16_t& out) {
        bool sample_ready = false;

        // Process bits MSB-first (bit 15 = first/oldest PDM sample)
        // I2S receives data MSB-first in the 16-bit word
        for (int bit = 15; bit >= 0; --bit) {
            // PDM bit: 0 -> -1, 1 -> +1
            int32_t input = ((pdm_word >> bit) & 1) ? 1 : -1;

            // 4-stage CIC integrators (running at PDM rate)
            integrator_[0] += input;
            integrator_[1] += integrator_[0];
            integrator_[2] += integrator_[1];
            integrator_[3] += integrator_[2];

            // CIC decimation (64x)
            if (++cic_count_ >= CIC_DECIMATION) {
                cic_count_ = 0;

                // 4-stage CIC comb filters (using int32_t for CM4 performance)
                int32_t cic_out = integrator_[3];
                for (int i = 0; i < CIC_STAGES; ++i) {
                    int32_t temp = cic_out;
                    cic_out = cic_out - comb_delay_[i];
                    comb_delay_[i] = temp;
                }

                // Scale CIC output
                int32_t cic_pcm = cic_out >> OUTPUT_SHIFT;

                // DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]
                // This removes DC while preserving AC components
                // In Q15 fixed-point: y = x - x_prev + (R * y_prev) >> 15
                // Use signed multiply for 32x32->64 result, then shift
                int32_t feedback = static_cast<int32_t>(
                    (static_cast<int64_t>(kDcBlockerR) * dc_prev_out_) >> 15);
                int32_t dc_removed = cic_pcm - dc_prev_in_ + feedback;
                dc_prev_in_ = cic_pcm;
                dc_prev_out_ = dc_removed;

                // Warmup period: allow DC filter to stabilize before outputting
                if (warmup_samples_ < WARMUP_SAMPLES) {
                    ++warmup_samples_;
                    out = 0;  // Output silence during warmup
                    sample_ready = true;
                } else {
                    // Apply gain and saturate
                    int32_t gained = dc_removed * gain_;
                    if (gained > 32767) { gained = 32767; }
                    if (gained < -32768) { gained = -32768; }

                    out = static_cast<int16_t>(gained);
                    sample_ready = true;
                }
            }
        }

        return sample_ready;
    }

    /// Process a buffer of PDM data and output PCM samples
    uint32_t process_buffer(const uint16_t* pdm_buf, uint32_t pdm_count,
                           int16_t* pcm_buf, uint32_t pcm_max) {
        uint32_t pcm_out = 0;
        for (uint32_t i = 0; i < pdm_count && pcm_out < pcm_max; ++i) {
            int16_t sample;
            if (process(pdm_buf[i], sample)) {
                pcm_buf[pcm_out++] = sample;
            }
        }
        return pcm_out;
    }
};

/// Simple linear interpolation resampler (16kHz -> 48kHz)
/// Ratio: 48/16 = 3, so for every 1 input sample, output 3
class Resampler16to48 {
    int16_t prev_sample_ = 0;

public:
    void reset() {
        prev_sample_ = 0;
    }

    /// Resample from 16kHz to 48kHz
    /// Input: N samples -> Output: N * 3 samples
    /// @param in Input buffer (16kHz)
    /// @param in_count Number of input samples
    /// @param out Output buffer (48kHz) - must have space for in_count * 3
    /// @return Number of output samples produced
    uint32_t process(const int16_t* in, uint32_t in_count, int16_t* out) {
        uint32_t out_count = 0;

        for (uint32_t i = 0; i < in_count; ++i) {
            int16_t curr = in[i];

            // For 3:1 ratio, every input sample produces 3 output samples
            // Linear interpolation at 0/3, 1/3, 2/3 positions
            out[out_count++] = prev_sample_;  // 0/3 position
            out[out_count++] = static_cast<int16_t>((2 * prev_sample_ + curr) / 3);  // 1/3
            out[out_count++] = static_cast<int16_t>((prev_sample_ + 2 * curr) / 3);  // 2/3

            prev_sample_ = curr;
        }

        return out_count;
    }
};

/// Linear interpolation resampler (32kHz -> 48kHz)
/// Ratio: 48/32 = 3/2
/// Uses fractional phase accumulator for accurate sample timing
class Resampler32to48 {
    int16_t prev_sample_ = 0;
    int16_t curr_sample_ = 0;
    // Phase: 0-65535 represents position between prev and curr samples
    // Input step = 32/48 = 2/3, in Q16 = 43691
    static constexpr uint32_t kInputStep = 43691;  // 2/3 in Q16
    uint32_t phase_ = 0;
    bool has_samples_ = false;

public:
    void reset() {
        prev_sample_ = 0;
        curr_sample_ = 0;
        phase_ = 0;
        has_samples_ = false;
    }

    /// Resample from 32kHz to 48kHz
    /// @param input Input buffer (32kHz)
    /// @param in_count Number of input samples
    /// @param output Output buffer (48kHz) - must have space for in_count * 3 / 2 + 1
    /// @return Number of output samples produced
    uint32_t process(const int16_t* input, uint32_t in_count, int16_t* output) {
        uint32_t out_count = 0;
        uint32_t in_idx = 0;

        // Initialize with first sample if needed
        if (!has_samples_ && in_count > 0) {
            prev_sample_ = input[in_idx++];
            if (in_idx < in_count) {
                curr_sample_ = input[in_idx++];
            } else {
                curr_sample_ = prev_sample_;
            }
            has_samples_ = true;
        }

        while (true) {
            // Generate output sample by linear interpolation
            // frac = phase / 65536, interpolate between prev and curr
            int32_t frac = static_cast<int32_t>(phase_);
            int32_t interp = prev_sample_ + (((curr_sample_ - prev_sample_) * frac) >> 16);
            output[out_count++] = static_cast<int16_t>(interp);

            // Advance phase (move toward next input sample)
            phase_ += kInputStep;

            // If phase >= 1.0, we need the next input sample
            if (phase_ >= 65536) {
                phase_ -= 65536;
                prev_sample_ = curr_sample_;

                // Get next input sample
                if (in_idx < in_count) {
                    curr_sample_ = input[in_idx++];
                } else {
                    // No more input samples
                    break;
                }
            }
        }

        return out_count;
    }
};

/// PDM Microphone driver using I2S2 Master RX mode
/// STM32F4-Discovery: MP45DT02 MEMS microphone
///   - PB10: I2S2_CK (AF5) - Clock output to mic
///   - PC3:  I2S2_SD (AF5) - PDM data input from mic
///
/// Based on STM32CubeF4 BSP implementation:
///   - I2S2 in Master RX mode
///   - DMA1 Stream 3, Channel 0
///   - Clock frequency: 2x AudioFreq for PDM (e.g., 2x16kHz = 32kHz I2S clock)
struct PdmMic {
    // SPI2/I2S2 base address
    static constexpr uint32_t SPI2_BASE = 0x40003800;

    // Register offsets
    static constexpr uint32_t CR2 = 0x04;
    static constexpr uint32_t SR = 0x08;
    static constexpr uint32_t DR = 0x0C;
    static constexpr uint32_t I2SCFGR = 0x1C;
    static constexpr uint32_t I2SPR = 0x20;

    // SPI/I2S control registers
    static constexpr uint32_t CR1 = 0x00;

    // CR1 bits (SPI mode)
    static constexpr uint32_t CR1_DFF = 1U << 11;     // Data frame format: 1=16-bit, 0=8-bit
    static constexpr uint32_t CR1_RXONLY = 1U << 10;  // Receive only mode
    static constexpr uint32_t CR1_SPE = 1U << 6;      // SPI enable
    static constexpr uint32_t CR1_BR_DIV2 = 0U << 3;  // Baud rate /2
    static constexpr uint32_t CR1_BR_DIV4 = 1U << 3;  // Baud rate /4
    static constexpr uint32_t CR1_BR_DIV8 = 2U << 3;  // Baud rate /8
    static constexpr uint32_t CR1_BR_DIV16 = 3U << 3; // Baud rate /16
    static constexpr uint32_t CR1_BR_DIV32 = 4U << 3; // Baud rate /32
    static constexpr uint32_t CR1_BR_DIV64 = 5U << 3; // Baud rate /64
    static constexpr uint32_t CR1_MSTR = 1U << 2;     // Master mode
    static constexpr uint32_t CR1_CPOL = 1U << 1;     // Clock polarity
    static constexpr uint32_t CR1_CPHA = 1U << 0;     // Clock phase

    // I2SCFGR bits
    static constexpr uint32_t I2SCFGR_I2SMOD = 1U << 11;  // I2S mode enable
    static constexpr uint32_t I2SCFGR_I2SE = 1U << 10;    // I2S enable
    static constexpr uint32_t I2SCFGR_I2SCFG_MASTER_RX = 3U << 8;  // Master receive
    static constexpr uint32_t I2SCFGR_I2SSTD_LSB = 2U << 4;  // LSB justified
    static constexpr uint32_t I2SCFGR_I2SSTD_PHILIPS = 0U << 4;  // I2S Philips standard
    static constexpr uint32_t I2SCFGR_CKPOL = 1U << 3;  // Clock polarity (idle high)
    static constexpr uint32_t I2SCFGR_DATLEN_16 = 0U << 1;
    static constexpr uint32_t I2SCFGR_CHLEN_16 = 0U << 0;

    // I2SPR bits
    static constexpr uint32_t I2SPR_ODD = 1U << 8;    // Odd factor

    // CR2 bits
    static constexpr uint32_t CR2_RXDMAEN = 1U << 0;  // RX DMA enable

    // SR bits
    static constexpr uint32_t SR_RXNE = 1U << 0;  // RX not empty

    static volatile uint32_t& reg(uint32_t offset) {
        return *reinterpret_cast<volatile uint32_t*>(SPI2_BASE + offset);
    }

    /// Initialize I2S2 for PDM microphone input (Master RX mode)
    /// STM32F4-Discovery: MP45DT02 MEMS microphone
    /// Hardware: PB10=I2S2_CK, PC3=I2S2_SD (requires I2S mode, not SPI mode)
    ///
    /// PLLI2S clock = 86MHz (from main.cc)
    /// Target: ~2.048MHz PDM clock for 32kHz output after 64x decimation
    void init() {
        // Disable I2S first
        reg(I2SCFGR) = 0;

        // Wait for disable
        while ((reg(I2SCFGR) & I2SCFGR_I2SE) != 0) {}

        // Configure: I2S mode, Master RX, LSB justified, 16-bit
        // CKPOL=1: Clock idle high (I2S_CPOL_HIGH in HAL)
        // This matches STM32CubeF4 BSP configuration for MP45DT02
        reg(I2SCFGR) =
            I2SCFGR_I2SMOD |
            I2SCFGR_I2SCFG_MASTER_RX |
            I2SCFGR_I2SSTD_LSB |       // LSB justified (same as BSP)
            I2SCFGR_CKPOL |            // Clock polarity: idle high (same as BSP)
            I2SCFGR_DATLEN_16 |
            I2SCFGR_CHLEN_16;

        // I2S prescaler calculation for ~2.048MHz PDM clock (32kHz output)
        // PLLI2S clock = 86MHz (from main.cc init_plli2s: N=258, R=3)
        //
        // STM32F4 I2S bit clock formula (no MCLK for PDM):
        //   I2S_CK = PLLI2SCLK / (2 * I2SDIV + ODD)
        //
        // For 2.048MHz PDM clock (32kHz x 64 decimation):
        //   With I2SDIV=21, ODD=0: 2*21 + 0 = 42
        //   86MHz / 42 = 2.048MHz ✓
        //
        // CIC 64x decimation: 2.048MHz / 64 = 32kHz PCM output
        // Then resample 32kHz -> 48kHz for USB Audio
        reg(I2SPR) = 21;  // 2.048MHz PDM clock for 32kHz output
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
        while ((reg(SR) & SR_RXNE) == 0) {}
        return static_cast<uint16_t>(reg(DR));
    }

    static uint32_t dr_addr() { return SPI2_BASE + DR; }
};

/// DMA for PDM microphone input via I2S2 (SPI2) Master RX
/// DMA1 Stream 3, Channel 0 for SPI2_RX
/// Reference: STM32CubeF4 BSP (stm32f4_discovery_audio.c)
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
    static constexpr uint32_t CR_CHSEL_0 = 0U << 25;  // Channel 0 for SPI2_RX

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
