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

add_rules("mode.debug", "mode.release")

-- Get the script directory for relative paths
local port_dir = os.scriptdir()

target("umi.port")
    set_kind("static")
    add_includedirs(port_dir, {public = true})

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

    -- Source files
    add_files(path.join(port_dir, "arch/cm4/**/*.cc"))
    add_files(path.join(port_dir, "common/**/*.cc"))
    add_files(path.join(port_dir, "mcu/stm32f4/*.cc"))

    -- Header files
    add_headerfiles(path.join(port_dir, "concepts/**/*.hh"))
    add_headerfiles(path.join(port_dir, "common/**/*.hh"))
    add_headerfiles(path.join(port_dir, "arch/**/*.hh"))
    add_headerfiles(path.join(port_dir, "mcu/**/*.hh"))
    add_headerfiles(path.join(port_dir, "board/**/*.hh"))
    add_headerfiles(path.join(port_dir, "device/**/*.hh"))
    add_headerfiles(path.join(port_dir, "platform/**/*.hh"))

target_end()
