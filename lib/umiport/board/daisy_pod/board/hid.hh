// SPDX-License-Identifier: MIT
// Daisy Pod HID Driver — knobs, buttons, encoder, RGB LEDs
// Algorithm reference: libDaisy src/hid/{switch,encoder,led,ctrl}.cpp
#pragma once

#include <cstdint>
#include <array>
#include <mcu/gpio.hh>
#include <mcu/adc.hh>
#include <mcu/dma.hh>
#include <mcu/rcc.hh>
#include <transport/direct.hh>
// Use relative include to avoid resolving to daisy_seed's bsp.hh
#include "bsp.hh"

namespace umi::daisy::pod {

// ============================================================================
// Button — debounced momentary switch (active low, internal pull-up)
// ============================================================================

template <typename PinDef>
class Button {
    std::uint8_t state = 0x00;

public:
    void init(mm::DirectTransportT<>& t) {
        stm32h7::gpio_configure_pin<typename PinDef::Port>(
            t, PinDef::pin,
            stm32h7::gpio_mode::INPUT,
            stm32h7::gpio_otype::PUSH_PULL,
            stm32h7::gpio_speed::LOW,
            stm32h7::gpio_pupd::PULL_UP);
    }

    /// Call at ~1 kHz
    void debounce(mm::DirectTransportT<>& t) {
        auto idr = t.read(typename PinDef::Port::IDR{});
        bool raw = (idr >> PinDef::pin) & 1U;
        // Inverted polarity: 1 = pressed (pin low)
        state = (state << 1) | (!raw ? 1 : 0);
    }

    /// True on press edge (7 consecutive pressed samples)
    [[nodiscard]] bool just_pressed() const { return state == 0x7F; }

    /// True while held
    [[nodiscard]] bool pressed() const { return (state & 0x01) != 0; }

    /// True on release edge
    [[nodiscard]] bool just_released() const { return state == 0x80; }
};

// ============================================================================
// Encoder — quadrature rotary encoder with push button
// ============================================================================

class Encoder {
    std::uint8_t a = 0xFF;
    std::uint8_t b = 0xFF;
    std::int8_t inc = 0;
    Button<EncClick> click;

public:
    void init(mm::DirectTransportT<>& t) {
        stm32h7::gpio_configure_pin<EncA::Port>(
            t, EncA::pin, stm32h7::gpio_mode::INPUT,
            stm32h7::gpio_otype::PUSH_PULL, stm32h7::gpio_speed::LOW,
            stm32h7::gpio_pupd::PULL_UP);
        stm32h7::gpio_configure_pin<EncB::Port>(
            t, EncB::pin, stm32h7::gpio_mode::INPUT,
            stm32h7::gpio_otype::PUSH_PULL, stm32h7::gpio_speed::LOW,
            stm32h7::gpio_pupd::PULL_UP);
        click.init(t);
    }

    /// Call at ~1 kHz
    void debounce(mm::DirectTransportT<>& t) {
        auto idr_d = t.read(stm32h7::GPIOD::IDR{});
        auto idr_a = t.read(stm32h7::GPIOA::IDR{});

        a = (a << 1) | ((idr_d >> EncA::pin) & 1U);
        b = (b << 1) | ((idr_a >> EncB::pin) & 1U);

        inc = 0;
        if ((a & 0x03) == 0x02 && (b & 0x03) == 0x00) {
            inc = 1;
        } else if ((b & 0x03) == 0x02 && (a & 0x03) == 0x00) {
            inc = -1;
        }

        click.debounce(t);
    }

    [[nodiscard]] std::int8_t increment() const { return inc; }
    [[nodiscard]] bool click_pressed() const { return click.pressed(); }
    [[nodiscard]] bool click_just_pressed() const { return click.just_pressed(); }
};

// ============================================================================
// SoftPwmLed — software PWM LED (single channel)
// ============================================================================

template <typename PinDef>
class SoftPwmLed {
    float brightness = 0.0f;
    float phase = 0.0f;
    bool inverted;

