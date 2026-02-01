// =====================================================================
// UMI Shell Core - Platform-independent shell primitives
// =====================================================================
//
// Provides:
//   - Input buffer management
//   - Command line parsing
//   - Output formatting helpers
//
// This library has NO dependencies on kernel, hardware, or OS.
// Commands are implemented separately by the application.
//
// =====================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace umi::shell {

// ============================================================================
// Output Buffer - snprintf-free output formatting
// ============================================================================

/// Output buffer with formatting helpers (no snprintf dependency)
template<size_t BufSize = 2048>
class OutputBuffer {
public:
    void clear() {
        pos_ = 0;
        buffer_[0] = '\0';
    }

    void put_char(char chr) {
        if (pos_ < BufSize - 1) {
            buffer_[pos_++] = chr;
            buffer_[pos_] = '\0';
        }
    }

    void put_str(const char* str) {
        while (*str != '\0' && pos_ < BufSize - 1) {
            buffer_[pos_++] = *str++;
        }
        buffer_[pos_] = '\0';
    }

    void put_line() { put_str("\n"); }

    void put_num(uint32_t num) {
        char tmp[12];
        int idx = 0;
        if (num == 0) {
            tmp[idx++] = '0';
        } else {
            while (num > 0) {
                tmp[idx++] = static_cast<char>('0' + (num % 10));
                num /= 10;
            }
        }
        while (idx > 0 && pos_ < BufSize - 1) {
            buffer_[pos_++] = tmp[--idx];
        }
        buffer_[pos_] = '\0';
    }

    void put_num_signed(int32_t num) {
        if (num < 0) {
            put_char('-');
            num = -num;
        }
        put_num(static_cast<uint32_t>(num));
    }

    void put_num2(uint32_t num) {  // 2-digit with leading zero
        if (num < 10) {
            put_str("0");
        }
        put_num(num);
    }

    void put_hex(uint32_t num, int digits = 0) {
        static const char hex_chars[] = "0123456789abcdef";
        char tmp[9];
        int idx = 0;
        if (num == 0) {
            tmp[idx++] = '0';
        } else {
            while (num > 0) {
                tmp[idx++] = hex_chars[num & 0xF];
                num >>= 4;
            }
        }
        // Pad with zeros if needed
        while (idx < digits) {
            tmp[idx++] = '0';
        }
        while (idx > 0 && pos_ < BufSize - 1) {
            buffer_[pos_++] = tmp[--idx];
        }
        buffer_[pos_] = '\0';
    }

    /// Fixed point: value/10^decimals (e.g., 1234, 2 -> "12.34")
    void put_fixed(uint32_t value, uint32_t decimals) {
        uint32_t divisor = 1;
        for (uint32_t idx = 0; idx < decimals; idx++) {
            divisor *= 10;
        }
        put_num(value / divisor);
        put_char('.');
        uint32_t frac = value % divisor;
        // Leading zeros for fractional part
        for (uint32_t div = divisor / 10; div > 0 && frac < div; div /= 10) {
            put_char('0');
        }
        if (frac > 0) {
            put_num(frac);
        }
    }

    /// Key-value pair: "  key: value\n"
    void put_kv(const char* key, const char* value) {
        put_str("  ");
        put_str(key);
        put_str(": ");
        put_str(value);
        put_line();
    }

    /// Key-value with number: "  key: 123 unit\n"
    void put_kv_num(const char* key, uint32_t value, const char* unit = "") {
        put_str("  ");
        put_str(key);
        put_str(": ");
        put_num(value);
        if (unit[0] != '\0') {
            put_str(" ");
            put_str(unit);
        }
        put_line();
    }

    [[nodiscard]] const char* c_str() const { return buffer_; }
    [[nodiscard]] size_t size() const { return pos_; }
    [[nodiscard]] bool empty() const { return pos_ == 0; }

private:
    char buffer_[BufSize]{};
    size_t pos_ = 0;
};

// ============================================================================
// Command Parser - tokenize command line
// ============================================================================

/// Parsed command with arguments
struct ParsedCommand {
    static constexpr size_t kMaxArgs = 8;
    static constexpr size_t kMaxLen = 256;

    char buffer[kMaxLen]{};      // Copy of input (modified during parse)
    const char* args[kMaxArgs]{};  // Pointers into buffer
    size_t argc = 0;

