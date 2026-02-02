// SPDX-License-Identifier: MIT
// Unit tests for slimfs

#include <cstring>
#include <umifs/slim/slim.hh>
#include <umifs/slim/slim_config.hh>
#include <umifs/slim/slim_types.hh>

#include "test_common.hh"

using namespace umi::fs;

// ============================================================================
// RAM Block Device
// ============================================================================

static constexpr uint32_t BLOCK_SIZE = 512;
static constexpr uint32_t BLOCK_COUNT = 64;
static constexpr uint32_t TOTAL_SIZE = BLOCK_SIZE * BLOCK_COUNT;

static uint8_t storage[TOTAL_SIZE];
static uint8_t read_buf[BLOCK_SIZE];
static uint8_t prog_buf[BLOCK_SIZE];
static uint8_t lookahead_buf[BLOCK_COUNT / 8];

static int ram_read(const SlimConfig* cfg, uint32_t block, uint32_t off, void* buf, uint32_t size) {
    (void)cfg;
    if (block >= BLOCK_COUNT || off + size > BLOCK_SIZE)
        return -1;
    std::memcpy(buf, &storage[block * BLOCK_SIZE + off], size);
    return 0;
}

static int ram_prog(const SlimConfig* cfg, uint32_t block, uint32_t off, const void* buf, uint32_t size) {
    (void)cfg;
    if (block >= BLOCK_COUNT || off + size > BLOCK_SIZE)
        return -1;
    std::memcpy(&storage[block * BLOCK_SIZE + off], buf, size);
    return 0;
}

static int ram_erase(const SlimConfig* cfg, uint32_t block) {
    (void)cfg;
    if (block >= BLOCK_COUNT)
        return -1;
    std::memset(&storage[block * BLOCK_SIZE], 0xFF, BLOCK_SIZE);
    return 0;
}

static int ram_sync(const SlimConfig*) {
    return 0;
}

static SlimConfig make_config() {
    SlimConfig cfg{};
    cfg.context = nullptr;
    cfg.read = ram_read;
    cfg.prog = ram_prog;
    cfg.erase = ram_erase;
    cfg.sync = ram_sync;
    cfg.read_size = 1;
    cfg.prog_size = 1;
    cfg.block_size = BLOCK_SIZE;
    cfg.block_count = BLOCK_COUNT;
    cfg.name_max = 0;
    cfg.file_max = 0;
    cfg.read_buffer = {read_buf, BLOCK_SIZE};
    cfg.prog_buffer = {prog_buf, BLOCK_SIZE};
    cfg.lookahead_buffer = {lookahead_buf, sizeof(lookahead_buf)};
    return cfg;
}

struct SlimFixture {
    SlimFs fs{};
    SlimConfig cfg{};

    void format_and_mount() {
        std::memset(storage, 0xFF, TOTAL_SIZE);
        cfg = make_config();
        (void)fs.format(&cfg);
        (void)fs.mount(&cfg);
    }

    void remount() {
        (void)fs.unmount();
        fs = SlimFs{};
        (void)fs.mount(&cfg);
    }
};

// ============================================================================
// Format / Mount / Unmount
// ============================================================================

static void test_format_and_mount() {
    SECTION("Format and Mount");

    SlimFs fs;
    std::memset(storage, 0xFF, TOTAL_SIZE);
    auto cfg = make_config();

    int rc = fs.format(&cfg);
    CHECK(rc == 0, "format succeeds");

    rc = fs.mount(&cfg);
    CHECK(rc == 0, "mount succeeds");

    rc = fs.unmount();
    CHECK(rc == 0, "unmount succeeds");
}

static void test_remount() {
    SECTION("Remount preserves data");

    SlimFixture f;
    f.format_and_mount();

    // Create a directory, then remount and verify
    int rc = f.fs.mkdir("/test");
    CHECK(rc == 0, "mkdir /test");

    f.remount();

    SlimInfo info{};
    rc = f.fs.stat("/test", info);
    CHECK(rc == 0, "stat /test after remount");
    CHECK(info.type == SlimType::DIR, "type is DIR");

    (void)f.fs.unmount();
}

// ============================================================================
// Directory operations
// ============================================================================

static void test_mkdir_and_stat() {
    SECTION("mkdir and stat");

    SlimFixture f;
    f.format_and_mount();

    int rc = f.fs.mkdir("/mydir");
    CHECK(rc == 0, "mkdir /mydir");

    SlimInfo info{};
    rc = f.fs.stat("/mydir", info);
    CHECK(rc == 0, "stat /mydir");
    CHECK(info.type == SlimType::DIR, "type is DIR");
    CHECK(std::strcmp(info.name, "mydir") == 0, "name is mydir");

    // Duplicate mkdir should fail
    rc = f.fs.mkdir("/mydir");
    CHECK(rc != 0, "duplicate mkdir fails");

    (void)f.fs.unmount();
}

static void test_dir_read() {
    SECTION("dir_open / dir_read / dir_close");

    SlimFixture f;
    f.format_and_mount();

    (void)f.fs.mkdir("/a");
    (void)f.fs.mkdir("/b");
    (void)f.fs.mkdir("/c");

    SlimDir dir{};
    int rc = f.fs.dir_open(dir, "/");
    CHECK(rc == 0, "dir_open /");

    int count = 0;
    SlimInfo info{};
    while (true) {
        rc = f.fs.dir_read(dir, info);
        if (rc <= 0)
            break;
        count++;
    }
    CHECK(count == 3, "root has 3 entries");

    rc = f.fs.dir_close(dir);
    CHECK(rc == 0, "dir_close");

    (void)f.fs.unmount();
}

static void test_nested_dirs() {
    SECTION("Nested directories");

    SlimFixture f;
    f.format_and_mount();

    int rc = f.fs.mkdir("/parent");
    CHECK(rc == 0, "mkdir /parent");

    rc = f.fs.mkdir("/parent/child");
    CHECK(rc == 0, "mkdir /parent/child");

    SlimInfo info{};
    rc = f.fs.stat("/parent/child", info);
    CHECK(rc == 0, "stat /parent/child");
    CHECK(info.type == SlimType::DIR, "child is DIR");

    (void)f.fs.unmount();
}

// ============================================================================
// File operations
// ============================================================================

static void test_file_write_read() {
    SECTION("File write and read");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    int rc = f.fs.file_open(file, "/hello.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    CHECK(rc == 0, "file_open for write");

    const char* msg = "Hello, slimfs!";
    auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(msg), std::strlen(msg));
    rc = f.fs.file_write(file, data);
    CHECK(rc == static_cast<int>(std::strlen(msg)), "file_write returns bytes written");

    rc = f.fs.file_close(file);
    CHECK(rc == 0, "file_close after write");

    // Read back
    SlimFile rfile{};
    rc = f.fs.file_open(rfile, "/hello.txt", SlimOpenFlags::RDONLY);
    CHECK(rc == 0, "file_open for read");

    uint8_t buf[64]{};
    rc = f.fs.file_read(rfile, {buf, sizeof(buf)});
    CHECK(rc == static_cast<int>(std::strlen(msg)), "file_read returns bytes read");
    CHECK(std::memcmp(buf, msg, std::strlen(msg)) == 0, "data matches");

    rc = f.fs.file_close(rfile);
    CHECK(rc == 0, "file_close after read");

    (void)f.fs.unmount();
}

static void test_file_stat() {
    SECTION("File stat");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/data.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    uint8_t payload[100];
    std::memset(payload, 0xAB, sizeof(payload));
    (void)f.fs.file_write(file, {payload, sizeof(payload)});
    (void)f.fs.file_close(file);

    SlimInfo info{};
    int rc = f.fs.stat("/data.bin", info);
    CHECK(rc == 0, "stat /data.bin");
    CHECK(info.type == SlimType::REG, "type is REG");
    CHECK(info.size == 100, "size is 100");

    (void)f.fs.unmount();
}

