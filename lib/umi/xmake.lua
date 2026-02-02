-- =====================================================================
-- UMI Package - Unified include paths for UMI libraries
-- =====================================================================
-- Usage:
--   includes("lib/umi")
--   target("my_app")
--       add_deps("umi")  -- or specific: "umi.core", "umi.dsp", "umi.midi"
-- =====================================================================

-- Get the lib directory (parent of this package)
local lib_dir = path.directory(os.scriptdir())

-- Include umiport (hardware abstraction layer)
includes(path.join(lib_dir, "umiport"))

-- Include umimmio (type-safe register abstraction)
includes(path.join(lib_dir, "umimmio"))

-- =====================================================================
-- UMI Shell Library (platform-independent shell primitives)
-- =====================================================================

target("umi.shell")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs(path.join(lib_dir, "umishell/include"), {public = true})
target_end()

-- =====================================================================
-- Core UMI-OS Library
-- =====================================================================

target("umi.core")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.shell")

    -- umios core includes
    add_includedirs(path.join(lib_dir, "umios/core"), {public = true})
    add_includedirs(path.join(lib_dir, "umios/kernel"), {public = true})
    add_includedirs(path.join(lib_dir, "umios/adapter"), {public = true})
    add_includedirs(lib_dir, {public = true})  -- for <umios/...> paths
target_end()

-- =====================================================================
-- UMI-OS WASM Backend
-- =====================================================================

target("umi.wasm")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.core")
    add_deps("umi.port.wasm")

target_end()

-- =====================================================================
-- UMI-OS Embedded Backend (Cortex-M)
-- =====================================================================

target("umi.embedded")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.core")
    add_deps("umi.port.embedded.stm32f4_disco")

    -- Legacy paths (bsp/hal not yet migrated to umiport)
    add_includedirs(path.join(lib_dir, "bsp/stm32f4-disco"), {public = true})
    add_includedirs(path.join(lib_dir, "hal/stm32"), {public = true})
target_end()

-- =====================================================================
-- UMI-OS Kernel Components
-- =====================================================================

target("umi.kernel")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.core")

    add_includedirs(path.join(lib_dir, "umios/kernel"), {public = true})
target_end()

-- =====================================================================
-- UMI DSP Library
-- =====================================================================

target("umi.dsp")
    set_kind("headeronly")
    set_group("umi")

    -- Include only the public header root
    add_includedirs(path.join(lib_dir, "umidsp/include"), {public = true})
target_end()

-- =====================================================================
-- UMI MIDI Library
-- =====================================================================

target("umi.midi")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs(path.join(lib_dir, "umidi/include"), {public = true})
target_end()

-- =====================================================================
-- UMI Boot Library
-- =====================================================================

target("umi.boot")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs(path.join(lib_dir, "umiboot/include"), {public = true})
target_end()

-- =====================================================================
-- UMI Synth Library (common synthesizer implementation)
-- =====================================================================

target("umi.synth")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.dsp")

    add_includedirs(path.join(lib_dir, "umisynth/include"), {public = true})
target_end()

-- =====================================================================
-- UMI USB Library
-- =====================================================================

target("umi.usb")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.dsp")  -- ASRC components

    add_includedirs(path.join(lib_dir, "umiusb/include"), {public = true})
target_end()

-- =====================================================================
-- Convenience Targets (bundles)
-- =====================================================================

-- Full UMI stack for WASM targets
target("umi.wasm.full")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.wasm", "umi.dsp", "umi.midi", "umi.boot")
target_end()

-- Full UMI stack for embedded targets
target("umi.embedded.full")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.embedded", "umi.dsp", "umi.midi", "umi.boot", "umi.usb")
target_end()

-- All UMI libraries (for tests)
target("umi.all")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.core", "umi.dsp", "umi.midi", "umi.boot")
target_end()
