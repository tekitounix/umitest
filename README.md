# umitest

English | [日本語](docs/readme.ja.md)

A zero-macro, header-only test framework for C++23.
Write test functions as ordinary C++ code with automatic source location capture via `std::source_location`.

## Why umitest

- Zero macros — all assertions are regular function calls
- Header-only — `#include <umitest/test.hh>` and use
- Embedded-ready — no exceptions, no heap, no RTTI
- Reporter-parameterized — `BasicSuite<R>` separates output from logic
- Compile-time contract enforcement — type constraints reject invalid comparisons at build time
- Self-testing — umitest tests itself, so framework regressions are immediately visible

## Quick Start

```cpp
#include <umitest/test.hh>
using namespace umi::test;

int main() {
    Suite suite("example");
    suite.run("add", [](auto& t) {
        t.eq(1 + 1, 2);
        t.is_true(true);
    });
    return suite.summary();
}
```

## Installation

External projects:

```lua
add_repositories("synthernet https://github.com/tekitounix/synthernet-xmake-repo.git main")
add_requires("umitest")
add_packages("umitest")
```

## Build and Test

```bash
xmake test 'test_umitest/*'
```

## Public API

Entrypoint: `include/umitest/test.hh`

Core types: `Suite` (`BasicSuite<StdioReporter>`), `TestContext`, `format_value()`

### TestContext Checks

| Soft check | Fatal check | Checks |
|------------|-------------|--------|
| `eq(a, b)` | `require_eq(a, b)` | `a == b` |
| `ne(a, b)` | `require_ne(a, b)` | `a != b` |
| `lt(a, b)` | `require_lt(a, b)` | `a < b` |
| `le(a, b)` | `require_le(a, b)` | `a <= b` |
| `gt(a, b)` | `require_gt(a, b)` | `a > b` |
| `ge(a, b)` | `require_ge(a, b)` | `a >= b` |
| `near(a, b, eps)` | `require_near(a, b, eps)` | `|a - b| <= eps` |
| `is_true(c)` | `require_true(c)` | boolean true |
| `is_false(c)` | `require_false(c)` | boolean false |

#### Exception Checks

Available only when exceptions are enabled (`-fno-exceptions` excludes these methods).

| Soft check | Fatal check | Checks |
|------------|-------------|--------|
| `throws<E>(fn)` | `require_throws<E>(fn)` | `fn()` throws `E` |
| `throws(fn)` | `require_throws(fn)` | `fn()` throws any exception |
| `nothrow(fn)` | `require_nothrow(fn)` | `fn()` does not throw |

Note: in generic lambdas (`auto& t`), use `t.template throws<E>(fn)`.

#### String Checks

| Soft check | Fatal check | Checks |
|------------|-------------|--------|
| `str_contains(s, sub)` | `require_str_contains(s, sub)` | `s` contains `sub` |
| `str_starts_with(s, pre)` | `require_str_starts_with(s, pre)` | `s` starts with `pre` |
| `str_ends_with(s, suf)` | `require_str_ends_with(s, suf)` | `s` ends with `suf` |

Soft checks record failure and continue. Fatal checks (`require_*`) return `false` on failure for early-return patterns:

```cpp
if (!t.require_true(ptr != nullptr)) return;
t.eq(ptr->value, 42);
```

#### Context Notes

`note(msg)` pushes a context string onto an RAII-guarded stack. On failure, active notes appear in diagnostic output:

```cpp
auto guard = t.note("processing header");
t.eq(header.version, 2);
```

### Free Functions (check.hh)

`constexpr bool` functions for use in `static_assert` or custom logic:

`check_eq`, `check_ne`, `check_lt`, `check_le`, `check_gt`, `check_ge`, `check_true`, `check_false`,
`check_str_contains`, `check_str_starts_with`, `check_str_ends_with`

`check_near` is the exception — it is not `constexpr` because `std::abs` and `std::isnan` are not `constexpr` in the current standard.

### Reporters

`BasicSuite<R>` is parameterized by a reporter satisfying `ReporterLike`:

- `StdioReporter` — ANSI colored stdout output (default via `Suite`)
- `PlainReporter` — plain text without escape codes
- `NullReporter` — silent, for testing the framework itself

## Writing Tests

### Test File Structure

