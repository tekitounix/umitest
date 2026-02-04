-- MMIO Library Tests

local test_dir = os.scriptdir()

local function mmio_test(name, file)
    target(name)
        add_rules("host.test")
        add_tests("default")
        set_group("tests/mmio")
        set_default(false)
        add_deps("umi.mmio", "umi.test")
        add_files(path.join(test_dir, file))
        add_cxxflags("-Wno-unused-variable", "-Wno-unused-but-set-variable", {force = true})
    target_end()
end

mmio_test("test_umi_mmio", "test.cc")
mmio_test("test_umi_mmio_assert", "test_assert.cc")
mmio_test("test_umi_mmio_register_value", "test_register_value.cc")
mmio_test("test_umi_mmio_value_get", "test_value_get.cc")
