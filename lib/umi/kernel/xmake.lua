add_rules("mode.debug", "mode.release")

target("umi.kernel")
    set_kind("static")
    add_includedirs(".", {public = true})

    -- Source files
    add_files("*.cc")

    -- Header files
    add_headerfiles("*.hh")
    add_headerfiles("syscall/*.hh")

    -- Dependencies
    add_deps("umi.core", "umi.runtime", "umi.port", "umi.service")
target_end()
