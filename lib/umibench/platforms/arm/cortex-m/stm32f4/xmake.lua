-- STM32F4 Renode Test Target for umibench

-- umiport shared platform infrastructure
local umiport_stm32f4 = path.join(os.scriptdir(), "../../../../../umiport/src/stm32f4")
local umiport_include = path.join(os.scriptdir(), "../../../../../umiport/include")

-- Clang-ARM version
target("umibench_stm32f4_renode")
    set_kind("binary")
    set_default(false)
    add_rules("embedded")

    -- MCU Configuration
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    set_values("embedded.toolchain", "clang-arm")

    -- Source files
    add_files(path.join(umiport_stm32f4, "startup.cc"))
    add_files(path.join(umiport_stm32f4, "syscalls.cc"))
    add_files("../../../../tests/test_*.cc")

    -- Linker script
    set_values("embedded.linker_script", path.join(umiport_stm32f4, "linker.ld"))

    -- Dependencies
    add_deps("umibench_embedded")
    umibench_add_umitest_dep()

    -- Include paths (public API + Cortex-M shared + STM32F4 board + umiport)
    add_includedirs("..", {public = false})
    add_includedirs(os.scriptdir(), {public = false})
    add_includedirs(umiport_include, {public = false})

    -- Renode run task
    on_run(function(target)
        import("core.base.option")
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then
            renode = "renode"
        end
        local resc = path.join(os.scriptdir(), "renode", "bench_stm32f4.resc")
        os.execv(renode, {"--console", "--disable-xwt", resc})
    end)
target_end()

-- GCC-ARM version
target("umibench_stm32f4_renode_gcc")
    set_kind("binary")
    set_default(false)
    add_rules("embedded")

    -- MCU Configuration
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    set_values("embedded.toolchain", "gcc-arm")

    -- Source files
    add_files(path.join(umiport_stm32f4, "startup.cc"))
    add_files(path.join(umiport_stm32f4, "syscalls.cc"))
    add_files("../../../../tests/test_*.cc")

    -- Linker script
    set_values("embedded.linker_script", path.join(umiport_stm32f4, "linker.ld"))

    -- Dependencies
    add_deps("umibench_embedded")
    umibench_add_umitest_dep()

    -- Include paths (public API + Cortex-M shared + STM32F4 board + umiport)
    add_includedirs("..", {public = false})
    add_includedirs(os.scriptdir(), {public = false})
    add_includedirs(umiport_include, {public = false})
target_end()
