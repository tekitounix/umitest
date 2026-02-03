add_rules("mode.debug", "mode.release")

target("umi_usb")
    set_kind("headeronly")
    add_rules("coding.umi_library")
    add_includedirs("include", {public = true})

    -- Header files
    add_headerfiles("include/(**.hh)")

    -- Dependencies
    add_deps("umi_core", "umi_dsp")
target_end()
