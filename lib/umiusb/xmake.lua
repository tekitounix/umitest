-- UMI-USB: Portable USB Device Stack
-- Header-only library with C++23 features

target("umiusb")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs("include", {public = true})

    -- Require C++23 for concepts and constexpr features
    set_languages("c++23")
target_end()