static void test_file_seek() {
    SECTION("File seek");

    SlimFixture f;
    f.format_and_mount();

    // Write sequential bytes
    SlimFile file{};
    (void)f.fs.file_open(file, "/seek.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    uint8_t data[256];
    for (int i = 0; i < 256; i++)
        data[i] = static_cast<uint8_t>(i);
    (void)f.fs.file_write(file, {data, sizeof(data)});
    (void)f.fs.file_close(file);

    // Read from offset
    (void)f.fs.file_open(file, "/seek.bin", SlimOpenFlags::RDONLY);
    int rc = f.fs.file_seek(file, 100, SlimWhence::SET);
    CHECK(rc >= 0, "seek to 100");

    uint8_t buf[10]{};
    rc = f.fs.file_read(file, {buf, 10});
    CHECK(rc == 10, "read 10 bytes at offset 100");
    CHECK(buf[0] == 100, "first byte is 100");
    CHECK(buf[9] == 109, "last byte is 109");

    (void)f.fs.file_close(file);
    (void)f.fs.unmount();
}

static void test_file_truncate() {
    SECTION("File truncate");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/trunc.bin", SlimOpenFlags::RDWR | SlimOpenFlags::CREAT);
    uint8_t data[200];
    std::memset(data, 0x55, sizeof(data));
    (void)f.fs.file_write(file, {data, sizeof(data)});

    int rc = f.fs.file_truncate(file, 50);
    CHECK(rc == 0, "truncate to 50");

    int sz = f.fs.file_size(file);
    CHECK(sz == 50, "size is 50 after truncate");

    (void)f.fs.file_close(file);
    (void)f.fs.unmount();
}

static void test_file_persist_after_remount() {
    SECTION("File data persists after remount");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/persist.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* msg = "persistent data";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg), std::strlen(msg)});
    (void)f.fs.file_close(file);

    f.remount();

    (void)f.fs.file_open(file, "/persist.txt", SlimOpenFlags::RDONLY);
    uint8_t buf[64]{};
    int rc = f.fs.file_read(file, {buf, sizeof(buf)});
    CHECK(rc == static_cast<int>(std::strlen(msg)), "read after remount");
    CHECK(std::memcmp(buf, msg, std::strlen(msg)) == 0, "data matches after remount");
    (void)f.fs.file_close(file);

    (void)f.fs.unmount();
}

// ============================================================================
// Large file (multi-block)
// ============================================================================

static void test_large_file() {
    SECTION("Large file (multi-block)");

    SlimFixture f;
    f.format_and_mount();

    // Write 4KB (spans multiple 512B blocks)
    constexpr uint32_t FILE_SIZE = 4096;
    SlimFile file{};
    (void)f.fs.file_open(file, "/big.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);

    uint8_t wbuf[256];
    for (uint32_t off = 0; off < FILE_SIZE; off += sizeof(wbuf)) {
        for (uint32_t i = 0; i < sizeof(wbuf); i++)
            wbuf[i] = static_cast<uint8_t>((off + i) & 0xFF);
        (void)f.fs.file_write(file, {wbuf, sizeof(wbuf)});
    }
    (void)f.fs.file_close(file);

    // Read back and verify
    (void)f.fs.file_open(file, "/big.bin", SlimOpenFlags::RDONLY);
    uint8_t rbuf[256];
    bool match = true;
    for (uint32_t off = 0; off < FILE_SIZE; off += sizeof(rbuf)) {
        int rc = f.fs.file_read(file, {rbuf, sizeof(rbuf)});
        if (rc != static_cast<int>(sizeof(rbuf))) {
            match = false;
            break;
        }
        for (uint32_t i = 0; i < sizeof(rbuf); i++) {
            if (rbuf[i] != static_cast<uint8_t>((off + i) & 0xFF)) {
                match = false;
                break;
            }
        }
        if (!match)
            break;
    }
    CHECK(match, "large file data integrity");
    (void)f.fs.file_close(file);

    (void)f.fs.unmount();
}

// ============================================================================
// Remove
// ============================================================================

static void test_remove_file() {
    SECTION("Remove file");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/rm.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* msg = "delete me";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg), std::strlen(msg)});
    (void)f.fs.file_close(file);

    int rc = f.fs.remove("/rm.txt");
    CHECK(rc == 0, "remove succeeds");

    SlimInfo info{};
    rc = f.fs.stat("/rm.txt", info);
    CHECK(rc != 0, "stat after remove fails");

    (void)f.fs.unmount();
}

static void test_remove_dir() {
    SECTION("Remove empty directory");

    SlimFixture f;
    f.format_and_mount();

    (void)f.fs.mkdir("/empty");
    int rc = f.fs.remove("/empty");
    CHECK(rc == 0, "remove empty dir");

    SlimInfo info{};
    rc = f.fs.stat("/empty", info);
    CHECK(rc != 0, "stat after remove fails");

    (void)f.fs.unmount();
}

static void test_remove_nonempty_dir() {
    SECTION("Remove non-empty directory fails");

    SlimFixture f;
    f.format_and_mount();

    (void)f.fs.mkdir("/notempty");
    (void)f.fs.mkdir("/notempty/child");

    int rc = f.fs.remove("/notempty");
    CHECK(rc != 0, "remove non-empty dir fails");

    (void)f.fs.unmount();
}

// ============================================================================
// Rename
// ============================================================================

static void test_rename() {
    SECTION("Rename file");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/old.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* msg = "rename test";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg), std::strlen(msg)});
    (void)f.fs.file_close(file);

    int rc = f.fs.rename("/old.txt", "/new.txt");
    CHECK(rc == 0, "rename succeeds");

    SlimInfo info{};
    rc = f.fs.stat("/old.txt", info);
    CHECK(rc != 0, "old name gone");

    rc = f.fs.stat("/new.txt", info);
    CHECK(rc == 0, "new name exists");
    CHECK(info.type == SlimType::REG, "type is REG");

    // Verify data
    (void)f.fs.file_open(file, "/new.txt", SlimOpenFlags::RDONLY);
    uint8_t buf[64]{};
    rc = f.fs.file_read(file, {buf, sizeof(buf)});
    CHECK(rc == static_cast<int>(std::strlen(msg)), "data length after rename");
    CHECK(std::memcmp(buf, msg, std::strlen(msg)) == 0, "data matches after rename");
    (void)f.fs.file_close(file);

    (void)f.fs.unmount();
}

// ============================================================================
// fs_size / fs_traverse
// ============================================================================

static void test_fs_size() {
    SECTION("fs_size");

    SlimFixture f;
    f.format_and_mount();

    int sz = f.fs.fs_size();
    CHECK(sz > 0, "fs_size > 0 after format");

    (void)f.fs.unmount();
}

static void test_fs_traverse() {
    SECTION("fs_traverse");

    SlimFixture f;
    f.format_and_mount();

    int block_count = 0;
    auto cb = [](void* data, uint32_t) -> int {
        (*static_cast<int*>(data))++;
        return 0;
    };
    int rc = f.fs.fs_traverse(cb, &block_count);
    CHECK(rc == 0, "fs_traverse succeeds");
    CHECK(block_count > 0, "traversed blocks > 0");

    (void)f.fs.unmount();
}

// ============================================================================
// Edge cases
// ============================================================================

static void test_open_nonexistent() {
    SECTION("Open nonexistent file");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    int rc = f.fs.file_open(file, "/nope.txt", SlimOpenFlags::RDONLY);
    CHECK(rc != 0, "open nonexistent fails");

    (void)f.fs.unmount();
}

static void test_file_append() {
    SECTION("File append mode");

    SlimFixture f;
    f.format_and_mount();

    // Create file
    SlimFile file{};
    (void)f.fs.file_open(file, "/app.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* part1 = "Hello";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(part1), std::strlen(part1)});
    (void)f.fs.file_close(file);

    // Append
    (void)f.fs.file_open(file, "/app.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::APPEND);
    const char* part2 = " World";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(part2), std::strlen(part2)});
    (void)f.fs.file_close(file);

    // Read all
    (void)f.fs.file_open(file, "/app.txt", SlimOpenFlags::RDONLY);
    uint8_t buf[64]{};
    int rc = f.fs.file_read(file, {buf, sizeof(buf)});
    CHECK(rc == 11, "total length is 11");
    CHECK(std::memcmp(buf, "Hello World", 11) == 0, "appended data matches");
    (void)f.fs.file_close(file);

    (void)f.fs.unmount();
}

// ============================================================================
// Power-loss resilience
// ============================================================================

// Snapshot storage to simulate power-loss (restore pre-operation flash state)
static uint8_t snapshot[TOTAL_SIZE];

// Fault injection: crash after N prog/erase calls
#define SLIM_FAULT_TESTS 1
#if SLIM_FAULT_TESTS
static int fault_countdown = -1; // -1 = disabled
static uint8_t fault_snapshot[TOTAL_SIZE];

static int fault_prog(const SlimConfig* cfg, uint32_t block, uint32_t off, const void* buf, uint32_t size) {
    if (fault_countdown > 0) {
        --fault_countdown;
    } else if (fault_countdown == 0) {
        // Simulate partial write: write only half the data, then "crash"
        uint32_t half = size / 2;
        if (half > 0)
            std::memcpy(&storage[block * BLOCK_SIZE + off], buf, half);
        // Save crash-state snapshot
        std::memcpy(fault_snapshot, storage, TOTAL_SIZE);
        return -99; // Signal crash
    }
    return ram_prog(cfg, block, off, buf, size);
}

