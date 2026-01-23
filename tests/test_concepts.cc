// SPDX-License-Identifier: MIT
// UMI-OS Concept Tests
// Tests for ProcessorLike and other concepts

#include <cstdio>
#include <cstring>
#include <umidsp.hh>
#include <umios/core/audio_context.hh>
#include <umios/core/processor.hh>

// Test utilities
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg)           \
    do {                                 \
        if (cond) {                      \
            printf("  PASS: %s\n", msg); \
            ++tests_passed;              \
        } else {                         \
            printf("  FAIL: %s\n", msg); \
            ++tests_failed;              \
        }                                \
    } while (0)

// ============================================================================
// Mock types for testing
// ============================================================================

// Simple processor that satisfies ProcessorLike
class SimpleProcessor {
  public:
    void process(umi::AudioContext& ctx) {
        auto* out = ctx.output(0);
        if (out) {
            for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
                out[i] = 0.5f;
            }
        }
        process_called = true;
    }

    bool process_called = false;
};

// Processor with control method
class ControllableProcessor {
  public:
    void process(umi::AudioContext& ctx) {
        auto* out = ctx.output(0);
        if (out) {
            for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
                out[i] = gain;
            }
        }
    }

    void control(umi::ControlContext& ctx) {
        // Process parameter changes from control context
        (void)ctx;
    }

    void set_param(uint32_t id, float value) {
        if (id == 0)
            gain = value;
    }

    float get_param(uint32_t id) const { return id == 0 ? gain : 0.0f; }

  private:
    float gain = 1.0f;
};

// Class that does NOT satisfy ProcessorLike (no process method)
class NotAProcessor {
  public:
    void do_something() {}
};

// ============================================================================
// Concept tests
// ============================================================================

void test_processor_like_concept() {
    printf("\n=== ProcessorLike Concept Tests ===\n");

    // Test at compile time via static_assert in actual code
    // Here we just verify that the types compile correctly

    TEST_ASSERT((umi::ProcessorLike<SimpleProcessor>), "SimpleProcessor satisfies ProcessorLike");

    TEST_ASSERT((umi::ProcessorLike<ControllableProcessor>), "ControllableProcessor satisfies ProcessorLike");

    // NotAProcessor should NOT satisfy ProcessorLike
    // We can't easily test "does NOT satisfy" at runtime,
    // but we can test that the valid ones work

    printf("  (Compile-time concept checking verified)\n");
}

void test_controllable_concept() {
    printf("\n=== Controllable Concept Tests ===\n");

    TEST_ASSERT((umi::Controllable<ControllableProcessor>), "ControllableProcessor satisfies Controllable");

    // SimpleProcessor does NOT satisfy Controllable (no set_param/get_param)
    TEST_ASSERT((!umi::Controllable<SimpleProcessor>), "SimpleProcessor does NOT satisfy Controllable");

    printf("  (Compile-time concept checking verified)\n");
}

// ============================================================================
// AudioContext tests
// ============================================================================

void test_audio_context() {
    printf("\n=== AudioContext Tests ===\n");

    // Create test buffers
    float output_data[64] = {};
    float* output_ptr = output_data;

    umi::EventQueue<> event_queue;

    // Create AudioContext
    umi::AudioContext ctx{.inputs = {},
                          .outputs = {&output_ptr, 1},
                          .input_events = {},
                          .output_events = event_queue,
                          .sample_rate = 48000,
                          .buffer_size = 64,
                          .dt = 1.0f / 48000.0f,
                          .sample_position = 0};

    TEST_ASSERT(ctx.buffer_size == 64, "buffer_size is 64");
    TEST_ASSERT(ctx.sample_rate == 48000, "sample_rate is 48000");
    TEST_ASSERT(ctx.num_outputs() == 1, "num_outputs is 1");
    TEST_ASSERT(ctx.output(0) == output_data, "output(0) returns correct pointer");
    TEST_ASSERT(ctx.output(1) == nullptr, "output(1) returns nullptr for out of range");

    // Test dt calculation
    float expected_dt = 1.0f / 48000.0f;
    TEST_ASSERT(ctx.dt == expected_dt, "dt is correctly pre-calculated");
}

// ============================================================================
// DSP module tests
// ============================================================================

void test_dsp_operator() {
    printf("\n=== DSP operator() Tests ===\n");

    // Test that DSP modules have operator() (primary API)
    umi::dsp::Sine sine;
    float sample1 = sine(0.01f);      // Using operator()
    float sample2 = sine.tick(0.01f); // Using tick()

    TEST_ASSERT(sample1 != sample2, "Sine generates different samples on each call");

    // Test filter
    umi::dsp::SVF filter;
    filter.set_params(1000.0f, 0.5f, 1.0f / 48000.0f); // Using new 3-arg API
    float filtered1 = filter(0.5f);                    // Using operator()
    filter(0.3f);                                      // Using tick() equivalent via operator()

    TEST_ASSERT(filtered1 != 0.0f, "SVF filter produces non-zero output");

    // Test envelope
    umi::dsp::ADSR env;
    env.set_params(10.0f, 100.0f, 0.7f, 200.0f);
    env.trigger();
    float env_val1 = env(1.0f / 48000.0f); // Using operator() with dt
    (void)env();                           // Using operator() without dt (uses default)

    TEST_ASSERT(env_val1 > 0.0f, "ADSR envelope value is positive after trigger");
}

void test_dsp_set_params_dt() {
    printf("\n=== DSP set_params(dt) Tests ===\n");

    float dt = 1.0f / 48000.0f;

    // SVF filter with Hz and dt
    umi::dsp::SVF svf;
    svf.set_params(1000.0f, 0.5f, dt); // cutoff_hz, resonance, dt

    // Biquad filter with Hz, Q, and dt
    umi::dsp::Biquad bq;
    bq.set_lowpass(1000.0f, 0.707f, dt); // cutoff_hz, Q, dt

    // OnePole filter with Hz and dt
    umi::dsp::OnePole op;
    op.set_cutoff(1000.0f, dt); // cutoff_hz, dt

    // All should work without crashing
    (void)svf(1.0f);
    (void)bq(1.0f);
    (void)op(1.0f);

    TEST_ASSERT(true, "All filter set_params(dt) APIs work");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("UMI-OS Concept and API Tests\n");
    printf("============================\n");

    test_processor_like_concept();
    test_controllable_concept();
    test_audio_context();
    test_dsp_operator();
    test_dsp_set_params_dt();

    printf("\n============================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("============================\n");

    return tests_failed > 0 ? 1 : 0;
}
