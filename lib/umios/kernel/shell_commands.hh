// =====================================================================
// UMI-OS Shell Commands
// =====================================================================
//
// Shell command implementations for UMI-OS.
// Uses umishell library for parsing and output formatting.
// Kernel state access is provided via template parameter.
//
// =====================================================================

#pragma once

#include <umishell/shell_core.hh>
#include <umishell/shell_auth.hh>
#include <cstdint>
#include <cstring>

namespace umi::os {

// ============================================================================
// Kernel State Interface
// ============================================================================

/// Interface for kernel state access (implemented by each backend)
struct KernelStateView {
    // Time
    uint64_t uptime_us = 0;
    uint32_t rtc_epoch_sec = 0;

    // Audio
    uint32_t audio_buffer_count = 0;
    uint32_t audio_drop_count = 0;
    uint32_t dsp_load_percent_x100 = 0;
    uint32_t dsp_load_peak_x100 = 0;
    bool audio_running = false;

    // MIDI
    uint32_t midi_rx_count = 0;
    uint32_t midi_tx_count = 0;

    // Power
    uint8_t battery_percent = 100;
    bool battery_charging = false;
    bool usb_connected = true;
    uint32_t battery_voltage_mv = 4200;

    // Watchdog
    uint32_t watchdog_timeout_ms = 0;
    uint64_t watchdog_last_feed_us = 0;
    bool watchdog_enabled = false;

    // Counters
    uint32_t irq_count = 0;
    uint32_t context_switches = 0;
    uint32_t hardfault_count = 0;
    uint32_t stack_overflow_count = 0;

    // Memory
    uint32_t heap_used = 0;
    uint32_t heap_total = 0;
    uint32_t heap_peak = 0;
    uint32_t sram_total = 0;
    uint32_t flash_total = 0;
    uint32_t flash_used = 0;

    // Tasks
    uint8_t task_count = 0;
    uint8_t task_ready = 0;
    uint8_t task_blocked = 0;

    // Logging
    uint8_t log_level = 2;
};

/// Shell configuration (user-settable)
struct ShellConfig {
    // MIDI settings
    uint8_t midi_channel = 1;      // 1-16
    int8_t midi_transpose = 0;     // -24 to +24

    // Audio settings
    uint8_t audio_gain = 80;       // 0-100
    uint32_t sample_rate = 48000;

    // Power settings
    uint16_t sleep_timeout_min = 5;
    uint8_t low_battery_threshold = 15;
    uint8_t shutdown_threshold = 5;

    // MIDI monitor
    bool midi_monitor_enabled = false;

    // Factory info
    const char* platform_name = "Unknown";
    char serial_number[20] = "UNSET";
    uint32_t manufacture_date = 0;
    bool factory_locked = false;
};

/// System mode for firmware updates etc.
enum class SystemMode : uint8_t {
    NORMAL = 0,
    DFU = 1,           // Device Firmware Update
    BOOTLOADER = 2,
    SAFE = 3
};

// ============================================================================
// Error Log
// ============================================================================

struct ErrorLogEntry {
    uint64_t timestamp_us = 0;
    uint8_t severity = 0;     // 0=info, 1=warning, 2=error, 3=critical
    char message[64] = {0};
};

template<size_t MaxEntries = 16>
class ErrorLog {
public:
    void add(uint8_t severity, uint64_t timestamp_us, const char* msg) {
        ErrorLogEntry& entry = entries_[head_];
        entry.timestamp_us = timestamp_us;
        entry.severity = severity;

        size_t idx = 0;
        while (msg[idx] != '\0' && idx < sizeof(entry.message) - 1) {
            entry.message[idx] = msg[idx];
            idx++;
        }
        entry.message[idx] = '\0';

        head_ = (head_ + 1) % MaxEntries;
        if (count_ < MaxEntries) {
            count_++;
        }
    }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

    [[nodiscard]] size_t count() const { return count_; }

