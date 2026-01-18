// SPDX-License-Identifier: MIT
// UMI-USB: Compile-time Descriptor Builder
//
// Type-safe, compile-time USB descriptor generation with:
// - Exact binary layout per USB specification
// - Automatic length and offset calculation
// - String descriptor with UTF-16LE encoding
// - Support for Audio/MIDI, WinUSB (MS OS 2.0), and more
//
#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <algorithm>

namespace umiusb::desc {

// ============================================================================
// Core Utilities
// ============================================================================

/// Compile-time byte buffer with concatenation
template<std::size_t N>
struct Bytes {
    std::array<uint8_t, N> data{};
    static constexpr std::size_t size = N;

    constexpr uint8_t operator[](std::size_t i) const { return data[i]; }
    constexpr uint8_t& operator[](std::size_t i) { return data[i]; }

    constexpr auto span() const { return std::span<const uint8_t, N>(data); }
};

/// Concatenate two Bytes
template<std::size_t N1, std::size_t N2>
constexpr auto operator+(const Bytes<N1>& a, const Bytes<N2>& b) {
    Bytes<N1 + N2> result{};
    for (std::size_t i = 0; i < N1; ++i) result[i] = a[i];
    for (std::size_t i = 0; i < N2; ++i) result[N1 + i] = b[i];
    return result;
}

/// Create Bytes from variadic uint8_t
template<typename... Args>
constexpr auto bytes(Args... args) {
    return Bytes<sizeof...(Args)>{{static_cast<uint8_t>(args)...}};
}

/// Little-endian 16-bit value as 2 bytes
constexpr auto le16(uint16_t v) {
    return bytes(v & 0xFF, (v >> 8) & 0xFF);
}

/// Little-endian 32-bit value as 4 bytes
constexpr auto le32(uint32_t v) {
    return bytes(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF);
}

// ============================================================================
// Descriptor Type Constants
// ============================================================================

namespace dtype {
    inline constexpr uint8_t Device = 0x01;
    inline constexpr uint8_t Configuration = 0x02;
    inline constexpr uint8_t String = 0x03;
    inline constexpr uint8_t Interface = 0x04;
    inline constexpr uint8_t Endpoint = 0x05;
    inline constexpr uint8_t DeviceQualifier = 0x06;
    inline constexpr uint8_t InterfaceAssociation = 0x0B;
    inline constexpr uint8_t CsInterface = 0x24;
    inline constexpr uint8_t CsEndpoint = 0x25;
    // BOS
    inline constexpr uint8_t BOS = 0x0F;
    inline constexpr uint8_t DeviceCapability = 0x10;
}

// ============================================================================
// String Descriptor
// ============================================================================

/// Compile-time string length
template<std::size_t N>
constexpr std::size_t str_len(const char (&)[N]) { return N - 1; }

/// String descriptor (UTF-16LE encoded)
template<std::size_t N>
constexpr auto String(const char (&s)[N]) {
    constexpr std::size_t len = N - 1;  // Exclude null terminator
    constexpr std::size_t desc_len = 2 + len * 2;

    Bytes<desc_len> result{};
    result[0] = static_cast<uint8_t>(desc_len);
    result[1] = dtype::String;

    for (std::size_t i = 0; i < len; ++i) {
        result[2 + i * 2] = static_cast<uint8_t>(s[i]);
        result[2 + i * 2 + 1] = 0;  // High byte (ASCII -> UTF-16LE)
    }
    return result;
}

/// Language ID descriptor (index 0)
constexpr auto StringLangId(uint16_t lang_id = 0x0409) {
    return bytes(4, dtype::String, lang_id & 0xFF, (lang_id >> 8) & 0xFF);
}

// ============================================================================
// Device Descriptor
// ============================================================================

struct DeviceDesc {
    uint16_t usb_version = 0x0200;   // USB 2.0
    uint8_t device_class = 0;
    uint8_t device_subclass = 0;
    uint8_t device_protocol = 0;
    uint8_t max_packet_size0 = 64;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    uint16_t device_version = 0x0100;
    uint8_t manufacturer_string = 0;
    uint8_t product_string = 0;
    uint8_t serial_string = 0;
    uint8_t num_configurations = 1;

