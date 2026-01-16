// SPDX-License-Identifier: MIT
// UMI-OS - Skin Base: Meter
//
// 表示系コントロール (レベルメーター、波形) の描画インターフェース

#pragma once

#include "gui/backend.hh"
#include "ui/controls.hh"

namespace umi::gui::skin {

/// 表示系コントロール描画の基底
class IMeter {
public:
    virtual ~IMeter() = default;
    
    /// メーターを描画
    virtual void draw(IBackend& backend, const ui::Meter& meter, Rect bounds) = 0;
};

/// ラベル描画の基底
class ILabel {
public:
    virtual ~ILabel() = default;
    
    /// ラベルを描画
    virtual void draw(IBackend& backend, const ui::Label& label, Rect bounds) = 0;
};

} // namespace umi::gui::skin
