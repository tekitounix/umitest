// SPDX-License-Identifier: MIT
// UMI-OS - UI Controls
//
// UI状態のみを管理するコントロール群。
// 描画ロジックは含まない（それはGUI/Skinの責務）。
// ハードウェアUI（エンコーダ、物理ボタン等）でも使用可能。

#pragma once

#include <cstdint>

namespace umi::ui {

// ============================================================================
// Base Control
// ============================================================================

/// 全コントロール共通の基底
struct Control {
    const char* name = nullptr;     // パラメータ名
    const char* unit = nullptr;     // 単位 (Hz, dB, %, etc.)
    
    float value = 0.0f;             // 正規化値 0-1
    float default_value = 0.0f;     // デフォルト値
    
    bool pressed = false;           // 押下状態
    bool focused = false;           // フォーカス状態
    
    void set_value(float v) { 
        value = (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v; 
    }
    
    void reset() { value = default_value; }
};

// ============================================================================
// Continuous Controls (連続値)
// ============================================================================

/// ノブ/ロータリーエンコーダ
struct Knob : Control {
    float min = 0.0f;               // 表示最小値
    float max = 1.0f;               // 表示最大値
    float step = 0.0f;              // ステップ (0 = 連続)
    
    /// 表示用の値を取得
    float display_value() const { 
        return min + value * (max - min); 
    }
    
    /// デルタから値を更新
    float apply_delta(float delta, float sensitivity = 0.005f) {
        set_value(value + delta * sensitivity);
        return value;
    }
};

/// スライダー/フェーダー
struct Slider : Control {
    float min = 0.0f;
    float max = 1.0f;
    bool vertical = true;           // 垂直/水平
    
    float display_value() const { 
        return min + value * (max - min); 
    }
    
    float apply_delta(float delta, float sensitivity = 0.01f) {
        set_value(value + delta * sensitivity);
        return value;
    }
    
    /// 位置から直接値を設定 (0-1)
    void set_from_position(float pos) {
        set_value(vertical ? (1.0f - pos) : pos);
    }
};

// ============================================================================
// Discrete Controls (離散値)
// ============================================================================

/// ボタン (モーメンタリ/トグル)
struct Button : Control {
    enum class Mode : uint8_t {
        Momentary,  // 押している間だけON
        Toggle,     // 押すたびにON/OFF切替
    };
    
    Mode mode = Mode::Momentary;
    
    bool is_on() const { return value > 0.5f; }
    void set_on(bool on) { value = on ? 1.0f : 0.0f; }
    
    void on_press() {
        pressed = true;
        if (mode == Mode::Toggle) {
            set_on(!is_on());
        }
    }
    
    void on_release() {
        pressed = false;
    }
};

/// セレクタ (複数選択肢)
struct Selector : Control {
    const char* const* options = nullptr;  // 選択肢の配列
    uint8_t option_count = 0;
    
    uint8_t selected() const { 
        return static_cast<uint8_t>(value * (option_count - 1) + 0.5f); 
    }
    
    void select(uint8_t index) {
        if (option_count > 1) {
            value = static_cast<float>(index) / (option_count - 1);
        }
    }
    
    void next() {
        uint8_t idx = selected();
        if (idx < option_count - 1) select(idx + 1);
    }
    
    void prev() {
        uint8_t idx = selected();
        if (idx > 0) select(idx - 1);
    }
    
    const char* selected_option() const {
        return options ? options[selected()] : nullptr;
    }
};

// ============================================================================
// Display Controls (表示専用)
// ============================================================================

/// レベルメーター
struct Meter : Control {
    float peak = 0.0f;              // ピーク値
    
    void update(float new_value, float decay = 0.95f) {
        set_value(new_value);
        if (new_value > peak) {
            peak = new_value;
        } else {
            peak *= decay;
        }
    }
    
    void reset_peak() { peak = 0.0f; }
};

/// テキストラベル
struct Label : Control {
    const char* text = nullptr;
    
    enum class Align : uint8_t { Left, Center, Right };
    Align align = Align::Left;
    bool dim = false;               // 薄い色で表示
};

// ============================================================================
// XY Pad
// ============================================================================

/// XYパッド (2軸コントロール)
struct XYPad : Control {
    float x = 0.5f;
    float y = 0.5f;
    
    void set_xy(float new_x, float new_y) {
        x = (new_x < 0.0f) ? 0.0f : (new_x > 1.0f) ? 1.0f : new_x;
        y = (new_y < 0.0f) ? 0.0f : (new_y > 1.0f) ? 1.0f : new_y;
    }
};

} // namespace umi::ui
