// SPDX-License-Identifier: MIT
// umidi Extended Protocol Tests - Transport, State, Object Transfer
#include <umitest.hh>
#include "protocol/umi_sysex.hh"
#include "protocol/umi_transport.hh"
#include "protocol/umi_state.hh"
#include "protocol/umi_object.hh"

using namespace umidi;
using namespace umidi::protocol;
using namespace umitest;

// =============================================================================
// Transport Abstraction Tests
// =============================================================================

bool test_bulk_crc16_basic(TestContext& t) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint16_t crc = bulk::crc16(data, sizeof(data));
    t.assert_true(crc != 0);

    // Same data -> same CRC
    uint16_t crc2 = bulk::crc16(data, sizeof(data));
    t.assert_eq(crc, crc2);
    return true;
}

bool test_bulk_crc16_different_data(TestContext& t) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t data2[] = {0x01, 0x02, 0x04};

    uint16_t crc1 = bulk::crc16(data1, sizeof(data1));
    uint16_t crc2 = bulk::crc16(data2, sizeof(data2));

    t.assert_ne(crc1, crc2);
    return true;
}

bool test_bulk_encode_decode(TestContext& t) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x80, 0xFF};
    uint8_t frame[32];
    uint8_t decoded[32];

    size_t frame_len = bulk::encode_frame(data, sizeof(data), frame);
    t.assert_eq(frame_len, sizeof(data) + bulk::FRAME_OVERHEAD);

    // Check length prefix
    size_t encoded_len = (size_t(frame[0]) << 8) | frame[1];
    t.assert_eq(encoded_len, sizeof(data));

    size_t decoded_len = bulk::decode_frame(frame, frame_len, decoded, sizeof(decoded));
    t.assert_eq(decoded_len, sizeof(data));

    for (size_t i = 0; i < sizeof(data); ++i) {
        t.assert_eq(decoded[i], data[i]);
    }
    return true;
}

bool test_bulk_invalid_frame_too_short(TestContext& t) {
    uint8_t decoded[32];
    uint8_t short_frame[] = {0x00, 0x01, 0x00};
    size_t result = bulk::decode_frame(short_frame, 3, decoded, sizeof(decoded));
    t.assert_eq(result, 0);
    return true;
}

bool test_bulk_invalid_frame_length_mismatch(TestContext& t) {
    uint8_t decoded[32];
    uint8_t bad_len[] = {0x00, 0x10, 0x00, 0x00, 0x00, 0x00};
    size_t result = bulk::decode_frame(bad_len, sizeof(bad_len), decoded, sizeof(decoded));
    t.assert_eq(result, 0);
    return true;
}

bool test_bulk_invalid_frame_crc_mismatch(TestContext& t) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint8_t frame[32];

    size_t frame_len = bulk::encode_frame(data, sizeof(data), frame);

    // Corrupt CRC
    frame[frame_len - 1] ^= 0x01;

    uint8_t decoded[32];
    size_t result = bulk::decode_frame(frame, frame_len, decoded, sizeof(decoded));
    t.assert_eq(result, 0);
    return true;
}

bool test_transport_capabilities_sysex(TestContext& t) {
    t.assert_eq(SYSEX_CAPABILITIES.supports_8bit, false);
    t.assert_eq(SYSEX_CAPABILITIES.requires_encoding, true);
    t.assert_eq(SYSEX_CAPABILITIES.max_packet_size, 64u);
    return true;
}

bool test_transport_capabilities_bulk(TestContext& t) {
    t.assert_eq(BULK_CAPABILITIES.supports_8bit, true);
    t.assert_eq(BULK_CAPABILITIES.requires_encoding, false);
    t.assert_eq(BULK_CAPABILITIES.max_packet_size, 512u);
    t.assert_gt(BULK_CAPABILITIES.max_message_size, SYSEX_CAPABILITIES.max_message_size);
    return true;
}

