// SPDX-License-Identifier: MIT
// UMI-OS - FrameBuffer Backend
//
// 組み込み向けIBackend実装。
// メモリ上のフレームバッファに描画。

#pragma once

#include "backend.hh"
#include <cstring>
#include <cstdlib>

namespace umi::gui {

/// FrameBuffer Backend for embedded targets
template<typename PixelT = uint16_t>
class FrameBufferBackend : public IBackend {
public:
    // RGB565 color conversion
    static constexpr uint16_t to_rgb565(Color c) {
        return ((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3);
    }
    
    // RGB888 to pixel (specialization handles different formats)
    static PixelT to_pixel(Color c);
    
    FrameBufferBackend(PixelT* buffer, uint16_t w, uint16_t h)
        : buffer_(buffer), width_(w), height_(h) {}
    
    uint16_t width() const override { return width_; }
    uint16_t height() const override { return height_; }
    
    PixelT* buffer() { return buffer_; }
    const PixelT* buffer() const { return buffer_; }
    
    void begin_frame() override {
        // Clear to black
        std::memset(buffer_, 0, width_ * height_ * sizeof(PixelT));
    }
    
    void end_frame() override {
        // Embedded: trigger DMA/SPI transfer here if needed
    }
    
    void set_pixel(int16_t x, int16_t y, Color c) override {
        if (x >= 0 && x < width_ && y >= 0 && y < height_) {
            buffer_[y * width_ + x] = to_pixel(c);
        }
    }
    
    void fill_rect(Rect r, Color c) override {
        PixelT pixel = to_pixel(c);
        int16_t x0 = (r.x < 0) ? 0 : r.x;
        int16_t y0 = (r.y < 0) ? 0 : r.y;
        int16_t x1 = (r.x + r.w > width_) ? width_ : r.x + r.w;
        int16_t y1 = (r.y + r.h > height_) ? height_ : r.y + r.h;
        
        for (int16_t y = y0; y < y1; ++y) {
            for (int16_t x = x0; x < x1; ++x) {
                buffer_[y * width_ + x] = pixel;
            }
        }
    }
    
    void draw_rect(Rect r, Color c) override {
        // Top
        draw_hline(r.x, r.y, r.w, c);
        // Bottom
        draw_hline(r.x, r.y + r.h - 1, r.w, c);
        // Left
        draw_vline(r.x, r.y, r.h, c);
        // Right
        draw_vline(r.x + r.w - 1, r.y, r.h, c);
    }
    
    void draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Color c) override {
        // Bresenham's line algorithm
        int16_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
        int16_t dy = y1 > y0 ? y1 - y0 : y0 - y1;
        int16_t sx = x0 < x1 ? 1 : -1;
        int16_t sy = y0 < y1 ? 1 : -1;
        int16_t err = dx - dy;
        
        PixelT pixel = to_pixel(c);
        
        while (true) {
            if (x0 >= 0 && x0 < width_ && y0 >= 0 && y0 < height_) {
                buffer_[y0 * width_ + x0] = pixel;
            }
            
            if (x0 == x1 && y0 == y1) break;
            
            int16_t e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
    
    void draw_circle(int16_t cx, int16_t cy, int16_t radius, Color c) override {
        // Midpoint circle algorithm
        int16_t x = radius;
        int16_t y = 0;
        int16_t err = 0;
        PixelT pixel = to_pixel(c);
        
        while (x >= y) {
            plot8(cx, cy, x, y, pixel);
            y++;
            err += 1 + 2 * y;
            if (2 * (err - x) + 1 > 0) {
                x--;
                err += 1 - 2 * x;
            }
        }
    }
    
    void fill_circle(int16_t cx, int16_t cy, int16_t radius, Color c) override {
        PixelT pixel = to_pixel(c);
        for (int16_t y = -radius; y <= radius; ++y) {
            for (int16_t x = -radius; x <= radius; ++x) {
                if (x * x + y * y <= radius * radius) {
                    int16_t px = cx + x;
                    int16_t py = cy + y;
                    if (px >= 0 && px < width_ && py >= 0 && py < height_) {
                        buffer_[py * width_ + px] = pixel;
                    }
                }
            }
        }
    }
    
    void draw_arc(int16_t cx, int16_t cy, int16_t radius, 
                  float start_angle, float end_angle, Color c) override {
        // Simple arc drawing by stepping through angle
        PixelT pixel = to_pixel(c);
        float step = 0.02f;  // ~3 degree steps
        
        for (float a = start_angle; a <= end_angle; a += step) {
            int16_t x = cx + static_cast<int16_t>(radius * std::sin(a));
            int16_t y = cy - static_cast<int16_t>(radius * std::cos(a));
            if (x >= 0 && x < width_ && y >= 0 && y < height_) {
                buffer_[y * width_ + x] = pixel;
            }
        }
    }
    
    void draw_text(int16_t x, int16_t y, const char* text, Color c) override {
        // Simple 5x7 font rendering
        // For now, just draw placeholder rectangles
        PixelT pixel = to_pixel(c);
        int16_t cx = x;
        for (const char* p = text; *p; ++p) {
            // Draw a simple 5x7 placeholder for each char
            for (int16_t row = 0; row < 7; ++row) {
                for (int16_t col = 0; col < 5; ++col) {
                    // Use a simple pattern based on char value
                    bool on = get_font_pixel(*p, col, row);
                    if (on) {
                        int16_t px = cx + col;
                        int16_t py = y + row;
                        if (px >= 0 && px < width_ && py >= 0 && py < height_) {
                            buffer_[py * width_ + px] = pixel;
                        }
                    }
                }
            }
            cx += 6;  // 5 + 1 spacing
        }
    }
    
    int16_t text_width(const char* text) override {
        int16_t w = 0;
        for (const char* p = text; *p; ++p) w += 6;
        return w > 0 ? w - 1 : 0;  // Remove last spacing
    }
    
private:
    void draw_hline(int16_t x, int16_t y, int16_t w, Color c) {
        if (y < 0 || y >= height_) return;
        PixelT pixel = to_pixel(c);
        int16_t x0 = (x < 0) ? 0 : x;
        int16_t x1 = (x + w > width_) ? width_ : x + w;
        for (int16_t i = x0; i < x1; ++i) {
            buffer_[y * width_ + i] = pixel;
        }
    }
    
    void draw_vline(int16_t x, int16_t y, int16_t h, Color c) {
        if (x < 0 || x >= width_) return;
        PixelT pixel = to_pixel(c);
        int16_t y0 = (y < 0) ? 0 : y;
        int16_t y1 = (y + h > height_) ? height_ : y + h;
        for (int16_t i = y0; i < y1; ++i) {
            buffer_[i * width_ + x] = pixel;
        }
    }
    
    void plot8(int16_t cx, int16_t cy, int16_t x, int16_t y, PixelT pixel) {
        auto plot = [&](int16_t px, int16_t py) {
            if (px >= 0 && px < width_ && py >= 0 && py < height_) {
                buffer_[py * width_ + px] = pixel;
            }
        };
        plot(cx + x, cy + y);
        plot(cx - x, cy + y);
        plot(cx + x, cy - y);
        plot(cx - x, cy - y);
        plot(cx + y, cy + x);
        plot(cx - y, cy + x);
        plot(cx + y, cy - x);
        plot(cx - y, cy - x);
    }
    
    // Simple 5x7 font (ASCII 32-127)
    static bool get_font_pixel(char ch, int col, int row) {
        // Minimal font data - just enough for common chars
        static const uint8_t font_data[][5] = {
            {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
            {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 '!'
            {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 '"'
            {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 '#'
            {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 '$'
            {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 '%'
            {0x36, 0x49, 0x56, 0x20, 0x50}, // 38 '&'
            {0x00, 0x08, 0x07, 0x03, 0x00}, // 39 '''
            {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 '('
            {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 ')'
            {0x2A, 0x1C, 0x7F, 0x1C, 0x2A}, // 42 '*'
            {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 '+'
            {0x00, 0x80, 0x70, 0x30, 0x00}, // 44 ','
            {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 '-'
            {0x00, 0x00, 0x60, 0x60, 0x00}, // 46 '.'
            {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 '/'
            {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 '0'
            {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 '1'
            {0x72, 0x49, 0x49, 0x49, 0x46}, // 50 '2'
            {0x21, 0x41, 0x49, 0x4D, 0x33}, // 51 '3'
            {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 '4'
            {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 '5'
            {0x3C, 0x4A, 0x49, 0x49, 0x31}, // 54 '6'
            {0x41, 0x21, 0x11, 0x09, 0x07}, // 55 '7'
            {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 '8'
            {0x46, 0x49, 0x49, 0x29, 0x1E}, // 57 '9'
            {0x00, 0x00, 0x14, 0x00, 0x00}, // 58 ':'
            {0x00, 0x40, 0x34, 0x00, 0x00}, // 59 ';'
            {0x00, 0x08, 0x14, 0x22, 0x41}, // 60 '<'
            {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 '='
            {0x00, 0x41, 0x22, 0x14, 0x08}, // 62 '>'
            {0x02, 0x01, 0x59, 0x09, 0x06}, // 63 '?'
            {0x3E, 0x41, 0x5D, 0x59, 0x4E}, // 64 '@'
            {0x7C, 0x12, 0x11, 0x12, 0x7C}, // 65 'A'
            {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66 'B'
            {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67 'C'
            {0x7F, 0x41, 0x41, 0x41, 0x3E}, // 68 'D'
            {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69 'E'
            {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70 'F'
            {0x3E, 0x41, 0x41, 0x51, 0x73}, // 71 'G'
            {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72 'H'
            {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73 'I'
            {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74 'J'
            {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75 'K'
            {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76 'L'
            {0x7F, 0x02, 0x1C, 0x02, 0x7F}, // 77 'M'
            {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78 'N'
            {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79 'O'
            {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80 'P'
            {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81 'Q'
            {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82 'R'
            {0x26, 0x49, 0x49, 0x49, 0x32}, // 83 'S'
            {0x03, 0x01, 0x7F, 0x01, 0x03}, // 84 'T'
            {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85 'U'
            {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86 'V'
            {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87 'W'
            {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 'X'
            {0x03, 0x04, 0x78, 0x04, 0x03}, // 89 'Y'
            {0x61, 0x59, 0x49, 0x4D, 0x43}, // 90 'Z'
        };
        
        int idx = static_cast<unsigned char>(ch) - 32;
        if (idx < 0 || idx >= static_cast<int>(sizeof(font_data) / sizeof(font_data[0]))) {
            return false;
        }
        
        if (col < 0 || col >= 5 || row < 0 || row >= 7) {
            return false;
        }
        
        return (font_data[idx][col] >> row) & 1;
    }
    
    PixelT* buffer_;
    uint16_t width_;
    uint16_t height_;
};

// Specialization for RGB565
template<>
inline uint16_t FrameBufferBackend<uint16_t>::to_pixel(Color c) {
    return to_rgb565(c);
}

// Specialization for ARGB8888
template<>
inline uint32_t FrameBufferBackend<uint32_t>::to_pixel(Color c) {
    return c.to_argb();
}

// Type aliases
using FrameBufferBackend565 = FrameBufferBackend<uint16_t>;
using FrameBufferBackend8888 = FrameBufferBackend<uint32_t>;

} // namespace umi::gui
