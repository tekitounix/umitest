// SPDX-License-Identifier: MIT
#include <umitest/check.hh>
#include <umitest/test.hh>
static_assert(umi::test::check_lt(1, 2));
void test() {
    umi::test::Suite s("x");
    s.run("x", [](auto& ctx) {
        ctx.lt(1, 2);
        ctx.require_lt(1, 2);
    });
}
