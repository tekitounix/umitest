-- =====================================================================
-- UMI-OS Build Configuration
-- =====================================================================
-- A modular RTOS for embedded audio/MIDI applications
--
-- Build targets:
--   Host (native):     xmake build test_kernel test_audio test_midi
--   ARM firmware:      xmake build firmware renode_test
--   All:               xmake build -a
--
-- Tasks:
--   xmake test         - Run host unit tests
--   xmake renode       - Interactive Renode session
--   xmake renode-test  - Automated Renode tests
--   xmake robot        - Robot Framework tests
--   xmake clean-all    - Clean all build artifacts
--   xmake info         - Show configuration
--
-- Configuration:
--   xmake f -m debug|release      - Build mode
--   xmake f --board=stm32f4|stub  - Target board
--   xmake f --kernel=mono|micro   - Kernel variant
-- =====================================================================

set_project("umi_os")
set_version("0.1.0")
set_xmakever("2.8.0")

-- =====================================================================
-- Custom Package Repository (ARM Embedded Toolchain)
-- =====================================================================

-- Local development: use .tools/ for editing packages
-- CI / new setup: auto-fetch from GitHub
if os.isdir(".tools/arm-embedded-xmake-repo") then
    add_repositories("arm-embedded .tools/arm-embedded-xmake-repo")
else
    add_repositories("arm-embedded https://github.com/tekitounix/arm-embedded-xmake-repo.git")
end

-- Request ARM toolchains and embedded support
add_requires("arm-embedded", {optional = true})

-- Request coding-rules for clangd/clang-format/clang-tidy configuration
-- Note: Temporarily disabled - needs task loading mechanism fix
-- add_requires("coding-rules", {optional = true})

-- =====================================================================
-- Global Configuration
-- =====================================================================

-- Language standard
set_languages("c++23")

-- Build modes
add_rules("mode.debug", "mode.release")

-- Generate compile_commands.json for clangd/VS Code IntelliSense
-- outputdir = "." generates at project root (clangd finds it automatically)
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})

-- Generate .vscode/settings.json with clangd configuration
-- Note: embedded.vscode rule disabled due to empty rule error
-- add_rules("embedded.vscode")

-- Strict warnings for all targets
set_warnings("all", "extra", "error")

-- =====================================================================
-- Configuration Options
-- =====================================================================

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

-- Apply coverage flags if enabled
if has_config("coverage") and is_mode("debug") then
    add_cxflags("--coverage", {force = true})
    add_ldflags("--coverage", {force = true})
end

-- =====================================================================
-- Core Library (Header-only)
-- =====================================================================

target("umios")
    set_kind("headeronly")
    set_group("libraries")

    add_headerfiles("lib/umios/*.hh")
    add_includedirs(".", {public = true})
    add_includedirs("lib/umios", {public = true})
    
    -- Preprocessor definitions based on config
    if is_config("kernel", "micro") then
        add_defines("UMI_KERNEL_MICRO", {public = true})
    end
target_end()

-- =====================================================================
-- Host Tests (Native compilation)
-- =====================================================================
-- These run on the development machine for rapid iteration

-- Common include paths for tests
local test_includedirs = {
    "lib/umios/core",
    "lib/umios/kernel",
    "lib/umidsp/include",
    "lib/umiboot/include",
    "lib/umidi/include",
    "lib",
    "tests"
}

-- Helper: Define a host test target
local function host_test(name, source, opts)
    opts = opts or {}
    target(name)
        set_kind("binary")
        set_group(opts.group or "tests/host")
        set_default(opts.default ~= false)
        add_files(source)
        add_includedirs(opts.includedirs or test_includedirs)
        add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
        on_run(function (target) os.execv(target:targetfile()) end)
    target_end()
end

