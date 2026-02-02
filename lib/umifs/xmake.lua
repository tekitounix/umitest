-- =====================================================================
-- UMI Filesystem Library (umifs)
-- =====================================================================
-- Usage:
--   includes("lib/umifs")
--   target("my_app")
--       add_deps("umi.fs.fatfs")     -- FATfs C++23 port
--       add_deps("umi.fs.slimfs")    -- slimfs
-- =====================================================================

local umifs_dir = os.scriptdir()
local lib_dir = path.directory(umifs_dir)

-- =====================================================================
-- FATfs C++23 port (static library)
-- =====================================================================

target("umi.fs.fatfs")
    set_kind("static")
    set_group("umi")
    add_deps("umi.kernel")

    add_files(path.join(umifs_dir, "fat/fat_core.cc"))
    add_files(path.join(umifs_dir, "fat/ff_unicode.cc"))
    add_includedirs(path.join(umifs_dir, "fat"), {public = true})
    add_includedirs(lib_dir, {public = true})
target_end()

-- =====================================================================
-- slimfs (static library)
-- =====================================================================

target("umi.fs.slimfs")
    set_kind("static")
    set_group("umi")
    add_deps("umi.kernel")

    add_files(path.join(umifs_dir, "slim/slim_core.cc"))
    add_includedirs(path.join(umifs_dir, "slim"), {public = true})
    add_includedirs(lib_dir, {public = true})
target_end()
