-- 単体repo判定
local standalone_repo = os.projectdir() == os.scriptdir()

if standalone_repo then
    set_project("umihal")
    set_xmakever("2.8.0")
    set_languages("c++23")
    add_rules("mode.debug", "mode.release")
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
    set_warnings("all", "extra", "error")
    add_requires("umitest", {optional = true})
end

-- 依存追加ヘルパー
function umihal_add_umitest_dep()
    if standalone_repo then
        add_packages("umitest")
    else
        add_deps("umitest")
    end
end

-- 公開ターゲット（ヘッダオンリー）
target("umihal")
    set_kind("headeronly")
    add_headerfiles("include/(umihal/**.hh)")
    add_includedirs("include", {public = true})
target_end()

-- テスト
includes("tests")
