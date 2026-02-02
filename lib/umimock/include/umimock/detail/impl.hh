// SPDX-License-Identifier: MIT
// umimock internal implementation details

#pragma once

namespace umi::mock::detail {

/// Clamp value to [0, 1] range.
constexpr float clamp01(float v) {
    if (v < 0.0f)
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    return v;
}

} // namespace umi::mock::detail
