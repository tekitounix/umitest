// SPDX-License-Identifier: MIT
#include <umitest/check.hh>
#include <umitest/test.hh>
void test() {
    umi::test::check_near(1.0, 1.0);
    umi::test::Suite s("x");
    s.run("x", [](auto& ctx) {
        ctx.near(1.0, 1.0);
        ctx.require_near(1.0, 1.0);
    });
}
