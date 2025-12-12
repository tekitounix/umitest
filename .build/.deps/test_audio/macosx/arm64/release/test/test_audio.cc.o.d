{
    depfiles = ".build/.objs/test_audio/macosx/arm64/release/test/__cpp_test_audio.cc.cc:   test/test_audio.cc test/../core/umi_audio.hh   test/../core/umi_kernel.hh test/../core/umi_midi.hh\
",
    depfiles_format = "gcc",
    files = {
        "test/test_audio.cc"
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