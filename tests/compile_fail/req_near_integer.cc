// SPDX-License-Identifier: MIT
#include <umitest/test.hh>

void test() {
    umi::test::Suite s("x");
    s.run("x", [](umi::test::TestContext& ctx) { ctx.require_near(1, 2); });
}
