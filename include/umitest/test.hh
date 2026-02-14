// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Umbrella header — includes all umitest components.
/// @author Shota Moriguchi @tekitounix
///
/// Provides Suite (test runner), TestContext (assertion context),
/// and format_value (snprintf-based value formatter) without macros,
/// exceptions, or RTTI.
///
/// Individual headers:
/// - `umitest/format.hh`  — format_value and ANSI color constants
/// - `umitest/context.hh` — TestContext class declaration
/// - `umitest/suite.hh`   — Suite class and TestContext implementation
#pragma once

#include <umitest/suite.hh>
