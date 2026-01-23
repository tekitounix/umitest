// SPDX-License-Identifier: MIT
// Loop style comparison test (index vs iterator)

#include <cstdio>
#include <vector>
#include <chrono>
#include <cmath>

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

static float run_index(const std::vector<float>& in, std::vector<float>& out) {
    const size_t n = in.size();
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float v = in[i] * 0.5f + 0.25f;
        out[i] = v;
        sum += v;
    }
    return sum;
}

static float run_iterator(const std::vector<float>& in, std::vector<float>& out) {
    float sum = 0.0f;
    auto in_it = in.begin();
    auto out_it = out.begin();
    auto in_end = in.end();
    for (; in_it != in_end; ++in_it, ++out_it) {
        float v = (*in_it) * 0.5f + 0.25f;
        *out_it = v;
        sum += v;
    }
    return sum;
}

static void test_loop_styles() {
    std::printf("\n[Loop Style: index vs iterator]\n");

    constexpr size_t n = 1u << 20; // 1,048,576
    constexpr int iters = 200;

    std::vector<float> in(n);
    std::vector<float> out(n);

    for (size_t i = 0; i < n; ++i) {
        in[i] = std::sin(static_cast<float>(i) * 0.001f);
    }

    // Warm-up
    volatile float sink = 0.0f;
    sink += run_index(in, out);
    sink += run_iterator(in, out);

    // Index loop timing
    auto t0 = std::chrono::steady_clock::now();
    float sum_index = 0.0f;
    for (int i = 0; i < iters; ++i) {
        sum_index += run_index(in, out);
    }
    auto t1 = std::chrono::steady_clock::now();

    // Iterator loop timing
    auto t2 = std::chrono::steady_clock::now();
    float sum_iter = 0.0f;
    for (int i = 0; i < iters; ++i) {
        sum_iter += run_iterator(in, out);
    }
    auto t3 = std::chrono::steady_clock::now();

    auto dur_index = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    auto dur_iter = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

    std::printf("index  : %lld us\n", static_cast<long long>(dur_index));
    std::printf("iterator: %lld us\n", static_cast<long long>(dur_iter));

    check(std::abs(sum_index - sum_iter) < 0.001f, "index and iterator sums match");

    // Keep sink alive to avoid aggressive optimization
    if (sink == 1234567.0f) {
        std::printf("sink=%f\n", sink);
    }
}

int main() {
    std::printf("\n=== Loop Style Tests ===\n");

    test_loop_styles();

    std::printf("\n=================================\n");
    std::printf("Tests: %d/%d passed\n", pass_count, test_count);
    std::printf("=================================\n\n");

    return (pass_count == test_count) ? 0 : 1;
}
