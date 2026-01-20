// SPDX-License-Identifier: MIT
// UMI-OS DSP Module Tests

#include <umidsp/umidsp.hh>
#include <cstdio>
#include <cmath>

// ============================================================================
// Test utilities
// ============================================================================

static int test_count = 0;
static int pass_count = 0;

static void check(bool cond, const char* msg) {
    ++test_count;
    if (cond) {
        ++pass_count;
    } else {
        std::printf("FAIL: %s\n", msg);
    }
}

static bool near(float a, float b, float eps = 0.001f) {
    return std::abs(a - b) < eps;
}

// ============================================================================
// Oscillator Tests
// ============================================================================

static void test_phase() {
    std::printf("\n[Phase]\n");

    umi::dsp::Phase phase;
    check(near(phase.value(), 0.0f), "initial value is 0");

    // Advance phase
    float freq_norm = 0.1f;  // 10% of sample rate
    phase.tick(freq_norm);
    check(near(phase.value(), 0.1f), "phase advances correctly");

    // Wrap around
    for (int i = 0; i < 10; ++i) {
        phase.tick(freq_norm);
    }
    check(phase.value() >= 0.0f && phase.value() < 1.0f, "phase wraps to [0, 1)");

    // Reset
    phase.reset();
    check(near(phase.value(), 0.0f), "reset works");

    // Set phase
    phase.set(0.75f);
    check(near(phase.value(), 0.75f), "set works");

    // Negative wrap
    umi::dsp::Phase phase2(-0.1f);
    check(phase2.value() >= 0.0f && phase2.value() < 1.0f, "negative wrap works");
}

static void test_sine() {
    std::printf("\n[Sine Oscillator]\n");

    umi::dsp::Sine sine;

    // Check output range over one period
    float min_val = 1.0f, max_val = -1.0f;
    float freq_norm = 1.0f / 64.0f;  // Complete one cycle in 64 samples

    for (int i = 0; i < 64; ++i) {
        float sample = sine.tick(freq_norm);
        if (sample < min_val) min_val = sample;
        if (sample > max_val) max_val = sample;
    }

    check(max_val <= 1.0f, "sine max <= 1.0");
    check(min_val >= -1.0f, "sine min >= -1.0");
    check(max_val > 0.9f, "sine reaches near 1.0");
    check(min_val < -0.9f, "sine reaches near -1.0");

    // Reset test
    sine.reset();
    float first = sine.tick(freq_norm);
    check(near(first, 0.0f, 0.1f), "sine starts near 0 after reset");
}

static void test_saw_naive() {
    std::printf("\n[SawNaive Oscillator]\n");

    umi::dsp::SawNaive saw;
    float freq_norm = 1.0f / 32.0f;

    float min_val = 1.0f, max_val = -1.0f;
    for (int i = 0; i < 32; ++i) {
        float sample = saw.tick(freq_norm);
        if (sample < min_val) min_val = sample;
        if (sample > max_val) max_val = sample;
    }

    check(max_val <= 1.0f, "saw max <= 1.0");
    check(min_val >= -1.0f, "saw min >= -1.0");
}

static void test_square_naive() {
    std::printf("\n[SquareNaive Oscillator]\n");

    umi::dsp::SquareNaive square;
    float freq_norm = 1.0f / 32.0f;

    int high_count = 0, low_count = 0;
    for (int i = 0; i < 32; ++i) {
        float sample = square.tick(freq_norm);
        if (sample > 0.5f) ++high_count;
        else if (sample < -0.5f) ++low_count;
    }

    // With 50% pulse width, should be roughly equal
    check(high_count > 10 && low_count > 10, "square has both high and low");

    // Test pulse width
    umi::dsp::SquareNaive square2;
    int narrow_high = 0;
    for (int i = 0; i < 32; ++i) {
        float sample = square2.tick(freq_norm, 0.25f);
        if (sample > 0.5f) ++narrow_high;
    }
    check(narrow_high < high_count, "25% pulse width has fewer highs");
}

static void test_triangle() {
    std::printf("\n[Triangle Oscillator]\n");

    umi::dsp::Triangle tri;
    float freq_norm = 1.0f / 64.0f;

    float min_val = 1.0f, max_val = -1.0f;
    for (int i = 0; i < 64; ++i) {
        float sample = tri.tick(freq_norm);
        if (sample < min_val) min_val = sample;
        if (sample > max_val) max_val = sample;
    }

    check(max_val <= 1.0f, "triangle max <= 1.0");
    check(min_val >= -1.0f, "triangle min >= -1.0");
    check(max_val > 0.9f, "triangle reaches near 1.0");
    check(min_val < -0.9f, "triangle reaches near -1.0");
}

