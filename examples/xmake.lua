target("test_umitest_examples")
    add_rules("host.test")
    set_default(true)
    add_deps("umitest")

    for _, f in ipairs(os.files(path.join(os.scriptdir(), "*.cc"))) do
        local name = path.basename(f)
        add_tests(name, {files = path.filename(f), build_should_pass = true})
    end
target_end()
