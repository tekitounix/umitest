-- =====================================================================
-- UMI Filesystem Tests
-- =====================================================================

local test_dir = os.scriptdir()
local umifs_dir = path.directory(test_dir)
local lib_dir = path.directory(umifs_dir)
local root_dir = path.directory(lib_dir)

-- =====================================================================
-- Host unit tests
-- =====================================================================

target("test_fs_fat")
    add_rules("host.test")
    set_default(true)
    add_deps("umi.fs.fatfs")
    add_files(path.join(test_dir, "test_fat.cc"))
    add_deps("umitest")
    add_includedirs(lib_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

target("test_fs_slim")
    add_rules("host.test")
    set_default(true)
    add_deps("umi.fs.slimfs")
    add_files(path.join(test_dir, "test_slim.cc"))
    add_deps("umitest")
    add_includedirs(lib_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- =====================================================================
-- Comparison tests (C++23 port vs reference C implementation) — legacy
-- =====================================================================

target("test_fs_fat_compare")
    add_rules("host.test")
    set_default(false)
    add_deps("umi.fs.fatfs")
    add_files(path.join(test_dir, "test_fat_compare.cc"))
    -- Reference FATfs C implementation
    add_files(path.join(root_dir, ".refs/fatfs/source/ff.c"))
    add_files(path.join(root_dir, ".refs/fatfs/source/ffunicode.c"))
    add_includedirs(path.join(root_dir, ".refs/fatfs/source"))
    add_deps("umitest")
    add_includedirs(lib_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    -- Suppress warnings in reference C code
    add_cflags(
        "-Wno-missing-field-initializers",
        "-Wno-unused-function",
        "-Wno-sign-compare",
        "-Wno-implicit-fallthrough",
        {force = true})
target_end()

-- =====================================================================
-- Unified benchmark tests
-- =====================================================================

-- Unified benchmark: lfs(ref), fat(cr), slim — 3 FS side-by-side
target("test_fs_bench")
    add_rules("host.test")
    set_default(false)
    add_deps("umi.fs.fatfs", "umi.fs.slimfs")
    add_files(path.join(test_dir, "test_bench.cc"))
    -- Reference littlefs C implementation
    add_files(path.join(root_dir, ".refs/littlefs/lfs.c"))
    add_files(path.join(root_dir, ".refs/littlefs/lfs_util.c"))
    add_includedirs(path.join(root_dir, ".refs/littlefs"))
    add_deps("umitest")
    add_includedirs(test_dir)
    add_includedirs(lib_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    add_cflags(
        "-Wno-missing-field-initializers",
        "-Wno-unused-function",
        "-Wno-sign-compare",
        {force = true})
target_end()

-- FATfs reference C benchmark (separate binary due to global symbol conflicts)
target("test_fs_bench_fat_ref")
    add_rules("host.test")
    set_default(false)
    add_deps("umi.fs.fatfs")
    add_files(path.join(test_dir, "test_bench_fat_ref.cc"))
    -- Reference FATfs C implementation
    add_files(path.join(root_dir, ".refs/fatfs/source/ff.c"))
    add_files(path.join(root_dir, ".refs/fatfs/source/ffunicode.c"))
    add_includedirs(path.join(root_dir, ".refs/fatfs/source"))
    add_deps("umitest")
    add_includedirs(test_dir)
    add_includedirs(lib_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    add_cflags(
        "-Wno-missing-field-initializers",
        "-Wno-unused-function",
        "-Wno-sign-compare",
        "-Wno-implicit-fallthrough",
        {force = true})
target_end()

-- =====================================================================
-- Renode embedded tests (Cortex-M4 STM32F407)
-- =====================================================================

local stm32f4_linker = path.join(lib_dir, "umiport/mcu/stm32f4/linker.ld")
local stm32f4_syscalls = path.join(lib_dir, "umiport/mcu/stm32f4/syscalls.cc")

target("renode_fs_test")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "renode_fs_test.cc"))
    add_files(path.join(umifs_dir, "fat/fat_core.cc"))
    add_files(path.join(umifs_dir, "fat/ff_unicode.cc"))
    add_files(path.join(umifs_dir, "slim/slim_core.cc"))
    add_includedirs(path.join(umifs_dir, "fat"))
    add_includedirs(lib_dir)
    add_cxxflags(
        "-Wno-address-of-temporary",
        "-Wno-c99-designator",
        "-Wno-c++11-narrowing",
        "-Wno-missing-field-initializers",
        "-Wno-reserved-user-defined-literal",
        "-Wno-unused-function",
        {force = true})
    on_run(function(target)
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        os.execv(renode, {"--console", "--disable-xwt", "-e",
            "include @" .. path.join(test_dir, "fs_test.resc")})
    end)
target_end()

-- =====================================================================
-- Binary size comparison targets (ARM Cortex-M4, -Oz)
-- Each links exactly one FS implementation for isolated size measurement.
-- Usage: xmake build size_fs_fat ... then arm-none-eabi-size
-- =====================================================================

local size_cxxflags = {
    "-Wno-address-of-temporary",
    "-Wno-c99-designator",
    "-Wno-c++11-narrowing",
    "-Wno-missing-field-initializers",
    "-Wno-reserved-user-defined-literal",
    "-Wno-unused-function",
    "-Wno-unused-variable",
}
local size_cflags = {
    "-Wno-missing-field-initializers",
    "-Wno-unused-function",
    "-Wno-sign-compare",
    "-Wno-implicit-fallthrough",
    "-Wno-unused-parameter",
    "-Wno-old-style-definition",
}

-- FATfs cleanroom
target("size_fs_fat")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "size_fat.cc"))
    add_files(path.join(umifs_dir, "fat/fat_core.cc"))
    add_files(path.join(umifs_dir, "fat/ff_unicode.cc"))
    add_includedirs(lib_dir)
    add_cxxflags(table.unpack(size_cxxflags))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- slimfs
target("size_fs_slim")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "size_slim.cc"))
    add_files(path.join(umifs_dir, "slim/slim_core.cc"))
    add_includedirs(lib_dir)
    add_cxxflags(table.unpack(size_cxxflags))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- littlefs reference C
