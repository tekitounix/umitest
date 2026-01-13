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

-- For local development: add_repositories("arm-embedded .tools/arm-embedded-xmake-repo")
add_repositories("arm-embedded https://github.com/tekitounix/arm-embedded-xmake-repo.git")

-- Request ARM toolchains and embedded support
add_requires("arm-embedded", {optional = true})

-- Request coding-rules for clangd/clang-format/clang-tidy configuration
add_requires("coding-rules", {optional = true})

-- =====================================================================
-- Global Configuration
-- =====================================================================

-- Language standard
set_languages("c++23")

-- Build modes
add_rules("mode.debug", "mode.release")

-- Generate compile_commands.json for clangd/VS Code IntelliSense
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".build"})

-- Generate .vscode/settings.json with clangd configuration
add_rules("embedded.vscode")

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
-- Toolchain: ARM GCC (Cross-compilation)
-- =====================================================================

-- Try to find ARM GCC: first check known paths, then fall back to PATH
local arm_gcc_path = nil
local arm_paths = {
    "/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi",
    "/usr/local/arm-none-eabi",
    "/opt/arm-none-eabi",
    "/opt/homebrew/arm-none-eabi",
}
for _, p in ipairs(arm_paths) do
    if os.isdir(p) then
        arm_gcc_path = p
        break
    end
end

-- If not found in known paths, try to find via PATH
local has_arm_toolchain = arm_gcc_path ~= nil
if not has_arm_toolchain then
    local gcc_path = os.which("arm-none-eabi-gcc")
    if gcc_path then
        -- Extract SDK dir from bin path: /path/to/bin/arm-none-eabi-gcc -> /path/to
        arm_gcc_path = path.directory(path.directory(gcc_path))
        has_arm_toolchain = true
    end
end

if has_arm_toolchain then
    toolchain("arm-none-eabi")
        set_kind("cross")
        set_sdkdir(arm_gcc_path)
        set_bindir(path.join(arm_gcc_path, "bin"))
        
        set_toolset("cc", "arm-none-eabi-gcc")
        set_toolset("cxx", "arm-none-eabi-g++")
        set_toolset("ld", "arm-none-eabi-g++")
        set_toolset("ar", "arm-none-eabi-ar")
        set_toolset("as", "arm-none-eabi-gcc")
        set_toolset("objcopy", "arm-none-eabi-objcopy")
        set_toolset("objdump", "arm-none-eabi-objdump")
        set_toolset("size", "arm-none-eabi-size")
        
        on_check(function (toolchain)
            return true  -- Already verified path exists
        end)
    toolchain_end()
end

-- =====================================================================
-- Core Library (Header-only)
-- =====================================================================

target("umi_core")
    set_kind("headeronly")
    set_group("libraries")
    
    add_headerfiles("core/*.hh")
    add_headerfiles("include/umi/*.hpp")
    add_includedirs(".", {public = true})
    add_includedirs("core", {public = true})
    add_includedirs("port", {public = true})
    add_includedirs("include", {public = true})
    
    -- Preprocessor definitions based on config
    if is_config("kernel", "micro") then
        add_defines("UMI_KERNEL_MICRO", {public = true})
    end
target_end()

-- =====================================================================
-- Host Tests (Native compilation)
-- =====================================================================
-- These run on the development machine for rapid iteration

target("test_dsp")
    set_kind("binary")
    set_group("tests/host")
    set_default(true)
    set_targetdir(".build")

    add_files("test/test_dsp.cc")
    add_includedirs("lib")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})

    on_run(function (target)
        os.execv(target:targetfile())
    end)
target_end()

-- =====================================================================
-- ARM Firmware Targets (only when ARM toolchain is available)
-- =====================================================================

if has_arm_toolchain then

-- Common ARM Cortex-M4F configuration rule
rule("cortex-m4f")
    on_load(function (target)
        target:set("plat", "cross")
        target:set("arch", "arm")
        target:set("toolchains", "arm-none-eabi")
        
        -- CPU flags
        local cpu_flags = {
            "-mcpu=cortex-m4",
            "-mthumb",
            "-mfloat-abi=hard",
            "-mfpu=fpv4-sp-d16",
        }
        for _, flag in ipairs(cpu_flags) do
            target:add("cxflags", flag, {force = true})
            target:add("ldflags", flag, {force = true})
        end
        
        -- Embedded C++ flags
        target:add("cxflags", "-fno-exceptions", "-fno-rtti", {force = true})
        target:add("cxflags", "-ffunction-sections", "-fdata-sections", {force = true})
        target:add("ldflags", "-Wl,--gc-sections", {force = true})
        target:add("ldflags", "-nostartfiles", "--specs=nosys.specs", {force = true})
    end)
    
    -- Generate .bin and .hex after linking
    after_build(function (target)
        local elf = target:targetfile()
        local bin = elf:gsub("%.elf$", ".bin")
        local hex = elf:gsub("%.elf$", ".hex")
        
        os.execv("arm-none-eabi-objcopy", {"-O", "binary", elf, bin})
        os.execv("arm-none-eabi-objcopy", {"-O", "ihex", elf, hex})
        os.execv("arm-none-eabi-size", {elf})
    end)
