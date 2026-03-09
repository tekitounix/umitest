# Test Writing Guide

How to write tests with umitest and define test targets in xmake.

## xmake Test Target Definition

### Minimal Pattern

```lua
target("test_<mylib>")
    set_kind("binary")
    set_default(false)
    add_files("test_*.cc")
    add_deps("<mylib>", "umitest")
    add_tests("default")
target_end()
```

- `set_kind("binary")` — tests are executable binaries
- `set_default(false)` — exclude from default build targets
- `add_files("test_*.cc")` — collect test files via glob
- `add_deps` — only the library under test + umitest. No unnecessary dependencies
- `add_tests("default")` — register with xmake test discovery
- `target_end()` — explicit target scope termination

### With compile_fail Tests

```lua
    for _, f in ipairs(os.files(path.join(os.scriptdir(), "compile_fail", "*.cc"))) do
        add_tests("fail_" .. path.basename(f),
            {files = path.join("compile_fail", path.filename(f)), build_should_fail = true})
    end
```

`{files = ..., build_should_fail = true}` compiles independently from the main binary.
It does not affect the main target build (standard xmake behavior).

### With smoke Tests

```lua
    for _, f in ipairs(os.files(path.join(os.scriptdir(), "smoke", "*.cc"))) do
        add_tests("smoke_" .. path.basename(f),
            {files = path.join("smoke", path.filename(f)), build_should_pass = true})
    end
```

For verifying that each header compiles on its own. `build_should_pass = true` compiles independently.

## Test File Structure

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

### Naming Conventions

- Test files: `test_<feature>.hh` (header-only, included from test_main.cc)
- compile_fail: `compile_fail/<constraint_name>.cc`
- smoke: `smoke/<header_name>.cc`
- Test names: `test_<lib>/default`, `test_<lib>/fail_<name>`, `test_<lib>/smoke_<name>`

## Writing C++ Test Code

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
        // soft check (continues on failure)
        t.eq(actual, expected);
        t.lt(a, b);
        t.near(actual, expected, tolerance);
        t.is_true(condition);

        // fatal check (return false on failure for early-return)
        if (!t.require_eq(actual, expected)) return;
    });
}

} // namespace <mylib>::test
```

### Soft vs Fatal Checks

Every check has a soft and fatal (`require_*`) variant. The principle:

- **soft** (`eq`, `lt`, `is_true`, etc.) — use for independent assertions. On failure, remaining checks still execute so you see all problems in one run
- **fatal** (`require_eq`, `require_true`, etc.) — use for preconditions. Returns `false` on failure for `if (!...) return;` early-return

```cpp
suite.run("parse result", [](auto& t) {
    auto result = parse(input);
    // result invalid → subsequent checks are meaningless → fatal
    if (!t.require_true(result.has_value())) return;
    // result guaranteed valid from here → soft
    t.eq(result->name, "test");
    t.gt(result->size, 0);
});
```

Continuing soft checks after a broken precondition causes crashes or meaningless cascading failures. Fatal checks guard preconditions so failure messages always carry meaningful information.

### Adding Context with note()

When testing in a loop, use `note()` to identify which iteration failed:

```cpp
suite.run("all entries valid", [](auto& t) {
    for (int i = 0; i < count; ++i) {
        auto guard = t.note("entry index");
        t.is_true(entries[i].valid());
        t.gt(entries[i].size(), 0);
    }
});
```

`note()` returns an RAII guard. The note is automatically removed when the guard goes out of scope. On failure, notes appear as `note:` lines in the output.

### Test Names Describe Behavior

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

## Adding New Tests

1. Create `tests/test_<feature>.hh` — include from `test_main.cc` and add a call in `main()`
2. For compile-fail tests, add a `.cc` file under `tests/compile_fail/` — automatically picked up by `os.files()` glob
3. For smoke tests, add a `.cc` file under `tests/smoke/` — automatically picked up by `os.files()` glob

No xmake.lua edit needed.

## Running Tests

All commands are run from the project root:

```bash
xmake test                              # all tests
xmake test 'test_<mylib>/*'            # specific library
xmake test 'test_<mylib>/default'      # self-test only
xmake test 'test_<mylib>/fail_*'       # compile_fail only
```