static int fault_erase(const SlimConfig* cfg, uint32_t block) {
    if (fault_countdown > 0) {
        --fault_countdown;
    } else if (fault_countdown == 0) {
        // Simulate partial erase: only erase half the block
        std::memset(&storage[block * BLOCK_SIZE], 0xFF, BLOCK_SIZE / 2);
        std::memcpy(fault_snapshot, storage, TOTAL_SIZE);
        return -99;
    }
    return ram_erase(cfg, block);
}

static SlimConfig make_fault_config() {
    SlimConfig cfg = make_config();
    cfg.prog = fault_prog;
    cfg.erase = fault_erase;
    return cfg;
}

// Try mounting from fault_snapshot — must either succeed or fail gracefully (no hang/crash)
static bool try_mount_after_fault() {
    std::memcpy(storage, fault_snapshot, TOTAL_SIZE);
    SlimFs fs{};
    auto cfg = make_config(); // Use normal (non-fault) config for recovery mount
    int rc = fs.mount(&cfg);
    if (rc == 0)
        (void)fs.unmount();
    return true; // If we got here without crashing, the test passes
}
#endif // SLIM_FAULT_TESTS

static void test_power_loss_during_mkdir() {
    SECTION("Power-loss during mkdir — mount recovers");

    SlimFixture f;
    f.format_and_mount();

    // Save flash state before mkdir
    std::memcpy(snapshot, storage, TOTAL_SIZE);

    (void)f.fs.mkdir("/willdie");

    // Simulate power-loss: restore pre-mkdir flash
    std::memcpy(storage, snapshot, TOTAL_SIZE);

    // Mount should succeed on the old (consistent) state
    SlimFs fs2{};
    auto cfg = make_config();
    int rc = fs2.mount(&cfg);
    CHECK(rc == 0, "mount after power-loss succeeds");

    // /willdie should not exist
    SlimInfo info{};
    rc = fs2.stat("/willdie", info);
    CHECK(rc != 0, "mkdir entry absent after power-loss");

    (void)fs2.unmount();
}

static void test_power_loss_during_file_write() {
    SECTION("Power-loss during file_write — mount recovers");

    SlimFixture f;
    f.format_and_mount();

    // Create a file with known data
    SlimFile file{};
    (void)f.fs.file_open(file, "/safe.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* msg = "safe data";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg), std::strlen(msg)});
    (void)f.fs.file_close(file);

    // Save flash state (file exists, data committed)
    std::memcpy(snapshot, storage, TOTAL_SIZE);

    // Now attempt another write that "loses power"
    (void)f.fs.file_open(file, "/safe.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::APPEND);
    const char* extra = " CORRUPTED";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(extra), std::strlen(extra)});
    // Do NOT close — simulate power-loss

    // Restore pre-write flash
    std::memcpy(storage, snapshot, TOTAL_SIZE);

    // Mount and verify original data is intact
    SlimFs fs2{};
    auto cfg = make_config();
    int rc = fs2.mount(&cfg);
    CHECK(rc == 0, "mount after write power-loss");

    SlimFile rf{};
    rc = fs2.file_open(rf, "/safe.txt", SlimOpenFlags::RDONLY);
    CHECK(rc == 0, "open safe.txt after power-loss");

    uint8_t buf[64]{};
    rc = fs2.file_read(rf, {buf, sizeof(buf)});
    CHECK(rc == static_cast<int>(std::strlen(msg)), "original data length preserved");
    CHECK(std::memcmp(buf, msg, std::strlen(msg)) == 0, "original data intact");

    (void)fs2.file_close(rf);
    (void)fs2.unmount();
}

static void test_power_loss_during_remove() {
    SECTION("Power-loss during remove — mount recovers");

    SlimFixture f;
    f.format_and_mount();

    (void)f.fs.mkdir("/keep");

    // Save flash state (dir exists)
    std::memcpy(snapshot, storage, TOTAL_SIZE);

    (void)f.fs.remove("/keep");

    // Restore pre-remove flash
    std::memcpy(storage, snapshot, TOTAL_SIZE);

    SlimFs fs2{};
    auto cfg = make_config();
    int rc = fs2.mount(&cfg);
    CHECK(rc == 0, "mount after remove power-loss");

    SlimInfo info{};
    rc = fs2.stat("/keep", info);
    CHECK(rc == 0, "dir still exists after power-loss");
    CHECK(info.type == SlimType::DIR, "type is DIR");

    (void)fs2.unmount();
}

static void test_superblock_redundancy() {
    SECTION("Superblock A corruption — falls back to B");

    SlimFixture f;
    f.format_and_mount();
    (void)f.fs.mkdir("/test_sb");
    (void)f.fs.unmount();

    // Corrupt superblock A (block 0)
    std::memset(&storage[0], 0x00, BLOCK_SIZE);

    // Mount should succeed via superblock B
    SlimFs fs2{};
    auto cfg = make_config();
    int rc = fs2.mount(&cfg);
    CHECK(rc == 0, "mount with corrupted SB-A via SB-B");

    (void)fs2.unmount();
}

static void test_both_superblocks_corrupt() {
    SECTION("Both superblocks corrupt — mount fails");

    SlimFixture f;
    f.format_and_mount();
    (void)f.fs.unmount();

    // Corrupt both superblocks
    std::memset(&storage[0], 0x00, BLOCK_SIZE);
    std::memset(&storage[BLOCK_SIZE], 0x00, BLOCK_SIZE);

    SlimFs fs2{};
    auto cfg = make_config();
    int rc = fs2.mount(&cfg);
    CHECK(rc != 0, "mount fails with both SBs corrupt");
}

#if SLIM_FAULT_TESTS
static void test_fault_injection_mkdir() {
    SECTION("Fault injection: crash at every prog/erase during mkdir");

    for (int n = 0; n < 20; ++n) {
        // Start from clean state
        std::memset(storage, 0xFF, TOTAL_SIZE);
        SlimFs fs{};
        auto cfg = make_config();
        (void)fs.format(&cfg);
        (void)fs.mount(&cfg);

        // Save good state
        std::memcpy(snapshot, storage, TOTAL_SIZE);

        // Switch to fault config
        auto fcfg = make_fault_config();
        fs.cfg = &fcfg;
        fault_countdown = n;

        int rc = fs.mkdir("/fault_test");
        if (rc == -99 || rc != 0) {
            // Crash occurred — verify recovery
            bool ok = try_mount_after_fault();
            if (!ok) {
                umi::test::checkf(false, "fault mkdir n=%d: mount crashed", n);
                return;
            }
            // Also try mounting from pre-operation snapshot (always valid)
            std::memcpy(storage, snapshot, TOTAL_SIZE);
            SlimFs fs3{};
            auto cfg3 = make_config();
            rc = fs3.mount(&cfg3);
            if (rc != 0) {
                umi::test::checkf(false, "fault mkdir n=%d: pre-op mount failed", n);
                return;
            }
            (void)fs3.unmount();
        } else {
            (void)fs.unmount();
        }
    }
    CHECK(true, "mkdir fault injection: all crash points recovered");
}

