local standalone_repo = os.projectdir() == os.scriptdir()
UMIBENCH_STANDALONE_REPO = standalone_repo

if standalone_repo then
    set_project("umibench")
    set_version("0.1.0")
    set_xmakever("2.8.0")

    set_languages("c++23")
    add_rules("mode.debug", "mode.release")
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
    set_warnings("all", "extra", "error")

    -- Standalone builds resolve shared UMI libs through xmake packages.
    add_requires("arm-embedded", {optional = true})
    add_requires("umimmio", {optional = true})
    add_requires("umitest", {optional = true})
end

function umibench_add_umimmio_dep()
    if standalone_repo then
        add_packages("umimmio")
    else
        add_deps("umimmio")
    end
end

function umibench_add_umitest_dep()
    if standalone_repo then
        add_packages("umitest")
    else
        add_deps("umitest")
    end
end

target("umibench_common")
    set_kind("headeronly")
    add_headerfiles("include/(umibench/**.hh)")
    add_includedirs("include", { public = true })

target("umibench_host")
    set_kind("headeronly")
    add_deps("umibench_common")
    add_defines("UMIBENCH_HOST")
    add_includedirs("platforms/host", { public = true })

target("umibench_wasm_platform")
    set_kind("headeronly")
    add_deps("umibench_common")
    add_defines("UMIBENCH_WASM")
    add_includedirs("platforms/wasm", { public = true })

target("umibench_embedded")
    set_kind("headeronly")
    add_deps("umibench_common")
    umibench_add_umimmio_dep()
    add_defines("UMIBENCH_EMBEDDED")

-- Host tests
includes("tests")

-- Embedded targets (STM32F4 with Renode)
includes("platforms/arm/cortex-m/stm32f4")

-- WASM target
includes("platforms/wasm")
