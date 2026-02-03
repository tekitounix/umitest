add_rules("mode.debug", "mode.release")

-- Target definition
local target_name = "umi.adapter"
target(target_name)
    set_kind("static")
    
    -- Include directory
    add_includedirs(".", {public = true})
    
    -- Header files only (header-only library)
    add_headerfiles("*.hh")
    add_headerfiles("web/*.hh")
    add_headerfiles("web/*.js")
    
    -- Dependencies
    add_deps("umi.core", "umi.kernel")
    
target_end()