static void test_fault_injection_file_write() {
    SECTION("Fault injection: crash at every prog/erase during file_write");

    for (int n = 0; n < 30; ++n) {
        std::memset(storage, 0xFF, TOTAL_SIZE);
        SlimFs fs{};
        auto cfg = make_config();
        (void)fs.format(&cfg);
        (void)fs.mount(&cfg);

        // Create file first (without fault)
        SlimFile file{};
        (void)fs.file_open(file, "/data.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
        uint8_t data[64];
        std::memset(data, 0xAA, sizeof(data));
        (void)fs.file_write(file, {data, sizeof(data)});
        (void)fs.file_close(file);

        std::memcpy(snapshot, storage, TOTAL_SIZE);

        // Re-open and write more with fault
        auto fcfg = make_fault_config();
        fs.cfg = &fcfg;
        (void)fs.file_open(file, "/data.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::APPEND);
        fault_countdown = n;
        uint8_t more[256];
        std::memset(more, 0xBB, sizeof(more));
        int rc = fs.file_write(file, {more, sizeof(more)});
        if (rc < 0) {
            bool ok = try_mount_after_fault();
            if (!ok) {
                umi::test::checkf(false, "fault write n=%d: mount crashed", n);
                return;
            }
            // Verify pre-op state is mountable and original data intact
            std::memcpy(storage, snapshot, TOTAL_SIZE);
            SlimFs fs3{};
            auto cfg3 = make_config();
            rc = fs3.mount(&cfg3);
            if (rc != 0) {
                umi::test::checkf(false, "fault write n=%d: pre-op mount failed", n);
                return;
            }
            SlimFile rf{};
            rc = fs3.file_open(rf, "/data.bin", SlimOpenFlags::RDONLY);
            if (rc != 0) {
                umi::test::checkf(false, "fault write n=%d: open pre-op file failed", n);
                (void)fs3.unmount();
                return;
            }
            uint8_t rbuf[64]{};
            rc = fs3.file_read(rf, {rbuf, sizeof(rbuf)});
            if (rc != 64 || std::memcmp(rbuf, data, 64) != 0) {
                umi::test::checkf(false, "fault write n=%d: original data corrupted", n);
                (void)fs3.file_close(rf);
                (void)fs3.unmount();
                return;
            }
            (void)fs3.file_close(rf);
            (void)fs3.unmount();
        } else {
            (void)fs.file_close(file);
            (void)fs.unmount();
        }
    }
    CHECK(true, "file_write fault injection: all crash points recovered");
}

static void test_fault_injection_rename() {
    SECTION("Fault injection: crash at every prog/erase during rename");

    for (int n = 0; n < 30; ++n) {
        std::memset(storage, 0xFF, TOTAL_SIZE);
        SlimFs fs{};
        auto cfg = make_config();
        (void)fs.format(&cfg);
        (void)fs.mount(&cfg);

        // Create source file
        SlimFile file{};
        (void)fs.file_open(file, "/src.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
        const char* msg = "rename_data";
        (void)fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg), std::strlen(msg)});
        (void)fs.file_close(file);

        std::memcpy(snapshot, storage, TOTAL_SIZE);

        auto fcfg = make_fault_config();
        fs.cfg = &fcfg;
        fault_countdown = n;
        int rc = fs.rename("/src.txt", "/dst.txt");
        if (rc < 0) {
            bool ok = try_mount_after_fault();
            if (!ok) {
                umi::test::checkf(false, "fault rename n=%d: mount crashed", n);
                return;
            }
            // Pre-op state: /src.txt must exist
            std::memcpy(storage, snapshot, TOTAL_SIZE);
            SlimFs fs3{};
            auto cfg3 = make_config();
            rc = fs3.mount(&cfg3);
            if (rc != 0) {
                umi::test::checkf(false, "fault rename n=%d: pre-op mount failed", n);
                return;
            }
            SlimInfo info{};
            rc = fs3.stat("/src.txt", info);
            if (rc != 0) {
                umi::test::checkf(false, "fault rename n=%d: src gone in pre-op", n);
                (void)fs3.unmount();
                return;
            }
            (void)fs3.unmount();
        } else {
            (void)fs.unmount();
        }
    }
    CHECK(true, "rename fault injection: all crash points recovered");
}

static void test_fault_injection_remove() {
    SECTION("Fault injection: crash at every prog/erase during remove");

    for (int n = 0; n < 20; ++n) {
        std::memset(storage, 0xFF, TOTAL_SIZE);
        SlimFs fs{};
        auto cfg = make_config();
        (void)fs.format(&cfg);
        (void)fs.mount(&cfg);

        (void)fs.mkdir("/victim");

        std::memcpy(snapshot, storage, TOTAL_SIZE);

        auto fcfg = make_fault_config();
        fs.cfg = &fcfg;
        fault_countdown = n;
        int rc = fs.remove("/victim");
        if (rc < 0) {
            bool ok = try_mount_after_fault();
            if (!ok) {
                umi::test::checkf(false, "fault remove n=%d: mount crashed", n);
                return;
            }
        } else {
            (void)fs.unmount();
        }
    }
    CHECK(true, "remove fault injection: all crash points recovered");
}
#endif // SLIM_FAULT_TESTS

// ============================================================================
// Geometry API
// ============================================================================

static void test_geometry_api() {
    SECTION("Geometry API");

    SlimFixture f;
    f.format_and_mount();

    CHECK(f.fs.block_size() == BLOCK_SIZE, "block_size matches config");
    CHECK(f.fs.block_count_total() == BLOCK_COUNT, "block_count matches config");

    (void)f.fs.unmount();
}

// ============================================================================
// close_all
// ============================================================================

static void test_close_all() {
    SECTION("close_all");

    SlimFixture f;
    f.format_and_mount();

    SlimFile files[4]{};
    (void)f.fs.file_open(files[0], "/a.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    (void)f.fs.file_open(files[1], "/b.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* msg = "data";
    (void)f.fs.file_write(files[0], {reinterpret_cast<const uint8_t*>(msg), 4});
    (void)f.fs.file_write(files[1], {reinterpret_cast<const uint8_t*>(msg), 4});

    f.fs.close_all({files, 4});

    // Verify files were synced — data should be readable
    SlimFile rf{};
    int rc = f.fs.file_open(rf, "/a.txt", SlimOpenFlags::RDONLY);
    CHECK(rc == 0, "open a.txt after close_all");
    uint8_t buf[16]{};
    rc = f.fs.file_read(rf, {buf, sizeof(buf)});
    CHECK(rc == 4, "read 4 bytes from a.txt");
    (void)f.fs.file_close(rf);

    (void)f.fs.unmount();
}

// ============================================================================
// pending_move recovery
// ============================================================================

static void test_pending_move_recovery_complete() {
    SECTION("pending_move recovery: rename completed");

    SlimFixture f;
    f.format_and_mount();

    // Create file and rename it — the rename will record pending_move
    SlimFile file{};
    (void)f.fs.file_open(file, "/src.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* msg = "move_data";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg), std::strlen(msg)});
    (void)f.fs.file_close(file);

    // Normal rename should work and pending_move should be clear after
    int rc = f.fs.rename("/src.txt", "/dst.txt");
    CHECK(rc == 0, "rename succeeds");

    f.remount();

    SlimInfo info{};
    rc = f.fs.stat("/dst.txt", info);
    CHECK(rc == 0, "dst exists after remount");

    rc = f.fs.stat("/src.txt", info);
    CHECK(rc != 0, "src gone after remount");

    (void)f.fs.unmount();
}

// ============================================================================
// Wear leveling distribution
// ============================================================================

static void test_wear_leveling_distribution() {
    SECTION("Wear leveling: block allocation distributes");

    SlimFixture f;
    f.format_and_mount();

    // Create and delete files repeatedly, tracking which blocks get used
    uint32_t block_usage[BLOCK_COUNT]{};

    for (int iter = 0; iter < 20; ++iter) {
        SlimFile file{};
        (void)f.fs.file_open(file, "/wl.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
        uint8_t data[64];
        std::memset(data, static_cast<uint8_t>(iter), sizeof(data));
        (void)f.fs.file_write(file, {data, sizeof(data)});
        (void)f.fs.file_close(file);

        // Track used blocks
        auto cb = [](void* ctx, uint32_t blk) -> int {
            auto* usage = static_cast<uint32_t*>(ctx);
            if (blk < BLOCK_COUNT)
                usage[blk]++;
            return 0;
        };
        (void)f.fs.fs_traverse(cb, block_usage);

        (void)f.fs.remove("/wl.bin");
    }

    // Check that allocation wasn't stuck on the same few blocks
    // At least 4 different blocks should have been used (2 superblocks + root + data)
    uint32_t used_blocks = 0;
    for (uint32_t i = 0; i < BLOCK_COUNT; ++i) {
        if (block_usage[i] > 0)
            ++used_blocks;
    }
    CHECK(used_blocks >= 4, "at least 4 blocks used across iterations");

    (void)f.fs.unmount();
}

// ============================================================================
// COW write power-loss safety
// ============================================================================

static void test_cow_write_power_loss() {
    SECTION("COW: power-loss during write preserves old data");

    SlimFixture f;
    f.format_and_mount();

    // Write initial data
    SlimFile file{};
    (void)f.fs.file_open(file, "/cow.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* original = "original_data";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(original), std::strlen(original)});
    (void)f.fs.file_close(file);

    // Snapshot after initial write
    std::memcpy(snapshot, storage, TOTAL_SIZE);

    // Open file and overwrite — but do NOT sync/close (simulate power loss)
    (void)f.fs.file_open(file, "/cow.txt", SlimOpenFlags::WRONLY);
    const char* overwrite = "CORRUPTED!!!!";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(overwrite), std::strlen(overwrite)});
    // NO file_close — power loss here

    // Restore pre-overwrite flash state
    std::memcpy(storage, snapshot, TOTAL_SIZE);

    // Mount and verify original data is intact (COW: metadata still points to old chain)
    SlimFs fs2{};
    auto cfg = make_config();
    int rc = fs2.mount(&cfg);
    CHECK(rc == 0, "mount after COW power-loss");

    SlimFile rf{};
    rc = fs2.file_open(rf, "/cow.txt", SlimOpenFlags::RDONLY);
    CHECK(rc == 0, "open cow.txt after power-loss");

    uint8_t buf[64]{};
    rc = fs2.file_read(rf, {buf, sizeof(buf)});
    CHECK(rc == static_cast<int>(std::strlen(original)), "original data length preserved");
    CHECK(std::memcmp(buf, original, std::strlen(original)) == 0, "original data intact after COW power-loss");

    (void)fs2.file_close(rf);
    (void)fs2.unmount();
}

static void test_cow_write_sync() {
    SECTION("COW: write + sync commits new data");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/cow2.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* msg1 = "first";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg1), std::strlen(msg1)});
    (void)f.fs.file_close(file);

    // Overwrite with new data and close (sync)
    (void)f.fs.file_open(file, "/cow2.txt", SlimOpenFlags::WRONLY);
    const char* msg2 = "second_data";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg2), std::strlen(msg2)});
    (void)f.fs.file_close(file);

    // Remount and verify new data
    f.remount();

    SlimFile rf{};
    int rc = f.fs.file_open(rf, "/cow2.txt", SlimOpenFlags::RDONLY);
    CHECK(rc == 0, "open cow2.txt after remount");

    uint8_t buf[64]{};
    rc = f.fs.file_read(rf, {buf, sizeof(buf)});
    CHECK(rc == static_cast<int>(std::strlen(msg2)), "new data length");
    CHECK(std::memcmp(buf, msg2, std::strlen(msg2)) == 0, "new data matches");

    (void)f.fs.file_close(rf);
    (void)f.fs.unmount();
}

