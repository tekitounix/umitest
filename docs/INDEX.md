# umitest Documentation

[日本語](ja/INDEX.md)

This page is the canonical documentation entry for both GitHub and Doxygen.

## Read in This Order

1. [Design](DESIGN.md)
2. [Testing](TESTING.md)

## API Reference Map

- Public entrypoint: `include/umitest/test.hh`
- Core components:
  - `include/umitest/suite.hh` — TestSuite, test registration and execution
  - `include/umitest/context.hh` — TestContext, assertion state tracking
  - `include/umitest/format.hh` — format_value for diagnostic output

## Local Generation

```bash
xmake doxygen -P . -o build/doxygen .
```

Generated entrypoint:

- `build/doxygen/html/index.html`
