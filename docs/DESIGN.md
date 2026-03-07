# umitest Design

[日本語](ja/DESIGN.md)

## 1. Vision

`umitest` is a zero-macro, header-only test framework for C++23:

1. Test code is written as void lambdas passed to `suite.run()`.
2. No preprocessor macros — `std::source_location` replaces `__FILE__`/`__LINE__`.
3. No external build dependencies — include and use.
4. Works on host, WASM, and embedded targets without modification.
5. Output is human-readable colored terminal text suitable for CI logs.
6. Reporter-parameterized — `BasicSuite<R>` separates test logic from output format.

---

## 2. Non-Negotiable Requirements

### 2.1 No Macros

Every assertion is a regular function call. There is no `ASSERT_EQ` or `TEST_CASE` macro.
Source location is captured via default arguments using `std::source_location::current()`.

### 2.2 Header-Only

The entire framework consists of header files under `include/umitest/`.
No static libraries, no link-time registration, no code generation.

### 2.3 No Heap Allocation

All internal state uses stack or static storage.
This ensures compatibility with bare-metal environments where dynamic allocation may be unavailable.

### 2.4 No Exceptions

Assertions do not throw. TestContext tracks failure state internally via `mark_failed()`.
Lambdas passed to `run()` must return `void`.

### 2.5 Dependency Boundaries

Layering is strict:

1. `umitest` depends only on C++23 standard library headers.
2. No dependency on other umi libraries.
3. Other umi libraries depend on `umitest` for testing (test-time only).

Reference dependency graph:

```text
umibench/tests -> umitest
umimmio/tests  -> umitest
umirtm/tests   -> umitest
umitest/tests  -> umitest (self-test)
```

---

## 3. Current Layout

```text
lib/umitest/
├── README.md
├── xmake.lua
├── docs/
│   ├── INDEX.md
│   ├── DESIGN.md
│   ├── TESTING.md
│   └── ja/
├── examples/
│   ├── minimal.cc
│   ├── assertions.cc
│   └── check_style.cc
├── include/umitest/
│   ├── test.hh          # Umbrella header (Suite = BasicSuite<StdioReporter>)
│   ├── suite.hh         # BasicSuite<R> template
│   ├── context.hh       # TestContext (soft + fatal checks)
│   ├── check.hh         # constexpr bool free functions
│   ├── format.hh        # format_value for diagnostic output
│   ├── failure.hh       # FailureView struct
│   ├── reporter.hh      # ReporterLike concept
│   └── reporters/
│       ├── stdio.hh     # StdioReporter (ANSI color)
│       ├── plain.hh     # PlainReporter (no color)
│       └── null.hh      # NullReporter (silent)
└── tests/
    ├── test_main.cc
    ├── test_fixture.hh
    ├── test_check.cc
    ├── test_context.cc
    ├── test_suite.cc
    ├── test_reporter.cc
    ├── test_format.cc
    ├── smoke/           # Baseline compilation tests
    ├── compile_fail/    # Constraint violation tests
    └── xmake.lua
```

---

## 4. Growth Layout

```text
lib/umitest/
├── include/umitest/
│   ├── test.hh
│   ├── suite.hh
│   ├── context.hh
│   ├── check.hh
│   ├── format.hh
│   └── matchers.hh       # Future: composable matchers (contains, starts_with)
├── examples/
│   ├── minimal.cc
│   ├── assertions.cc
│   ├── check_style.cc
│   └── matchers.cc        # Future: matcher usage demo
└── tests/
    ├── test_main.cc
    ├── test_*.cc
    ├── smoke/
    ├── compile_fail/
    └── xmake.lua
```

Notes:

1. Public headers stay under `include/umitest/`.
2. Future matchers are opt-in via separate header — no bloat on minimal usage.
3. `BasicSuite<R>` and `TestContext` remain the only two user-facing types.

---

## 5. Programming Model

### 5.0 API Reference

Public entrypoint: `include/umitest/test.hh`

Core types:

- `umi::test::Suite` — `BasicSuite<StdioReporter>`, default test runner
- `umi::test::PlainSuite` — `BasicSuite<PlainReporter>`, no ANSI color
- `umi::test::TestContext` — assertion context passed to test lambdas
- `umi::test::format_value()` — stdio-free value formatter

TestContext checks (soft checks continue on failure, fatal checks return `false`):

| Soft check | Fatal check | Checks |
|------------|-------------|--------|
| `eq(a, b)` | `require_eq(a, b)` | `a == b` |
| `ne(a, b)` | `require_ne(a, b)` | `a != b` |
| `lt(a, b)` | `require_lt(a, b)` | `a < b` |
| `le(a, b)` | `require_le(a, b)` | `a <= b` |
| `gt(a, b)` | `require_gt(a, b)` | `a > b` |
| `ge(a, b)` | `require_ge(a, b)` | `a >= b` |
| `near(a, b, eps)` | `require_near(a, b, eps)` | `|a - b| < eps` |
| `is_true(c)` | `require_true(c)` | boolean true |
| `is_false(c)` | `require_false(c)` | boolean false |

Free functions (`check.hh`): `check_eq`, `check_ne`, `check_lt`, `check_le`, `check_gt`, `check_ge`, `check_near`, `check_true`, `check_false` — pure `constexpr bool` for `static_assert` or custom logic.

