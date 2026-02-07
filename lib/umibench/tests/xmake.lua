target("test_umibench")
    add_rules("host.test")
    add_tests("default")
    set_default(true)
    set_kind("binary")
    add_files("test_*.cc")
    add_deps("umibench_host")
    umibench_add_umitest_dep()

target("test_umibench_compile_fail")
    set_kind("phony")
    set_default(false)
    add_tests("calibrate_zero")

    on_test(function()
        import("lib.detect.find_tool")

        local cxx = find_tool("c++") or find_tool("g++") or find_tool("clang++")
        assert(cxx and cxx.program, "no host C++ compiler found for compile-fail test")

        local source = path.join(os.scriptdir(), "compile_fail", "calibrate_zero.cc")
        local include_dir = path.join(os.scriptdir(), "..", "include")
        local object = os.tmpfile() .. ".o"

        local ok = false
        local err = ""
        try {
            function()
                os.iorunv(cxx.program, {"-std=c++23", "-I" .. include_dir, "-c", source, "-o", object})
                ok = true
            end,
            catch {
                function(errors)
                    err = tostring(errors)
                end
            }
        }

        os.tryrm(object)

        if ok then
            raise("compile-fail test failed: calibrate<0>() compiled successfully")
        end

        local message = tostring(err)
        if #message == 0 then
            raise("compile-fail test failed: expected compiler diagnostics were empty")
        end

        return true
    end)
