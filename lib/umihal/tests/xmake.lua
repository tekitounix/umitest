target("test_umihal")
    add_rules("host.test")
    add_tests("default")
    set_default(true)
    set_kind("binary")
    add_files("test_*.cc")
    add_deps("umihal")
    umihal_add_umitest_dep()
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

target("test_umihal_compile_fail")
    set_kind("phony")
    set_default(false)
    add_tests("platform_concept", "transport_concept")

    on_test(function()
        import("lib.detect.find_tool")

        local cxx = find_tool("c++") or find_tool("g++") or find_tool("clang++")
        assert(cxx and cxx.program, "no host C++ compiler found for compile-fail test")

        local include_dir = path.join(os.scriptdir(), "..", "include")

        local test_cases = {"platform_concept", "transport_concept"}
        for _, name in ipairs(test_cases) do
            local source = path.join(os.scriptdir(), "compile_fail", name .. ".cc")
            local object = os.tmpfile() .. ".o"

            local ok = false
            try {
                function()
                    os.iorunv(cxx.program, {"-std=c++23", "-I" .. include_dir, "-c", source, "-o", object})
                    ok = true
                end,
                catch {
                    function() end
                }
            }

            os.tryrm(object)

            if ok then
                raise("compile-fail test '%s' failed: compiled successfully", name)
            end
        end

        return true
    end)
target_end()
