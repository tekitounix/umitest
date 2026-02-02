// SPDX-License-Identifier: MIT
// umimock ARM/Renode test — minimal test for cross-compilation verification
// Uses UART output via syscalls.cc _write()

#include <umimock/mock.hh>

#include <cstdio>

using namespace umi::mock;

static int passed = 0;
static int failed = 0;

static void check(bool cond, const char* msg) {
    if (cond) {
        passed++;
    } else {
        failed++;
        std::printf("FAIL: %s\n", msg);
    }
}

static void check_near(float actual, float expected, const char* msg, float eps = 0.001f) {
    float diff = actual - expected;
    if (diff < 0) diff = -diff;
    check(diff < eps, msg);
}

int main() {
    std::printf("[umimock ARM test]\n");

    // Constant signal
    MockSignal sig(Shape::CONSTANT, 0.5f);
    check_near(sig.generate(), 0.5f, "constant generate");

    // set_value with this-> disambiguation
    sig.set_value(0.75f);
    check_near(sig.get_value(), 0.75f, "set_value");

    // Reset
    sig.reset();
    check_near(sig.get_value(), default_value, "reset");

    // Ramp signal
    MockSignal ramp(Shape::RAMP, 1.0f);
    float first = ramp.generate();
    float second = ramp.generate();
    check(second > first, "ramp increases");

    // fill_buffer
    MockSignal constant(Shape::CONSTANT, 0.25f);
    float buf[4] = {};
    fill_buffer(constant, buf, 4);
    for (int i = 0; i < 4; ++i) {
        check_near(buf[i], 0.25f, "fill_buffer");
    }

    // constexpr
    constexpr MockSignal csig(Shape::CONSTANT, 1.0f);
    check(csig.get_value() == 1.0f, "constexpr");

    // Summary
    std::printf("=================================\n");
    std::printf("Tests: %d/%d passed\n", passed, passed + failed);
    std::printf("=================================\n");

    return failed > 0 ? 1 : 0;
}
