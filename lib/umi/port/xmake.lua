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

-- =====================================================================
-- Embedded Targets (STM32F4)
-- =====================================================================

local stm32f4_linker = path.join(port_dir, "mcu/stm32f4/linker.ld")
local stm32f4_syscalls = path.join(port_dir, "mcu/stm32f4/syscalls.cc")
local stm32f4_port_sources = {
    path.join(port_dir, "arch/cm4/**/*.cc"),
    path.join(port_dir, "common/**/*.cc"),
    path.join(port_dir, "mcu/stm32f4/*.cc"),
}

local function stm32f4_target(name, opts)
    opts = opts or {}
    target(name)
        set_group(opts.group or "firmware")
        set_default(false)
        add_rules("embedded")
        set_values("embedded.mcu", "stm32f407vg")
        set_values("embedded.linker_script", stm32f4_linker)
        if opts.optimize then
            set_values("embedded.optimize", opts.optimize)
        end
        add_deps("umi.embedded.full")
        if opts.deps then
            add_deps(opts.deps)
        end
        if opts.includedirs then
            add_includedirs(table.unpack(opts.includedirs))
        end
        add_defines("STM32F4", "BOARD_STM32F4")
        for _, src in ipairs(stm32f4_port_sources) do
            add_files(src)
        end
        add_files(stm32f4_syscalls)
        add_files(opts.source)
        if opts.renode_script then
            on_run(function()
                local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
                if not os.isfile(renode) then renode = "renode" end
                local renode_script = opts.renode_script
                if not path.isabs(renode_script) then
                    renode_script = path.join(os.projectdir(), renode_script)
                end
                os.execv(renode, {"--console", "--disable-xwt", "-e", "include @" .. renode_script})
            end)
        end
    target_end()
end

stm32f4_target("firmware", {
    source = path.join(os.projectdir(), "examples/embedded/example_app.cc")
})

target("renode_test")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", path.join(port_dir, "platform/renode/linker.ld"))
    set_values("embedded.optimize", "size")
    add_includedirs(path.join(os.projectdir(), "lib/umi"))
    add_includedirs(path.join(port_dir, "common"))
    add_includedirs(path.join(port_dir, "arch/cm4"))
    add_includedirs(path.join(port_dir, "mcu/stm32f4"))
    add_includedirs(path.join(port_dir, "board/stm32f4_disco"))
    add_includedirs(path.join(port_dir, "platform/embedded"))
    add_includedirs(path.join(os.projectdir(), "lib"))
    add_deps("umi.core", "umi.dsp")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files(path.join(os.projectdir(), "examples/renode_test/startup.cc"))
    add_files(path.join(os.projectdir(), "examples/renode_test/main.cc"))
    on_run(function()
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        os.execv(renode, {"--console", "--disable-xwt", "-e",
            "include @lib/umi/port/platform/renode/platform.resc; start"})
    end)
target_end()

stm32f4_target("bench_midi_format", {
    source = path.join(os.projectdir(), "lib/umi/midi/bench/bench_midi_format.cc"),
    optimize = "size",
    renode_script = "lib/umi/midi/bench/renode/bench_midi.resc"
})

stm32f4_target("bench_diode_ladder", {
    source = path.join(os.projectdir(), "lib/umi/dsp/bench/bench_diode_ladder.cc"),
    optimize = "fast",
    renode_script = "lib/umi/dsp/bench/renode/bench_diode_ladder.resc"
})

stm32f4_target("bench_waveshaper", {
    source = path.join(os.projectdir(), "lib/umi/dsp/bench/bench_waveshaper.cc"),
    optimize = "fast",
    renode_script = "lib/umi/dsp/bench/renode/bench_waveshaper.resc",
    includedirs = {path.join(os.projectdir(), ".refs/chowdsp_wdf/include")}
})

stm32f4_target("bench_waveshaper_fast", {
    source = path.join(os.projectdir(), "lib/umi/dsp/bench/bench_waveshaper_fast.cc"),
    optimize = "fast",
    renode_script = "lib/umi/dsp/bench/renode/bench_waveshaper_fast.resc"
})

stm32f4_target("umidi_test_renode", {
    source = path.join(os.projectdir(), "lib/umi/midi/test/test_renode.cc"),
    group = "tests/umidi",
    optimize = "size",
    renode_script = "lib/umi/midi/test/renode/umidi_test.resc"
})

target("umimock_renode")
    set_group("tests/umimock")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files(stm32f4_syscalls)
    add_files(path.join(os.projectdir(), "lib/umi/ref/test/test_mock_renode.cc"))
    add_includedirs(path.join(os.projectdir(), "lib/umi/ref/include"))
target_end()
