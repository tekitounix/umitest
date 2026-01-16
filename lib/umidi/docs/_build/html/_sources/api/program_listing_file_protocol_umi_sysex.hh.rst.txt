
.. _program_listing_file_protocol_umi_sysex.hh:

Program Listing for File umi_sysex.hh
=====================================

|exhale_lsh| :ref:`Return to documentation for file <file_protocol_umi_sysex.hh>` (``protocol/umi_sysex.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   #pragma once
   
   // Core protocol components
   #include "encoding.hh"
   #include "commands.hh"
   #include "message.hh"
   #include "standard_io.hh"
   
   // Extended components (included separately due to dependencies)
   #include "umi_auth.hh"
   #include "umi_firmware.hh"
   #include "umi_bootloader.hh"
   #include "umi_session.hh"
   
   #include <cstring>
   
   namespace umidi::protocol {
   
   // =============================================================================
   // Firmware Update State
   // =============================================================================
   
   enum class UpdateState : uint8_t {
       IDLE,            
       RECEIVING,       
       VERIFYING,       
       READY_TO_COMMIT, 
       COMMITTED,       
       ERROR,           
   };
   
   // =============================================================================
   // Basic Firmware Update Handler
   // =============================================================================
   
   template <size_t MaxChunkSize = 256>
   class FirmwareUpdate {
   public:
       void set_flash_callback(FlashWriteCallback cb, void* ctx) noexcept {
           flash_cb_ = cb;
           flash_ctx_ = ctx;
       }
   
       void set_verify_callback(SignatureVerifyCallback cb, void* ctx) noexcept {
           verify_cb_ = cb;
           verify_ctx_ = ctx;
       }
   
       template <typename SendFn>
       bool process_message(const uint8_t* data, size_t len, SendFn send_fn) {
           auto msg = parse_message(data, len);
           if (!msg.valid) return false;
   
           switch (msg.command) {
           case Command::FW_QUERY: return handle_query(msg, send_fn);
           case Command::FW_BEGIN: return handle_begin(msg, send_fn);
           case Command::FW_DATA:  return handle_data(msg, send_fn);
           case Command::FW_VERIFY: return handle_verify(msg, send_fn);
           case Command::FW_COMMIT: return handle_commit(msg, send_fn);
           case Command::FW_REBOOT: return handle_reboot(msg, send_fn);
           default: return false;
           }
       }
   
       [[nodiscard]] UpdateState state() const noexcept { return state_; }
       [[nodiscard]] uint32_t received_bytes() const noexcept { return received_bytes_; }
       [[nodiscard]] uint32_t total_size() const noexcept { return expected_size_; }
   
       void reset() noexcept {
           state_ = UpdateState::IDLE;
           expected_size_ = 0;
           received_bytes_ = 0;
           running_crc_ = 0xFFFFFFFF;
           expected_seq_ = 0;
       }
   
   private:
       template <typename SendFn>
       bool handle_query(const ParsedMessage& msg, SendFn send_fn) {
           MessageBuilder<128> builder;
           builder.begin(Command::FW_INFO, msg.sequence);
           builder.add_u32(0x01000000);
           builder.add_u32(0);
           builder.add_u32(0);
           send_fn(builder.data(), builder.finalize());
           return true;
       }
   
       template <typename SendFn>
       bool handle_begin(const ParsedMessage& msg, SendFn send_fn) {
           if (state_ != UpdateState::IDLE) {
               send_nack(msg.sequence, ErrorCode::UPDATE_IN_PROGRESS, send_fn);
               return true;
           }
   
           uint8_t decoded[16];
           size_t dec_len = msg.decode_payload(decoded, sizeof(decoded));
           if (dec_len < 4) {
               send_nack(msg.sequence, ErrorCode::INVALID_COMMAND, send_fn);
               return true;
           }
   
           expected_size_ = (uint32_t(decoded[0]) << 24) |
                            (uint32_t(decoded[1]) << 16) |
                            (uint32_t(decoded[2]) << 8) | decoded[3];
   
           state_ = UpdateState::RECEIVING;
           received_bytes_ = 0;
           running_crc_ = 0xFFFFFFFF;
           expected_seq_ = (msg.sequence + 1) & 0x7F;
   
           send_ack(msg.sequence, send_fn);
           return true;
       }
   
       template <typename SendFn>
       bool handle_data(const ParsedMessage& msg, SendFn send_fn) {
           if (state_ != UpdateState::RECEIVING) {
               send_nack(msg.sequence, ErrorCode::UPDATE_NOT_STARTED, send_fn);
               return true;
           }
   
           if (msg.sequence != expected_seq_) {
               send_nack(msg.sequence, ErrorCode::INVALID_SEQUENCE, send_fn);
               return true;
           }
   
           uint8_t decoded[MaxChunkSize];
           size_t dec_len = msg.decode_payload(decoded, sizeof(decoded));
           if (dec_len == 0) {
               send_nack(msg.sequence, ErrorCode::INVALID_COMMAND, send_fn);
               return true;
           }
   
           if (flash_cb_) {
               if (!flash_cb_(received_bytes_, decoded, dec_len, flash_ctx_)) {
                   state_ = UpdateState::ERROR;
                   send_nack(msg.sequence, ErrorCode::FLASH_ERROR, send_fn);
                   return true;
               }
           }
   
           update_crc(decoded, dec_len);
           received_bytes_ += dec_len;
           expected_seq_ = (expected_seq_ + 1) & 0x7F;
   
           send_ack(msg.sequence, send_fn);
           return true;
       }
   
       template <typename SendFn>
       bool handle_verify(const ParsedMessage& msg, SendFn send_fn) {
           if (state_ != UpdateState::RECEIVING) {
               send_nack(msg.sequence, ErrorCode::UPDATE_NOT_STARTED, send_fn);
               return true;
           }
   
           if (received_bytes_ != expected_size_) {
               send_nack(msg.sequence, ErrorCode::VERIFY_FAILED, send_fn);
               return true;
           }
   
           uint32_t final_crc = running_crc_ ^ 0xFFFFFFFF;
   
           uint8_t decoded[64];
           size_t dec_len = msg.decode_payload(decoded, sizeof(decoded));
           if (dec_len < 4) {
               send_nack(msg.sequence, ErrorCode::INVALID_COMMAND, send_fn);
               return true;
           }
   
           uint32_t expected_crc = (uint32_t(decoded[0]) << 24) |
                                   (uint32_t(decoded[1]) << 16) |
                                   (uint32_t(decoded[2]) << 8) | decoded[3];
   
           if (final_crc != expected_crc) {
               state_ = UpdateState::ERROR;
               send_nack(msg.sequence, ErrorCode::VERIFY_FAILED, send_fn);
               return true;
           }
   
           state_ = UpdateState::READY_TO_COMMIT;
           send_ack(msg.sequence, send_fn);
           return true;
       }
   
       template <typename SendFn>
       bool handle_commit(const ParsedMessage& msg, SendFn send_fn) {
           if (state_ != UpdateState::READY_TO_COMMIT) {
               send_nack(msg.sequence, ErrorCode::VERIFY_FAILED, send_fn);
               return true;
           }
           state_ = UpdateState::COMMITTED;
           send_ack(msg.sequence, send_fn);
           return true;
       }
   
       template <typename SendFn>
       bool handle_reboot(const ParsedMessage& msg, SendFn send_fn) {
           send_ack(msg.sequence, send_fn);
           return true;
       }
   
       template <typename SendFn>
       void send_ack(uint8_t seq, SendFn send_fn) {
           MessageBuilder<8> builder;
           builder.begin(Command::FW_ACK, seq);
           send_fn(builder.data(), builder.finalize());
       }
   
       template <typename SendFn>
       void send_nack(uint8_t seq, ErrorCode err, SendFn send_fn) {
           MessageBuilder<8> builder;
           builder.begin(Command::FW_NACK, seq);
           builder.add_byte(static_cast<uint8_t>(err));
           send_fn(builder.data(), builder.finalize());
       }
   
       void update_crc(const uint8_t* data, size_t len) noexcept {
           constexpr uint32_t CRC32_POLY = 0xEDB88320;
           for (size_t i = 0; i < len; ++i) {
               running_crc_ ^= data[i];
               for (int j = 0; j < 8; ++j) {
                   if (running_crc_ & 1) {
                       running_crc_ = (running_crc_ >> 1) ^ CRC32_POLY;
                   } else {
                       running_crc_ >>= 1;
                   }
               }
           }
       }
   
       FlashWriteCallback flash_cb_ = nullptr;
       void* flash_ctx_ = nullptr;
       SignatureVerifyCallback verify_cb_ = nullptr;
       void* verify_ctx_ = nullptr;
   
       UpdateState state_ = UpdateState::IDLE;
       uint32_t expected_size_ = 0;
       uint32_t received_bytes_ = 0;
       uint32_t running_crc_ = 0xFFFFFFFF;
       uint8_t expected_seq_ = 0;
   };
   
   // =============================================================================
   // Combined Protocol Handler
   // =============================================================================
   
   template <size_t TxBufSize = 512, size_t RxBufSize = 512>
   class ProtocolHandler {
   public:
       StandardIO<TxBufSize, RxBufSize> io;
       FirmwareUpdate<256> fw_update;
   
       template <typename SendFn>
       bool process(const uint8_t* data, size_t len, SendFn send_fn) {
           if (io.process_message(data, len)) return true;
           if (fw_update.process_message(data, len, send_fn)) return true;
           return false;
       }
   };
   
   // =============================================================================
   // Secure Firmware Update (Full Implementation)
   // =============================================================================
   
   template <const PlatformConfig* Config, size_t MaxChunkSize = 256>
   class SecureFirmwareUpdate {
   public:
       void init(const FlashInterface& flash,
                 const uint8_t* public_key = nullptr,
                 const uint8_t* shared_secret = nullptr) noexcept {
           bootloader_.init(flash);
           validator_.set_crc(crc32_fn);
           if (public_key) {
               validator_.set_public_key(public_key);
               validator_.require_signature(true);
           }
           validator_.set_board_id(Config->name);
           if (shared_secret) {
               auth_.init(shared_secret, nullptr, nullptr);
               require_auth_ = true;
           }
           session_.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);
           flash_ = &flash;
       }
   
       void set_crypto(HmacSha256Fn hmac, RandomFn rng,
                       Ed25519VerifyFn ed25519_verify, Sha256Fn sha256) noexcept {
           hmac_fn_ = hmac;
           rng_fn_ = rng;
           validator_.set_crypto(ed25519_verify, sha256, crc32_fn);
       }
   
       template <typename SendFn>
       bool process(const uint8_t* data, size_t len,
                    uint32_t current_time, SendFn send_fn) {
           auto timeout = session_.check_timeout(current_time);
           if (timeout != TimeoutEvent::NONE) {
               if (!session_.handle_timeout(timeout)) {
                   abort_update(send_fn);
                   return true;
               }
           }
   
           auto msg = parse_message(data, len);
           if (!msg.valid) return false;
   
           if (handle_auth_command(msg, current_time, send_fn)) return true;
   
           if (require_auth_ && !auth_.is_authenticated(current_time)) {
               if (is_firmware_command(msg.command)) {
                   send_nack(msg.sequence, ErrorCode::INVALID_COMMAND, send_fn);
                   return true;
               }
           }
   
           return handle_firmware_command(msg, current_time, send_fn);
       }
   
       [[nodiscard]] UpdateState state() const noexcept { return state_; }
       [[nodiscard]] float progress() const noexcept { return session_.progress(); }
       [[nodiscard]] bool rollback_available() const noexcept {
           return bootloader_.rollback_available();
       }
       [[nodiscard]] bool reboot_requested() const noexcept { return reboot_requested_; }
       void clear_reboot_request() noexcept { reboot_requested_ = false; }
   
       BootloaderInterface<Config>& bootloader() noexcept { return bootloader_; }
       FirmwareValidator<16>& validator() noexcept { return validator_; }
       Authenticator<32, 300000>& authenticator() noexcept { return auth_; }
   
   private:
       template <typename SendFn>
       bool handle_auth_command(const ParsedMessage& msg, uint32_t current_time, SendFn send_fn) {
           auto cmd = static_cast<AuthCommand>(static_cast<uint8_t>(msg.command));
           switch (cmd) {
           case AuthCommand::AUTH_CHALLENGE_REQ: {
               MessageBuilder<64> builder;
               builder.begin(static_cast<Command>(AuthCommand::AUTH_CHALLENGE), msg.sequence);
               uint8_t challenge[32];
               auth_.generate_challenge(challenge);
               builder.add_data(challenge, 32);
               send_fn(builder.data(), builder.finalize());
               return true;
           }
           case AuthCommand::AUTH_RESPONSE: {
               uint8_t response[32];
               if (msg.decode_payload(response, 32) != 32) {
                   send_auth_fail(msg.sequence, AuthError::INVALID_RESPONSE, send_fn);
                   return true;
               }
               if (auth_.verify_response(response, current_time)) {
                   send_auth_ok(msg.sequence, send_fn);
               } else {
                   send_auth_fail(msg.sequence, auth_.last_error(), send_fn);
               }
               return true;
           }
           case AuthCommand::AUTH_LOGOUT:
               auth_.logout();
               send_auth_ok(msg.sequence, send_fn);
               return true;
           case AuthCommand::AUTH_STATUS: {
               MessageBuilder<16> builder;
               auto status_cmd = auth_.is_authenticated(current_time)
                   ? AuthCommand::AUTH_OK : AuthCommand::AUTH_FAIL;
               builder.begin(static_cast<Command>(status_cmd), msg.sequence);
               send_fn(builder.data(), builder.finalize());
               return true;
           }
           default:
               return false;
           }
       }
   
       template <typename SendFn>
       bool handle_firmware_command(const ParsedMessage& msg, uint32_t current_time, SendFn send_fn) {
           switch (msg.command) {
           case Command::FW_QUERY: return handle_fw_query(msg, send_fn);
           case Command::FW_BEGIN: return handle_fw_begin(msg, current_time, send_fn);
           case Command::FW_DATA:  return handle_fw_data(msg, current_time, send_fn);
           case Command::FW_VERIFY: return handle_fw_verify(msg, send_fn);
           case Command::FW_COMMIT: return handle_fw_commit(msg, send_fn);
           case Command::FW_ROLLBACK: return handle_fw_rollback(msg, send_fn);
           case Command::FW_REBOOT: return handle_fw_reboot(msg, send_fn);
           default: return false;
           }
       }
   
       template <typename SendFn>
       bool handle_fw_query(const ParsedMessage& msg, SendFn send_fn) {
           MessageBuilder<128> builder;
           builder.begin(Command::FW_INFO, msg.sequence);
           auto slot = bootloader_.active_slot();
           const auto& config = bootloader_.config();
           uint32_t version = (slot == Slot::SLOT_A) ? config.slot_a_version : config.slot_b_version;
           builder.add_u32(version);
           builder.add_u32(bootloader_.max_firmware_size());
           builder.add_byte(static_cast<uint8_t>(slot));
           builder.add_byte(rollback_available() ? 1 : 0);
           send_fn(builder.data(), builder.finalize());
           return true;
       }
   
       template <typename SendFn>
       bool handle_fw_begin(const ParsedMessage& msg, uint32_t current_time, SendFn send_fn) {
           if (state_ != UpdateState::IDLE) {
               send_nack(msg.sequence, ErrorCode::UPDATE_IN_PROGRESS, send_fn);
               return true;
           }
   
           uint8_t header_data[sizeof(FirmwareHeader)];
           size_t decoded = msg.decode_payload(header_data, sizeof(header_data));
           if (decoded < sizeof(FirmwareHeader)) {
               send_nack(msg.sequence, ErrorCode::INVALID_COMMAND, send_fn);
               return true;
           }
   
           const auto* header = reinterpret_cast<const FirmwareHeader*>(header_data);
           auto result = validator_.validate_header(*header);
           if (result != ValidationResult::OK) {
               send_nack(msg.sequence, validation_to_error(result), send_fn);
               return true;
           }
   
           if (header->image_size > bootloader_.max_firmware_size()) {
               send_nack(msg.sequence, ErrorCode::BUFFER_OVERFLOW, send_fn);
               return true;
           }
   
           target_slot_ = bootloader_.update_target();
           auto flash_result = bootloader_.prepare_slot(target_slot_);
           if (flash_result != FlashResult::OK) {
               send_nack(msg.sequence, ErrorCode::FLASH_ERROR, send_fn);
               return true;
           }
   
           std::memcpy(&pending_header_, header, sizeof(FirmwareHeader));
           session_.start(current_time, header->image_size + sizeof(FirmwareHeader));
           state_ = UpdateState::RECEIVING;
           write_offset_ = 0;
   
           bootloader_.write_firmware(target_slot_, 0, header_data, sizeof(FirmwareHeader));
           write_offset_ = sizeof(FirmwareHeader);
   
           send_ack(msg.sequence, send_fn);
           return true;
       }
   
       template <typename SendFn>
       bool handle_fw_data(const ParsedMessage& msg, uint32_t current_time, SendFn send_fn) {
           if (state_ != UpdateState::RECEIVING) {
               send_nack(msg.sequence, ErrorCode::UPDATE_NOT_STARTED, send_fn);
               return true;
           }
   
           uint8_t chunk[MaxChunkSize];
           size_t decoded = msg.decode_payload(chunk, sizeof(chunk));
           if (decoded == 0) {
               send_nack(msg.sequence, ErrorCode::INVALID_COMMAND, send_fn);
               return true;
           }
   
           if (!session_.receive_data(msg.sequence, decoded, current_time)) {
               send_nack(msg.sequence, ErrorCode::INVALID_SEQUENCE, send_fn);
               return true;
           }
   
           auto result = bootloader_.write_firmware(target_slot_, write_offset_, chunk, decoded);
           if (result != FlashResult::OK) {
               state_ = UpdateState::ERROR;
               send_nack(msg.sequence, ErrorCode::FLASH_ERROR, send_fn);
               return true;
           }
   
           write_offset_ += decoded;
   
           MessageBuilder<16> builder;
           builder.begin(Command::FW_ACK, msg.sequence);
           builder.add_byte(session_.get_ack());
           send_fn(builder.data(), builder.finalize());
           session_.record_ack_sent(current_time);
           return true;
       }
   
       template <typename SendFn>
       bool handle_fw_verify(const ParsedMessage& msg, SendFn send_fn) {
           if (state_ != UpdateState::RECEIVING) {
               send_nack(msg.sequence, ErrorCode::UPDATE_NOT_STARTED, send_fn);
               return true;
           }
   
           if (!session_.is_complete()) {
               send_nack(msg.sequence, ErrorCode::VERIFY_FAILED, send_fn);
               return true;
           }
   
           state_ = UpdateState::VERIFYING;
   
           uint8_t signature[ED25519_SIGNATURE_SIZE];
           bool has_signature = (msg.decode_payload(signature, ED25519_SIGNATURE_SIZE)
                                 == ED25519_SIGNATURE_SIZE);
   
           if (has_signature || (pending_header_.flags &
                                 static_cast<uint8_t>(FirmwareFlags::SIGNED))) {
               auto result = validator_.validate_signature(pending_header_, nullptr, signature);
               if (result != ValidationResult::OK) {
                   state_ = UpdateState::ERROR;
                   send_nack(msg.sequence, ErrorCode::SIGNATURE_INVALID, send_fn);
                   return true;
               }
           }
   
           bootloader_.mark_slot_pending(target_slot_);
           state_ = UpdateState::READY_TO_COMMIT;
           send_ack(msg.sequence, send_fn);
           return true;
       }
   
       template <typename SendFn>
       bool handle_fw_commit(const ParsedMessage& msg, SendFn send_fn) {
           if (state_ != UpdateState::READY_TO_COMMIT) {
               send_nack(msg.sequence, ErrorCode::VERIFY_FAILED, send_fn);
               return true;
           }
   
           uint32_t version = pack_version(pending_header_.fw_version_major,
                                           pending_header_.fw_version_minor,
                                           pending_header_.fw_version_patch);
           bootloader_.mark_slot_valid(target_slot_, version, pending_header_.image_crc32);
   
           if (!bootloader_.commit_update()) {
               send_nack(msg.sequence, ErrorCode::FLASH_ERROR, send_fn);
               return true;
           }
   
           state_ = UpdateState::COMMITTED;
           session_.end();
           send_ack(msg.sequence, send_fn);
           return true;
       }
   
       template <typename SendFn>
       bool handle_fw_rollback(const ParsedMessage& msg, SendFn send_fn) {
           if (!bootloader_.rollback_available()) {
               send_nack(msg.sequence, ErrorCode::ROLLBACK_UNAVAIL, send_fn);
               return true;
           }
           if (!bootloader_.rollback()) {
               send_nack(msg.sequence, ErrorCode::FLASH_ERROR, send_fn);
               return true;
           }
           send_ack(msg.sequence, send_fn);
           return true;
       }
   
       template <typename SendFn>
       bool handle_fw_reboot(const ParsedMessage& msg, SendFn send_fn) {
           send_ack(msg.sequence, send_fn);
           reboot_requested_ = true;
           return true;
       }
   
       template <typename SendFn>
       void abort_update(SendFn send_fn) {
           state_ = UpdateState::ERROR;
           session_.end();
           MessageBuilder<8> builder;
           builder.begin(Command::FW_NACK, 0);
           builder.add_byte(static_cast<uint8_t>(ErrorCode::TIMEOUT));
           send_fn(builder.data(), builder.finalize());
       }
   
       template <typename SendFn>
       void send_ack(uint8_t seq, SendFn send_fn) {
           MessageBuilder<8> builder;
           builder.begin(Command::FW_ACK, seq);
           send_fn(builder.data(), builder.finalize());
       }
   
       template <typename SendFn>
       void send_nack(uint8_t seq, ErrorCode err, SendFn send_fn) {
           MessageBuilder<8> builder;
           builder.begin(Command::FW_NACK, seq);
           builder.add_byte(static_cast<uint8_t>(err));
           send_fn(builder.data(), builder.finalize());
       }
   
       template <typename SendFn>
       void send_auth_ok(uint8_t seq, SendFn send_fn) {
           MessageBuilder<8> builder;
           builder.begin(static_cast<Command>(AuthCommand::AUTH_OK), seq);
           send_fn(builder.data(), builder.finalize());
       }
   
       template <typename SendFn>
       void send_auth_fail(uint8_t seq, AuthError err, SendFn send_fn) {
           MessageBuilder<8> builder;
           builder.begin(static_cast<Command>(AuthCommand::AUTH_FAIL), seq);
           builder.add_byte(static_cast<uint8_t>(err));
           send_fn(builder.data(), builder.finalize());
       }
   
       static bool is_firmware_command(Command cmd) noexcept {
           auto c = static_cast<uint8_t>(cmd);
           return c >= 0x10 && c <= 0x1F;
       }
   
       static ErrorCode validation_to_error(ValidationResult result) noexcept {
           switch (result) {
           case ValidationResult::CRC_MISMATCH:
           case ValidationResult::HASH_MISMATCH:
               return ErrorCode::VERIFY_FAILED;
           case ValidationResult::SIGNATURE_INVALID:
               return ErrorCode::SIGNATURE_INVALID;
           default:
               return ErrorCode::INVALID_COMMAND;
           }
       }
   
       BootloaderInterface<Config> bootloader_;
       FirmwareValidator<16> validator_;
       Authenticator<32, 300000> auth_;
       FirmwareUpdateSessionWithProgress<4> session_;
       const FlashInterface* flash_ = nullptr;
       HmacSha256Fn hmac_fn_ = nullptr;
       RandomFn rng_fn_ = nullptr;
       UpdateState state_ = UpdateState::IDLE;
       Slot target_slot_ = Slot::SLOT_A;
       FirmwareHeader pending_header_{};
       uint32_t write_offset_ = 0;
       bool require_auth_ = false;
       bool reboot_requested_ = false;
   };
   
   // Platform Type Aliases
   using SecureFirmwareUpdateSTM32F4 = SecureFirmwareUpdate<&platforms::STM32F4_512K>;
   using SecureFirmwareUpdateSTM32H7 = SecureFirmwareUpdate<&platforms::STM32H7_2M>;
   using SecureFirmwareUpdateESP32 = SecureFirmwareUpdate<&platforms::ESP32_4M>;
   using SecureFirmwareUpdateRP2040 = SecureFirmwareUpdate<&platforms::RP2040_2M>;
   
   } // namespace umidi::protocol