    /// Parse command line into tokens
    void parse(const char* input) {
        argc = 0;
        size_t len = 0;

        // Copy input to buffer
        while (input[len] != '\0' && len < kMaxLen - 1) {
            buffer[len] = input[len];
            len++;
        }
        buffer[len] = '\0';

        // Tokenize
        char* ptr = buffer;
        while (*ptr != '\0' && argc < kMaxArgs) {
            // Skip whitespace
            while (*ptr == ' ' || *ptr == '\t') {
                ptr++;
            }
            if (*ptr == '\0') {
                break;
            }

            // Start of token
            args[argc++] = ptr;

            // Find end of token
            while (*ptr != '\0' && *ptr != ' ' && *ptr != '\t') {
                ptr++;
            }
            if (*ptr != '\0') {
                *ptr = '\0';
                ptr++;
            }
        }
    }

    /// Get command name (first argument)
    [[nodiscard]] const char* command() const {
        return argc > 0 ? args[0] : "";
    }

    /// Get argument by index (0 = command, 1 = first arg, etc.)
    [[nodiscard]] const char* arg(size_t index) const {
        return index < argc ? args[index] : "";
    }

    /// Check if command matches
    [[nodiscard]] bool is_command(const char* cmd) const {
        return argc > 0 && std::strcmp(args[0], cmd) == 0;
    }

    /// Check if subcommand matches (e.g., "show system" -> is_subcommand("system"))
    [[nodiscard]] bool is_subcommand(const char* sub) const {
        return argc > 1 && std::strcmp(args[1], sub) == 0;
    }
};

// ============================================================================
// String Utilities
// ============================================================================

/// Check if string starts with prefix
inline bool starts_with(const char* str, const char* prefix) {
    while (*prefix != '\0') {
        if (*str++ != *prefix++) {
            return false;
        }
    }
    return true;
}

/// Parse integer from string
inline int32_t parse_int(const char* str, bool* success = nullptr) {
    bool neg = false;
    if (*str == '-') {
        neg = true;
        str++;
    } else if (*str == '+') {
        str++;
    }

    int32_t val = 0;
    bool found = false;
    while (*str >= '0' && *str <= '9') {
        val = val * 10 + (*str - '0');
        str++;
        found = true;
    }
    if (success != nullptr) {
        *success = found;
    }
    return neg ? -val : val;
}

/// Parse unsigned integer
inline uint32_t parse_uint(const char* str, bool* success = nullptr) {
    uint32_t val = 0;
    bool found = false;
    while (*str >= '0' && *str <= '9') {
        val = val * 10 + static_cast<uint32_t>(*str - '0');
        str++;
        found = true;
    }
    if (success != nullptr) {
        *success = found;
    }
    return val;
}

// ============================================================================
// Input Line Buffer
// ============================================================================

/// Line input buffer with editing support
template<size_t BufSize = 256>
class LineBuffer {
public:
    /// Process a single input character
    /// Returns true if line is complete (Enter pressed)
    bool process_char(char chr) {
        if (chr == '\n' || chr == '\r') {
            if (len_ > 0) {
                buffer_[len_] = '\0';
                ready_ = true;
                return true;
            }
            return false;
        }

        if (chr == '\b' || chr == 127) {  // Backspace
            if (len_ > 0) {
                len_--;
            }
            return false;
        }

        if (len_ < BufSize - 1) {
            buffer_[len_++] = chr;
        }
        return false;
    }

    /// Get completed line (null-terminated)
    [[nodiscard]] const char* get_line() const {
        return ready_ ? buffer_ : nullptr;
    }

    /// Check if line is ready
    [[nodiscard]] bool is_ready() const { return ready_; }

    /// Clear buffer for next line
    void clear() {
        len_ = 0;
        ready_ = false;
    }

    /// Get current content (may not be complete)
    [[nodiscard]] const char* content() const {
        return buffer_;
    }

    [[nodiscard]] size_t length() const { return len_; }

private:
    char buffer_[BufSize]{};
    size_t len_ = 0;
    bool ready_ = false;
};

// ============================================================================
// Command Handler Interface
// ============================================================================

/// Command result
enum class CmdResult {
    OK,            // Command executed successfully
    ERROR,         // Command failed
    NOT_FOUND,     // Unknown command
    ACCESS_DENIED, // Insufficient permissions
};

/// Command handler function type
using CmdHandler = CmdResult (*)(const ParsedCommand& cmd, OutputBuffer<>& out, void* ctx);

/// Command descriptor
struct CmdDescriptor {
    const char* name;       // Command name
    const char* help;       // Help text
    CmdHandler handler;     // Handler function
    uint8_t min_level;      // Minimum access level (0=USER, 1=ADMIN, 2=FACTORY)
};

}  // namespace umi::shell
