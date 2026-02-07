local has_emscripten = os.getenv("EMSDK") ~= nil
    or os.isfile("/opt/homebrew/bin/emcc")
    or os.isfile("/usr/local/bin/emcc")
    or os.isfile("/usr/bin/emcc")

if has_emscripten then

target("umibench_wasm")
    set_kind("binary")
    set_default(false)
    set_group("wasm")
    set_languages("c++23")
    set_plat("wasm")
    set_arch("wasm32")
    set_toolchains("emcc")
    set_filename("umibench_wasm.js")
    add_tests("default")

    add_files("../../tests/test_*.cc")
    add_deps("umibench_wasm_platform")
    umibench_add_umitest_dep()

    add_cxflags("-fno-exceptions", "-fno-rtti", {force = true})

    add_ldflags("-sEXPORTED_FUNCTIONS=['_main']", {force = true})

    on_run(function(target)
        local node = "node"
        if os.isfile("/opt/homebrew/bin/node") then
            node = "/opt/homebrew/bin/node"
        elseif os.isfile("/usr/local/bin/node") then
            node = "/usr/local/bin/node"
        elseif os.isfile("/usr/bin/node") then
            node = "/usr/bin/node"
        end
        local out = os.iorunv(node, {target:targetfile()})
        if out and #out > 0 then
            print(out)
        end
    end)

    on_test(function(target)
        local node = "node"
        if os.isfile("/opt/homebrew/bin/node") then
            node = "/opt/homebrew/bin/node"
        elseif os.isfile("/usr/local/bin/node") then
            node = "/usr/local/bin/node"
        elseif os.isfile("/usr/bin/node") then
            node = "/usr/bin/node"
        end
        local out = os.iorunv(node, {target:targetfile()})
        if out and #out > 0 then
            print(out)
        end
        return true
    end)
target_end()

end
