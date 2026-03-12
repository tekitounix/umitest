#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for TestContext — soft/fatal checks, note stack, counting.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cstddef>
#include <string>
#include <utility>

#include <umitest/failure.hh>
#include <umitest/reporter.hh>
#include <umitest/reporters/null.hh>
#include <umitest/suite.hh>
#include <umitest/test.hh>

namespace umitest::test {
using umi::test::TestContext;
namespace detail_test {

/// @brief Recording reporter for meta-testing (§16).
class RecordingReporter {
  public:
    void section(const char* /*title*/) {}
    void test_begin(const char* /*name*/) {}
    void test_pass(const char* /*name*/) { pass_count++; }
    void test_fail(const char* /*name*/) { fail_count++; }

    void report_failure(const umi::test::FailureView& fv) {
        if (recorded < max_records) {
            auto& r = records[static_cast<std::size_t>(recorded++)];
            r.kind = fv.kind != nullptr ? fv.kind : "";
            r.lhs = fv.lhs != nullptr ? fv.lhs : "";
            r.rhs = fv.rhs != nullptr ? fv.rhs : "";
            r.is_fatal = fv.is_fatal;
            r.note_count = static_cast<int>(fv.notes.size());
            if (!fv.notes.empty()) {
                r.first_note = fv.notes[0];
            }
        }
    }

    void summary(const umi::test::SummaryView& /*sv*/) {}

    int pass_count = 0;
    int fail_count = 0;

    static constexpr int max_records = 16;
    struct Record {
        std::string kind;
        std::string lhs;
        std::string rhs;
        bool is_fatal = false;
        int note_count = 0;
        std::string first_note;
    };
    std::array<Record, max_records> records{};
    int recorded = 0;
};

static_assert(umi::test::ReporterLike<RecordingReporter>);

} // namespace detail_test

inline void run_context_tests(umi::test::Suite& suite) {
    using detail_test::RecordingReporter;

    suite.section("TestContext");

    suite.run("soft check pass increments checked", [](TestContext& t) {
        umi::test::BasicSuite<umi::test::NullReporter> inner("inner");
        inner.run("x", [](TestContext& ctx) {
            ctx.eq(1, 1);
            ctx.eq(2, 2);
            ctx.is_true(true);
        });
        // All 3 checks passed → inner should report 1 pass, 0 fail
        t.eq(inner.summary(), 0);
    });

    suite.run("soft check fail records failure", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("deliberately fail", [](TestContext& ctx) { ctx.eq(1, 2); });
        const auto& r = inner.get_reporter();
        t.eq(r.fail_count, 1);
        t.eq(r.recorded, 1);
        t.eq(r.records[0].kind, std::string("eq"));
    });

    suite.run("soft check fail continues", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("multi fail", [](TestContext& ctx) {
            ctx.eq(1, 2);
            ctx.eq(3, 4);
        });
        const auto& r = inner.get_reporter();
        t.eq(r.fail_count, 1);
        t.eq(r.recorded, 2);
    });

    suite.run("fatal check sets is_fatal", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("fatal", [](TestContext& ctx) {
            if (!ctx.require_eq(1, 2)) {
                return;
            }
        });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.is_true(r.records[0].is_fatal);
    });

    suite.run("note appears in failure", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("with note", [](TestContext& ctx) {
            auto guard = ctx.note("parsing header");
            ctx.eq(1, 2);
        });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.eq(r.records[0].note_count, 1);
        t.eq(r.records[0].first_note, std::string("parsing header"));
    });

    suite.run("note pops on scope exit", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("note scope", [](TestContext& ctx) {
            {
                auto guard = ctx.note("inner scope");
            }
            ctx.eq(1, 2);
        });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.eq(r.records[0].note_count, 0);
    });

    suite.run("note nullptr becomes (null)", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("null note", [](TestContext& ctx) {
            auto guard = ctx.note(nullptr);
            ctx.eq(1, 2);
        });
        const auto& r = inner.get_reporter();
        t.eq(r.records[0].first_note, std::string("(null)"));
    });

    suite.run("check_near failure reports extra", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("near fail", [](TestContext& ctx) { ctx.near(1.0, 2.0); });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.eq(r.records[0].kind, std::string("near"));
    });

    suite.run("is_true / is_false", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("bool checks", [](TestContext& ctx) {
            ctx.is_true(false);
            ctx.is_false(true);
        });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 2);
        t.eq(r.records[0].kind, std::string("true"));
        t.eq(r.records[1].kind, std::string("false"));
    });

    suite.run("ordering checks", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("lt/le/gt/ge", [](TestContext& ctx) {
            ctx.lt(2, 1);
            ctx.le(2, 1);
            ctx.gt(1, 2);
            ctx.ge(1, 2);
        });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 4);
        t.eq(r.records[0].kind, std::string("lt"));
        t.eq(r.records[1].kind, std::string("le"));
        t.eq(r.records[2].kind, std::string("gt"));
        t.eq(r.records[3].kind, std::string("ge"));
    });

    suite.run("require_true pass", [](TestContext& t) {
        const bool result = t.require_true(true);
        t.is_true(result);
    });

    suite.run("require_true fail", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("x", [](TestContext& ctx) { (void)ctx.require_true(false); });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.is_true(r.records[0].is_fatal);
        t.eq(r.records[0].kind, std::string("true"));
    });

    suite.run("require_false pass", [](TestContext& t) {
        const bool result = t.require_false(false);
        t.is_true(result);
    });

    suite.run("require_false fail", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("x", [](TestContext& ctx) { (void)ctx.require_false(true); });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.is_true(r.records[0].is_fatal);
        t.eq(r.records[0].kind, std::string("false"));
    });

    suite.run("require_ne pass", [](TestContext& t) {
        const bool result = t.require_ne(1, 2);
        t.is_true(result);
    });

    suite.run("require_ne fail", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("x", [](TestContext& ctx) { (void)ctx.require_ne(1, 1); });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.is_true(r.records[0].is_fatal);
        t.eq(r.records[0].kind, std::string("ne"));
    });

    suite.run("require_le pass", [](TestContext& t) {
        const bool result = t.require_le(1, 2);
        t.is_true(result);
    });

    suite.run("require_le fail", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("x", [](TestContext& ctx) { (void)ctx.require_le(2, 1); });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.is_true(r.records[0].is_fatal);
        t.eq(r.records[0].kind, std::string("le"));
    });

    suite.run("require_gt pass", [](TestContext& t) {
        const bool result = t.require_gt(2, 1);
        t.is_true(result);
    });

    suite.run("require_gt fail", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("x", [](TestContext& ctx) { (void)ctx.require_gt(1, 2); });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.is_true(r.records[0].is_fatal);
        t.eq(r.records[0].kind, std::string("gt"));
    });

    suite.run("require_ge pass", [](TestContext& t) {
        const bool result = t.require_ge(1, 1);
        t.is_true(result);
    });

    suite.run("require_ge fail", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("x", [](TestContext& ctx) { (void)ctx.require_ge(1, 2); });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.is_true(r.records[0].is_fatal);
        t.eq(r.records[0].kind, std::string("ge"));
    });

    suite.run("require_near pass", [](TestContext& t) {
        const bool result = t.require_near(1.0, 1.0005);
        t.is_true(result);
    });

    suite.run("require_near fail", [](TestContext& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("x", [](TestContext& ctx) { (void)ctx.require_near(1.0, 2.0); });
        const auto& r = inner.get_reporter();
        t.eq(r.recorded, 1);
        t.is_true(r.records[0].is_fatal);
        t.eq(r.records[0].kind, std::string("near"));
    });
}

} // namespace umitest::test
