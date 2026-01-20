-- umidsp library build configuration
-- Standalone xmake.lua for building and testing umidsp independently

set_project("umidsp")
set_version("1.0.0")

set_languages("c++23")
add_rules("mode.debug", "mode.release")

-- Compiler settings
set_warnings("all", "extra")
add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})

-- Header-only library target
target("umidsp")
    set_kind("headeronly")
    add_headerfiles("include/(**.hh)")
    -- Include parent dir so users can: #include <umidsp/umidsp.hh>
    add_includedirs("$(projectdir)/lib/umidsp", {public = true})
target_end()

-- =============================================================================
-- Test targets
-- =============================================================================

target("umidsp_test")
    set_kind("binary")
    set_default(false)
    add_files("test/test_dsp.cc")
    add_includedirs("$(projectdir)/lib/umidsp")
target_end()

-- =============================================================================
-- Test runner
-- =============================================================================

-- Run all tests: xmake run test
target("test")
    set_kind("phony")
    set_default(false)
    on_run(function (target)
        print("Running umidsp_test...")
        os.execv(path.join("build", "umidsp_test"))
    end)
    add_deps("umidsp_test")
target_end()
