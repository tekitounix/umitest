-- SPDX-License-Identifier: MIT
-- umidi Test Build Configuration
-- Standalone tests for the umidi library

-- umidi is header-only, so tests only need include paths
local umidi_includedirs = {
    "$(projectdir)/lib/umidi",
    "$(projectdir)/lib/umidi/test"
}

-- Common test settings
local function add_umidi_test(name, source)
    target("umidi_test_" .. name)
        set_kind("binary")
        set_languages("c++23")
        add_files(source)
        add_includedirs(umidi_includedirs)
        set_warnings("allextra", "error")
        add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
        set_group("umidi_tests")
end

-- Individual test targets
add_umidi_test("core", "test_core.cc")
add_umidi_test("messages", "test_messages.cc")
add_umidi_test("protocol", "test_protocol.cc")
add_umidi_test("extended_protocol", "test_extended_protocol.cc")

-- All-in-one test runner
target("umidi_test_all")
    set_kind("phony")
    set_group("umidi_tests")
    add_deps("umidi_test_core", "umidi_test_messages", "umidi_test_protocol", "umidi_test_extended_protocol")
    on_run(function (target)
        import("core.project.project")
        local tests = {"umidi_test_core", "umidi_test_messages", "umidi_test_protocol", "umidi_test_extended_protocol"}
        local failed = 0
        for _, name in ipairs(tests) do
            local t = project.target(name)
            if t then
                print("Running " .. name .. "...")
                local ok = os.execv(t:targetfile())
                if ok ~= 0 then
                    failed = failed + 1
                end
                print("")
            end
        end
        if failed > 0 then
            print("FAILED: " .. failed .. " test suite(s)")
            os.exit(1)
        else
            print("All umidi tests passed!")
        end
    end)

-- Embedded test target (for Renode)
target("umidi_test_renode")
    set_kind("binary")
    set_languages("c++23")
    add_files("test_renode.cc")
    add_includedirs(umidi_includedirs)
    set_warnings("allextra", "error")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})

    -- ARM Cortex-M4 settings for STM32F4
    set_arch("arm")
    add_cxxflags(
        "-mcpu=cortex-m4",
        "-mthumb",
        "-mfloat-abi=hard",
        "-mfpu=fpv4-sp-d16",
        "-ffunction-sections",
        "-fdata-sections",
        {force = true}
    )
    add_ldflags(
        "-mcpu=cortex-m4",
        "-mthumb",
        "-mfloat-abi=hard",
        "-mfpu=fpv4-sp-d16",
        "-Wl,--gc-sections",
        "-nostdlib",
        {force = true}
    )
    set_group("umidi_tests_embedded")