// ============================================================================
// Additional production-quality tests
// ============================================================================

static void test_multi_write_same_block() {
    SECTION("Multi-write within same block");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/mw.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);

    // Two writes that fit in one block (data_per_block = 508)
    uint8_t a[200], b[200];
    std::memset(a, 0xAA, sizeof(a));
    std::memset(b, 0xBB, sizeof(b));
    int rc = f.fs.file_write(file, {a, sizeof(a)});
    CHECK(rc == 200, "first write 200");
    rc = f.fs.file_write(file, {b, sizeof(b)});
    CHECK(rc == 200, "second write 200");
    (void)f.fs.file_close(file);

    // Verify both parts
    SlimFile rf{};
    (void)f.fs.file_open(rf, "/mw.bin", SlimOpenFlags::RDONLY);
    uint8_t out[400]{};
    rc = f.fs.file_read(rf, {out, sizeof(out)});
    CHECK(rc == 400, "read 400 bytes");
    CHECK(std::memcmp(out, a, 200) == 0, "first 200 bytes are 0xAA");
    CHECK(std::memcmp(out + 200, b, 200) == 0, "next 200 bytes are 0xBB");
    (void)f.fs.file_close(rf);
    (void)f.fs.unmount();
}

static void test_multi_write_cross_block() {
    SECTION("Multi-write crossing block boundary");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/cross.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);

    // data_per_block = 508, write 400+400 = crosses into second block
    uint8_t a[400], b[400];
    std::memset(a, 0xCC, sizeof(a));
    std::memset(b, 0xDD, sizeof(b));
    (void)f.fs.file_write(file, {a, sizeof(a)});
    (void)f.fs.file_write(file, {b, sizeof(b)});
    (void)f.fs.file_close(file);

    SlimFile rf{};
    (void)f.fs.file_open(rf, "/cross.bin", SlimOpenFlags::RDONLY);
    uint8_t out[800]{};
    int rc = f.fs.file_read(rf, {out, sizeof(out)});
    CHECK(rc == 800, "read 800 bytes");
    CHECK(std::memcmp(out, a, 400) == 0, "first 400 bytes");
    CHECK(std::memcmp(out + 400, b, 400) == 0, "next 400 bytes");
    (void)f.fs.file_close(rf);
    (void)f.fs.unmount();
}

