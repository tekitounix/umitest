target("umitest")
    set_kind("headeronly")
    set_license("MIT")
    add_headerfiles("include/(umitest/**.hh)")
    add_includedirs("include", {public = true})

includes("tests", "examples")
