-- Synth Application (.umia)
-- Example application that runs on STM32F4 kernel
-- Uses embedded rule from arm-embedded package

target("synth_app")
    set_group("apps")
    set_default(false)
    
    -- Use embedded rule for ARM build
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
set_values("embedded.linker_script", "$(projectdir)/lib/umi/app/app.ld")
    set_values("embedded.optimize", "size")  -- -Os for apps
    
    -- Source files (app startup + main)
    add_files("src/*.cc")
add_files("$(projectdir)/lib/umi/app/crt0.cc")
    
    -- Include paths
    add_includedirs("src")
add_includedirs("$(projectdir)/lib/umi/app")
add_includedirs("$(projectdir)/lib/umi/kernel")
add_includedirs("$(projectdir)/lib/umi")
    add_includedirs("$(projectdir)/lib")               -- For lib-relative paths
add_includedirs("$(projectdir)/lib/umi/synth")
    add_includedirs("$(projectdir)/examples/headless_webhost/src")  -- For synth.hh
    
    -- DSP library
    add_deps("umi.dsp", "umi.core")
    
    -- Defines
    add_defines("UMIOS_APP=1")
    
    -- Additional linker flags for minimal runtime
    add_ldflags("-nodefaultlibs", {force = true})
add_ldflags("-Wl,-L" .. path.join(os.projectdir(), "lib", "umi", "app"), {force = true})
    
    -- Post-build: generate .umia with header
    -- Note: embedded rule generates .bin, .hex, .map automatically
    after_build(function (target)
        local targetdir = target:targetdir()
        local targetname = target:name()
        local binfile = path.join(targetdir, targetname .. ".bin")
        local umiafile = path.join(targetdir, targetname .. ".umia")
        
        -- Wait for embedded rule to create .bin file
        if not os.isfile(binfile) then
            print("Warning: .bin file not found, skipping .umia generation")
            return
        end
        
            -- Create .umia using Python tool
            local make_umia = path.join(os.projectdir(), "lib", "umi", "tools", "build", "make_umia.py")
            if os.isfile(make_umia) then
            os.execv("python3", {make_umia, binfile, umiafile, "--name", "SynthApp"})
            print("App: " .. umiafile)
        else
            -- Fallback: just copy binary (no header)
            os.cp(binfile, umiafile)
            print("Warning: make_umia.py not found, using raw binary")
        end
    end)

task("flash-synth-app")
    set_category("action")
    on_run(function ()
        print("Building synth app...")
        os.exec("xmake build synth_app")
        local umia = path.join(os.projectdir(), "build", "synth_app", "release", "synth_app.umia")
        if not os.isfile(umia) then
            raise(".umia not found: " .. umia)
        end
        print("Flashing synth app to APP_FLASH via pyOCD...")
        os.execv("pyocd", {"flash", "-t", "stm32f407vg", "-a", "0x08060000", "--format", "bin", umia})
    end)
    set_menu {usage = "xmake flash-synth-app", description = "Build and flash synth app (.umia) to APP_FLASH"}
task_end()
