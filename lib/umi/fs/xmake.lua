add_rules("mode.debug", "mode.release")

-- =====================================================================
-- FATfs C++23 port (static library)
-- =====================================================================

target("umi.fs.fatfs")
    set_kind("static")
    add_includedirs("fat", {public = true})
    add_includedirs(".", {public = true})

    -- Source files
    add_files("fat/fat_core.cc")
    add_files("fat/ff_unicode.cc")

    -- Header files
    add_headerfiles("fat/*.hh")

    -- Dependencies
    add_deps("umi.core")
target_end()

-- =====================================================================
-- slimfs (static library)
-- =====================================================================

target("umi.fs.slimfs")
    set_kind("static")
    add_includedirs("slim", {public = true})
    add_includedirs(".", {public = true})

    -- Source files
    add_files("slim/slim_core.cc")

    -- Header files
    add_headerfiles("slim/*.hh")

    -- Dependencies
    add_deps("umi.core")
target_end()
