-- =====================================================================
-- UMI-Port Tests
-- =====================================================================

local test_dir = os.scriptdir()
local umiport_dir = path.directory(test_dir)
local lib_dir = path.directory(umiport_dir)
local root_dir = path.directory(lib_dir)

-- =====================================================================
-- Host unit tests
-- =====================================================================

target("test_port_concepts")
    add_rules("host.test")
    set_default(true)
    add_files(path.join(test_dir, "test_concepts.cc"))
    add_includedirs(path.join(umiport_dir, "concepts"))
    add_deps("umitest")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

target("test_port_hal_h7")
    add_rules("host.test")
    set_default(true)
    add_files(path.join(test_dir, "test_hal_stm32h7.cc"))
    add_includedirs(path.join(umiport_dir, "mcu/stm32h7"))
    add_includedirs(path.join(lib_dir, "umi/mmio"))
    add_deps("umitest")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

target("test_port_audio")
    add_rules("host.test")
    set_default(true)
    add_files(path.join(test_dir, "test_audio_driver.cc"))
    add_includedirs(path.join(umiport_dir, "concepts"))
    add_deps("umitest")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

target("test_port_drivers")
    add_rules("host.test")
    set_default(true)
    add_files(path.join(test_dir, "test_drivers.cc"))
    add_includedirs(path.join(umiport_dir, "device"))
    add_includedirs(path.join(lib_dir, "umi/mmio"))
    add_deps("umitest")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
