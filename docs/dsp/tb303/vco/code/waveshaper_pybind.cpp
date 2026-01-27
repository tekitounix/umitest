// =============================================================================
// TB-303 WaveShaper Python Bindings (pybind11)
//
// C++実装をPythonから直接使用可能にするバインディング
// ビルド: clang++ -std=c++17 -O3 -shared -fPIC -undefined dynamic_lookup \
//         $(python3 -m pybind11 --includes) waveshaper_pybind.cpp \
//         -o tb303_waveshaper$(python3-config --extension-suffix)
//
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
    auto buf = input.request();
    if (buf.ndim != 1) {
        throw std::runtime_error("Input must be a 1-dimensional array");
    }

    size_t n = buf.size;
    float* in_ptr = static_cast<float*>(buf.ptr);

    auto result = py::array_t<float>(n);
    auto result_buf = result.request();
    float* out_ptr = static_cast<float*>(result_buf.ptr);

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

        Classes:
            WaveShaperReference: High-precision reference (100 iterations, std::exp)
            WaveShaperNewton1/2/3: Newton solver with N iterations
            WaveShaperSchur1/2: Schur complement solver with N iterations

        Example:
            >>> import tb303_waveshaper as ws
            >>> shaper = ws.WaveShaperSchur2()
            >>> shaper.set_sample_rate(48000.0)
            >>> shaper.reset()
            >>> output = shaper.process(9.0)
    )pbdoc";

    // =========================================================================
    // 回路定数エクスポート (TB-303 回路図準拠)
    // =========================================================================
    m.attr("V_T") = tb303::fast::V_T;
    m.attr("I_S") = tb303::fast::I_S;
    m.attr("BETA_F") = tb303::fast::BETA_F;
    m.attr("ALPHA_F") = tb303::fast::ALPHA_F;
    m.attr("BETA_R") = tb303::fast::BETA_R;
    m.attr("ALPHA_R") = tb303::fast::ALPHA_R;
    m.attr("V_CC") = tb303::fast::V_CC;
    m.attr("V_BIAS") = tb303::fast::V_BIAS;  // コレクタバイアス電圧 (5.33V)
    m.attr("R34") = tb303::fast::R34;    // 10kΩ (Input)
    m.attr("R35") = tb303::fast::R35;    // 100kΩ (Input)
    m.attr("R36") = tb303::fast::R36;    // 10kΩ
    m.attr("R45") = tb303::fast::R45;    // 22kΩ
    m.attr("C10") = tb303::fast::C10;    // 0.01μF
    m.attr("C11") = tb303::fast::C11;    // 1μF

    // =========================================================================
    // WaveShaperReference (高精度リファレンス: 100回反復, std::exp)
    // =========================================================================
    py::class_<tb303::fast::WaveShaperReference>(m, "WaveShaperReference",
        "High-precision reference solver (100 iterations, std::exp)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaperReference::setSampleRate,
             py::arg("sample_rate"),
             "Set the sample rate in Hz")
        .def("reset", &tb303::fast::WaveShaperReference::reset,
             "Reset internal state to initial values")
        .def("process", &tb303::fast::WaveShaperReference::process,
             py::arg("v_in"),
             "Process a single sample, returns output voltage")
        .def("process_array",
             [](tb303::fast::WaveShaperReference& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"),
             "Process an array of samples, returns output array");

    // =========================================================================
    // WaveShaperNewton<1> (1回反復)
    // =========================================================================
    py::class_<tb303::fast::WaveShaperNewton<1>>(m, "WaveShaperNewton1",
        "Newton solver with 1 iteration")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::fast::WaveShaperNewton<1>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::fast::WaveShaperNewton<1>::reset)
        .def("process", &tb303::fast::WaveShaperNewton<1>::process,
             py::arg("v_in"))
        .def("process_array",
             [](tb303::fast::WaveShaperNewton<1>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaperNewton<2> (2回反復)
    // =========================================================================
    py::class_<tb303::fast::WaveShaperNewton<2>>(m, "WaveShaperNewton2",
        "Newton solver with 2 iterations")
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
        "Newton solver with 3 iterations")
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
        "Schur complement solver with 1 iteration")
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
    // WaveShaperSchurUltra (B-C diode 1-sample delay, 1 iteration)
    // =========================================================================
    py::class_<tb303::fast::WaveShaperSchurUltra>(m, "WaveShaperSchurUltra",
        "Schur solver with B-C diode 1-sample delay (exp once per sample)")
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
    // WaveShaperLUT (LUTベース, j22ピボット, 2回反復)
    // =========================================================================
    py::class_<tb303::lut::WaveShaperLUT>(m, "WaveShaperLUT",
        "LUT-based solver with j22 pivot (2 iterations)")
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
    // WaveShaperPade (パデ[2,2]近似, j22ピボット, 2回反復)
    // =========================================================================
    py::class_<tb303::lut::WaveShaperPade>(m, "WaveShaperPade",
        "Pade[2,2] approximation solver with j22 pivot (2 iterations)")
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

    // =========================================================================
    // WaveShaperPade33 (パデ[3,3]高精度, j22ピボット, 2回反復)
    // =========================================================================
    py::class_<tb303::lut::WaveShaperPade33>(m, "WaveShaperPade33",
        "Pade[3,3] high-precision solver with j22 pivot (2 iterations)")
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
}
