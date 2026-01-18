// SPDX-License-Identifier: MIT
// USB MIDI Device Class for STM32F4
#pragma once

#include <cstdint>
#include <cstring>
#include "usb_otg.hh"

namespace umi::stm32 {

/// USB MIDI Class Implementation
/// Implements USB Audio Class with MIDI Streaming Interface
class USB_MIDI {
public:
    static constexpr uint8_t EP_MIDI_OUT = 1;  // Host -> Device
    static constexpr uint8_t EP_MIDI_IN = 1;   // Device -> Host
    static constexpr uint8_t MIDI_PACKET_SIZE = 64;

    // USB Device Descriptor (18 bytes)
    static constexpr uint8_t DEVICE_DESC[] = {
        18,         // bLength
        0x01,       // bDescriptorType = DEVICE
        0x00, 0x02, // bcdUSB = 2.00 (little-endian)
        0x00,       // bDeviceClass = defined at interface level
        0x00,       // bDeviceSubClass
        0x00,       // bDeviceProtocol
        64,         // bMaxPacketSize0
        0x09, 0x12, // idVendor = 0x1209 (little-endian)
        0x01, 0x00, // idProduct = 0x0001 (little-endian)
        0x00, 0x02, // bcdDevice = 2.00 (little-endian)
        1,          // iManufacturer
        2,          // iProduct
        0,          // iSerialNumber
        1,          // bNumConfigurations
    };