bool test_transport_manager_basic(TestContext& t) {
    TransportManager<2> manager;
    t.assert_eq(manager.count(), 0u);
    t.assert_true(!manager.any_connected());
    t.assert_true(manager.get_best() == nullptr);
    return true;
}

// =============================================================================
// State Synchronization Tests
// =============================================================================

bool test_state_report_size(TestContext& t) {
    t.assert_eq(sizeof(StateReport), 16);
    return true;
}

bool test_resume_info_size(TestContext& t) {
    t.assert_eq(sizeof(ResumeInfo), 20);
    return true;
}

bool test_boot_verification_size(TestContext& t) {
    t.assert_eq(sizeof(BootVerification), 16);
    return true;
}

bool test_state_manager_init(TestContext& t) {
    StateManager mgr;
    mgr.init();

    t.assert_eq(mgr.state(), DeviceState::IDLE);
    t.assert_eq(mgr.session_id(), 0u);
    t.assert_eq(mgr.received_bytes(), 0u);
    t.assert_eq(mgr.total_bytes(), 0u);
    return true;
}

bool test_state_manager_start_session(TestContext& t) {
    StateManager mgr;
    mgr.init();

    uint32_t session_id = mgr.start_session(1000);
    t.assert_true(session_id > 0);
    t.assert_eq(mgr.state(), DeviceState::UPDATE_STARTING);
    t.assert_eq(mgr.total_bytes(), 1000u);
    t.assert_eq(mgr.session_id(), session_id);
    return true;
}

bool test_state_manager_progress(TestContext& t) {
    StateManager mgr;
    mgr.init();

    mgr.start_session(1000);
    mgr.set_state(DeviceState::UPDATE_RECEIVING);

    mgr.record_received(100, 1);
    auto report = mgr.build_report();
    t.assert_eq(report.progress_percent, 10);
    t.assert_eq(report.last_ack_seq, 1);
    t.assert_eq(report.received_bytes, 100u);

    mgr.record_received(400, 2);
    report = mgr.build_report();
    t.assert_eq(report.progress_percent, 50);
    t.assert_eq(report.received_bytes, 500u);
    return true;
}

bool test_state_manager_flags(TestContext& t) {
    StateManager mgr;
    mgr.init();

    mgr.set_flag(StateReport::FLAG_AUTHENTICATED);
    auto report = mgr.build_report();
    t.assert_true(report.is_authenticated());
    t.assert_true(!report.is_resumable());

    mgr.set_flag(StateReport::FLAG_RESUMABLE);
    report = mgr.build_report();
    t.assert_true(report.is_authenticated());
    t.assert_true(report.is_resumable());

    mgr.clear_flag(StateReport::FLAG_AUTHENTICATED);
    report = mgr.build_report();
    t.assert_true(!report.is_authenticated());
    t.assert_true(report.is_resumable());
    return true;
}

bool test_state_manager_error(TestContext& t) {
    StateManager mgr;
    mgr.init();

    mgr.set_error(0x05);
    t.assert_eq(mgr.state(), DeviceState::ERROR);

    auto report = mgr.build_report();
    t.assert_eq(report.last_error, 0x05);

    mgr.clear_error();
    t.assert_eq(mgr.state(), DeviceState::IDLE);
    return true;
}

bool test_state_manager_subscribers(TestContext& t) {
    StateManager mgr;
    mgr.init();

    t.assert_true(!mgr.has_subscribers());

    mgr.add_subscriber();
    t.assert_true(mgr.has_subscribers());

    mgr.add_subscriber();
    mgr.remove_subscriber();
    t.assert_true(mgr.has_subscribers());

    mgr.remove_subscriber();
    t.assert_true(!mgr.has_subscribers());
    return true;
}

bool test_boot_verification_init(TestContext& t) {
    BootVerification boot;
    boot.init(3);

    t.assert_true(boot.is_valid());
    t.assert_eq(boot.magic, BootVerification::MAGIC);
    t.assert_eq(boot.max_attempts, 3);
    t.assert_eq(boot.boot_count, 0);
    t.assert_eq(boot.verified, 1);
    t.assert_true(!boot.should_rollback());
    return true;
}

