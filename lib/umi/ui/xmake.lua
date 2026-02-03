add_rules("mode.debug", "mode.release")

local target_name = "umi_ui"
target(target_name)
    set_kind("static")
    add_rules("coding.umi_library")
    
    add_includedirs(".", {public = true})
    
    add_headerfiles("*.hh")
    
target_end()
