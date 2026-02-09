-- umirtm example ARM targets

target("umirtm_example_stm32f4_renode_gcc")
    set_kind("binary")
    set_default(false)
    add_rules("embedded", "umiport.board")

    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    set_values("embedded.toolchain", "gcc-arm")
    set_values("umiport.board", "stm32f4-renode")

    add_files("print_demo.cc")

    add_deps("umirtm", "umiport")
    umirtm_add_umimmio_dep()
target_end()

-- STM32F4 Discovery (RTT output via OpenOCD/J-Link)
target("umirtm_example_stm32f4_disco")
    set_kind("binary")
    set_default(false)
    add_rules("embedded", "umiport.board")

    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    set_values("embedded.toolchain", "gcc-arm")
    set_values("umiport.board", "stm32f4-disco")

    add_files("print_demo.cc")

    add_deps("umirtm", "umiport")
    umirtm_add_umimmio_dep()
target_end()
