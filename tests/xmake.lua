target("test_umitest")
    set_kind("binary")
    set_default(false)
    add_files("test_*.cc")
    add_deps("umitest")
    add_tests("default")

    for _, f in ipairs(os.files(path.join(os.scriptdir(), "smoke", "*.cc"))) do
        local name = "smoke_" .. path.basename(f)
        add_tests(name, {files = path.join("smoke", path.filename(f)), build_should_pass = true})
    end

    for _, f in ipairs(os.files(path.join(os.scriptdir(), "compile_fail", "*.cc"))) do
        local name = "fail_" .. path.basename(f)
        add_tests(name, {files = path.join("compile_fail", path.filename(f)), build_should_fail = true})
    end
target_end()
