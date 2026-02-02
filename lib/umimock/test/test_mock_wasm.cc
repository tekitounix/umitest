// SPDX-License-Identifier: MIT
// umimock WASM test — Emscripten build, exports C API for Node.js testing

#include <umimock/mock.hh>

#include <emscripten/emscripten.h>

using namespace umi::mock;

extern "C" {

EMSCRIPTEN_KEEPALIVE
float umimock_constant(float value) {
    MockSignal sig(Shape::CONSTANT, value);
    return sig.generate();
}

EMSCRIPTEN_KEEPALIVE
float umimock_ramp_first() {
    MockSignal sig(Shape::RAMP, 1.0f);
    return sig.generate();
}

EMSCRIPTEN_KEEPALIVE
float umimock_set_and_get(float value) {
    MockSignal sig;
    sig.set_value(value);
    return sig.get_value();
}

EMSCRIPTEN_KEEPALIVE
float umimock_reset_value() {
    MockSignal sig(Shape::CONSTANT, 0.99f);
    sig.reset();
    return sig.get_value();
}

EMSCRIPTEN_KEEPALIVE
int umimock_fill_buffer_check(float value, int size) {
    MockSignal sig(Shape::CONSTANT, value);
    float buf[64] = {};
    if (size > 64) size = 64;
    fill_buffer(sig, buf, size);
    for (int i = 0; i < size; ++i) {
        if (buf[i] != value) return 0;
    }
    return 1;
}

} // extern "C"

int main() {
    // Entry point required by Emscripten
    return 0;
}
