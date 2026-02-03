add_rules("mode.debug", "mode.release")

local target_name = "umi.mmio"
target(target_name)
    set_kind("static")
    set_group("umi")

    add_includedirs(".", {public = true})

    add_headerfiles("*.hh")
    add_headerfiles("transport/*.hh")

    add_deps("umi.test")

    add_files("mmio.cc")

target_end()
