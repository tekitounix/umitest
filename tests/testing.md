# Testing

[README](../README.md) | English | [日本語](testing.ja.md)

umitest is the test framework itself, so its tests prove that every framework contract works correctly.

## Test Composition

| Category | Count | Purpose |
|----------|-------|---------|
| default (runtime) | 97 cases, 231 assertions / 7 modules | Behavioral verification of all APIs |
| compile_fail | 23 files | Prove type constraints reject invalid code |
| smoke | 1 file | All public headers compile independently |

## Runtime Test Modules (test_main.cc → test_*.hh)

| Module | Target | Coverage |
|--------|--------|----------|
| test_check | check.hh free functions | static_assert + runtime: check_true/false/eq/ne/lt/le/gt/ge/near + C string + mixed-sign integer |
| test_format | format.hh | format_value for all types (bool, nullptr, char, int, unsigned, float, enum, pointer, string, string_view, char[], "(?)") + BoundedWriter edge cases (size=0/1/truncation) |
| test_context | context.hh TestContext | All soft checks + all fatal checks (require_true/false/eq/ne/lt/le/gt/ge/near) with pass/fail for each + NoteGuard RAII |
| test_suite | suite.hh BasicSuite | Pass/fail counting, empty test, multiple failures, section, summary exit code |
| test_reporter | reporters/*.hh | ReporterLike concept satisfaction (Stdio/Plain/Null) + FailureView/SummaryView field verification + op_for_kind all 16 mappings |
| test_exception | context.hh exception checks | throws\<E\>/throws(any)/nothrow + require variants, all pass/fail patterns |
| test_string | check.hh + context.hh string checks | check_str_contains/starts_with/ends_with + ctx soft/fatal all pass/fail |

## compile_fail Test Matrix

7 type constraints × 3 layers (free function / ctx method / require method) + 2 structural constraints = 23 files.

| Constraint | free | ctx | require |
|------------|------|-----|---------|
| eq_incomparable (incomparable types) | eq_incomparable | ctx_eq_incomparable | req_eq_incomparable |
| eq_char8t (char8_t pair) | eq_char8t | ctx_eq_char8t | req_eq_char8t |
| eq_char8t_mixed (char8_t × string) | eq_char8t_mixed | ctx_eq_char8t_mixed | req_eq_char8t_mixed |
| eq_char8t_nullptr (char8_t × nullptr) | eq_char8t_nullptr | ctx_eq_char8t_nullptr | req_eq_char8t_nullptr |
| lt_unordered (unordered type) | lt_unordered | ctx_lt_unordered | req_lt_unordered |
| lt_pointer (pointer ordering) | lt_pointer | ctx_lt_pointer | req_lt_pointer |
| near_integer (near on integers) | near_integer | ctx_near_integer | req_near_integer |

| Structural constraint | File |
|-----------------------|------|
| TestContext non-copyable | ctx_copy |
| run() rejects non-void lambda | non_void_lambda |

### Why 3 Layers

`check_eq` (free function), `ctx.eq` (ctx method), and `ctx.require_eq` (require method) are independent templates with their own constraints. A change to one layer's constraint does not necessarily propagate to others, so each layer needs its own compile_fail test.

## Smoke Test

`standalone.cc` includes all 10 public headers individually and verifies each compiles on its own.

## Completeness Rationale

1. **API symmetry** — Every soft check has a corresponding require_* and both pass/fail paths are tested
2. **Type constraint matrix** — 7 constraints × 3 layers cover compile_fail exhaustively
3. **Formatter coverage** — All format_value branches (12 types + fallback) are tested
4. **Edge cases** — BoundedWriter size=0/1, check_near NaN/negative-eps, safe_eq nullptr/mixed-sign
5. **Meta-testing** — RecordingReporter verifies internal state (is_fatal flag, kind string)