bool test_boot_verification_increment(TestContext& t) {
    BootVerification boot;
    boot.init(3);

    boot.increment_boot();
    t.assert_eq(boot.boot_count, 1);
    t.assert_eq(boot.verified, 0);
    t.assert_true(!boot.should_rollback());

    boot.increment_boot();
    t.assert_eq(boot.boot_count, 2);
    t.assert_true(!boot.should_rollback());

    boot.increment_boot();
    t.assert_eq(boot.boot_count, 3);
    t.assert_true(boot.should_rollback());
    return true;
}

bool test_boot_verification_success(TestContext& t) {
    BootVerification boot;
    boot.init(3);

    boot.increment_boot();
    boot.increment_boot();
    t.assert_eq(boot.boot_count, 2);

    boot.mark_success(12345);
    t.assert_eq(boot.boot_count, 0);
    t.assert_eq(boot.verified, 1);
    t.assert_eq(boot.last_success_time, 12345u);
    t.assert_true(!boot.should_rollback());
    return true;
}

bool test_boot_verification_checksum(TestContext& t) {
    BootVerification boot;
    boot.init(3);

    boot.update_checksum();
    t.assert_true(boot.verify_checksum());

    boot.boot_count = 5;
    t.assert_true(!boot.verify_checksum());

    boot.update_checksum();
    t.assert_true(boot.verify_checksum());
    return true;
}

bool test_resume_info_can_resume(TestContext& t) {
    ResumeInfo info{
        .session_id = 12345,
        .firmware_hash = 0xDEADBEEF,
        .received_bytes = 500,
        .total_bytes = 1000,
        .last_ack_seq = 5,
        .chunk_size = 64,
        .reserved = 0,
    };

    t.assert_true(info.can_resume(12345, 0xDEADBEEF));
    t.assert_true(!info.can_resume(12345, 0x12345678));  // Different hash
    t.assert_true(!info.can_resume(99999, 0xDEADBEEF));  // Different session
    t.assert_eq(info.next_offset(), 500u);
    return true;
}

// =============================================================================
// Object Transfer Protocol Tests
// =============================================================================

bool test_object_header_size(TestContext& t) {
    t.assert_eq(sizeof(ObjectHeader), 80);
    return true;
}

bool test_sequence_metadata_size(TestContext& t) {
    t.assert_eq(sizeof(SequenceMetadata), 16);
    return true;
}

bool test_sample_metadata_size(TestContext& t) {
    t.assert_eq(sizeof(SampleMetadata), 16);
    return true;
}

bool test_preset_metadata_size(TestContext& t) {
    t.assert_eq(sizeof(PresetMetadata), 16);
    return true;
}

bool test_config_metadata_size(TestContext& t) {
    t.assert_eq(sizeof(ConfigMetadata), 16);
    return true;
}

bool test_object_header_init(TestContext& t) {
    ObjectHeader header;
    header.init(ObjectType::SEQUENCE, 42, "Test Sequence", 1024);

    t.assert_true(header.is_valid());
    t.assert_eq(header.magic, OBJECT_MAGIC);
    t.assert_eq(header.header_version, 1);
    t.assert_eq(header.type(), ObjectType::SEQUENCE);
    t.assert_eq(header.object_id, 42u);
    t.assert_eq(header.data_size, 1024u);
    t.assert_true(strcmp(header.get_name(), "Test Sequence") == 0);
    return true;
}

bool test_object_header_name_truncation(TestContext& t) {
    ObjectHeader header;
    header.init(ObjectType::SAMPLE, 1,
                "This name is way too long and should be truncated", 512);

    // Name should be null-terminated within 32 bytes
    t.assert_le(strlen(header.get_name()), 31);
    return true;
}

