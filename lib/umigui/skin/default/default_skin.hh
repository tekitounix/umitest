// SPDX-License-Identifier: MIT
// UMI-OS - Default Skin
//
// シンプルなデフォルトスキン

#pragma once

#include "../skin.hh"
#include <cmath>

namespace umi::gui::skin {

/// デフォルトスキンのカラーパレット
struct DefaultPalette {
    Color background    = Color{32, 32, 32};
    Color foreground    = Color::white();
    Color accent        = Color{0, 160, 255};
    
    Color knob_body     = Color{60, 60, 60};
    Color knob_indicator = Color::white();
    
    Color slider_track  = Color{40, 40, 40};
    Color slider_thumb  = Color{100, 100, 100};
    
    Color button_normal = Color{60, 60, 60};
    Color button_pressed = Color{80, 80, 80};
    Color button_toggled = Color{0, 160, 255};
    
    Color meter_bg      = Color{30, 30, 30};
    Color meter_green   = Color{0, 200, 0};
    Color meter_yellow  = Color{200, 200, 0};
    Color meter_red     = Color{200, 0, 0};
    
    Color text          = Color::white();
    Color text_dim      = Color::gray();
};

/// デフォルト回転系描画
class DefaultRotary : public IRotary {
public:
    DefaultPalette& palette;
    
    DefaultRotary(DefaultPalette& p) : palette(p) {}
    
    void draw(IBackend& backend, const ui::Knob& knob, Rect bounds) override {
        const int16_t cx = bounds.center_x();
        const int16_t cy = bounds.center_y();
        const int16_t radius = (bounds.w < bounds.h ? bounds.w : bounds.h) / 2 - 2;
        
        // Body
        Color body_color = knob.pressed ? palette.accent : palette.knob_body;
        backend.fill_circle(cx, cy, radius, body_color);
        
        // Indicator line
        constexpr float start_angle = -2.356f;  // -135 degrees
        constexpr float end_angle = 2.356f;     // +135 degrees
        float angle = start_angle + knob.value * (end_angle - start_angle);
        int16_t ix = cx + static_cast<int16_t>(radius * 0.7f * std::sin(angle));
        int16_t iy = cy - static_cast<int16_t>(radius * 0.7f * std::cos(angle));
        backend.draw_line(cx, cy, ix, iy, palette.knob_indicator);
        
        // Label
        if (knob.name) {
            backend.draw_text(bounds.x, bounds.bottom() + 2, knob.name, palette.text);
        }
    }
};

/// デフォルト直線系描画
class DefaultLinear : public ILinear {
public:
    DefaultPalette& palette;
    
    DefaultLinear(DefaultPalette& p) : palette(p) {}
    
    void draw(IBackend& backend, const ui::Slider& slider, Rect bounds) override {
        // Track
        backend.fill_rect(bounds, palette.slider_track);
        
        // Thumb
        Rect thumb;
        if (slider.vertical) {
            int16_t thumb_h = 8;
            int16_t range = bounds.h - thumb_h;
            int16_t thumb_y = bounds.y + bounds.h - thumb_h - static_cast<int16_t>(slider.value * range);
            thumb = {bounds.x, thumb_y, bounds.w, thumb_h};
        } else {
            int16_t thumb_w = 8;
            int16_t range = bounds.w - thumb_w;
            int16_t thumb_x = bounds.x + static_cast<int16_t>(slider.value * range);
            thumb = {thumb_x, bounds.y, thumb_w, bounds.h};
        }
        
        Color thumb_color = slider.pressed ? palette.accent : palette.slider_thumb;
        backend.fill_rect(thumb, thumb_color);
        
        // Label
        if (slider.name) {
            if (slider.vertical) {
                backend.draw_text(bounds.x, bounds.y - 12, slider.name, palette.text);
            } else {
                backend.draw_text(bounds.x, bounds.bottom() + 2, slider.name, palette.text);
            }
        }
    }
};

/// デフォルトON/OFF系描画
class DefaultToggle : public IToggle {
public:
    DefaultPalette& palette;
    
    DefaultToggle(DefaultPalette& p) : palette(p) {}
    
