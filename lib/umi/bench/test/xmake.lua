-- SPDX-License-Identifier: MIT
-- bench tests (self-contained)

local bench_dir = path.directory(os.scriptdir())
local include_dir = path.join(bench_dir, "include")
local platform_host_dir = path.join(bench_dir, "platform/host")

target("test_bench")
    set_kind("binary")
    set_group("bench_tests")
    set_languages("c++23")
    add_files(path.join(bench_dir, "test/test_bench.cc"))
    add_includedirs(include_dir, platform_host_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