    constexpr auto build() const {
        return bytes(18, dtype::Device) +
               le16(usb_version) +
               bytes(device_class, device_subclass, device_protocol, max_packet_size0) +
               le16(vendor_id) + le16(product_id) + le16(device_version) +
               bytes(manufacturer_string, product_string, serial_string, num_configurations);
    }
};

// ============================================================================
// Configuration Descriptor Header
// ============================================================================

/// Configuration descriptor header (total length filled by ConfigBuilder)
struct ConfigHeader {
    uint8_t config_value = 1;
    uint8_t config_string = 0;
    uint8_t attributes = 0xC0;  // Self-powered
    uint8_t max_power = 50;     // 100mA

    template<std::size_t TotalLen, uint8_t NumInterfaces>
    constexpr auto build() const {
        return bytes(9, dtype::Configuration) +
               le16(static_cast<uint16_t>(TotalLen)) +
               bytes(NumInterfaces, config_value, config_string, attributes, max_power);
    }
};

// ============================================================================
// Interface Descriptor
// ============================================================================

struct InterfaceDesc {
    uint8_t interface_number = 0;
    uint8_t alternate_setting = 0;
    uint8_t num_endpoints = 0;
    uint8_t interface_class = 0;
    uint8_t interface_subclass = 0;
    uint8_t interface_protocol = 0;
    uint8_t interface_string = 0;

    constexpr auto build() const {
        return bytes(9, dtype::Interface,
                     interface_number, alternate_setting, num_endpoints,
                     interface_class, interface_subclass, interface_protocol,
                     interface_string);
    }
};

// ============================================================================
// Endpoint Descriptor
// ============================================================================

namespace ep {
    // Direction
    inline constexpr uint8_t Out = 0x00;
    inline constexpr uint8_t In = 0x80;

    // Transfer type
    inline constexpr uint8_t Control = 0x00;
    inline constexpr uint8_t Isochronous = 0x01;
    inline constexpr uint8_t Bulk = 0x02;
    inline constexpr uint8_t Interrupt = 0x03;

    // Sync type (for isochronous)
    inline constexpr uint8_t NoSync = 0x00;
    inline constexpr uint8_t Async = 0x04;
    inline constexpr uint8_t Adaptive = 0x08;
    inline constexpr uint8_t Sync = 0x0C;

    // Usage type (for isochronous)
    inline constexpr uint8_t Data = 0x00;
    inline constexpr uint8_t Feedback = 0x10;
    inline constexpr uint8_t ImplicitFb = 0x20;
}

struct EndpointDesc {
    uint8_t address = 0;        // ep::In | ep::Out | endpoint_number
    uint8_t attributes = 0;     // ep::Bulk, etc.
    uint16_t max_packet_size = 64;
    uint8_t interval = 0;

    constexpr auto build() const {
        return bytes(7, dtype::Endpoint, address, attributes) +
               le16(max_packet_size) +
               bytes(interval);
    }
};

/// Audio endpoint (9 bytes, includes bRefresh and bSynchAddress)
struct AudioEndpointDesc {
    uint8_t address = 0;
    uint8_t attributes = 0;
    uint16_t max_packet_size = 64;
    uint8_t interval = 0;
    uint8_t refresh = 0;
    uint8_t synch_address = 0;

    constexpr auto build() const {
        return bytes(9, dtype::Endpoint, address, attributes) +
               le16(max_packet_size) +
               bytes(interval, refresh, synch_address);
    }
};

// ============================================================================
// Interface Association Descriptor (IAD)
// ============================================================================

struct InterfaceAssociationDesc {
    uint8_t first_interface = 0;
    uint8_t interface_count = 0;
    uint8_t function_class = 0;
    uint8_t function_subclass = 0;
    uint8_t function_protocol = 0;
    uint8_t function_string = 0;

