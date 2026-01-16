// SPDX-License-Identifier: MIT
// UMI-OS - GUI Library
//
// 統合ヘッダ

#pragma once

// Backend
#include "backend.hh"
#include "layout.hh"

// Backend implementations
#include "framebuffer_backend.hh"
#ifdef __EMSCRIPTEN__
#include "canvas2d_backend.hh"
#endif

// Skin system
#include "skin/skin.hh"
#include "skin/default/default_skin.hh"
