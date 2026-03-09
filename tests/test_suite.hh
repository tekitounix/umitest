#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for BasicSuite — counting, run()-only API, summary.
/// @author Shota Moriguchi @tekitounix

#include <umitest/reporters/null.hh>
#include <umitest/suite.hh>
#include <umitest/test.hh>

namespace umitest::test {

inline void run_suite_tests(umi::test::Suite& suite) {
    suite.section("BasicSuite");

    suite.run("pass counting", [](auto& t) {
        umi::test::BasicSuite<umi::test::NullReporter> inner("inner");
        inner.run("a", [](auto& ctx) { ctx.eq(1, 1); });
        inner.run("b", [](auto& ctx) { ctx.eq(2, 2); });
        t.eq(inner.summary(), 0);
    });

    suite.run("fail counting", [](auto& t) {
        umi::test::BasicSuite<umi::test::NullReporter> inner("inner");
        inner.run("a", [](auto& ctx) { ctx.eq(1, 2); });
        inner.run("b", [](auto& ctx) { ctx.eq(1, 1); });
        t.eq(inner.summary(), 1);
    });

    suite.run("empty test passes", [](auto& t) {
        umi::test::BasicSuite<umi::test::NullReporter> inner("inner");
        inner.run("empty", [](auto& /*ctx*/) {});
        t.eq(inner.summary(), 0);
    });

    suite.run("multiple failures in one test = one failed case", [](auto& t) {
        umi::test::BasicSuite<umi::test::NullReporter> inner("inner");
        inner.run("multi", [](auto& ctx) {
            ctx.eq(1, 2);
            ctx.eq(3, 4);
            ctx.eq(5, 6);
        });
        t.eq(inner.summary(), 1);
    });

    suite.run("section does not affect counting", [](auto& t) {
        umi::test::BasicSuite<umi::test::NullReporter> inner("inner");
        inner.section("group A");
        inner.run("a", [](auto& ctx) { ctx.eq(1, 1); });
        inner.section("group B");
        inner.run("b", [](auto& ctx) { ctx.eq(2, 2); });
        t.eq(inner.summary(), 0);
    });
}

} // namespace umitest::test
