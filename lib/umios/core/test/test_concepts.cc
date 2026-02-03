// SPDX-License-Identifier: MIT
// UMI-OS Concept Tests
// Tests for ProcessorLike and other concepts

#include <umitest.hh>
#include <umios/core/audio_context.hh>
#include <umios/core/processor.hh>

using namespace umitest;

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

bool test_processor_like_concept(TestContext& t) {
    t.assert_true(umi::ProcessorLike<SimpleProcessor>, "SimpleProcessor satisfies ProcessorLike");
    t.assert_true(umi::ProcessorLike<ControllableProcessor>, "ControllableProcessor satisfies ProcessorLike");
    return true;
}

bool test_controllable_concept(TestContext& t) {
    t.assert_true(umi::Controllable<ControllableProcessor>, "ControllableProcessor satisfies Controllable");
    t.assert_true(!umi::Controllable<SimpleProcessor>, "SimpleProcessor does NOT satisfy Controllable");
    return true;
}

// ============================================================================
// AudioContext tests
// ============================================================================

bool test_audio_context(TestContext& t) {
    float output_data[64] = {};
    float* output_ptr = output_data;

    umi::EventQueue<> event_queue;

    umi::AudioContext ctx{.inputs = {},
                          .outputs = {&output_ptr, 1},
                          .input_events = {},
                          .output_events = event_queue,
                          .sample_rate = 48000,
                          .buffer_size = 64,
                          .dt = 1.0f / 48000.0f,
                          .sample_position = 0};

    t.assert_eq(ctx.buffer_size, 64u);
    t.assert_eq(ctx.sample_rate, 48000u);
    t.assert_eq(ctx.num_outputs(), 1u);
    t.assert_eq(ctx.output(0), output_data);
    t.assert_eq(ctx.output(1), static_cast<float*>(nullptr));

    float expected_dt = 1.0f / 48000.0f;
    t.assert_near(ctx.dt, expected_dt);
    return true;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    Suite s("umios/core/concepts");

    s.section("ProcessorLike Concept");
    s.run("processor_like", test_processor_like_concept);

    s.section("Controllable Concept");
    s.run("controllable", test_controllable_concept);

    s.section("AudioContext");
    s.run("audio_context", test_audio_context);

    return s.summary();
}