    // USB Audio/MIDI Class Configuration Descriptor
    // Total: 101 bytes
    static constexpr uint8_t CONFIG_DESC[] = {
        // === Configuration Descriptor (9 bytes) ===
        0x09,       // bLength
        0x02,       // bDescriptorType = CONFIGURATION
        0x65, 0x00, // wTotalLength = 101 bytes
        0x02,       // bNumInterfaces = 2 (Audio Control + MIDI Streaming)
        0x01,       // bConfigurationValue = 1
        0x00,       // iConfiguration = 0
        0xC0,       // bmAttributes = Self-powered
        0x32,       // bMaxPower = 100mA

        // === Interface 0: Audio Control (required by USB Audio Class) ===
        // Standard AC Interface Descriptor (9 bytes)
        0x09,       // bLength
        0x04,       // bDescriptorType = INTERFACE
        0x00,       // bInterfaceNumber = 0
        0x00,       // bAlternateSetting = 0
        0x00,       // bNumEndpoints = 0
        0x01,       // bInterfaceClass = Audio
        0x01,       // bInterfaceSubClass = Audio Control
        0x00,       // bInterfaceProtocol
        0x00,       // iInterface = 0

        // Class-specific AC Interface Descriptor (9 bytes)
        0x09,       // bLength
        0x24,       // bDescriptorType = CS_INTERFACE
        0x01,       // bDescriptorSubtype = HEADER
        0x00, 0x01, // bcdADC = 1.00
        0x09, 0x00, // wTotalLength = 9 (header only)
        0x01,       // bInCollection = 1 (1 streaming interface)
        0x01,       // baInterfaceNr(1) = 1 (MIDI Streaming interface)

        // === Interface 1: MIDI Streaming ===
        // Standard MS Interface Descriptor (9 bytes)
        0x09,       // bLength
        0x04,       // bDescriptorType = INTERFACE
        0x01,       // bInterfaceNumber = 1
        0x00,       // bAlternateSetting = 0
        0x02,       // bNumEndpoints = 2
        0x01,       // bInterfaceClass = Audio
        0x03,       // bInterfaceSubClass = MIDI Streaming
        0x00,       // bInterfaceProtocol
        0x00,       // iInterface = 0

        // Class-specific MS Interface Descriptor (7 bytes)
        0x07,       // bLength
        0x24,       // bDescriptorType = CS_INTERFACE
        0x01,       // bDescriptorSubtype = MS_HEADER
        0x00, 0x01, // bcdMSC = 1.00
        0x41, 0x00, // wTotalLength = 65 (7+6+9+6+9+9+5+9+5)

        // MIDI IN Jack (Embedded) - receives from host (6 bytes)
        0x06,       // bLength
        0x24,       // bDescriptorType = CS_INTERFACE
        0x02,       // bDescriptorSubtype = MIDI_IN_JACK
        0x01,       // bJackType = Embedded
        0x01,       // bJackID = 1
        0x00,       // iJack = 0

        // MIDI IN Jack (External) - physical MIDI input (6 bytes)
        0x06,       // bLength
        0x24,       // bDescriptorType = CS_INTERFACE
        0x02,       // bDescriptorSubtype = MIDI_IN_JACK
        0x02,       // bJackType = External
        0x02,       // bJackID = 2
        0x00,       // iJack = 0

        // MIDI OUT Jack (Embedded) - sends to host (9 bytes)
        0x09,       // bLength
        0x24,       // bDescriptorType = CS_INTERFACE
        0x03,       // bDescriptorSubtype = MIDI_OUT_JACK
        0x01,       // bJackType = Embedded
        0x03,       // bJackID = 3
        0x01,       // bNrInputPins = 1
        0x02,       // BaSourceID(1) = 2 (External IN Jack)
        0x01,       // BaSourcePin(1) = 1
        0x00,       // iJack = 0

        // MIDI OUT Jack (External) - physical MIDI output (9 bytes)
        0x09,       // bLength
        0x24,       // bDescriptorType = CS_INTERFACE
        0x03,       // bDescriptorSubtype = MIDI_OUT_JACK
        0x02,       // bJackType = External
        0x04,       // bJackID = 4
        0x01,       // bNrInputPins = 1
        0x01,       // BaSourceID(1) = 1 (Embedded IN Jack)
        0x01,       // BaSourcePin(1) = 1
        0x00,       // iJack = 0

        // === Endpoint 1 OUT (Bulk, Host -> Device) ===
        // Standard Endpoint Descriptor (9 bytes)
        0x09,       // bLength
        0x05,       // bDescriptorType = ENDPOINT
        0x01,       // bEndpointAddress = EP1 OUT
        0x02,       // bmAttributes = Bulk
        0x40, 0x00, // wMaxPacketSize = 64
        0x00,       // bInterval = 0 (ignored for Bulk)
        0x00,       // bRefresh = 0
        0x00,       // bSynchAddress = 0

        // Class-specific MS Bulk OUT Endpoint Descriptor (5 bytes)
        0x05,       // bLength
        0x25,       // bDescriptorType = CS_ENDPOINT
        0x01,       // bDescriptorSubtype = MS_GENERAL
        0x01,       // bNumEmbMIDIJack = 1
        0x01,       // BaAssocJackID(1) = 1 (Embedded IN Jack)

        // === Endpoint 1 IN (Bulk, Device -> Host) ===
        // Standard Endpoint Descriptor (9 bytes)
        0x09,       // bLength
        0x05,       // bDescriptorType = ENDPOINT
        0x81,       // bEndpointAddress = EP1 IN
        0x02,       // bmAttributes = Bulk
        0x40, 0x00, // wMaxPacketSize = 64
        0x00,       // bInterval = 0 (ignored for Bulk)
        0x00,       // bRefresh = 0
        0x00,       // bSynchAddress = 0

        // Class-specific MS Bulk IN Endpoint Descriptor (5 bytes)
        0x05,       // bLength
        0x25,       // bDescriptorType = CS_ENDPOINT
        0x01,       // bDescriptorSubtype = MS_GENERAL
        0x01,       // bNumEmbMIDIJack = 1
        0x03,       // BaAssocJackID(1) = 3 (Embedded OUT Jack)
    };

    // String descriptors
    static constexpr uint8_t STRING_LANGID[] = {0x04, 0x03, 0x09, 0x04};  // English

    static constexpr uint8_t STRING_MANUFACTURER[] = {
        18, 0x03, 'U', 0, 'M', 0, 'I', 0, '-', 0, 'O', 0, 'S', 0, ' ', 0, ' ', 0};

