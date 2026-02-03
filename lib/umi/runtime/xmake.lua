add_rules("mode.debug", "mode.release")

-- Target definition
local target_name = "umi.runtime"
target(target_name)
    set_kind("static")
    
    -- Include directory
    add_includedirs(".", {public = true})
    
    -- Header files only (header-only library)
    add_headerfiles("*.hh")
    
    -- Dependencies
    add_deps("umi.core")
    
    -- Tests
    for _, file in ipairs(os.files("test/**/test_*.cc")) do
        local name = path.basename(file)
        add_tests(name, {files = file, deps = {target_name}})
    end
target_end()
