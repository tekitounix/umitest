-- Daisy Pod Synth Application (.umia) for STM32H750 QSPI XIP
-- Separate from kernel, loaded at 0x90000000

target("daisy_pod_synth_h7")
    set_group("apps")
    set_default(false)

    add_rules("embedded")
    set_values("embedded.mcu", "stm32h750ib")
    set_values("embedded.linker_script", path.join(os.scriptdir(), "app_h7.ld"))
    set_values("embedded.optimize", "size")

    -- Source files
    add_files("src/*.cc")
    add_files("$(projectdir)/lib/umios/app/crt0.cc")

    -- Include paths
    add_includedirs("src")
    add_includedirs("$(projectdir)/lib/umios/app")
    add_includedirs("$(projectdir)/lib/umios/kernel")
    add_includedirs("$(projectdir)/lib/umios")
    add_includedirs("$(projectdir)/lib")

    -- Defines
    add_defines("UMIOS_APP=1")

    -- Minimal runtime
    add_ldflags("-nodefaultlibs", {force = true})
    add_ldflags("-Wl,-L" .. path.join(os.projectdir(), "lib", "umios", "app"), {force = true})

    -- Post-build: generate .umia
    after_build(function (target)
        local targetdir = target:targetdir()
        local targetname = target:name()
        local binfile = path.join(targetdir, targetname .. ".bin")
        local umiafile = path.join(targetdir, targetname .. ".umia")

        if not os.isfile(binfile) then
            print("Warning: .bin file not found, skipping .umia generation")
            return
        end

        local make_umia = path.join(os.projectdir(), "tools", "make_umia.py")
        if os.isfile(make_umia) then
            os.execv("python3", {make_umia, binfile, umiafile, "--name", "DaisySynth"})
            print("App: " .. umiafile)
        else
            os.cp(binfile, umiafile)
            print("Warning: make_umia.py not found, using raw binary")
        end
    end)

task("flash-synth-h7")
    set_category("action")
    on_run(function ()
        print("Building H7 synth app...")
        os.exec("xmake build daisy_pod_synth_h7")
        print("Flash synth app to QSPI (0x90000000) via STM32CubeProgrammer:")
        print("  STM32_Programmer_CLI -c port=SWD -el IS25LP064A_DaisySeed.stldr -w <umia> 0x90000000")
    end)
    set_menu {usage = "xmake flash-synth-h7", description = "Build and flash H7 synth app (.umia) to QSPI"}
task_end()
