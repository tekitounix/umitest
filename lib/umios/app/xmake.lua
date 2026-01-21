-- UMI-OS Application SDK
-- Provides include paths and linker script for .umiapp applications
-- Note: The actual build is handled by embedded rule in each app's xmake.lua

-- Get lib directory
local lib_dir = path.directory(path.directory(os.scriptdir()))

-- Header-only SDK for application development
target("umios_app_sdk")
    set_kind("headeronly")
    set_group("umi")
    
    -- Include paths (public so apps can use them)
    add_includedirs(os.scriptdir(), {public = true})
    add_includedirs(path.join(lib_dir, "umios/kernel"), {public = true})  -- For shared types
    
    -- Define for application code
    add_defines("UMIOS_APP=1", {public = true})
target_end()
