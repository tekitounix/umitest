// SPDX-License-Identifier: MIT
#include <string>

#include <umitest/test.hh>

void test() {
    umi::test::Suite s("x");
    s.run("x", [](auto& ctx) {
        const char8_t* p = u8"a";
        ctx.require_eq(std::u8string{}, p);
    });
}