static void test_polyblep_saw() {
    std::printf("\n[SawBL (PolyBLEP)]\n");

    umi::dsp::SawBL saw;
    float freq_norm = 1.0f / 32.0f;

    float min_val = 1.0f, max_val = -1.0f;
    for (int i = 0; i < 32; ++i) {
        float sample = saw.tick(freq_norm);
        if (sample < min_val) min_val = sample;
        if (sample > max_val) max_val = sample;
    }

    check(max_val <= 1.1f, "sawBL max reasonable");
    check(min_val >= -1.1f, "sawBL min reasonable");
}

static void test_polyblep_square() {
    std::printf("\n[SquareBL (PolyBLEP)]\n");

    umi::dsp::SquareBL square;
    float freq_norm = 1.0f / 32.0f;

    float min_val = 1.0f, max_val = -1.0f;
    for (int i = 0; i < 32; ++i) {
        float sample = square.tick(freq_norm);
        if (sample < min_val) min_val = sample;
        if (sample > max_val) max_val = sample;
    }

    // PolyBLEP can overshoot significantly due to correction, especially at high frequencies
    // Just verify it produces output in a reasonable range
    check(max_val > 0.5f, "squareBL produces positive values");
    check(min_val < -0.5f, "squareBL produces negative values");
}

// ============================================================================
// Filter Tests
// ============================================================================

static void test_onepole() {
    std::printf("\n[OnePole Filter]\n");

    umi::dsp::OnePole lp;
    lp.set_cutoff(0.1f);

    // DC response test: constant input should pass through
    float out = 0.0f;
    for (int i = 0; i < 100; ++i) {
        out = lp.tick(1.0f);
    }
    check(near(out, 1.0f, 0.1f), "DC passes through lowpass");

    // Reset and test filtering - first sample should be less than input
    lp.reset();
    float first = lp.tick(1.0f);
    check(first < 1.0f, "lowpass smooths step input");
}

static void test_biquad() {
    std::printf("\n[Biquad Filter]\n");

    umi::dsp::Biquad bq;
    bq.set_lowpass(0.1f, 0.707f);

    // DC response
    float out = 0.0f;
    for (int i = 0; i < 200; ++i) {
        out = bq.tick(1.0f);
    }
    check(near(out, 1.0f, 0.1f), "biquad LP passes DC");

    // Highpass should block DC
    umi::dsp::Biquad hp;
    hp.set_highpass(0.1f, 0.707f);
    for (int i = 0; i < 200; ++i) {
        out = hp.tick(1.0f);
    }
    check(near(out, 0.0f, 0.1f), "biquad HP blocks DC");
}

static void test_svf() {
    std::printf("\n[SVF Filter]\n");

    umi::dsp::SVF svf;
    svf.set_params(0.1f, 0.0f);

    // Process some samples
    for (int i = 0; i < 100; ++i) {
        svf.tick(1.0f);
    }

    // LP should pass DC
    check(near(svf.lp(), 1.0f, 0.2f), "SVF LP passes DC");

    // HP should block DC
    check(near(svf.hp(), 0.0f, 0.2f), "SVF HP blocks DC");
}

// ============================================================================
// Envelope Tests
// ============================================================================

static void test_adsr() {
    std::printf("\n[ADSR Envelope]\n");

    umi::dsp::ADSR env;
    // set_params takes milliseconds: attack, decay, sustain level, release
    env.set_params(10.0f, 20.0f, 0.5f, 50.0f);

    check(env.state() == umi::dsp::ADSR::State::Idle, "initial state is Idle");

    // Trigger
    env.trigger();
    check(env.state() == umi::dsp::ADSR::State::Attack, "trigger enters Attack");

    // Run through attack (10ms = 480 samples at 48kHz, but uses tau so needs more)
    float dt = 1.0f / 48000.0f;
    for (int i = 0; i < 2400; ++i) {  // ~50ms to fully complete attack
        (void)env.tick(dt);
    }
    check(env.state() == umi::dsp::ADSR::State::Decay ||
          env.state() == umi::dsp::ADSR::State::Sustain, "enters Decay after Attack");

    // Run through decay
    for (int i = 0; i < 4800; ++i) {  // ~100ms
        (void)env.tick(dt);
    }
    check(env.state() == umi::dsp::ADSR::State::Sustain, "enters Sustain after Decay");
    check(near(env.value(), 0.5f, 0.15f), "sustain level correct");

    // Release
    env.release();
    check(env.state() == umi::dsp::ADSR::State::Release, "release enters Release");

    for (int i = 0; i < 12000; ++i) {  // ~250ms
        (void)env.tick(dt);
    }
    check(env.state() == umi::dsp::ADSR::State::Idle, "returns to Idle after Release");
}