-- Main host tests (default = true)
host_test("test_dsp", "lib/umidsp/test/test_dsp.cc")
host_test("test_kernel", "tests/test_kernel.cc")
host_test("test_audio", "tests/test_audio.cc")
host_test("test_midi", "tests/test_midi.cc")
host_test("test_midi_lib", "tests/test_midi_lib.cc")
host_test("test_umidi_comprehensive", "tests/test_umidi_comprehensive.cc")

-- umidi library tests (default = false)
local umidi_dirs = {"lib/umidi/include", "lib/umiboot/include"}
host_test("umidi_test_core", "lib/umidi/test/test_core.cc", {group = "tests/umidi", default = false, includedirs = umidi_dirs})
host_test("umidi_test_messages", "lib/umidi/test/test_messages.cc", {group = "tests/umidi", default = false, includedirs = umidi_dirs})
host_test("umidi_test_protocol", "lib/umidi/test/test_protocol.cc", {group = "tests/umidi", default = false, includedirs = umidi_dirs})
host_test("umidi_test_extended", "lib/umidi/test/test_extended_protocol.cc", {group = "tests/umidi", default = false, includedirs = umidi_dirs})

-- umiboot library tests (default = false)
local umiboot_dirs = {"lib/umiboot/include"}
host_test("umiboot_test_auth", "lib/umiboot/test/test_auth.cc", {group = "tests/umiboot", default = false, includedirs = umiboot_dirs})
host_test("umiboot_test_firmware", "lib/umiboot/test/test_firmware.cc", {group = "tests/umiboot", default = false, includedirs = umiboot_dirs})
host_test("umiboot_test_session", "lib/umiboot/test/test_session.cc", {group = "tests/umiboot", default = false, includedirs = umiboot_dirs})

-- =====================================================================
-- ARM Firmware Targets (using embedded rule from arm-embedded package)
-- =====================================================================
-- Configuration is handled by the 'embedded' rule:
--   - MCU: stm32f407vg (STM32F4 Cortex-M4F with FPU)
--   - Toolchain: gcc-arm (default) or clang-arm
--   - Outputs: .elf, .hex, .bin, .map

-- Helper: Renode runner
local function renode_run(script)
    return function(target)
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        os.execv(renode, {"--console", "--disable-xwt", "-e", "include @renode/" .. script})
    end
end

-- Common paths for STM32F4 BSP
local stm32f4_bsp = "lib/bsp/stm32f4-disco"
local stm32f4_linker = stm32f4_bsp .. "/linker.ld"
local stm32f4_syscalls = stm32f4_bsp .. "/syscalls.cc"

-- Common include paths for embedded targets
local embedded_includedirs = {
    ".",
    "lib",
    "lib/umios/core",
    "lib/umios/kernel",
    "lib/umios/adapter",
    "lib/umios/backend/cm",
    "lib/bsp/stm32f4-disco",
    "lib/hal/stm32",
    "lib/umidi/include",
    "lib/umiboot/include",
    "lib/umidsp/include",
}

target("firmware")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)

    add_deps("umios")
    add_includedirs(embedded_includedirs)
    add_defines("STM32F4", "BOARD_STM32F4")

    add_files(stm32f4_syscalls)
    add_files("examples/embedded/example_app.cc")
target_end()

target("renode_test")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")

    add_deps("umios")
    add_includedirs(embedded_includedirs)
    add_defines("STM32F4", "BOARD_STM32F4")

    add_files("tests/renode_test.cc")
    add_files(stm32f4_syscalls)

    on_run(renode_run("test.resc"))
target_end()

target("bench_midi_format")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")

    add_includedirs(embedded_includedirs)
    add_defines("STM32F4", "BOARD_STM32F4")

    add_files("tests/bench_midi_format.cc")
    add_files(stm32f4_syscalls)

    on_run(renode_run("bench_midi.resc"))
target_end()

target("umidi_test_renode")
    set_group("tests/umidi")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")

    add_includedirs(embedded_includedirs)
    add_defines("STM32F4", "BOARD_STM32F4")

    add_files("lib/umidi/test/test_renode.cc")
    add_files(stm32f4_syscalls)

    on_run(renode_run("umidi_test.resc"))
