// SPDX-License-Identifier: MIT
#include <umitest/test.hh>

void test() {
    umi::test::Suite s("x");
    s.run("x", [](auto& ctx) {
        const char8_t* p = nullptr;
        ctx.eq(p, nullptr);
    });
}
