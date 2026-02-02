-- MMIO Library Tests
-- Note: tests use assert() for verification; suppress unused-variable
-- warnings that appear when NDEBUG strips assert expressions.

local function mmio_test(name, file, test_name)
    target(name)
        set_kind("binary")
        set_group("test")
        set_default(false)
        set_languages("c++23")
        add_cxflags("-Wno-unused-variable", "-Wno-unused-but-set-variable")

        add_deps("umi.mmio")
        add_files(file)
        add_tests(test_name)
    target_end()
end

mmio_test("test_mmio", "test.cc", "mmio")
mmio_test("test_mmio_assert", "test_assert.cc", "mmio_assert")
mmio_test("test_mmio_register_value", "test_register_value.cc", "mmio_register_value")
mmio_test("test_mmio_value_get", "test_value_get.cc", "mmio_value_get")

-- CS43L22 device driver test (needs umiport/device/ include path)
target("test_mmio_cs43l22")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")
    add_cxflags("-Wno-unused-variable", "-Wno-unused-but-set-variable")

    add_deps("umi.mmio")
    add_includedirs(path.join(os.scriptdir(), "../../umiport/device"), {public = false})
    add_files("test_cs43l22.cc")
    add_tests("mmio_cs43l22")
target_end()
