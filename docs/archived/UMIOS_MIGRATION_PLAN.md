# umios Migration Plan: STM32F4 Synth Application

## Overview

This document describes the migration plan for the STM32F4-Discovery USB Audio Full Duplex + MIDI Synth application from bare-metal implementation to umios kernel-based architecture.

## Current State (v0.7.0)

Working bare-metal implementation:
- **Audio OUT**: USB -> Ring Buffer -> I2S DMA -> CS43L22 DAC (48kHz stereo)
- **Audio IN**: PDM mic (2.048MHz -> 32kHz CIC) + Synth (32kHz) -> Resample (48kHz) -> USB
- **MIDI**: USB MIDI -> Synth control
- All processing done directly in DMA IRQ handlers

### Current Architecture
```
DMA1_Stream3 IRQ (PDM):
  - CIC decimation (2.048MHz -> 32kHz)
  - Synth processing (32kHz)
  - Resample both to 48kHz
  - Write stereo buffer to USB Audio IN ring buffer

DMA1_Stream5 IRQ (I2S):
  - Read from USB Audio OUT ring buffer
  - Fill I2S DMA buffer

OTG_FS IRQ (USB):
  - Handle USB events
  - MIDI callback -> synth.handle_midi()
```

## Target Architecture (umios)

### Kernel Components

1. **Kernel Instance**
   ```cpp
   using Kernel = umi::Kernel<8, 16>;  // 8 tasks, 16 timers
   Kernel kernel;
   ```

2. **Task Structure**
   | Task | Priority | Description |
   |------|----------|-------------|
   | Audio | Realtime (0) | Audio processing (deferred from DMA IRQ) |
   | USB | Server (1) | USB event handling (deferred from USB IRQ) |
   | Main | User (2) | Initialization, UI, state management |
   | Idle | Idle (3) | WFI sleep, watchdog |

3. **Event Flow**
   ```
   DMA IRQ -> kernel.notify(audio_task, Event::AudioReady)
   USB IRQ -> kernel.notify(usb_task, Event::UsbReady)
   ```

### Key Files

| Component | Path |
|-----------|------|
| Kernel | `lib/umios/kernel/umi_kernel.hh` |
| Audio Engine | `lib/umios/kernel/umi_audio.hh` |
| Startup | `lib/umios/kernel/umi_startup.hh` |
| MIDI | `lib/umios/kernel/umi_midi.hh` |
| STM32F4 Backend | `lib/umios/backend/cm/stm32f4/` |
| Cortex-M4 | `lib/umios/backend/cm/cortex_m4.hh` |

## Migration Steps

### Phase 1: Kernel Integration

1. **Add kernel instance and hardware abstraction**
   ```cpp
   #include <umi_kernel.hh>
   #include <umi_startup.hh>

   using HW = umi::port::CortexM4<168'000'000>;  // 168MHz
   using Kernel = umi::Kernel<8, 16>;

   Kernel kernel;
   ```

2. **Create task entry points**
   ```cpp
   void audio_task_entry(void*);
   void usb_task_entry(void*);
   void main_task_entry(void*);
   ```

3. **Modify main() to use umi::start()**
   ```cpp
   int main() {
       RCC::init_168mhz();
       umi::start<Kernel, HW>(kernel, linker_symbols, shared_symbols, main_task_entry);
   }
   ```

### Phase 2: IRQ Handler Simplification

**Before (bare-metal):**
```cpp
extern "C" void DMA1_Stream3_IRQHandler() {
    // Full PDM processing, CIC, resample, USB write
    // ~100+ lines of processing code
}
```

**After (umios):**
```cpp
extern "C" void DMA1_Stream3_IRQHandler() {
    if (dma_pdm.transfer_complete()) {
        dma_pdm.clear_tc();
        kernel.notify(audio_task_id, Event::PdmReady);
    }
}
```

### Phase 3: Audio Task Implementation

```cpp
void audio_task_entry(void*) {
    while (true) {
        kernel.wait_block(Event::PdmReady | Event::I2sReady);

        // PDM processing (moved from DMA1_Stream3 IRQ)
        if (pending & Event::PdmReady) {
            process_pdm_buffer();
        }

        // I2S processing (moved from DMA1_Stream5 IRQ)
        if (pending & Event::I2sReady) {
            fill_i2s_buffer();
        }
    }
}
```

### Phase 4: USB Task Implementation

```cpp
void usb_task_entry(void*) {
    while (true) {
        kernel.wait_block(Event::UsbReady);
        usb_device.poll();  // Process deferred USB events
    }
}
```

### Phase 5: MIDI Queue Integration

