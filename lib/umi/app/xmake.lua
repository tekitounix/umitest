add_rules("mode.debug", "mode.release")

local target_name = "umi.app"
target(target_name)
    set_kind("static")
    
    add_includedirs(".", {public = true})
    
    add_headerfiles("*.hh")
    add_headerfiles("*.ld")
    add_headerfiles("*.cc")
    
    add_deps("umi.core")
    
target_end()