rule_end()

target("firmware")
    set_kind("binary")
    set_group("firmware")
    set_default(false)
    set_targetdir(".build")
    set_filename("umi_firmware.elf")
    
    add_rules("cortex-m4f")
    add_deps("umi_core")
    add_includedirs(".", "core", "port")
    add_defines("STM32F4", "BOARD_STM32F4")
    
    -- Linker script
    add_ldflags("-Tport/board/stm32f4/linker.ld", {force = true})
    
    -- Sources
    add_files("port/board/stm32f4/syscalls.cc")
    add_files("examples/embedded/example_app.cc")
target_end()

target("renode_test")
    set_kind("binary")
    set_group("firmware")
    set_default(false)
    set_targetdir(".build")
    set_filename("renode_test.elf")
    
    add_rules("cortex-m4f")
    add_deps("umi_core")
    add_includedirs(".", "core", "port", "include")
    add_defines("STM32F4", "BOARD_STM32F4")
    
    -- Optimize for size with debug info
    add_cxflags("-Os", "-g", {force = true})
    
    -- Linker script
    add_ldflags("-Tport/board/stm32f4/linker.ld", {force = true})
    
    -- Sources
    add_files("test/renode_test.cc")
    add_files("port/board/stm32f4/syscalls.cc")
    
    -- Run with Renode
    on_run(function (target)
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        os.execv(renode, {"--console", "--disable-xwt", "-e", "include @renode/test.resc"})
    end)
target_end()

end  -- if has_arm_toolchain

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

-- UMIM common settings
local umim_exported_funcs = "['_malloc','_free','_umi_create','_umi_process','_umi_note_on','_umi_note_off','_umi_set_param','_umi_get_param','_umi_get_param_count','_umi_get_param_name','_umi_get_param_min','_umi_get_param_max','_umi_get_param_default','_umi_get_param_curve','_umi_get_param_unit','_umi_process_cc']"
local umim_runtime_methods = "['ccall','cwrap','UTF8ToString','HEAPF32','HEAP8']"

local function umim_target(name, source_file)
    target("umim_" .. name)
        set_kind("binary")
        set_group("umim")
        set_default(false)
        set_plat("wasm")
        set_arch("wasm32")
        set_toolchains("emcc")
        set_targetdir(".build/umim")
        set_filename(name .. ".js")

        add_includedirs("core", "lib")
        add_files(source_file)

        add_cxflags("-fno-exceptions", "-fno-rtti", "-O3", {force = true})
        add_ldflags("-sWASM=1", "-sALLOW_MEMORY_GROWTH=1", {force = true})
        add_ldflags("-sEXPORTED_FUNCTIONS=" .. umim_exported_funcs, {force = true})
        add_ldflags("-sEXPORTED_RUNTIME_METHODS=" .. umim_runtime_methods, {force = true})
        add_ldflags("-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sENVIRONMENT=web,worker,node", {force = true})
    target_end()
end

-- UMIM Modules
umim_target("synth", "examples/workbench/umim/synth/synth_wasm.cc")
umim_target("delay", "examples/workbench/umim/delay/delay_wasm.cc")
umim_target("volume", "examples/workbench/umim/volume/volume.cc")

end  -- if has_emscripten

-- =====================================================================
-- Tasks
-- =====================================================================

-- Run all host tests
task("test")
    set_category("action")
    on_run(function ()
        import("core.project.project")

        -- Build host tests
        os.exec("xmake build test_dsp")

        print("\n" .. string.rep("=", 60))
        print("Running Host Unit Tests")
        print(string.rep("=", 60) .. "\n")

        local tests = {"test_dsp"}
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
            print("All tests passed ✓")
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
        local logfile = ".build/renode_uart.log"
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
            "cd '%s' && env PATH=\"%s\" %s renode/umi_tests_simple.robot -r .build",
            os.projectdir(), env_path, runner
        ))
        
        -- Summary
        local xml = ".build/robot_output.xml"
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
                    print("See: .build/log.html")
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
        os.tryrm(".build")
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
        print("Buildir: " .. (get_config("buildir") or ".build"))
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
        print("Output: .build/umim/")
        print(string.rep("=", 60))

        -- List generated files
        os.exec("ls -la .build/umim/")
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
