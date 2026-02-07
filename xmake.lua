set_project("umi")
set_version("0.2.0")
set_xmakever("2.8.0")

set_languages("c++23")
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
set_warnings("all", "extra", "error")

includes("lib/umimmio")
includes("lib/umitest")
includes("lib/umibench")
includes("lib/umirtm")

includes("tools/release.lua")
