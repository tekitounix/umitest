#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for exception check methods (throws, nothrow).
/// @author Shota Moriguchi @tekitounix

#include <stdexcept>

#include <umitest/reporters/null.hh>
#include <umitest/test.hh>

namespace umitest::test {

inline void run_exception_tests(umi::test::Suite& suite) {
    using umi::test::BasicSuite;
    using umi::test::NullReporter;

    suite.section("exception checks");

    // -- throws<E>(fn) --

    suite.run("throws_typed_correct", [](auto& t) {
        t.template throws<std::runtime_error>([] { throw std::runtime_error("boom"); });
        t.is_true(t.ok());
    });

    suite.run("throws_typed_wrong_type", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x",
                  [](auto& ctx) { ctx.template throws<std::runtime_error>([] { throw std::logic_error("wrong"); }); });
        t.eq(inner.summary(), 1); // failed
    });

    suite.run("throws_typed_no_throw", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.template throws<std::runtime_error>([] {}); });
        t.eq(inner.summary(), 1); // failed
    });

    // -- throws(fn) (any exception) --

    suite.run("throws_any_passes", [](auto& t) {
        t.throws([] { throw 42; });
        t.is_true(t.ok());
    });

    suite.run("throws_any_fails_no_throw", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.throws([] {}); });
        t.eq(inner.summary(), 1);
    });

    // -- nothrow(fn) --

    suite.run("nothrow_passes", [](auto& t) {
        t.nothrow([] {});
        t.is_true(t.ok());
    });

    suite.run("nothrow_fails_on_throw", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.nothrow([] { throw 42; }); });
        t.eq(inner.summary(), 1);
    });

    // -- require_throws<E>(fn) --

    suite.run("require_throws_typed_passes", [](auto& t) {
        const bool result = t.template require_throws<std::runtime_error>([] { throw std::runtime_error("ok"); });
        t.is_true(result);
    });

    suite.run("require_throws_typed_fails", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.template require_throws<std::runtime_error>([] {}); });
        t.eq(inner.summary(), 1);
    });

    // -- require_throws(fn) (any exception) --

    suite.run("require_throws_any_passes", [](auto& t) {
        const bool result = t.require_throws([] { throw 42; });
        t.is_true(result);
    });

    suite.run("require_throws_any_fails", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.require_throws([] {}); });
        t.eq(inner.summary(), 1);
    });

    // -- require_nothrow(fn) --

    suite.run("require_nothrow_passes", [](auto& t) {
        const bool result = t.require_nothrow([] {});
        t.is_true(result);
    });

    suite.run("require_nothrow_fails", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.require_nothrow([] { throw 42; }); });
        t.eq(inner.summary(), 1);
    });
}

} // namespace umitest::test
