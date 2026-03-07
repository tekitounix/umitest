// SPDX-License-Identifier: MIT
#include <umitest/check.hh>

struct S {};

void test() {
    S a, b;
    umi::test::check_lt(a, b);
}
