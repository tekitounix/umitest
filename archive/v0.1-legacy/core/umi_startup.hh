#pragma once
// =====================================================================
// UMI Startup Template
// =====================================================================
//
// Hardware-independent startup logic. 
// Platform-specific code provides:
//   - Linker symbols (extern "C")
//   - HW policy class
//   - Interrupt handlers
//
// =====================================================================

#include "umi_kernel.hh"
#include <cstdint>

namespace umi::startup {

// =====================================================================
// Linker Symbols (must be provided by platform .cc file)
// =====================================================================

struct LinkerSymbols {
    std::uint32_t* sidata;   // .data source (flash)
    std::uint32_t* sdata;    // .data start (RAM)
    std::uint32_t* edata;    // .data end
    std::uint32_t* sbss;     // .bss start
    std::uint32_t* ebss;     // .bss end
    
    void (**preinit_start)();
    void (**preinit_end)();
    void (**init_start)();
    void (**init_end)();
};

struct SharedMemorySymbols {
    std::uint8_t* audio_start;
    std::uint8_t* audio_end;
    std::uint8_t* midi_start;
    std::uint8_t* midi_end;
    std::uint8_t* fb_start;
    std::uint8_t* fb_end;
    std::uint8_t* hwstate_start;
    std::uint8_t* hwstate_end;
};

// =====================================================================
// C++ Runtime Initialization
// =====================================================================

inline void init_runtime(const LinkerSymbols& sym) {
    // Clear .bss
    for (auto* p = sym.sbss; p < sym.ebss; ++p) *p = 0;
    
    // Copy .data from flash to RAM
    for (auto *s = sym.sidata, *d = sym.sdata; d < sym.edata;) *d++ = *s++;
    
    // Call static constructors
    for (auto fn = sym.preinit_start; fn < sym.preinit_end; ++fn) (*fn)();
    for (auto fn = sym.init_start; fn < sym.init_end; ++fn) (*fn)();
}

// =====================================================================
// Kernel Bootstrap
// =====================================================================

template <typename Kernel, typename HW>
struct Bootstrap {
    Kernel& kernel;
    TaskId idle_task{};
    TaskId main_task{};
    
    explicit Bootstrap(Kernel& k) : kernel(k) {}
    
    // Register shared memory regions
    void register_shared_memory(const SharedMemorySymbols& sym) {
        auto region_size = [](auto* start, auto* end) {
            return static_cast<std::size_t>(end - start);
        };
        
        kernel.register_shared(SharedRegionId::Audio, 
            sym.audio_start, region_size(sym.audio_start, sym.audio_end));
        kernel.register_shared(SharedRegionId::Midi,
            sym.midi_start, region_size(sym.midi_start, sym.midi_end));
        kernel.register_shared(SharedRegionId::Framebuffer,
            sym.fb_start, region_size(sym.fb_start, sym.fb_end));
        kernel.register_shared(SharedRegionId::HwState,
            sym.hwstate_start, region_size(sym.hwstate_start, sym.hwstate_end));
    }
    
    // Create system tasks
    void create_system_tasks(void (*app_main)(void*)) {
        // Idle task
        idle_task = kernel.create_task({
            .entry = [](void*) {
                while (true) {
                    HW::watchdog_feed();
                    HW::enter_sleep();
                }
            },
            .arg = nullptr,
            .prio = Priority::Idle,
            .core_affinity = static_cast<std::uint8_t>(Core::Any),
            .uses_fpu = false,
        });
        
        // Main task (app entry)
        main_task = kernel.create_task({
            .entry = app_main,
            .arg = nullptr,
            .prio = Priority::User,
            .core_affinity = static_cast<std::uint8_t>(Core::Any),
            .uses_fpu = true,
        });
    }
    
    // Start scheduler
    [[noreturn]] void run() {
        if (auto next = kernel.get_next_task()) {
            kernel.prepare_switch(*next);
            HW::start_first_task();
        }
        
        // Fallback (should never reach)
        while (true) HW::enter_sleep();
    }
};

// =====================================================================
// Startup Template
// =====================================================================

/// Main startup sequence. Call from _start() after early HW init.
template <typename Kernel, typename HW>
[[noreturn]] void start(
    Kernel& kernel,
    const LinkerSymbols& linker_sym,
    const SharedMemorySymbols& shared_sym,
    void (*app_main)(void*)
) {
    // Phase 2: C++ runtime
    init_runtime(linker_sym);
    
    // Phase 3: Kernel init
    HW::watchdog_init(1000);
    
    Bootstrap<Kernel, HW> boot(kernel);
    boot.register_shared_memory(shared_sym);
    boot.create_system_tasks(app_main);
    
    // Phase 4: Run
    boot.run();
}

} // namespace umi::startup
