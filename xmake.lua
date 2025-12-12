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
-- Global Configuration
-- =====================================================================

-- Language standard
set_languages("c++23")

-- Build modes
add_rules("mode.debug", "mode.release")

-- Generate compile_commands.json for clangd/VS Code IntelliSense
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".build"})

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

-- =====================================================================
-- Toolchain: ARM GCC (Cross-compilation)
-- =====================================================================

local arm_gcc_path = nil
local arm_paths = {
    "/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi",
    "/usr/local/arm-none-eabi",
    "/opt/arm-none-eabi",
}
for _, p in ipairs(arm_paths) do
    if os.isdir(p) then
        arm_gcc_path = p
        break
    end
end

local has_arm_toolchain = arm_gcc_path ~= nil

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
    add_includedirs(".", {public = true})
    add_includedirs("core", {public = true})
    add_includedirs("port", {public = true})
    
    -- Preprocessor definitions based on config
    if is_config("kernel", "micro") then
        add_defines("UMI_KERNEL_MICRO", {public = true})
    end
target_end()

-- =====================================================================
-- Host Tests (Native compilation)
-- =====================================================================
-- These run on the development machine for rapid iteration

target("test_kernel")
    set_kind("binary")
    set_group("tests/host")
    set_default(true)
    set_targetdir(".build")
    
    add_files("test/test_kernel.cc")
    add_deps("umi_core")
    
    -- Host-specific: embedded constraints
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    
    on_run(function (target)
        os.execv(target:targetfile())
    end)
target_end()

target("test_audio")
    set_kind("binary")
    set_group("tests/host")
    set_default(true)
    set_targetdir(".build")
    
    add_files("test/test_audio.cc")
    add_deps("umi_core")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    
    on_run(function (target)
        os.execv(target:targetfile())
    end)
target_end()

target("test_midi")
    set_kind("binary")
    set_group("tests/host")
    set_default(true)
    set_targetdir(".build")
    
    add_files("test/test_midi.cc")
    add_deps("umi_core")
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
    add_files("examples/example_app.cc")
target_end()

target("renode_test")
    set_kind("binary")
    set_group("firmware")
    set_default(false)
    set_targetdir(".build")
    set_filename("renode_test.elf")
    
    add_rules("cortex-m4f")
    add_deps("umi_core")
    add_includedirs(".", "core", "port")
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
-- Tasks
-- =====================================================================

-- Run all host tests
task("test")
    set_category("action")
    on_run(function ()
        import("core.project.project")
        
        -- Build host tests individually
        os.exec("xmake build test_kernel")
        os.exec("xmake build test_audio")
        os.exec("xmake build test_midi")
        
        print("\n" .. string.rep("=", 60))
        print("Running Host Unit Tests")
        print(string.rep("=", 60) .. "\n")
        
        local tests = {"test_kernel", "test_audio", "test_midi"}
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
        if not os.isexec(runner) then
            runner = "renode-test"
        end
        if not os.isexec(runner) then
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
        if os.isfile(arm_gcc) or os.isexec("arm-none-eabi-gcc") then
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
    end)
    set_menu {
        usage = "xmake info",
        description = "Show build configuration"
    }
task_end()
