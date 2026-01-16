
.. _program_listing_file_include_umidi_core_sysex_buffer.hh:

Program Listing for File sysex_buffer.hh
========================================

|exhale_lsh| :ref:`Return to documentation for file <file_include_umidi_core_sysex_buffer.hh>` (``include/umidi/core/sysex_buffer.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   // umidi - SysEx Ring Buffer (Fixed-size, no heap allocation)
   #pragma once
   
   #include <cstddef>
   #include <cstdint>
   
   namespace umidi {
   
   // =============================================================================
   // SysExBuffer: Fixed-size ring buffer for SysEx data
   // =============================================================================
   // No heap allocation, suitable for embedded systems
   
   template <size_t MaxSize = 256>
   class SysExBuffer {
   public:
       constexpr SysExBuffer() noexcept = default;
   
       [[nodiscard]] constexpr bool push(uint8_t byte) noexcept {
           if (full()) {
               return false;
           }
           buffer_[write_pos_] = byte;
           write_pos_ = (write_pos_ + 1) % MaxSize;
           ++size_;
           return true;
       }
   
       [[nodiscard]] constexpr bool pop(uint8_t& byte) noexcept {
           if (empty()) {
               return false;
           }
           byte = buffer_[read_pos_];
           read_pos_ = (read_pos_ + 1) % MaxSize;
           --size_;
           return true;
       }
   
       [[nodiscard]] constexpr bool peek(uint8_t& byte) const noexcept {
           if (empty()) {
               return false;
           }
           byte = buffer_[read_pos_];
           return true;
       }
   
       [[nodiscard]] constexpr const uint8_t* data() const noexcept {
           return &buffer_[read_pos_];
       }
   
       [[nodiscard]] constexpr size_t contiguous_size() const noexcept {
           if (empty()) return 0;
           if (write_pos_ > read_pos_) {
               return size_;
           }
           return MaxSize - read_pos_;
       }
   
       constexpr size_t copy_to(uint8_t* output, size_t max_len) const noexcept {
           size_t copied = 0;
           size_t pos = read_pos_;
           size_t remaining = size_;
   
           while (remaining > 0 && copied < max_len) {
               output[copied++] = buffer_[pos];
               pos = (pos + 1) % MaxSize;
               --remaining;
           }
           return copied;
       }
   
       constexpr void clear() noexcept {
           write_pos_ = 0;
           read_pos_ = 0;
           size_ = 0;
       }
   
       [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
       [[nodiscard]] constexpr bool full() const noexcept { return size_ == MaxSize; }
       [[nodiscard]] constexpr size_t size() const noexcept { return size_; }
       [[nodiscard]] static constexpr size_t capacity() noexcept { return MaxSize; }
       [[nodiscard]] constexpr size_t available() const noexcept { return MaxSize - size_; }
   
   private:
       uint8_t buffer_[MaxSize] = {};
       size_t write_pos_ = 0;
       size_t read_pos_ = 0;
       size_t size_ = 0;
   };
   
   // Common specializations
   using SysExBuffer128 = SysExBuffer<128>;
   using SysExBuffer256 = SysExBuffer<256>;
   using SysExBuffer512 = SysExBuffer<512>;
   
   } // namespace umidi
