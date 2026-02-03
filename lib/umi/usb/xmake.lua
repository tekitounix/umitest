add_rules("mode.debug", "mode.release")

target("umi.usb")
    set_kind("headeronly")
    add_includedirs("include", {public = true})

    -- Header files
    add_headerfiles("include/(**.hh)")

    -- Dependencies
    add_deps("umi.core", "umi.dsp")
target_end()