    static float cube(float x) { return x * x * x; }

public:
    void init(mm::DirectTransportT<>& t, bool invert = true) {
        inverted = invert;
        stm32h7::gpio_configure_pin<typename PinDef::Port>(
            t, PinDef::pin,
            stm32h7::gpio_mode::OUTPUT,
            stm32h7::gpio_otype::PUSH_PULL,
            stm32h7::gpio_speed::LOW,
            stm32h7::gpio_pupd::NONE);
        // Start off
        if (inverted) {
            stm32h7::gpio_set<typename PinDef::Port>(t, PinDef::pin);
        } else {
            stm32h7::gpio_reset<typename PinDef::Port>(t, PinDef::pin);
        }
    }

    void set(float val) { brightness = cube(val); }

    /// Call at ~1 kHz. PWM frequency ~120 Hz (matching libDaisy).
    void update(mm::DirectTransportT<>& t, float samplerate) {
        phase += 120.0f / samplerate;
        if (phase > 1.0f) phase -= 1.0f;
        bool on = brightness > phase;
        if (inverted) on = !on;
        if (on) {
            stm32h7::gpio_set<typename PinDef::Port>(t, PinDef::pin);
        } else {
            stm32h7::gpio_reset<typename PinDef::Port>(t, PinDef::pin);
        }
    }
};

// ============================================================================
// RgbLed — 3-channel software PWM RGB LED
// ============================================================================

template <typename R, typename G, typename B>
class RgbLed {
    SoftPwmLed<R> r;
    SoftPwmLed<G> g;
    SoftPwmLed<B> b;

public:
    void init(mm::DirectTransportT<>& t, bool invert = true) {
        r.init(t, invert);
        g.init(t, invert);
        b.init(t, invert);
    }

    void set(float rv, float gv, float bv) {
        r.set(rv);
        g.set(gv);
        b.set(bv);
    }

    void update(mm::DirectTransportT<>& t, float samplerate) {
        r.update(t, samplerate);
        g.update(t, samplerate);
        b.update(t, samplerate);
    }
};

// ============================================================================
// Knobs — ADC1 with DMA, 16-bit, 32x oversampling
// ============================================================================

class Knobs {
    // DMA destination buffer (2 channels, 16-bit each)
    alignas(32) std::array<std::uint16_t, NUM_KNOBS> dma_buf{};