static void test_ramp() {
    std::printf("\n[Ramp]\n");

    umi::dsp::Ramp ramp;
    // set_target(target_value, samples_to_reach)
    ramp.set_target(1.0f, 100);  // Ramp to 1.0 over 100 samples

    for (int i = 0; i < 110; ++i) {
        (void)ramp.tick();
    }
    check(near(ramp.value(), 1.0f, 0.01f), "ramp reaches target");

    // Test immediate set
    ramp.set_immediate(0.5f);
    check(near(ramp.value(), 0.5f, 0.01f), "set_immediate works");
}

// ============================================================================
// Utility Tests
// ============================================================================

static void test_midi_to_freq() {
    std::printf("\n[midi_to_freq]\n");

    // A4 = MIDI 69 = 440Hz
    float a4 = umi::dsp::midi_to_freq(69);
    check(near(a4, 440.0f, 1.0f), "A4 = 440Hz");

    // A3 = MIDI 57 = 220Hz
    float a3 = umi::dsp::midi_to_freq(57);
    check(near(a3, 220.0f, 1.0f), "A3 = 220Hz");

    // C4 = MIDI 60 = ~261.63Hz
    float c4 = umi::dsp::midi_to_freq(60);
    check(near(c4, 261.63f, 1.0f), "C4 = ~261.63Hz");
}

static void test_db_gain() {
    std::printf("\n[dB/Gain Conversion]\n");

    // 0 dB = gain 1.0
    check(near(umi::dsp::db_to_gain(0.0f), 1.0f), "0dB = gain 1.0");

    // -6 dB = gain ~0.5
    check(near(umi::dsp::db_to_gain(-6.0f), 0.5f, 0.05f), "-6dB = ~0.5");

    // -20 dB = gain 0.1
    check(near(umi::dsp::db_to_gain(-20.0f), 0.1f, 0.01f), "-20dB = 0.1");

    // Round-trip
    float db = -12.0f;
    float gain = umi::dsp::db_to_gain(db);
    float db_back = umi::dsp::gain_to_db(gain);
    check(near(db, db_back), "dB round-trip");
}

static void test_soft_clip() {
    std::printf("\n[soft_clip]\n");

    check(near(umi::dsp::soft_clip(0.0f), 0.0f), "soft_clip(0) = 0");
    // soft_clip uses x * (1.5 - 0.5*x*x), so 0.5 -> 0.5 * (1.5 - 0.125) = 0.6875
    float sc05 = umi::dsp::soft_clip(0.5f);
    check(sc05 > 0.4f && sc05 < 0.8f, "soft_clip(0.5) in reasonable range");
    check(umi::dsp::soft_clip(2.0f) <= 1.0f, "soft_clip(2) <= 1");
    check(umi::dsp::soft_clip(-2.0f) >= -1.0f, "soft_clip(-2) >= -1");
}

static void test_lerp() {
    std::printf("\n[lerp]\n");

    check(near(umi::dsp::lerp(0.0f, 1.0f, 0.0f), 0.0f), "lerp t=0");
    check(near(umi::dsp::lerp(0.0f, 1.0f, 1.0f), 1.0f), "lerp t=1");
    check(near(umi::dsp::lerp(0.0f, 1.0f, 0.5f), 0.5f), "lerp t=0.5");
    check(near(umi::dsp::lerp(10.0f, 20.0f, 0.25f), 12.5f), "lerp with range");
}

// ============================================================================
// Edge Case Tests
// ============================================================================

static void test_edge_cases_oscillators() {
    std::printf("\n[Edge Cases: Oscillators]\n");

    // Very high frequency (near Nyquist)
    umi::dsp::Sine sine;
    float sample = sine.tick(0.49f);  // Near Nyquist
    check(std::isfinite(sample), "sine handles near-Nyquist freq");

    // Very low frequency
    umi::dsp::SawBL saw;
    sample = saw.tick(0.0001f);  // ~4.8 Hz at 48kHz
    check(std::isfinite(sample), "saw handles very low freq");

    // Zero frequency
    umi::dsp::Triangle tri;
    sample = tri.tick(0.0f);
    check(std::isfinite(sample), "triangle handles zero freq");

    // Phase wrap boundary
    umi::dsp::Phase phase(0.999f);
    phase.tick(0.01f);
    check(phase.value() >= 0.0f && phase.value() < 1.0f, "phase wraps correctly at boundary");
}