    [[nodiscard]] const ErrorLogEntry& get(size_t idx) const {
        size_t actual = (head_ + MaxEntries - count_ + idx) % MaxEntries;
        return entries_[actual];
    }

private:
    ErrorLogEntry entries_[MaxEntries]{};
    size_t head_ = 0;
    size_t count_ = 0;
};

// ============================================================================
// Shell Commands - Template based for backend flexibility
// ============================================================================

/// Shell command processor
/// StateProvider must implement:
///   - KernelStateView& state()
///   - ShellConfig& config()
///   - ErrorLog<>& error_log()
///   - SystemMode& system_mode()
///   - void reset_system()
///   - void feed_watchdog()
///   - void enable_watchdog(bool)
template<typename StateProvider, size_t OutputBufSize = 2048>
class ShellCommands {
public:
    using Output = shell::OutputBuffer<OutputBufSize>;

    explicit ShellCommands(StateProvider& provider)
        : provider_(provider) {}

    /// Execute a command and return output
    const char* execute(const char* input) {
        shell::ParsedCommand cmd;
        cmd.parse(input);

        // Update auth state
        auth_.touch(provider_.state().uptime_us);
        auth_.check_timeout(provider_.state().uptime_us);

        out_.clear();

        // Dispatch command
        if (cmd.is_command("help") || cmd.is_command("?")) {
            return cmd_help();
        }
        if (cmd.is_command("version")) {
            return "UMI-OS v2.0.0";
        }
        if (cmd.is_command("uptime")) {
            return cmd_uptime();
        }
        if (cmd.is_command("whoami")) {
            return cmd_whoami();
        }
        if (cmd.is_command("auth")) {
            return cmd_auth(cmd);
        }
        if (cmd.is_command("logout")) {
            auth_.logout();
            return "Logged out. Access level: User";
        }

        // Show commands
        if (cmd.is_command("show")) {
            return cmd_show(cmd);
        }

        // Config commands (ADMIN+)
        if (cmd.is_command("config")) {
            if (!auth_.has_access(shell::AccessLevel::ADMIN)) {
                return "ERROR: ADMIN access required";
            }
            return cmd_config(cmd);
        }

        // MIDI commands
        if (cmd.is_command("midi")) {
            return cmd_midi(cmd);
        }

        // Diag commands (ADMIN+)
        if (cmd.is_command("diag")) {
            if (!auth_.has_access(shell::AccessLevel::ADMIN)) {
                return "ERROR: ADMIN access required";
            }
            return cmd_diag(cmd);
        }

        // Factory commands (FACTORY only)
        if (cmd.is_command("factory")) {
            if (!auth_.has_access(shell::AccessLevel::FACTORY)) {
                return "ERROR: FACTORY access required";
            }
            return cmd_factory(cmd);
        }

        // Mode commands
        if (cmd.is_command("mode")) {
            return cmd_mode(cmd);
        }

        // Legacy aliases
        if (cmd.is_command("status")) {
            return cmd_show_system();
        }
        if (cmd.is_command("mem")) {
            return cmd_show_memory();
        }
        if (cmd.is_command("tasks")) {
            return cmd_show_tasks();
        }
        if (cmd.is_command("reset")) {
            if (!auth_.has_access(shell::AccessLevel::ADMIN)) {
                return "ERROR: ADMIN access required";
            }
            return "RESET_REQUESTED";
        }

        if (cmd.argc > 0) {
            return "Unknown command. Type 'help' for commands.";
        }
        return "";
    }

    /// Get current access level
    [[nodiscard]] shell::AccessLevel access_level() const {
        return auth_.level();
    }

private:
    StateProvider& provider_;
    shell::AuthState auth_;
    Output out_;

    // ========== Help ==========

