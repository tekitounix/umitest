-- Daisy Pod Kernel (STM32H750)
-- Phase 6: Audio + RTOS + USB Audio/MIDI + Synth + EventRouter

local board_dir = path.join(os.projectdir(), "lib/umi/port/board/daisy_seed")
local pod_board_dir = path.join(os.projectdir(), "lib/umi/port/board/daisy_pod")

target("daisy_pod_kernel")
    set_group("firmware")
    set_default(false)

    -- Use embedded rule from arm-embedded package
    add_rules("embedded")
    set_values("embedded.mcu", "stm32h750ib")
    set_values("embedded.linker_script", path.join(os.scriptdir(), "kernel.ld"))
    set_values("embedded.optimize", "size")

    if is_mode("release") then
        set_values("embedded.lto", "thin")
        add_cxflags("-ffunction-sections", "-fdata-sections")
        add_ldflags("-Wl,--gc-sections")
    end

    -- Linker search path for INCLUDE memory.ld
    add_ldflags("-L" .. board_dir, {force = true})

    -- Source files
    add_files("src/*.cc")
    add_files("$(projectdir)/lib/umi/port/common/common/irq.cc")
    -- handlers.cc replaced by local arch.cc (with kernel callback mechanism)

    -- Dependencies
    add_deps("umi.mmio")

    -- Defines
    add_defines("UMI_CM_NUM_IRQS=150")
    add_defines("STM32H7", "BOARD_DAISY_SEED")
    if is_mode("debug") then
        add_defines("UMI_DEBUG=1")
    end

    -- Include paths (port layers)
    add_includedirs("src")
    add_includedirs(path.join(os.projectdir(), "lib/umi/mmio"))
    add_includedirs(path.join(os.projectdir(), "lib/umi/port/arch/cm7"))
    add_includedirs(path.join(os.projectdir(), "lib/umi/port/mcu/stm32h7"))
    add_includedirs(path.join(os.projectdir(), "lib/umi/port/common"))
    add_includedirs(path.join(os.projectdir(), "lib"))
    add_includedirs(path.join(os.projectdir(), "lib/umi/usb"))
    add_includedirs(path.join(os.projectdir(), "lib/umi/usb/include"))
    add_includedirs(path.join(os.projectdir(), "lib/umi/dsp"))
    add_includedirs(path.join(os.projectdir(), "lib/umi/dsp/include"))
    add_includedirs(path.join(os.projectdir(), "lib/umi/core"))
    add_includedirs(path.join(os.projectdir(), "lib/umi"))
    add_includedirs(board_dir)
    add_includedirs(pod_board_dir)
target_end()

-- Flash kernel to internal flash via pyOCD (STLINK-V3)
task("flash-h7-kernel")
    set_category("action")
    on_run(function ()
        print("[DEPRECATED] Use: xmake flash -t daisy_pod_kernel")
        print("Delegating to unified flash command...")
        os.exec("xmake flash -t daisy_pod_kernel")
    end)
    set_menu {usage = "xmake flash-h7-kernel", description = "Build and flash H7 kernel via pyOCD (deprecated, use 'xmake flash -t daisy_pod_kernel')"}
task_end()

-- Flash synth app to QSPI (0x90000000) via pyOCD
task("flash-h7-app")
    set_category("action")
    on_run(function ()
        print("[DEPRECATED] Use: xmake flash -t daisy_pod_synth_h7 -a 0x90000000")
        print("Delegating to unified flash command...")
        os.exec("xmake flash -t daisy_pod_synth_h7 -a 0x90000000")
    end)
    set_menu {usage = "xmake flash-h7-app", description = "Build and flash H7 synth app to QSPI via pyOCD (deprecated, use 'xmake flash -t daisy_pod_synth_h7 -a 0x90000000')"}
task_end()
