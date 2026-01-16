# Transport Abstraction

Unified interface for SysEx (WebMIDI) and Bulk (WebUSB) transports.

## Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    UMI Protocol Layer                        │
│  - Message framing, Commands, Authentication, Flow control   │
└─────────────────────────────┬───────────────────────────────┘
                              │
        ┌─────────────────────┴─────────────────┐
        │                                       │
┌───────▼───────┐                       ┌───────▼───────┐
│  SysEx Framing │                       │  Bulk Framing │
│  (7-bit encode)│                       │  (8-bit raw)  │
└───────┬───────┘                       └───────┬───────┘
        │                                       │
┌───────▼───────┐                       ┌───────▼───────┐
│   USB MIDI    │                       │    WebUSB     │
│   Class       │                       │  Vendor Class │
└───────────────┘                       └───────────────┘
```

## Transport Types (protocol/umi_transport.hh)

```cpp
enum class TransportType : uint8_t {
    SYSEX,   // USB MIDI SysEx (7-bit encoding) - WebMIDI compatible
    BULK,    // USB Bulk (8-bit raw) - WebUSB
    SYSEX8,  // MIDI 2.0 SysEx8 (8-bit, future)
};
```

## Transport Capabilities

```cpp
struct TransportCapabilities {
    uint32_t max_packet_size;      // Maximum single packet size
    uint32_t max_message_size;     // Maximum message size (may span packets)
    bool supports_8bit;            // True for Bulk/SysEx8
    bool requires_encoding;        // True if needs 7-bit encoding
    bool supports_streaming;       // True if can stream without framing
};

// Default capabilities
inline constexpr TransportCapabilities SYSEX_CAPABILITIES = {
    .max_packet_size = 64,         // Typical USB-MIDI packet
    .max_message_size = 65536,     // Practical SysEx limit
    .supports_8bit = false,
    .requires_encoding = true,
    .supports_streaming = false,
};

inline constexpr TransportCapabilities BULK_CAPABILITIES = {
    .max_packet_size = 512,        // USB HS bulk
    .max_message_size = 1048576,   // 1MB practical limit
    .supports_8bit = true,
    .requires_encoding = false,
    .supports_streaming = true,
};
```

## Transport Interface

```cpp
class Transport {
public:
    virtual ~Transport() = default;

    /// Get transport type
    virtual TransportType type() const noexcept = 0;

    /// Get transport capabilities
    virtual const TransportCapabilities& capabilities() const noexcept = 0;

    /// Check if connected
    virtual bool is_connected() const noexcept = 0;

    /// Send raw data (transport handles framing/encoding)
    virtual size_t send(const uint8_t* data, size_t len) = 0;

    /// Receive data (transport handles deframing/decoding)
    virtual size_t receive(uint8_t* out, size_t max_len) = 0;

    /// Flush pending data
    virtual void flush() = 0;

    /// Reset transport state
    virtual void reset() = 0;
};
```

## Bulk Framing

Simple length-prefixed framing for 8-bit bulk transfers.

```
Frame format:
[LEN_HI:8][LEN_LO:8][DATA:LEN bytes][CRC16:16]
```

```cpp
namespace bulk {

constexpr size_t FRAME_HEADER_SIZE = 2;
constexpr size_t FRAME_TRAILER_SIZE = 2;
constexpr size_t FRAME_OVERHEAD = 4;
constexpr size_t MAX_FRAME_DATA = 65535 - FRAME_OVERHEAD;

/// CRC-16-CCITT
uint16_t crc16(const uint8_t* data, size_t len) noexcept;

/// Encode data into bulk frame
size_t encode_frame(const uint8_t* data, size_t len, uint8_t* out) noexcept;

/// Decode bulk frame
size_t decode_frame(const uint8_t* frame, size_t frame_len,
                    uint8_t* out, size_t max_len) noexcept;

} // namespace bulk
```

## SysEx Transport

```cpp
template <size_t TxBufSize = 512, size_t RxBufSize = 512>
class SysExTransport : public Transport {
public:
    using SendFn = void (*)(const uint8_t* data, size_t len, void* ctx);
    using ReceiveFn = size_t (*)(uint8_t* out, size_t max_len, void* ctx);

