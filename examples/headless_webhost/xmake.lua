-- =====================================================================
-- Headless Web Host - UMI-OS Web Simulation
-- =====================================================================
-- A web-based simulation host for headless embedded applications.
-- Supports multiple backends:
--   - WASM: Direct WASM compilation (fastest, no server)
--   - Renode: Cycle-accurate hardware simulation (requires server)
--
-- Standalone build:
--   cd examples/headless_webhost && xmake -P . build
--   xmake -P . run serve
--
-- From root (via includes):
--   xmake build webhost_sim
-- =====================================================================

-- Detect if we're being included from parent project or running standalone
-- If scriptdir == projectdir, we're standalone; otherwise we're included
local is_standalone = (os.scriptdir() == os.projectdir())

if is_standalone then
    set_project("headless_webhost")
    set_version("0.1.0")
    set_xmakever("2.8.0")
    set_languages("c++23")
    add_rules("mode.debug", "mode.release")
end

-- =====================================================================
-- Path Configuration
-- =====================================================================

-- Determine paths based on context
local subproject_dir = os.scriptdir()
local project_root = is_standalone and path.absolute(subproject_dir .. "/../..") or os.projectdir()

local src_dir = path.join(subproject_dir, "src")
local build_dir = path.join(subproject_dir, "build")

-- For standalone mode, include umi package from project root
if is_standalone then
    includes(path.join(project_root, "lib/umi"))
end

-- =====================================================================
-- Emscripten Check
-- =====================================================================

local has_emscripten = os.getenv("EMSDK") ~= nil
    or os.isfile("/opt/homebrew/bin/emcc")
    or os.isfile("/usr/local/bin/emcc")
    or os.isfile("/usr/bin/emcc")

if is_standalone and not has_emscripten then
    print("Warning: Emscripten not found. WASM targets will not be available.")
    print("Install: brew install emscripten (macOS) or visit https://emscripten.org")
end

-- =====================================================================
-- WASM Target
-- =====================================================================

if has_emscripten then

local exported_funcs = "[" .. table.concat({
    "'_malloc'", "'_free'",
    -- Simulation API
    "'_umi_sim_init'", "'_umi_sim_reset'", "'_umi_sim_process'",
    "'_umi_sim_note_on'", "'_umi_sim_note_off'", "'_umi_sim_cc'", "'_umi_sim_midi'",
    "'_umi_sim_load'", "'_umi_sim_position_lo'", "'_umi_sim_position_hi'",
    "'_umi_sim_get_name'", "'_umi_sim_get_vendor'", "'_umi_sim_get_version'",
    -- UMIM-compatible API
    "'_umi_create'", "'_umi_destroy'", "'_umi_process'",
    "'_umi_note_on'", "'_umi_note_off'",
    "'_umi_get_processor_name'", "'_umi_get_name'", "'_umi_get_vendor'", "'_umi_get_version'",
    "'_umi_get_type'", "'_umi_get_param_count'", "'_umi_set_param'", "'_umi_get_param'",
    "'_umi_get_param_name'", "'_umi_get_param_min'", "'_umi_get_param_max'",
    "'_umi_get_param_default'", "'_umi_get_param_curve'", "'_umi_get_param_id'",
    "'_umi_get_param_unit'", "'_umi_process_cc'"
}, ",") .. "]"

-- UMIM exports (lightweight DSP-only)
local umim_exported_funcs = "[" .. table.concat({
    "'_malloc'", "'_free'",
    "'_umi_create'", "'_umi_destroy'", "'_umi_process'",
    "'_umi_note_on'", "'_umi_note_off'",
    "'_umi_get_processor_name'", "'_umi_get_name'", "'_umi_get_vendor'", "'_umi_get_version'",
    "'_umi_get_type'", "'_umi_get_param_count'", "'_umi_set_param'", "'_umi_get_param'",
    "'_umi_get_param_name'", "'_umi_get_param_min'", "'_umi_get_param_max'",
    "'_umi_get_param_default'", "'_umi_get_param_curve'", "'_umi_get_param_id'",
    "'_umi_get_param_unit'", "'_umi_process_cc'"
}, ",") .. "]"

