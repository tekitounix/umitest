// =====================================================================
// UMI-OS Debug Shell
// =====================================================================
//
// A minimal debug shell for embedded systems.
// Provides commands: ps, mem, load, reboot, help
//
// Usage:
//   Shell<HW, Kernel> shell(kernel);
//   shell.process_char(c);  // Call from UART RX interrupt
//
// =====================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace umi {

/// Shell output function type (printf-like)
using ShellWriteFn = void (*)(const char* str);

/// Minimal debug shell for UMI-OS
template<typename HW, typename Kernel>
class Shell {
public:
    Shell(Kernel& k, ShellWriteFn write_fn)
        : kernel(k), write(write_fn) {
        clear_line();
    }

    /// Process a single character from input.
    void process_char(char c) {
        if (c == '\r' || c == '\n') {
            if (line_pos > 0) {
                execute_command();
            }
            clear_line();
        } else if (c == '\b' || c == 0x7F) {
            // Backspace
            if (line_pos > 0) {
                line_pos--;
            }
        } else if (c >= ' ' && c < 0x7F && line_pos < sizeof(line) - 1) {
            line[line_pos++] = c;
        }
    }

private:
    Kernel& kernel;
    ShellWriteFn write;
    char line[64] {};
    std::size_t line_pos {0};

    void clear_line() {
        line_pos = 0;
        std::memset(line, 0, sizeof(line));
    }

