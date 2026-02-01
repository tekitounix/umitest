// SPDX-License-Identifier: MIT
// UMI-USB: Type Definitions and Descriptor Structures
#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>

namespace umiusb {

// ============================================================================
// USB Constants
// ============================================================================

enum class Speed : uint8_t {
    LOW = 0,    // 1.5 Mbps
    FULL = 1,   // 12 Mbps
    HIGH = 2,   // 480 Mbps
};

enum class TransferType : uint8_t {
    CONTROL = 0,
    ISOCHRONOUS = 1,
    BULK = 2,
    INTERRUPT = 3,
};

enum class Direction : uint8_t {
    OUT = 0,  // Host to Device
    IN = 1,   // Device to Host
};

// Standard request codes
namespace bRequest {
    inline constexpr uint8_t GetStatus = 0x00;
    inline constexpr uint8_t ClearFeature = 0x01;
    inline constexpr uint8_t SetFeature = 0x03;
    inline constexpr uint8_t SetAddress = 0x05;
    inline constexpr uint8_t GetDescriptor = 0x06;
    inline constexpr uint8_t SetDescriptor = 0x07;
    inline constexpr uint8_t GetConfiguration = 0x08;
    inline constexpr uint8_t SetConfiguration = 0x09;
    inline constexpr uint8_t GetInterface = 0x0A;
    inline constexpr uint8_t SetInterface = 0x0B;
    inline constexpr uint8_t SynchFrame = 0x0C;
}

// Descriptor types
namespace bDescriptorType {
    inline constexpr uint8_t Device = 0x01;
    inline constexpr uint8_t Configuration = 0x02;
    inline constexpr uint8_t String = 0x03;
    inline constexpr uint8_t Interface = 0x04;
    inline constexpr uint8_t Endpoint = 0x05;
    inline constexpr uint8_t DeviceQualifier = 0x06;
    inline constexpr uint8_t OtherSpeedConfig = 0x07;
    inline constexpr uint8_t InterfacePower = 0x08;
    inline constexpr uint8_t InterfaceAssociation = 0x0B;  // IAD
    inline constexpr uint8_t Bos = 0x0F;                   // BOS
    // Audio class specific
    inline constexpr uint8_t CsInterface = 0x24;
    inline constexpr uint8_t CsEndpoint = 0x25;
}

// USB Class codes
namespace bDeviceClass {
    inline constexpr uint8_t PerInterface = 0x00;
    inline constexpr uint8_t Audio = 0x01;
    inline constexpr uint8_t CDC = 0x02;
    inline constexpr uint8_t HID = 0x03;
    inline constexpr uint8_t MassStorage = 0x08;
    inline constexpr uint8_t Misc = 0xEF;
    inline constexpr uint8_t Vendor = 0xFF;
}

// Audio subclass codes
namespace bInterfaceSubClass {
    inline constexpr uint8_t AudioControl = 0x01;
    inline constexpr uint8_t AudioStreaming = 0x02;
    inline constexpr uint8_t MidiStreaming = 0x03;
}

// ============================================================================
// Descriptor Structures (packed, no padding)
// ============================================================================

#pragma pack(push, 1)

struct DeviceDescriptor {
    uint8_t bLength = 18;
    uint8_t bDescriptorType = bDescriptorType::Device;
    uint16_t bcdUSB = 0x0200;
    uint8_t bDeviceClass = 0;
    uint8_t bDeviceSubClass = 0;
    uint8_t bDeviceProtocol = 0;
    uint8_t bMaxPacketSize0 = 64;
    uint16_t idVendor = 0;
    uint16_t idProduct = 0;
    uint16_t bcdDevice = 0x0100;
    uint8_t iManufacturer = 0;
    uint8_t iProduct = 0;
    uint8_t iSerialNumber = 0;
    uint8_t bNumConfigurations = 1;
};
static_assert(sizeof(DeviceDescriptor) == 18, "DeviceDescriptor must be 18 bytes");

struct ConfigurationDescriptor {
    uint8_t bLength = 9;
    uint8_t bDescriptorType = bDescriptorType::Configuration;
    uint16_t wTotalLength = 9;
    uint8_t bNumInterfaces = 0;
    uint8_t bConfigurationValue = 1;
    uint8_t iConfiguration = 0;
    uint8_t bmAttributes = 0xC0;  // Self-powered
    uint8_t bMaxPower = 50;       // 100mA
};
static_assert(sizeof(ConfigurationDescriptor) == 9, "ConfigurationDescriptor must be 9 bytes");

struct InterfaceDescriptor {
    uint8_t bLength = 9;
    uint8_t bDescriptorType = bDescriptorType::Interface;
    uint8_t bInterfaceNumber = 0;
    uint8_t bAlternateSetting = 0;
    uint8_t bNumEndpoints = 0;
    uint8_t bInterfaceClass = 0;
    uint8_t bInterfaceSubClass = 0;
    uint8_t bInterfaceProtocol = 0;
    uint8_t iInterface = 0;
};
static_assert(sizeof(InterfaceDescriptor) == 9, "InterfaceDescriptor must be 9 bytes");

struct EndpointDescriptor {
    uint8_t bLength = 7;
    uint8_t bDescriptorType = bDescriptorType::Endpoint;
    uint8_t bEndpointAddress = 0;
    uint8_t bmAttributes = 0;
    uint16_t wMaxPacketSize = 0;
    uint8_t bInterval = 0;

