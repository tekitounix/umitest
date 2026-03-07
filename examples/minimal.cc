// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Minimal test example — one suite, one test.
/// @author Shota Moriguchi @tekitounix

#include <umitest/test.hh>

int main() {
    umi::test::Suite suite("minimal");

    suite.run("one plus one equals two", [](auto& t) { t.eq(1 + 1, 2); });

    return suite.summary();
}
