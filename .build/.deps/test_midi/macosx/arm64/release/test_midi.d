{
    files = {
        ".build/.objs/test_midi/macosx/arm64/release/test/test_midi.cc.o"
    },
    values = {
        "/Library/Developer/CommandLineTools/usr/bin/clang++",
        {
            "-isysroot",
            "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
            "-lz",
            "-Wl,-x",
            "-Wl,-dead_strip"
        }
    }
}