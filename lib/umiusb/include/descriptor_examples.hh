// SPDX-License-Identifier: MIT
// UMI-USB: Descriptor Builder Usage Examples
//
// This file demonstrates how to use the compile-time descriptor builder.
//
#pragma once

#include "core/descriptor.hh"

namespace umiusb::examples {

using namespace desc;

// ============================================================================
// Example 1: Simple USB MIDI Device
// ============================================================================

namespace midi_device {

// Device descriptor
constexpr auto device_desc = DeviceDesc{
    .usb_version = 0x0200,
    .vendor_id = 0x1209,       // pid.codes test VID
    .product_id = 0x0001,
    .device_version = 0x0100,
    .manufacturer_string = 1,
    .product_string = 2,
}.build();

// Configuration descriptor (using convenience builder)
constexpr auto config_desc = midi::StandardConfig<1, 64>();

// String descriptors
constexpr auto string0 = StringLangId();
constexpr auto string1 = String("UMI-OS");
constexpr auto string2 = String("UMI MIDI Synth");

// Verify sizes at compile time
static_assert(device_desc.size == 18, "Device descriptor must be 18 bytes");
static_assert(config_desc.size == 101, "MIDI config descriptor must be 101 bytes");

}  // namespace midi_device

// ============================================================================
// Example 2: WinUSB Device with Custom GUID
// ============================================================================

namespace winusb_device {

// For WinUSB, device class is typically 0xFF (vendor-specific) or use IAD
constexpr auto device_desc = DeviceDesc{
    .usb_version = 0x0201,     // USB 2.01 for BOS
    .device_class = 0xFF,      // Vendor-specific
    .vendor_id = 0x1209,
    .product_id = 0x0002,
    .device_version = 0x0100,
    .manufacturer_string = 1,
    .product_string = 2,
}.build();

// Simple vendor interface
constexpr auto if0 = InterfaceDesc{
    .interface_number = 0,
    .num_endpoints = 2,
    .interface_class = 0xFF,
    .interface_subclass = 0x00,
    .interface_protocol = 0x00,
}.build();

constexpr auto ep_out = EndpointDesc{
    .address = 0x01,
    .attributes = ep::Bulk,
    .max_packet_size = 64,
}.build();

constexpr auto ep_in = EndpointDesc{
    .address = 0x81,
    .attributes = ep::Bulk,
    .max_packet_size = 64,
}.build();

constexpr auto config_desc = ConfigWithInterfaces<1>(
    ConfigHeader{},
    if0, ep_out, ep_in
);

// MS OS 2.0 Descriptor Set for automatic WinUSB driver binding
// Use vendor request code 0x01 to retrieve this
constexpr auto VENDOR_CODE = 0x01;

// Device Interface GUID (generate your own!)
constexpr char DEVICE_GUID[] = "{12345678-1234-1234-1234-123456789ABC}";

// Build MS OS 2.0 descriptor set
constexpr auto ms_compat_id = winusb::CompatibleIdWinUsb();
constexpr auto ms_registry = winusb::RegistryPropertyGuid(DEVICE_GUID);

constexpr auto ms_func_subset = winusb::FunctionSubsetHeader(0,
    8 + ms_compat_id.size + ms_registry.size);

constexpr auto ms_config_subset = winusb::ConfigSubsetHeader(0,
    8 + ms_func_subset.size + ms_compat_id.size + ms_registry.size);

constexpr auto ms_desc_set =
    winusb::DescriptorSetHeader(10 + ms_config_subset.size +
                                ms_func_subset.size + ms_compat_id.size +
                                ms_registry.size) +
    ms_config_subset +
    ms_func_subset +
    ms_compat_id +
    ms_registry;

// BOS descriptor (required for MS OS 2.0)
constexpr auto bos_cap = winusb::PlatformCapability(
    static_cast<uint16_t>(ms_desc_set.size), VENDOR_CODE);

constexpr auto bos_desc = winusb::BosHeader(
    static_cast<uint16_t>(5 + bos_cap.size), 1) + bos_cap;

}  // namespace winusb_device

// ============================================================================
// Example 3: Composite Device (MIDI + WinUSB)
// ============================================================================

namespace composite_device {

// USB 2.01 for BOS, use IAD for composite
constexpr auto device_desc = DeviceDesc{
    .usb_version = 0x0201,
    .device_class = 0xEF,      // Miscellaneous
    .device_subclass = 0x02,   // Common Class
    .device_protocol = 0x01,   // Interface Association Descriptor
    .vendor_id = 0x1209,
    .product_id = 0x0003,
    .device_version = 0x0100,
    .manufacturer_string = 1,
    .product_string = 2,
}.build();

// IAD for Audio/MIDI function
constexpr auto iad_midi = InterfaceAssociationDesc{
    .first_interface = 0,
    .interface_count = 2,
    .function_class = 0x01,    // Audio
    .function_subclass = 0x00,
    .function_protocol = 0x00,
}.build();

// MIDI interfaces (same as standard)
constexpr auto if0_ac = InterfaceDesc{
    .interface_number = 0,
    .num_endpoints = 0,
    .interface_class = 0x01,
    .interface_subclass = audio::AudioControl,
}.build();

constexpr auto ac_header = audio::AcHeader<1>();

constexpr auto if1_ms = InterfaceDesc{
    .interface_number = 1,
    .num_endpoints = 2,
    .interface_class = 0x01,
    .interface_subclass = audio::MidiStreaming,
}.build();

constexpr auto ms_header = audio::MsHeader<65>();
constexpr auto midi_in_emb = audio::MidiInJack(audio::JackEmbedded, 1);
constexpr auto midi_in_ext = audio::MidiInJack(audio::JackExternal, 2);
constexpr auto midi_out_emb = audio::MidiOutJack(audio::JackEmbedded, 3, 2);
constexpr auto midi_out_ext = audio::MidiOutJack(audio::JackExternal, 4, 1);

constexpr auto ep1_out = AudioEndpointDesc{
    .address = 0x01,
    .attributes = ep::Bulk,
    .max_packet_size = 64,
}.build();
constexpr auto ep1_out_cs = audio::MsBulkEndpoint(1);

constexpr auto ep1_in = AudioEndpointDesc{
    .address = 0x81,
    .attributes = ep::Bulk,
    .max_packet_size = 64,
}.build();
constexpr auto ep1_in_cs = audio::MsBulkEndpoint(3);

// IAD for vendor function (WinUSB)
constexpr auto iad_vendor = InterfaceAssociationDesc{
    .first_interface = 2,
    .interface_count = 1,
    .function_class = 0xFF,
    .function_subclass = 0x00,
    .function_protocol = 0x00,
}.build();

constexpr auto if2_vendor = InterfaceDesc{
    .interface_number = 2,
    .num_endpoints = 2,
    .interface_class = 0xFF,
}.build();

constexpr auto ep2_out = EndpointDesc{
    .address = 0x02,
    .attributes = ep::Bulk,
    .max_packet_size = 64,
}.build();

constexpr auto ep2_in = EndpointDesc{
    .address = 0x82,
    .attributes = ep::Bulk,
    .max_packet_size = 64,
}.build();

// Complete configuration
constexpr auto config_desc = ConfigWithInterfaces<3>(
    ConfigHeader{},
    // MIDI function
    iad_midi, if0_ac, ac_header,
    if1_ms, ms_header, midi_in_emb, midi_in_ext, midi_out_emb, midi_out_ext,
    ep1_out, ep1_out_cs, ep1_in, ep1_in_cs,
    // Vendor function
    iad_vendor, if2_vendor, ep2_out, ep2_in
);

}  // namespace composite_device

// ============================================================================
// Example 4: Manual descriptor with exact byte control
// ============================================================================

namespace manual_example {

// You can build descriptors byte-by-byte if needed
constexpr auto custom_desc =
    bytes(0x09, 0x04) +              // Interface descriptor header
    bytes(0x00, 0x00, 0x02) +        // bInterfaceNumber, bAlternateSetting, bNumEndpoints
    bytes(0xFF, 0x00, 0x00, 0x00) +  // Class, Subclass, Protocol, iInterface
    // Followed by endpoints...
    bytes(0x07, 0x05, 0x01, 0x02) + le16(64) + bytes(0x00) +  // EP OUT
    bytes(0x07, 0x05, 0x81, 0x02) + le16(64) + bytes(0x00);   // EP IN

static_assert(custom_desc.size == 9 + 7 + 7, "Custom descriptor size check");

}  // namespace manual_example

}  // namespace umiusb::examples
