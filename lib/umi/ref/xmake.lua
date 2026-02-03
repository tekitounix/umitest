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
