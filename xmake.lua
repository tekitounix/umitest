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

if os.isdir(".refs/arm-embedded-xmake-repo") then
    add_repositories("arm-embedded .refs/arm-embedded-xmake-repo")
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

-- Set UMIOS_BUILD_TYPE based on build_type option
if is_config("build_type", "release") then
    add_defines("UMIOS_BUILD_TYPE=umi::kernel::BuildType::RELEASE", {public = true})
else
    add_defines("UMIOS_BUILD_TYPE=umi::kernel::BuildType::DEVELOPMENT", {public = true})
end

-- =====================================================================
-- UMI Package (provides all library include paths)
-- =====================================================================

includes("lib/umi")
includes("lib/umi/test")
includes("lib/umi/ref")
includes("lib/umi/fs")
includes("lib/umi/fs/test")
includes("lib/umi/usb/test")
includes("lib/umi/port")
includes("lib/umi/port/test")
includes("lib/umi/kernel/test")
includes("lib/umi/core/test")
includes("lib/umi/crypto/test")

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

-- NOTE: All tests have been moved to lib/<name>/test/
-- DSP test (using umitest framework)
target("test_dsp")
    add_rules("host.test")
    set_default(true)
    add_deps("umi.all", "umitest")
    add_files("lib/umi/dsp/test/test_dsp.cc")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- umidi library tests
for _, test in ipairs({
    {"umidi_test_core", "lib/umi/midi/test/test_core.cc"},
    {"umidi_test_messages", "lib/umi/midi/test/test_messages.cc"},
    {"umidi_test_protocol", "lib/umi/midi/test/test_protocol.cc"},
    {"umidi_test_extended", "lib/umi/midi/test/test_extended_protocol.cc"},
}) do
    target(test[1])
        add_rules("host.test")
        set_group("tests/umidi")
        add_deps("umi.midi", "umi.boot", "umitest")
        add_files(test[2])
        add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    target_end()
end

-- umiboot library tests
for _, test in ipairs({
    {"umiboot_test_auth", "lib/umi/boot/test/test_auth.cc"},
    {"umiboot_test_firmware", "lib/umi/boot/test/test_firmware.cc"},
    {"umiboot_test_session", "lib/umi/boot/test/test_session.cc"},
}) do
    target(test[1])
        add_rules("host.test")
        set_group("tests/umiboot")
        add_deps("umi.boot", "umitest")
        add_files(test[2])
        add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    target_end()
end

-- =====================================================================
-- ARM Firmware Targets (using embedded rule from arm-embedded)
-- =====================================================================

local stm32f4_linker = "lib/umi/port/mcu/stm32f4/linker.ld"
local stm32f4_syscalls = "lib/umi/port/mcu/stm32f4/syscalls.cc"

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

-- Renode test with syscall/MPU support
target("renode_test")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", "tools/renode/linker.ld")
    set_values("embedded.optimize", "size")
    -- Kernel/driver includes
    add_includedirs("lib/umi")
    add_includedirs("lib")
    add_deps("umi.core", "umi.dsp")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files("examples/renode_test/startup.cc")
    add_files("examples/renode_test/main.cc")
    on_run(function(target)
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        os.execv(renode, {"--console", "--disable-xwt", "-e",
            "include @tools/renode/platform.resc; loadUmi; start"})
    end)
target_end()

stm32f4_target("bench_midi_format", {
    source = "lib/umi/midi/bench/bench_midi_format.cc",
    optimize = "size",
    renode_script = "bench_midi.resc"
})

stm32f4_target("bench_diode_ladder", {
    source = "lib/umi/dsp/bench/bench_diode_ladder.cc",
    optimize = "fast",
    renode_script = "tools/renode/bench_diode_ladder.resc"
})

stm32f4_target("bench_waveshaper", {
    source = "lib/umi/dsp/bench/bench_waveshaper.cc",
    optimize = "fast",
    renode_script = "tools/renode/bench_waveshaper.resc"
})

stm32f4_target("bench_waveshaper_fast", {
    source = "lib/umi/dsp/bench/bench_waveshaper_fast.cc",
    optimize = "fast",
    renode_script = "tools/renode/bench_waveshaper_fast.resc"
})