bool test_object_header_flags(TestContext& t) {
    ObjectHeader header;
    header.init(ObjectType::SAMPLE, 1, "Sample", 4096);

    header.set_flags(ObjectFlags::COMPRESSED | ObjectFlags::READONLY);
    auto flags = header.get_flags();

    t.assert_true(flags & ObjectFlags::COMPRESSED);
    t.assert_true(flags & ObjectFlags::READONLY);
    t.assert_true(!(flags & ObjectFlags::ENCRYPTED));
    t.assert_true(!(flags & ObjectFlags::SIGNED));
    return true;
}

bool test_sequence_metadata_bpm(TestContext& t) {
    SequenceMetadata meta{};

    meta.set_bpm(120.0f);
    t.assert_eq(meta.bpm_x10, 1200);
    t.assert_near(meta.get_bpm(), 120.0f, 0.1f);

    meta.set_bpm(145.5f);
    t.assert_eq(meta.bpm_x10, 1455);
    t.assert_near(meta.get_bpm(), 145.5f, 0.1f);
    return true;
}

bool test_sample_metadata_root_note(TestContext& t) {
    SampleMetadata meta{};

    meta.set_root_note(60, 0);  // Middle C, no detuning
    t.assert_eq(meta.get_root_note(), 60);
    t.assert_eq(meta.get_fine_tune(), 0);

    meta.set_root_note(69, 50);  // A4 + 50 cents
    t.assert_eq(meta.get_root_note(), 69);
    t.assert_eq(meta.get_fine_tune(), 50);
    return true;
}

bool test_ram_storage_init(TestContext& t) {
    RAMObjectStorage<8, 4096> storage;

    auto info = storage.get_storage_info();
    t.assert_eq(info.object_count, 0u);
    t.assert_eq(info.max_objects, 8u);
    t.assert_eq(info.total_space, 4096u);
    t.assert_eq(info.free_space, 4096u);
    return true;
}

bool test_ram_storage_write_read(TestContext& t) {
    RAMObjectStorage<8, 4096> storage;

    // Create object
    ObjectHeader header;
    header.init(ObjectType::PRESET, storage.generate_id(), "Test Preset", 64);

    t.assert_true(storage.write_begin(header));

    uint8_t data[64];
    for (int i = 0; i < 64; ++i) data[i] = static_cast<uint8_t>(i);
    t.assert_true(storage.write_data(header.object_id, 0, data, 64));
    t.assert_true(storage.write_commit(header.object_id));

    // Verify stored
    auto info = storage.get_storage_info();
    t.assert_eq(info.object_count, 1u);

    // Read back header
    ObjectHeader read_header;
    t.assert_true(storage.get_header(header.object_id, read_header));
    t.assert_true(strcmp(read_header.get_name(), "Test Preset") == 0);

    // Read back data
    uint8_t read_data[64];
    size_t read_len = storage.read_data(header.object_id, 0, read_data, 64);
    t.assert_eq(read_len, 64u);
    t.assert_eq(read_data[0], 0);
    t.assert_eq(read_data[63], 63);
    return true;
}

bool test_ram_storage_partial_read(TestContext& t) {
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
    t.assert_eq(len, 20u);
    t.assert_eq(partial[0], 50);
    t.assert_eq(partial[19], 69);
    return true;
}

bool test_ram_storage_delete(TestContext& t) {
    RAMObjectStorage<8, 4096> storage;

    ObjectHeader header;
    header.init(ObjectType::CONFIG, storage.generate_id(), "Config", 16);
    storage.write_begin(header);
    uint8_t data[16] = {0};
    storage.write_data(header.object_id, 0, data, 16);
    storage.write_commit(header.object_id);

    t.assert_eq(storage.get_storage_info().object_count, 1u);

    t.assert_true(storage.remove(header.object_id));
    t.assert_eq(storage.get_storage_info().object_count, 0u);

    // Can't get deleted header
    ObjectHeader deleted_header;
    t.assert_true(!storage.get_header(header.object_id, deleted_header));
    return true;
}

