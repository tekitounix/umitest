add_rules("mode.debug", "mode.release")

local target_name = "umi_app"
target(target_name)
    set_kind("static")
    add_rules("coding.umi_library")
    
    add_includedirs(".", {public = true})
    
    add_headerfiles("*.hh")
    add_headerfiles("*.ld")
    add_headerfiles("*.cc")
    
    add_deps("umi_core")
    
target_end()
