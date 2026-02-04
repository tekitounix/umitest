-- =====================================================================
-- UMI Package - Unified include paths for UMI libraries
-- =====================================================================
-- Usage:
--   includes("lib/umi")
--   target("my_app")
--       add_deps("umi")  -- or specific: "umi.core", "umi.dsp", "umi.midi"
-- =====================================================================

-- Get the umi lib directory (directory containing this script)
local umi_dir = os.scriptdir()
local lib_dir = path.directory(umi_dir)

-- Include port (hardware abstraction layer)
includes(path.join(umi_dir, "port"))

-- Include mmio (type-safe register abstraction)
includes(path.join(umi_dir, "mmio"))

-- Include umi.test (testing framework)
includes(path.join(lib_dir, "umi/test"))

-- =====================================================================
-- UMI Shell Library (platform-independent shell primitives)
-- =====================================================================

target("umi.shell")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs(path.join(lib_dir, "umi/shell"), {public = true})
target_end()

-- =====================================================================
-- Core UMI-OS Library
-- =====================================================================

target("umi.core")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.shell")

    add_includedirs(path.join(lib_dir, "umi/core"), {public = true})
    add_includedirs(path.join(lib_dir, "umi/kernel"), {public = true})
    add_includedirs(path.join(lib_dir, "umi/adapter"), {public = true})
    add_includedirs(path.join(lib_dir, "umi"), {public = true})
    -- For backward compatibility with umios/* includes (via symlink lib/umios -> lib/umi)
    add_includedirs(lib_dir, {public = true})
target_end()

-- =====================================================================
-- UMI-OS WASM Backend
-- =====================================================================

target("umi.wasm")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.core")

target_end()

-- =====================================================================
-- UMI-OS Embedded Backend (Cortex-M)
-- =====================================================================

target("umi.embedded")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.core", "umi.port")

target_end()

-- =====================================================================
-- UMI-OS Kernel Components
-- =====================================================================

target("umi.kernel")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.core")

    add_includedirs(path.join(lib_dir, "umi/kernel"), {public = true})
target_end()

-- =====================================================================
-- UMI DSP Library
-- =====================================================================

target("umi.dsp")
    set_kind("headeronly")
    set_group("umi")

    -- Include only the public header root
    add_includedirs(path.join(lib_dir, "umi/dsp"), {public = true})
target_end()

-- =====================================================================
-- UMI MIDI Library
-- =====================================================================

target("umi.midi")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs(path.join(lib_dir, "umi/midi"), {public = true})
target_end()

-- =====================================================================
-- UMI Boot Library
-- =====================================================================

target("umi.boot")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs(path.join(lib_dir, "umi/boot"), {public = true})
target_end()

-- =====================================================================
-- UMI Synth Library (common synthesizer implementation)
-- =====================================================================

target("umi.synth")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.dsp")

    add_includedirs(path.join(lib_dir, "umi/synth"), {public = true})
target_end()

-- =====================================================================
-- UMI USB Library
-- =====================================================================

target("umi.usb")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.dsp")  -- ASRC components

    add_includedirs(path.join(lib_dir, "umi/usb"), {public = true})
target_end()

target("umi.mmio")
    set_kind("static")
    set_group("umi")
    add_includedirs(path.join(lib_dir, "umi/mmio"), {public = true})
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
