-- umimock library build configuration
-- Standalone xmake.lua for building and testing umimock independently

set_project("umimock")
set_version("1.0.0")

set_languages("c++23")
add_rules("mode.debug", "mode.release")

-- Compiler settings
set_warnings("all", "extra")
add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})

-- Header-only library target
target("umimock")
    set_kind("headeronly")
    add_headerfiles("include/(umimock/**.hh)")
    add_includedirs("include", {public = true})
target_end()

-- Include test targets
includes("test")
