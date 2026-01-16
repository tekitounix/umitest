// SPDX-License-Identifier: MIT
// UMI-OS - GUI Backend Interface
//
// 描画先の抽象化。組み込み(FrameBuffer)とWeb(Canvas2D)で共通。

#pragma once

#include <cstdint>

namespace umi::gui {

// ============================================================================
// Geometry Types
// ============================================================================

struct Point {
    int16_t x = 0;
    int16_t y = 0;
};

struct Rect {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;
    
    constexpr bool contains(int16_t px, int16_t py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    
    constexpr int16_t center_x() const noexcept { return x + w / 2; }
    constexpr int16_t center_y() const noexcept { return y + h / 2; }
    constexpr int16_t right() const noexcept { return x + w; }
    constexpr int16_t bottom() const noexcept { return y + h; }
};

// ============================================================================
// Color (ARGB8888)
// ============================================================================

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
    
    constexpr Color() = default;
    constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
    
    constexpr explicit Color(uint32_t argb)
        : r((argb >> 16) & 0xFF)
        , g((argb >> 8) & 0xFF)
        , b(argb & 0xFF)
        , a((argb >> 24) & 0xFF) {}
    
    constexpr uint32_t to_argb() const noexcept {
        return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }
    
    constexpr uint32_t to_rgba() const noexcept {
        return (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | a;
    }
    
    // Predefined colors
    static constexpr Color black()   { return {0, 0, 0}; }
    static constexpr Color white()   { return {255, 255, 255}; }
    static constexpr Color red()     { return {255, 0, 0}; }
    static constexpr Color green()   { return {0, 255, 0}; }
    static constexpr Color blue()    { return {0, 0, 255}; }
    static constexpr Color gray()    { return {128, 128, 128}; }
    static constexpr Color dark()    { return {32, 32, 32}; }
    
    // Utility
    constexpr Color with_alpha(uint8_t new_a) const {
        return {r, g, b, new_a};
    }
};

// ============================================================================
// Backend Interface
// ============================================================================

/// 描画バックエンド抽象インターフェース
class IBackend {
public:
    virtual ~IBackend() = default;
    
    // Canvas info
    virtual uint16_t width() const = 0;
    virtual uint16_t height() const = 0;
    
    // Primitives
    virtual void set_pixel(int16_t x, int16_t y, Color c) = 0;
    virtual void fill_rect(Rect r, Color c) = 0;
    virtual void draw_rect(Rect r, Color c) = 0;
    virtual void draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Color c) = 0;
    virtual void draw_circle(int16_t cx, int16_t cy, int16_t radius, Color c) = 0;
    virtual void fill_circle(int16_t cx, int16_t cy, int16_t radius, Color c) = 0;
    virtual void draw_arc(int16_t cx, int16_t cy, int16_t radius, 
                          float start_angle, float end_angle, Color c) = 0;
    
    // Text
    virtual void draw_text(int16_t x, int16_t y, const char* text, Color c) = 0;
    virtual int16_t text_width(const char* text) = 0;
    
    // Frame management
    virtual void begin_frame() {}
    virtual void end_frame() {}
};

} // namespace umi::gui
