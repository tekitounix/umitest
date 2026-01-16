// SPDX-License-Identifier: MIT
// UMI-OS - Canvas2D Backend
//
// Web向けIBackend実装。
// Emscriptenを経由してJavaScriptのCanvas2D APIを呼び出す。

#pragma once

#include "backend.hh"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// EM_JS: Define JS functions inline
// These get the canvas context from a global UmiCanvas object

EM_JS(void, canvas_set_size, (int w, int h), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.setSize(w, h);
    }
});

EM_JS(void, canvas_begin_frame, (), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.beginFrame();
    }
});

EM_JS(void, canvas_end_frame, (), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.endFrame();
    }
});

EM_JS(void, canvas_set_pixel, (int x, int y, unsigned int rgba), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.setPixel(x, y, rgba);
    }
});

EM_JS(void, canvas_fill_rect, (int x, int y, int w, int h, unsigned int rgba), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.fillRect(x, y, w, h, rgba);
    }
});

EM_JS(void, canvas_stroke_rect, (int x, int y, int w, int h, unsigned int rgba), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.strokeRect(x, y, w, h, rgba);
    }
});

EM_JS(void, canvas_draw_line, (int x0, int y0, int x1, int y1, unsigned int rgba), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.drawLine(x0, y0, x1, y1, rgba);
    }
});

EM_JS(void, canvas_stroke_circle, (int cx, int cy, int r, unsigned int rgba), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.strokeCircle(cx, cy, r, rgba);
    }
});

EM_JS(void, canvas_fill_circle, (int cx, int cy, int r, unsigned int rgba), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.fillCircle(cx, cy, r, rgba);
    }
});

EM_JS(void, canvas_stroke_arc, (int cx, int cy, int r, float start, float end, unsigned int rgba), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.strokeArc(cx, cy, r, start, end, rgba);
    }
});

EM_JS(void, canvas_draw_text, (int x, int y, const char* text, unsigned int rgba), {
    if (typeof UmiCanvas !== 'undefined') {
        UmiCanvas.drawText(x, y, UTF8ToString(text), rgba);
    }
});

EM_JS(int, canvas_text_width, (const char* text), {
    if (typeof UmiCanvas !== 'undefined') {
        return UmiCanvas.textWidth(UTF8ToString(text));
    }
    return 0;
});

#endif // __EMSCRIPTEN__

namespace umi::gui {

/// Canvas2D Backend for Web
class Canvas2DBackend : public IBackend {
public:
    Canvas2DBackend(uint16_t w = 320, uint16_t h = 240) 
        : width_(w), height_(h) {
#ifdef __EMSCRIPTEN__
        canvas_set_size(w, h);
#endif
    }
    
    uint16_t width() const override { return width_; }
    uint16_t height() const override { return height_; }
    
    void set_size(uint16_t w, uint16_t h) {
        width_ = w;
        height_ = h;
#ifdef __EMSCRIPTEN__
        canvas_set_size(w, h);
#endif
    }
    
    void begin_frame() override {
#ifdef __EMSCRIPTEN__
        canvas_begin_frame();
#endif
    }
    
    void end_frame() override {
#ifdef __EMSCRIPTEN__
        canvas_end_frame();
#endif
    }
    
    void set_pixel(int16_t x, int16_t y, Color c) override {
#ifdef __EMSCRIPTEN__
        canvas_set_pixel(x, y, c.to_rgba());
#else
        (void)x; (void)y; (void)c;
#endif
    }
    
    void fill_rect(Rect r, Color c) override {
#ifdef __EMSCRIPTEN__
        canvas_fill_rect(r.x, r.y, r.w, r.h, c.to_rgba());
#else
        (void)r; (void)c;
#endif
    }
    
    void draw_rect(Rect r, Color c) override {
#ifdef __EMSCRIPTEN__
        canvas_stroke_rect(r.x, r.y, r.w, r.h, c.to_rgba());
#else
        (void)r; (void)c;
#endif
    }
    
    void draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Color c) override {
#ifdef __EMSCRIPTEN__
        canvas_draw_line(x0, y0, x1, y1, c.to_rgba());
#else
        (void)x0; (void)y0; (void)x1; (void)y1; (void)c;
#endif
    }
    
    void draw_circle(int16_t cx, int16_t cy, int16_t radius, Color c) override {
#ifdef __EMSCRIPTEN__
        canvas_stroke_circle(cx, cy, radius, c.to_rgba());
#else
        (void)cx; (void)cy; (void)radius; (void)c;
#endif
    }
    
    void fill_circle(int16_t cx, int16_t cy, int16_t radius, Color c) override {
#ifdef __EMSCRIPTEN__
        canvas_fill_circle(cx, cy, radius, c.to_rgba());
#else
        (void)cx; (void)cy; (void)radius; (void)c;
#endif
    }
    
    void draw_arc(int16_t cx, int16_t cy, int16_t radius, 
                  float start_angle, float end_angle, Color c) override {
#ifdef __EMSCRIPTEN__
        canvas_stroke_arc(cx, cy, radius, start_angle, end_angle, c.to_rgba());
#else
        (void)cx; (void)cy; (void)radius; (void)start_angle; (void)end_angle; (void)c;
#endif
    }
    
    void draw_text(int16_t x, int16_t y, const char* text, Color c) override {
#ifdef __EMSCRIPTEN__
        canvas_draw_text(x, y, text, c.to_rgba());
#else
        (void)x; (void)y; (void)text; (void)c;
#endif
    }
    
    int16_t text_width(const char* text) override {
#ifdef __EMSCRIPTEN__
        return static_cast<int16_t>(canvas_text_width(text));
#else
        // Fallback: estimate 6px per char
        int16_t w = 0;
        for (const char* p = text; *p; ++p) w += 6;
        return w;
#endif
    }
    
private:
    uint16_t width_;
    uint16_t height_;
};

} // namespace umi::gui