    void execute_command() {
        line[line_pos] = '\0';
        
        // Parse command (first word)
        char* cmd = line;
        while (*cmd == ' ') cmd++;  // Skip leading spaces
        
        char* arg = cmd;
        while (*arg && *arg != ' ') arg++;  // Find space
        if (*arg) {
            *arg = '\0';
            arg++;
            while (*arg == ' ') arg++;  // Skip spaces after command
        }

        if (strcmp(cmd, "ps") == 0) {
            cmd_ps();
        } else if (strcmp(cmd, "mem") == 0) {
            cmd_mem();
        } else if (strcmp(cmd, "load") == 0) {
            cmd_load();
        } else if (strcmp(cmd, "led") == 0) {
            cmd_led(arg);
        } else if (strcmp(cmd, "audio") == 0) {
            cmd_audio();
        } else if (strcmp(cmd, "midi") == 0) {
            cmd_midi();
        } else if (strcmp(cmd, "status") == 0) {
            cmd_status();
        } else if (strcmp(cmd, "reboot") == 0) {
            cmd_reboot();
        } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
            cmd_help();
        } else if (cmd[0] != '\0') {
            write("Unknown command: ");
            write(cmd);
            write("\r\n");
        }
    }

    void cmd_help() {
        write("Commands:\r\n");
        write("  ps         - List tasks\r\n");
        write("  mem        - Show memory usage\r\n");
        write("  load       - Show CPU load/uptime\r\n");
        write("  led <n> <on|off|toggle>\r\n");
        write("             - Control LED (0-3)\r\n");
        write("  audio      - Audio statistics\r\n");
        write("  midi       - MIDI statistics\r\n");
        write("  status     - System status\r\n");
        write("  reboot     - System reset\r\n");
        write("  help       - Show this help\r\n");
    }

    void cmd_ps() {
        write("ID  Name            Prio     State\r\n");
        write("--  --------------  -------  -------\r\n");
        
        kernel.for_each_task([this](auto id, auto& cfg, auto state) {
            char buf[64];
            const char* prio_str = "?";
            using Priority = typename Kernel::Priority;
            switch (cfg.prio) {
                case Priority::REALTIME: prio_str = "RT"; break;
                case Priority::USER:     prio_str = "User"; break;
                case Priority::IDLE:     prio_str = "Idle"; break;
            }
            
            const char* state_str = "?";
            using State = typename Kernel::State;
            if constexpr (requires { state == State::Ready; }) {
                switch (state) {
                    case State::Ready:   state_str = "Ready"; break;
                    case State::Running: state_str = "Running"; break;
                    case State::Blocked: state_str = "Blocked"; break;
                    default:             state_str = "?"; break;
                }
            }
            
            snprintf(buf, sizeof(buf), "%2u  %-14s  %-7s  %s\r\n",
                     id.value, cfg.name ? cfg.name : "<?>", prio_str, state_str);
            write(buf);
        });
    }

    void cmd_mem() {
        // Get stack/heap info from HW layer if available
        char buf[64];
        
        // Show shared region allocations
        write("Shared Regions:\r\n");
        write("  ID  Name          Base        Size\r\n");
        write("  --  ------------  ----------  ------\r\n");
        
        // Note: In a real implementation, kernel would expose shared regions
        write("  (Use kernel.get_shared() to query)\r\n\r\n");
        
        // Estimate free stack (if HW provides it)
        if constexpr (requires { HW::get_free_stack(); }) {
            auto free_stack = HW::get_free_stack();
            snprintf(buf, sizeof(buf), "Free Stack: %u bytes\r\n", 
                     static_cast<unsigned>(free_stack));
            write(buf);
        }
        
        // Estimate free heap (if HW provides it)
        if constexpr (requires { HW::get_free_heap(); }) {
            auto free_heap = HW::get_free_heap();
            snprintf(buf, sizeof(buf), "Free Heap:  %u bytes\r\n",
                     static_cast<unsigned>(free_heap));
            write(buf);
        }
    }

    void cmd_load() {
        char buf[64];
        
        // CPU load estimation (if kernel tracks it)
        if constexpr (requires { kernel.get_cpu_load(); }) {
            auto load = kernel.get_cpu_load();
            snprintf(buf, sizeof(buf), "CPU Load: %u%%\r\n",
                     static_cast<unsigned>(load));
            write(buf);
        } else {
            // Simple uptime
            auto time_us = kernel.time();
            auto secs = time_us / 1'000'000;
            auto mins = secs / 60;
            secs %= 60;
            snprintf(buf, sizeof(buf), "Uptime: %um %us\r\n",
                     static_cast<unsigned>(mins),
                     static_cast<unsigned>(secs));
            write(buf);
        }
        
        // Task count
        unsigned task_count = 0;
        kernel.for_each_task([&task_count](auto, auto&, auto) {
            task_count++;
        });
        snprintf(buf, sizeof(buf), "Active Tasks: %u\r\n", task_count);
        write(buf);
    }

    void cmd_led(const char* arg) {
        // Parse: led <n> <on|off|toggle>
        if (!arg || !*arg) {
            // Show LED state
            if constexpr (requires { HW::get_led_state(); }) {
                char buf[32];
                uint8_t state = HW::get_led_state();
                snprintf(buf, sizeof(buf), "LEDs: %c%c%c%c\r\n",
                         (state & 1) ? '1' : '0',
                         (state & 2) ? '1' : '0',
                         (state & 4) ? '1' : '0',
                         (state & 8) ? '1' : '0');
                write(buf);
            } else {
                write("LED control not available\r\n");
            }
            return;
        }

        // Parse LED number
        int led = arg[0] - '0';
        if (led < 0 || led > 3) {
            write("Invalid LED (0-3)\r\n");
            return;
        }

        // Skip to action
        const char* action = arg + 1;
        while (*action == ' ') action++;

        if constexpr (requires { HW::set_led(0, true); }) {
            if (strcmp(action, "on") == 0 || strcmp(action, "1") == 0) {
                HW::set_led(static_cast<uint8_t>(led), true);
                write("OK\r\n");
            } else if (strcmp(action, "off") == 0 || strcmp(action, "0") == 0) {
                HW::set_led(static_cast<uint8_t>(led), false);
                write("OK\r\n");
            } else if (strcmp(action, "toggle") == 0 || strcmp(action, "t") == 0) {
                HW::toggle_led(static_cast<uint8_t>(led));
                write("OK\r\n");
            } else {
                write("Usage: led <0-3> <on|off|toggle>\r\n");
            }
        } else {
            write("LED control not available\r\n");
        }
    }

    void cmd_audio() {
        char buf[64];

        if constexpr (requires { HW::get_audio_stats(); }) {
            auto stats = HW::get_audio_stats();
            snprintf(buf, sizeof(buf), "Sample Rate: %u Hz\r\n",
                     static_cast<unsigned>(stats.sample_rate));
            write(buf);
            snprintf(buf, sizeof(buf), "Buffer: %u/%u frames\r\n",
                     static_cast<unsigned>(stats.buffered),
                     static_cast<unsigned>(stats.capacity));
            write(buf);
            snprintf(buf, sizeof(buf), "Underruns: %u\r\n",
                     static_cast<unsigned>(stats.underruns));
            write(buf);
            snprintf(buf, sizeof(buf), "Overruns: %u\r\n",
                     static_cast<unsigned>(stats.overruns));
            write(buf);
        } else {
            write("Audio stats not available\r\n");
        }
    }

    void cmd_midi() {
        char buf[64];

        if constexpr (requires { HW::get_midi_stats(); }) {
            auto stats = HW::get_midi_stats();
            snprintf(buf, sizeof(buf), "RX: %u messages\r\n",
                     static_cast<unsigned>(stats.rx_count));
            write(buf);
            snprintf(buf, sizeof(buf), "TX: %u messages\r\n",
                     static_cast<unsigned>(stats.tx_count));
            write(buf);
        } else {
            write("MIDI stats not available\r\n");
        }
    }

    void cmd_status() {
        char buf[64];

        // Platform info
        if constexpr (requires { HW::get_platform_name(); }) {
            snprintf(buf, sizeof(buf), "Platform: %s\r\n", HW::get_platform_name());
            write(buf);
        }

        // Uptime
        auto time_us = kernel.time();
        auto secs = time_us / 1'000'000;
        auto mins = secs / 60;
        auto hours = mins / 60;
        mins %= 60;
        secs %= 60;
        snprintf(buf, sizeof(buf), "Uptime: %uh %um %us\r\n",
                 static_cast<unsigned>(hours),
                 static_cast<unsigned>(mins),
                 static_cast<unsigned>(secs));
        write(buf);

        // Task count
        unsigned task_count = 0;
        kernel.for_each_task([&task_count](auto, auto&, auto) {
            task_count++;
        });
        snprintf(buf, sizeof(buf), "Tasks: %u\r\n", task_count);
        write(buf);

        // LED state
        if constexpr (requires { HW::get_led_state(); }) {
            uint8_t led = HW::get_led_state();
            snprintf(buf, sizeof(buf), "LEDs: 0x%02X\r\n", led);
            write(buf);
        }
    }

    void cmd_reboot() {
        write("Rebooting...\r\n");
        HW::system_reset();
    }
};

} // namespace umi
