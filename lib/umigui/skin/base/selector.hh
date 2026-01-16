// SPDX-License-Identifier: MIT
// UMI-OS - Skin Base: Selector
//
// 選択系コントロール (ドロップダウン、ラジオ、セグメント) の描画インターフェース

#pragma once

#include "gui/backend.hh"
#include "ui/controls.hh"

namespace umi::gui::skin {

/// 選択系コントロール描画の基底
class ISelector {
public:
    virtual ~ISelector() = default;
    
    /// セレクタを描画
    virtual void draw(IBackend& backend, const ui::Selector& selector, Rect bounds) = 0;
    
    /// ヒットテスト
    virtual bool hit_test(const Rect& bounds, int16_t x, int16_t y) {
        return bounds.contains(x, y);
    }
};

} // namespace umi::gui::skin
