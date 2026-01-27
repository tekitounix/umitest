#pragma once

#include <cstdint>
#include <cmath>
#include <vector>
#include "stm32g4xx_ll_cordic.h"
#include "stm32g4xx_ll_fmac.h"

namespace stm32g4 {

class MathLLWrapper {
 private:
  static constexpr float Q31_TO_FLOAT_FACTOR = 1.0f / (1u << 31);
  static constexpr float FLOAT_TO_Q31_FACTOR = static_cast<float>(1u << 31);

  static inline float q31_to_float(int32_t q) {
    return static_cast<float>(q) * Q31_TO_FLOAT_FACTOR;
  }

  static inline int32_t float_to_q31(float f) {
    return static_cast<int32_t>(f * FLOAT_TO_Q31_FACTOR);
  }

 public:
  enum class CORDICFunction {
    COSINE,
    PHASE,
    ARCTANGENT,
    HCOSINE,
    HARCTANGENT,
    NATURALLOG,
    SQUAREROOT
  };

  static void configure_cordic(CORDICFunction func) {
    static CORDICFunction current_func = CORDICFunction::COSINE;
    if (current_func != func) {
      switch (func) {
        case CORDICFunction::COSINE:
          LL_CORDIC_Config(CORDIC, LL_CORDIC_FUNCTION_COSINE, LL_CORDIC_PRECISION_3CYCLES, LL_CORDIC_SCALE_0,
                           LL_CORDIC_NBWRITE_1, LL_CORDIC_NBREAD_2, LL_CORDIC_INSIZE_32BITS, LL_CORDIC_OUTSIZE_32BITS);
          break;
        case CORDICFunction::PHASE:
          LL_CORDIC_Config(CORDIC, LL_CORDIC_FUNCTION_PHASE, LL_CORDIC_PRECISION_3CYCLES, LL_CORDIC_SCALE_0,
                           LL_CORDIC_NBWRITE_2, LL_CORDIC_NBREAD_1, LL_CORDIC_INSIZE_32BITS, LL_CORDIC_OUTSIZE_32BITS);
          break;
        case CORDICFunction::ARCTANGENT:
          LL_CORDIC_Config(CORDIC, LL_CORDIC_FUNCTION_ARCTANGENT, LL_CORDIC_PRECISION_3CYCLES, LL_CORDIC_SCALE_0,
                           LL_CORDIC_NBWRITE_1, LL_CORDIC_NBREAD_1, LL_CORDIC_INSIZE_32BITS, LL_CORDIC_OUTSIZE_32BITS);
          break;
        case CORDICFunction::HCOSINE:
          LL_CORDIC_Config(CORDIC, LL_CORDIC_FUNCTION_HCOSINE, LL_CORDIC_PRECISION_3CYCLES, LL_CORDIC_SCALE_0,
                           LL_CORDIC_NBWRITE_1, LL_CORDIC_NBREAD_2, LL_CORDIC_INSIZE_32BITS, LL_CORDIC_OUTSIZE_32BITS);
          break;
        case CORDICFunction::HARCTANGENT:
          LL_CORDIC_Config(CORDIC, LL_CORDIC_FUNCTION_HARCTANGENT, LL_CORDIC_PRECISION_3CYCLES, LL_CORDIC_SCALE_0,
                           LL_CORDIC_NBWRITE_1, LL_CORDIC_NBREAD_1, LL_CORDIC_INSIZE_32BITS, LL_CORDIC_OUTSIZE_32BITS);
          break;
        case CORDICFunction::NATURALLOG:
          LL_CORDIC_Config(CORDIC, LL_CORDIC_FUNCTION_NATURALLOG, LL_CORDIC_PRECISION_3CYCLES, LL_CORDIC_SCALE_0,
                           LL_CORDIC_NBWRITE_1, LL_CORDIC_NBREAD_1, LL_CORDIC_INSIZE_32BITS, LL_CORDIC_OUTSIZE_32BITS);
          break;
        case CORDICFunction::SQUAREROOT:
          LL_CORDIC_Config(CORDIC, LL_CORDIC_FUNCTION_SQUAREROOT, LL_CORDIC_PRECISION_3CYCLES, LL_CORDIC_SCALE_0,
                           LL_CORDIC_NBWRITE_1, LL_CORDIC_NBREAD_1, LL_CORDIC_INSIZE_32BITS, LL_CORDIC_OUTSIZE_32BITS);
          break;
      }
      current_func = func;
    }
  }

  static void init() {
    // Enable CORDIC clock
    RCC->AHB1ENR |= RCC_AHB1ENR_CORDICEN;

    // Initial CORDIC configuration
    configure_cordic(CORDICFunction::COSINE);
  }

  static float sin(float x) {
    // configure_cordic(CORDICFunction::COSINE);
    int32_t input = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    LL_CORDIC_ReadData(CORDIC);  // Discard cosine
    return q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }

  static float cos(float x) {
    // configure_cordic(CORDICFunction::COSINE);
    int32_t input = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    return q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }

  static void sin_cos(float x, float& sin_result, float& cos_result) {
    // configure_cordic(CORDICFunction::COSINE);
    int32_t input = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    cos_result = q31_to_float(LL_CORDIC_ReadData(CORDIC));
    sin_result = q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }

  static float phase(float y, float x) {
    // configure_cordic(CORDICFunction::PHASE);
    int32_t input_y = float_to_q31(y);
    int32_t input_x = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input_y);
    LL_CORDIC_WriteData(CORDIC, input_x);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    return q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }

  static float modulus(float y, float x) {
    // configure_cordic(CORDICFunction::PHASE);
    int32_t input_y = float_to_q31(y);
    int32_t input_x = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input_y);
    LL_CORDIC_WriteData(CORDIC, input_x);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    LL_CORDIC_ReadData(CORDIC);  // Discard phase
    return q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }

  static float atan(float x) {
    // configure_cordic(CORDICFunction::ARCTANGENT);
    int32_t input = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    return q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }

  static float sinh(float x) {
    // configure_cordic(CORDICFunction::HCOSINE);
    int32_t input = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    LL_CORDIC_ReadData(CORDIC);  // Discard cosh
    return q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }

  static float cosh(float x) {
    // configure_cordic(CORDICFunction::HCOSINE);
    int32_t input = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    return q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }

  static float tanh(float x) {
    // configure_cordic(CORDICFunction::HCOSINE);
    int32_t input = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    float cosh_value = q31_to_float(LL_CORDIC_ReadData(CORDIC));
    float sinh_value = q31_to_float(LL_CORDIC_ReadData(CORDIC));
    return sinh_value / cosh_value;
  }

  static float atanh(float x) {
    // configure_cordic(CORDICFunction::HARCTANGENT);
    int32_t input = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    return q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }

  static float ln(float x) {
    // configure_cordic(CORDICFunction::NATURALLOG);
    int32_t input = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    return q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }

  static float sqrt(float x) {
    // configure_cordic(CORDICFunction::SQUAREROOT);
    int32_t input = float_to_q31(x);
    LL_CORDIC_WriteData(CORDIC, input);
    while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC));
    return q31_to_float(LL_CORDIC_ReadData(CORDIC));
  }
};

}  // namespace stm32g4