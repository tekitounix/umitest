-- Synth Application (.umiapp)
-- Example application that runs on STM32F4 kernel
-- Uses embedded rule from arm-embedded package

target("synth_app")
    set_group("apps")
    set_default(false)
    
    -- Use embedded rule for ARM build
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", "$(projectdir)/lib/umios/app/app.ld")
    set_values("embedded.optimize", "size")  -- -Os for apps
    
    -- Source files (app startup + main)
    add_files("src/*.cc")
    add_files("$(projectdir)/lib/umios/app/crt0.cc")
    
    -- Include paths
    add_includedirs("src")
    add_includedirs("$(projectdir)/lib/umios/app")
    add_includedirs("$(projectdir)/lib/umios/kernel")  -- For shared types
    add_includedirs("$(projectdir)/lib/umios")         -- For umios/core/...
    add_includedirs("$(projectdir)/lib")               -- For lib-relative paths
    add_includedirs("$(projectdir)/examples/headless_webhost/src")  -- For synth.hh
    
    -- DSP library
    add_deps("umi.dsp", "umi.core")
    
    -- Defines
    add_defines("UMIOS_APP=1")
    
    -- Additional linker flags for minimal runtime
    add_ldflags("-nodefaultlibs", {force = true})
    
    -- Post-build: generate .umiapp with header
    -- Note: embedded rule generates .bin, .hex, .map automatically
    after_build(function (target)
        local targetdir = target:targetdir()
        local targetname = target:name()
        local binfile = path.join(targetdir, targetname .. ".bin")
        local umiappfile = path.join(targetdir, targetname .. ".umiapp")
        
        -- Wait for embedded rule to create .bin file
        if not os.isfile(binfile) then
            print("Warning: .bin file not found, skipping .umiapp generation")
            return
        end
        
        -- Create .umiapp using Python tool
        local make_umiapp = path.join(os.projectdir(), "tools", "make_umiapp.py")
        if os.isfile(make_umiapp) then
            os.execv("python3", {make_umiapp, binfile, umiappfile, "--name", "SynthApp"})
            print("App: " .. umiappfile)
        else
            -- Fallback: just copy binary (no header)
            os.cp(binfile, umiappfile)
            print("Warning: make_umiapp.py not found, using raw binary")
        end
    end)
