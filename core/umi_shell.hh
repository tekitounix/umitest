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

    /// Process a single character from UART input.
    /// Call this from UART RX interrupt or polling loop.
    void process_char(char c) {
        if (c == '\r' || c == '\n') {
            write("\r\n");
            if (line_pos > 0) {
                execute_command();
            }
            clear_line();
            print_prompt();
        } else if (c == '\b' || c == 0x7F) {
            // Backspace
            if (line_pos > 0) {
                line_pos--;
                write("\b \b");
            }
        } else if (c >= ' ' && c < 0x7F && line_pos < sizeof(line) - 1) {
            line[line_pos++] = c;
            char echo[2] = {c, 0};
            write(echo);
        }
    }

    /// Print initial prompt
    void start() {
        write("\r\n");
        write("UMI-OS Shell v0.1\r\n");
        write("Type 'help' for available commands.\r\n");
        print_prompt();
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

    void print_prompt() {
        write("umi> ");
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
        write("  ps      - List tasks\r\n");
        write("  mem     - Show memory usage\r\n");
        write("  load    - Show CPU load\r\n");
        write("  reboot  - System reset\r\n");
        write("  help    - Show this help\r\n");
    }

    void cmd_ps() {
        write("ID  Name            Prio     State\r\n");
        write("--  --------------  -------  -------\r\n");
        
        kernel.for_each_task([this](auto id, auto& cfg, auto state) {
            char buf[64];
            const char* prio_str = "?";
            using Priority = typename Kernel::Priority;
            switch (cfg.prio) {
                case Priority::Realtime: prio_str = "RT"; break;
                case Priority::User:     prio_str = "User"; break;
                case Priority::Idle:     prio_str = "Idle"; break;
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

    void cmd_reboot() {
        write("Rebooting...\r\n");
        HW::system_reset();
    }
};

} // namespace umi
