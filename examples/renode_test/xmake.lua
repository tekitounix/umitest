-- UMI-OS Renode Test Example
-- Build: xmake f -p cross -a arm --cross=arm-none-eabi- --sdk=/path/to/arm-toolchain && xmake build renode_test

target("renode_test")
    set_kind("binary")
    set_toolchains("cross")

    -- Target CPU
    add_cxflags("-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16")
    add_ldflags("-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16")

    -- C++ settings
    set_languages("c++23")
    add_cxflags("-fno-exceptions", "-fno-rtti")

    -- Optimization
    add_cxflags("-Os", "-ffunction-sections", "-fdata-sections")
    add_ldflags("-Wl,--gc-sections")

    -- Linker script
    add_ldflags("-T" .. path.join(os.scriptdir(), "../../tools/renode/linker.ld"))
    add_ldflags("-nostartfiles", "-nostdlib")

    -- Entry point
    add_ldflags("-Wl,-eReset_Handler")

    -- Libraries (minimal)
    add_ldflags("-lgcc")

    -- Include paths
    -- Platform-specific headers via port
    add_includedirs("$(projectdir)/lib/umi/port/common")
    add_includedirs("$(projectdir)/lib/umi/port/arch/cm4")
    add_includedirs("$(projectdir)/lib/umi/port/mcu/stm32f4")
    add_includedirs("$(projectdir)/lib/umi/port/board/stm32f4_disco")
    add_includedirs("$(projectdir)/lib/umi/port/platform/embedded")
    -- Kernel headers
    add_includedirs("$(projectdir)/lib/umi")
    -- General lib path
    add_includedirs("$(projectdir)/lib")

    -- Source files
    add_files("startup.cc")
    add_files("main.cc")

    -- Output ELF
    set_targetdir("$(buildir)/cross/arm-none-eabi/$(mode)")
    set_filename("renode_test.elf")

    -- Post-build: generate binary and disassembly
    after_build(function(target)
        local objcopy = "arm-none-eabi-objcopy"
        local objdump = "arm-none-eabi-objdump"
        local elf = target:targetfile()
        local bin = path.join(path.directory(elf), "renode_test.bin")
        local lst = path.join(path.directory(elf), "renode_test.lst")

        os.execv(objcopy, {"-O", "binary", elf, bin})
        os.execv(objdump, {"-d", "-S", elf}, {stdout = lst})
        print("Generated: " .. bin)
        print("Generated: " .. lst)
    end)

target_end()
