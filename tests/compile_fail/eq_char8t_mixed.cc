// SPDX-License-Identifier: MIT
#include <string>

#include <umitest/check.hh>

void test() {
    const char8_t* p = u8"a";
    umi::test::check_eq(std::u8string{}, p);
}