    [[nodiscard]] constexpr uint8_t number() const { return bEndpointAddress & 0x0F; }
    [[nodiscard]] constexpr Direction direction() const {
        return (bEndpointAddress & 0x80) ? Direction::IN : Direction::OUT;
    }
    [[nodiscard]] constexpr TransferType transfer_type() const {
        return static_cast<TransferType>(bmAttributes & 0x03);
    }
};
static_assert(sizeof(EndpointDescriptor) == 7, "EndpointDescriptor must be 7 bytes");

// Audio class endpoint descriptor (9 bytes)
struct AudioEndpointDescriptor {
    uint8_t bLength = 9;
    uint8_t bDescriptorType = bDescriptorType::Endpoint;
    uint8_t bEndpointAddress = 0;
    uint8_t bmAttributes = 0;
    uint16_t wMaxPacketSize = 0;
    uint8_t bInterval = 0;
    uint8_t bRefresh = 0;
    uint8_t bSynchAddress = 0;
};
static_assert(sizeof(AudioEndpointDescriptor) == 9, "AudioEndpointDescriptor must be 9 bytes");

#pragma pack(pop)

// ============================================================================
// Setup Packet
// ============================================================================

#pragma pack(push, 1)
struct SetupPacket {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;

    [[nodiscard]] constexpr Direction direction() const {
        return (bmRequestType & 0x80) ? Direction::IN : Direction::OUT;
    }

    [[nodiscard]] constexpr uint8_t type() const {
        return (bmRequestType >> 5) & 0x03;  // 0=Standard, 1=Class, 2=Vendor
    }

    [[nodiscard]] constexpr uint8_t recipient() const {
        return bmRequestType & 0x1F;  // 0=Device, 1=Interface, 2=Endpoint
    }

    [[nodiscard]] constexpr uint8_t descriptor_type() const { return wValue >> 8; }
    [[nodiscard]] constexpr uint8_t descriptor_index() const { return wValue & 0xFF; }
};
static_assert(sizeof(SetupPacket) == 8, "SetupPacket must be 8 bytes");
#pragma pack(pop)

// ============================================================================
// Endpoint Configuration
// ============================================================================

struct EndpointConfig {
    uint8_t number;
    Direction direction;
    TransferType type;
    uint16_t max_packet_size;

    [[nodiscard]] constexpr uint8_t address() const {
        return number | (direction == Direction::IN ? 0x80 : 0x00);
    }
};

// ============================================================================
// String Descriptor Helpers
// ============================================================================

// Language ID descriptor (always index 0)
#pragma pack(push, 1)
struct StringLangId {
    uint8_t bLength = 4;
    uint8_t bDescriptorType = bDescriptorType::String;
    uint16_t wLangId = 0x0409;  // English (US)
};
static_assert(sizeof(StringLangId) == 4, "StringLangId must be 4 bytes");
#pragma pack(pop)

// ============================================================================
// Compile-time String Descriptor
// ============================================================================

/// Compile-time USB string descriptor
/// Usage: constexpr auto str = StringDesc<"Hello">();
template<std::size_t N>
struct StringDesc {
    uint8_t bLength;
    uint8_t bDescriptorType = bDescriptorType::String;
    char16_t str[N];

    constexpr StringDesc(const char (&s)[N + 1]) : bLength(2 + N * 2), str{} {
        for (std::size_t i = 0; i < N; ++i) {
            str[i] = static_cast<char16_t>(s[i]);
        }
    }

    [[nodiscard]] constexpr const uint8_t* data() const {
        return reinterpret_cast<const uint8_t*>(this);
    }

    [[nodiscard]] constexpr std::size_t size() const { return bLength; }
};

// Deduction guide for string literals
template<std::size_t N>
StringDesc(const char (&)[N]) -> StringDesc<N - 1>;

// ============================================================================
// Descriptor Builder Utilities
// ============================================================================

/// Concatenate multiple descriptor arrays at compile time
template<std::size_t... Sizes>
constexpr auto concat_descriptors(const std::array<uint8_t, Sizes>&... arrays) {
    constexpr std::size_t total = (Sizes + ...);
    std::array<uint8_t, total> result{};
    std::size_t offset = 0;

    auto copy_one = [&result, &offset](const auto& arr) {
        for (std::size_t i = 0; i < arr.size(); ++i) {
            result[offset++] = arr[i];
        }
    };

    (copy_one(arrays), ...);
    return result;
}

/// Convert a packed struct to std::array at compile time
template<typename T>
constexpr auto to_array(const T& desc) {
    std::array<uint8_t, sizeof(T)> result{};
    const auto* bytes = reinterpret_cast<const uint8_t*>(&desc);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        result[i] = bytes[i];
    }
    return result;
}

}  // namespace umiusb