target_end()

target("synth_example")
    set_group("examples")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")

    add_deps("umios")
    add_includedirs(embedded_includedirs)
    add_defines("STM32F4", "BOARD_STM32F4")

    add_files("examples/synth/synth_example.cc")
    add_files(stm32f4_syscalls)

    on_run(renode_run("synth.resc"))
target_end()

target("synth_renode")
    set_group("examples")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")

    add_deps("umios")
    add_includedirs(embedded_includedirs)
    add_defines("STM32F4", "BOARD_STM32F4")

    add_files("examples/synth/synth_renode.cc")
    add_files(stm32f4_syscalls)

    on_run(renode_run("synth_audio.resc"))
target_end()

-- =====================================================================
-- WASM Targets (Emscripten)
-- =====================================================================

-- Check for Emscripten by checking common paths
local has_emscripten = os.getenv("EMSDK") ~= nil 
    or os.isfile("/opt/homebrew/bin/emcc")
    or os.isfile("/usr/local/bin/emcc")
    or os.isfile("/usr/bin/emcc")

if has_emscripten then

rule("wasm-worklet")
    on_load(function (target)
        target:set("plat", "wasm")
        target:set("arch", "wasm32")
        
        -- Embedded C++ flags
        target:add("cxflags", "-fno-exceptions", "-fno-rtti", {force = true})
        target:add("cxflags", "-O3", {force = true})
        
        -- WASM specific flags
        target:add("ldflags", "-sWASM=1", {force = true})
        target:add("ldflags", "-sALLOW_MEMORY_GROWTH=1", {force = true})
        target:add("ldflags", "-sSTACK_SIZE=65536", {force = true})
        target:add("ldflags", "-sINITIAL_MEMORY=1048576", {force = true})
        
        -- AudioWorklet support
        target:add("ldflags", "-sAUDIO_WORKLET=1", {force = true})
        target:add("ldflags", "-sWASM_WORKERS=1", {force = true})
        
        -- Export functions for JavaScript
        target:add("ldflags", "-sEXPORTED_FUNCTIONS=['_malloc','_free','_umi_create','_umi_destroy','_umi_process','_umi_process_midi','_umi_set_param']", {force = true})
        target:add("ldflags", "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']", {force = true})
        
        -- Modular output
        target:add("ldflags", "-sMODULARIZE=1", {force = true})
        target:add("ldflags", "-sEXPORT_ES6=1", {force = true})
        target:add("ldflags", "-sENVIRONMENT=web,worker,node", {force = true})
    end)
rule_end()

-- UMIM exported functions (generated by UMIM_EXPORT macro)
local umim_exported_funcs = "[" .. table.concat({
    "'_malloc'", "'_free'",
    -- Lifecycle
    "'_umi_create'",
    -- Audio processing
    "'_umi_process'",
    -- Parameter API
    "'_umi_set_param'", "'_umi_get_param'", "'_umi_get_param_count'",
    "'_umi_get_param_name'", "'_umi_get_param_min'", "'_umi_get_param_max'",
    "'_umi_get_param_default'", "'_umi_get_param_curve'", "'_umi_get_param_id'",
    "'_umi_get_param_unit'",
    -- Note API (optional, SFINAE)
    "'_umi_note_on'", "'_umi_note_off'", "'_umi_process_cc'",
    -- Plugin info
    "'_umi_get_processor_name'"
}, ",") .. "]"
local umim_runtime_methods = "['ccall','cwrap','UTF8ToString','HEAPF32','HEAP8']"

