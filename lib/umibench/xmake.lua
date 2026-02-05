target("umibench")
    set_kind("headeronly")
    add_headerfiles("include/(umibench/**.hh)")
    add_includedirs("include", { public = true })

-- Host tests
includes("test")

-- Embedded targets (future: migrate from lib/umi/bench)
-- includes("target/stm32f4")
