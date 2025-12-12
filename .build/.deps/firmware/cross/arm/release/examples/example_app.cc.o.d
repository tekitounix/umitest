{
    depfiles = "example_app.o: examples/example_app.cc examples/../core/umi_coro.hh\
",
    depfiles_format = "gcc",
    files = {
        "examples/example_app.cc"
    },
    values = {
        "/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin/arm-none-eabi-g++",
        {
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O3",
            "-std=c++23",
            "-I.",
            "-Icore",
            "-Iport",
            "-DSTM32F4",
            "-DBOARD_STM32F4",
            "-mcpu=cortex-m4",
            "-mthumb",
            "-mfloat-abi=hard",
            "-mfpu=fpv4-sp-d16",
            "-fno-exceptions",
            "-fno-rtti",
            "-ffunction-sections",
            "-fdata-sections",
            "-DNDEBUG"
        }
    }
}