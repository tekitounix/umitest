target("test_umitest_examples")
    set_kind("binary")
    set_default(false)
    add_deps("umitest")

    for _, f in ipairs(os.files(path.join(os.scriptdir(), "*.cc"))) do
        local name = path.basename(f)
        add_tests(name, {files = path.filename(f), build_should_pass = true})
    end
target_end()
