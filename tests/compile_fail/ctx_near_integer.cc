// SPDX-License-Identifier: MIT
#include <umitest/test.hh>

void test() {
    umi::test::Suite s("x");
    s.run("x", [](umi::test::TestContext& ctx) { ctx.near(1, 2); });
}
