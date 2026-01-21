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
set_version("0.2.0")
set_xmakever("2.8.0")

-- =====================================================================
-- Custom Package Repository (ARM Embedded Toolchain)
-- =====================================================================

if os.isdir(".tools/arm-embedded-xmake-repo") then
    add_repositories("arm-embedded .tools/arm-embedded-xmake-repo")
else
    add_repositories("arm-embedded https://github.com/tekitounix/arm-embedded-xmake-repo.git")
end

add_requires("arm-embedded", {optional = true})

-- =====================================================================
-- Global Configuration
-- =====================================================================

set_languages("c++23")
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
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

if has_config("coverage") and is_mode("debug") then
    add_cxflags("--coverage", {force = true})
    add_ldflags("--coverage", {force = true})
end

-- =====================================================================
-- UMI Package (provides all library include paths)
-- =====================================================================

includes("lib/umi")

-- Legacy umios target for backward compatibility
target("umios")
    set_kind("headeronly")
    set_group("libraries")
    add_deps("umi.core")
    if is_config("kernel", "micro") then
        add_defines("UMI_KERNEL_MICRO", {public = true})
    end
target_end()

-- =====================================================================
-- Host Tests (using host.test rule from arm-embedded)
-- =====================================================================

-- Main host tests (use umi.all for all library includes)
for _, test in ipairs({
    {"test_dsp", "lib/umidsp/test/test_dsp.cc"},
    {"test_kernel", "tests/test_kernel.cc"},
    {"test_audio", "tests/test_audio.cc"},
    {"test_midi", "tests/test_midi.cc"},
    {"test_midi_lib", "tests/test_midi_lib.cc"},
    {"test_umidi_comprehensive", "tests/test_umidi_comprehensive.cc"},
    {"test_concepts", "tests/test_concepts.cc"},
}) do
    target(test[1])
        add_rules("host.test")
        set_default(true)
        add_deps("umi.all")
        add_files(test[2])
        add_includedirs("tests")
        add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    target_end()
end

-- umidi library tests
for _, test in ipairs({
    {"umidi_test_core", "lib/umidi/test/test_core.cc"},
    {"umidi_test_messages", "lib/umidi/test/test_messages.cc"},
    {"umidi_test_protocol", "lib/umidi/test/test_protocol.cc"},
    {"umidi_test_extended", "lib/umidi/test/test_extended_protocol.cc"},
}) do
    target(test[1])
        add_rules("host.test")
        set_group("tests/umidi")
        add_deps("umi.midi", "umi.boot")
        add_files(test[2])
        add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    target_end()
end

-- umiboot library tests
for _, test in ipairs({
    {"umiboot_test_auth", "lib/umiboot/test/test_auth.cc"},
    {"umiboot_test_firmware", "lib/umiboot/test/test_firmware.cc"},
    {"umiboot_test_session", "lib/umiboot/test/test_session.cc"},
}) do
    target(test[1])
        add_rules("host.test")
        set_group("tests/umiboot")
        add_deps("umi.boot")
        add_files(test[2])
        add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    target_end()
end

-- =====================================================================
-- ARM Firmware Targets (using embedded rule from arm-embedded)
-- =====================================================================

local stm32f4_bsp = "lib/bsp/stm32f4-disco"
local stm32f4_linker = stm32f4_bsp .. "/linker.ld"
local stm32f4_syscalls = stm32f4_bsp .. "/syscalls.cc"

-- Helper: Create STM32F4 embedded target
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
        -- Use umi.embedded.full for all embedded targets
        add_deps("umi.embedded.full")
        if opts.deps then
            add_deps(opts.deps)
        end
        add_defines("STM32F4", "BOARD_STM32F4")
        add_files(stm32f4_syscalls)
        add_files(opts.source)
        if opts.renode_script then
            on_run(function(target)
                local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
                if not os.isfile(renode) then renode = "renode" end
                os.execv(renode, {"--console", "--disable-xwt", "-e", "include @renode/" .. opts.renode_script})
            end)
        end
    target_end()
end

stm32f4_target("firmware", {
    source = "examples/embedded/example_app.cc",
    deps = "umios"
})

stm32f4_target("renode_test_legacy", {
    source = "tests/renode_test.cc",
    deps = "umios",
    optimize = "size",
    renode_script = "test.resc"
})

