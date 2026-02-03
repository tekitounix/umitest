// SPDX-License-Identifier: MIT
// umi_boot Firmware Validation Tests

#include <umitest.hh>
#include <umiboot/firmware.hh>

using namespace umiboot;
using namespace umitest;

// =============================================================================
// Firmware Header Tests
// =============================================================================

bool test_firmware_header_size(TestContext& t) {
    t.assert_eq(sizeof(FirmwareHeader), 128u);
    return true;
}

bool test_firmware_magic(TestContext& t) {
    t.assert_eq(FIRMWARE_MAGIC, 0x554D4946u);  // "UMIF"
    return true;
}

bool test_firmware_header_builder(TestContext& t) {
    auto header = FirmwareHeaderBuilder()
        .version(1, 2, 3)
        .build_number(100)
        .image_size(1024)
        .crc32(0xDEADBEEF)
        .load_address(0x08000000)
        .entry_point(0x08000100)
        .board("STM32F411")
        .build();

    t.assert_eq(header.magic, FIRMWARE_MAGIC);
    t.assert_eq(header.header_version, FIRMWARE_HEADER_VERSION);
    t.assert_eq(header.fw_version_major, 1u);
    t.assert_eq(header.fw_version_minor, 2u);
    t.assert_eq(header.fw_version_patch, 3u);
    t.assert_eq(header.fw_build_number, 100u);
    t.assert_eq(header.image_size, 1024u);
    t.assert_eq(header.image_crc32, 0xDEADBEEFu);
    t.assert_eq(header.load_address, 0x08000000u);
    t.assert_eq(header.entry_point, 0x08000100u);
    t.assert_eq(std::strcmp(reinterpret_cast<const char*>(header.target_board), "STM32F411"), 0);
    return true;
}

// =============================================================================
// Version Utilities Tests
// =============================================================================

bool test_version_compare_equal(TestContext& t) {
    auto result = compare_versions(1, 2, 3, 1, 2, 3);
    t.assert_eq(result, VersionCompare::EQUAL);
    return true;
}

bool test_version_compare_newer_major(TestContext& t) {
    auto result = compare_versions(2, 0, 0, 1, 9, 9);
    t.assert_eq(result, VersionCompare::NEWER);
    return true;
}

bool test_version_compare_newer_minor(TestContext& t) {
    auto result = compare_versions(1, 3, 0, 1, 2, 9);
    t.assert_eq(result, VersionCompare::NEWER);
    return true;
}

bool test_version_compare_newer_patch(TestContext& t) {
    auto result = compare_versions(1, 2, 4, 1, 2, 3);
    t.assert_eq(result, VersionCompare::NEWER);
    return true;
}

bool test_version_compare_older(TestContext& t) {
    auto result = compare_versions(1, 0, 0, 2, 0, 0);
    t.assert_eq(result, VersionCompare::OLDER);
    return true;
}

bool test_pack_version(TestContext& t) {
    uint32_t packed = pack_version(1, 2, 3);
    t.assert_eq(packed, 0x01020300u);
    return true;
}

bool test_unpack_version(TestContext& t) {
    uint32_t packed = 0x01020300;
    t.assert_eq(version_major(packed), 1);
    t.assert_eq(version_minor(packed), 2);
    t.assert_eq(version_patch(packed), 3);
    return true;
}

// =============================================================================
// CRC-32 Tests
// =============================================================================

bool test_crc32_empty(TestContext& t) {
    uint8_t data[1] = {0};
    uint32_t result = crc32(data, 0);
    t.assert_eq(result, 0x00000000u);
    return true;
}

bool test_crc32_known_value(TestContext& t) {
    // "123456789" -> 0xCBF43926 (IEEE 802.3)
    const uint8_t data[] = "123456789";
    uint32_t result = crc32(data, 9);
    t.assert_eq(result, 0xCBF43926u);
    return true;
}

bool test_crc32_single_byte(TestContext& t) {
    uint8_t data[] = {0x00};
    uint32_t result = crc32(data, 1);
    t.assert_eq(result, 0xD202EF8Du);
    return true;
}

// =============================================================================
// Firmware Validator Tests
// =============================================================================

bool test_validator_valid_header(TestContext& t) {
    FirmwareValidator<> validator;
    validator.set_bootloader_version(1);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .build();

    auto result = validator.validate_header(header);
    t.assert_eq(result, ValidationResult::OK);
    return true;
}

bool test_validator_invalid_magic(TestContext& t) {
    FirmwareValidator<> validator;

    FirmwareHeader header{};
    header.magic = 0x12345678;  // Wrong magic
    header.header_version = FIRMWARE_HEADER_VERSION;

    auto result = validator.validate_header(header);
    t.assert_eq(result, ValidationResult::INVALID_MAGIC);
    return true;
}