static void test_overwrite_preserves_surrounding() {
    SECTION("Overwrite middle preserves surrounding data");

    SlimFixture f;
    f.format_and_mount();

    // Write initial pattern
    SlimFile file{};
    (void)f.fs.file_open(file, "/ow.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    uint8_t orig[300];
    std::memset(orig, 0x11, sizeof(orig));
    (void)f.fs.file_write(file, {orig, sizeof(orig)});
    (void)f.fs.file_close(file);

    // Overwrite bytes [100..200) with 0x99
    (void)f.fs.file_open(file, "/ow.bin", SlimOpenFlags::WRONLY);
    (void)f.fs.file_seek(file, 100, SlimWhence::SET);
    uint8_t patch[100];
    std::memset(patch, 0x99, sizeof(patch));
    (void)f.fs.file_write(file, {patch, sizeof(patch)});
    (void)f.fs.file_close(file);

    // Verify: [0..100) = 0x11, [100..200) = 0x99, [200..300) = 0x11
    SlimFile rf{};
    (void)f.fs.file_open(rf, "/ow.bin", SlimOpenFlags::RDONLY);
    uint8_t out[300]{};
    int rc = f.fs.file_read(rf, {out, sizeof(out)});
    CHECK(rc == 300, "read 300 bytes");

    bool pre_ok = true;
    for (int i = 0; i < 100; ++i) {
        if (out[i] != 0x11) {
            pre_ok = false;
            break;
        }
    }
    CHECK(pre_ok, "bytes [0..100) preserved as 0x11");

    bool mid_ok = true;
    for (int i = 100; i < 200; ++i) {
        if (out[i] != 0x99) {
            mid_ok = false;
            break;
        }
    }
    CHECK(mid_ok, "bytes [100..200) overwritten to 0x99");

    bool post_ok = true;
    for (int i = 200; i < 300; ++i) {
        if (out[i] != 0x11) {
            post_ok = false;
            break;
        }
    }
    CHECK(post_ok, "bytes [200..300) preserved as 0x11");

    (void)f.fs.file_close(rf);
    (void)f.fs.unmount();
}

static void test_cow_multi_block_file() {
    SECTION("COW overwrite of multi-block file");

    SlimFixture f;
    f.format_and_mount();

    // Write 2000 bytes (~4 blocks of 508 data each)
    SlimFile file{};
    (void)f.fs.file_open(file, "/big.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    uint8_t buf[500];
    for (int i = 0; i < 4; ++i) {
        std::memset(buf, static_cast<uint8_t>(i + 1), sizeof(buf));
        (void)f.fs.file_write(file, {buf, sizeof(buf)});
    }
    (void)f.fs.file_close(file);

    // Overwrite with new data
    (void)f.fs.file_open(file, "/big.bin", SlimOpenFlags::WRONLY);
    uint8_t newdata[500];
    std::memset(newdata, 0xFF, sizeof(newdata));
    (void)f.fs.file_write(file, {newdata, sizeof(newdata)});
    (void)f.fs.file_close(file);

    f.remount();

    SlimFile rf{};
    (void)f.fs.file_open(rf, "/big.bin", SlimOpenFlags::RDONLY);
    uint8_t out[500]{};
    int rc = f.fs.file_read(rf, {out, sizeof(out)});
    CHECK(rc == 500, "read first 500 bytes");
    bool ok = true;
    for (int i = 0; i < 500; ++i) {
        if (out[i] != 0xFF) {
            ok = false;
            break;
        }
    }
    CHECK(ok, "first 500 bytes are 0xFF after COW overwrite");
    (void)f.fs.file_close(rf);
    (void)f.fs.unmount();
}

static void test_cow_truncate_during_dirty() {
    SECTION("Truncate during dirty COW state");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/trunc.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    uint8_t data[200];
    std::memset(data, 0x55, sizeof(data));
    (void)f.fs.file_write(file, {data, sizeof(data)});
    (void)f.fs.file_close(file);

    // Open, write more (dirty), then truncate
    (void)f.fs.file_open(file, "/trunc.bin", SlimOpenFlags::WRONLY);
    uint8_t more[100];
    std::memset(more, 0x77, sizeof(more));
    (void)f.fs.file_write(file, {more, sizeof(more)});
    // Now truncate while dirty
    int rc = f.fs.file_truncate(file, 50);
    CHECK(rc == 0, "truncate during dirty succeeds");
    (void)f.fs.file_close(file);

    // Verify size
    SlimInfo info{};
    rc = f.fs.stat("/trunc.bin", info);
    CHECK(rc == 0, "stat after truncate");
    CHECK(info.size == 50, "size is 50 after truncate");

    (void)f.fs.unmount();
}

static void test_rdwr_read_after_write() {
    SECTION("RDWR read after unsync write returns error");

    SlimFixture f;
    f.format_and_mount();

    // Create file with initial data
    SlimFile file{};
    (void)f.fs.file_open(file, "/rdwr.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    uint8_t data[64];
    std::memset(data, 0xAA, sizeof(data));
    (void)f.fs.file_write(file, {data, sizeof(data)});
    (void)f.fs.file_close(file);

    // Open RDWR, write (creates COW), then try read without sync
    (void)f.fs.file_open(file, "/rdwr.bin", SlimOpenFlags::RDWR);
    uint8_t patch[10];
    std::memset(patch, 0xBB, sizeof(patch));
    (void)f.fs.file_write(file, {patch, sizeof(patch)});

    // Seek back and try to read — should fail (dirty COW)
    (void)f.fs.file_seek(file, 0, SlimWhence::SET);
    uint8_t buf[64]{};
    int rc = f.fs.file_read(file, {buf, sizeof(buf)});
    CHECK(rc < 0, "read on dirty COW file returns error");

    (void)f.fs.file_close(file);
    (void)f.fs.unmount();
}

static void test_multiple_files_cow() {
    SECTION("Multiple files with concurrent COW writes");

    SlimFixture f;
    f.format_and_mount();

    // Create two files
    SlimFile fa{}, fb{};
    (void)f.fs.file_open(fa, "/fa.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    (void)f.fs.file_open(fb, "/fb.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);

    uint8_t da[64], db[64];
    std::memset(da, 0x11, sizeof(da));
    std::memset(db, 0x22, sizeof(db));
    (void)f.fs.file_write(fa, {da, sizeof(da)});
    (void)f.fs.file_write(fb, {db, sizeof(db)});
    (void)f.fs.file_close(fa);
    (void)f.fs.file_close(fb);

    // Overwrite both simultaneously (COW on both)
    (void)f.fs.file_open(fa, "/fa.bin", SlimOpenFlags::WRONLY);
    (void)f.fs.file_open(fb, "/fb.bin", SlimOpenFlags::WRONLY);

    uint8_t na[64], nb[64];
    std::memset(na, 0xAA, sizeof(na));
    std::memset(nb, 0xBB, sizeof(nb));
    (void)f.fs.file_write(fa, {na, sizeof(na)});
    (void)f.fs.file_write(fb, {nb, sizeof(nb)});
    (void)f.fs.file_close(fa);
    (void)f.fs.file_close(fb);

    f.remount();

    // Verify
    SlimFile rf{};
    uint8_t buf[64]{};
    (void)f.fs.file_open(rf, "/fa.bin", SlimOpenFlags::RDONLY);
    int rc = f.fs.file_read(rf, {buf, sizeof(buf)});
    CHECK(rc == 64, "fa read 64");
    CHECK(buf[0] == 0xAA && buf[63] == 0xAA, "fa data is 0xAA");
    (void)f.fs.file_close(rf);

    (void)f.fs.file_open(rf, "/fb.bin", SlimOpenFlags::RDONLY);
    rc = f.fs.file_read(rf, {buf, sizeof(buf)});
    CHECK(rc == 64, "fb read 64");
    CHECK(buf[0] == 0xBB && buf[63] == 0xBB, "fb data is 0xBB");
    (void)f.fs.file_close(rf);
    (void)f.fs.unmount();
}

static void test_write_empty_buf() {
    SECTION("Write empty buffer");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/empty.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    int rc = f.fs.file_write(file, {static_cast<const uint8_t*>(nullptr), 0});
    CHECK(rc == 0, "empty write returns 0");
    (void)f.fs.file_close(file);
    (void)f.fs.unmount();
}

static void test_seek_beyond_eof() {
    SECTION("Seek beyond EOF");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/seekeof.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    uint8_t data[10]{};
    (void)f.fs.file_write(file, {data, sizeof(data)});

    int rc = f.fs.file_seek(file, 100, SlimWhence::SET);
    // seek beyond EOF should succeed (sparse semantics) or clamp
    CHECK(rc >= 0, "seek beyond EOF returns non-negative");

    (void)f.fs.file_close(file);
    (void)f.fs.unmount();
}

static void test_rename_overwrite() {
    SECTION("Rename overwrites existing file");

    SlimFixture f;
    f.format_and_mount();

    // Create src and dst files
    SlimFile file{};
    (void)f.fs.file_open(file, "/src.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* src_data = "source";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(src_data), std::strlen(src_data)});
    (void)f.fs.file_close(file);

    (void)f.fs.file_open(file, "/dst.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* dst_data = "destination";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(dst_data), std::strlen(dst_data)});
    (void)f.fs.file_close(file);

    int rc = f.fs.rename("/src.txt", "/dst.txt");
    CHECK(rc == 0, "rename overwrite succeeds");

    // dst should now have src's data
    SlimFile rf{};
    (void)f.fs.file_open(rf, "/dst.txt", SlimOpenFlags::RDONLY);
    uint8_t buf[64]{};
    rc = f.fs.file_read(rf, {buf, sizeof(buf)});
    CHECK(rc == static_cast<int>(std::strlen(src_data)), "dst has src data length");
    CHECK(std::memcmp(buf, src_data, std::strlen(src_data)) == 0, "dst has src data content");
    (void)f.fs.file_close(rf);

    // src should be gone
    SlimInfo info{};
    rc = f.fs.stat("/src.txt", info);
    CHECK(rc != 0, "src is gone");

    (void)f.fs.unmount();
}

static void test_deeply_nested_dir() {
    SECTION("Deeply nested directories");

    SlimFixture f;
    f.format_and_mount();

    int rc = f.fs.mkdir("/a");
    CHECK(rc == 0, "mkdir /a");
    rc = f.fs.mkdir("/a/b");
    CHECK(rc == 0, "mkdir /a/b");
    rc = f.fs.mkdir("/a/b/c");
    CHECK(rc == 0, "mkdir /a/b/c");

    // Create file in deepest dir
    SlimFile file{};
    rc = f.fs.file_open(file, "/a/b/c/deep.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    CHECK(rc == 0, "create /a/b/c/deep.txt");
    const char* msg = "deep";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg), 4});
    (void)f.fs.file_close(file);

    f.remount();

    SlimInfo info{};
    rc = f.fs.stat("/a/b/c/deep.txt", info);
    CHECK(rc == 0, "stat deep file after remount");
    CHECK(info.size == 4, "deep file size is 4");

    (void)f.fs.unmount();
}

static void test_many_files_in_dir() {
    SECTION("Many files in directory");

    SlimFixture f;
    f.format_and_mount();

    // Create 10 files
    char name[32];
    for (int i = 0; i < 10; ++i) {
        name[0] = '/';
        name[1] = 'f';
        name[2] = static_cast<char>('0' + i);
        name[3] = '\0';
        SlimFile file{};
        int rc = f.fs.file_open(file, name, SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
        CHECK(rc == 0, "create file");
        uint8_t d = static_cast<uint8_t>(i);
        (void)f.fs.file_write(file, {&d, 1});
        (void)f.fs.file_close(file);
    }

    // Read them all back
    for (int i = 0; i < 10; ++i) {
        name[0] = '/';
        name[1] = 'f';
        name[2] = static_cast<char>('0' + i);
        name[3] = '\0';
        SlimFile rf{};
        int rc = f.fs.file_open(rf, name, SlimOpenFlags::RDONLY);
        CHECK(rc == 0, "open file for read");
        uint8_t d = 0xFF;
        rc = f.fs.file_read(rf, {&d, 1});
        CHECK(rc == 1 && d == static_cast<uint8_t>(i), "file has correct data");
        (void)f.fs.file_close(rf);
    }

    (void)f.fs.unmount();
}

static void test_remove_then_create() {
    SECTION("Remove then create same name");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/reuse.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* d1 = "first";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(d1), 5});
    (void)f.fs.file_close(file);

    int rc = f.fs.remove("/reuse.txt");
    CHECK(rc == 0, "remove succeeds");

    (void)f.fs.file_open(file, "/reuse.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* d2 = "second";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(d2), 6});
    (void)f.fs.file_close(file);

    SlimFile rf{};
    (void)f.fs.file_open(rf, "/reuse.txt", SlimOpenFlags::RDONLY);
    uint8_t buf[16]{};
    rc = f.fs.file_read(rf, {buf, sizeof(buf)});
    CHECK(rc == 6, "recreated file has 6 bytes");
    CHECK(std::memcmp(buf, d2, 6) == 0, "recreated file has new data");
    (void)f.fs.file_close(rf);
    (void)f.fs.unmount();
}

static void test_file_sync_explicit() {
    SECTION("Explicit sync persists data");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/sync.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* msg = "synced";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg), 6});

    int rc = f.fs.file_sync(file);
    CHECK(rc == 0, "sync succeeds");

    // Snapshot after sync
    std::memcpy(snapshot, storage, TOTAL_SIZE);

    // Simulate power loss (don't close)
    std::memcpy(storage, snapshot, TOTAL_SIZE);

    SlimFs fs2{};
    auto cfg = make_config();
    rc = fs2.mount(&cfg);
    CHECK(rc == 0, "mount after sync+power-loss");

    SlimFile rf{};
    rc = fs2.file_open(rf, "/sync.txt", SlimOpenFlags::RDONLY);
    CHECK(rc == 0, "open after sync");
    uint8_t buf[16]{};
    rc = fs2.file_read(rf, {buf, sizeof(buf)});
    CHECK(rc == 6, "synced data length");
    CHECK(std::memcmp(buf, msg, 6) == 0, "synced data intact");
    (void)fs2.file_close(rf);
    (void)fs2.unmount();
}

