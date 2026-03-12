# umitest Design

English | [日本語](design.ja.md)

Internal design decisions and architectural constraints.

---

## 1. Non-Negotiable Requirements

1. **No macros** — `std::source_location::current()` as default argument. No `ASSERT_EQ` or `TEST_CASE`.
2. **Header-only** — no static libraries, no link-time registration, no code generation.
3. **No heap allocation** — all internal state uses stack or static storage. Bare-metal compatible.
4. **No exceptions in assertions** — assertions do not throw. TestContext tracks failure state internally.

---

## 2. Dependency Boundaries

Layering is strict:

1. `umitest` depends only on C++23 standard library headers.
2. No dependency on other umi libraries.
3. Other umi libraries depend on `umitest` for testing (test-time only).

---

## 3. Source Location Capture

All check methods accept `std::source_location loc = std::source_location::current()` as the last parameter.
The compiler fills in the caller's file and line at the call site — no macro expansion needed.

This is the fundamental mechanism that eliminates `ASSERT_EQ(a, b)` macros.
In traditional frameworks, macros capture `__FILE__` and `__LINE__` at expansion time.
umitest achieves the same via default argument evaluation, which is standard C++20/23 behavior.

---

## 4. TestContext Architecture

### 4.1 Unified Template Pattern

Every check method delegates to a private template parameterized by `<bool Fatal>`:

```cpp
template <bool Fatal>
bool bool_check(bool passed, const char* kind, std::source_location loc);

template <bool Fatal, typename A, typename B>
bool compare_check(bool passed, const A& a, const char* kind, const B& b, std::source_location loc);
```

The public API is a thin forwarding layer:

- `eq(a, b, loc)` → `compare_check<false>(...)`
- `require_eq(a, b, loc)` → `compare_check<true>(...)`

This eliminates code duplication between soft and fatal variants. The `Fatal` flag is carried through to `FailureView::is_fatal` for reporter-side formatting.

### 4.2 Failure Callback

TestContext does not know about reporters. It receives a function pointer at construction:

```cpp
using FailCallback = void (*)(const FailureView& failure, void* ctx);
```

`BasicSuite::run()` injects a lambda that forwards to the reporter:

```cpp
TestContext ctx(test_name,
    [](const FailureView& fv, void* p) { static_cast<BasicSuite*>(p)->reporter.report_failure(fv); },
    this);
```

This decouples TestContext from the reporter template parameter. TestContext is a concrete class (not a template), which keeps binary size small and compilation fast.

### 4.3 No Heap, Fixed Capacity

- Failure messages use `std::array<char, 256>` on the stack — no `std::string`, no allocation.
- Note stack is `std::array<const char*, 4>` with a depth counter.
- `BoundedWriter` guarantees NUL-termination and truncation safety for any buffer size (including 0).

### 4.4 Fatal Check Return Value

Fatal checks (`require_*`) are `[[nodiscard]]` and return `bool`. The intended pattern:

```cpp
if (!t.require_true(ptr != nullptr)) return;
t.eq(ptr->value, 42);
```

This is deliberate: umitest does not use exceptions, `setjmp`/`longjmp`, or any non-local control flow. The test function is responsible for early return. This keeps the framework bare-metal compatible and the control flow explicit.

---

## 5. Reporter Abstraction

### 5.1 ReporterLike Concept

```cpp
template <typename R>
concept ReporterLike =
    std::move_constructible<R> && requires(R r, const char* s, const FailureView& fv, const SummaryView& sv) {
        r.section(s);
        r.test_begin(s);
        r.test_pass(s);
        r.test_fail(s);
        r.report_failure(fv);
        r.summary(sv);
    };
```

Reporters are duck-typed via concept — no base class, no vtable. `BasicSuite<R>` is the only template; all other types (TestContext, FailureView, SummaryView) are concrete.

### 5.2 FailureView / SummaryView

Reporters receive structured views, not strings. This separates formatting from logic:

- `FailureView`: test name, source location, fatal flag, check kind, LHS/RHS values, extra info, active notes.
- `SummaryView`: suite name, pass/fail counts, assertion statistics.

Views are valid only during the reporter call — reporters must copy data if they need to retain it.

### 5.3 Built-in Reporters

| Reporter | Use case |
|----------|----------|
| `StdioReporter` | ANSI-colored stdout via `<cstdio>` (default `Suite`) |
| `PlainReporter` | Plain text without escape codes |
| `NullReporter` | Silent — for self-testing the framework |

Each has a `static_assert(ReporterLike<...>)` immediately after the class definition.

---

## 6. Check Function Layer (check.hh)

### 6.1 Two-Layer Design

Checks are split into two layers:

1. **Free functions** (`check_eq`, `check_lt`, etc.) — pure `constexpr bool` functions. No side effects, no state. Usable in `static_assert`.
2. **TestContext methods** (`eq`, `lt`, etc.) — call the free functions, then format and report on failure.

This separation means compile-time contract verification and runtime testing share the exact same comparison logic.

### 6.2 Type Constraints

| Concept | Purpose |
|---------|---------|
| `std::equality_comparable_with<A, B>` | Gates `eq`/`ne` |
| `OrderableNonPointer<A, B>` | Gates `lt`/`le`/`gt`/`ge` — rejects pointers |
| `std::floating_point<T>` | Gates `near` |
| `std::invocable<F>` | Gates `throws`/`nothrow` |

