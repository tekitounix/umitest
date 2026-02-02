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
