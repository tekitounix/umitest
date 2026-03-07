# umitest Documentation

[日本語](ja/INDEX.md)

This page is the canonical documentation entry for both GitHub and Doxygen.

## Read in This Order

1. [Design](DESIGN.md)
2. [Testing](TESTING.md)

## API Reference Map

- Public entrypoint: `include/umitest/test.hh`
- Core components:
  - `include/umitest/suite.hh` — BasicSuite<R>, test registration and execution
  - `include/umitest/context.hh` — TestContext, soft + fatal assertion checks
  - `include/umitest/check.hh` — constexpr bool free functions
  - `include/umitest/format.hh` — format_value for diagnostic output
  - `include/umitest/reporter.hh` — ReporterLike concept
  - `include/umitest/failure.hh` — FailureView struct
  - `include/umitest/reporters/stdio.hh` — StdioReporter (ANSI color)
  - `include/umitest/reporters/plain.hh` — PlainReporter (no color)
  - `include/umitest/reporters/null.hh` — NullReporter (silent)

## Local Generation

```bash
xmake doxygen -P . -o build/doxygen .
```

Generated entrypoint:

- `build/doxygen/html/index.html`
