-- umiboot: Bootloader and Firmware Update Library
-- Standalone build configuration

set_project("umiboot")
set_version("1.0.0")
set_languages("c++23")

add_rules("mode.debug", "mode.release")
set_warnings("all", "extra")
add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})

-- Header-only library
target("umiboot")
    set_kind("headeronly")
    add_headerfiles("include/(umiboot/**.hh)")
    add_includedirs("include", {public = true})
target_end()

-- Test: Authentication
target("umiboot_test_auth")
    set_kind("binary")
    set_default(false)
    add_files("test/test_auth.cc")
    add_includedirs("include")
target_end()

-- Test: Firmware validation
target("umiboot_test_firmware")
    set_kind("binary")
    set_default(false)
    add_files("test/test_firmware.cc")
    add_includedirs("include")
target_end()

-- Test: Session management
target("umiboot_test_session")
    set_kind("binary")
    set_default(false)
    add_files("test/test_session.cc")
    add_includedirs("include")
target_end()

-- Test runner
target("test")
    set_kind("phony")
    set_default(false)
    on_run(function (target)
        local tests = {"umiboot_test_auth", "umiboot_test_firmware", "umiboot_test_session"}
        for _, name in ipairs(tests) do
            print("Running " .. name .. "...")
            os.execv(path.join("build", name))
        end
        print("All tests passed!")
    end)
    add_deps("umiboot_test_auth", "umiboot_test_firmware", "umiboot_test_session")
target_end()
