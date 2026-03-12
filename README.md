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
    Suite s("example");
    s.run("add", [](auto& t) {
        t.eq(1 + 1, 2);
        t.is_true(true);
    });
    return s.summary();
}
```

## Build and Test

From the project root:

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

## Design Decisions

### Non-Negotiable Requirements

1. **No macros** — `std::source_location::current()` as default argument. No `ASSERT_EQ` or `TEST_CASE`.
2. **Header-only** — no static libraries, no link-time registration, no code generation.
3. **No heap allocation** — all internal state uses stack or static storage. Bare-metal compatible.
4. **No exceptions** — assertions do not throw. TestContext tracks failure state internally.

### Dependency Boundaries

Layering is strict:

1. `umitest` depends only on C++23 standard library headers.
2. No dependency on other umi libraries.
3. Other umi libraries depend on `umitest` for testing (test-time only).

### Usage

umitest is a **library**, not a project. It has no `set_project()` and no standalone build.

In the UMI monorepo:

```lua
add_deps("umitest")
```

External projects:

```lua
add_requires("umitest")
add_packages("umitest")
```

## Examples

- [`examples/minimal.cc`](examples/minimal.cc) — shortest complete test
- [`examples/assertions.cc`](examples/assertions.cc) — all assertion methods
- [`examples/check_style.cc`](examples/check_style.cc) — sections and structured tests
- [`examples/constexpr.cc`](examples/constexpr.cc) — compile-time checks with `static_assert`

## License

MIT — See [LICENSE](../../LICENSE)
