-- SPDX-License-Identifier: MIT
-- umimock Test Build Configuration

target("test_umimock")
    add_rules("host.test")
    add_tests("default")
    set_group("tests/ref")
    set_default(true)
    add_files("test_mock.cc")
    add_deps("umitest")
    add_includedirs("$(projectdir)/lib/umi/ref/include")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
