-- lib/umios/core/test/xmake.lua

local test_dir = os.scriptdir()

-- Concept tests (ProcessorLike, Controllable, AudioContext)
target("test_umios_concepts")
    add_rules("host.test")
    set_default(true)
    add_deps("umi.all", "umitest")
    add_files(path.join(test_dir, "test_concepts.cc"))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- Syscall number and AudioContext tests
target("test_umios_syscall")
    add_rules("host.test")
    set_default(true)
    add_deps("umi.all", "umitest")
    add_files(path.join(test_dir, "test_syscall_context.cc"))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
