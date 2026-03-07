# umitest

[日本語](docs/ja/README.md)

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

```bash
xmake test
```

## Public API

- Entrypoint: `include/umitest/test.hh`
- Core types: `Suite` (`BasicSuite<StdioReporter>`), `TestContext`, `format_value()`

### TestContext Checks

| Soft check | Fatal check | Checks |
|------------|-------------|--------|
| `eq(a, b)` | `require_eq(a, b)` | `a == b` |
| `ne(a, b)` | `require_ne(a, b)` | `a != b` |
| `lt(a, b)` | `require_lt(a, b)` | `a < b` |
| `le(a, b)` | `require_le(a, b)` | `a <= b` |
| `gt(a, b)` | `require_gt(a, b)` | `a > b` |
| `ge(a, b)` | `require_ge(a, b)` | `a >= b` |
| `near(a, b)` | `require_near(a, b)` | `|a - b| < eps` |
| `is_true(c)` | `require_true(c)` | boolean true |
| `is_false(c)` | `require_false(c)` | boolean false |

Soft checks record failure and continue. Fatal checks (`require_*`) return `false` on failure for early-return patterns:

```cpp
if (!t.require_true(ptr != nullptr)) return;
t.eq(ptr->value, 42);
```

### Free Functions (check.hh)

Pure `constexpr bool` functions for use in `static_assert` or custom logic:

`check_eq`, `check_ne`, `check_lt`, `check_le`, `check_gt`, `check_ge`, `check_near`, `check_true`, `check_false`

## Examples

- [`examples/minimal.cc`](examples/minimal.cc) — shortest complete test
- [`examples/assertions.cc`](examples/assertions.cc) — all assertion methods
- [`examples/check_style.cc`](examples/check_style.cc) — sections and structured tests

## Documentation

- [Design & API](docs/DESIGN.md)
- [Common Guides](../docs/INDEX.md)
- API docs: `doxygen Doxyfile` → `build/doxygen/html/index.html`

## License

MIT — See [LICENSE](../../LICENSE)