    // Filtered values (one-pole low-pass)
    std::array<float, NUM_KNOBS> filtered{};
    float coeff = 0.0f;

public:
    /// Initialize ADC1 with DMA1_Stream2 for 2 knob channels.
    /// Must be called after RCC clocks for GPIOC, ADC, DMA1 are enabled.
    void init(mm::DirectTransportT<>& t, float update_rate) {
        // Slew filter: 2ms time constant (matching libDaisy default)
        coeff = 1.0f / (0.002f * update_rate * 0.5f);
        if (coeff > 1.0f) coeff = 1.0f;

        // Configure knob pins as analog
        stm32h7::gpio_configure_pin<Knob1::Port>(t, Knob1::pin, stm32h7::gpio_mode::ANALOG);
        stm32h7::gpio_configure_pin<Knob2::Port>(t, Knob2::pin, stm32h7::gpio_mode::ANALOG);

        // --- ADC1 Setup ---
        // Exit deep power down, enable voltage regulator
        auto cr = t.read(stm32h7::ADC1::CR{});
        cr &= ~(1U << 29);  // Clear DEEPPWD
        t.write(stm32h7::ADC1::CR::value(cr));
        cr |= (1U << 28);   // Set ADVREGEN
        t.write(stm32h7::ADC1::CR::value(cr));

        // Wait for regulator startup (~10 µs)
        for (int i = 0; i < 5000; ++i) { asm volatile("" ::: "memory"); }

        // Configure ADC common: async clock with prescaler DIV4
        // per_ck = HSI 64 MHz → 64/4 = 16 MHz ADC clock
        auto ccr = t.read(stm32h7::ADC12_Common::CCR{});
        ccr &= ~(0x3U << 16);  // CKMODE = 00 (async)
        ccr &= ~(0xFU << 18);  // Clear PRESC
        ccr |= (stm32h7::adc_presc::DIV4 << 18);  // PRESC = DIV4
        t.write(stm32h7::ADC12_Common::CCR::value(ccr));

        // Set BOOST mode for 16 MHz ADC clock (BOOST=0b10 for 12.5-25 MHz range)
        cr = t.read(stm32h7::ADC1::CR{});
        cr &= ~(0x3U << 8);    // Clear BOOST
        cr |= (0x2U << 8);     // BOOST = 0b10
        t.write(stm32h7::ADC1::CR::value(cr));

        // Calibrate (offset + linearity)
        cr = t.read(stm32h7::ADC1::CR{});
        cr &= ~(1U << 30);  // Single-ended calibration
        cr |= (1U << 16);   // ADCALLIN = linearity calibration
        cr |= (1U << 31);   // ADCAL = start calibration
        t.write(stm32h7::ADC1::CR::value(cr));
        while (t.read(stm32h7::ADC1::CR{}) & (1U << 31)) {}  // Wait for ADCAL=0

        // CFGR: 16-bit resolution, continuous, DMA circular, no external trigger
        std::uint32_t cfgr = 0;
        cfgr |= (stm32h7::adc_dmngt::DMA_CIRCULAR << 0);  // DMNGT
        cfgr |= (stm32h7::adc_res::BITS_16 << 2);          // RES = 16-bit
        cfgr |= (1U << 13);                                  // CONT = continuous
        t.write(stm32h7::ADC1::CFGR::value(cfgr));

        // CFGR2: 32x oversampling, 5-bit right shift, triggered single
        std::uint32_t cfgr2 = 0;
        cfgr2 |= (1U << 0);          // ROVSE = enable oversampling
        cfgr2 |= (5U << 5);          // OVSS = 5 (right shift by 5 = divide by 32)
        cfgr2 |= (1U << 9);          // TROVS = triggered oversampling
        cfgr2 |= (31U << 16);        // OSVR = 31 (32 samples)
        t.write(stm32h7::ADC1::CFGR2::value(cfgr2));

        // Pre-channel selection: enable channels 4 and 10
        std::uint32_t pcsel = (1U << Knob1::adc_channel) | (1U << Knob2::adc_channel);
        t.write(stm32h7::ADC1::PCSEL::value(pcsel));

        // Sampling time: 8.5 cycles for both channels
        // Ch4 in SMPR1 bits[14:12], Ch10 in SMPR2 bits[2:0]
        auto smpr1 = t.read(stm32h7::ADC1::SMPR1{});
        smpr1 &= ~(0x7U << (Knob1::adc_channel * 3));
        smpr1 |= (stm32h7::adc_smp::CYCLES_8_5 << (Knob1::adc_channel * 3));
        t.write(stm32h7::ADC1::SMPR1::value(smpr1));

        auto smpr2 = t.read(stm32h7::ADC1::SMPR2{});
        smpr2 &= ~(0x7U << ((Knob2::adc_channel - 10) * 3));
        smpr2 |= (stm32h7::adc_smp::CYCLES_8_5 << ((Knob2::adc_channel - 10) * 3));
        t.write(stm32h7::ADC1::SMPR2::value(smpr2));

        // Sequence: 2 conversions — SQ1=ch4, SQ2=ch10
        std::uint32_t sqr1 = 0;
        sqr1 |= (1U << 0);                         // L = 1 (2 conversions, 0-based)
        sqr1 |= (Knob1::adc_channel << 6);         // SQ1 = 4
        sqr1 |= (Knob2::adc_channel << 12);        // SQ2 = 10
        t.write(stm32h7::ADC1::SQR1::value(sqr1));

        // --- DMA1 Stream2 Setup ---
        // Disable stream first
        auto dma_cr = t.read(stm32h7::DMA1_Stream2::CR{});
        dma_cr &= ~1U;  // EN = 0
        t.write(stm32h7::DMA1_Stream2::CR::value(dma_cr));
        while (t.read(stm32h7::DMA1_Stream2::CR{}) & 1U) {}

        // Clear DMA flags for stream 2 (LIFCR bits 21:16)
        t.write(stm32h7::DMA1::LIFCR::value(stm32h7::dma_flags::S0_ALL << 16));

        // Configure: P2M, circular, halfword, mem increment, low priority
        dma_cr = 0;
        dma_cr |= (stm32h7::dma_dir::PERIPH_TO_MEM << 6);
        dma_cr |= (1U << 8);   // CIRC
        dma_cr |= (1U << 10);  // MINC
        dma_cr |= (stm32h7::dma_size::HALFWORD << 11);  // PSIZE
        dma_cr |= (stm32h7::dma_size::HALFWORD << 13);  // MSIZE
        dma_cr |= (stm32h7::dma_pl::LOW << 16);
        t.write(stm32h7::DMA1_Stream2::CR::value(dma_cr));

        // Disable FIFO
        t.write(stm32h7::DMA1_Stream2::FCR::value(0));

        // Source: ADC1 DR register
        t.write(stm32h7::DMA1_Stream2::PAR::value(
            stm32h7::ADC1::base_address + 0x40));  // DR offset

        // Destination: DMA buffer
        t.write(stm32h7::DMA1_Stream2::M0AR::value(
            reinterpret_cast<std::uint32_t>(dma_buf.data())));

        // Number of items
        t.write(stm32h7::DMA1_Stream2::NDTR::value(NUM_KNOBS));

        // DMAMUX1 channel 2: ADC1 request
        using DMAMUX1_Ch2 = stm32h7::DMAmuxChannel<0x4002'0808>;
        auto mux_ccr = t.read(DMAMUX1_Ch2::CCR{});
        mux_ccr &= ~0xFFU;
        mux_ccr |= 9;  // DMA_REQUEST_ADC1
        t.write(DMAMUX1_Ch2::CCR::value(mux_ccr));

        // Enable DMA stream
        dma_cr = t.read(stm32h7::DMA1_Stream2::CR{});
        dma_cr |= 1U;  // EN
        t.write(stm32h7::DMA1_Stream2::CR::value(dma_cr));

        // Enable ADC
        cr = t.read(stm32h7::ADC1::CR{});
        cr |= 1U;  // ADEN
        t.write(stm32h7::ADC1::CR::value(cr));
        // Wait for ADRDY
        while (!(t.read(stm32h7::ADC1::ISR{}) & 1U)) {}
        // Clear ADRDY
        t.write(stm32h7::ADC1::ISR::value(1U));

        // Start continuous conversion
        cr = t.read(stm32h7::ADC1::CR{});
        cr |= (1U << 2);  // ADSTART
        t.write(stm32h7::ADC1::CR::value(cr));
    }

