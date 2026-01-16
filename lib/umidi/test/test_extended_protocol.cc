// SPDX-License-Identifier: MIT
// umidi Extended Protocol Tests - Transport, State, Object Transfer
#include "test_framework.hh"
#include "umidi/protocol/umi_sysex.hh"
#include "umidi/protocol/umi_transport.hh"
#include "umidi/protocol/umi_state.hh"
#include "umidi/protocol/umi_object.hh"

using namespace umidi;
using namespace umidi::protocol;
using namespace umidi::test;

// =============================================================================
// Transport Abstraction Tests
// =============================================================================

TEST(bulk_crc16_basic) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint16_t crc = bulk::crc16(data, sizeof(data));
    ASSERT(crc != 0);

    // Same data -> same CRC
    uint16_t crc2 = bulk::crc16(data, sizeof(data));
    ASSERT_EQ(crc, crc2);
    TEST_PASS();
}

TEST(bulk_crc16_different_data) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t data2[] = {0x01, 0x02, 0x04};

    uint16_t crc1 = bulk::crc16(data1, sizeof(data1));
    uint16_t crc2 = bulk::crc16(data2, sizeof(data2));

    ASSERT_NE(crc1, crc2);
    TEST_PASS();
}

TEST(bulk_encode_decode) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x80, 0xFF};
    uint8_t frame[32];
    uint8_t decoded[32];

    size_t frame_len = bulk::encode_frame(data, sizeof(data), frame);
    ASSERT_EQ(frame_len, sizeof(data) + bulk::FRAME_OVERHEAD);

    // Check length prefix
    size_t encoded_len = (size_t(frame[0]) << 8) | frame[1];
    ASSERT_EQ(encoded_len, sizeof(data));

    size_t decoded_len = bulk::decode_frame(frame, frame_len, decoded, sizeof(decoded));
    ASSERT_EQ(decoded_len, sizeof(data));

    for (size_t i = 0; i < sizeof(data); ++i) {
        ASSERT_EQ(decoded[i], data[i]);
    }
    TEST_PASS();
}

TEST(bulk_invalid_frame_too_short) {
    uint8_t decoded[32];
    uint8_t short_frame[] = {0x00, 0x01, 0x00};
    size_t result = bulk::decode_frame(short_frame, 3, decoded, sizeof(decoded));
    ASSERT_EQ(result, 0);
    TEST_PASS();
}

TEST(bulk_invalid_frame_length_mismatch) {
    uint8_t decoded[32];
    uint8_t bad_len[] = {0x00, 0x10, 0x00, 0x00, 0x00, 0x00};
    size_t result = bulk::decode_frame(bad_len, sizeof(bad_len), decoded, sizeof(decoded));
    ASSERT_EQ(result, 0);
    TEST_PASS();
}

TEST(bulk_invalid_frame_crc_mismatch) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint8_t frame[32];

    size_t frame_len = bulk::encode_frame(data, sizeof(data), frame);

    // Corrupt CRC
    frame[frame_len - 1] ^= 0x01;

    uint8_t decoded[32];
    size_t result = bulk::decode_frame(frame, frame_len, decoded, sizeof(decoded));
    ASSERT_EQ(result, 0);
    TEST_PASS();
}

TEST(transport_capabilities_sysex) {
    ASSERT_EQ(SYSEX_CAPABILITIES.supports_8bit, false);
    ASSERT_EQ(SYSEX_CAPABILITIES.requires_encoding, true);
    ASSERT_EQ(SYSEX_CAPABILITIES.max_packet_size, 64u);
    TEST_PASS();
}

TEST(transport_capabilities_bulk) {
    ASSERT_EQ(BULK_CAPABILITIES.supports_8bit, true);
    ASSERT_EQ(BULK_CAPABILITIES.requires_encoding, false);
    ASSERT_EQ(BULK_CAPABILITIES.max_packet_size, 512u);
    ASSERT_GT(BULK_CAPABILITIES.max_message_size, SYSEX_CAPABILITIES.max_message_size);
    TEST_PASS();
}

TEST(transport_manager_basic) {
    TransportManager<2> manager;
    ASSERT_EQ(manager.count(), 0u);
    ASSERT(!manager.any_connected());
    ASSERT(manager.get_best() == nullptr);
    TEST_PASS();
}

