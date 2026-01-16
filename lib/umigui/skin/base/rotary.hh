// SPDX-License-Identifier: MIT
// UMI-OS - Skin Base: Rotary
//
// 回転系コントロール (ノブ、エンコーダ) の描画インターフェース

#pragma once

#include "gui/backend.hh"
#include "ui/controls.hh"

namespace umi::gui::skin {

/// 回転系コントロール描画の基底
class IRotary {
public:
    virtual ~IRotary() = default;
    
    /// ノブを描画
    virtual void draw(IBackend& backend, const ui::Knob& knob, Rect bounds) = 0;
    
    /// ヒットテスト (オプション、デフォルトは矩形)
    virtual bool hit_test(const Rect& bounds, int16_t x, int16_t y) {
        return bounds.contains(x, y);
    }
};

} // namespace umi::gui::skin