Headers:

- `include/umitest/test.hh` — umbrella (includes suite + reporters)
- `include/umitest/suite.hh` — `BasicSuite<R>` template
- `include/umitest/context.hh` — TestContext with soft + fatal checks
- `include/umitest/check.hh` — constexpr free functions
- `include/umitest/format.hh` — format_value for diagnostic output
- `include/umitest/reporter.hh` — ReporterLike concept
- `include/umitest/failure.hh` — FailureView struct

### 5.1 Minimal Path

Required minimal flow:

1. Construct `Suite`.
2. Call `suite.run("name", [](auto& t) { ... })` with a void lambda.
3. Return `suite.summary()` from `main`.

### 5.2 Testing Style

Tests are written as void lambdas passed to `suite.run()`:

```cpp
Suite s("foo");
s.run("test_foo", [](auto& t) {
    t.eq(1 + 1, 2);
    t.is_true(true);
});
```

Fatal checks enable early-return guard patterns:

```cpp
s.run("test_bar", [](auto& t) {
    if (!t.require_true(ptr != nullptr)) return;
    t.eq(ptr->value, 42);
});
```

### 5.3 Advanced Path

Advanced usage includes:

1. `section()` for logical grouping within output,
2. `near()` / `require_near()` for floating-point comparison,
3. `note()` for contextual annotations (RAII scoped),
4. custom `format_value` specializations for user types,
5. multiple Suites in a single test binary for independent statistics,
6. custom reporters via `BasicSuite<MyReporter>`.

---

## 6. Assertion Semantics

### 6.1 Soft Checks

All soft check methods on TestContext (`eq`, `ne`, `lt`, `le`, `gt`, `ge`, `near`, `is_true`, `is_false`):

1. Return `bool` — `true` if the check passed, `false` if it failed.
2. On failure, call `mark_failed()` to set the context failure flag.
3. On failure, report source location and compared values via the reporter.
4. Do NOT throw, abort, or longjmp. Test execution continues.

### 6.2 Fatal Checks

All `require_*` methods on TestContext:

1. Return `bool` — same as soft checks.
2. On failure, additionally mark the context as fatally failed.
3. Intended for guard patterns: `if (!t.require_true(precond)) return;`

### 6.3 Value Formatting

`format_value<T>` converts values to human-readable strings for failure messages.
Supported types: integral, floating-point, bool, char, const char*, std::string_view, std::nullptr_t, and pointer types.

---

## 7. Output Model

### 7.1 Human-Readable Report

Terminal output with ANSI color codes (StdioReporter):

1. Section headers in cyan.
2. Pass results in green (`OK`).
3. Fail results in red (`FAIL`) with source location.
4. Summary line with total pass/fail counts.

### 7.2 Exit Code Convention

`summary()` returns `0` if all tests passed, `1` if any failed.
This is compatible with CI pipelines and `xmake test`.

### 7.3 Reporter Architecture

`BasicSuite<R>` is parameterized by a reporter satisfying `ReporterLike`:

- `StdioReporter` — ANSI colored stdout output (default via `Suite`)
- `PlainReporter` — plain text without escape codes
- `NullReporter` — silent, for testing the framework itself

---

## 8. Test Strategy

1. umitest is self-testing: `tests/` use umitest itself to verify behavior.
2. Test files are split by concern: check functions, context, suite, reporter, format.
3. All tests run on host via `xmake test`.
4. Tests focus on semantic correctness, not timing.
5. CI runs host tests on all supported platforms.

### 8.1 Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_check.cc`: constexpr free functions (check_eq, check_lt, check_near, etc.)
- `tests/test_context.cc`: TestContext soft + fatal checks
- `tests/test_suite.cc`: BasicSuite lifecycle, run(), section(), summary()
- `tests/test_reporter.cc`: reporter output verification
- `tests/test_format.cc`: format_value for all supported types
- `tests/smoke/`: baseline compilation tests (4 files)
- `tests/compile_fail/`: constraint violation tests (20 files)

### 8.2 Running Tests

```bash
xmake test                    # all targets
xmake test 'test_umitest/*'  # umitest only
```

### 8.3 Quality Gates

- All self-tests pass on host
- Smoke baseline files compile successfully
- Compile-fail tests reject invalid code at compile time
- Format value tests cover all supported types
- Self-testing: umitest uses itself — any framework regression is immediately visible

---

## 9. Example Strategy

Examples represent learning stages:

1. `minimal`: shortest complete test.
2. `assertions`: all assertion methods demonstrated.
3. `check_style`: sections and structured test patterns.

---

## 10. Near-Term Improvement Plan

1. Add composable matchers for string/container checks.
2. Expand `format_value` for user-defined types via ADL customization point.
3. Add benchmarking integration example (umitest + umibench combined).

---

## 11. Design Principles

1. Zero macros — all functionality via regular C++ functions.
2. Header-only — include and use, no build step.
3. Embedded-safe — no heap, no exceptions, no RTTI.
4. Explicit failure location — `std::source_location` in every assertion.
5. Reporter-parameterized — output format is decoupled from test logic.
6. Compile-time contracts — type constraints prevent misuse at build time.