stm32f4_target("umidi_test_renode", {
    source = "lib/umi/midi/test/test_renode.cc",
    group = "tests/umidi",
    optimize = "size",
    renode_script = "umidi_test.resc"
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
    add_files("lib/umi/ref/test/test_mock_renode.cc")
    add_includedirs("lib/umi/ref/include")
target_end()

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

-- umimock WASM test
target("umimock_wasm")
    set_kind("binary")
    set_default(false)
    set_group("wasm")
    set_languages("c++23")
    set_plat("wasm")
    set_arch("wasm32")
    set_toolchains("emcc")
    set_filename("umimock_wasm.js")
    add_files("lib/umi/ref/test/test_mock_wasm.cc")
    add_includedirs("lib/umi/ref/include")
    add_cxflags("-fno-exceptions", "-fno-rtti", {force = true})
    add_ldflags("-sEXPORTED_FUNCTIONS=['_main','_umimock_constant','_umimock_ramp_first','_umimock_set_and_get','_umimock_reset_value','_umimock_fill_buffer_check']", {force = true})
    add_ldflags("-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']", {force = true})
    add_ldflags("-sMODULARIZE=1", "-sEXPORT_NAME='createUmimock'", {force = true})
target_end()

end  -- if has_emscripten

-- =====================================================================
-- STM32F4 Kernel + Application (Separated Architecture)
-- =====================================================================

includes("examples/stm32f4_kernel")
includes("examples/synth_app")
includes("examples/daisy_pod_kernel")
includes("examples/daisy_pod_synth_h7")

-- =====================================================================
-- Tasks
-- =====================================================================

task("test")
    set_category("action")
    on_run(function ()
        import("core.project.project")
        local tests = {
            "test_dsp",
            "test_umios_kernel",
            "test_umios_audio",
            "test_umios_midi",
            "test_umios_concepts",
            "test_umios_syscall",
            "test_umios_crypto"
        }
        for _, name in ipairs(tests) do
            os.exec("xmake build " .. name)
        end
        print("\n" .. string.rep("=", 60))
        print("Running Host Unit Tests")
        print(string.rep("=", 60) .. "\n")
        local failed = {}
        for _, name in ipairs(tests) do
            print(">>> " .. name)
            local ok = try { function() os.exec("xmake run " .. name) return true end }
            if not ok then table.insert(failed, name) end
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

-- =====================================================================
-- Python Bindings (pybind11)
-- =====================================================================
-- TB-303 WaveShaper Python module
-- Build: xmake build tb303_waveshaper_py
-- Install: cp build/.../tb303_waveshaper.*.so docs/dsp/tb303/vco/test/

-- Check for Python and pybind11
local has_python = os.getenv("PYTHON") ~= nil
    or os.isfile("/usr/bin/python3")
    or os.isfile("/opt/homebrew/bin/python3")
    or os.isfile("/usr/local/bin/python3")
    or os.which("python3") ~= nil

if has_python then

target("tb303_waveshaper_py")
    set_kind("shared")
    set_group("python")
    set_default(false)
    set_languages("c++17")
    add_files("docs/dsp/tb303/vco/code/waveshaper_pybind.cpp")
    add_includedirs("docs/dsp/tb303/vco/code")

    -- Python extension settings
    set_prefixname("")  -- Remove lib prefix

    on_load(function (target)
        local python = os.getenv("PYTHON") or "python3"

        -- Get Python extension suffix
        local suffix = os.iorun(python .. " -c \"import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))\"")
        if suffix then
            suffix = suffix:gsub("%s+", "")
            target:set("extension", suffix)
        else
            -- Fallback
            if is_plat("macosx") then
                target:set("extension", ".cpython-311-darwin.so")
            else
                target:set("extension", ".so")
            end
        end

        -- Get Python include path
        local includes = os.iorun(python .. " -c \"import sysconfig; print(sysconfig.get_path('include'))\"")
        if includes then
            includes = includes:gsub("%s+", "")
            target:add("includedirs", includes)
        end

        -- Get pybind11 include path (installed via pip)
        try {
            function()
                local pybind_inc = os.iorun(python .. " -c \"import pybind11; print(pybind11.get_include())\"")
                if pybind_inc then
                    pybind_inc = pybind_inc:gsub("%s+", "")
                    target:add("includedirs", pybind_inc)
                end
            end,
            catch {
                function(e)
                    -- pybind11 not installed, skip
                end
            }
        }
    end)

    -- Don't add Python library linkage on macOS (undefined dynamic lookup)
    if is_plat("macosx") then
        add_ldflags("-undefined", "dynamic_lookup", {force = true})
    end
target_end()

task("waveshaper-py")
    set_category("action")
    on_run(function ()
        print("Building TB-303 WaveShaper Python module...")
        os.exec("xmake build tb303_waveshaper_py")

        -- Find and copy the built module
        import("core.project.project")
        local target = project.target("tb303_waveshaper_py")
        local targetfile = target:targetfile()
        local destdir = "docs/dsp/tb303/vco/test"

        if os.isfile(targetfile) then
            os.cp(targetfile, destdir .. "/")
            print("\n" .. string.rep("=", 60))
            print("Python module built successfully!")
            print("Copied to: " .. destdir)
            print("\nUsage:")
            print("  cd " .. destdir)
            print("  python3 -c \"import tb303_waveshaper as ws; print(ws.V_T)\"")
            print(string.rep("=", 60))
        else
            print("ERROR: Build failed, target file not found: " .. targetfile)
        end
    end)
    set_menu {usage = "xmake waveshaper-py", description = "Build TB-303 WaveShaper Python module"}
task_end()

end  -- if has_python

-- =====================================================================
-- Filesystem check: tests + benchmarks + size comparison
-- =====================================================================

task("fs-check")
    set_category("action")
    on_run(function ()
        import("core.project.project")
        local sep = string.rep("=", 70)

        -- ---------------------------------------------------------------
        -- 1. Host unit tests
        -- ---------------------------------------------------------------
        local tests = {"test_fs_fat", "test_fs_slim"}
        for _, name in ipairs(tests) do
            os.exec("xmake build " .. name)
        end
        print("\n" .. sep)
        print("  1. Host Unit Tests")
        print(sep .. "\n")
        local failed = {}
        for _, name in ipairs(tests) do
            print(">>> " .. name)
            local ok = os.execv("xmake", {"run", name}, {try = true})
            if ok ~= 0 then table.insert(failed, name) end
            print("")
        end
        if #failed > 0 then
            print("FAILED: " .. table.concat(failed, ", "))
            os.exit(1)
        end
        print("All unit tests passed!\n")

        -- ---------------------------------------------------------------
        -- 2. Renode benchmarks (slim + fat_cr, then lfs_ref + fat_ref)
        -- ---------------------------------------------------------------
        print(sep)
        print("  2. Renode Benchmarks (DWT cycles, ARM Cortex-M4)")
        print(sep .. "\n")

        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
         if not os.isfile(renode) then renode = "renode" end
         local test_dir = "lib/umi/fs/test"
         local resc = path.join(test_dir, "fs_test.resc")
        local log = "build/renode_fs_uart.log"

        -- slim + fat(cr)
        os.exec("xmake build renode_fs_test")
        os.execv(renode, {"--console", "--disable-xwt", "-e", "include @" .. resc})
        local log_cr = ""
        if os.isfile(log) then
            log_cr = io.readfile(log)
        end

        -- lfs(ref) + fat(ref)
        os.exec("xmake build renode_fs_test_ref")
        -- Generate .resc pointing to ref ELF
        local resc_ref = "build/fs_test_ref.resc"
        local resc_content = io.readfile(resc)
        resc_content = resc_content:gsub(
            "renode_fs_test/release/renode_fs_test%.elf",
            "renode_fs_test_ref/release/renode_fs_test_ref.elf")
        io.writefile(resc_ref, resc_content)
        os.execv(renode, {"--console", "--disable-xwt", "-e", "include @" .. resc_ref})
        local log_ref = ""
        if os.isfile(log) then
            log_ref = io.readfile(log)
        end

        -- Print both results
        if #log_cr > 0 then
            print("\n--- fat(cr) + slim ---")
            print(log_cr)
        end
        if #log_ref > 0 then
            print("--- lfs(ref) + fat(ref) ---")
            print(log_ref)
        end

        -- ---------------------------------------------------------------
        -- 3. ARM binary size comparison
        -- ---------------------------------------------------------------
        print("\n" .. sep)
        print("  3. ARM Binary Size Comparison (Cortex-M4, -Oz)")
        print(sep .. "\n")

        local size_targets = {"size_fs_slim", "size_fs_lfs_ref", "size_fs_fat", "size_fs_fat_ref"}
        for _, name in ipairs(size_targets) do
            os.exec("xmake build " .. name)
        end

        local size_cmd = "arm-none-eabi-size"
        local labels = {
            size_fs_slim    = "slim",
            size_fs_lfs_ref = "lfs(ref)",
            size_fs_fat     = "fat(cr)",
            size_fs_fat_ref = "fat(ref)",
        }
        -- Print header
        printf("  %-10s %8s %8s %8s %8s\n", "", ".text", ".data", ".bss", "total")
        printf("  %-10s %8s %8s %8s %8s\n", "----------", "--------", "--------", "--------", "--------")
        for _, name in ipairs(size_targets) do
            local target = project.target(name)
            local elf = target:targetfile()
            local output = os.iorunv(size_cmd, {elf})
            -- Parse second line: text data bss dec hex filename
            for line in output:gmatch("[^\n]+") do
                local t, d, b, dec = line:match("^%s*(%d+)%s+(%d+)%s+(%d+)%s+(%d+)")
                if t then
                    printf("  %-10s %8s %8s %8s %8s\n", labels[name], t, d, b, dec)
                end
            end
        end

        -- ---------------------------------------------------------------
        -- Summary
        -- ---------------------------------------------------------------
        print("\n" .. sep)
        print("  fs-check complete")
        print(sep)
    end)
    set_menu {usage = "xmake fs-check", description = "Run FS tests, Renode benchmarks, and ARM size comparison"}
task_end()
