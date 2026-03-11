target("umitest")
    set_kind("headeronly")
    set_license("MIT")
    add_headerfiles("include/(umitest/**.hh)")
    add_includedirs("include", {public = true})

    set_values("publish", true)
    set_values("publish.description", "Zero-macro lightweight test framework for C++23")
    set_values("publish.remote", "umitest-public")
    set_values("publish.main_header", "umitest/test.hh")
    set_values("publish.test_init", 'umi::test::Suite s("pkg_test");')

includes("tests", "examples")
