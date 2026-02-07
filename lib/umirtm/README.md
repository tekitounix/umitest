# umirtm

[日本語](docs/ja/README.md)

A header-only Real-Time Monitor library for C++23.
SEGGER RTT compatible ring buffers, an embedded printf, and `{}` placeholder print — all with zero heap allocation.

## Why umirtm

- RTT compatible — works with existing RTT viewers (J-Link, pyOCD, OpenOCD)
- Three output layers — raw ring buffer, printf, and `{}` format print
- Lightweight printf — no heap, configurable feature set for code size control
- Header-only — zero build dependencies
- Host testable — includes host-side bridge for unit testing and shared memory export

## Quick Start

```cpp
#include <umirtm/rtm.hh>
#include <umirtm/print.hh>

int main() {
    rtm::init("MY_RTM");
    rtm::log<0>("hello\n");
    rt::println("value = {}", 42);
    return 0;
}
```

## Build and Test

```bash
xmake test
```

## Public API

- `umirtm/rtm.hh` — RTT monitor core (Monitor, Mode, terminal colors)
- `umirtm/printf.hh` — Lightweight printf/snprintf (PrintConfig, format engine)
- `umirtm/print.hh` — `{}` format print/println helper
- `umirtm/rtm_host.hh` — Host-side bridge (stdout, shared memory, TCP)

## Examples

- [`examples/minimal.cc`](examples/minimal.cc) — basic RTT monitor usage
- [`examples/printf_demo.cc`](examples/printf_demo.cc) — printf format examples
- [`examples/print_demo.cc`](examples/print_demo.cc) — `{}` placeholder print

## Documentation

- [Design & API](docs/DESIGN.md)
- [Common Guides](../docs/INDEX.md)
- API docs: `doxygen Doxyfile` → `build/doxygen/html/index.html`

## License

MIT — See [LICENSE](../../LICENSE)
