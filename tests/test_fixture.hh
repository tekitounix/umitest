// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Shared test declarations for umitest self-tests.
/// @author Shota Moriguchi @tekitounix
#pragma once

#include <umitest/test.hh>

namespace umitest::test {

void run_check_tests(umi::test::Suite& suite);
void run_format_tests(umi::test::Suite& suite);
void run_context_tests(umi::test::Suite& suite);
void run_suite_tests(umi::test::Suite& suite);
void run_reporter_tests(umi::test::Suite& suite);

} // namespace umitest::test
