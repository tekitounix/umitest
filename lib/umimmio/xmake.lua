-- =====================================================================
-- UMI MMIO Library
-- Type-safe register abstraction with transport-agnostic access
-- =====================================================================

local umimmio_dir = os.scriptdir()

target("umi.mmio")
    set_kind("headeronly")
    set_group("umi")

    add_includedirs(path.join(umimmio_dir, "include"), {public = true})
target_end()

-- Tests
includes("test")