    const char* cmd_help() {
        out_.put_str("UMI-OS Shell v2.0\n");
        out_.put_str("================\n");
        out_.put_str("Basic commands:\n");
        out_.put_str("  help          - Show help\n");
        out_.put_str("  version       - Show version\n");
        out_.put_str("  uptime        - Show uptime\n");
        out_.put_str("  whoami        - Show access level\n");
        out_.put_str("  auth <lvl> <pw> - Authenticate\n");
        out_.put_str("  logout        - Return to USER\n");
        out_.put_line();
        out_.put_str("Show commands:\n");
        out_.put_str("  show system   - System overview\n");
        out_.put_str("  show cpu      - CPU load\n");
        out_.put_str("  show memory   - Memory usage\n");
        out_.put_str("  show tasks    - Task list\n");
        out_.put_str("  show audio    - Audio status\n");
        out_.put_str("  show midi     - MIDI status\n");
        out_.put_str("  show battery  - Battery status\n");
        out_.put_str("  show power    - Power management\n");
        out_.put_str("  show usb      - USB status\n");
        out_.put_str("  show errors   - Error log\n");
        out_.put_str("  show config   - Current settings\n");
        out_.put_line();
        out_.put_str("Mode commands:\n");
        out_.put_str("  mode          - Show current mode\n");
        out_.put_str("  mode <name>   - Switch mode (ADMIN)\n");

        if (auth_.has_access(shell::AccessLevel::ADMIN)) {
            out_.put_line();
            out_.put_str("Config commands (ADMIN):\n");
            out_.put_str("  config midi channel <1-16>\n");
            out_.put_str("  config midi transpose <-24..24>\n");
            out_.put_str("  config audio gain <0-100>\n");
            out_.put_str("  config power sleep <min>\n");
            out_.put_str("  config save / config reset\n");
            out_.put_line();
            out_.put_str("Diag commands (ADMIN):\n");
            out_.put_str("  diag watchdog [feed|enable|disable]\n");
            out_.put_str("  diag reset [soft|hard]\n");
        }

        if (auth_.has_access(shell::AccessLevel::FACTORY)) {
            out_.put_line();
            out_.put_str("Factory commands (FACTORY):\n");
            out_.put_str("  factory info\n");
            out_.put_str("  factory serial [set <sn>|clear]\n");
            out_.put_str("  factory test <all|...>\n");
            out_.put_str("  factory lock\n");
        }

        return out_.c_str();
    }

    // ========== Basic Commands ==========

    const char* cmd_uptime() {
        auto& st = provider_.state();
        uint32_t secs = static_cast<uint32_t>(st.uptime_us / 1000000);
        uint32_t mins = secs / 60;
        uint32_t hours = mins / 60;
        uint32_t days = hours / 24;

        out_.put_str("Uptime: ");
        if (days > 0) {
            out_.put_num(days);
            out_.put_str("d ");
        }
        out_.put_num2(hours % 24);
        out_.put_char(':');
        out_.put_num2(mins % 60);
        out_.put_char(':');
        out_.put_num2(secs % 60);
        return out_.c_str();
    }

    const char* cmd_whoami() {
        out_.put_str("Access level: ");
        switch (auth_.level()) {
            case shell::AccessLevel::USER: out_.put_str("USER"); break;
            case shell::AccessLevel::ADMIN: out_.put_str("ADMIN"); break;
            case shell::AccessLevel::FACTORY: out_.put_str("FACTORY"); break;
        }
        return out_.c_str();
    }

    const char* cmd_auth(const shell::ParsedCommand& cmd) {
        if (cmd.argc < 3) {
            return "Usage: auth <admin|factory> <password>";
        }

        uint64_t now = provider_.state().uptime_us;
        if (auth_.is_locked_out(now)) {
            out_.put_str("ERROR: Locked out. Wait ");
            out_.put_num(auth_.lockout_remaining_sec(now));
            out_.put_str(" seconds.");
            return out_.c_str();
        }

        shell::AccessLevel target = shell::AccessLevel::USER;
        if (std::strcmp(cmd.arg(1), "admin") == 0) {
            target = shell::AccessLevel::ADMIN;
        } else if (std::strcmp(cmd.arg(1), "factory") == 0) {
            target = shell::AccessLevel::FACTORY;
        } else {
            return "Usage: auth <admin|factory> <password>";
        }

        if (auth_.authenticate(target, cmd.arg(2), now,
                               shell::simple_password_check, nullptr)) {
            out_.put_str("Authenticated as ");
            out_.put_str(target == shell::AccessLevel::ADMIN ? "ADMIN" : "FACTORY");
            return out_.c_str();
        }
        return "ERROR: Authentication failed";
    }

    // ========== Show Commands ==========

