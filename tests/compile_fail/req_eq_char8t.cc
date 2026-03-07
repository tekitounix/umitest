// SPDX-License-Identifier: MIT
#include <umitest/test.hh>

void test() {
    umi::test::Suite s("x");
    s.run("x", [](auto& ctx) { ctx.require_eq(u8"a", u8"b"); });
}
