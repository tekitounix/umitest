{
    files = {
        ".build/.objs/test_kernel/macosx/arm64/release/test/test_kernel.cc.o"
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