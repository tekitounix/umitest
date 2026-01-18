# UMI-OS Platform Abstraction

This directory contains common interfaces that are implemented by platform-specific backends.

## Directory Structure

```
lib/umios/
├── platform/              # Common interfaces (this directory)
│   ├── syscall.hh        # Syscall interface (implemented by backends)
│   ├── protection.hh     # Memory protection interface
│   └── privilege.hh      # Privilege control interface
│
├── backend/
│   ├── cm/               # Cortex-M implementation
│   │   └── platform/     # CM4 implements platform/*.hh
│   │       ├── syscall.hh
│   │       ├── protection.hh
│   │       └── privilege.hh
│   │
│   └── wasm/             # WASM implementation
│       └── platform/
│           ├── syscall.hh
│           └── protection.hh
```

## Usage

Build system configures include path to select the backend:

```lua
-- For Cortex-M4 target
add_includedirs("lib/umios/backend/cm")  -- picks up cm/platform/*.hh

-- For WASM target
add_includedirs("lib/umios/backend/wasm")  -- picks up wasm/platform/*.hh
```

Application code uses:
```cpp
#include <platform/syscall.hh>  // Resolved by build system
```
