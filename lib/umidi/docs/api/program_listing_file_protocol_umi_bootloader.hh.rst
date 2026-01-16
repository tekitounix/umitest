
.. _program_listing_file_protocol_umi_bootloader.hh:

Program Listing for File umi_bootloader.hh
==========================================

|exhale_lsh| :ref:`Return to documentation for file <file_protocol_umi_bootloader.hh>` (``protocol/umi_bootloader.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   // UMI-OS Bootloader Interface
   // A/B partition support with platform-specific flash layouts
   #pragma once
   
   #include "umi_firmware.hh"
   #include <cstdint>
   #include <cstring>
   
   namespace umidi::protocol {
   
   // =============================================================================
   // A/B Partition Scheme
   // =============================================================================
   //
   // Flash Layout:
   //
   // +------------------+ 0x00000000
   // | Bootloader       | (fixed, not updated via this protocol)
   // +------------------+ BOOTLOADER_END
   // | BootConfig       | (boot configuration, 2 sectors for redundancy)
   // +------------------+ CONFIG_END
   // | Slot A (Primary) | (firmware slot A)
   // +------------------+ SLOT_A_END
   // | Slot B (Backup)  | (firmware slot B)
   // +------------------+ SLOT_B_END
   // | User Data        | (application data, optional)
   // +------------------+
   //
   // Boot Process:
   // 1. Bootloader reads BootConfig
   // 2. Check boot_count < MAX_BOOT_ATTEMPTS
   // 3. If exceeded, switch to other slot (auto-rollback)
   // 4. Increment boot_count
   // 5. Jump to active slot
   // 6. Application calls mark_boot_successful() to reset boot_count
   //
   // =============================================================================
   
   enum class Slot : uint8_t {
       SLOT_A = 0,
       SLOT_B = 1,
   };
   
   enum class SlotState : uint8_t {
       EMPTY       = 0x00,     // No firmware
       VALID       = 0x01,     // Valid firmware
       PENDING     = 0x02,     // Update pending (not yet verified)
       INVALID     = 0x03,     // Validation failed
       BOOTING     = 0x04,     // Currently booting (boot_count > 0)
   };
   
   struct BootConfig {
       uint32_t magic;             // 0x00: Magic number for validation
       uint8_t  config_version;    // 0x04: Config structure version
       uint8_t  active_slot;       // 0x05: Currently active slot (0 or 1)
       uint8_t  boot_count;        // 0x06: Boot attempt counter
       uint8_t  max_boot_attempts; // 0x07: Max attempts before rollback
   
       SlotState slot_a_state;     // 0x08: Slot A state
       SlotState slot_b_state;     // 0x09: Slot B state
       uint8_t  reserved1[2];      // 0x0A: Alignment
   
       uint32_t slot_a_version;    // 0x0C: Slot A firmware version (packed)
       uint32_t slot_b_version;    // 0x10: Slot B firmware version (packed)
   
       uint32_t slot_a_crc;        // 0x14: Slot A firmware CRC
       uint32_t slot_b_crc;        // 0x18: Slot B firmware CRC
   
       uint32_t last_update_time;  // 0x1C: Timestamp of last update
       uint32_t boot_success_count;// 0x20: Successful boots since last update
   
       uint32_t reserved2[6];      // 0x24: Reserved for future use (6 * 4 = 24)
       uint32_t checksum;          // 0x3C: Simple checksum of this struct
   
       // Total: 64 bytes (0x40)
   };
   
   static_assert(sizeof(BootConfig) == 64, "BootConfig must be 64 bytes");
   
   inline constexpr uint32_t BOOT_CONFIG_MAGIC = 0x554D4942;  // "UMIB"
   
   inline constexpr uint8_t DEFAULT_MAX_BOOT_ATTEMPTS = 3;
   
   // =============================================================================
   // Platform Configuration
   // =============================================================================
   
   struct FlashRegion {
       uint32_t base_address;      // Start address
       uint32_t size;              // Region size
       uint32_t sector_size;       // Minimum erase unit
       uint32_t write_size;        // Minimum write unit (alignment)
   };
   
   struct PlatformConfig {
       const char* name;           // Platform name
       FlashRegion bootloader;     // Bootloader region (read-only)
       FlashRegion boot_config;    // Boot configuration
       FlashRegion slot_a;         // Firmware slot A
       FlashRegion slot_b;         // Firmware slot B
       uint32_t ram_buffer_addr;   // RAM buffer for staging (0 if not used)
       uint32_t ram_buffer_size;   // RAM buffer size
   };
   
   // =============================================================================
   // Pre-defined Platform Configurations
   // =============================================================================
   
   namespace platforms {
   
   inline constexpr PlatformConfig STM32F4_512K = {
       .name = "STM32F4-512K",
       .bootloader = {
           .base_address = 0x08000000,
           .size = 0x10000,            // 64KB (sectors 0-3 + part of 4)
           .sector_size = 0x4000,      // 16KB (smallest)
           .write_size = 4,
       },
       .boot_config = {
           .base_address = 0x08010000,
           .size = 0x10000,            // 64KB (sector 4)
           .sector_size = 0x10000,
           .write_size = 4,
       },
       .slot_a = {
           .base_address = 0x08020000,
           .size = 0x30000,            // 192KB (sectors 5-6)
           .sector_size = 0x20000,     // 128KB
           .write_size = 4,
       },
       .slot_b = {
           .base_address = 0x08050000,
           .size = 0x30000,            // 192KB (sector 7 + remaining)
           .sector_size = 0x20000,
           .write_size = 4,
       },
       .ram_buffer_addr = 0x20000000,
       .ram_buffer_size = 0x8000,      // 32KB staging buffer
   };
   
   inline constexpr PlatformConfig STM32H7_2M = {
       .name = "STM32H7-2M",
       .bootloader = {
           .base_address = 0x08000000,
           .size = 0x40000,            // 256KB (sectors 0-1)
           .sector_size = 0x20000,     // 128KB
           .write_size = 32,           // 256-bit writes
       },
       .boot_config = {
           .base_address = 0x08040000,
           .size = 0x20000,            // 128KB (sector 2)
           .sector_size = 0x20000,
           .write_size = 32,
       },
       .slot_a = {
           .base_address = 0x08060000,
           .size = 0xC0000,            // 768KB (sectors 3-8)
           .sector_size = 0x20000,
           .write_size = 32,
       },
       .slot_b = {
           .base_address = 0x08120000,
           .size = 0xC0000,            // 768KB (sectors 9-14)
           .sector_size = 0x20000,
           .write_size = 32,
       },
       .ram_buffer_addr = 0x24000000,  // AXI SRAM
       .ram_buffer_size = 0x80000,     // 512KB
   };
   
   inline constexpr PlatformConfig ESP32_4M = {
       .name = "ESP32-4M",
       .bootloader = {
           .base_address = 0x1000,     // Second-stage bootloader
           .size = 0x7000,             // 28KB
           .sector_size = 0x1000,      // 4KB
           .write_size = 4,
       },
       .boot_config = {
           .base_address = 0x9000,     // Partition table + NVS
           .size = 0x7000,             // 28KB
           .sector_size = 0x1000,
           .write_size = 4,
       },
       .slot_a = {
           .base_address = 0x10000,    // ota_0
           .size = 0x1A0000,           // ~1.6MB
           .sector_size = 0x1000,
           .write_size = 4,
       },
       .slot_b = {
           .base_address = 0x1B0000,   // ota_1
           .size = 0x1A0000,           // ~1.6MB
           .sector_size = 0x1000,
           .write_size = 4,
       },
       .ram_buffer_addr = 0,           // Use heap
       .ram_buffer_size = 0x10000,     // 64KB recommended
   };
   
   inline constexpr PlatformConfig RP2040_2M = {
       .name = "RP2040-2M",
       .bootloader = {
           .base_address = 0x10000000,
           .size = 0x10000,            // 64KB
           .sector_size = 0x1000,      // 4KB
           .write_size = 256,          // Page size
       },
       .boot_config = {
           .base_address = 0x10010000,
           .size = 0x2000,             // 8KB
           .sector_size = 0x1000,
           .write_size = 256,
       },
       .slot_a = {
           .base_address = 0x10012000,
           .size = 0xF7000,            // ~988KB
           .sector_size = 0x1000,
           .write_size = 256,
       },
       .slot_b = {
           .base_address = 0x10109000,
           .size = 0xF7000,            // ~988KB
           .sector_size = 0x1000,
           .write_size = 256,
       },
       .ram_buffer_addr = 0x20000000,
       .ram_buffer_size = 0x8000,      // 32KB
   };
   
   } // namespace platforms
   
   // =============================================================================
   // Flash Interface
   // =============================================================================
   
   enum class FlashResult : uint8_t {
       OK,
       ERROR_ERASE,
       ERROR_WRITE,
       ERROR_VERIFY,
       ERROR_LOCKED,
       ERROR_ALIGNMENT,
       ERROR_OUT_OF_RANGE,
   };
   
   struct FlashInterface {
       FlashResult (*erase)(uint32_t address, size_t size);
   
       FlashResult (*write)(uint32_t address, const uint8_t* data, size_t size);
   
       FlashResult (*read)(uint32_t address, uint8_t* data, size_t size);
   
       FlashResult (*verify)(uint32_t address, const uint8_t* data, size_t size);
   
       void (*lock)(void);
       void (*unlock)(void);
   };
   
   // =============================================================================
   // Bootloader Interface
   // =============================================================================
   
   template <const PlatformConfig* Config>
   class BootloaderInterface {
   public:
       void init(const FlashInterface& flash) noexcept {
           flash_ = &flash;
           load_config();
       }
   
       bool load_config() noexcept {
           if (!flash_ || !flash_->read) return false;
   
           auto result = flash_->read(
               Config->boot_config.base_address,
               reinterpret_cast<uint8_t*>(&config_),
               sizeof(BootConfig)
           );
   
           if (result != FlashResult::OK) return false;
   
           // Validate config
           if (config_.magic != BOOT_CONFIG_MAGIC ||
               !verify_config_checksum()) {
               init_default_config();
               return false;
           }
   
           return true;
       }
   
       bool save_config() noexcept {
           if (!flash_ || !flash_->erase || !flash_->write) return false;
   
           // Update checksum
           config_.checksum = calculate_config_checksum();
   
           // Erase config sector
           auto result = flash_->erase(
               Config->boot_config.base_address,
               Config->boot_config.sector_size
           );
           if (result != FlashResult::OK) return false;
   
           // Write config
           result = flash_->write(
               Config->boot_config.base_address,
               reinterpret_cast<const uint8_t*>(&config_),
               sizeof(BootConfig)
           );
   
           return result == FlashResult::OK;
       }
   
       [[nodiscard]] Slot active_slot() const noexcept {
           return static_cast<Slot>(config_.active_slot & 1);
       }
   
       [[nodiscard]] Slot inactive_slot() const noexcept {
           return static_cast<Slot>((config_.active_slot + 1) & 1);
       }
   
       [[nodiscard]] SlotState slot_state(Slot slot) const noexcept {
           return (slot == Slot::SLOT_A) ? config_.slot_a_state : config_.slot_b_state;
       }
   
       [[nodiscard]] const FlashRegion& slot_region(Slot slot) const noexcept {
           return (slot == Slot::SLOT_A) ? Config->slot_a : Config->slot_b;
       }
   
       [[nodiscard]] Slot update_target() const noexcept {
           return inactive_slot();
       }
   
       [[nodiscard]] uint32_t update_target_address() const noexcept {
           return slot_region(update_target()).base_address;
       }
   
       [[nodiscard]] uint32_t max_firmware_size() const noexcept {
           return Config->slot_a.size;  // Both slots same size
       }
   
       FlashResult prepare_slot(Slot slot) noexcept {
           if (!flash_ || !flash_->erase) return FlashResult::ERROR_ERASE;
   
           const auto& region = slot_region(slot);
           auto result = flash_->erase(region.base_address, region.size);
   
           if (result == FlashResult::OK) {
               set_slot_state(slot, SlotState::EMPTY);
           }
   
           return result;
       }
   
       FlashResult write_firmware(Slot slot, uint32_t offset,
                                   const uint8_t* data, size_t size) noexcept {
           if (!flash_ || !flash_->write) return FlashResult::ERROR_WRITE;
   
           const auto& region = slot_region(slot);
   
           // Check bounds
           if (offset + size > region.size) {
               return FlashResult::ERROR_OUT_OF_RANGE;
           }
   
           // Check alignment
           if ((offset % Config->slot_a.write_size) != 0 ||
               (size % Config->slot_a.write_size) != 0) {
               // Handle misalignment - buffer if necessary
               // For simplicity, require aligned writes
               return FlashResult::ERROR_ALIGNMENT;
           }
   
           return flash_->write(region.base_address + offset, data, size);
       }
   
       void mark_slot_valid(Slot slot, uint32_t version, uint32_t crc) noexcept {
           set_slot_state(slot, SlotState::VALID);
           if (slot == Slot::SLOT_A) {
               config_.slot_a_version = version;
               config_.slot_a_crc = crc;
           } else {
               config_.slot_b_version = version;
               config_.slot_b_crc = crc;
           }
       }
   
       void mark_slot_pending(Slot slot) noexcept {
           set_slot_state(slot, SlotState::PENDING);
       }
   
       bool commit_update() noexcept {
           Slot target = inactive_slot();
   
           // Only commit if pending
           if (slot_state(target) != SlotState::PENDING &&
               slot_state(target) != SlotState::VALID) {
               return false;
           }
   
           // Switch active slot
           config_.active_slot = static_cast<uint8_t>(target);
           config_.boot_count = 0;
           config_.last_update_time = 0;  // Should be set by caller
   
           return save_config();
       }
   
       bool mark_boot_successful() noexcept {
           if (config_.boot_count > 0) {
               config_.boot_count = 0;
               config_.boot_success_count++;
               set_slot_state(active_slot(), SlotState::VALID);
               return save_config();
           }
           return true;  // Already marked
       }
   
       bool increment_boot_count() noexcept {
           config_.boot_count++;
   
           // Check for auto-rollback
           if (config_.boot_count >= config_.max_boot_attempts) {
               // Too many failed boots, rollback to other slot
               Slot other = inactive_slot();
               if (slot_state(other) == SlotState::VALID) {
                   config_.active_slot = static_cast<uint8_t>(other);
                   config_.boot_count = 0;
               }
           }
   
           set_slot_state(active_slot(), SlotState::BOOTING);
           return save_config();
       }
   
       bool rollback() noexcept {
           Slot other = inactive_slot();
           if (slot_state(other) != SlotState::VALID) {
               return false;  // No valid slot to rollback to
           }
   
           config_.active_slot = static_cast<uint8_t>(other);
           config_.boot_count = 0;
           return save_config();
       }
   
       [[nodiscard]] bool rollback_available() const noexcept {
           return slot_state(inactive_slot()) == SlotState::VALID;
       }
   
       [[nodiscard]] const BootConfig& config() const noexcept {
           return config_;
       }
   
       [[nodiscard]] static constexpr const PlatformConfig& platform() noexcept {
           return *Config;
       }
   
   private:
       void init_default_config() noexcept {
           std::memset(&config_, 0, sizeof(config_));
           config_.magic = BOOT_CONFIG_MAGIC;
           config_.config_version = 1;
           config_.active_slot = 0;
           config_.boot_count = 0;
           config_.max_boot_attempts = DEFAULT_MAX_BOOT_ATTEMPTS;
           config_.slot_a_state = SlotState::EMPTY;
           config_.slot_b_state = SlotState::EMPTY;
           config_.checksum = calculate_config_checksum();
       }
   
       void set_slot_state(Slot slot, SlotState state) noexcept {
           if (slot == Slot::SLOT_A) {
               config_.slot_a_state = state;
           } else {
               config_.slot_b_state = state;
           }
       }
   
       [[nodiscard]] uint32_t calculate_config_checksum() const noexcept {
           // Simple XOR checksum of all bytes except checksum field
           const auto* bytes = reinterpret_cast<const uint8_t*>(&config_);
           uint32_t sum = 0;
           for (size_t i = 0; i < offsetof(BootConfig, checksum); ++i) {
               sum ^= static_cast<uint32_t>(bytes[i]) << ((i % 4) * 8);
           }
           return sum;
       }
   
       [[nodiscard]] bool verify_config_checksum() const noexcept {
           return config_.checksum == calculate_config_checksum();
       }
   
       const FlashInterface* flash_ = nullptr;
       BootConfig config_{};
   };
   
   // =============================================================================
   // Convenience Type Aliases
   // =============================================================================
   
   using BootloaderSTM32F4 = BootloaderInterface<&platforms::STM32F4_512K>;
   
   using BootloaderSTM32H7 = BootloaderInterface<&platforms::STM32H7_2M>;
   
   using BootloaderESP32 = BootloaderInterface<&platforms::ESP32_4M>;
   
   using BootloaderRP2040 = BootloaderInterface<&platforms::RP2040_2M>;
   
   // =============================================================================
   // Platform Detection (optional)
   // =============================================================================
   
   #if defined(STM32F4)
       using DefaultBootloader = BootloaderSTM32F4;
   #elif defined(STM32H7)
       using DefaultBootloader = BootloaderSTM32H7;
   #elif defined(ESP32) || defined(ESP_PLATFORM)
       using DefaultBootloader = BootloaderESP32;
   #elif defined(PICO_BOARD) || defined(PICO_RP2040)
       using DefaultBootloader = BootloaderRP2040;
   #else
       // No default - user must select platform
   #endif
   
   } // namespace umidi::protocol