```cpp
#include <umi_midi.hh>
#include <spsc_queue.hh>

umi::SpscQueue<umi::midi::Message, 64> midi_queue;

// In USB MIDI callback (ISR context):
usb_audio.set_midi_callback([](uint8_t cable, const uint8_t* data, uint8_t len) {
    if (len >= 3) {
        midi_queue.try_push(umi::midi::Message{data[0], data[1], data[2]});
    }
});

// In audio task:
while (auto msg = midi_queue.try_pop()) {
    synth.handle_midi_message(*msg);
}
```

## Event Definitions

```cpp
namespace Event {
    constexpr uint32_t PdmReady  = 1 << 0;  // PDM DMA complete
    constexpr uint32_t I2sReady  = 1 << 1;  // I2S DMA complete
    constexpr uint32_t UsbReady  = 1 << 2;  // USB interrupt
    constexpr uint32_t MidiReady = 1 << 3;  // MIDI data available
}
```

## Task Configuration

```cpp
// Audio task: Realtime priority, FPU enabled
TaskConfig audio_config {
    .entry = audio_task_entry,
    .arg = nullptr,
    .prio = Priority::Realtime,
    .core_affinity = 0,
    .uses_fpu = true,
    .name = "audio"
};

// USB task: Server priority
TaskConfig usb_config {
    .entry = usb_task_entry,
    .arg = nullptr,
    .prio = Priority::Server,
    .core_affinity = 0,
    .uses_fpu = false,
    .name = "usb"
};
```

## Memory Layout

### DMA Buffers (unchanged)
```cpp
__attribute__((section(".dma_buffer")))
int16_t audio_buf0[BUFFER_SIZE * 2];
int16_t audio_buf1[BUFFER_SIZE * 2];
uint16_t pdm_buf0[PDM_BUF_SIZE];
uint16_t pdm_buf1[PDM_BUF_SIZE];
```

### Task Stacks
```cpp
// Linker script additions
.task_stacks (NOLOAD) : {
    . = ALIGN(8);
    _audio_stack_start = .;
    . += 2048;  /* Audio task: 2KB (FPU context) */
    _audio_stack_end = .;

    _usb_stack_start = .;
    . += 1024;  /* USB task: 1KB */
    _usb_stack_end = .;

    _main_stack_start = .;
    . += 2048;  /* Main task: 2KB */
    _main_stack_end = .;
} > RAM
```

## Interrupt Priority Mapping

| IRQ | Priority | Description |
|-----|----------|-------------|
| SysTick | 0 | Kernel tick (highest) |
| PendSV | 15 | Context switch (lowest) |
| DMA1_Stream3 | 5 | PDM DMA |
| DMA1_Stream5 | 5 | I2S DMA |
| OTG_FS | 6 | USB |

## AudioEngine Integration (Optional)

For more structured audio processing:

```cpp
#include <umi_audio.hh>

struct AudioIO {
    static constexpr uint32_t SAMPLE_RATE = 48000;
    static constexpr uint16_t BUFFER_SIZE = 64;
    static constexpr uint8_t INPUT_CHANNELS = 2;
    static constexpr uint8_t OUTPUT_CHANNELS = 2;
};

using Engine = umi::AudioEngine<HW, AudioIO, float>;
Engine audio_engine;

// In DMA ISR:
audio_engine.on_dma_complete(input_buf, output_buf);
kernel.notify(audio_task_id, Event::AudioReady);

// In audio task:
audio_engine.process([&](auto& ctx) {
    // Process MIDI
    while (auto msg = midi_queue.try_pop()) {
        synth.handle_midi_message(*msg);
    }

    // Generate synth output
    for (uint32_t i = 0; i < ctx.frames(); ++i) {
        float sample = synth.process_sample();
        ctx.output(0, i) = sample;  // Left
        ctx.output(1, i) = sample;  // Right
    }
});
```

## Benefits

1. **Deterministic Timing**: Audio processing in task context with guaranteed priority
2. **Reduced ISR Time**: IRQs only do notification, no heavy processing
3. **Better Debugging**: Task-based architecture easier to trace
4. **Scalability**: Easy to add more tasks (UI, networking, etc.)
5. **Load Monitoring**: Built-in DSP load measurement
6. **Standard Patterns**: Follows umios conventions for portability

## Considerations

1. **FPU Context**: Audio task must have `uses_fpu = true`
2. **DMA Buffers**: Must remain in SRAM, not CCM
3. **Latency**: Task switch adds small latency (< 10us typical)
4. **Stack Usage**: Monitor stack usage with kernel debug features
5. **Existing Drivers**: Continue using STM32F4 backend drivers as-is

## Testing Checklist

- [ ] Kernel boots and creates tasks
- [ ] Audio OUT plays without glitches
- [ ] Audio IN records without dropouts
- [ ] MIDI controls synth correctly
- [ ] No buffer overflows/underflows
- [ ] DSP load within budget (< 50% target)
- [ ] All LEDs indicate correct state