-- Common WASM settings
-- AudioWorklet loads WASM directly via WebAssembly.instantiate()
-- Uses -O3 which minifies imports to 'a' module (matches synth_sim_worklet.js)
-- NOTE: --no-entry is required for standalone WASM (no main function)
--       -sSTANDALONE_WASM=1 removes env/wasi dependencies
local wasm_cxflags = {"-fno-exceptions", "-fno-rtti", "-O3"}
local wasm_ldflags = {
    "-sWASM=1",
    "-sALLOW_MEMORY_GROWTH=0",
    "-sSTACK_SIZE=65536",
    "-sINITIAL_MEMORY=1048576",
    "-sERROR_ON_UNDEFINED_SYMBOLS=0",  -- Allow undefined imports (worklet provides them)
    "-sSTANDALONE_WASM=1",
    "--no-entry"
}

-- UMIM-specific flags: Minimal Emscripten WASM for AudioWorklet
-- Uses same flags as UMI-OS backend for consistency
-- NOTE: --no-entry is required for standalone WASM (no main function)
--       -sEXPORT_KEEPALIVE=1 prevents export name minification with -O3
local umim_ldflags = {
    "-sWASM=1",
    "-sALLOW_MEMORY_GROWTH=0",
    "-sSTACK_SIZE=65536",
    "-sINITIAL_MEMORY=1048576",
    "-sERROR_ON_UNDEFINED_SYMBOLS=0",
    "-sEXPORT_KEEPALIVE=1",
    "--no-entry"
}

-- UMIM Backend (lightweight DSP-only, no kernel)
target("umim_synth")
    set_kind("binary")
    set_group("examples")
    set_default(false)
    set_languages("c++23")
    set_plat("wasm")
    set_arch("wasm32")
    set_toolchains("emcc")
    set_targetdir(build_dir)
    set_filename("umim_synth.wasm")
    add_files(path.join(src_dir, "umim_synth.cc"))
    add_deps("umi.dsp")
    add_includedirs(src_dir)
    for _, flag in ipairs(wasm_cxflags) do add_cxflags(flag, {force = true}) end
    for _, flag in ipairs(umim_ldflags) do add_ldflags(flag, {force = true}) end
    add_ldflags("-sEXPORTED_FUNCTIONS=" .. umim_exported_funcs, {force = true})
target_end()

-- UMI-OS Backend (full kernel simulation)
target("webhost_sim")
    set_kind("binary")
    set_group("examples")
    set_default(is_standalone)
    set_languages("c++23")
    set_plat("wasm")
    set_arch("wasm32")
    set_toolchains("emcc")
    set_targetdir(build_dir)
    set_filename("webhost_sim.wasm")
    add_files(path.join(src_dir, "synth_sim.cc"))
    add_deps("umi.wasm.full")
    add_includedirs(path.join(project_root, "lib/umi/synth/include"))
    add_includedirs(path.join(project_root, "lib/umi/port/platform/wasm"))
    add_includedirs(path.join(project_root, "lib/umi"))
    add_includedirs(src_dir)
    for _, flag in ipairs(wasm_cxflags) do add_cxflags(flag, {force = true}) end
    for _, flag in ipairs(wasm_ldflags) do add_ldflags(flag, {force = true}) end
    add_ldflags("-sEXPORTED_FUNCTIONS=" .. exported_funcs, {force = true})
target_end()

end  -- if has_emscripten

-- =====================================================================
-- Tasks (standalone only)
-- =====================================================================

if is_standalone then

task("serve")
    set_category("action")
    on_run(function ()
        print("Building WASM module...")
        os.exec("xmake -P . build webhost_sim")

        -- Copy WASM files to web directory
        local script_dir = os.scriptdir()
        os.cp(path.join(script_dir, "build/webhost_sim.js"), path.join(script_dir, "web/"))
        os.cp(path.join(script_dir, "build/webhost_sim.wasm"), path.join(script_dir, "web/"))

        print("\nStarting local server...")
        print("Open: http://localhost:8080/")
        os.exec("cd " .. path.join(script_dir, "web") .. " && python3 -m http.server 8080")
    end)
    set_menu {usage = "xmake -P . run serve", description = "Build and serve web host"}
task_end()

task("clean-all")
    set_category("action")
    on_run(function ()
        local script_dir = os.scriptdir()
        os.tryrm(path.join(script_dir, "build"))
        os.tryrm(path.join(script_dir, ".xmake"))
        os.tryrm(path.join(script_dir, "web/webhost_sim.js"))
        os.tryrm(path.join(script_dir, "web/webhost_sim.wasm"))
        print("Cleaned build artifacts")
    end)
    set_menu {usage = "xmake -P . clean-all", description = "Remove all build artifacts"}
task_end()

end  -- if is_standalone
