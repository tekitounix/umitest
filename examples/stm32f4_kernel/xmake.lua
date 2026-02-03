-- STM32F4-Discovery Kernel
-- Separate kernel binary that loads .umia applications
-- Uses embedded rule from arm-embedded package

target("stm32f4_kernel")
    set_group("firmware")
    set_default(false)
    
    -- Use embedded rule from arm-embedded package
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", path.join(os.scriptdir(), "kernel.ld"))
    set_values("embedded.optimize", "size")  -- -Os for kernel
    -- LTO test result: -3.2% Flash (-1,348B), -6.6% RAM (-5,120B)
    if is_mode("release") then
        set_values("embedded.lto", "thin")

        -- Section GC
        add_cxflags("-ffunction-sections", "-fdata-sections")
        add_ldflags("-Wl,--gc-sections")
    end
    
    -- Source files
    add_files("src/*.cc")
    add_files("$(projectdir)/lib/umi/port/mcu/stm32f4/syscalls.cc")
    add_files("$(projectdir)/lib/umi/service/loader/loader.cc")
    add_files("$(projectdir)/lib/umi/port/common/common/irq.cc")

    -- Crypto library for signature verification
    add_files("$(projectdir)/lib/umi/crypto/sha512.cc")
    add_files("$(projectdir)/lib/umi/crypto/ed25519.cc")

    -- Dependencies
    add_deps("umi.embedded.full")

    -- Include paths
    add_includedirs("src")
    add_includedirs("$(projectdir)/lib/umi/kernel")
    add_includedirs("$(projectdir)/lib/umi/crypto")
    add_includedirs("$(projectdir)/lib/umi/mmio/include")
    add_includedirs("$(projectdir)/lib/umi/port/device")

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
