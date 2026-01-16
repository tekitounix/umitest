// SPDX-License-Identifier: MIT
// UMI-OS - Skin Base: Linear
//
// 直線系コントロール (スライダー、フェーダー) の描画インターフェース

#pragma once

#include "gui/backend.hh"
#include "ui/controls.hh"

namespace umi::gui::skin {

/// 直線系コントロール描画の基底
class ILinear {
public:
    virtual ~ILinear() = default;
    
    /// スライダーを描画
    virtual void draw(IBackend& backend, const ui::Slider& slider, Rect bounds) = 0;
    
    /// ヒットテスト
    virtual bool hit_test(const Rect& bounds, int16_t x, int16_t y) {
        return bounds.contains(x, y);
    }
};

} // namespace umi::gui::skin