    const char* cmd_show(const shell::ParsedCommand& cmd) {
        if (cmd.argc < 2) {
            return "Usage: show <system|cpu|memory|tasks|audio|midi|battery|power|usb|errors|config>";
        }

        const char* sub = cmd.arg(1);
        if (std::strcmp(sub, "system") == 0) return cmd_show_system();
        if (std::strcmp(sub, "cpu") == 0) return cmd_show_cpu();
        if (std::strcmp(sub, "memory") == 0 || std::strcmp(sub, "mem") == 0) return cmd_show_memory();
        if (std::strcmp(sub, "tasks") == 0) return cmd_show_tasks();
        if (std::strcmp(sub, "audio") == 0) return cmd_show_audio();
        if (std::strcmp(sub, "midi") == 0) return cmd_show_midi();
        if (std::strcmp(sub, "battery") == 0 || std::strcmp(sub, "bat") == 0) return cmd_show_battery();
        if (std::strcmp(sub, "power") == 0) return cmd_show_power();
        if (std::strcmp(sub, "usb") == 0) return cmd_show_usb();
        if (std::strcmp(sub, "errors") == 0 || std::strcmp(sub, "err") == 0) return cmd_show_errors();
        if (std::strcmp(sub, "config") == 0) return cmd_show_config();
        if (std::strcmp(sub, "mode") == 0) return cmd_show_mode();

        return "Unknown show target";
    }

    const char* cmd_show_system() {
        auto& st = provider_.state();
        auto& cfg = provider_.config();

        out_.put_str("System Information\n");
        out_.put_str("==================\n");
        out_.put_kv("Platform", cfg.platform_name ? cfg.platform_name : "Unknown");
        out_.put_kv("Version", "UMI-OS v2.0.0");
        out_.put_kv("Serial", cfg.serial_number);
        out_.put_str("  Uptime: ");
        uint32_t secs = static_cast<uint32_t>(st.uptime_us / 1000000);
        uint32_t mins = secs / 60;
        uint32_t hours = mins / 60;
        out_.put_num(hours);
        out_.put_str("h ");
        out_.put_num(mins % 60);
        out_.put_str("m\n");
        out_.put_kv_num("Tasks", st.task_count);
        out_.put_kv_num("Ctx switches", st.context_switches);
        out_.put_kv_num("Log level", st.log_level);
        return out_.c_str();
    }

    const char* cmd_show_cpu() {
        auto& st = provider_.state();

        out_.put_str("CPU Status\n");
        out_.put_str("==========\n");
        out_.put_str("  DSP load: ");
        out_.put_num(st.dsp_load_percent_x100 / 100);
        out_.put_char('.');
        out_.put_num2(st.dsp_load_percent_x100 % 100);
        out_.put_str("%\n");
        out_.put_str("  DSP peak: ");
        out_.put_num(st.dsp_load_peak_x100 / 100);
        out_.put_char('.');
        out_.put_num2(st.dsp_load_peak_x100 % 100);
        out_.put_str("%\n");
        out_.put_kv_num("IRQ count", st.irq_count);
        return out_.c_str();
    }

    const char* cmd_show_memory() {
        auto& st = provider_.state();

        out_.put_str("Memory Status\n");
        out_.put_str("=============\n");
        out_.put_str("Heap:\n");
        out_.put_str("  Used: ");
        out_.put_num(st.heap_used);
        out_.put_char('/');
        out_.put_num(st.heap_total);
        out_.put_str(" bytes\n");
        out_.put_str("  Peak: ");
        out_.put_num(st.heap_peak);
        out_.put_str(" bytes\n");
        uint32_t heap_pct = st.heap_total > 0 ?
            (st.heap_used * 100 / st.heap_total) : 0;
        out_.put_str("  Usage: ");
        out_.put_num(heap_pct);
        out_.put_str("%\n");
        out_.put_kv_num("SRAM Total", st.sram_total / 1024, "KB");
        out_.put_str("Flash:\n");
        out_.put_str("  Used: ");
        out_.put_num(st.flash_used / 1024);
        out_.put_char('/');
        out_.put_num(st.flash_total / 1024);
        out_.put_str(" KB\n");
        return out_.c_str();
    }

