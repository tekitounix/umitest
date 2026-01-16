// SPDX-License-Identifier: MIT
// UMI-OS - GUI Layout
//
// コンポーネントの配置とサイズを定義。
// スキンとは独立して、画面サイズに応じた配置を管理。

#pragma once

#include <cstdint>

namespace umi::gui {

/// レイアウト定義
struct Layout {
    // Canvas size
    uint16_t width      = 800;
    uint16_t height     = 480;
    
    // Component sizes
    uint16_t knob_size      = 64;
    uint16_t slider_width   = 24;
    uint16_t slider_height  = 120;
    uint16_t button_width   = 80;
    uint16_t button_height  = 32;
    uint16_t meter_width    = 16;
    uint16_t meter_height   = 120;
    
    // Spacing
    uint16_t padding        = 16;
    uint16_t spacing        = 12;
    uint16_t label_height   = 16;
    uint16_t group_spacing  = 24;
    
    // Presets
    static constexpr Layout compact() {
        Layout l;
        l.width = 320; l.height = 240;
        l.knob_size = 40;
        l.slider_width = 16; l.slider_height = 80;
        l.button_width = 50; l.button_height = 24;
        l.meter_width = 12; l.meter_height = 80;
        l.padding = 8; l.spacing = 6;
        l.label_height = 12; l.group_spacing = 12;
        return l;
    }
    
    static constexpr Layout standard() { return Layout{}; }
    
    static constexpr Layout large() {
        Layout l;
        l.width = 1280; l.height = 720;
        l.knob_size = 96;
        l.slider_width = 32; l.slider_height = 180;
        l.button_width = 120; l.button_height = 48;
        l.meter_width = 24; l.meter_height = 180;
        l.padding = 24; l.spacing = 16;
        l.label_height = 20; l.group_spacing = 32;
        return l;
    }
};

} // namespace umi::gui
