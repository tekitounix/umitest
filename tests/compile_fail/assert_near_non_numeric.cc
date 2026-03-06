// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: assert_near with non-numeric types must fail.
/// @author Shota Moriguchi @tekitounix
/// @details assert_near requires std::is_arithmetic_v for both types.

#include <umitest/suite.hh>

struct Opaque {
    int x;
};

/// @brief Compile-fail test entrypoint.
int main() {
    umi::test::TestContext ctx;
    ctx.assert_near(Opaque{1}, Opaque{2});
    return 0;
}