    void draw(IBackend& backend, const ui::Button& button, Rect bounds) override {
        bool active = (button.mode == ui::Button::Mode::Momentary) 
            ? button.pressed : button.is_on();
        
        // Background
        Color bg = active ? palette.button_toggled :
                  (button.pressed ? palette.button_pressed : palette.button_normal);
        backend.fill_rect(bounds, bg);
        
        // Border
        backend.draw_rect(bounds, button.focused ? palette.accent : palette.foreground);
        
        // Label
        if (button.name) {
            int16_t text_w = 0;
            for (const char* p = button.name; *p; ++p) text_w += 6;
            int16_t tx = bounds.x + (bounds.w - text_w) / 2;
            int16_t ty = bounds.y + (bounds.h - 8) / 2;
            backend.draw_text(tx, ty, button.name, active ? palette.background : palette.text);
        }
    }
};

/// デフォルト選択系描画
class DefaultSelector : public ISelector {
public:
    DefaultPalette& palette;
    
    DefaultSelector(DefaultPalette& p) : palette(p) {}
    
    void draw(IBackend& backend, const ui::Selector& selector, Rect bounds) override {
        // Background
        backend.fill_rect(bounds, palette.button_normal);
        backend.draw_rect(bounds, palette.foreground);
        
        // Selected option
        const char* text = selector.selected_option();
        if (text) {
            int16_t text_w = 0;
            for (const char* p = text; *p; ++p) text_w += 6;
            int16_t tx = bounds.x + (bounds.w - text_w) / 2;
            int16_t ty = bounds.y + (bounds.h - 8) / 2;
            backend.draw_text(tx, ty, text, palette.text);
        }
    }
};

/// デフォルトメーター描画
class DefaultMeter : public IMeter {
public:
    DefaultPalette& palette;
    
    DefaultMeter(DefaultPalette& p) : palette(p) {}
    
    void draw(IBackend& backend, const ui::Meter& meter, Rect bounds) override {
        // Background
        backend.fill_rect(bounds, palette.meter_bg);
        
        // Filled region (vertical)
        int16_t fill_h = static_cast<int16_t>(meter.value * bounds.h);
        Rect filled = {
            bounds.x,
            static_cast<int16_t>(bounds.y + bounds.h - fill_h),
            bounds.w,
            fill_h
        };
        
        // Color based on level
        Color fill_color;
        if (meter.value < 0.6f) {
            fill_color = palette.meter_green;
        } else if (meter.value < 0.85f) {
            fill_color = palette.meter_yellow;
        } else {
            fill_color = palette.meter_red;
        }
        backend.fill_rect(filled, fill_color);
        
        // Peak indicator
        if (meter.peak > 0.01f) {
            int16_t peak_y = bounds.y + bounds.h - static_cast<int16_t>(meter.peak * bounds.h);
            backend.draw_line(bounds.x, peak_y, bounds.right() - 1, peak_y, palette.foreground);
        }
    }
};

/// デフォルトラベル描画
class DefaultLabel : public ILabel {
public:
    DefaultPalette& palette;
    
    DefaultLabel(DefaultPalette& p) : palette(p) {}
    
    void draw(IBackend& backend, const ui::Label& label, Rect bounds) override {
        if (!label.text) return;
        
        Color color = label.dim ? palette.text_dim : palette.text;
        
        int16_t text_w = 0;
        for (const char* p = label.text; *p; ++p) text_w += 6;
        
        int16_t tx;
        switch (label.align) {
            case ui::Label::Align::Left:   tx = bounds.x; break;
            case ui::Label::Align::Center: tx = bounds.x + (bounds.w - text_w) / 2; break;
            case ui::Label::Align::Right:  tx = bounds.x + bounds.w - text_w; break;
        }
        
        int16_t ty = bounds.y + (bounds.h - 8) / 2;
        backend.draw_text(tx, ty, label.text, color);
    }
};

/// デフォルトスキン
class DefaultSkin : public ISkin {
public:
    DefaultSkin() 
        : rotary_(palette_)
        , linear_(palette_)
        , toggle_(palette_)
        , selector_(palette_)
        , meter_(palette_)
        , label_(palette_) {}
    
    IRotary& rotary() override { return rotary_; }
    ILinear& linear() override { return linear_; }
    IToggle& toggle() override { return toggle_; }
    ISelector& selector() override { return selector_; }
    IMeter& meter() override { return meter_; }
    ILabel& label() override { return label_; }
    
    Color background() const override { return palette_.background; }
    Color foreground() const override { return palette_.foreground; }
    Color accent() const override { return palette_.accent; }
    
    // パレットへのアクセス (カスタマイズ用)
    DefaultPalette& palette() { return palette_; }
    
private:
    DefaultPalette palette_;
    DefaultRotary rotary_;
    DefaultLinear linear_;
    DefaultToggle toggle_;
    DefaultSelector selector_;
    DefaultMeter meter_;
    DefaultLabel label_;
};

} // namespace umi::gui::skin