local function umim_target(name, source_file)
    target("umim_" .. name)
        set_kind("binary")
        set_group("umim")
        set_default(false)
        set_plat("wasm")
        set_arch("wasm32")
        set_toolchains("emcc")
        set_targetdir("build/umim")
        set_filename(name .. ".js")

        add_includedirs("lib/umios/core", "lib/umios/kernel", "lib/umios/adapter", "lib", "lib/umidsp/include", "examples/synth")
        add_files(source_file)

        add_cxflags("-fno-exceptions", "-fno-rtti", "-O3", {force = true})
        add_ldflags("-sWASM=1", "-sALLOW_MEMORY_GROWTH=1", {force = true})
        add_ldflags("-sEXPORTED_FUNCTIONS=" .. umim_exported_funcs, {force = true})
        add_ldflags("-sEXPORTED_RUNTIME_METHODS=" .. umim_runtime_methods, {force = true})
        -- AudioWorklet compatible: synchronous WASM init
        add_ldflags("-sENVIRONMENT=web,worker", {force = true})
        add_ldflags("-sWASM_ASYNC_COMPILATION=0", {force = true})
        add_ldflags("--pre-js=examples/workbench/worklet/worklet-preamble.js", {force = true})
        add_ldflags("--post-js=examples/workbench/worklet/umi-processor-sync.js", {force = true})
    target_end()
end

-- UMIM Modules
umim_target("synth", "examples/synth/synth_wasm.cc")
umim_target("delay", "examples/workbench/umim/delay/delay_wasm.cc")
umim_target("volume", "examples/workbench/umim/volume/volume.cc")

-- =====================================================================
-- Synth Simulator (umios simulation on Web)
-- =====================================================================
-- This is different from umim_synth - it simulates the embedded environment
-- including the umios kernel, rather than just exposing a UMIM plugin.

local sim_exported_funcs = "[" .. table.concat({
    "'_malloc'", "'_free'",
    -- Simulation API
    "'_umi_sim_init'", "'_umi_sim_reset'", "'_umi_sim_process'",
    "'_umi_sim_note_on'", "'_umi_sim_note_off'", "'_umi_sim_cc'", "'_umi_sim_midi'",
    "'_umi_sim_load'", "'_umi_sim_position_lo'", "'_umi_sim_position_hi'",
    "'_umi_sim_get_name'", "'_umi_sim_get_vendor'", "'_umi_sim_get_version'",
    -- UMIM-compatible API (for standard worklet usage)
    "'_umi_create'", "'_umi_destroy'", "'_umi_process'",
    "'_umi_note_on'", "'_umi_note_off'",
    "'_umi_get_processor_name'", "'_umi_get_name'", "'_umi_get_vendor'", "'_umi_get_version'",
    "'_umi_get_type'", "'_umi_get_param_count'", "'_umi_set_param'", "'_umi_get_param'",
    "'_umi_get_param_name'", "'_umi_get_param_min'", "'_umi_get_param_max'",
    "'_umi_get_param_default'", "'_umi_get_param_curve'", "'_umi_get_param_id'",
    "'_umi_get_param_unit'", "'_umi_process_cc'"
}, ",") .. "]"

target("synth_sim")
    set_kind("binary")
    set_group("simulator")
    set_default(false)
    set_plat("wasm")
    set_arch("wasm32")
    set_toolchains("emcc")
    set_targetdir("examples/synth")
    set_filename("synth_sim.js")

    add_files("examples/synth/synth_sim.cc")
    add_includedirs(
        "lib/umios/core",
        "lib/umios/kernel",
        "lib/umios/backend/wasm",
        "lib/umidsp/include",
        "lib",
        "examples/synth"
    )

    add_cxflags("-fno-exceptions", "-fno-rtti", "-O3", {force = true})
    add_ldflags("-sWASM=1", {force = true})
    add_ldflags("-sALLOW_MEMORY_GROWTH=0", {force = true})
    add_ldflags("-sSTACK_SIZE=65536", {force = true})
    add_ldflags("-sINITIAL_MEMORY=1048576", {force = true})
    add_ldflags("-sEXPORTED_FUNCTIONS=" .. sim_exported_funcs, {force = true})
    add_ldflags("-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','HEAPF32','HEAP8']", {force = true})
    add_ldflags("-sENVIRONMENT=web,worker", {force = true})
    add_ldflags("-sWASM_ASYNC_COMPILATION=1", {force = true})
    add_ldflags("-sMODULARIZE=1", {force = true})
    add_ldflags("-sEXPORT_NAME='createSynthModule'", {force = true})