    const char* cmd_show_tasks() {
        auto& st = provider_.state();

        out_.put_str("Task List\n");
        out_.put_str("=========\n");
        out_.put_str("  Total: ");
        out_.put_num(st.task_count);
        out_.put_str(" (");
        out_.put_num(st.task_ready);
        out_.put_str("R/");
        out_.put_num(st.task_blocked);
        out_.put_str("B)\n");
        return out_.c_str();
    }

    const char* cmd_show_audio() {
        auto& st = provider_.state();
        auto& cfg = provider_.config();

        out_.put_str("Audio Status\n");
        out_.put_str("============\n");
        out_.put_kv("State", st.audio_running ? "Running" : "Stopped");
        out_.put_kv_num("Sample rate", cfg.sample_rate, "Hz");
        out_.put_kv_num("Buffers", st.audio_buffer_count);
        out_.put_kv_num("Drops", st.audio_drop_count);
        out_.put_str("  DSP load: ");
        out_.put_num(st.dsp_load_percent_x100 / 100);
        out_.put_str("%\n");
        out_.put_kv_num("Output gain", cfg.audio_gain, "%");
        return out_.c_str();
    }

    const char* cmd_show_midi() {
        auto& st = provider_.state();
        auto& cfg = provider_.config();

        out_.put_str("MIDI Status\n");
        out_.put_str("===========\n");
        out_.put_kv_num("Channel", cfg.midi_channel);
        out_.put_str("  Transpose: ");
        out_.put_num_signed(cfg.midi_transpose);
        out_.put_line();
        out_.put_kv_num("RX count", st.midi_rx_count);
        out_.put_kv_num("TX count", st.midi_tx_count);
        out_.put_kv("Monitor", cfg.midi_monitor_enabled ? "ON" : "OFF");
        return out_.c_str();
    }

    const char* cmd_show_battery() {
        auto& st = provider_.state();

        out_.put_str("Battery Status\n");
        out_.put_str("==============\n");
        out_.put_str("  Voltage: ");
        out_.put_num(st.battery_voltage_mv / 1000);
        out_.put_char('.');
        out_.put_num2((st.battery_voltage_mv % 1000) / 10);
        out_.put_str(" V\n");
        out_.put_kv_num("Capacity", st.battery_percent, "%");
        out_.put_kv("State", st.battery_charging ? "Charging" : "Discharging");
        return out_.c_str();
    }

    const char* cmd_show_power() {
        auto& st = provider_.state();
        auto& cfg = provider_.config();

        out_.put_str("Power Management\n");
        out_.put_str("================\n");
        out_.put_kv("Mode", "Normal");
        out_.put_str("  Sleep timeout: ");
        if (cfg.sleep_timeout_min > 0) {
            out_.put_num(cfg.sleep_timeout_min);
            out_.put_str(" min\n");
        } else {
            out_.put_str("Disabled\n");
        }
        out_.put_kv_num("Low battery", cfg.low_battery_threshold, "%");
        out_.put_kv_num("Shutdown", cfg.shutdown_threshold, "%");
        out_.put_kv("USB", st.usb_connected ? "Connected" : "Disconnected");
        return out_.c_str();
    }

    const char* cmd_show_usb() {
        auto& st = provider_.state();

        out_.put_str("USB Status\n");
        out_.put_str("==========\n");
        out_.put_kv("State", st.usb_connected ? "Connected" : "Disconnected");
        out_.put_kv("Mode", "Device");
        if (st.usb_connected) {
            out_.put_kv("Speed", "Full Speed (12 Mbps)");
            out_.put_kv("Class", "Audio/MIDI Composite");
        }
        out_.put_str("Endpoints:\n");
        out_.put_str("  MIDI IN:  Enabled\n");
        out_.put_str("  MIDI OUT: Enabled\n");
        out_.put_str("  Audio OUT: Enabled\n");
        return out_.c_str();
    }

