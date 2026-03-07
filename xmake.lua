target("umitest")
    set_kind("headeronly")
    add_headerfiles("include/(umitest/**.hh)")
    add_includedirs("include", {public = true})

includes("tests", "examples")