target("size_fs_lfs_ref")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_defines("LFS_NO_DEBUG", "LFS_NO_WARN", "LFS_NO_ERROR", "LFS_NO_ASSERT", "LFS_NO_MALLOC")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "size_lfs_ref.cc"))
    add_files(path.join(root_dir, ".refs/littlefs/lfs.c"))
    add_files(path.join(root_dir, ".refs/littlefs/lfs_util.c"))
    add_includedirs(path.join(root_dir, ".refs/littlefs"))
    add_includedirs(lib_dir)
    add_cflags(table.unpack(size_cflags))
    add_cxxflags(table.unpack(size_cxxflags))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- FATfs reference C
target("size_fs_fat_ref")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "size_fat_ref.cc"))
    add_files(path.join(root_dir, ".refs/fatfs/source/ff.c"))
    add_files(path.join(root_dir, ".refs/fatfs/source/ffunicode.c"))
    add_includedirs(test_dir)   -- for ffconf.h
    add_includedirs(path.join(root_dir, ".refs/fatfs/source"))
    add_includedirs(lib_dir)
    add_cflags(table.unpack(size_cflags))
    add_cxxflags(table.unpack(size_cxxflags))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

target("renode_fs_test_ref")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_defines("LFS_NO_DEBUG", "LFS_NO_WARN", "LFS_NO_ERROR", "LFS_NO_ASSERT")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "renode_fs_test_ref.cc"))
    -- Reference C littlefs
    add_files(path.join(root_dir, ".refs/littlefs/lfs.c"))
    add_files(path.join(root_dir, ".refs/littlefs/lfs_util.c"))
    add_includedirs(path.join(root_dir, ".refs/littlefs"))
    -- Reference C FATfs (with custom ffconf.h from test/)
    add_files(path.join(root_dir, ".refs/fatfs/source/ff.c"))
    add_files(path.join(root_dir, ".refs/fatfs/source/ffunicode.c"))
    add_includedirs(test_dir)   -- for ffconf.h
    add_includedirs(path.join(root_dir, ".refs/fatfs/source"))
    add_includedirs(lib_dir)
    add_cflags(
        "-Wno-unused-parameter",
        "-Wno-sign-compare",
        "-Wno-implicit-fallthrough",
        "-Wno-missing-field-initializers",
        "-Wno-old-style-definition",
        "-Wno-unused-function",
        {force = true})
    add_cxxflags(
        "-Wno-address-of-temporary",
        "-Wno-c99-designator",
        "-Wno-c++11-narrowing",
        "-Wno-missing-field-initializers",
        "-Wno-reserved-user-defined-literal",
        "-Wno-unused-function",
        {force = true})
target_end()
