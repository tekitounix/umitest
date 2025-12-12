{
    files = {
        ".build/.objs/renode_test/cross/arm/release/test/renode_test.cc.o",
        ".build/.objs/renode_test/cross/arm/release/port/board/stm32f4/syscalls.cc.o"
    },
    values = {
        "/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin/arm-none-eabi-g++",
        {
            "-s",
            "-Tport/board/stm32f4/linker.ld",
            "-mcpu=cortex-m4",
            "-mthumb",
            "-mfloat-abi=hard",
            "-mfpu=fpv4-sp-d16",
            "-Wl,--gc-sections",
            "-nostartfiles",
            "--specs=nosys.specs"
        }
    }
}