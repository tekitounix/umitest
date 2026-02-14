# Testing

[Docs Home](INDEX.md) | [日本語](ja/TESTING.md)

## Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_assertions.cc`: assert_eq, assert_ne, assert_true, assert_near, comparisons
- `tests/test_suite_workflow.cc`: TestSuite pass/fail/mixed/lambda/section patterns
- `tests/test_format.cc`: format_value for bool, char, int, float, enum, pointer
- `tests/compile_fail/near_non_numeric.cc`: compile-fail guard — assert_near rejects non-numeric types

## Run Tests

```bash
xmake test
```

Useful subsets:

```bash
xmake test 'test_umitest/*'
xmake test 'test_umitest_compile_fail/*'
```

## Test Strategy

umitest is the test framework itself, so its tests validate:

1. **Assertion correctness** — each assert macro produces the expected pass/fail result
2. **Suite workflow** — pass counting, fail counting, mixed suites, sub-suite nesting
3. **Diagnostic formatting** — format_value produces human-readable output for all types
4. **Compile-fail guards** — assert_near rejects non-numeric types at compile time

## Quality Gates for Release

- All host tests pass
- Compile-fail contract tests pass (near_non_numeric)
- WASM cross-build and test pass

## Adding New Tests

1. Create `tests/test_<feature>.cc`
2. Add to `tests/xmake.lua` via `add_files()`
3. For compile-fail tests, add under `tests/compile_fail/` and register in xmake.lua
4. Run `xmake test "test_umitest/*"`
