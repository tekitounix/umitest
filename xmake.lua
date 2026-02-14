local standalone_repo = os.projectdir() == os.scriptdir()
UMITEST_STANDALONE_REPO = standalone_repo

if standalone_repo then
    set_project("umitest")
    set_version("0.1.0")
    set_xmakever("2.8.0")

    set_languages("c++23")
    add_rules("mode.debug", "mode.release")
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
    set_warnings("all", "extra", "error")

    add_requires("arm-embedded", {optional = true})
    add_requires("umimmio", {optional = true})
end

function umitest_add_umimmio_dep()
    if standalone_repo then
        add_packages("umimmio")
    else
        add_deps("umimmio")
    end
end

target("umitest")
    set_kind("headeronly")
    add_headerfiles("include/(umitest/**.hh)")
    add_includedirs("include", { public = true })

-- Host tests + ARM embedded targets
includes("tests")

-- WASM target
includes("platforms/wasm")