    const char* cmd_show_errors() {
        auto& log = provider_.error_log();

        out_.put_str("Error Log\n");
        out_.put_str("=========\n");
        size_t count = log.count();
        if (count == 0) {
            out_.put_str("No errors recorded.\n");
            return out_.c_str();
        }
        out_.put_str("Last ");
        out_.put_num(static_cast<uint32_t>(count));
        out_.put_str(" entries:\n\n");

        for (size_t idx = 0; idx < count; ++idx) {
            const auto& entry = log.get(idx);
            auto secs = static_cast<uint32_t>(entry.timestamp_us / 1000000);
            out_.put_char('[');
            out_.put_num(secs);
            out_.put_str("s] ");
            switch (entry.severity) {
                case 0: out_.put_str("[INFO] "); break;
                case 1: out_.put_str("[WARN] "); break;
                case 2: out_.put_str("[ERR]  "); break;
                case 3: out_.put_str("[CRIT] "); break;
                default: out_.put_str("[???]  "); break;
            }
            out_.put_str(entry.message);
            out_.put_line();
        }
        return out_.c_str();
    }

    const char* cmd_show_config() {
        auto& cfg = provider_.config();
        auto& st = provider_.state();

        out_.put_str("Current Configuration\n");
        out_.put_str("=====================\n");
        out_.put_str("MIDI:\n");
        out_.put_kv_num("  Channel", cfg.midi_channel);
        out_.put_str("  Transpose: ");
        out_.put_num_signed(cfg.midi_transpose);
        out_.put_line();
        out_.put_str("Audio:\n");
        out_.put_kv_num("  Gain", cfg.audio_gain, "%");
        out_.put_kv_num("  Sample rate", cfg.sample_rate, "Hz");
        out_.put_str("Power:\n");
        out_.put_kv_num("  Sleep timeout", cfg.sleep_timeout_min, "min");
        out_.put_kv_num("  Low battery", cfg.low_battery_threshold, "%");
        out_.put_str("Logging:\n");
        out_.put_kv_num("  Level", st.log_level);
        return out_.c_str();
    }

    const char* cmd_show_mode() {
        auto& mode = provider_.system_mode();

        out_.put_str("System Mode\n");
        out_.put_str("===========\n");
        out_.put_str("  Current: ");
        switch (mode) {
            case SystemMode::NORMAL: out_.put_str("NORMAL"); break;
            case SystemMode::DFU: out_.put_str("DFU (Firmware Update)"); break;
            case SystemMode::BOOTLOADER: out_.put_str("BOOTLOADER"); break;
            case SystemMode::SAFE: out_.put_str("SAFE MODE"); break;
        }
        out_.put_line();
        out_.put_str("\nAvailable modes:\n");
        out_.put_str("  normal     - Normal operation\n");
        out_.put_str("  dfu        - Device Firmware Update\n");
        out_.put_str("  bootloader - Bootloader mode\n");
        out_.put_str("  safe       - Safe mode (minimal)\n");
        return out_.c_str();
    }

    // ========== Config Commands ==========