    void init(SendFn send_fn, ReceiveFn recv_fn, void* ctx) noexcept;

    TransportType type() const noexcept override;
    const TransportCapabilities& capabilities() const noexcept override;
    bool is_connected() const noexcept override;

    size_t send(const uint8_t* data, size_t len) override;
    size_t receive(uint8_t* out, size_t max_len) override;
    void flush() override;
    void reset() override;

    void set_connected(bool connected) noexcept;
};
```

## Bulk Transport

```cpp
template <size_t TxBufSize = 1024, size_t RxBufSize = 1024>
class BulkTransport : public Transport {
public:
    using SendFn = void (*)(const uint8_t* data, size_t len, void* ctx);
    using ReceiveFn = size_t (*)(uint8_t* out, size_t max_len, void* ctx);

    void init(SendFn send_fn, ReceiveFn recv_fn, void* ctx) noexcept;

    TransportType type() const noexcept override;
    const TransportCapabilities& capabilities() const noexcept override;
    bool is_connected() const noexcept override;

    size_t send(const uint8_t* data, size_t len) override;
    size_t receive(uint8_t* out, size_t max_len) override;
    void flush() override;
    void reset() override;

    void set_connected(bool connected) noexcept;
};
```

## Transport Handler

Wraps protocol handlers to work with any transport.

```cpp
template <typename ProtocolHandler>
class TransportHandler {
public:
    TransportHandler(Transport& transport, ProtocolHandler& handler);

    /// Process incoming messages
    bool process(uint32_t current_time);

    /// Send message through transport
    size_t send(const uint8_t* data, size_t len);

    Transport& transport();
    ProtocolHandler& handler();
};
```

## Transport Manager

Manages multiple transports with automatic fallback.

```cpp
template <size_t MaxTransports = 2>
class TransportManager {
public:
    /// Register a transport
    bool register_transport(Transport* transport) noexcept;

    /// Get best available transport (prefers Bulk over SysEx)
    Transport* get_best() noexcept;

    /// Get transport by type
    Transport* get(TransportType type) noexcept;

    /// Check if any transport is connected
    bool any_connected() const noexcept;

    /// Get number of registered transports
    size_t count() const noexcept;
};
```

## Usage Example

```cpp
#include <umidi/umidi.hh>

// Create transports
umidi::protocol::SysExTransport<512, 512> sysex_transport;
umidi::protocol::BulkTransport<1024, 1024> bulk_transport;

// Initialize with platform-specific callbacks
sysex_transport.init(midi_send, midi_receive, nullptr);
bulk_transport.init(usb_send, usb_receive, nullptr);

// Register with manager
umidi::protocol::TransportManager<2> manager;
manager.register_transport(&sysex_transport);
manager.register_transport(&bulk_transport);

// Use best available transport
void process() {
    Transport* transport = manager.get_best();
    if (transport) {
        uint8_t buffer[1024];
        size_t len = transport->receive(buffer, sizeof(buffer));
        if (len > 0) {
            // Process received data
            handle_message(buffer, len);
        }
    }
}
```

## Web Client Fallback Strategy

```javascript
class UMIConnection {
    async connect() {
        // 1. Try WebUSB first (faster, landing page support)
        if (navigator.usb) {
            try {
                this.transport = await this.connectWebUSB();
                return;
            } catch (e) {
                console.log('WebUSB not available, falling back to WebMIDI');
            }
        }

        // 2. Fall back to WebMIDI (universal support)
        if (navigator.requestMIDIAccess) {
            this.transport = await this.connectWebMIDI();
            return;
        }

        throw new Error('No supported transport available');
    }
}
```
