// SPDX-License-Identifier: MIT
// Unit tests for littlefs C++23 port

#include "test_common.hh"

#include <umios/fs/littlefs/lfs.hh>
#include <umios/fs/littlefs/lfs_config.hh>
#include <cstring>

using namespace umi::fs::lfs;

// ============================================================================
// RAM Block Device for testing
// ============================================================================

static constexpr uint32_t BLOCK_SIZE = 256;
static constexpr uint32_t BLOCK_COUNT = 64;
static constexpr uint32_t TOTAL_SIZE = BLOCK_SIZE * BLOCK_COUNT;

struct RamBlockDevice {
    uint8_t storage[TOTAL_SIZE]{};

    int read(uint32_t block, uint32_t offset, void* buffer, uint32_t size) {
        if (block >= BLOCK_COUNT || offset + size > BLOCK_SIZE) {
            return -1;
        }
        std::memcpy(buffer, &storage[block * BLOCK_SIZE + offset], size);
        return 0;
    }

    int write(uint32_t block, uint32_t offset, const void* buffer, uint32_t size) {
        if (block >= BLOCK_COUNT || offset + size > BLOCK_SIZE) {
            return -1;
        }
        std::memcpy(&storage[block * BLOCK_SIZE + offset], buffer, size);
        return 0;
    }

    int erase(uint32_t block) {
        if (block >= BLOCK_COUNT) {
            return -1;
        }
        std::memset(&storage[block * BLOCK_SIZE], 0xFF, BLOCK_SIZE);
        return 0;
    }

    uint32_t block_size() const { return BLOCK_SIZE; }
    uint32_t block_count() const { return BLOCK_COUNT; }
};

// ============================================================================
// Tests
// ============================================================================

static RamBlockDevice ram_dev;

// Buffers for littlefs (provided statically, no malloc)
static uint8_t read_buf[BLOCK_SIZE];
static uint8_t prog_buf[BLOCK_SIZE];
static uint8_t lookahead_buf[16];  // 16 bytes = 128 blocks of lookahead
static uint8_t file_buf[BLOCK_SIZE];

static void test_format_and_mount() {
    SECTION("Format and Mount");

    Lfs lfs;
    auto cfg = make_lfs_config(ram_dev, BLOCK_SIZE, 16, read_buf, prog_buf, lookahead_buf);

    int err = lfs.format(&cfg);
    CHECK(err == 0, "format succeeds");

    err = lfs.mount(&cfg);
    CHECK(err == 0, "mount succeeds");

    err = lfs.unmount();
    CHECK(err == 0, "unmount succeeds");
}

static void test_file_write_read() {
    SECTION("File Write/Read");

    Lfs lfs;
    auto cfg = make_lfs_config(ram_dev, BLOCK_SIZE, 16, read_buf, prog_buf, lookahead_buf);

    int err = lfs.format(&cfg);
    CHECK(err == 0, "format");
    err = lfs.mount(&cfg);
    CHECK(err == 0, "mount");

    // Write a file
    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    err = lfs.file_opencfg(&file, "hello.txt",
                           static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT | LfsOpenFlags::TRUNC), &fcfg);
    CHECK(err == 0, "file open for write");

    const char* msg = "Hello, littlefs!";
    lfs_ssize_t written = lfs.file_write(&file, msg, std::strlen(msg));
    CHECK(written == static_cast<lfs_ssize_t>(std::strlen(msg)), "file write");

    err = lfs.file_close(&file);
    CHECK(err == 0, "file close after write");

    // Read it back
    LfsFile file2{};
    LfsFileConfig fcfg2{};
    fcfg2.buffer = file_buf;

    err = lfs.file_opencfg(&file2, "hello.txt", static_cast<int>(LfsOpenFlags::RDONLY), &fcfg2);
    CHECK(err == 0, "file open for read");

    char buf[64]{};
    lfs_ssize_t nread = lfs.file_read(&file2, buf, sizeof(buf));
    CHECK(nread == static_cast<lfs_ssize_t>(std::strlen(msg)), "file read size");
    CHECK(std::memcmp(buf, msg, std::strlen(msg)) == 0, "file content matches");

    err = lfs.file_close(&file2);
    CHECK(err == 0, "file close after read");

    err = lfs.unmount();
    CHECK(err == 0, "unmount");
}

