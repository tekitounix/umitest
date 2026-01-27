#pragma once
// https://pastebin.com/THe5JG5f

#include <algorithm>
#include "dsp/math.hh"
#include "dsp/filter/hpf.hh"
#include "dsp/filter/halfband.hh"
#include "dsp/clip/adaa.hh"

namespace mo::dsp {
inline namespace tb303 {
class Tb303Ladder {
  enum HpfType {
    Input,
    Feedback,
    Output,
    Compensation,
  };

 public:
  Tb303Ladder(float fs)
    : hpf{fs, fs, fs, fs}, hard_clipper(fs) {
    setSampleRate(fs);
    hpf[HpfType::Input].setCutoff(80.f).processCoef();
    hpf[HpfType::Feedback].setCutoff(92.f).processCoef();
    hpf[HpfType::Output].setCutoff(100.f).processCoef();
    hpf[HpfType::Compensation].setCutoff(240.f).processCoef();
    setCutoff(100.f);
    setResonance(0.5f);
    // processCoef();
  }

  Tb303Ladder& reset() {
    std::ranges::fill(z, 0);
    return *this;
  }

  Tb303Ladder& setSampleRate(float fs) {
    const auto fs2 = fs * 2.0f;
    dt = 1.f / (fs2);
    for (auto&& itr : hpf) { itr.setSampleRate(fs2); }
    return *this;
  }

  float normFreq(float f) { return f * dt; }

  Tb303Ladder& setFeedbackGain(float fb_gain) {
    this->fb_gain = fb_gain;
    return *this;
  }

  Tb303Ladder& setResonance(float q) {
    this->q = q;
    k = q * fb_gain;
    A = q * 2.2f;
    return *this;
  }

  Tb303Ladder& setNormalizedCutoff(float wc) {
    this->wc = mo::clamp(wc, 0.002f, 0.5f - 0.04f);
    return *this;
  }

  Tb303Ladder& setCutoff(float fc) {
    setNormalizedCutoff(fc * dt);
    return *this;
  }

  // Tb303Ladder& processCoef() {
  //   a = tan(pi<float> * wc) * inv_sqrt2<float>;
  //   div_a = 1.0f / a;
  //   aa = a * a;
  //   aaaa = aa * aa;
  //   b = 2.0f * a + 1.0f;
  //   bb = b * b;
  //   c = 1.0f / (2.0f * aaaa - 4.0f * aa * bb + bb * bb);
  //   g = 2.0f * aaaa * c;
  //   return *this;
  // }

  // float processOut(float x) {
  //   anti_alias[1].process(processOutImpl(anti_alias[0].process(x)));
  //   return anti_alias[1].process(processOutImpl(anti_alias[0].process(0)));
  // }

  float process(float x) {
    anti_alias[1].process(processImpl(anti_alias[0].process(x)));
    return anti_alias[1].process(processImpl(anti_alias[0].process(0)));
  }

 private:
  // float processOutImpl(float x) {
  //   const auto x0 = hpf[HpfType::Input].processOut(x);

  //   const auto s = (z[0] * aa * a
  //                   + z[1] * aa * b
  //                   + z[2] * (bb - 2.0f * aa) * a
  //                   + z[3] * (bb - 3.0f * aa) * b)
  //                  * c;

  //   const auto fb = hpf[HpfType::Feedback].processOut(s);

  //   auto y5 = (g * x0 + fb) / (1.0f + g * k);

  //   const auto y0 = clamp(x0 - k * y5, -1.0f, 1.0f);

  //   // y5 = g * y0 + s;

  //   const auto y4 = g * y0 + s;
  //   const auto y3 = (b * y4 - z[3]) * div_a;
  //   const auto y2 = (b * y3 - a * y4 - z[2]) * div_a;
  //   const auto y1 = (b * y2 - a * y3 - z[1]) * div_a;

  //   const auto a2 = 2.0f * a;
  //   z[0] += 2.0f * a2 * (y0 - y1 + y2);
  //   z[1] += a2 * (y1 - 2.0f * y2 + y3);
  //   z[2] += a2 * (y2 - 2.0f * y3 + y4);
  //   z[3] += a2 * (y3 - 2.0f * y4);

  //   return hpf[HpfType::Output].processOut(y4)
  //          + hpf[HpfType::Compensation].processOut(y4 * A);
  // }

  float processImpl(float x) {
    a = tan(pi<float> * wc) * inv_sqrt2<float>;
    const auto div_a = 1.0f / a;
    const auto aa = a * a;
    const auto aaaa = aa * aa;
    const auto b = 2.0f * a + 1.0f;
    const auto bb = b * b;
    const auto c = 1.0f / (2.0f * aaaa - 4.0f * aa * bb + bb * bb);
    const auto g = 2.0f * aaaa * c;

    const auto x0 = hpf[HpfType::Input].processOut(x);

    const auto s = (z[0] * aa * a
                    + z[1] * aa * b
                    + z[2] * (bb - 2.0f * aa) * a
                    + z[3] * (bb - 3.0f * aa) * b)
                   * c;

    const auto fb = hpf[HpfType::Feedback].processOut(s);

    // auto y5 = (g * x0 + fb) / (1.0f + k * g);

    // const auto y0 = clamp(x0 - k * y5, -1.0f, 1.0f);
    // const auto y0 = hard_clipper.process(x0 - k * y5);

    // y5 = g * y0 + s;

    const float y0 = clip(x0 - k * (g * x0 + fb) / (1 + k * g));

    const auto y4 = g * y0 + s;
    const auto y3 = (b * y4 - z[3]) * div_a;
    const auto y2 = (b * y3 - a * y4 - z[2]) * div_a;
    const auto y1 = (b * y2 - a * y3 - z[1]) * div_a;

    // const auto a2 = 2.0f * a;
    // z[0] += 2.0f * a2 * (y0 - y1 + y2);
    // z[1] += a2 * (y1 - 2.0f * y2 + y3);
    // z[2] += a2 * (y2 - 2.0f * y3 + y4);
    // z[3] += a2 * (y3 - 2.0f * y4);

    z[0] += 4 * a * (y0 - y1 + y2);
    z[1] += 2 * a * (y1 - 2 * y2 + y3);
    z[2] += 2 * a * (y2 - 2 * y3 + y4);
    z[3] += 2 * a * (y3 - 2 * y4);

    return hpf[HpfType::Output].processOut(y4)
           + hpf[HpfType::Compensation].processOut(y4 * A);
  }

  float dt = 0, wc = 0;
  float z[4] = {};
  float a = 0, aa = 0, aaaa = 0, div_a = 0, b = 0, bb = 0, c = 0;
  float g = 0, q = 0, k = 0, A = 0;
  // float fb_gain = 14.5f /*TB-303*/;
  float fb_gain = 16.0f;

  Hpf hpf[4];
  Halfband anti_alias[2];
  Hardclip hard_clipper;
};
}  // namespace tb303
}  // namespace mo::dsp