// =============================================================================
// State Synchronization Tests
// =============================================================================

TEST(state_report_size) {
    ASSERT_EQ(sizeof(StateReport), 16);
    TEST_PASS();
}

TEST(resume_info_size) {
    ASSERT_EQ(sizeof(ResumeInfo), 20);
    TEST_PASS();
}

TEST(boot_verification_size) {
    ASSERT_EQ(sizeof(BootVerification), 16);
    TEST_PASS();
}

TEST(state_manager_init) {
    StateManager mgr;
    mgr.init();

    ASSERT_EQ(mgr.state(), DeviceState::IDLE);
    ASSERT_EQ(mgr.session_id(), 0u);
    ASSERT_EQ(mgr.received_bytes(), 0u);
    ASSERT_EQ(mgr.total_bytes(), 0u);
    TEST_PASS();
}

TEST(state_manager_start_session) {
    StateManager mgr;
    mgr.init();

    uint32_t session_id = mgr.start_session(1000);
    ASSERT(session_id > 0);
    ASSERT_EQ(mgr.state(), DeviceState::UPDATE_STARTING);
    ASSERT_EQ(mgr.total_bytes(), 1000u);
    ASSERT_EQ(mgr.session_id(), session_id);
    TEST_PASS();
}

TEST(state_manager_progress) {
    StateManager mgr;
    mgr.init();

    mgr.start_session(1000);
    mgr.set_state(DeviceState::UPDATE_RECEIVING);

    mgr.record_received(100, 1);
    auto report = mgr.build_report();
    ASSERT_EQ(report.progress_percent, 10);
    ASSERT_EQ(report.last_ack_seq, 1);
    ASSERT_EQ(report.received_bytes, 100u);

    mgr.record_received(400, 2);
    report = mgr.build_report();
    ASSERT_EQ(report.progress_percent, 50);
    ASSERT_EQ(report.received_bytes, 500u);
    TEST_PASS();
}

TEST(state_manager_flags) {
    StateManager mgr;
    mgr.init();

    mgr.set_flag(StateReport::FLAG_AUTHENTICATED);
    auto report = mgr.build_report();
    ASSERT(report.is_authenticated());
    ASSERT(!report.is_resumable());

    mgr.set_flag(StateReport::FLAG_RESUMABLE);
    report = mgr.build_report();
    ASSERT(report.is_authenticated());
    ASSERT(report.is_resumable());

    mgr.clear_flag(StateReport::FLAG_AUTHENTICATED);
    report = mgr.build_report();
    ASSERT(!report.is_authenticated());
    ASSERT(report.is_resumable());
    TEST_PASS();
}

TEST(state_manager_error) {
    StateManager mgr;
    mgr.init();

    mgr.set_error(0x05);
    ASSERT_EQ(mgr.state(), DeviceState::ERROR);

    auto report = mgr.build_report();
    ASSERT_EQ(report.last_error, 0x05);

    mgr.clear_error();
    ASSERT_EQ(mgr.state(), DeviceState::IDLE);
    TEST_PASS();
}

TEST(state_manager_subscribers) {
    StateManager mgr;
    mgr.init();

    ASSERT(!mgr.has_subscribers());

    mgr.add_subscriber();
    ASSERT(mgr.has_subscribers());

    mgr.add_subscriber();
    mgr.remove_subscriber();
    ASSERT(mgr.has_subscribers());

    mgr.remove_subscriber();
    ASSERT(!mgr.has_subscribers());
    TEST_PASS();
}

TEST(boot_verification_init) {
    BootVerification boot;
    boot.init(3);

    ASSERT(boot.is_valid());
    ASSERT_EQ(boot.magic, BootVerification::MAGIC);
    ASSERT_EQ(boot.max_attempts, 3);
    ASSERT_EQ(boot.boot_count, 0);
    ASSERT_EQ(boot.verified, 1);
    ASSERT(!boot.should_rollback());
    TEST_PASS();
}

TEST(boot_verification_increment) {
    BootVerification boot;
    boot.init(3);

    boot.increment_boot();
    ASSERT_EQ(boot.boot_count, 1);
    ASSERT_EQ(boot.verified, 0);
    ASSERT(!boot.should_rollback());

    boot.increment_boot();
    ASSERT_EQ(boot.boot_count, 2);
    ASSERT(!boot.should_rollback());

    boot.increment_boot();
    ASSERT_EQ(boot.boot_count, 3);
    ASSERT(boot.should_rollback());
    TEST_PASS();
}