    /// Call at audio callback rate to update filtered values
    void process() {
        for (std::uint8_t i = 0; i < NUM_KNOBS; ++i) {
            float raw = static_cast<float>(dma_buf[i]) / 65536.0f;
            filtered[i] += coeff * (raw - filtered[i]);
        }
    }

    /// Get filtered knob value [0.0, 1.0]
    [[nodiscard]] float value(std::uint8_t idx) const {
        return (idx < NUM_KNOBS) ? filtered[idx] : 0.0f;
    }

    /// Get raw ADC value (16-bit)
    [[nodiscard]] std::uint16_t raw(std::uint8_t idx) const {
        return (idx < NUM_KNOBS) ? dma_buf[idx] : 0;
    }
};

// ============================================================================
// PodHid — aggregate of all Pod controls
// ============================================================================

class PodHid {
public:
    Button<Button1> button1;
    Button<Button2> button2;
    Encoder encoder;
    RgbLed<Led1R, Led1G, Led1B> led1;
    RgbLed<Led2R, Led2G, Led2B> led2;
    Knobs knobs;

    /// Initialize all HID hardware.
    /// Requires: GPIO clocks (A,B,C,D,G), DMA1 clock, ADC clock enabled.
    void init(mm::DirectTransportT<>& t, float update_rate) {
        button1.init(t);
        button2.init(t);
        encoder.init(t);
        led1.init(t, true);   // inverted polarity
        led2.init(t, true);
        knobs.init(t, update_rate);
    }

    /// Call at ~1 kHz for digital inputs + LED PWM
    void update_controls(mm::DirectTransportT<>& t, float samplerate) {
        button1.debounce(t);
        button2.debounce(t);
        encoder.debounce(t);
        led1.update(t, samplerate);
        led2.update(t, samplerate);
    }

    /// Call at audio callback rate for knob filtering
    void process_knobs() {
        knobs.process();
    }
};

} // namespace umi::daisy::pod
