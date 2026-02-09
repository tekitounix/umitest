// SPDX-License-Identifier: MIT
// compile-fail test: incomplete Platform must not satisfy umi::hal::Platform.

#include <umihal/concept/platform.hh>

struct IncompleteOutput {
    // Missing init() and putc() — must not satisfy OutputDevice.
};

struct IncompletePlatform {
    using Output = IncompleteOutput;
    static void init() {}
};

static_assert(umi::hal::Platform<IncompletePlatform>,
    "This should fail: IncompleteOutput does not satisfy OutputDevice");