TEST(boot_verification_success) {
    BootVerification boot;
    boot.init(3);

    boot.increment_boot();
    boot.increment_boot();
    ASSERT_EQ(boot.boot_count, 2);

    boot.mark_success(12345);
    ASSERT_EQ(boot.boot_count, 0);
    ASSERT_EQ(boot.verified, 1);
    ASSERT_EQ(boot.last_success_time, 12345u);
    ASSERT(!boot.should_rollback());
    TEST_PASS();
}

TEST(boot_verification_checksum) {
    BootVerification boot;
    boot.init(3);

    boot.update_checksum();
    ASSERT(boot.verify_checksum());

    boot.boot_count = 5;
    ASSERT(!boot.verify_checksum());

    boot.update_checksum();
    ASSERT(boot.verify_checksum());
    TEST_PASS();
}

TEST(resume_info_can_resume) {
    ResumeInfo info{
        .session_id = 12345,
        .firmware_hash = 0xDEADBEEF,
        .received_bytes = 500,
        .total_bytes = 1000,
        .last_ack_seq = 5,
        .chunk_size = 64,
        .reserved = 0,
    };

    ASSERT(info.can_resume(12345, 0xDEADBEEF));
    ASSERT(!info.can_resume(12345, 0x12345678));  // Different hash
    ASSERT(!info.can_resume(99999, 0xDEADBEEF));  // Different session
    ASSERT_EQ(info.next_offset(), 500u);
    TEST_PASS();
}

// =============================================================================
// Object Transfer Protocol Tests
// =============================================================================

TEST(object_header_size) {
    ASSERT_EQ(sizeof(ObjectHeader), 80);
    TEST_PASS();
}

TEST(sequence_metadata_size) {
    ASSERT_EQ(sizeof(SequenceMetadata), 16);
    TEST_PASS();
}

TEST(sample_metadata_size) {
    ASSERT_EQ(sizeof(SampleMetadata), 16);
    TEST_PASS();
}

TEST(preset_metadata_size) {
    ASSERT_EQ(sizeof(PresetMetadata), 16);
    TEST_PASS();
}

TEST(config_metadata_size) {
    ASSERT_EQ(sizeof(ConfigMetadata), 16);
    TEST_PASS();
}

TEST(object_header_init) {
    ObjectHeader header;
    header.init(ObjectType::SEQUENCE, 42, "Test Sequence", 1024);

    ASSERT(header.is_valid());
    ASSERT_EQ(header.magic, OBJECT_MAGIC);
    ASSERT_EQ(header.header_version, 1);
    ASSERT_EQ(header.type(), ObjectType::SEQUENCE);
    ASSERT_EQ(header.object_id, 42u);
    ASSERT_EQ(header.data_size, 1024u);
    ASSERT(strcmp(header.get_name(), "Test Sequence") == 0);
    TEST_PASS();
}

TEST(object_header_name_truncation) {
    ObjectHeader header;
    header.init(ObjectType::SAMPLE, 1,
                "This name is way too long and should be truncated", 512);

    // Name should be null-terminated within 32 bytes
    ASSERT_LE(strlen(header.get_name()), 31);
    TEST_PASS();
}

TEST(object_header_flags) {
    ObjectHeader header;
    header.init(ObjectType::SAMPLE, 1, "Sample", 4096);

    header.set_flags(ObjectFlags::COMPRESSED | ObjectFlags::READONLY);
    auto flags = header.get_flags();

    ASSERT(flags & ObjectFlags::COMPRESSED);
    ASSERT(flags & ObjectFlags::READONLY);
    ASSERT(!(flags & ObjectFlags::ENCRYPTED));
    ASSERT(!(flags & ObjectFlags::SIGNED));
    TEST_PASS();
}

TEST(sequence_metadata_bpm) {
    SequenceMetadata meta{};

    meta.set_bpm(120.0f);
    ASSERT_EQ(meta.bpm_x10, 1200);
    ASSERT_NEAR(meta.get_bpm(), 120.0f, 0.1f);

    meta.set_bpm(145.5f);
    ASSERT_EQ(meta.bpm_x10, 1455);
    ASSERT_NEAR(meta.get_bpm(), 145.5f, 0.1f);
    TEST_PASS();
}

