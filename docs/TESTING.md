# Testing

[Docs Home](INDEX.md) | [日本語](ja/TESTING.md)

## Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_check.cc`: constexpr free functions (check_eq, check_lt, check_near, etc.)
- `tests/test_context.cc`: TestContext soft + fatal checks (eq, is_true, require_eq, etc.)
- `tests/test_suite.cc`: BasicSuite lifecycle, run(), section(), summary()
- `tests/test_reporter.cc`: reporter output verification
- `tests/test_format.cc`: format_value for bool, char, int, float, enum, pointer
- `tests/smoke/`: baseline compilation tests (4 cases — `build_should_pass`)
- `tests/compile_fail/`: constraint violation tests (20 cases — `build_should_fail`)

## Run Tests

```bash
xmake test
```

Useful subsets:

```bash
xmake test 'test_umitest/*'                   # all (default + smoke + compile-fail)
xmake test 'test_umitest/default'              # self-tests only
xmake test 'test_umitest/smoke_*'              # smoke baselines only
xmake test 'test_umitest/fail_*'               # compile-fail only
```

## Test Strategy

umitest is the test framework itself, so its tests validate:

1. **Check function correctness** — each check_* free function returns the expected bool
2. **TestContext checks** — soft checks (eq, lt, near, is_true) and fatal checks (require_*)
3. **Suite workflow** — pass counting, fail counting, section grouping, summary exit code
4. **Reporter output** — StdioReporter, PlainReporter, NullReporter produce expected output
5. **Diagnostic formatting** — format_value produces human-readable output for all types
6. **Compile-time contracts** — type constraints reject invalid comparisons (20 `build_should_fail` tests)
7. **Smoke baselines** — headers compile cleanly in isolation (4 `build_should_pass` tests)

## Quality Gates for Release

- All host self-tests pass
- Smoke baseline files compile successfully
- All compile-fail contract tests reject invalid code
- WASM cross-build and test pass

## Adding New Tests

1. Create `tests/test_<feature>.cc`
2. Add to `tests/xmake.lua` via `add_files()`
3. For compile-fail tests, add under `tests/compile_fail/` and add `add_tests("fail_<name>", {files = "compile_fail/<name>.cc", build_should_fail = true})`
4. Run `xmake test 'test_umitest/*'`
