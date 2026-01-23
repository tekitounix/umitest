-- STM32F4-Discovery Kernel
-- Separate kernel binary that loads .umiapp applications
-- Uses embedded rule from arm-embedded package

target("stm32f4_kernel")
    set_group("firmware")
    set_default(false)
    
    -- Use embedded rule from arm-embedded package
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", path.join(os.scriptdir(), "kernel.ld"))
    set_values("embedded.optimize", "size")  -- -Os for kernel
    
    -- Source files
    add_files("src/*.cc")
    add_files("$(projectdir)/lib/bsp/stm32f4-disco/syscalls.cc")
    add_files("$(projectdir)/lib/umios/kernel/loader.cc")
    add_files("$(projectdir)/lib/umios/backend/cm/common/irq.cc")

    -- Crypto library for signature verification
    add_files("$(projectdir)/lib/umios/crypto/sha512.cc")
    add_files("$(projectdir)/lib/umios/crypto/ed25519.cc")

    -- Dependencies
    add_deps("umi.embedded.full")

    -- Include paths
    add_includedirs("src")
    add_includedirs("$(projectdir)/lib/umios/kernel")
    add_includedirs("$(projectdir)/lib/umios/crypto")

    -- Defines
    add_defines("UMIOS_KERNEL=1")
    add_defines("STM32F4", "BOARD_STM32F4")
    
    -- Note: embedded rule handles .bin/.hex/.map generation automatically

task("flash-kernel")
    set_category("action")
    on_run(function ()
        import("core.project.project")
        local target = project.target("stm32f4_kernel")
        if not target then
            raise("target stm32f4_kernel not found")
        end
        print("Building STM32F4 kernel...")
        os.exec("xmake build " .. target:name())
        local binfile = path.join(target:targetdir(), target:name() .. ".bin")
        if not os.isfile(binfile) then
            raise("kernel binary not found: " .. binfile)
        end
        print("Flashing kernel via pyOCD...")
        os.execv("pyocd", {"flash", "-t", "stm32f407vg", binfile})
    end)
    set_menu {usage = "xmake flash-kernel", description = "Build and flash STM32F4 kernel"}
task_end()
