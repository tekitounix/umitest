-- lib/umios/kernel/test/xmake.lua

local test_dir = os.scriptdir()

-- Kernel tests
target("test_umios_kernel")
    add_rules("host.test")
    set_default(true)
    add_deps("umi.all", "umitest")
    add_files(path.join(test_dir, "test_kernel.cc"))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- Audio engine tests
target("test_umios_audio")
    add_rules("host.test")
    set_default(true)
    add_deps("umi.all", "umitest")
    add_files(path.join(test_dir, "test_audio.cc"))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- Kernel MIDI tests
target("test_umios_midi")
    add_rules("host.test")
    set_default(true)
    add_deps("umi.all", "umitest")
    add_files(path.join(test_dir, "test_midi.cc"))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
