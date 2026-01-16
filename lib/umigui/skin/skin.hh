// SPDX-License-Identifier: MIT
// UMI-OS - Skin Interface
//
// スキン = 描画ロジックの集合
// 各形状ベースクラスを束ねて統一されたルックを提供

#pragma once

#include "base/rotary.hh"
#include "base/linear.hh"
#include "base/toggle.hh"
#include "base/selector.hh"
#include "base/meter.hh"

namespace umi::gui::skin {

/// スキンインターフェース
/// 全形状の描画を統一したルックで提供
class ISkin {
public:
    virtual ~ISkin() = default;
    
    // 各形状のレンダラーを取得
    virtual IRotary& rotary() = 0;
    virtual ILinear& linear() = 0;
    virtual IToggle& toggle() = 0;
    virtual ISelector& selector() = 0;
    virtual IMeter& meter() = 0;
    virtual ILabel& label() = 0;
    
    // 背景色
    virtual Color background() const = 0;
    virtual Color foreground() const = 0;
    virtual Color accent() const = 0;
};

} // namespace umi::gui::skin