bool test_ram_storage_rename(TestContext& t) {
    RAMObjectStorage<8, 4096> storage;

    ObjectHeader header;
    header.init(ObjectType::SEQUENCE, storage.generate_id(), "Original", 32);
    storage.write_begin(header);
    uint8_t data[32] = {0};
    storage.write_data(header.object_id, 0, data, 32);
    storage.write_commit(header.object_id);

    t.assert_true(storage.rename(header.object_id, "Renamed"));

    ObjectHeader read_header;
    storage.get_header(header.object_id, read_header);
    t.assert_true(strcmp(read_header.get_name(), "Renamed") == 0);
    return true;
}

bool test_ram_storage_list(TestContext& t) {
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
    t.assert_eq(count, 3u);
    return true;
}

bool test_ram_storage_abort(TestContext& t) {
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
    t.assert_eq(storage.get_storage_info().object_count, 0u);
    return true;
}

bool test_ram_storage_full(TestContext& t) {
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
    t.assert_true(!storage.write_begin(header3));
    return true;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    Suite s("umidi_extended");

    s.section("Bulk Transport");
    s.run("bulk_crc16_basic", test_bulk_crc16_basic);
    s.run("bulk_crc16_different_data", test_bulk_crc16_different_data);
    s.run("bulk_encode_decode", test_bulk_encode_decode);
    s.run("bulk_invalid_frame_too_short", test_bulk_invalid_frame_too_short);
    s.run("bulk_invalid_frame_length_mismatch", test_bulk_invalid_frame_length_mismatch);
    s.run("bulk_invalid_frame_crc_mismatch", test_bulk_invalid_frame_crc_mismatch);

    s.section("Transport Capabilities");
    s.run("transport_capabilities_sysex", test_transport_capabilities_sysex);
    s.run("transport_capabilities_bulk", test_transport_capabilities_bulk);
    s.run("transport_manager_basic", test_transport_manager_basic);

    s.section("State Report");
    s.run("state_report_size", test_state_report_size);
    s.run("resume_info_size", test_resume_info_size);
    s.run("boot_verification_size", test_boot_verification_size);

    s.section("State Manager");
    s.run("state_manager_init", test_state_manager_init);
    s.run("state_manager_start_session", test_state_manager_start_session);
    s.run("state_manager_progress", test_state_manager_progress);
    s.run("state_manager_flags", test_state_manager_flags);
    s.run("state_manager_error", test_state_manager_error);
    s.run("state_manager_subscribers", test_state_manager_subscribers);

    s.section("Boot Verification");
    s.run("boot_verification_init", test_boot_verification_init);
    s.run("boot_verification_increment", test_boot_verification_increment);
    s.run("boot_verification_success", test_boot_verification_success);
    s.run("boot_verification_checksum", test_boot_verification_checksum);

    s.section("Resume Info");
    s.run("resume_info_can_resume", test_resume_info_can_resume);

    s.section("Object Header");
    s.run("object_header_size", test_object_header_size);
    s.run("object_header_init", test_object_header_init);
    s.run("object_header_name_truncation", test_object_header_name_truncation);
    s.run("object_header_flags", test_object_header_flags);

    s.section("Type-Specific Metadata");
    s.run("sequence_metadata_size", test_sequence_metadata_size);
    s.run("sample_metadata_size", test_sample_metadata_size);
    s.run("preset_metadata_size", test_preset_metadata_size);
    s.run("config_metadata_size", test_config_metadata_size);
    s.run("sequence_metadata_bpm", test_sequence_metadata_bpm);
    s.run("sample_metadata_root_note", test_sample_metadata_root_note);

    s.section("RAM Object Storage");
    s.run("ram_storage_init", test_ram_storage_init);
    s.run("ram_storage_write_read", test_ram_storage_write_read);
    s.run("ram_storage_partial_read", test_ram_storage_partial_read);
    s.run("ram_storage_delete", test_ram_storage_delete);
    s.run("ram_storage_rename", test_ram_storage_rename);
    s.run("ram_storage_list", test_ram_storage_list);
    s.run("ram_storage_abort", test_ram_storage_abort);
    s.run("ram_storage_full", test_ram_storage_full);

    return s.summary();
}
