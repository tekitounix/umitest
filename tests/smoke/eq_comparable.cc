// SPDX-License-Identifier: MIT
#include <umitest/check.hh>
#include <umitest/test.hh>
static_assert(umi::test::check_eq(1, 1));
void test() {
    umi::test::Suite s("x");
    s.run("x", [](auto& ctx) {
        ctx.eq(1, 1);
        ctx.require_eq(1, 1);
    });
}
