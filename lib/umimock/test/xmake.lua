-- SPDX-License-Identifier: MIT
-- umimock Test Build Configuration

target("test_umimock")
    add_rules("host.test")
    set_default(true)
    add_files("test_mock.cc")
    add_includedirs("$(projectdir)/lib/umimock/include")
    add_includedirs("$(projectdir)/tests")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
