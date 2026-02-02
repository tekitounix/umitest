// SPDX-License-Identifier: MIT
// UartMidiInput — UART MIDI transport adapter (05-midi.md)
// Satisfies umidi::MidiInput concept.
// DMA circular buffer polling at ~1kHz from SystemTask.
#pragma once

#include <cstdint>
#include <umidi/core/parser.hh>
#include <umidi/core/transport.hh>

namespace umi::backend::cm {

/// UART MIDI input adapter.
/// Polls a DMA circular buffer for incoming MIDI bytes.
/// Satisfies umidi::MidiInput.
template <typename TimerFn, typename QueueType>
class UartMidiInput {
public:
    UartMidiInput(TimerFn timer, QueueType& queue, uint8_t source_id) noexcept
        : timer_(timer), queue_(&queue), source_id_(source_id) {}

    [[nodiscard]] bool is_connected() const noexcept { return true; }

    /// Poll DMA circular buffer for new bytes.
    /// Call at ~1kHz from SystemTask or SysTick.
    void poll() noexcept {
        if (!dma_buf_) {
            return;
        }
        auto ts = timer_();
        uint16_t write_pos = buf_size_ - *dma_ndtr_;
        while (read_pos_ != write_pos) {
            umidi::UMP32 ump;
            if (parser_.parse(dma_buf_[read_pos_], ump)) {
                queue_->push({ts, source_id_, ump});
            }
            read_pos_ = (read_pos_ + 1) % buf_size_;
        }
    }

    /// Bind to a DMA circular buffer.
    /// @param buf Pointer to DMA buffer
    /// @param size Buffer size in bytes
    /// @param ndtr Pointer to DMA NDTR register (remaining count)
    void bind(const uint8_t* buf, uint16_t size, volatile uint16_t* ndtr) noexcept {
        dma_buf_ = buf;
        buf_size_ = size;
        dma_ndtr_ = ndtr;
        read_pos_ = 0;
    }

private:
    umidi::Parser parser_{};
    TimerFn timer_;
    QueueType* queue_ = nullptr;
    const uint8_t* dma_buf_ = nullptr;
    volatile uint16_t* dma_ndtr_ = nullptr;
    uint16_t buf_size_ = 0;
    uint16_t read_pos_ = 0;
    uint8_t source_id_ = 0;
};

} // namespace umi::backend::cm
