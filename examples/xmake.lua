-- Each example is a standalone binary with its own main().
-- Build-tested individually to verify examples compile and link.

for _, f in ipairs(os.files(path.join(os.scriptdir(), "*.cc"))) do
    local name = "test_umitest_example_" .. path.basename(f)
    target(name)
        set_kind("binary")
        set_default(false)
        add_files(f)
        add_deps("umitest")
        add_tests("build", {build_should_pass = true})
    target_end()
end