    static constexpr uint8_t STRING_PRODUCT[] = {
        26, 0x03, 'U', 0, 'M', 0, 'I', 0, ' ', 0, 'S', 0, 'y', 0, 'n', 0, 't', 0, 'h', 0, ' ', 0, ' ', 0, ' ', 0};

    USBDevice& usb;

    // MIDI receive callback
    void (*on_midi)(uint8_t cable, const uint8_t* data, uint8_t len) = nullptr;

    // SysEx receive callback (for standard IO over SysEx)
    void (*on_sysex)(const uint8_t* data, uint16_t len) = nullptr;

    explicit USB_MIDI(USBDevice& u) : usb(u) {
        usb.on_setup = [](uint8_t* /*data*/) {
            // Will be replaced with proper callback
        };
    }

    void init() {
        usb.init();

        // Set up callbacks
        usb.on_setup = handle_setup_static;
        usb.on_rx = handle_rx_static;

        // Store this pointer for static callbacks
        instance_ = this;
    }

    void poll() {
        usb.handle_irq();
    }

    /// Send MIDI message (3 bytes)
    void send_midi(uint8_t cable, uint8_t b0, uint8_t b1, uint8_t b2) {
        // USB-MIDI Event Packet
        uint8_t cin = (b0 >> 4) & 0x0F;  // Code Index Number
        uint8_t packet[4] = {
            static_cast<uint8_t>((cable << 4) | cin),
            b0, b1, b2
        };
        usb.ep_write(EP_MIDI_IN, packet, 4);
    }

    /// Send SysEx message (for standard IO)
    void send_sysex(const uint8_t* data, uint16_t len) {
        uint8_t packet[64];
        uint16_t pos = 0;
        uint16_t pkt_pos = 0;

        while (pos < len) {
            uint16_t remaining = len - pos;

            if (pos == 0) {
                // SysEx start
                if (remaining >= 3) {
                    packet[pkt_pos++] = 0x04;  // SysEx start
                    packet[pkt_pos++] = data[pos++];
                    packet[pkt_pos++] = data[pos++];
                    packet[pkt_pos++] = data[pos++];
                } else if (remaining == 2) {
                    packet[pkt_pos++] = 0x06;  // SysEx ends with 2 bytes
                    packet[pkt_pos++] = data[pos++];
                    packet[pkt_pos++] = data[pos++];
                    packet[pkt_pos++] = 0;
                } else {
                    packet[pkt_pos++] = 0x05;  // SysEx ends with 1 byte
                    packet[pkt_pos++] = data[pos++];
                    packet[pkt_pos++] = 0;
                    packet[pkt_pos++] = 0;
                }
            } else if (remaining >= 3) {
                packet[pkt_pos++] = 0x04;  // SysEx continue
                packet[pkt_pos++] = data[pos++];
                packet[pkt_pos++] = data[pos++];
                packet[pkt_pos++] = data[pos++];
            } else if (remaining == 2) {
                packet[pkt_pos++] = 0x06;  // SysEx ends with 2 bytes
                packet[pkt_pos++] = data[pos++];
                packet[pkt_pos++] = data[pos++];
                packet[pkt_pos++] = 0;
            } else {
                packet[pkt_pos++] = 0x05;  // SysEx ends with 1 byte
                packet[pkt_pos++] = data[pos++];
                packet[pkt_pos++] = 0;
                packet[pkt_pos++] = 0;
            }

            // Send when packet is full or done
            if (pkt_pos >= 60 || pos >= len) {
                usb.ep_write(EP_MIDI_IN, packet, pkt_pos);
                pkt_pos = 0;
            }
        }
    }

    bool is_configured() const { return usb.configured; }

private:
    static USB_MIDI* instance_;

    // SysEx accumulation buffer
    uint8_t sysex_buf_[256];
    uint16_t sysex_pos_ = 0;
    bool in_sysex_ = false;

