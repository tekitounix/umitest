-- Daisy Pod Kernel (STM32H750)
-- Phase 6: Audio + RTOS + USB Audio/MIDI + Synth + EventRouter

local board_dir = path.join(os.projectdir(), "lib/umiport/board/daisy_seed")
local pod_board_dir = path.join(os.projectdir(), "lib/umiport/board/daisy_pod")

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
    add_files("$(projectdir)/lib/umiport/common/common/irq.cc")
    add_files("$(projectdir)/lib/umiport/arch/cm7/arch/handlers.cc")

    -- Dependencies
    add_deps("umi.mmio")

    -- Defines
    add_defines("UMI_CM_NUM_IRQS=150")
    add_defines("STM32H7", "BOARD_DAISY_SEED")

    -- Include paths (umiport layers)
    add_includedirs("src")
    add_includedirs(path.join(os.projectdir(), "lib/umiport/arch/cm7"))
    add_includedirs(path.join(os.projectdir(), "lib/umiport/mcu/stm32h7"))
    add_includedirs(path.join(os.projectdir(), "lib/umiport/common"))
    add_includedirs(path.join(os.projectdir(), "lib"))
    add_includedirs(path.join(os.projectdir(), "lib/umiusb/include"))
    add_includedirs(path.join(os.projectdir(), "lib/umidsp/include"))
    add_includedirs(path.join(os.projectdir(), "lib/umios/core"))
    add_includedirs(board_dir)
    add_includedirs(pod_board_dir)
target_end()
