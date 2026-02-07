# umibench

English | [日本語](docs/ja/README.md)

`umibench` is a small cross-target micro-benchmark library for C++.
It lets you write one benchmark program style and run it on host, WebAssembly, and embedded targets.

## Release Status

- Current version: `0.1.0`
- Stability: initial release
- Versioning policy: [`RELEASE.md`](RELEASE.md)
- Changelog: [`CHANGELOG.md`](CHANGELOG.md)

## Why umibench

- Single benchmark style across targets (`#include <umibench/bench.hh>`, `#include <umibench/platform.hh>`)
- Baseline-corrected measurement with robust median-based calibration
- Lightweight output abstraction (`OutputLike`) and timer abstraction (`TimerLike`)
- Test suite covering semantics, numeric edge cases, and compile-fail API guards

## Quick Start

```cpp
#include <umibench/bench.hh>
#include <umibench/platform.hh>

int main() {
    using Platform = umi::bench::Platform;

    umi::bench::Runner<Platform::Timer> runner;
    runner.calibrate<64>();

    auto stats = runner.run<64>(100, [] {
        volatile int x = 0;
        x += 1;
        (void)x;
    });

    umi::bench::report<Platform>("sample", stats);
    Platform::halt();
    return 0;
}
```

## Build and Test

```bash
xmake test
xmake build umibench_stm32f4_renode
xmake build umibench_stm32f4_renode_gcc
```

## Documentation

- Documentation index (recommended entry): [`docs/INDEX.md`](docs/INDEX.md)
- Getting started: [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md)
- Detailed usage: [`docs/USAGE.md`](docs/USAGE.md)
- Platform model: [`docs/PLATFORMS.md`](docs/PLATFORMS.md)
- Testing and quality gates: [`docs/TESTING.md`](docs/TESTING.md)
- Example guide: [`docs/EXAMPLES.md`](docs/EXAMPLES.md)
- Design note: [`docs/DESIGN.md`](docs/DESIGN.md)

Japanese versions are available under [`docs/ja/`](docs/ja/README.md).

Generate Doxygen HTML locally:

```bash
xmake doxygen -P . -o build/doxygen .
```

GitHub automation is available via [`.github/workflows/umibench-doxygen.yml`](.github/workflows/umibench-doxygen.yml).

## License

MIT (`LICENSE`)