target_end()

end  -- if has_emscripten

-- =====================================================================
-- Tasks
-- =====================================================================

-- Run all host tests
task("test")
    set_category("action")
    on_run(function ()
        import("core.project.project")

        local tests = {"test_dsp", "test_kernel", "test_audio", "test_midi"}

        -- Build each test target individually
        for _, name in ipairs(tests) do
            os.exec("xmake build " .. name)
        end

        print("\n" .. string.rep("=", 60))
        print("Running Host Unit Tests")
        print(string.rep("=", 60) .. "\n")

        local failed = {}

        for _, name in ipairs(tests) do
            print(">>> " .. name)
            local target = project.target(name)
            local ok = os.execv(target:targetfile(), {}, {try = true})
            if ok ~= 0 then
                table.insert(failed, name)
            end
            print("")
        end

        print(string.rep("=", 60))
        if #failed > 0 then
            print("FAILED: " .. table.concat(failed, ", "))
            os.exit(1)
        else
            print("All tests passed!")
        end
    end)
    set_menu {
        usage = "xmake test",
        description = "Run all host unit tests"
    }
task_end()

-- Interactive Renode session
task("renode")
    set_category("action")
    on_run(function ()
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        
        print("Starting Renode emulator (interactive)...")
        os.execv(renode, {"renode/run.resc"})
    end)
    set_menu {
        usage = "xmake renode",
        description = "Start Renode emulator (interactive)"
    }
task_end()

-- Automated Renode test
task("renode-test")
    set_category("action")
    on_run(function ()
        print("Building firmware for Renode...")
        os.exec("xmake build renode_test")
        
        print("\nRunning Renode automated tests...")
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        
        os.execv(renode, {"--console", "--disable-xwt", "-e", "include @renode/test.resc"})
        
        -- Display results
        local logfile = "build/renode_uart.log"
        if os.isfile(logfile) then
            print("\n" .. string.rep("=", 60))
            print("UART Output")
            print(string.rep("=", 60))
            os.exec("cat " .. logfile)
        end
    end)
    set_menu {
        usage = "xmake renode-test",
        description = "Run Renode automated tests"
    }
task_end()

-- Synth example with Renode
task("renode-synth")
    set_category("action")
    on_run(function ()
        print("Building synth example for Renode...")
        os.exec("xmake build synth_example")

        print("\nRunning Synth example in Renode...")
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end

        os.execv(renode, {"--console", "--disable-xwt", "-e", "include @renode/synth.resc"})

        -- Display results
        local logfile = "build/synth_uart.log"
        if os.isfile(logfile) then
            print("\n" .. string.rep("=", 60))
            print("UART Output")
            print(string.rep("=", 60))
            os.exec("cat " .. logfile)
        end
    end)
    set_menu {
        usage = "xmake renode-synth",
        description = "Run synth example in Renode emulator"
    }
task_end()

