// SPDX-License-Identifier: MIT
// STM32H7 HAL host unit tests
// Tests constexpr clock calculation helpers and GPIO utility functions

#include <umitest.hh>

using namespace umitest;

#include <mcu/rcc.hh>

using namespace umi::stm32h7;

static void test_pll_ref_clock(Suite& s) {
    s.section("PLL ref clock");
    {
        PllConfig cfg{.src_hz = 16'000'000, .m = 8, .n = 240, .p = 1};
        s.check_eq(static_cast<int>(pll_ref_hz(cfg)), 2'000'000);
    }
}

static void test_pll_vco(Suite& s) {
    s.section("PLL VCO 480MHz");
    {
        PllConfig cfg{.src_hz = 16'000'000, .m = 8, .n = 240, .p = 1};
        s.check_eq(static_cast<int>(pll_vco_hz(cfg)), 480'000'000);
    }
}

static void test_pll_p_output(Suite& s) {
    s.section("PLL P output");
    {
        PllConfig cfg{.src_hz = 16'000'000, .m = 8, .n = 240, .p = 1};
        s.check_eq(static_cast<int>(pll_p_hz(cfg)), 480'000'000);
    }
}

static void test_pll_p_divided(Suite& s) {
    s.section("PLL P divided");
    {
        PllConfig cfg{.src_hz = 16'000'000, .m = 8, .n = 240, .p = 2};
        s.check_eq(static_cast<int>(pll_p_hz(cfg)), 240'000'000);
    }
}

static void test_pll2_sai(Suite& s) {
    s.section("PLL2 SAI clock");
    {
        PllConfig cfg{.src_hz = 16'000'000, .m = 4, .n = 100, .p = 2};
        s.check_eq(static_cast<int>(pll_ref_hz(cfg)), 4'000'000);
        s.check_eq(static_cast<int>(pll_vco_hz(cfg)), 400'000'000);
        s.check_eq(static_cast<int>(pll_p_hz(cfg)), 200'000'000);
    }
}

static void test_pll_constexpr(Suite& s) {
    s.section("PLL constexpr");
    {
        constexpr PllConfig cfg{.src_hz = 16'000'000, .m = 8, .n = 240, .p = 1};
        static_assert(pll_vco_hz(cfg) == 480'000'000);
        static_assert(pll_p_hz(cfg) == 480'000'000);
        static_assert(pll_ref_hz(cfg) == 2'000'000);
        s.check(true, "static_assert passed");
    }
}

static void test_flash_ws_vos0(Suite& s) {
    s.section("Flash wait states VOS0 boost");
    {
        s.check_eq(static_cast<int>(flash_wait_states(240'000'000, true)), 4);
        s.check_eq(static_cast<int>(flash_wait_states(210'000'000, true)), 3);
        s.check_eq(static_cast<int>(flash_wait_states(185'000'000, true)), 2);
        s.check_eq(static_cast<int>(flash_wait_states(140'000'000, true)), 1);
        s.check_eq(static_cast<int>(flash_wait_states(70'000'000, true)), 0);
    }
}

static void test_flash_ws_vos1(Suite& s) {
    s.section("Flash wait states VOS1");
    {
        s.check_eq(static_cast<int>(flash_wait_states(200'000'000, false)), 2);
        s.check_eq(static_cast<int>(flash_wait_states(140'000'000, false)), 1);
        s.check_eq(static_cast<int>(flash_wait_states(70'000'000, false)), 0);
    }
}

static void test_flash_constexpr(Suite& s) {
    s.section("Flash constexpr");
    {
        static_assert(flash_wait_states(240'000'000, true) == 4);
        static_assert(flash_wait_states(70'000'000, false) == 0);
        s.check(true, "static_assert passed");
    }
}

static void test_gpio_2bit_mask(Suite& s) {
    s.section("GPIO 2-bit mask");
    {
        s.check_eq(static_cast<int>(gpio_2bit_mask(0)), 0x3);
        s.check_eq(static_cast<int>(gpio_2bit_mask(7)), 0x3 << 14);
        s.check_eq(static_cast<int>(gpio_2bit_mask(15)), static_cast<int>(0x3u << 30));
    }
}

static void test_gpio_1bit_mask(Suite& s) {
    s.section("GPIO 1-bit mask");
    {
        s.check_eq(static_cast<int>(gpio_1bit_mask(0)), 1);
        s.check_eq(static_cast<int>(gpio_1bit_mask(7)), 1 << 7);
    }
}

static void test_gpio_af_reg_index(Suite& s) {
    s.section("GPIO AF register index");
    {
        s.check_eq(static_cast<int>(gpio_af_reg_index(0)), 0);
        s.check_eq(static_cast<int>(gpio_af_reg_index(7)), 0);
        s.check_eq(static_cast<int>(gpio_af_reg_index(8)), 1);
        s.check_eq(static_cast<int>(gpio_af_reg_index(15)), 1);
    }
}

static void test_gpio_af_shift(Suite& s) {
    s.section("GPIO AF shift");
    {
        s.check_eq(static_cast<int>(gpio_af_shift(0)), 0);
        s.check_eq(static_cast<int>(gpio_af_shift(5)), 20);
        s.check_eq(static_cast<int>(gpio_af_shift(12)), 16);
    }
}

static void test_gpio_constexpr(Suite& s) {
    s.section("GPIO constexpr");
    {
        static_assert(gpio_2bit_mask(0) == 0x3);
        static_assert(gpio_1bit_mask(7) == 128);
        static_assert(gpio_af_reg_index(15) == 1);
        static_assert(gpio_af_shift(3) == 12);
        s.check(true, "static_assert passed");
    }
}

int main() {
    Suite s("port_hal_h7");

    test_pll_ref_clock(s);
    test_pll_vco(s);
    test_pll_p_output(s);
    test_pll_p_divided(s);
    test_pll2_sai(s);
    test_pll_constexpr(s);
    test_flash_ws_vos0(s);
    test_flash_ws_vos1(s);
    test_flash_constexpr(s);
    test_gpio_2bit_mask(s);
    test_gpio_1bit_mask(s);
    test_gpio_af_reg_index(s);
    test_gpio_af_shift(s);
    test_gpio_constexpr(s);

    return s.summary();
}