-- New Renode test with syscall/MPU support
target("renode_test")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", "port/renode/stm32f4/linker.ld")
    set_values("embedded.optimize", "size")
    -- Platform-specific includes (cm/platform/*.hh)
    add_includedirs("lib/umios/backend/cm")
    -- Kernel/driver includes
    add_includedirs("lib/umios")
    add_includedirs("lib")
    add_deps("umi.core", "umi.dsp")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files("examples/renode_test/startup.cc")
    add_files("examples/renode_test/main.cc")
    on_run(function(target)
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        os.execv(renode, {"--console", "--disable-xwt", "-e",
            "include @port/renode/stm32f4/platform.resc; loadUmi; start"})
    end)
target_end()

stm32f4_target("bench_midi_format", {
    source = "tests/bench_midi_format.cc",
    optimize = "size",
    renode_script = "bench_midi.resc"
})

stm32f4_target("umidi_test_renode", {
    source = "lib/umidi/test/test_renode.cc",
    group = "tests/umidi",
    optimize = "size",
    renode_script = "umidi_test.resc"
})

-- Note: synth_example and synth_renode moved to examples/_archive
-- These targets are now part of headless_webhost project

-- =====================================================================
-- WASM Targets (Emscripten)
-- =====================================================================

local has_emscripten = os.getenv("EMSDK") ~= nil
    or os.isfile("/opt/homebrew/bin/emcc")
    or os.isfile("/usr/local/bin/emcc")
    or os.isfile("/usr/bin/emcc")

if has_emscripten then

-- Include headless_webhost subproject
includes("examples/headless_webhost")

end  -- if has_emscripten

-- =====================================================================
-- STM32F4 Kernel + Application (Separated Architecture)
-- =====================================================================

includes("examples/stm32f4_kernel")
includes("examples/synth_app")

-- =====================================================================
-- Tasks
-- =====================================================================

task("test")
    set_category("action")
    on_run(function ()
        import("core.project.project")
        local tests = {"test_dsp", "test_kernel", "test_audio", "test_midi"}
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
            if ok ~= 0 then table.insert(failed, name) end
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
    set_menu {usage = "xmake test", description = "Run all host unit tests"}
task_end()

task("renode")
    set_category("action")
    on_run(function ()
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        print("Starting Renode emulator (interactive)...")
        os.execv(renode, {"renode/run.resc"})
    end)
    set_menu {usage = "xmake renode", description = "Start Renode emulator (interactive)"}
task_end()

task("renode-test")
    set_category("action")
    on_run(function ()
        print("Building firmware for Renode...")
        os.exec("xmake build renode_test")
        print("\nRunning Renode automated tests...")
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        os.execv(renode, {"--console", "--disable-xwt", "-e", "include @renode/test.resc"})
        local logfile = "build/renode_uart.log"
        if os.isfile(logfile) then
            print("\n" .. string.rep("=", 60))
            print("UART Output")
            print(string.rep("=", 60))
            os.exec("cat " .. logfile)
        end
    end)
    set_menu {usage = "xmake renode-test", description = "Run Renode automated tests"}
task_end()

task("robot")
    set_category("action")
    on_run(function ()
        print("Building firmware for Renode...")
        os.exec("xmake build renode_test")
        local runner = "/Applications/Renode.app/Contents/MacOS/renode-test"
        if not os.isfile(runner) then
            runner = os.which("renode-test") or "renode-test"
        end
        if not os.isfile(runner) and not os.which("renode-test") then
            print("ERROR: renode-test not found")
            print("Install: pip install -r /Applications/Renode.app/Contents/MacOS/tests/requirements.txt")
            os.exit(1)
        end
        local venv = path.join(os.projectdir(), ".venv", "bin")
        local env_path = os.getenv("PATH") or ""
        if os.isdir(venv) then env_path = venv .. ":" .. env_path end
        print("\nRunning Robot Framework tests...")
        os.exec(string.format("cd '%s' && env PATH=\"%s\" %s renode/umi_tests_simple.robot -r build", os.projectdir(), env_path, runner))
        local xml = "build/robot_output.xml"
        if os.isfile(xml) then
            local f = io.open(xml, "r")
            if f then
                local content = f:read("*a")
                f:close()
                print("\n" .. string.rep("=", 60))
                print(content:find('status="PASS"') and "Robot tests PASSED" or "Robot tests FAILED\nSee: build/log.html")
            end
        end
    end)
    set_menu {usage = "xmake robot", description = "Run Robot Framework tests"}
task_end()

task("clean-all")
    set_category("action")
    on_run(function ()
        print("Cleaning all build artifacts...")
        os.tryrm("build")
        os.tryrm(".xmake")
        print("Done")
    end)
    set_menu {usage = "xmake clean-all", description = "Remove all build artifacts and cache"}
task_end()

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
        local arm_gcc = "/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin/arm-none-eabi-gcc"
        print("ARM GCC: " .. ((os.isfile(arm_gcc) or os.which("arm-none-eabi-gcc")) and "Available" or "Not found"))
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        print("Renode:  " .. (os.isfile(renode) and "Available" or "Not found"))
        print("Emcc:    " .. (os.which("emcc") and "Available" or "Not found"))
    end)
    set_menu {usage = "xmake info", description = "Show build configuration"}
task_end()

task("webhost")
    set_category("action")
    on_run(function ()
        print("Building headless web host...")
        os.exec("xmake build headless_webhost")
        -- Copy to web directory
        os.cp("examples/headless_webhost/build/webhost_sim.js", "examples/headless_webhost/web/")
        os.cp("examples/headless_webhost/build/webhost_sim.wasm", "examples/headless_webhost/web/")
        print("\n" .. string.rep("=", 60))
        print("Web host build complete!")
        print("Output: examples/headless_webhost/build/")
        print(string.rep("=", 60))
    end)
    set_menu {usage = "xmake webhost", description = "Build headless web host WASM module"}
task_end()

task("webhost-serve")
    set_category("action")
    on_run(function ()
        print("Building headless web host...")
        os.exec("xmake build headless_webhost")
        -- Copy to web directory
        os.cp("examples/headless_webhost/build/webhost_sim.js", "examples/headless_webhost/web/")
        os.cp("examples/headless_webhost/build/webhost_sim.wasm", "examples/headless_webhost/web/")
        print("\nStarting local server...")
        print("Open: http://localhost:8080/")
        os.exec("cd examples/headless_webhost/web && python3 -m http.server 8080")
    end)
    set_menu {usage = "xmake webhost-serve", description = "Build and serve headless web host"}
task_end()