    const char* cmd_config(const shell::ParsedCommand& cmd) {
        if (cmd.argc < 3) {
            return "Usage: config <midi|audio|power|log> <setting> <value>";
        }

        auto& cfg = provider_.config();
        const char* category = cmd.arg(1);
        const char* setting = cmd.arg(2);

        // config midi channel <1-16>
        if (std::strcmp(category, "midi") == 0 && std::strcmp(setting, "channel") == 0) {
            if (cmd.argc < 4) return "Usage: config midi channel <1-16>";
            bool ok = false;
            int32_t val = shell::parse_int(cmd.arg(3), &ok);
            if (ok && val >= 1 && val <= 16) {
                cfg.midi_channel = static_cast<uint8_t>(val);
                out_.put_str("MIDI channel set to ");
                out_.put_num(val);
                return out_.c_str();
            }
            return "ERROR: Channel must be 1-16";
        }

        // config midi transpose <-24..24>
        if (std::strcmp(category, "midi") == 0 && std::strcmp(setting, "transpose") == 0) {
            if (cmd.argc < 4) return "Usage: config midi transpose <-24..24>";
            bool ok = false;
            int32_t val = shell::parse_int(cmd.arg(3), &ok);
            if (ok && val >= -24 && val <= 24) {
                cfg.midi_transpose = static_cast<int8_t>(val);
                out_.put_str("Transpose set to ");
                out_.put_num_signed(val);
                return out_.c_str();
            }
            return "ERROR: Transpose must be -24 to +24";
        }

        // config audio gain <0-100>
        if (std::strcmp(category, "audio") == 0 && std::strcmp(setting, "gain") == 0) {
            if (cmd.argc < 4) return "Usage: config audio gain <0-100>";
            bool ok = false;
            int32_t val = shell::parse_int(cmd.arg(3), &ok);
            if (ok && val >= 0 && val <= 100) {
                cfg.audio_gain = static_cast<uint8_t>(val);
                out_.put_str("Audio gain set to ");
                out_.put_num(val);
                out_.put_str("%");
                return out_.c_str();
            }
            return "ERROR: Gain must be 0-100";
        }

        // config power sleep <minutes>
        if (std::strcmp(category, "power") == 0 && std::strcmp(setting, "sleep") == 0) {
            if (cmd.argc < 4) return "Usage: config power sleep <0-60>";
            bool ok = false;
            int32_t val = shell::parse_int(cmd.arg(3), &ok);
            if (ok && val >= 0 && val <= 60) {
                cfg.sleep_timeout_min = static_cast<uint16_t>(val);
                if (val > 0) {
                    out_.put_str("Sleep timeout set to ");
                    out_.put_num(val);
                    out_.put_str(" min");
                } else {
                    out_.put_str("Sleep timeout disabled");
                }
                return out_.c_str();
            }
            return "ERROR: Sleep timeout must be 0-60";
        }

        // config save / config reset
        if (std::strcmp(category, "save") == 0) {
            return "Configuration saved";
        }
        if (std::strcmp(category, "reset") == 0) {
            cfg = ShellConfig{};
            return "Configuration reset to defaults";
        }

        return "Unknown config option";
    }

    // ========== MIDI Commands ==========

    const char* cmd_midi(const shell::ParsedCommand& cmd) {
        auto& cfg = provider_.config();

        if (cmd.argc < 2 || std::strcmp(cmd.arg(1), "status") == 0) {
            return cmd_show_midi();
        }

        if (std::strcmp(cmd.arg(1), "monitor") == 0) {
            if (cmd.argc >= 3) {
                if (std::strcmp(cmd.arg(2), "on") == 0) {
                    cfg.midi_monitor_enabled = true;
                } else if (std::strcmp(cmd.arg(2), "off") == 0) {
                    cfg.midi_monitor_enabled = false;
                }
            } else {
                cfg.midi_monitor_enabled = !cfg.midi_monitor_enabled;
            }
            return cfg.midi_monitor_enabled ? "MIDI monitor enabled" : "MIDI monitor disabled";
        }

        if (std::strcmp(cmd.arg(1), "panic") == 0) {
            return "All notes off sent";
        }

        return "Usage: midi <status|monitor|panic>";
    }

    // ========== Diag Commands ==========

    const char* cmd_diag(const shell::ParsedCommand& cmd) {
        if (cmd.argc < 2) {
            return "Usage: diag <watchdog|reset>";
        }

        const char* sub = cmd.arg(1);

        if (std::strcmp(sub, "watchdog") == 0 || std::strcmp(sub, "wdt") == 0) {
            if (cmd.argc >= 3) {
                if (std::strcmp(cmd.arg(2), "feed") == 0) {
                    provider_.feed_watchdog();
                    return "Watchdog fed";
                }
                if (std::strcmp(cmd.arg(2), "enable") == 0) {
                    provider_.enable_watchdog(true);
                    return "Watchdog enabled";
                }
                if (std::strcmp(cmd.arg(2), "disable") == 0) {
                    provider_.enable_watchdog(false);
                    return "Watchdog disabled";
                }
            }
            // Show status
            auto& st = provider_.state();
            out_.put_str("Watchdog Status\n");
            out_.put_str("===============\n");
            out_.put_kv("State", st.watchdog_enabled ? "Enabled" : "Disabled");
            if (st.watchdog_enabled) {
                out_.put_kv_num("Timeout", st.watchdog_timeout_ms, "ms");
            }
            return out_.c_str();
        }

        if (std::strcmp(sub, "reset") == 0) {
            return "RESET_REQUESTED";
        }

        return "Unknown diag command";
    }

