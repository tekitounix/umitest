# umitest Design

[日本語](ja/DESIGN.md)

## 1. Vision

`umitest` is a zero-macro, header-only test framework for C++23:

1. Test code is written as ordinary C++ functions returning `bool`.
2. No preprocessor macros — `std::source_location` replaces `__FILE__`/`__LINE__`.
3. No external build dependencies — include and use.
4. Works on host, WASM, and embedded targets without modification.
5. Output is human-readable colored terminal text suitable for CI logs.

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

Assertions do not throw. Test functions return `bool`.
TestContext tracks failure state internally via `mark_failed()`.

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
│   ├── test.hh          # Umbrella header
│   ├── suite.hh         # Suite class + TestContext impl
│   ├── context.hh       # TestContext declaration
│   └── format.hh        # format_value for diagnostic output
└── tests/
    ├── test_main.cc
    ├── test_fixture.hh
    ├── test_assertions.cc
    ├── test_format.cc
    ├── test_suite_workflow.cc
    ├── compile_fail/
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
    └── xmake.lua
```

Notes:

1. Public headers stay under `include/umitest/`.
2. Future matchers are opt-in via separate header — no bloat on minimal usage.
3. Suite and TestContext remain the only two user-facing types.

---

## 5. Programming Model

### 5.0 API Reference

Public entrypoint: `include/umitest/test.hh`

Core types:

- `umi::test::Suite` — test runner and statistics
- `umi::test::TestContext` — assertion context for structured tests
- `umi::test::format_value()` — stdio-free value formatter

Available assertions (`assert_*` on TestContext, `check_*` on Suite):

| Method | Checks |
|--------|--------|
| `assert_eq` / `check_eq` | `a == b` |
| `assert_ne` / `check_ne` | `a != b` |
| `assert_lt` / `check_lt` | `a < b` |
| `assert_le` / `check_le` | `a <= b` |
| `assert_gt` / `check_gt` | `a > b` |
| `assert_ge` / `check_ge` | `a >= b` |
| `assert_near` / `check_near` | `\|a - b\| < eps` |
| `assert_true` / `check` | boolean condition |

Headers:

- `include/umitest/suite.hh` — Suite class + TestContext impl
- `include/umitest/context.hh` — TestContext declaration
- `include/umitest/format.hh` — format_value for diagnostic output

### 5.1 Minimal Path

Required minimal flow:

1. Construct `Suite`.
2. Define test function taking `TestContext&` and returning `bool`.
3. Call `suite.run("name", fn)`.
4. Return `suite.summary()` from `main`.

### 5.2 Two Testing Styles

**Structured style** (with `TestContext`):

```cpp
bool test_foo(TestContext& t) {
    t.assert_eq(1 + 1, 2);
    return true;
}

Suite s("foo");
s.run("test_foo", test_foo);
```

**Inline style** (direct `check_*` on Suite):

```cpp
Suite s("bar");
s.section("arithmetic");
s.check_eq(1 + 1, 2);
s.check_ne(1, 2);
return s.summary();
```

### 5.3 Advanced Path

Advanced usage includes:

1. `section()` for logical grouping within output,
2. `check_near()` / `assert_near()` for floating-point comparison,
3. custom `format_value` specializations for user types,
4. multiple Suites in a single test binary for independent statistics.

---

## 6. Assertion Semantics

### 6.1 TestContext Assertions

All `assert_*` methods on TestContext:

1. Return `bool` — `true` if the assertion passed, `false` if it failed.
2. On failure, call `mark_failed()` to set the context failure flag.
3. On failure, print source location and compared values to stdout.
4. Do NOT throw, abort, or longjmp. Test execution continues.

### 6.2 Suite Inline Checks

All `check_*` methods on Suite:

1. Return `bool` — same semantics as assertions.
2. Directly increment the `passed` or `failed` counter.
3. No TestContext is involved; simpler for quick checks.

### 6.3 Value Formatting

`format_value<T>` converts values to human-readable strings for failure messages.
Supported types: integral, floating-point, bool, char, const char*, std::string_view, std::nullptr_t, and pointer types.

---

## 7. Output Model

### 7.1 Human-Readable Report

Terminal output with ANSI color codes:

1. Section headers in cyan.
2. Pass results in green (`OK`).
3. Fail results in red (`FAIL`) with source location.
4. Summary line with total pass/fail counts.

### 7.2 Exit Code Convention

`summary()` returns `0` if all tests passed, `1` if any failed.
This is compatible with CI pipelines and `xmake test`.

---

## 8. Test Strategy

1. umitest is self-testing: `tests/` use umitest itself to verify behavior.
2. Test files are split by concern: assertions, format, suite workflow.
3. All tests run on host via `xmake test`.
4. Tests focus on semantic correctness, not timing.
5. CI runs host tests on all supported platforms.

### 8.1 Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_assertions.cc`: all assert_* methods (eq, ne, lt, le, gt, ge, near, true)
- `tests/test_format.cc`: format_value for all supported types
- `tests/test_suite_workflow.cc`: Suite lifecycle, run(), check_*, summary()

### 8.2 Running Tests

```bash
xmake test                    # all targets
xmake test 'test_umitest/*'  # umitest only
```

### 8.3 Quality Gates

- All assertion tests pass on host
- Format value tests cover all supported types
- Suite workflow tests verify pass/fail counting and exit code semantics
- Self-testing: umitest uses itself — any framework regression is immediately visible

---

## 9. Example Strategy

Examples represent learning stages:

1. `minimal`: shortest complete test.
2. `assertions`: all assertion methods demonstrated.
3. `check_style`: sections and inline check style.

---

## 10. Near-Term Improvement Plan

1. Add composable matchers for string/container checks.
2. Add compile-fail test for read-only assertions if applicable.
3. Expand `format_value` for user-defined types via ADL customization point.
4. Add benchmarking integration example (umitest + umibench combined).

---

## 11. Design Principles

1. Zero macros — all functionality via regular C++ functions.
2. Header-only — include and use, no build step.
3. Embedded-safe — no heap, no exceptions, no RTTI.
4. Explicit failure location — `std::source_location` in every assertion.
5. Two styles, one framework — structured (TestContext) and inline (Suite checks) coexist.