    static inline uint32_t dbg_static_called = 0;
    static void handle_setup_static(uint8_t* data) {
        dbg_static_called++;
        if (instance_) instance_->handle_setup(data);
    }

    static void handle_rx_static(uint8_t ep, uint8_t* data, uint16_t len) {
        if (instance_) instance_->handle_rx(ep, data, len);
    }

    // Debug counters - accessible via GDB
    uint32_t dbg_setup_count = 0;
    uint32_t dbg_get_desc_count = 0;
    uint32_t dbg_set_addr_count = 0;
    uint8_t dbg_last_request = 0;

    void handle_setup(uint8_t* data) {
        dbg_setup_count++;
        // DEBUG: Toggle orange LED (PD13) on every SETUP
        *reinterpret_cast<volatile uint32_t*>(0x40020C14) ^= (1U << 13);

        uint8_t bmRequestType = data[0];
        uint8_t bRequest = data[1];
        uint16_t wValue = data[2] | (data[3] << 8);
        uint16_t wIndex = data[4] | (data[5] << 8);
        uint16_t wLength = data[6] | (data[7] << 8);

        dbg_last_request = bRequest;

        // Standard requests
        if ((bmRequestType & 0x60) == 0x00) {
            switch (bRequest) {
                case 0x06:  // GET_DESCRIPTOR
                    dbg_get_desc_count++;
                    // DEBUG: Red LED on for GET_DESCRIPTOR
                    *reinterpret_cast<volatile uint32_t*>(0x40020C14) |= (1U << 14);
                    handle_get_descriptor(wValue, wLength);
                    break;

                case 0x05:  // SET_ADDRESS
                    dbg_set_addr_count++;
                    // HAL sets address BEFORE sending ZLP (USB_SetDevAddress then CtlSendStatus)
                    // Hardware applies it after status phase completes
                    usb.set_address(static_cast<uint8_t>(wValue));
                    usb.ep_write(0, nullptr, 0);  // ZLP status
                    break;

                case 0x09:  // SET_CONFIGURATION
                    usb.configured = (wValue != 0);
                    if (usb.configured) {
                        // Configure MIDI endpoints
                        usb.configure_endpoint(EP_MIDI_OUT, USB_OTG::EPTYP_BULK,
                                              MIDI_PACKET_SIZE, false);
                        usb.configure_endpoint(EP_MIDI_IN, USB_OTG::EPTYP_BULK,
                                              MIDI_PACKET_SIZE, true);
                    }
                    usb.ep_write(0, nullptr, 0);  // ZLP
                    break;

                case 0x00:  // GET_STATUS
                    {
                        uint8_t status[2] = {0, 0};
                        usb.ep_write(0, status, 2);
                    }
                    break;

                default:
                    usb.ep_stall(0, true);
                    break;
            }
        } else {
            // Class/vendor requests - just ACK for now
            usb.ep_write(0, nullptr, 0);
        }
        (void)wIndex;
    }

    void handle_get_descriptor(uint16_t wValue, uint16_t wLength) {
        uint8_t type = wValue >> 8;
        uint8_t index = wValue & 0xFF;

        switch (type) {
            case 1: {  // Device descriptor
                uint16_t len = (wLength < sizeof(DEVICE_DESC)) ? wLength : sizeof(DEVICE_DESC);
                usb.ep_write(0, DEVICE_DESC, len);
                break;
            }

            case 2: {  // Configuration descriptor
                uint16_t len = (wLength < sizeof(CONFIG_DESC)) ? wLength : sizeof(CONFIG_DESC);
                usb.ep_write(0, CONFIG_DESC, len);
                break;
            }

            case 3: {  // String descriptor
                const uint8_t* str = nullptr;
                uint8_t str_len = 0;
                switch (index) {
                    case 0:
                        str = STRING_LANGID;
                        str_len = sizeof(STRING_LANGID);
                        break;
                    case 1:
                        str = STRING_MANUFACTURER;
                        str_len = sizeof(STRING_MANUFACTURER);
                        break;
                    case 2:
                        str = STRING_PRODUCT;
                        str_len = sizeof(STRING_PRODUCT);
                        break;
                }
                if (str) {
                    uint16_t len = (wLength < str_len) ? wLength : str_len;
                    usb.ep_write(0, str, len);
                } else {
                    usb.ep_stall(0, true);
                }
                break;
            }

            default:
                usb.ep_stall(0, true);
                break;
        }
    }

