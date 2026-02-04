add_rules("mode.debug", "mode.release")

target("umi.midi")
    set_kind("headeronly")
    add_includedirs("include", {public = true})

    -- Header files
    add_headerfiles("include/(**.hh)")

    -- Dependencies
    add_deps("umi.core")
target_end()

-- umidi library tests
for _, test in ipairs({
    {"umidi_test_core", "test/test_core.cc"},
    {"umidi_test_messages", "test/test_messages.cc"},
    {"umidi_test_protocol", "test/test_protocol.cc"},
    {"umidi_test_extended", "test/test_extended_protocol.cc"},
}) do
    target(test[1])
        add_rules("host.test")
        add_tests("default")
        set_group("tests/umidi")
        add_deps("umi.midi", "umi.boot", "umitest")
        add_files(test[2])
        add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    target_end()
end
