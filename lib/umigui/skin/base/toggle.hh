// SPDX-License-Identifier: MIT
// UMI-OS - Skin Base: Toggle
//
// ON/OFF系コントロール (ボタン、スイッチ) の描画インターフェース

#pragma once

#include "gui/backend.hh"
#include "ui/controls.hh"

namespace umi::gui::skin {

/// ON/OFF系コントロール描画の基底
class IToggle {
public:
    virtual ~IToggle() = default;
    
    /// ボタンを描画
    virtual void draw(IBackend& backend, const ui::Button& button, Rect bounds) = 0;
    
    /// ヒットテスト
    virtual bool hit_test(const Rect& bounds, int16_t x, int16_t y) {
        return bounds.contains(x, y);
    }
};

} // namespace umi::gui::skin
