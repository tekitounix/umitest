// SPDX-License-Identifier: MIT
// umi_boot Firmware Validation Tests

#include "test_framework.hh"
#include <umiboot/firmware.hh>

using namespace umiboot;
using namespace umiboot::test;

// =============================================================================
// Firmware Header Tests
// =============================================================================

TEST(firmware_header_size) {
    ASSERT_EQ(sizeof(FirmwareHeader), 128u);
    TEST_PASS();
}

TEST(firmware_magic) {
    ASSERT_EQ(FIRMWARE_MAGIC, 0x554D4946u);  // "UMIF"
    TEST_PASS();
}

TEST(firmware_header_builder) {
    auto header = FirmwareHeaderBuilder()
        .version(1, 2, 3)
        .build_number(100)
        .image_size(1024)
        .crc32(0xDEADBEEF)
        .load_address(0x08000000)
        .entry_point(0x08000100)
        .board("STM32F411")
        .build();

    ASSERT_EQ(header.magic, FIRMWARE_MAGIC);
    ASSERT_EQ(header.header_version, FIRMWARE_HEADER_VERSION);
    ASSERT_EQ(header.fw_version_major, 1u);
    ASSERT_EQ(header.fw_version_minor, 2u);
    ASSERT_EQ(header.fw_version_patch, 3u);
    ASSERT_EQ(header.fw_build_number, 100u);
    ASSERT_EQ(header.image_size, 1024u);
    ASSERT_EQ(header.image_crc32, 0xDEADBEEFu);
    ASSERT_EQ(header.load_address, 0x08000000u);
    ASSERT_EQ(header.entry_point, 0x08000100u);
    ASSERT_EQ(std::strcmp(reinterpret_cast<const char*>(header.target_board), "STM32F411"), 0);
    TEST_PASS();
}

// =============================================================================
// Version Utilities Tests
// =============================================================================

TEST(version_compare_equal) {
    auto result = compare_versions(1, 2, 3, 1, 2, 3);
    ASSERT_EQ(result, VersionCompare::EQUAL);
    TEST_PASS();
}

TEST(version_compare_newer_major) {
    auto result = compare_versions(2, 0, 0, 1, 9, 9);
    ASSERT_EQ(result, VersionCompare::NEWER);
    TEST_PASS();
}

TEST(version_compare_newer_minor) {
    auto result = compare_versions(1, 3, 0, 1, 2, 9);
    ASSERT_EQ(result, VersionCompare::NEWER);
    TEST_PASS();
}

TEST(version_compare_newer_patch) {
    auto result = compare_versions(1, 2, 4, 1, 2, 3);
    ASSERT_EQ(result, VersionCompare::NEWER);
    TEST_PASS();
}

TEST(version_compare_older) {
    auto result = compare_versions(1, 0, 0, 2, 0, 0);
    ASSERT_EQ(result, VersionCompare::OLDER);
    TEST_PASS();
}

TEST(pack_version) {
    uint32_t packed = pack_version(1, 2, 3);
    ASSERT_EQ(packed, 0x01020300u);
    TEST_PASS();
}

TEST(unpack_version) {
    uint32_t packed = 0x01020300;
    ASSERT_EQ(version_major(packed), 1);
    ASSERT_EQ(version_minor(packed), 2);
    ASSERT_EQ(version_patch(packed), 3);
    TEST_PASS();
}

// =============================================================================
// CRC-32 Tests
// =============================================================================

TEST(crc32_empty) {
    uint8_t data[1] = {0};
    uint32_t result = crc32(data, 0);
    ASSERT_EQ(result, 0x00000000u);
    TEST_PASS();
}

TEST(crc32_known_value) {
    // "123456789" -> 0xCBF43926 (IEEE 802.3)
    const uint8_t data[] = "123456789";
    uint32_t result = crc32(data, 9);
    ASSERT_EQ(result, 0xCBF43926u);
    TEST_PASS();
}

TEST(crc32_single_byte) {
    uint8_t data[] = {0x00};
    uint32_t result = crc32(data, 1);
    ASSERT_EQ(result, 0xD202EF8Du);
    TEST_PASS();
}

// =============================================================================
// Firmware Validator Tests
// =============================================================================

TEST(validator_valid_header) {
    FirmwareValidator<> validator;
    validator.set_bootloader_version(1);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .build();

    auto result = validator.validate_header(header);
    ASSERT_EQ(result, ValidationResult::OK);
    TEST_PASS();
}

