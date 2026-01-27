// =============================================================================
// TB-303 WaveShaper Python Bindings (pybind11)
//
// C++実装をPythonから直接使用可能にするバインディング
// ビルド: xmake build tb303_waveshaper_py
// 使用法: import tb303_waveshaper as ws
//
// Author: Claude (Anthropic)
// License: MIT
// =============================================================================

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "tb303_waveshaper_fast.hpp"

namespace py = pybind11;

// =============================================================================
// NumPy配列処理用ヘルパー
// =============================================================================
template <typename WaveShaperT>
py::array_t<float> process_array(WaveShaperT& ws, py::array_t<float> input) {
    // 入力配列の情報を取得
    auto buf = input.request();
    if (buf.ndim != 1) {
        throw std::runtime_error("Input must be a 1-dimensional array");
    }

    size_t n = buf.size;
    float* in_ptr = static_cast<float*>(buf.ptr);

    // 出力配列を作成
    auto result = py::array_t<float>(n);
    auto result_buf = result.request();
    float* out_ptr = static_cast<float*>(result_buf.ptr);

    // 処理
    for (size_t i = 0; i < n; ++i) {
        out_ptr[i] = ws.process(in_ptr[i]);
    }

    return result;
}

// =============================================================================
// Python Module Definition
// =============================================================================
PYBIND11_MODULE(tb303_waveshaper, m) {
    m.doc() = R"pbdoc(
        TB-303 WaveShaper DSP Module
        ----------------------------

        C++ implementation of the TB-303 wave shaper circuit.
        Multiple solver variants with different accuracy/performance trade-offs.

        Classes:
            WaveShaperNewton<N>: Newton iteration solver (N = 1, 2, 3)
            WaveShaperSchur<N>: Schur complement solver (N = 1, 2)
            WaveShaperSchurUltra: Ultra-optimized Schur solver
            WaveShaper3Var: 3-variable Newton solver

        Example:
            >>> import tb303_waveshaper as ws
            >>> shaper = ws.WaveShaperSchur2()
            >>> shaper.set_sample_rate(48000.0)
            >>> shaper.reset()
            >>> output = shaper.process(9.0)
    )pbdoc";

    // =========================================================================
    // 2SA733P パラメータ定数をエクスポート
    // =========================================================================
    m.attr("V_T") = tb303::fast::V_T;
    m.attr("I_S") = tb303::fast::I_S;
    m.attr("BETA_F") = tb303::fast::BETA_F;
    m.attr("ALPHA_F") = tb303::fast::ALPHA_F;
    m.attr("BETA_R") = tb303::fast::BETA_R;
    m.attr("ALPHA_R") = tb303::fast::ALPHA_R;

    // 回路定数
    m.attr("V_CC") = tb303::fast::V_CC;
    m.attr("V_COLL") = tb303::fast::V_COLL;
    m.attr("R2") = tb303::fast::R2;
    m.attr("R3") = tb303::fast::R3;
    m.attr("R4") = tb303::fast::R4;
    m.attr("R5") = tb303::fast::R5;
    m.attr("C1") = tb303::fast::C1;
    m.attr("C2") = tb303::fast::C2;

    // =========================================================================
    // WaveShaperNewton<1> (1回反復)
    // =========================================================================
    py::class_<tb303::fast::WaveShaperNewton<1>>(m, "WaveShaperNewton1",
        "Newton iteration solver with 1 iteration")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaperNewton<1>::setSampleRate,
             py::arg("sample_rate"),
             "Set the sample rate in Hz")
        .def("reset", &tb303::fast::WaveShaperNewton<1>::reset,
             "Reset internal state to initial values")
        .def("process", &tb303::fast::WaveShaperNewton<1>::process,
             py::arg("v_in"),
             "Process a single sample, returns output voltage")
        .def("process_array",
             [](tb303::fast::WaveShaperNewton<1>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"),
             "Process an array of samples, returns output array");

    // =========================================================================
    // WaveShaperNewton<2> (2回反復)
    // =========================================================================
    py::class_<tb303::fast::WaveShaperNewton<2>>(m, "WaveShaperNewton2",
        "Newton iteration solver with 2 iterations")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaperNewton<2>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::fast::WaveShaperNewton<2>::reset)
        .def("process", &tb303::fast::WaveShaperNewton<2>::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::fast::WaveShaperNewton<2>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaperNewton<3> (3回反復)
    // =========================================================================
    py::class_<tb303::fast::WaveShaperNewton<3>>(m, "WaveShaperNewton3",
        "Newton iteration solver with 3 iterations")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaperNewton<3>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::fast::WaveShaperNewton<3>::reset)
        .def("process", &tb303::fast::WaveShaperNewton<3>::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::fast::WaveShaperNewton<3>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaperSchur<1> (Schur補行列法 1回反復)
    // =========================================================================
    py::class_<tb303::fast::WaveShaperSchur<1>>(m, "WaveShaperSchur1",
        "Schur complement solver with 1 iteration (fastest accurate solver)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaperSchur<1>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::fast::WaveShaperSchur<1>::reset)
        .def("process", &tb303::fast::WaveShaperSchur<1>::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::fast::WaveShaperSchur<1>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaperSchur<2> (Schur補行列法 2回反復)
    // =========================================================================
    py::class_<tb303::fast::WaveShaperSchur<2>>(m, "WaveShaperSchur2",
        "Schur complement solver with 2 iterations (recommended)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaperSchur<2>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::fast::WaveShaperSchur<2>::reset)
        .def("process", &tb303::fast::WaveShaperSchur<2>::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::fast::WaveShaperSchur<2>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaperSchurUltra (極限最適化版)
    // =========================================================================
    py::class_<tb303::fast::WaveShaperSchurUltra>(m, "WaveShaperSchurUltra",
        "Ultra-optimized Schur solver (B-C diode delayed evaluation)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaperSchurUltra::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::fast::WaveShaperSchurUltra::reset)
        .def("process", &tb303::fast::WaveShaperSchurUltra::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::fast::WaveShaperSchurUltra& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaper3Var<1> (3変数Newton法 1回反復)
    // =========================================================================
    py::class_<tb303::fast::WaveShaper3Var<1>>(m, "WaveShaper3Var1",
        "3-variable Newton solver with 1 iteration (v_cap eliminated algebraically)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaper3Var<1>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::fast::WaveShaper3Var<1>::reset)
        .def("process", &tb303::fast::WaveShaper3Var<1>::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::fast::WaveShaper3Var<1>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaper3Var<2> (3変数Newton法 2回反復)
    // =========================================================================
    py::class_<tb303::fast::WaveShaper3Var<2>>(m, "WaveShaper3Var2",
        "3-variable Newton solver with 2 iterations (v_cap eliminated algebraically)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaper3Var<2>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::fast::WaveShaper3Var<2>::reset)
        .def("process", &tb303::fast::WaveShaper3Var<2>::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::fast::WaveShaper3Var<2>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaper3Var<3> (3変数Newton法 3回反復)
    // =========================================================================
    py::class_<tb303::fast::WaveShaper3Var<3>>(m, "WaveShaper3Var3",
        "3-variable Newton solver with 3 iterations (v_cap eliminated algebraically)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaper3Var<3>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::fast::WaveShaper3Var<3>::reset)
        .def("process", &tb303::fast::WaveShaper3Var<3>::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::fast::WaveShaper3Var<3>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // 後方互換性のためのエイリアス
    // =========================================================================
    m.attr("WaveShaperOneIter") = m.attr("WaveShaperNewton1");
    m.attr("WaveShaperTwoIter") = m.attr("WaveShaperNewton2");
    m.attr("WaveShaperThreeIter") = m.attr("WaveShaperNewton3");
    m.attr("WaveShaperOptimized") = m.attr("WaveShaperSchur1");
    m.attr("WaveShaper3Var1Iter") = m.attr("WaveShaper3Var1");
    m.attr("WaveShaper3Var2Iter") = m.attr("WaveShaper3Var2");

    // =========================================================================
    // LUTベース実装
    // =========================================================================
    py::class_<tb303::lut::WaveShaperLUT>(m, "WaveShaperLUT",
        "LUT-based exponential approximation + Schur solver")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::lut::WaveShaperLUT::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::lut::WaveShaperLUT::reset)
        .def("process", &tb303::lut::WaveShaperLUT::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::lut::WaveShaperLUT& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // パデ近似実装
    // =========================================================================
    py::class_<tb303::lut::WaveShaperPade>(m, "WaveShaperPade",
        "Pade [2,2] exponential approximation + Schur solver")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::lut::WaveShaperPade::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::lut::WaveShaperPade::reset)
        .def("process", &tb303::lut::WaveShaperPade::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::lut::WaveShaperPade& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    py::class_<tb303::lut::WaveShaperPade33>(m, "WaveShaperPade33",
        "Pade [3,3] exponential approximation + Schur solver (higher accuracy)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::lut::WaveShaperPade33::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::lut::WaveShaperPade33::reset)
        .def("process", &tb303::lut::WaveShaperPade33::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::lut::WaveShaperPade33& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // ユーティリティ関数
    // =========================================================================
    m.def("fast_exp", &tb303::fast::fast_exp,
          py::arg("x"),
          "Fast exponential approximation (Schraudolph improved)");

    m.def("lut_exp", &tb303::exp_approx::lut_exp,
          py::arg("x"),
          "LUT-based exponential approximation");

    m.def("pade_exp", &tb303::exp_approx::pade_exp,
          py::arg("x"),
          "Pade [2,2] exponential approximation");

    m.def("pade33_exp", &tb303::exp_approx::pade33_exp,
          py::arg("x"),
          "Pade [3,3] exponential approximation");
}
