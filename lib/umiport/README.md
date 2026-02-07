# umiport

Shared embedded platform support for UMI library tests.

## What This Provides

- STM32F4 startup code (`startup.cc`, `syscalls.cc`, `linker.ld`)
- Renode simulation platform description (`stm32f4_test.repl`)
- Common UART output backend (`RenodeUartOutput`)

## Usage

Libraries that run tests on STM32F4/Renode reference umiport's source files
in their `xmake.lua`:

```lua
local umiport_stm32f4 = path.join(os.scriptdir(), "path/to/lib/umiport/src/stm32f4")

add_files(path.join(umiport_stm32f4, "startup.cc"))
add_files(path.join(umiport_stm32f4, "syscalls.cc"))
set_values("embedded.linker_script", path.join(umiport_stm32f4, "linker.ld"))
```

Each library provides its own `platform.hh` that defines `umi::port::Platform`
using `umiport/stm32f4/uart_output.hh`.

## Build and Test

umiport is not tested independently. It is validated through the libraries
that depend on it (umitest, umimmio, umirtm, umibench).

```bash
xmake test    # runs all library tests
```

## License

MIT — See [LICENSE](LICENSE)
