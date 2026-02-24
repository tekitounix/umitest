// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: check_near with non-numeric types must fail.
/// @details check_near uses static_cast<double>, which fails for non-numeric types.

#include <umitest/test.hh>

struct Opaque {
    int x;
};

/// @brief Compile-fail test entrypoint.
int main() {
    umi::test::Suite suite("fail");
    suite.check_near(Opaque{1}, Opaque{2});
    return 0;
}