    // ========== Factory Commands ==========

    const char* cmd_factory(const shell::ParsedCommand& cmd) {
        auto& cfg = provider_.config();

        if (cmd.argc < 2) {
            return "Usage: factory <info|serial|test|lock>";
        }

        const char* sub = cmd.arg(1);

        if (std::strcmp(sub, "info") == 0) {
            out_.put_str("Factory Information\n");
            out_.put_str("===================\n");
            out_.put_kv("Serial", cfg.serial_number);
            out_.put_kv_num("Manufacture date", cfg.manufacture_date);
            out_.put_kv("Locked", cfg.factory_locked ? "Yes" : "No");
            return out_.c_str();
        }

        if (std::strcmp(sub, "serial") == 0) {
            if (cfg.factory_locked) {
                return "ERROR: Device is factory locked";
            }
            if (cmd.argc >= 4 && std::strcmp(cmd.arg(2), "set") == 0) {
                const char* sn = cmd.arg(3);
                size_t idx = 0;
                while (sn[idx] != '\0' && idx < sizeof(cfg.serial_number) - 1) {
                    cfg.serial_number[idx] = sn[idx];
                    idx++;
                }
                cfg.serial_number[idx] = '\0';
                out_.put_str("Serial number set to: ");
                out_.put_str(cfg.serial_number);
                return out_.c_str();
            }
            if (cmd.argc >= 3 && std::strcmp(cmd.arg(2), "clear") == 0) {
                std::strcpy(cfg.serial_number, "UNSET");
                return "Serial number cleared";
            }
            return "Usage: factory serial <set <sn>|clear>";
        }

        if (std::strcmp(sub, "test") == 0) {
            if (cfg.factory_locked) {
                return "ERROR: Device is factory locked";
            }
            out_.put_str("Factory Test Results\n");
            out_.put_str("====================\n");
            out_.put_str("[PASS] All tests passed (simulated)\n");
            return out_.c_str();
        }

        if (std::strcmp(sub, "lock") == 0) {
            if (cfg.factory_locked) {
                return "Device is already locked";
            }
            cfg.factory_locked = true;
            return "WARNING: Device is now factory locked. This cannot be undone.";
        }

        return "Unknown factory command";
    }

    // ========== Mode Commands ==========

    const char* cmd_mode(const shell::ParsedCommand& cmd) {
        auto& mode = provider_.system_mode();

        if (cmd.argc < 2) {
            return cmd_show_mode();
        }

        if (!auth_.has_access(shell::AccessLevel::ADMIN)) {
            return "ERROR: ADMIN access required to change mode";
        }

        const char* target = cmd.arg(1);

        if (std::strcmp(target, "normal") == 0) {
            mode = SystemMode::NORMAL;
            return "Mode set to NORMAL";
        }
        if (std::strcmp(target, "dfu") == 0) {
            mode = SystemMode::DFU;
            out_.put_str("Mode set to DFU.\n");
            out_.put_str("Device is ready for firmware update.\n");
            out_.put_str("Use 'mode normal' to exit DFU mode.");
            return out_.c_str();
        }
        if (std::strcmp(target, "bootloader") == 0 || std::strcmp(target, "boot") == 0) {
            mode = SystemMode::BOOTLOADER;
            out_.put_str("Mode set to BOOTLOADER.\n");
            out_.put_str("Use 'mode normal' to return to normal mode.");
            return out_.c_str();
        }
        if (std::strcmp(target, "safe") == 0) {
            mode = SystemMode::SAFE;
            out_.put_str("Mode set to SAFE.\n");
            out_.put_str("Running with minimal features.\n");
            out_.put_str("Use 'mode normal' to return to normal operation.");
            return out_.c_str();
        }

        return "Usage: mode [normal|dfu|bootloader|safe]";
    }
};

}  // namespace umi::os