bool test_validator_invalid_header_version(TestContext& t) {
    FirmwareValidator<> validator;

    FirmwareHeader header{};
    header.magic = FIRMWARE_MAGIC;
    header.header_version = 99;  // Wrong version

    auto result = validator.validate_header(header);
    t.assert_eq(result, ValidationResult::INVALID_HEADER_VERSION);
    return true;
}

bool test_validator_board_mismatch(TestContext& t) {
    FirmwareValidator<> validator;
    validator.set_board_id("STM32F411");

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .board("ESP32")
        .build();

    auto result = validator.validate_header(header);
    t.assert_eq(result, ValidationResult::BOARD_MISMATCH);
    return true;
}

bool test_validator_bootloader_too_old(TestContext& t) {
    FirmwareValidator<> validator;
    validator.set_bootloader_version(1);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .min_bootloader(2)  // Requires bootloader v2
        .build();

    auto result = validator.validate_header(header);
    t.assert_eq(result, ValidationResult::BOOTLOADER_TOO_OLD);
    return true;
}

bool test_validator_rollback_check(TestContext& t) {
    FirmwareValidator<> validator;
    validator.set_rollback_version(5);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .rollback_version(3)  // Too old
        .build();

    auto result = validator.validate_header(header);
    t.assert_eq(result, ValidationResult::VERSION_TOO_OLD);
    return true;
}

bool test_validator_signature_required(TestContext& t) {
    FirmwareValidator<> validator;
    validator.require_signature(true);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .build();  // Not signed

    auto result = validator.validate_header(header);
    t.assert_eq(result, ValidationResult::NOT_SIGNED);
    return true;
}

bool test_validator_signature_present(TestContext& t) {
    FirmwareValidator<> validator;
    validator.require_signature(true);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .signed_firmware()
        .build();

    auto result = validator.validate_header(header);
    t.assert_eq(result, ValidationResult::OK);
    return true;
}

bool test_validator_data_crc(TestContext& t) {
    FirmwareValidator<> validator;
    validator.set_crc(crc32_fn);

    uint8_t data[] = {1, 2, 3, 4, 5};
    uint32_t expected_crc = crc32(data, 5);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .image_size(5)
        .crc32(expected_crc)
        .build();

    auto result = validator.validate_data(header, data, 5);
    t.assert_eq(result, ValidationResult::OK);
    return true;
}

bool test_validator_data_crc_mismatch(TestContext& t) {
    FirmwareValidator<> validator;
    validator.set_crc(crc32_fn);

    uint8_t data[] = {1, 2, 3, 4, 5};

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .image_size(5)
        .crc32(0xDEADBEEF)  // Wrong CRC
        .build();

    auto result = validator.validate_data(header, data, 5);
    t.assert_eq(result, ValidationResult::CRC_MISMATCH);
    return true;
}

bool test_validator_size_mismatch(TestContext& t) {
    FirmwareValidator<> validator;

    uint8_t data[10] = {0};

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .image_size(5)  // Says 5 bytes
        .build();

    auto result = validator.validate_data(header, data, 10);  // But got 10
    t.assert_eq(result, ValidationResult::INVALID_SIZE);
    return true;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    Suite s("umiboot_firmware");

    s.section("Firmware Header");
    s.run("firmware_header_size", test_firmware_header_size);
    s.run("firmware_magic", test_firmware_magic);
    s.run("firmware_header_builder", test_firmware_header_builder);

    s.section("Version Utilities");
    s.run("version_compare_equal", test_version_compare_equal);
    s.run("version_compare_newer_major", test_version_compare_newer_major);
    s.run("version_compare_newer_minor", test_version_compare_newer_minor);
    s.run("version_compare_newer_patch", test_version_compare_newer_patch);
    s.run("version_compare_older", test_version_compare_older);
    s.run("pack_version", test_pack_version);
    s.run("unpack_version", test_unpack_version);

    s.section("CRC-32");
    s.run("crc32_empty", test_crc32_empty);
    s.run("crc32_known_value", test_crc32_known_value);
    s.run("crc32_single_byte", test_crc32_single_byte);

    s.section("Firmware Validator");
    s.run("validator_valid_header", test_validator_valid_header);
    s.run("validator_invalid_magic", test_validator_invalid_magic);
    s.run("validator_invalid_header_version", test_validator_invalid_header_version);
    s.run("validator_board_mismatch", test_validator_board_mismatch);
    s.run("validator_bootloader_too_old", test_validator_bootloader_too_old);
    s.run("validator_rollback_check", test_validator_rollback_check);
    s.run("validator_signature_required", test_validator_signature_required);
    s.run("validator_signature_present", test_validator_signature_present);
    s.run("validator_data_crc", test_validator_data_crc);
    s.run("validator_data_crc_mismatch", test_validator_data_crc_mismatch);
    s.run("validator_size_mismatch", test_validator_size_mismatch);

    return s.summary();
}