-- Robot Framework tests
task("robot")
    set_category("action")
    on_run(function ()
        print("Building firmware for Renode...")
        os.exec("xmake build renode_test")
        
        -- Find renode-test runner
        local runner = "/Applications/Renode.app/Contents/MacOS/renode-test"
        if not os.isfile(runner) then
            runner = os.which("renode-test") or "renode-test"
        end
        if not os.isfile(runner) and not os.which("renode-test") then
            print("ERROR: renode-test not found")
            print("Install: pip install -r /Applications/Renode.app/Contents/MacOS/tests/requirements.txt")
            os.exit(1)
        end
        
        -- Setup Python venv in PATH
        local venv = path.join(os.projectdir(), ".venv", "bin")
        local env_path = os.getenv("PATH") or ""
        if os.isdir(venv) then
            env_path = venv .. ":" .. env_path
        end
        
        print("\nRunning Robot Framework tests...")
        os.exec(string.format(
            "cd '%s' && env PATH=\"%s\" %s renode/umi_tests_simple.robot -r build",
            os.projectdir(), env_path, runner
        ))
        
        -- Summary
        local xml = "build/robot_output.xml"
        if os.isfile(xml) then
            local f = io.open(xml, "r")
            if f then
                local content = f:read("*a")
                f:close()
                print("\n" .. string.rep("=", 60))
                if content:find('status="PASS"') then
                    print("Robot tests PASSED ✓")
                else
                    print("Robot tests FAILED ✗")
                    print("See: build/log.html")
                end
            end
        end
    end)
    set_menu {
        usage = "xmake robot",
        description = "Run Robot Framework tests"
    }
task_end()

-- Clean all artifacts
task("clean-all")
    set_category("action")
    on_run(function ()
        print("Cleaning all build artifacts...")
        os.tryrm("build")
        os.tryrm(".xmake")
        print("Done ✓")
    end)
    set_menu {
        usage = "xmake clean-all",
        description = "Remove all build artifacts and cache"
    }
task_end()

-- Show configuration
task("info")
    set_category("action")
    on_run(function ()
        print("UMI-OS Build Configuration")
        print(string.rep("=", 40))
        print("Board:   " .. (get_config("board") or "stm32f4"))
        print("Kernel:  " .. (get_config("kernel") or "mono"))
        print("Mode:    " .. (get_config("mode") or "release"))
        print("Buildir: " .. (get_config("buildir") or "build"))
        print(string.rep("=", 40))
        
        -- Check toolchain (use known path)
        local arm_gcc = "/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin/arm-none-eabi-gcc"
        if os.isfile(arm_gcc) or os.which("arm-none-eabi-gcc") then
            print("ARM GCC: ✓ Available")
        else
            print("ARM GCC: ✗ Not found")
        end
        
        -- Check Renode
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if os.isfile(renode) then
            print("Renode:  ✓ Available")
        else
            print("Renode:  ✗ Not found")
        end
        
        -- Check Emscripten
        if os.which("emcc") then
            print("Emcc:    ✓ Available")
        else
            print("Emcc:    ✗ Not found (install: brew install emscripten)")
        end
    end)
    set_menu {
        usage = "xmake info",
        description = "Show build configuration"
    }
task_end()

-- WASM build
task("wasm")
    set_category("action")
    on_run(function ()
        print("Building UMIM modules...")
        os.exec("xmake build umim_synth umim_delay umim_volume")

        print("\n" .. string.rep("=", 60))
        print("WASM build complete!")
        print("Output: build/umim/")
        print(string.rep("=", 60))

        -- List generated files
        os.exec("ls -la build/umim/")
    end)
    set_menu {
        usage = "xmake wasm",
        description = "Build UMIM WASM modules"
    }
task_end()

-- WASM test (headless Node.js)
task("wasm-test")
    set_category("action")
    on_run(function ()
        print("Building UMIM modules...")
        os.exec("xmake build umim_synth umim_delay umim_volume")

        print("\nRunning WASM tests in Node.js...")
        os.exec("node test/test-headless.mjs")
    end)
    set_menu {
        usage = "xmake wasm-test",
        description = "Run WASM tests in Node.js"
    }
task_end()

-- WASM serve for workbench
task("wasm-serve")
    set_category("action")
    on_run(function ()
        print("Building UMIM modules...")
        os.exec("xmake build umim_synth umim_delay umim_volume")

        print("\nStarting local server...")
        print("Open: http://localhost:8080/")
        os.exec("cd examples/workbench && python3 -m http.server 8080")
    end)
    set_menu {
        usage = "xmake wasm-serve",
        description = "Build and serve WASM workbench"
    }
task_end()
