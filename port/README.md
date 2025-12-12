# UMI-OS Porting Layer

Hardware abstraction organized by architecture, vendor, and board.

## Directory Structure

```
port/
├── arm/                      # ARM Architecture
│   └── cortex-m/
│       ├── common/           # Shared peripherals (SysTick, NVIC, SCB, DWT)
│       └── cortex_m4.hh      # Cortex-M4 primitives
│
├── vendor/                   # Vendor peripherals
│   └── stm32/stm32f4/        # RCC, GPIO, UART
│
└── board/                    # Board-specific implementations
    ├── stm32f4/              # STM32F4 target
    │   ├── hw_impl.hh        # HW implementation for kernel
    │   ├── startup.cc        # Vector table, reset handler
    │   ├── syscalls.cc       # Newlib stubs
    │   └── linker.ld         # Linker script
    └── stub/                 # Host testing stub
        └── hw_impl.hh
```

## Porting to a New Board

### 1. Create `port/board/<your_board>/hw_impl.hh`

```cpp
#include "../../arm/cortex-m/cortex_m4.hh"
#include "../../../core/umi_kernel.hh"

namespace umi::board::your_board {

struct Hw {
    // Timer
    static void set_timer_absolute(umi::usec t);
    static umi::usec monotonic_time_usecs();
    
    // Critical section
    static void enter_critical();  // cpsid i / BASEPRI
    static void exit_critical();
    
    // Context switch
    static void request_context_switch();  // PendSV
    
    // ... see port/board/stm32f4/hw_impl.hh for full API
};

template <std::size_t MaxTasks = 8, std::size_t MaxTimers = 8>
using Kernel = umi::Kernel<MaxTasks, MaxTimers, umi::Hw<Hw>>;

} // namespace
```

### 2. Copy and adapt `startup.cc`, `syscalls.cc`, `linker.ld`

### 3. Update `xmake.lua` to include your board

## Platform Notes

### Cortex-M4/M7
- Critical: Use `BASEPRI` to keep audio IRQ enabled
- Context: `PendSV` for deferred context switch
- Cache: M7 only, M4 is no-op

### Host Testing
Use `port/board/stub/hw_impl.hh` for unit tests without hardware.