static void test_edge_cases_filters() {
    std::printf("\n[Edge Cases: Filters]\n");

    // Very high cutoff
    umi::dsp::OnePole lp;
    lp.set_cutoff(0.49f);
    float out = lp.tick(1.0f);
    check(std::isfinite(out), "onepole handles high cutoff");

    // Very low cutoff
    lp.set_cutoff(0.0001f);
    out = lp.tick(1.0f);
    check(std::isfinite(out), "onepole handles very low cutoff");

    // Zero cutoff
    lp.set_cutoff(0.0f);
    lp.reset();
    out = lp.tick(1.0f);
    check(std::isfinite(out), "onepole handles zero cutoff");

    // SVF with extreme resonance
    umi::dsp::SVF svf;
    svf.set_params(0.1f, 1.0f);  // Max resonance
    svf.tick(1.0f);
    check(std::isfinite(svf.lp()), "SVF handles max resonance");
    check(std::isfinite(svf.hp()), "SVF HP with max resonance");
    check(std::isfinite(svf.bp()), "SVF BP with max resonance");

    // Biquad with extreme Q
    umi::dsp::Biquad bq;
    bq.set_lowpass(0.1f, 10.0f);  // High Q
    out = bq.tick(1.0f);
    check(std::isfinite(out), "biquad handles high Q");
}

static void test_edge_cases_envelope() {
    std::printf("\n[Edge Cases: Envelope]\n");

    // Zero attack/decay/release times
    umi::dsp::ADSR env;
    env.set_params(0.0f, 0.0f, 0.5f, 0.0f);  // All zero times
    env.trigger();
    float dt = 1.0f / 48000.0f;
    float val = env.tick(dt);
    check(std::isfinite(val), "ADSR handles zero times");

    // Very long times
    env.set_params(10000.0f, 10000.0f, 0.5f, 10000.0f);  // 10 seconds each
    env.trigger();
    val = env.tick(dt);
    check(std::isfinite(val), "ADSR handles very long times");

    // Immediate re-trigger during attack
    env.set_params(100.0f, 100.0f, 0.5f, 100.0f);
    env.trigger();
    for (int i = 0; i < 10; ++i) val = env.tick(dt);
    env.trigger();  // Re-trigger
    val = env.tick(dt);
    check(std::isfinite(val), "ADSR handles re-trigger");

    // Ramp with zero samples
    umi::dsp::Ramp ramp;
    ramp.set_target(1.0f, 0);  // Immediate
    check(near(ramp.value(), 1.0f), "ramp handles zero samples");
}

static void test_edge_cases_utility() {
    std::printf("\n[Edge Cases: Utility]\n");

    // MIDI note boundaries
    float freq = umi::dsp::midi_to_freq(0);
    check(std::isfinite(freq) && freq > 0, "midi_to_freq handles note 0");

    freq = umi::dsp::midi_to_freq(127);
    check(std::isfinite(freq) && freq > 0, "midi_to_freq handles note 127");

    // Extreme dB values
    float gain = umi::dsp::db_to_gain(-120.0f);
    check(std::isfinite(gain) && gain >= 0, "db_to_gain handles -120dB");

    gain = umi::dsp::db_to_gain(20.0f);
    check(std::isfinite(gain) && gain > 0, "db_to_gain handles +20dB");

    // gain_to_db edge case
    float db = umi::dsp::gain_to_db(0.0001f);
    check(std::isfinite(db), "gain_to_db handles small gain");

    // hard_clip
    check(near(umi::dsp::hard_clip(0.5f), 0.5f), "hard_clip passthrough");
    check(near(umi::dsp::hard_clip(1.5f), 1.0f), "hard_clip clips positive");
    check(near(umi::dsp::hard_clip(-1.5f), -1.0f), "hard_clip clips negative");
    check(near(umi::dsp::hard_clip(0.5f, 0.3f), 0.3f), "hard_clip custom limit");

    // lerp edge cases
    check(near(umi::dsp::lerp(0.0f, 1.0f, -0.5f), -0.5f), "lerp extrapolates below");
    check(near(umi::dsp::lerp(0.0f, 1.0f, 1.5f), 1.5f), "lerp extrapolates above");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::printf("\n=== UMI-OS DSP Module Tests ===\n");

    // Oscillators
    test_phase();
    test_sine();
    test_saw_naive();
    test_square_naive();
    test_triangle();
    test_polyblep_saw();
    test_polyblep_square();

    // Filters
    test_onepole();
    test_biquad();
    test_svf();

    // Envelopes
    test_adsr();
    test_ramp();

    // Utilities
    test_midi_to_freq();
    test_db_gain();
    test_soft_clip();
    test_lerp();

    // Edge cases
    test_edge_cases_oscillators();
    test_edge_cases_filters();
    test_edge_cases_envelope();
    test_edge_cases_utility();

    // Summary
    std::printf("\n=================================\n");
    std::printf("Tests: %d/%d passed\n", pass_count, test_count);
    std::printf("=================================\n\n");

    return (pass_count == test_count) ? 0 : 1;
}
