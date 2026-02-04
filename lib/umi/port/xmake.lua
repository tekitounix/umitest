-- =====================================================================
-- UMI-Port: Hardware Abstraction Layer
-- =====================================================================
-- Layer structure (each subdirectory is an include root):
--   concepts/    - C++23 Concept contracts (always included)
--   common/      - Cortex-M common (NVIC, SCB, SysTick, DWT, etc.)
--   arch/<arch>/ - CPU core (cm4, cm7) context switch, cache, FPU
--   mcu/<mcu>/   - SoC peripherals (stm32f4, stm32h7)
--   board/<brd>/ - Board-level drivers (stm32f4_disco, daisy_seed)
--   platform/<p>/ - Execution environment (embedded, wasm)
-- =====================================================================
--
-- NOTE: This is a header-only target for include paths only.
-- Source files must be added by the consuming embedded target.
-- =====================================================================

add_rules("mode.debug", "mode.release")

-- Get the script directory for relative paths
local port_dir = os.scriptdir()
local lib_dir = path.directory(path.directory(port_dir))

target("umi.port")
    set_kind("headeronly")
    add_includedirs(port_dir, {public = true})
    -- For backward compatibility with umios/* includes (via symlink lib/umios -> lib/umi)
    add_includedirs(lib_dir, {public = true})

    -- Core include directories (always included)
    add_includedirs(path.join(port_dir, "concepts"), {public = true})
    add_includedirs(path.join(port_dir, "common"), {public = true})

    -- Architecture-specific (Cortex-M4)
    add_includedirs(path.join(port_dir, "arch/cm4"), {public = true})

    -- MCU-specific (STM32F4)
    add_includedirs(path.join(port_dir, "mcu/stm32f4"), {public = true})

    -- Board-specific (STM32F4 Discovery)
    add_includedirs(path.join(port_dir, "board/stm32f4_disco"), {public = true})

    -- Platform-specific (embedded)
    add_includedirs(path.join(port_dir, "platform/embedded"), {public = true})

    -- Header files (use relative paths for xmake format compatibility)
    add_headerfiles("concepts/**/*.hh")
    add_headerfiles("common/**/*.hh")
    add_headerfiles("arch/**/*.hh")
    add_headerfiles("mcu/**/*.hh")
    add_headerfiles("board/**/*.hh")
    add_headerfiles("device/**/*.hh")
    add_headerfiles("platform/**/*.hh")

target_end()