TEST(sample_metadata_root_note) {
    SampleMetadata meta{};

    meta.set_root_note(60, 0);  // Middle C, no detuning
    ASSERT_EQ(meta.get_root_note(), 60);
    ASSERT_EQ(meta.get_fine_tune(), 0);

    meta.set_root_note(69, 50);  // A4 + 50 cents
    ASSERT_EQ(meta.get_root_note(), 69);
    ASSERT_EQ(meta.get_fine_tune(), 50);
    TEST_PASS();
}

TEST(ram_storage_init) {
    RAMObjectStorage<8, 4096> storage;

    auto info = storage.get_storage_info();
    ASSERT_EQ(info.object_count, 0u);
    ASSERT_EQ(info.max_objects, 8u);
    ASSERT_EQ(info.total_space, 4096u);
    ASSERT_EQ(info.free_space, 4096u);
    TEST_PASS();
}

TEST(ram_storage_write_read) {
    RAMObjectStorage<8, 4096> storage;

    // Create object
    ObjectHeader header;
    header.init(ObjectType::PRESET, storage.generate_id(), "Test Preset", 64);

    ASSERT(storage.write_begin(header));

    uint8_t data[64];
    for (int i = 0; i < 64; ++i) data[i] = static_cast<uint8_t>(i);
    ASSERT(storage.write_data(header.object_id, 0, data, 64));
    ASSERT(storage.write_commit(header.object_id));

    // Verify stored
    auto info = storage.get_storage_info();
    ASSERT_EQ(info.object_count, 1u);

    // Read back header
    ObjectHeader read_header;
    ASSERT(storage.get_header(header.object_id, read_header));
    ASSERT(strcmp(read_header.get_name(), "Test Preset") == 0);

    // Read back data
    uint8_t read_data[64];
    size_t read_len = storage.read_data(header.object_id, 0, read_data, 64);
    ASSERT_EQ(read_len, 64u);
    ASSERT_EQ(read_data[0], 0);
    ASSERT_EQ(read_data[63], 63);
    TEST_PASS();
}

TEST(ram_storage_partial_read) {
    RAMObjectStorage<8, 4096> storage;

    ObjectHeader header;
    header.init(ObjectType::SAMPLE, storage.generate_id(), "Sample", 100);

    storage.write_begin(header);
    uint8_t data[100];
    for (int i = 0; i < 100; ++i) data[i] = static_cast<uint8_t>(i);
    storage.write_data(header.object_id, 0, data, 100);
    storage.write_commit(header.object_id);

    // Read from offset
    uint8_t partial[20];
    size_t len = storage.read_data(header.object_id, 50, partial, 20);
    ASSERT_EQ(len, 20u);
    ASSERT_EQ(partial[0], 50);
    ASSERT_EQ(partial[19], 69);
    TEST_PASS();
}

TEST(ram_storage_delete) {
    RAMObjectStorage<8, 4096> storage;

    ObjectHeader header;
    header.init(ObjectType::CONFIG, storage.generate_id(), "Config", 16);
    storage.write_begin(header);
    uint8_t data[16] = {0};
    storage.write_data(header.object_id, 0, data, 16);
    storage.write_commit(header.object_id);

    ASSERT_EQ(storage.get_storage_info().object_count, 1u);

    ASSERT(storage.remove(header.object_id));
    ASSERT_EQ(storage.get_storage_info().object_count, 0u);

    // Can't get deleted header
    ObjectHeader deleted_header;
    ASSERT(!storage.get_header(header.object_id, deleted_header));
    TEST_PASS();
}

TEST(ram_storage_rename) {
    RAMObjectStorage<8, 4096> storage;

    ObjectHeader header;
    header.init(ObjectType::SEQUENCE, storage.generate_id(), "Original", 32);
    storage.write_begin(header);
    uint8_t data[32] = {0};
    storage.write_data(header.object_id, 0, data, 32);
    storage.write_commit(header.object_id);

    ASSERT(storage.rename(header.object_id, "Renamed"));

    ObjectHeader read_header;
    storage.get_header(header.object_id, read_header);
    ASSERT(strcmp(read_header.get_name(), "Renamed") == 0);
    TEST_PASS();
}

