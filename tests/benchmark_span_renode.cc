// =====================================================================
// std::span vs raw pointer benchmark (Renode/Cortex-M4)
// =====================================================================
// DWT (Data Watchpoint and Trace) サイクルカウンタを使用
// =====================================================================

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>

// DWT Cycle Counter (Cortex-M3/M4/M7)
#define DWT_CTRL   (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004)
#define SCB_DEMCR  (*(volatile uint32_t*)0xE000EDFC)

extern "C" void umi_log(const char* msg);
extern "C" void umi_log_hex(uint32_t val);

constexpr size_t BUFFER_SIZE = 256;
constexpr size_t NUM_CHANNELS = 2;
constexpr size_t ITERATIONS = 1000;

// === Raw pointer version ===
struct AudioContextRaw {
    const float* const* inputs;
    float* const* outputs;
    size_t num_inputs;
    size_t num_outputs;
    uint32_t buffer_size;

    const float* input(size_t ch) const {
        return ch < num_inputs ? inputs[ch] : nullptr;
    }
    float* output(size_t ch) const {
        return ch < num_outputs ? outputs[ch] : nullptr;
    }
};

// === std::span version ===
struct AudioContextSpan {
    std::span<const float* const> inputs;
    std::span<float* const> outputs;
    uint32_t buffer_size;

    const float* input(size_t ch) const {
        return ch < inputs.size() ? inputs[ch] : nullptr;
    }
    float* output(size_t ch) const {
        return ch < outputs.size() ? outputs[ch] : nullptr;
    }
};

// Prevent inlining to get accurate measurements
__attribute__((noinline))
void process_raw(AudioContextRaw& ctx, float gain) {
    for (size_t ch = 0; ch < ctx.num_outputs; ++ch) {
        const float* in = ctx.input(ch);
        float* out = ctx.output(ch);
        if (!in || !out) continue;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = in[i] * gain;
        }
    }
}

__attribute__((noinline))
void process_span(AudioContextSpan& ctx, float gain) {
    for (size_t ch = 0; ch < ctx.outputs.size(); ++ch) {
        const float* in = ctx.input(ch);
        float* out = ctx.output(ch);
        if (!in || !out) continue;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = in[i] * gain;
        }
    }
}

// Direct access versions (most common pattern)
__attribute__((noinline))
void process_raw_direct(AudioContextRaw& ctx, float gain) {
    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        ctx.outputs[0][i] = ctx.inputs[0][i] * gain;
        ctx.outputs[1][i] = ctx.inputs[1][i] * gain;
    }
}

__attribute__((noinline))
void process_span_direct(AudioContextSpan& ctx, float gain) {
    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        ctx.outputs[0][i] = ctx.inputs[0][i] * gain;
        ctx.outputs[1][i] = ctx.inputs[1][i] * gain;
    }
}

static void dwt_init() {
    SCB_DEMCR |= (1 << 24);  // Enable DWT
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1;           // Enable cycle counter
}

static uint32_t dwt_get_cycles() {
    return DWT_CYCCNT;
}

// Buffers (static to avoid stack issues)
static std::array<float, BUFFER_SIZE> in_buf0;
static std::array<float, BUFFER_SIZE> in_buf1;
static std::array<float, BUFFER_SIZE> out_buf0;
static std::array<float, BUFFER_SIZE> out_buf1;

extern "C" void benchmark_span_main() {
    umi_log("=== span vs raw pointer benchmark ===");
    umi_log("Buffer: 256 samples, 2ch, 1000 iterations");

    // Initialize DWT
    dwt_init();

    // Fill buffers with test data
    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
        in_buf0[i] = static_cast<float>(i) / BUFFER_SIZE;
        in_buf1[i] = static_cast<float>(i) / BUFFER_SIZE;
    }

    // Setup pointer arrays
    const float* inputs_arr[] = {in_buf0.data(), in_buf1.data()};
    float* outputs_arr[] = {out_buf0.data(), out_buf1.data()};

    // Create contexts
    AudioContextRaw ctx_raw{
        .inputs = inputs_arr,
        .outputs = outputs_arr,
        .num_inputs = NUM_CHANNELS,
        .num_outputs = NUM_CHANNELS,
        .buffer_size = BUFFER_SIZE,
    };

    AudioContextSpan ctx_span{
        .inputs = std::span<const float* const>(inputs_arr),
        .outputs = std::span<float* const>(outputs_arr),
        .buffer_size = BUFFER_SIZE,
    };

    float gain = 0.5f;

    // Warmup
    for (size_t i = 0; i < 10; ++i) {
        process_raw(ctx_raw, gain);
        process_span(ctx_span, gain);
    }

    // === Benchmark 1: Helper method access ===
    umi_log("");
    umi_log("[Helper method access]");

    // Raw pointer
    uint32_t start = dwt_get_cycles();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        process_raw(ctx_raw, gain);
    }
    uint32_t raw_cycles = dwt_get_cycles() - start;

    umi_log("Raw pointer cycles:");
    umi_log_hex(raw_cycles);

    // Span
    start = dwt_get_cycles();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        process_span(ctx_span, gain);
    }
    uint32_t span_cycles = dwt_get_cycles() - start;

    umi_log("std::span cycles:");
    umi_log_hex(span_cycles);

    // === Benchmark 2: Direct access ===
    umi_log("");
    umi_log("[Direct array access]");

    start = dwt_get_cycles();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        process_raw_direct(ctx_raw, gain);
    }
    uint32_t raw_direct_cycles = dwt_get_cycles() - start;

    umi_log("Raw pointer cycles:");
    umi_log_hex(raw_direct_cycles);

    start = dwt_get_cycles();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        process_span_direct(ctx_span, gain);
    }
    uint32_t span_direct_cycles = dwt_get_cycles() - start;

    umi_log("std::span cycles:");
    umi_log_hex(span_direct_cycles);

    // Summary
    umi_log("");
    umi_log("=== Summary ===");
    umi_log("Helper: span/raw ratio (x100):");
    umi_log_hex((span_cycles * 100) / raw_cycles);
    umi_log("Direct: span/raw ratio (x100):");
    umi_log_hex((span_direct_cycles * 100) / raw_direct_cycles);

    umi_log("");
    umi_log("BENCHMARK_COMPLETE");
}