static void test_mkdir_and_stat() {
    SECTION("Mkdir and Stat");

    Lfs lfs;
    auto cfg = make_lfs_config(ram_dev, BLOCK_SIZE, 16, read_buf, prog_buf, lookahead_buf);

    lfs.format(&cfg);
    lfs.mount(&cfg);

    int err = lfs.mkdir("mydir");
    CHECK(err == 0, "mkdir");

    LfsInfo info{};
    err = lfs.stat("mydir", &info);
    CHECK(err == 0, "stat");
    CHECK(info.type == static_cast<uint8_t>(LfsType::DIR), "is directory");

    err = lfs.unmount();
    CHECK(err == 0, "unmount");
}

static void test_dir_read() {
    SECTION("Directory Read");

    Lfs lfs;
    auto cfg = make_lfs_config(ram_dev, BLOCK_SIZE, 16, read_buf, prog_buf, lookahead_buf);

    lfs.format(&cfg);
    lfs.mount(&cfg);

    // Create a file and a directory
    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;
    lfs.file_opencfg(&file, "test.bin",
                     static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    uint8_t data = 42;
    lfs.file_write(&file, &data, 1);
    lfs.file_close(&file);

    lfs.mkdir("subdir");

    // Read root directory
    LfsDir dir{};
    int err = lfs.dir_open(&dir, "/");
    CHECK(err == 0, "dir open root");

    int count = 0;
    LfsInfo info{};
    while (lfs.dir_read(&dir, &info) > 0) {
        count++;
    }
    // Expect: ".", "..", "test.bin", "subdir" = 4 entries
    CHECK(count == 4, "root has 4 entries (., .., test.bin, subdir)");

    lfs.dir_close(&dir);
    lfs.unmount();
}

static void test_remove() {
    SECTION("Remove");

    Lfs lfs;
    auto cfg = make_lfs_config(ram_dev, BLOCK_SIZE, 16, read_buf, prog_buf, lookahead_buf);

    lfs.format(&cfg);
    lfs.mount(&cfg);

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;
    lfs.file_opencfg(&file, "deleteme.txt",
                     static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    lfs.file_close(&file);

    LfsInfo info{};
    int err = lfs.stat("deleteme.txt", &info);
    CHECK(err == 0, "file exists before remove");

    err = lfs.remove("deleteme.txt");
    CHECK(err == 0, "remove succeeds");

    err = lfs.stat("deleteme.txt", &info);
    CHECK(err != 0, "file gone after remove");

    lfs.unmount();
}

static void test_file_seek() {
    SECTION("File Seek");

    Lfs lfs;
    auto cfg = make_lfs_config(ram_dev, BLOCK_SIZE, 16, read_buf, prog_buf, lookahead_buf);

    lfs.format(&cfg);
    lfs.mount(&cfg);

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;
    lfs.file_opencfg(&file, "seek.bin",
                     static_cast<int>(LfsOpenFlags::RDWR | LfsOpenFlags::CREAT), &fcfg);

    const char* data = "ABCDEFGHIJ";
    lfs.file_write(&file, data, 10);

    // Seek to position 5
    lfs_soff_t pos = lfs.file_seek(&file, 5, static_cast<int>(LfsWhence::SET));
    CHECK(pos == 5, "seek to 5");

    char buf[5]{};
    lfs_ssize_t nread = lfs.file_read(&file, buf, 5);
    CHECK(nread == 5, "read 5 bytes from pos 5");
    CHECK(std::memcmp(buf, "FGHIJ", 5) == 0, "content from pos 5");

    lfs.file_close(&file);
    lfs.unmount();
}

int main() {
    test_format_and_mount();
    test_file_write_read();
    test_mkdir_and_stat();
    test_dir_read();
    test_remove();
    test_file_seek();

    TEST_SUMMARY();
}