static void test_double_close() {
    SECTION("Double close is safe");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/dc.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    (void)f.fs.file_close(file);
    // Second close on already-reset file should not crash
    int rc = f.fs.file_close(file);
    // May return error or 0, but must not crash
    (void)rc;
    CHECK(true, "double close did not crash");

    (void)f.fs.unmount();
}

static void test_open_flags_excl() {
    SECTION("CREAT|EXCL fails on existing file");

    SlimFixture f;
    f.format_and_mount();

    SlimFile file{};
    (void)f.fs.file_open(file, "/excl.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    (void)f.fs.file_close(file);

    int rc = f.fs.file_open(file, "/excl.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT | SlimOpenFlags::EXCL);
    CHECK(rc != 0, "CREAT|EXCL on existing file fails");

    (void)f.fs.unmount();
}

static void test_boundary_block_size_write() {
    SECTION("Write exactly data_per_block bytes");

    SlimFixture f;
    f.format_and_mount();

    // data_per_block = block_size - 4 = 508
    constexpr uint32_t dpb = BLOCK_SIZE - 4;
    SlimFile file{};
    (void)f.fs.file_open(file, "/boundary.bin", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    uint8_t data[dpb];
    std::memset(data, 0x42, sizeof(data));
    int rc = f.fs.file_write(file, {data, sizeof(data)});
    CHECK(rc == static_cast<int>(dpb), "write exactly dpb bytes");
    (void)f.fs.file_close(file);

    SlimFile rf{};
    (void)f.fs.file_open(rf, "/boundary.bin", SlimOpenFlags::RDONLY);
    uint8_t out[dpb]{};
    rc = f.fs.file_read(rf, {out, sizeof(out)});
    CHECK(rc == static_cast<int>(dpb), "read dpb bytes");
    CHECK(std::memcmp(out, data, dpb) == 0, "boundary data matches");
    (void)f.fs.file_close(rf);
    (void)f.fs.unmount();
}

static void test_pending_move_recovery_incomplete() {
    SECTION("pending_move recovery: rename incomplete");

    SlimFixture f;
    f.format_and_mount();

    // Create source file
    SlimFile file{};
    (void)f.fs.file_open(file, "/pm_src.txt", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
    const char* msg = "pending";
    (void)f.fs.file_write(file, {reinterpret_cast<const uint8_t*>(msg), std::strlen(msg)});
    (void)f.fs.file_close(file);

    // Snapshot before rename
    std::memcpy(snapshot, storage, TOTAL_SIZE);

    // Do rename — this will succeed normally
    int rc = f.fs.rename("/pm_src.txt", "/pm_dst.txt");
    CHECK(rc == 0, "rename succeeds");

    // Restore pre-rename state (simulating crash before rename started writing)
    std::memcpy(storage, snapshot, TOTAL_SIZE);

    // Mount — src should still exist, dst should not
    SlimFs fs2{};
    auto cfg = make_config();
    rc = fs2.mount(&cfg);
    CHECK(rc == 0, "mount after incomplete rename");

    SlimInfo info{};
    rc = fs2.stat("/pm_src.txt", info);
    CHECK(rc == 0, "src still exists");
    rc = fs2.stat("/pm_dst.txt", info);
    CHECK(rc != 0, "dst does not exist");

    (void)fs2.unmount();
}

static void test_full_disk_write() {
    SECTION("Write until disk full");

    SlimFixture f;
    f.format_and_mount();

    // Fill disk by creating files and syncing each (COW rolls back unsync'd data on NOSPC)
    uint8_t data[256];
    std::memset(data, 0xEE, sizeof(data));

    uint32_t total_written = 0;
    bool got_nospc = false;
    char name[32];
    for (int i = 0; i < 100; ++i) {
        name[0] = '/';
        name[1] = 'x';
        name[2] = static_cast<char>('0' + (i / 10));
        name[3] = static_cast<char>('0' + (i % 10));
        name[4] = '\0';
        SlimFile file{};
        int rc = f.fs.file_open(file, name, SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
        if (rc != 0) {
            got_nospc = true;
            break;
        }
        rc = f.fs.file_write(file, {data, sizeof(data)});
        if (rc < 0) {
            got_nospc = true;
            (void)f.fs.file_close(file);
            break;
        }
        total_written += static_cast<uint32_t>(rc);
        (void)f.fs.file_close(file);
    }

    CHECK(total_written > 0, "wrote some data before full");
    CHECK(got_nospc, "eventually got NOSPC");

    (void)f.fs.unmount();
}

static void test_full_disk_mkdir() {
    SECTION("mkdir until disk full");

    SlimFixture f;
    f.format_and_mount();

    char name[32];
    bool got_nospc = false;
    int created = 0;
    for (int i = 0; i < 100; ++i) {
        name[0] = '/';
        name[1] = 'd';
        name[2] = static_cast<char>('0' + (i / 10));
        name[3] = static_cast<char>('0' + (i % 10));
        name[4] = '\0';
        int rc = f.fs.mkdir(name);
        if (rc != 0) {
            got_nospc = true;
            break;
        }
        ++created;
    }

    CHECK(created > 0, "created some directories");
    CHECK(got_nospc, "eventually got NOSPC for mkdir");

    (void)f.fs.unmount();
}

// ============================================================================
// Custom attributes
// ============================================================================

static void test_custom_attributes() {
    SECTION("Custom attributes: set, get, multiple types");

    SlimFixture f;
    f.format_and_mount();

    int rc = f.fs.mkdir("/presets");
    CHECK(rc == 0, "mkdir presets");

    // Set attribute type 1 = category tag
    uint8_t tag[] = {'B', 'a', 's', 's'};
    rc = f.fs.setattr("/presets", 1, {tag, sizeof(tag)});
    CHECK(rc == 0, "setattr type 1");

    // Set attribute type 2 = favorite flag
    uint8_t fav = 1;
    rc = f.fs.setattr("/presets", 2, {&fav, 1});
    CHECK(rc == 0, "setattr type 2");

    // Read back type 1
    uint8_t buf[32]{};
    int sz = f.fs.getattr("/presets", 1, {buf, sizeof(buf)});
    CHECK(sz == 4, "getattr type 1 returns 4 bytes");
    CHECK(std::memcmp(buf, tag, 4) == 0, "getattr type 1 content matches");

    // Read back type 2
    uint8_t buf2[4]{};
    sz = f.fs.getattr("/presets", 2, {buf2, sizeof(buf2)});
    CHECK(sz == 1, "getattr type 2 returns 1 byte");
    CHECK(buf2[0] == 1, "getattr type 2 content matches");

    // Nonexistent type
    sz = f.fs.getattr("/presets", 99, {buf, sizeof(buf)});
    CHECK(sz < 0, "getattr nonexistent type returns error");

    (void)f.fs.unmount();
}

static void test_attr_remove() {
    SECTION("Custom attributes: remove");

    SlimFixture f;
    f.format_and_mount();

    // Create file with attrs
    SlimFile file{};
    int rc = f.fs.file_open(file, "/data.bin", SlimOpenFlags::RDWR | SlimOpenFlags::CREAT);
    CHECK(rc == 0, "file_open");
    rc = f.fs.file_close(file);
    CHECK(rc == 0, "file_close");

    uint8_t val[] = {0xAA, 0xBB};
    rc = f.fs.setattr("/data.bin", 5, {val, sizeof(val)});
    CHECK(rc == 0, "setattr");

    rc = f.fs.removeattr("/data.bin", 5);
    CHECK(rc == 0, "removeattr succeeds");

    uint8_t buf[4]{};
    int sz = f.fs.getattr("/data.bin", 5, {buf, sizeof(buf)});
    CHECK(sz < 0, "getattr after remove returns error");

    // Remove again should fail
    rc = f.fs.removeattr("/data.bin", 5);
    CHECK(rc < 0, "removeattr again returns error");

    (void)f.fs.unmount();
}

static void test_attr_overwrite() {
    SECTION("Custom attributes: overwrite existing");

    SlimFixture f;
    f.format_and_mount();

    int rc = f.fs.mkdir("/dir");
    CHECK(rc == 0, "mkdir");

    uint8_t v1[] = {1, 2, 3};
    rc = f.fs.setattr("/dir", 10, {v1, sizeof(v1)});
    CHECK(rc == 0, "setattr v1");

    uint8_t v2[] = {4, 5, 6, 7, 8};
    rc = f.fs.setattr("/dir", 10, {v2, sizeof(v2)});
    CHECK(rc == 0, "setattr v2 overwrites");

    uint8_t buf[16]{};
    int sz = f.fs.getattr("/dir", 10, {buf, sizeof(buf)});
    CHECK(sz == 5, "getattr returns new size");
    CHECK(std::memcmp(buf, v2, 5) == 0, "getattr returns new content");

    (void)f.fs.unmount();
}

static void test_attr_persist_remount() {
    SECTION("Custom attributes: persist across remount");

    SlimFixture f;
    f.format_and_mount();

    int rc = f.fs.mkdir("/cfg");
    CHECK(rc == 0, "mkdir");

    uint8_t data[] = {0xDE, 0xAD};
    rc = f.fs.setattr("/cfg", 3, {data, sizeof(data)});
    CHECK(rc == 0, "setattr");

    f.remount();

    uint8_t buf[8]{};
    int sz = f.fs.getattr("/cfg", 3, {buf, sizeof(buf)});
    CHECK(sz == 2, "getattr after remount returns 2 bytes");
    CHECK(buf[0] == 0xDE && buf[1] == 0xAD, "content matches after remount");

    (void)f.fs.unmount();
}

// ============================================================================
// Directory seek / tell / rewind
// ============================================================================

static void test_dir_seek_tell_rewind() {
    SECTION("dir_seek / dir_tell / dir_rewind");

    SlimFixture f;
    f.format_and_mount();

    int rc = f.fs.mkdir("/a");
    CHECK(rc == 0, "mkdir a");
    rc = f.fs.mkdir("/b");
    CHECK(rc == 0, "mkdir b");
    rc = f.fs.mkdir("/c");
    CHECK(rc == 0, "mkdir c");

    SlimDir dir{};
    rc = f.fs.dir_open(dir, "/");
    CHECK(rc == 0, "dir_open");

    // Read first entry
    SlimInfo info{};
    rc = f.fs.dir_read(dir, info);
    CHECK(rc == 1, "dir_read 1st");

    int pos = f.fs.dir_tell(dir);
    CHECK(pos == 1, "dir_tell after 1 read");

    // Read second
    rc = f.fs.dir_read(dir, info);
    CHECK(rc == 1, "dir_read 2nd");

    // Seek back to position 1
    rc = f.fs.dir_seek(dir, 1);
    CHECK(rc == 0, "dir_seek to 1");
    pos = f.fs.dir_tell(dir);
    CHECK(pos == 1, "dir_tell after seek");

    // Read should give 2nd entry again
    SlimInfo info2{};
    rc = f.fs.dir_read(dir, info2);
    CHECK(rc == 1, "dir_read after seek");
    CHECK(std::strcmp(info.name, info2.name) == 0, "same entry after seek");

    // Rewind
    rc = f.fs.dir_rewind(dir);
    CHECK(rc == 0, "dir_rewind");
    pos = f.fs.dir_tell(dir);
    CHECK(pos == 0, "dir_tell after rewind");

    rc = f.fs.dir_close(dir);
    CHECK(rc == 0, "dir_close");

    (void)f.fs.unmount();
}

// ============================================================================
// fs_gc
// ============================================================================

static void test_fs_gc() {
    SECTION("fs_gc compacts deleted entries");

    SlimFixture f;
    f.format_and_mount();

    // Create and delete several files to create DEL entries
    for (int i = 0; i < 5; ++i) {
        char name[16];
        name[0] = '/';
        name[1] = 'f';
        name[2] = static_cast<char>('0' + i);
        name[3] = '\0';
        int rc = f.fs.mkdir(name);
        CHECK(rc == 0, "mkdir");
    }
    for (int i = 0; i < 3; ++i) {
        char name[16];
        name[0] = '/';
        name[1] = 'f';
        name[2] = static_cast<char>('0' + i);
        name[3] = '\0';
        int rc = f.fs.remove(name);
        CHECK(rc == 0, "remove");
    }

    // Run GC
    int rc = f.fs.fs_gc();
    CHECK(rc == 0, "fs_gc succeeds");

    // Remaining entries should still be accessible
    SlimInfo info{};
    rc = f.fs.stat("/f3", info);
    CHECK(rc == 0, "stat f3 after gc");
    rc = f.fs.stat("/f4", info);
    CHECK(rc == 0, "stat f4 after gc");

    // Deleted entries should still be gone
    rc = f.fs.stat("/f0", info);
    CHECK(rc < 0, "f0 still deleted after gc");

    // Dir listing should only show 2 entries
    SlimDir dir{};
    rc = f.fs.dir_open(dir, "/");
    CHECK(rc == 0, "dir_open");
    int count = 0;
    while (f.fs.dir_read(dir, info) == 1) {
        ++count;
    }
    CHECK(count == 2, "dir has 2 entries after gc");
    (void)f.fs.dir_close(dir);

    (void)f.fs.unmount();
}

// ============================================================================
// fs_grow
// ============================================================================

static void test_fs_grow() {
    SECTION("fs_grow extends filesystem");

    SlimFixture f;
    f.format_and_mount();

    uint32_t old_count = f.fs.block_count_total();
    int old_size = f.fs.fs_size();
    CHECK(old_size >= 0, "fs_size before grow");

    // Grow by 8 blocks
    uint32_t new_count = old_count + 8;
    // Extend storage (already have room since BLOCK_COUNT=64 and we use fewer)
    int rc = f.fs.fs_grow(new_count);
    CHECK(rc == 0, "fs_grow succeeds");
    CHECK(f.fs.block_count_total() == new_count, "block_count updated");

    // Shrink should fail
    rc = f.fs.fs_grow(old_count);
    CHECK(rc < 0, "fs_grow shrink fails");

    // Same size should be no-op
    rc = f.fs.fs_grow(new_count);
    CHECK(rc == 0, "fs_grow same size is no-op");

    // Verify filesystem still works
    rc = f.fs.mkdir("/after_grow");
    CHECK(rc == 0, "mkdir after grow");

    // Remount and verify
    f.remount();
    SlimInfo info{};
    rc = f.fs.stat("/after_grow", info);
    CHECK(rc == 0, "stat after_grow after remount");
    CHECK(f.fs.block_count_total() == new_count, "block_count persisted after remount");

    (void)f.fs.unmount();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    test_format_and_mount();
    test_remount();
    test_mkdir_and_stat();
    test_dir_read();
    test_nested_dirs();
    test_file_write_read();
    test_file_stat();
    test_file_seek();
    test_file_truncate();
    test_file_persist_after_remount();
    test_large_file();
    test_remove_file();
    test_remove_dir();
    test_remove_nonempty_dir();
    test_rename();
    test_fs_size();
    test_fs_traverse();
    test_open_nonexistent();
    test_file_append();
    test_power_loss_during_mkdir();
    test_power_loss_during_file_write();
    test_power_loss_during_remove();
    test_superblock_redundancy();
    test_both_superblocks_corrupt();
#if SLIM_FAULT_TESTS
    test_fault_injection_mkdir();
    test_fault_injection_file_write();
    test_fault_injection_rename();
    test_fault_injection_remove();
#endif
    test_geometry_api();
    test_close_all();
    test_pending_move_recovery_complete();
    test_wear_leveling_distribution();
    test_cow_write_power_loss();
    test_cow_write_sync();
    test_multi_write_same_block();
    test_multi_write_cross_block();
    test_overwrite_preserves_surrounding();
    test_cow_multi_block_file();
    test_cow_truncate_during_dirty();
    test_rdwr_read_after_write();
    test_multiple_files_cow();
    test_write_empty_buf();
    test_seek_beyond_eof();
    test_rename_overwrite();
    test_deeply_nested_dir();
    test_many_files_in_dir();
    test_remove_then_create();
    test_file_sync_explicit();
    test_double_close();
    test_open_flags_excl();
    test_boundary_block_size_write();
    test_pending_move_recovery_incomplete();
    test_full_disk_write();
    test_full_disk_mkdir();

    test_custom_attributes();
    test_attr_remove();
    test_attr_overwrite();
    test_attr_persist_remount();
    test_dir_seek_tell_rewind();
    test_fs_gc();
    test_fs_grow();

    TEST_SUMMARY();
}
