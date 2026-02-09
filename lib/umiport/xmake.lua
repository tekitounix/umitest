includes("rules/board.lua")

target("umiport")
    set_kind("headeronly")
    add_headerfiles("include/(umiport/**.hh)")
    add_includedirs("include", { public = true })
    add_deps("umihal")