**Pointer exclusion**: `OrderableNonPointer` explicitly rejects pointer types via `!std::is_pointer_v<std::decay_t<T>>`. Pointer ordering is implementation-defined in C++; comparing addresses with `<` is almost always a bug.

**Char pointer routing**: `eq(const char*, const char*)` is a non-template overload that compares by content (via `std::string_view`), not by address. The template overload excludes char pointer pairs via `excluded_char_pointer_v` to prevent ambiguous resolution.

### 6.3 Mixed-Sign Integer Safety

When both operands satisfy `SafeCmpInteger` (integral, non-bool, non-char), comparisons use `std::cmp_equal`, `std::cmp_less`, etc. These handle signed/unsigned comparison without undefined behavior:

```cpp
t.eq(-1, 0u);  // Uses std::cmp_equal → false (correct)
                // Without safe cmp: -1 == 0u wraps → true (wrong)
```

---

## 7. Value Formatting (format.hh)

### 7.1 BoundedWriter

All formatting goes through `BoundedWriter`, a safe buffer writer with guarantees:

- `size == 0`: no writes, no memory access.
- `size == 1`: writes only NUL terminator.
- `size >= 2`: normal operation with NUL always maintained.
- Truncation never causes UB; detectable via `truncated()`.

`BoundedWriter` is `constexpr` — integer and string formatting works at compile time.

### 7.2 format_value Dispatch

`format_value<T>` uses `if constexpr` chains (not overload resolution or ADL) to select formatting:

| Type | Format |
|------|--------|
| `bool` | `"true"` / `"false"` |
| `nullptr_t` | `"nullptr"` |
| `char` | `'c' (N)` — escaped char + numeric value |
| `std::string` / `std::string_view` | `"quoted and escaped"` |
| `const char*` | `"quoted and escaped"` or `"(null)"` |
| unsigned integer | decimal |
| signed integer | decimal |
| floating-point | shortest round-trip via `std::to_chars` |
| enum | underlying integer value |
| pointer | `0xhex` |
| unknown | `"(?)"` |

Floating-point formatting uses `std::to_chars` for shortest round-trip representation, with special handling for NaN, infinity, and negative zero. Whole numbers get `.0` appended for clarity.

### 7.3 No stdio Dependency

`format_value` does not use `printf`, `snprintf`, or any `<cstdio>` function. Only `std::to_chars` (from `<charconv>`) is used for floating-point. This keeps formatting usable on freestanding targets.

`StdioReporter` is the only component that uses `<cstdio>`, and it is a leaf — nothing depends on it.

---

## 8. Exception Portability

Exception-related methods (`throws`, `nothrow`, `require_throws`, `require_nothrow`) are conditionally compiled:

```cpp
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
```

When exceptions are disabled (`-fno-exceptions`), these methods do not exist — calling them is a compile error, not a runtime error. This is intentional: on `fno-exceptions` targets (Cortex-M, WASM), exception tests are meaningless.

---

## 9. Current Layout

```text
lib/umitest/
├── README.md
├── LICENSE
├── xmake.lua
│
├── docs/
│   ├── readme.ja.md
│   ├── design.md / design.ja.md
│
├── include/umitest/
│   ├── test.hh            # Umbrella header (Suite, PlainSuite aliases)
│   ├── suite.hh           # BasicSuite<R> template
│   ├── context.hh         # TestContext (all check methods)
│   ├── check.hh           # constexpr bool free functions + concepts
│   ├── format.hh          # BoundedWriter + format_value
│   ├── failure.hh         # FailureView, SummaryView, op_for_kind
│   ├── reporter.hh        # ReporterLike concept
│   └── reporters/
│       ├── stdio.hh       # StdioReporter (ANSI colored)
│       ├── plain.hh       # PlainReporter (no escape codes)
│       └── null.hh        # NullReporter (silent)
│
├── tests/
│   ├── xmake.lua
│   ├── test_main.cc
│   ├── test_check.hh           # check_* free function tests
│   ├── test_context.hh         # TestContext soft/fatal check tests
│   ├── test_exception.hh       # throws/nothrow tests
│   ├── test_format.hh          # format_value tests
│   ├── test_reporter.hh        # ReporterLike concept + FailureView/SummaryView tests
│   ├── test_string.hh          # str_contains/starts_with/ends_with tests
│   ├── test_suite.hh           # Suite/BasicSuite integration tests
│   ├── smoke/
│   │   └── standalone.cc
│   └── compile_fail/           # 23 negative compile tests
│       ├── .clangd
│       └── *.cc
│
└── examples/
    ├── minimal.cc
    ├── assertions.cc
    ├── check_style.cc
    └── constexpr.cc
```

---

## 10. Design Principles

1. Zero-cost — no virtual dispatch, no RTTI, no heap allocation.
2. Compile-time safety — type constraints reject invalid comparisons before runtime.
3. Embedded-first — works on Cortex-M with `-fno-exceptions -fno-rtti`.
4. Single source of truth — `check_*` functions are used by both `static_assert` and runtime checks.
5. Reporter-agnostic — test logic is decoupled from output format.