TEST(ram_storage_list) {
    RAMObjectStorage<8, 4096> storage;

    // Create multiple objects
    for (int i = 0; i < 3; ++i) {
        ObjectHeader header;
        char name[32];
        snprintf(name, sizeof(name), "Object %d", i);
        header.init(ObjectType::PRESET, storage.generate_id(), name, 16);
        storage.write_begin(header);
        uint8_t data[16] = {0};
        storage.write_data(header.object_id, 0, data, 16);
        storage.write_commit(header.object_id);
    }

    ObjectHeader headers[8];
    size_t count = storage.list(ObjectType::PRESET, headers, 8);
    ASSERT_EQ(count, 3u);
    TEST_PASS();
}

TEST(ram_storage_abort) {
    RAMObjectStorage<8, 4096> storage;

    ObjectHeader header;
    header.init(ObjectType::WAVETABLE, storage.generate_id(), "Aborted", 256);
    storage.write_begin(header);

    // Write some data but don't commit
    uint8_t data[64] = {0};
    storage.write_data(header.object_id, 0, data, 64);

    // Abort
    storage.write_abort(header.object_id);

    // Should not be stored
    ASSERT_EQ(storage.get_storage_info().object_count, 0u);
    TEST_PASS();
}

TEST(ram_storage_full) {
    RAMObjectStorage<2, 256> storage;  // Only 2 slots

    for (int i = 0; i < 2; ++i) {
        ObjectHeader header;
        header.init(ObjectType::CONFIG, storage.generate_id(), "Slot", 8);
        storage.write_begin(header);
        uint8_t data[8] = {0};
        storage.write_data(header.object_id, 0, data, 8);
        storage.write_commit(header.object_id);
    }

    // Third object should fail
    ObjectHeader header3;
    header3.init(ObjectType::CONFIG, storage.generate_id(), "Fail", 8);
    ASSERT(!storage.write_begin(header3));
    TEST_PASS();
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== umidi Extended Protocol Tests ===\n");

    SECTION("Bulk Transport");
    RUN_TEST(bulk_crc16_basic);
    RUN_TEST(bulk_crc16_different_data);
    RUN_TEST(bulk_encode_decode);
    RUN_TEST(bulk_invalid_frame_too_short);
    RUN_TEST(bulk_invalid_frame_length_mismatch);
    RUN_TEST(bulk_invalid_frame_crc_mismatch);

    SECTION("Transport Capabilities");
    RUN_TEST(transport_capabilities_sysex);
    RUN_TEST(transport_capabilities_bulk);
    RUN_TEST(transport_manager_basic);

    SECTION("State Report");
    RUN_TEST(state_report_size);
    RUN_TEST(resume_info_size);
    RUN_TEST(boot_verification_size);

    SECTION("State Manager");
    RUN_TEST(state_manager_init);
    RUN_TEST(state_manager_start_session);
    RUN_TEST(state_manager_progress);
    RUN_TEST(state_manager_flags);
    RUN_TEST(state_manager_error);
    RUN_TEST(state_manager_subscribers);

    SECTION("Boot Verification");
    RUN_TEST(boot_verification_init);
    RUN_TEST(boot_verification_increment);
    RUN_TEST(boot_verification_success);
    RUN_TEST(boot_verification_checksum);

    SECTION("Resume Info");
    RUN_TEST(resume_info_can_resume);

    SECTION("Object Header");
    RUN_TEST(object_header_size);
    RUN_TEST(object_header_init);
    RUN_TEST(object_header_name_truncation);
    RUN_TEST(object_header_flags);

    SECTION("Type-Specific Metadata");
    RUN_TEST(sequence_metadata_size);
    RUN_TEST(sample_metadata_size);
    RUN_TEST(preset_metadata_size);
    RUN_TEST(config_metadata_size);
    RUN_TEST(sequence_metadata_bpm);
    RUN_TEST(sample_metadata_root_note);

    SECTION("RAM Object Storage");
    RUN_TEST(ram_storage_init);
    RUN_TEST(ram_storage_write_read);
    RUN_TEST(ram_storage_partial_read);
    RUN_TEST(ram_storage_delete);
    RUN_TEST(ram_storage_rename);
    RUN_TEST(ram_storage_list);
    RUN_TEST(ram_storage_abort);
    RUN_TEST(ram_storage_full);

    return summary();
}
