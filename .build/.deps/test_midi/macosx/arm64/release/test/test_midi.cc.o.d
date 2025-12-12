{
    depfiles = ".build/.objs/test_midi/macosx/arm64/release/test/__cpp_test_midi.cc.cc:   test/test_midi.cc test/../core/umi_midi.hh test/../core/umi_kernel.hh\
",
    depfiles_format = "gcc",
    files = {
        "test/test_midi.cc"
    },
    values = {
        "/Library/Developer/CommandLineTools/usr/bin/clang",
        {
            "-Qunused-arguments",
            "-isysroot",
            "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O3",
            "-std=c++23",
            "-I.",
            "-Icore",
            "-Iport",
            "-fno-exceptions",
            "-fno-rtti",
            "-DNDEBUG"
        }
    }
}