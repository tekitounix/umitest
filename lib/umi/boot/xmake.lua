add_rules("mode.debug", "mode.release")

local target_name = "umi.boot"
target(target_name)
    set_kind("static")
    
    add_includedirs(".", {public = true})
    add_includedirs("include", {public = true})
    
    add_headerfiles("*.hh")
    add_headerfiles("include/*.hh")
    
    add_deps("umi.crypto")
    
target_end()
