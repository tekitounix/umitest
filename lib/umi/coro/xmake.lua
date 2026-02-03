add_rules("mode.debug", "mode.release")

target("umi.coro")
    set_kind("static")
    add_includedirs(".", {public = true})
    add_headerfiles("*.hh")
    add_deps("umi.core")
target_end()
