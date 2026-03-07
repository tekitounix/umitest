// SPDX-License-Identifier: MIT
#include <cstddef>

#include <umitest/check.hh>

void test() {
    const char8_t* p = nullptr;
    umi::test::check_eq(p, nullptr);
}