    constexpr auto build() const {
        return bytes(8, dtype::InterfaceAssociation,
                     first_interface, interface_count,
                     function_class, function_subclass, function_protocol,
                     function_string);
    }
};

// ============================================================================
// USB Audio Class Descriptors (UAC 1.0)
// ============================================================================

namespace audio {

// Subclass
inline constexpr uint8_t AudioControl = 0x01;
inline constexpr uint8_t AudioStreaming = 0x02;
inline constexpr uint8_t MidiStreaming = 0x03;

// AC descriptor subtypes
namespace ac {
    inline constexpr uint8_t Header = 0x01;
    inline constexpr uint8_t InputTerminal = 0x02;
    inline constexpr uint8_t OutputTerminal = 0x03;
    inline constexpr uint8_t MixerUnit = 0x04;
    inline constexpr uint8_t SelectorUnit = 0x05;
    inline constexpr uint8_t FeatureUnit = 0x06;
}

// MS descriptor subtypes
namespace ms {
    inline constexpr uint8_t Header = 0x01;
    inline constexpr uint8_t MidiInJack = 0x02;
    inline constexpr uint8_t MidiOutJack = 0x03;
    inline constexpr uint8_t Element = 0x04;
    inline constexpr uint8_t General = 0x01;
}

// Jack types
inline constexpr uint8_t JackEmbedded = 0x01;
inline constexpr uint8_t JackExternal = 0x02;

/// AC Interface Header
template<uint8_t... StreamingInterfaces>
constexpr auto AcHeader(uint16_t bcd_adc = 0x0100) {
    constexpr std::size_t n = sizeof...(StreamingInterfaces);
    constexpr std::size_t len = 8 + n;
    return bytes(static_cast<uint8_t>(len), dtype::CsInterface, ac::Header) +
           le16(bcd_adc) +
           le16(static_cast<uint16_t>(len)) +
           bytes(static_cast<uint8_t>(n), StreamingInterfaces...);
}

/// MS Interface Header
template<std::size_t TotalLen>
constexpr auto MsHeader(uint16_t bcd_msc = 0x0100) {
    return bytes(7, dtype::CsInterface, ms::Header) +
           le16(bcd_msc) +
           le16(static_cast<uint16_t>(TotalLen));
}

/// MIDI IN Jack
constexpr auto MidiInJack(uint8_t jack_type, uint8_t jack_id, uint8_t string_idx = 0) {
    return bytes(6, dtype::CsInterface, ms::MidiInJack, jack_type, jack_id, string_idx);
}

/// MIDI OUT Jack (1 input pin)
constexpr auto MidiOutJack(uint8_t jack_type, uint8_t jack_id,
                           uint8_t source_id, uint8_t source_pin = 1,
                           uint8_t string_idx = 0) {
    return bytes(9, dtype::CsInterface, ms::MidiOutJack,
                 jack_type, jack_id,
                 1,  // bNrInputPins
                 source_id, source_pin, string_idx);
}

/// MS Bulk Endpoint (class-specific)
constexpr auto MsBulkEndpoint(uint8_t assoc_jack_id) {
    return bytes(5, dtype::CsEndpoint, ms::General, 1, assoc_jack_id);
}

/// MS Bulk Endpoint with multiple jacks
template<typename... JackIds>
constexpr auto MsBulkEndpointMulti(JackIds... jack_ids) {
    constexpr std::size_t n = sizeof...(JackIds);
    return bytes(4 + n, dtype::CsEndpoint, ms::General, static_cast<uint8_t>(n),
                 static_cast<uint8_t>(jack_ids)...);
}

}  // namespace audio

// ============================================================================
// WinUSB / MS OS 2.0 Descriptors
// ============================================================================

namespace winusb {

// MS OS 2.0 descriptor types
inline constexpr uint16_t SetHeaderDescriptor = 0x00;
inline constexpr uint16_t SubsetHeaderConfig = 0x01;
inline constexpr uint16_t SubsetHeaderFunction = 0x02;
inline constexpr uint16_t CompatibleId = 0x03;
inline constexpr uint16_t RegistryProperty = 0x04;

// Platform capability UUID for MS OS 2.0
// {D8DD60DF-4589-4CC7-9CD2-659D9E648A9F}
constexpr auto MsOs20PlatformCapabilityUuid() {
    return bytes(
        0xDF, 0x60, 0xDD, 0xD8,  // DWORD (little-endian)
        0x89, 0x45,              // WORD
        0xC7, 0x4C,              // WORD
        0x9C, 0xD2,              // 2 bytes
        0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F  // 6 bytes
    );
}

/// BOS descriptor header
constexpr auto BosHeader(uint16_t total_len, uint8_t num_caps) {
    return bytes(5, dtype::BOS) + le16(total_len) + bytes(num_caps);
}

/// Platform capability header for MS OS 2.0
constexpr auto PlatformCapability(uint16_t desc_set_len, uint8_t vendor_code,
                                   uint8_t alt_enum_code = 0) {
    // Total length: 28 bytes
    return bytes(28, dtype::DeviceCapability, 0x05) +  // 0x05 = Platform capability
           bytes(0x00) +  // bReserved
           MsOs20PlatformCapabilityUuid() +
           le32(0x06030000) +  // dwWindowsVersion (Win 8.1+)
           le16(desc_set_len) +
           bytes(vendor_code, alt_enum_code);
}

/// MS OS 2.0 Descriptor Set Header
constexpr auto DescriptorSetHeader(uint16_t total_len) {
    return le16(10) + le16(SetHeaderDescriptor) +
           le32(0x06030000) +  // Windows version
           le16(total_len);
}

/// MS OS 2.0 Configuration Subset Header
constexpr auto ConfigSubsetHeader(uint8_t config_idx, uint16_t subset_len) {
    return le16(8) + le16(SubsetHeaderConfig) +
           bytes(config_idx, 0x00) +  // bConfigurationIndex, bReserved
           le16(subset_len);
}

/// MS OS 2.0 Function Subset Header
constexpr auto FunctionSubsetHeader(uint8_t first_interface, uint16_t subset_len) {
    return le16(8) + le16(SubsetHeaderFunction) +
           bytes(first_interface, 0x00) +  // bFirstInterface, bReserved
           le16(subset_len);
}

/// MS OS 2.0 Compatible ID (WINUSB)
constexpr auto CompatibleIdWinUsb() {
    return le16(20) + le16(CompatibleId) +
           bytes('W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00) +  // CompatibleID
           bytes(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);  // SubCompatibleID
}

/// MS OS 2.0 Registry Property (Device Interface GUID)
template<std::size_t N>
constexpr auto RegistryPropertyGuid(const char (&guid)[N]) {
    // Property name: "DeviceInterfaceGUIDs" (UTF-16LE, null-terminated)
    constexpr char prop_name[] = "DeviceInterfaceGUIDs";
    constexpr std::size_t name_len = sizeof(prop_name);  // includes null
    constexpr std::size_t name_bytes = name_len * 2;     // UTF-16LE

    // GUID value (UTF-16LE, double-null terminated for REG_MULTI_SZ)
    constexpr std::size_t guid_len = N - 1;  // exclude null
    constexpr std::size_t guid_bytes = (guid_len + 2) * 2;  // +2 for double null

    constexpr std::size_t total = 10 + name_bytes + guid_bytes;

    Bytes<total> result{};
    std::size_t pos = 0;

    // wLength (2 bytes)
    result[pos++] = total & 0xFF;
    result[pos++] = (total >> 8) & 0xFF;

    // wDescriptorType (2 bytes)
    result[pos++] = RegistryProperty & 0xFF;
    result[pos++] = (RegistryProperty >> 8) & 0xFF;

    // wPropertyDataType (2 bytes) - REG_MULTI_SZ = 7
    result[pos++] = 7;
    result[pos++] = 0;

    // wPropertyNameLength (2 bytes)
    result[pos++] = name_bytes & 0xFF;
    result[pos++] = (name_bytes >> 8) & 0xFF;

    // PropertyName (UTF-16LE)
    for (std::size_t i = 0; i < name_len; ++i) {
        result[pos++] = static_cast<uint8_t>(prop_name[i]);
        result[pos++] = 0;
    }

    // wPropertyDataLength (2 bytes)
    result[pos++] = guid_bytes & 0xFF;
    result[pos++] = (guid_bytes >> 8) & 0xFF;

    // PropertyData (GUID in UTF-16LE, double-null terminated)
    for (std::size_t i = 0; i < guid_len; ++i) {
        result[pos++] = static_cast<uint8_t>(guid[i]);
        result[pos++] = 0;
    }
    // Double null termination
    result[pos++] = 0;
    result[pos++] = 0;
    result[pos++] = 0;
    result[pos++] = 0;

    return result;
}

}  // namespace winusb

// ============================================================================
// Configuration Builder
// ============================================================================

/// Build complete configuration descriptor from parts
template<typename... Parts>
constexpr auto Config(const ConfigHeader& header, Parts... parts) {
    // Calculate total size
    constexpr std::size_t body_size = (Parts::size + ...);
    constexpr std::size_t total_size = 9 + body_size;

    // Count interfaces (heuristic: count interface descriptors)
    // For simplicity, user should provide correct count
    // This is a placeholder - real implementation would parse parts

    auto body = (parts + ...);
    return header.build<total_size, 2>() + body;  // TODO: auto-count interfaces
}

/// Build configuration with explicit interface count
template<uint8_t NumInterfaces, typename... Parts>
constexpr auto ConfigWithInterfaces(const ConfigHeader& header, Parts... parts) {
    constexpr std::size_t body_size = (Parts::size + ...);
    constexpr std::size_t total_size = 9 + body_size;

    auto body = (parts + ...);
    return header.build<total_size, NumInterfaces>() + body;
}

// ============================================================================
// MIDI Device Builder (Convenience)
// ============================================================================

namespace midi {

/// Build standard USB MIDI configuration descriptor
/// @param midi_ep Endpoint number for MIDI (1-15)
/// @param max_packet Max packet size (default 64)
template<uint8_t MidiEp = 1, uint16_t MaxPacket = 64>
constexpr auto StandardConfig() {
    // Interface 0: Audio Control
    constexpr auto if0 = InterfaceDesc{
        .interface_number = 0,
        .num_endpoints = 0,
        .interface_class = 0x01,  // Audio
        .interface_subclass = audio::AudioControl,
    }.build();

    // CS AC Header
    constexpr auto ac_header = audio::AcHeader<1>();  // 1 streaming interface

    // Interface 1: MIDI Streaming
    constexpr auto if1 = InterfaceDesc{
        .interface_number = 1,
        .num_endpoints = 2,
        .interface_class = 0x01,  // Audio
        .interface_subclass = audio::MidiStreaming,
    }.build();

    // MS Header (calculate size: 7+6+6+9+9+9+5+9+5 = 65)
    constexpr auto ms_header = audio::MsHeader<65>();

    // MIDI Jacks
    constexpr auto in_emb = audio::MidiInJack(audio::JackEmbedded, 1);
    constexpr auto in_ext = audio::MidiInJack(audio::JackExternal, 2);
    constexpr auto out_emb = audio::MidiOutJack(audio::JackEmbedded, 3, 2);  // src=ext in
    constexpr auto out_ext = audio::MidiOutJack(audio::JackExternal, 4, 1);  // src=emb in

    // Endpoints
    constexpr auto ep_out = AudioEndpointDesc{
        .address = MidiEp,
        .attributes = ep::Bulk,
        .max_packet_size = MaxPacket,
    }.build();

    constexpr auto ep_out_cs = audio::MsBulkEndpoint(1);  // Embedded IN jack

    constexpr auto ep_in = AudioEndpointDesc{
        .address = static_cast<uint8_t>(ep::In | MidiEp),
        .attributes = ep::Bulk,
        .max_packet_size = MaxPacket,
    }.build();

    constexpr auto ep_in_cs = audio::MsBulkEndpoint(3);  // Embedded OUT jack

    // Build config
    return ConfigWithInterfaces<2>(
        ConfigHeader{},
        if0, ac_header,
        if1, ms_header, in_emb, in_ext, out_emb, out_ext,
        ep_out, ep_out_cs, ep_in, ep_in_cs
    );
}

}  // namespace midi

// ============================================================================
// Descriptor Set (Device + Config + Strings)
// ============================================================================

/// Complete descriptor set for a device
template<typename DevDesc, typename ConfigDesc, typename... StringDescs>
struct DescriptorSet {
    DevDesc device;
    ConfigDesc config;
    std::tuple<StringDescs...> strings;

    static constexpr std::size_t string_count = sizeof...(StringDescs);

    constexpr auto device_descriptor() const { return device; }
    constexpr auto config_descriptor() const { return config; }

    template<std::size_t I>
    constexpr auto string_descriptor() const { return std::get<I>(strings); }
};

}  // namespace umiusb::desc
