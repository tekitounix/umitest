add_rules("mode.debug", "mode.release")

local target_name = "umi.ref"
target(target_name)
    set_kind("static")
    
    add_includedirs(".", {public = true})
    add_includedirs("include", {public = true})
    
    add_headerfiles("*.hh")
    add_headerfiles("include/*.hh")
    
    add_deps("umi.core")
    
target_end()

-- umimock WASM test
local has_emscripten = os.getenv("EMSDK") ~= nil
    or os.isfile("/opt/homebrew/bin/emcc")
    or os.isfile("/usr/local/bin/emcc")
    or os.isfile("/usr/bin/emcc")

if has_emscripten then

target("umimock_wasm")
    set_kind("binary")
    set_default(false)
    set_group("wasm")
    set_languages("c++23")
    set_plat("wasm")
    set_arch("wasm32")
    set_toolchains("emcc")
    set_filename("umimock_wasm.js")
    add_files("test/test_mock_wasm.cc")
    add_includedirs("include")
    add_cxflags("-fno-exceptions", "-fno-rtti", {force = true})
    add_ldflags("-sEXPORTED_FUNCTIONS=['_main','_umimock_constant','_umimock_ramp_first','_umimock_set_and_get','_umimock_reset_value','_umimock_fill_buffer_check']", {force = true})
    add_ldflags("-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']", {force = true})
    add_ldflags("-sMODULARIZE=1", "-sEXPORT_NAME='createUmimock'", {force = true})
target_end()

end
