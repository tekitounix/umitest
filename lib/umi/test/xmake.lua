add_rules("mode.debug", "mode.release")

includes("umitest")

local target_name = "umi.test"
target(target_name)
    set_kind("headeronly")
    
    add_includedirs(".", {public = true})
    add_includedirs("include", {public = true})
    
    add_headerfiles("*.hh")
    add_headerfiles("include/*.hh")
    
target_end()
