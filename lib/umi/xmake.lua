-- =====================================================================
-- UMI Package - Unified include paths for UMI libraries
-- =====================================================================
-- Usage:
--   includes("lib/umi")
--   target("my_app")
--       add_deps("umi")  -- or specific: "umi.core", "umi.dsp", "umi.midi"
-- =====================================================================

-- =====================================================================
-- Project Configuration (library-side)
-- =====================================================================

if os.isdir(path.join(os.projectdir(), ".refs/arm-embedded-xmake-repo")) then
    add_repositories("arm-embedded " .. path.join(os.projectdir(), ".refs/arm-embedded-xmake-repo"))
else
    add_repositories("arm-embedded https://github.com/tekitounix/arm-embedded-xmake-repo.git")
end

add_requires("arm-embedded", {optional = true})

set_languages("c++23")
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
set_warnings("all", "extra", "error")

option("board")
    set_default("stm32f4")
    set_showmenu(true)
    set_description("Target board")
    set_values("stm32f4", "stub")
option_end()

option("kernel")
    set_default("mono")
    set_showmenu(true)
    set_description("Kernel configuration")
    set_values("mono", "micro")
option_end()

option("coverage")
    set_default(false)
    set_showmenu(true)
    set_description("Enable code coverage instrumentation")
option_end()

option("build_type")
    set_default("dev")
    set_showmenu(true)
    set_description("Build type for kernel/app (dev=skip signature, release=require signature)")
    set_values("dev", "release")
option_end()

if has_config("coverage") and is_mode("debug") then
    add_cxflags("--coverage", {force = true})
    add_ldflags("--coverage", {force = true})
end

if is_config("build_type", "release") then
    add_defines("UMIOS_BUILD_TYPE=umi::kernel::BuildType::RELEASE", {public = true})
else
    add_defines("UMIOS_BUILD_TYPE=umi::kernel::BuildType::DEVELOPMENT", {public = true})
end

-- Get the umi lib directory (directory containing this script)
local umi_dir = os.scriptdir()
local lib_dir = path.directory(umi_dir)

-- Include port (hardware abstraction layer)
includes(path.join(umi_dir, "port"))

-- Include mmio (type-safe register abstraction)
includes(path.join(umi_dir, "mmio"))

-- Include umi.test (testing framework)
includes(path.join(lib_dir, "umi/test"))

-- Include library subprojects
includes(path.join(umi_dir, "bench"))
includes(path.join(umi_dir, "ref"))
includes(path.join(umi_dir, "fs"))
includes(path.join(umi_dir, "fs/test"))
includes(path.join(umi_dir, "usb/test"))
includes(path.join(umi_dir, "port/test"))

-- =====================================================================
-- UMI Shell Library (platform-independent shell primitives)
-- =====================================================================

target("umi.shell")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs(path.join(lib_dir, "umi/shell/include"), {public = true})
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
    add_includedirs(path.join(lib_dir, "umi/dsp/include"), {public = true})
target_end()

-- =====================================================================
-- UMI MIDI Library
-- =====================================================================

target("umi.midi")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs(path.join(lib_dir, "umi/midi/include"), {public = true})
target_end()

-- =====================================================================
-- UMI Boot Library
-- =====================================================================

target("umi.boot")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs(path.join(lib_dir, "umi/boot/include"), {public = true})
target_end()

-- =====================================================================
-- UMI Synth Library (common synthesizer implementation)
-- =====================================================================

target("umi.synth")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.dsp")

    add_includedirs(path.join(lib_dir, "umi/synth/include"), {public = true})
target_end()

-- =====================================================================
-- UMI USB Library
-- =====================================================================

target("umi.usb")
    set_kind("headeronly")
    set_group("umi")
    add_deps("umi.dsp")  -- ASRC components

    add_includedirs(path.join(lib_dir, "umi/usb/include"), {public = true})
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
