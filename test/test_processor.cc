// SPDX-License-Identifier: MIT
// UMI-OS Processor API test

#include <umi/processor.hpp>
#include <umi/audio_context.hpp>
#include <umi/event.hpp>
#include <umi/time.hpp>
#include <umi/types.hpp>

#include <cstdio>
#include <array>
#include <cmath>

// ============================================================================
// Test utilities
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  [TEST] %s... ", #name); \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("OK\n"); \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("FAILED: %s\n", msg); \
    } while(0)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { FAIL(#cond); return; } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { FAIL(#a " != " #b); return; } \
    } while(0)

#define ASSERT_NEAR(a, b, eps) \
    do { \
        if (std::abs((a) - (b)) > (eps)) { FAIL(#a " != " #b); return; } \
    } while(0)

// ============================================================================
// Simple sine oscillator for testing (RAII design)
// ============================================================================

class SineOscillator : public umi::Processor {
public:
    explicit SineOscillator(const umi::StreamConfig& config)
        : Processor(config)
    {
        sample_rate_ = static_cast<float>(config.sample_rate);
        phase_inc_ = frequency_ / sample_rate_;
    }
    
    void process(umi::AudioContext& ctx) override {
        if (ctx.num_outputs() < 1) return;
        
        auto* out = ctx.output(0);
        for (size_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = std::sin(phase_ * 2.0f * 3.14159265f);
            phase_ += phase_inc_;
            if (phase_ >= 1.0f) phase_ -= 1.0f;
        }
        process_count_++;
    }
    
    void reconfigure(const umi::StreamConfig& new_config) override {
        Processor::reconfigure(new_config);
        sample_rate_ = static_cast<float>(new_config.sample_rate);
        phase_inc_ = frequency_ / sample_rate_;
    }
    
    // Test accessors
    int process_count() const { return process_count_; }
    float sample_rate() const { return sample_rate_; }
    
private:
    float phase_ = 0.0f;
    float phase_inc_ = 0.0f;
    float frequency_ = 440.0f;
    float sample_rate_ = 48000.0f;
    int process_count_ = 0;
};

// ============================================================================
// Tests
// ============================================================================

void test_time_utils() {
    TEST(time_ms_to_samples);
    ASSERT_EQ(umi::time::ms_to_samples(1000.0f, 48000), 48000u);
    ASSERT_EQ(umi::time::ms_to_samples(500.0f, 44100), 22050u);
    PASS();
    
    TEST(time_samples_to_ms);
    ASSERT_NEAR(umi::time::samples_to_ms(48000, 48000), 1000.0f, 0.01f);
    ASSERT_NEAR(umi::time::samples_to_ms(22050, 44100), 500.0f, 0.01f);
    PASS();
    
    TEST(time_bpm_to_samples);
    // 120 BPM = 2 beats/sec = 0.5 sec/beat = 24000 samples/beat @ 48kHz
    ASSERT_EQ(umi::time::bpm_to_samples_per_beat(120.0f, 48000), 24000u);
    PASS();
    
    TEST(time_hz_conversions);
    ASSERT_NEAR(umi::time::hz_to_samples_per_period(440.0f, 44100), 100.227f, 0.01f);
    PASS();
}

void test_event_queue() {
    TEST(event_queue_push_pop);
    umi::EventQueue<16> queue;
    ASSERT(queue.empty());
    
    auto e1 = umi::Event::note_on(0, 0, 0, 60, 100);
    ASSERT(queue.push(e1));
    ASSERT(!queue.empty());
    ASSERT_EQ(queue.size(), 1u);
    
    umi::Event out;
    ASSERT(queue.pop(out));
    ASSERT_EQ(out.midi.note(), 60);
    ASSERT_EQ(out.midi.velocity(), 100);
    ASSERT(queue.empty());
    PASS();
    
    TEST(event_queue_midi_helpers);
    umi::EventQueue<16> q;
    (void)q.push_midi(0, 10, 0x90, 64, 127);  // Note On
    
    umi::Event e;
    ASSERT(q.pop(e));
    ASSERT(e.midi.is_note_on());
    ASSERT_EQ(e.midi.note(), 64);
    ASSERT_EQ(e.sample_pos, 10u);
    PASS();
    
    TEST(event_queue_param);
    umi::EventQueue<16> pq;
    (void)pq.push_param(1, 5, 0.75f);
    
    umi::Event pe;
    ASSERT(pq.pop(pe));
    ASSERT_EQ(pe.type, umi::EventType::Param);
    ASSERT_EQ(pe.param.id, 1u);
    ASSERT_NEAR(pe.param.value, 0.75f, 0.001f);
    PASS();
    
    TEST(event_queue_pop_until);
    umi::EventQueue<16> tq;
    (void)tq.push(umi::Event::note_on(0, 0, 0, 60, 100));
    (void)tq.push(umi::Event::note_on(0, 64, 0, 62, 100));
    (void)tq.push(umi::Event::note_on(0, 128, 0, 64, 100));
    
    umi::Event te;
    ASSERT(tq.pop_until(32, te));
    ASSERT_EQ(te.midi.note(), 60);
    ASSERT(!tq.pop_until(32, te));  // Next event is at 64
    ASSERT(tq.pop_until(64, te));
    ASSERT_EQ(te.midi.note(), 62);
    PASS();
}

void test_processor_lifecycle() {
    TEST(processor_raii_construction);
    
    umi::StreamConfig config{48000, 256};
    SineOscillator osc(config);
    
    ASSERT_EQ(osc.config().sample_rate, 48000u);
    ASSERT_EQ(osc.config().buffer_size, 256u);
    ASSERT_NEAR(osc.sample_rate(), 48000.0f, 0.1f);
    PASS();
    
    TEST(processor_process);
    
    // Create audio context for process
    std::array<umi::sample_t, 256> out_buf{};
    umi::sample_t* out_ptr = out_buf.data();
    umi::EventQueue<> events;
    
    umi::AudioContext ctx{
        .inputs = {},
        .outputs = std::span<umi::sample_t* const>(&out_ptr, 1),
        .events = events,
        .sample_rate = 48000,
        .buffer_size = 256,
        .sample_position = 0
    };
    
    osc.process(ctx);
    ASSERT_EQ(osc.process_count(), 1);
    
    // Verify output is not silent
    bool has_nonzero = false;
    for (auto s : out_buf) {
        if (s != 0.0f) has_nonzero = true;
    }
    ASSERT(has_nonzero);
    PASS();
    
    TEST(processor_reconfigure);
    
    umi::StreamConfig new_config{96000, 512};
    osc.reconfigure(new_config);
    
    ASSERT_EQ(osc.config().sample_rate, 96000u);
    ASSERT_NEAR(osc.sample_rate(), 96000.0f, 0.1f);
    PASS();
}

void test_audio_context() {
    TEST(audio_context_clear);
    
    std::array<umi::sample_t, 64> buf1, buf2;
    buf1.fill(1.0f);
    buf2.fill(2.0f);
    
    umi::sample_t* outs[] = {buf1.data(), buf2.data()};
    umi::EventQueue<> events;
    
    umi::AudioContext ctx{
        .inputs = {},
        .outputs = std::span(outs),
        .events = events,
        .sample_rate = 48000,
        .buffer_size = 64,
        .sample_position = 0
    };
    
    ctx.clear_outputs();
    
    for (size_t i = 0; i < 64; ++i) {
        ASSERT_NEAR(buf1[i], 0.0f, 0.001f);
        ASSERT_NEAR(buf2[i], 0.0f, 0.001f);
    }
    PASS();
    
    TEST(audio_context_passthrough);
    
    std::array<umi::sample_t, 64> in_buf, out_buf;
    for (size_t i = 0; i < 64; ++i) in_buf[i] = static_cast<float>(i);
    out_buf.fill(0.0f);
    
    const umi::sample_t* in_ptr = in_buf.data();
    umi::sample_t* out_ptr = out_buf.data();
    umi::EventQueue<> events2;
    
    umi::AudioContext ctx2{
        .inputs = std::span(&in_ptr, 1),
        .outputs = std::span(&out_ptr, 1),
        .events = events2,
        .sample_rate = 48000,
        .buffer_size = 64,
        .sample_position = 0
    };
    
    ctx2.passthrough();
    
    for (size_t i = 0; i < 64; ++i) {
        ASSERT_NEAR(out_buf[i], static_cast<float>(i), 0.001f);
    }
    PASS();
}

void test_param_descriptor() {
    TEST(param_normalize);
    
    umi::ParamDescriptor freq{
        .id = 0,
        .name = "Frequency",
        .default_value = 440.0f,
        .min_value = 20.0f,
        .max_value = 20000.0f
    };
    
    ASSERT_NEAR(freq.normalize(20.0f), 0.0f, 0.001f);
    ASSERT_NEAR(freq.normalize(20000.0f), 1.0f, 0.001f);
    ASSERT_NEAR(freq.denormalize(0.5f), 10010.0f, 0.1f);
    PASS();
    
    TEST(param_clamp);
    ASSERT_NEAR(freq.clamp(10.0f), 20.0f, 0.001f);
    ASSERT_NEAR(freq.clamp(30000.0f), 20000.0f, 0.001f);
    ASSERT_NEAR(freq.clamp(440.0f), 440.0f, 0.001f);
    PASS();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n=== UMI-OS Processor API Tests ===\n\n");
    
    printf("[Time Utils]\n");
    test_time_utils();
    
    printf("\n[Event Queue]\n");
    test_event_queue();
    
    printf("\n[Processor Lifecycle]\n");
    test_processor_lifecycle();
    
    printf("\n[Audio Context]\n");
    test_audio_context();
    
    printf("\n[Param Descriptor]\n");
    test_param_descriptor();
    
    printf("\n=================================\n");
    printf("Tests: %d/%d passed\n", tests_passed, tests_run);
    printf("=================================\n\n");
    
    return (tests_passed == tests_run) ? 0 : 1;
}