TEST(validator_invalid_magic) {
    FirmwareValidator<> validator;

    FirmwareHeader header{};
    header.magic = 0x12345678;  // Wrong magic
    header.header_version = FIRMWARE_HEADER_VERSION;

    auto result = validator.validate_header(header);
    ASSERT_EQ(result, ValidationResult::INVALID_MAGIC);
    TEST_PASS();
}

TEST(validator_invalid_header_version) {
    FirmwareValidator<> validator;

    FirmwareHeader header{};
    header.magic = FIRMWARE_MAGIC;
    header.header_version = 99;  // Wrong version

    auto result = validator.validate_header(header);
    ASSERT_EQ(result, ValidationResult::INVALID_HEADER_VERSION);
    TEST_PASS();
}

TEST(validator_board_mismatch) {
    FirmwareValidator<> validator;
    validator.set_board_id("STM32F411");

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .board("ESP32")
        .build();

    auto result = validator.validate_header(header);
    ASSERT_EQ(result, ValidationResult::BOARD_MISMATCH);
    TEST_PASS();
}

TEST(validator_bootloader_too_old) {
    FirmwareValidator<> validator;
    validator.set_bootloader_version(1);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .min_bootloader(2)  // Requires bootloader v2
        .build();

    auto result = validator.validate_header(header);
    ASSERT_EQ(result, ValidationResult::BOOTLOADER_TOO_OLD);
    TEST_PASS();
}

TEST(validator_rollback_check) {
    FirmwareValidator<> validator;
    validator.set_rollback_version(5);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .rollback_version(3)  // Too old
        .build();

    auto result = validator.validate_header(header);
    ASSERT_EQ(result, ValidationResult::VERSION_TOO_OLD);
    TEST_PASS();
}

TEST(validator_signature_required) {
    FirmwareValidator<> validator;
    validator.require_signature(true);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .build();  // Not signed

    auto result = validator.validate_header(header);
    ASSERT_EQ(result, ValidationResult::NOT_SIGNED);
    TEST_PASS();
}

TEST(validator_signature_present) {
    FirmwareValidator<> validator;
    validator.require_signature(true);

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .signed_firmware()
        .build();

    auto result = validator.validate_header(header);
    ASSERT_EQ(result, ValidationResult::OK);
    TEST_PASS();
}

TEST(validator_data_crc) {
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
    ASSERT_EQ(result, ValidationResult::OK);
    TEST_PASS();
}

TEST(validator_data_crc_mismatch) {
    FirmwareValidator<> validator;
    validator.set_crc(crc32_fn);

    uint8_t data[] = {1, 2, 3, 4, 5};

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .image_size(5)
        .crc32(0xDEADBEEF)  // Wrong CRC
        .build();

    auto result = validator.validate_data(header, data, 5);
    ASSERT_EQ(result, ValidationResult::CRC_MISMATCH);
    TEST_PASS();
}

TEST(validator_size_mismatch) {
    FirmwareValidator<> validator;

    uint8_t data[10] = {0};

    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .image_size(5)  // Says 5 bytes
        .build();

    auto result = validator.validate_data(header, data, 10);  // But got 10
    ASSERT_EQ(result, ValidationResult::INVALID_SIZE);
    TEST_PASS();
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("umi_boot Firmware Validation Tests\n");
    printf("===================================\n");

    SECTION("Firmware Header");
    RUN_TEST(firmware_header_size);
    RUN_TEST(firmware_magic);
    RUN_TEST(firmware_header_builder);

    SECTION("Version Utilities");
    RUN_TEST(version_compare_equal);
    RUN_TEST(version_compare_newer_major);
    RUN_TEST(version_compare_newer_minor);
    RUN_TEST(version_compare_newer_patch);
    RUN_TEST(version_compare_older);
    RUN_TEST(pack_version);
    RUN_TEST(unpack_version);

    SECTION("CRC-32");
    RUN_TEST(crc32_empty);
    RUN_TEST(crc32_known_value);
    RUN_TEST(crc32_single_byte);

    SECTION("Firmware Validator");
    RUN_TEST(validator_valid_header);
    RUN_TEST(validator_invalid_magic);
    RUN_TEST(validator_invalid_header_version);
    RUN_TEST(validator_board_mismatch);
    RUN_TEST(validator_bootloader_too_old);
    RUN_TEST(validator_rollback_check);
    RUN_TEST(validator_signature_required);
    RUN_TEST(validator_signature_present);
    RUN_TEST(validator_data_crc);
    RUN_TEST(validator_data_crc_mismatch);
    RUN_TEST(validator_size_mismatch);

    return summary();
}
