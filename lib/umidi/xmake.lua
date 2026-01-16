-- umidi library build configuration
-- Standalone xmake.lua for building and testing umidi independently

set_project("umidi")
set_version("1.0.0")

set_languages("c++23")
add_rules("mode.debug", "mode.release")

-- Build directory
set_targetdir(".build")

-- Compiler settings
set_warnings("all", "extra")
add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})

-- Header-only library target
target("umidi")
    set_kind("headeronly")
    add_headerfiles("include/(umidi/**.hh)")
    add_includedirs("include", {public = true})
target_end()

-- =============================================================================
-- Test targets
-- =============================================================================

target("umidi_test_core")
    set_kind("binary")
    set_default(false)
    add_files("test/test_core.cc")
    add_includedirs("include")
target_end()

target("umidi_test_messages")
    set_kind("binary")
    set_default(false)
    add_files("test/test_messages.cc")
    add_includedirs("include")
target_end()

target("umidi_test_protocol")
    set_kind("binary")
    set_default(false)
    add_files("test/test_protocol.cc")
    add_includedirs("include")
target_end()

target("umidi_test_extended")
    set_kind("binary")
    set_default(false)
    add_files("test/test_extended_protocol.cc")
    add_includedirs("include")
target_end()

-- =============================================================================
-- Examples
-- =============================================================================

target("umidi_example_parser")
    set_kind("binary")
    set_default(false)
    add_files("examples/basic_parser.cc")
    add_includedirs("include")
target_end()

target("umidi_example_dispatch")
    set_kind("binary")
    set_default(false)
    add_files("examples/message_dispatch.cc")
    add_includedirs("include")
target_end()

target("umidi_example_sysex")
    set_kind("binary")
    set_default(false)
    add_files("examples/sysex_protocol.cc")
    add_includedirs("include")
target_end()

-- =============================================================================
-- Test runner
-- =============================================================================

-- Run all tests: xmake run test
target("test")
    set_kind("phony")
    set_default(false)
    on_run(function (target)
        local tests = {
            "umidi_test_core",
            "umidi_test_messages",
            "umidi_test_protocol",
            "umidi_test_extended"
        }
        for _, name in ipairs(tests) do
            print("Running " .. name .. "...")
            os.execv(path.join(".build", name))
        end
    end)
    add_deps("umidi_test_core", "umidi_test_messages", "umidi_test_protocol", "umidi_test_extended")
target_end()
