# umitest

[日本語](docs/ja/README.md)

A zero-macro, header-only test framework for C++23.
Write test functions as ordinary C++ code with automatic source location capture via `std::source_location`.

## Why umitest

- Zero macros — all assertions are regular function calls
- Header-only — `#include <umitest/test.hh>` and use
- Embedded-ready — no exceptions, no heap, no RTTI
- Two testing styles — structured (`TestContext`) and inline (`Suite::check_*`)
- Self-testing — umitest tests itself, so framework regressions are immediately visible

## Quick Start

```cpp
#include <umitest/test.hh>
using namespace umi::test;

bool test_add(TestContext& t) {
    t.assert_eq(1 + 1, 2);
    t.assert_true(true);
    return true;
}

int main() {
    Suite s("example");
    s.run("add", test_add);
    return s.summary();
}
```

## Build and Test

```bash
xmake test
```

## Public API

- Entrypoint: `include/umitest/test.hh`
- Core types: `Suite`, `TestContext`, `format_value()`
- Assertions: `assert_eq`, `assert_ne`, `assert_lt`, `assert_le`, `assert_gt`, `assert_ge`, `assert_near`, `assert_true`

## Examples

- [`examples/minimal.cc`](examples/minimal.cc) — shortest complete test
- [`examples/assertions.cc`](examples/assertions.cc) — all assertion methods
- [`examples/check_style.cc`](examples/check_style.cc) — sections and inline checks

## Documentation

- [Design & API](docs/DESIGN.md)
- [Common Guides](../docs/INDEX.md)
- API docs: `doxygen Doxyfile` → `build/doxygen/html/index.html`

## License

MIT — See [LICENSE](../../LICENSE)