```
lib/<name>/tests/
  xmake.lua           -- test target definition
  test_main.cc         -- single compilation unit (includes all .hh)
  test_<feature>.hh    -- per-feature header-only test (inline run_<feature>_tests)
  compile_fail/        -- compile failure tests (type constraint verification)
    <name>.cc
  smoke/               -- single-header compilation tests
    <name>.cc
```

### xmake Test Target Definition

```lua
target("test_<mylib>")
    set_kind("binary")
    set_default(false)
    add_files("test_*.cc")
    add_deps("<mylib>", "umitest")
    add_tests("default")
target_end()
```

With compile_fail tests:

```lua
    for _, f in ipairs(os.files(path.join(os.scriptdir(), "compile_fail", "*.cc"))) do
        add_tests("fail_" .. path.basename(f),
            {files = path.join("compile_fail", path.filename(f)), build_should_fail = true})
    end
```

With smoke tests:

```lua
    for _, f in ipairs(os.files(path.join(os.scriptdir(), "smoke", "*.cc"))) do
        add_tests("smoke_" .. path.basename(f),
            {files = path.join("smoke", path.filename(f)), build_should_pass = true})
    end
```

### Entry Point

```cpp
#include "test_feature.hh"

int main() {
    umi::test::Suite suite("test_<mylib>");
    <mylib>::test::run_feature_tests(suite);
    return suite.summary();
}
```

### Test Functions (header-only)

```cpp
#pragma once
#include <umitest/test.hh>

namespace <mylib>::test {

inline void run_feature_tests(umi::test::Suite& suite) {
    suite.section("feature");

    suite.run("basic", [](auto& t) {
        t.eq(actual, expected);
        if (!t.require_eq(actual, expected)) return;
    });
}

} // namespace <mylib>::test
```

### Soft vs Fatal Checks

- **soft** (`eq`, `lt`, `is_true`, etc.) — independent assertions. On failure, remaining checks still execute
- **fatal** (`require_eq`, `require_true`, etc.) — preconditions. Returns `false` on failure for `if (!...) return;`

```cpp
suite.run("parse result", [](auto& t) {
    auto result = parse(input);
    if (!t.require_true(result.has_value())) return;
    t.eq(result->name, "test");
    t.gt(result->size, 0);
});
```

### Test Naming

Test names should state what is being verified. Describe behavior, not implementation details:

```cpp
// Good: behavior is clear
suite.run("near rejects NaN", ...);
suite.run("require_eq returns false on mismatch", ...);

// Bad: implementation detail or vague
suite.run("test1", ...);
suite.run("check function", ...);
```

### compile_fail Tests

Write code that **should fail to compile**. Verifies that type constraints correctly reject invalid usage.

```cpp
// compile_fail/eq_incomparable.cc
#include <umitest/check.hh>

struct A {};
struct B {};

void should_fail() {
    umi::test::check_eq(A{}, B{});  // A and B are not comparable → compile error
}
```

### Naming Conventions

- Test files: `test_<feature>.hh` (header-only, included from test_main.cc)
- compile_fail: `compile_fail/<constraint_name>.cc`
- smoke: `smoke/<header_name>.cc`
- Test names: `test_<lib>/default`, `test_<lib>/fail_<name>`, `test_<lib>/smoke_<name>`

### Adding New Tests

1. Create `tests/test_<feature>.hh` — include from `test_main.cc` and add a call in `main()`
2. compile-fail: add `.cc` under `tests/compile_fail/` — auto-discovered by glob
3. smoke: add `.cc` under `tests/smoke/` — auto-discovered by glob

No xmake.lua edit needed.

### Running Tests

```bash
xmake test                              # all tests
xmake test 'test_<mylib>/*'            # specific library
xmake test 'test_<mylib>/default'      # self-test only
xmake test 'test_<mylib>/fail_*'       # compile_fail only
```

## Examples

- [`examples/minimal.cc`](examples/minimal.cc) — shortest complete test
- [`examples/assertions.cc`](examples/assertions.cc) — all assertion methods
- [`examples/check_style.cc`](examples/check_style.cc) — sections and structured tests
- [`examples/constexpr.cc`](examples/constexpr.cc) — compile-time checks with `static_assert`

## License

MIT — See [LICENSE](LICENSE)