    // Debug counter for MIDI RX
    uint32_t dbg_midi_rx_count = 0;

    void handle_rx(uint8_t ep, uint8_t* data, uint16_t len) {
        if (ep != EP_MIDI_OUT) return;

        // Debug: Toggle blue LED (PD15) on MIDI receive
        *reinterpret_cast<volatile uint32_t*>(0x40020C14) ^= (1U << 15);
        dbg_midi_rx_count++;

        // Process USB-MIDI packets (4 bytes each)
        for (uint16_t i = 0; i + 3 < len; i += 4) {
            uint8_t cin = data[i] & 0x0F;
            uint8_t cable = data[i] >> 4;
            uint8_t b0 = data[i + 1];
            uint8_t b1 = data[i + 2];
            uint8_t b2 = data[i + 3];

            switch (cin) {
                case 0x04:  // SysEx start or continue
                    if (!in_sysex_) {
                        in_sysex_ = true;
                        sysex_pos_ = 0;
                    }
                    if (sysex_pos_ + 3 < sizeof(sysex_buf_)) {
                        sysex_buf_[sysex_pos_++] = b0;
                        sysex_buf_[sysex_pos_++] = b1;
                        sysex_buf_[sysex_pos_++] = b2;
                    }
                    break;

                case 0x05:  // SysEx ends with 1 byte
                    if (sysex_pos_ + 1 < sizeof(sysex_buf_)) {
                        sysex_buf_[sysex_pos_++] = b0;
                    }
                    if (on_sysex && sysex_pos_ > 0) {
                        on_sysex(sysex_buf_, sysex_pos_);
                    }
                    in_sysex_ = false;
                    sysex_pos_ = 0;
                    break;

                case 0x06:  // SysEx ends with 2 bytes
                    if (sysex_pos_ + 2 < sizeof(sysex_buf_)) {
                        sysex_buf_[sysex_pos_++] = b0;
                        sysex_buf_[sysex_pos_++] = b1;
                    }
                    if (on_sysex && sysex_pos_ > 0) {
                        on_sysex(sysex_buf_, sysex_pos_);
                    }
                    in_sysex_ = false;
                    sysex_pos_ = 0;
                    break;

                case 0x07:  // SysEx ends with 3 bytes
                    if (sysex_pos_ + 3 < sizeof(sysex_buf_)) {
                        sysex_buf_[sysex_pos_++] = b0;
                        sysex_buf_[sysex_pos_++] = b1;
                        sysex_buf_[sysex_pos_++] = b2;
                    }
                    if (on_sysex && sysex_pos_ > 0) {
                        on_sysex(sysex_buf_, sysex_pos_);
                    }
                    in_sysex_ = false;
                    sysex_pos_ = 0;
                    break;

                case 0x08:  // Note Off
                case 0x09:  // Note On
                case 0x0A:  // Poly Aftertouch
                case 0x0B:  // Control Change
                case 0x0E:  // Pitch Bend
                    if (on_midi) {
                        uint8_t msg[3] = {b0, b1, b2};
                        on_midi(cable, msg, 3);
                    }
                    break;

                case 0x0C:  // Program Change
                case 0x0D:  // Channel Aftertouch
                    if (on_midi) {
                        uint8_t msg[2] = {b0, b1};
                        on_midi(cable, msg, 2);
                    }
                    break;

                default:
                    break;
            }
        }
    }
};

// Static instance pointer
inline USB_MIDI* USB_MIDI::instance_ = nullptr;

}  // namespace umi::stm32
