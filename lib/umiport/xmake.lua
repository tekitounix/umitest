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

local umiport_dir = os.scriptdir()
local lib_dir = path.directory(umiport_dir)

-- =====================================================================
-- umi-port rule: auto-adds concepts/ and common/ include dirs
-- =====================================================================

rule("umi-port")
    on_load(function(target)
        target:add("includedirs", path.join(umiport_dir, "concepts"), {public = true})
        target:add("includedirs", path.join(umiport_dir, "common"), {public = true})
    end)
rule_end()

-- =====================================================================
-- Port target: Cortex-M embedded (STM32F4 Discovery)
-- =====================================================================

target("umi.port.embedded.stm32f4_disco")
    set_kind("headeronly")
    set_group("umi")
    add_rules("umi-port")

    add_includedirs(path.join(umiport_dir, "arch/cm4"), {public = true})
    add_includedirs(path.join(umiport_dir, "mcu/stm32f4"), {public = true})
    add_includedirs(path.join(umiport_dir, "board/stm32f4_disco"), {public = true})
    add_includedirs(path.join(umiport_dir, "platform/embedded"), {public = true})
target_end()

-- =====================================================================
-- Port target: WASM
-- =====================================================================

target("umi.port.wasm")
    set_kind("headeronly")
    set_group("umi")
    add_rules("umi-port")

    add_includedirs(path.join(umiport_dir, "platform/wasm"), {public = true})
target_end()
