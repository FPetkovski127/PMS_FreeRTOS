/*
 * ESP32 Second-Order Sections IIR Filter implementation
 *
 * (c)2019 Ivan Kostoski   (https://github.com/ikostoski/esp32-i2s-slm)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * MODIFIED for this project: the Xtensa-LX6 FPU assembly is only used on the
 * classic ESP32. Other chips (ESP32-S2 has NO hardware FPU, ESP32-C3 is RISC-V)
 * use the plain C++ implementation below, which computes exactly the same thing.
 * Include this file from ONE translation unit only (it defines functions).
 */

#ifndef SOS_IIR_FILTER_H
#define SOS_IIR_FILTER_H

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
  #define SOS_USE_ASM 1
#else
  #define SOS_USE_ASM 0
#endif

struct SOS_Coefficients {
  float b1;
  float b2;
  float a1;
  float a2;
};

struct SOS_Delay_State {
  float w0 = 0;
  float w1 = 0;
};

#if SOS_USE_ASM
extern "C" {
  int sos_filter_f32(float *input, float *output, int len, const SOS_Coefficients &coeffs, SOS_Delay_State &w);
}
__asm__ (
  //
  // ESP32 implementation of IIR Second-Order Section filter
  // Assumes a0 and b0 coefficients are one (1.0)
  //
  // float* a2 = input;
  // float* a3 = output;
  // int    a4 = len;
  // float* a5 = coeffs;
  // float* a6 = w;
  // float  a7 = gain;
  //
  ".text                    \n"
  ".align  4                \n"
  ".global sos_filter_f32   \n"
  ".type   sos_filter_f32,@function\n"
  "sos_filter_f32:          \n"
  "  entry   a1, 16         \n"
  "  lsi     f0, a5, 0      \n" // float f0 = coeffs.b1;
  "  lsi     f1, a5, 4      \n" // float f1 = coeffs.b2;
  "  lsi     f2, a5, 8      \n" // float f2 = coeffs.a1;
  "  lsi     f3, a5, 12     \n" // float f3 = coeffs.a2;
  "  lsi     f4, a6, 0      \n" // float f4 = w[0];
  "  lsi     f5, a6, 4      \n" // float f5 = w[1];
  "  loopnez a4, 1f         \n" // for (; len>0; len--) {
  "    lsip    f6, a2, 4    \n" //   float f6 = *input++;
  "    madd.s  f6, f2, f4   \n" //   f6 += f2 * f4; // coeffs.a1 * w0
  "    madd.s  f6, f3, f5   \n" //   f6 += f3 * f5; // coeffs.a2 * w1
  "    mov.s   f7, f6       \n" //   f7 = f6; // b0 assumed 1.0
  "    madd.s  f7, f0, f4   \n" //   f7 += f0 * f4; // coeffs.b1 * w0
  "    madd.s  f7, f1, f5   \n" //   f7 += f1 * f5; // coeffs.b2 * w1 -> result
  "    ssip    f7, a3, 4    \n" //   *output++ = f7;
  "    mov.s   f5, f4       \n" //   f5 = f4; // w1 = w0
  "    mov.s   f4, f6       \n" //   f4 = f6; // w0 = f6
  "  1:                     \n" // }
  "  ssi     f4, a6, 0      \n" // w[0] = f4;
  "  ssi     f5, a6, 4      \n" // w[1] = f5;
  "  movi.n   a2, 0         \n" // return 0;
  "  retw.n                 \n"
);

extern "C" {
  float sos_filter_sum_sqr_f32(float *input, float *output, int len, const SOS_Coefficients &coeffs, SOS_Delay_State &w, float gain);
}
__asm__ (
  //
  // ESP32 implementation of IIR Second-Order section filter with applied gain.
  // Assumes a0 and b0 coefficients are one (1.0)
  // Returns sum of squares of filtered samples
  //
  ".text                    \n"
  ".align  4                \n"
  ".global sos_filter_sum_sqr_f32 \n"
  ".type   sos_filter_sum_sqr_f32,@function \n"
  "sos_filter_sum_sqr_f32:  \n"
  "  entry   a1, 16         \n"
  "  lsi     f0, a5, 0      \n"  // float f0 = coeffs.b1;
  "  lsi     f1, a5, 4      \n"  // float f1 = coeffs.b2;
  "  lsi     f2, a5, 8      \n"  // float f2 = coeffs.a1;
  "  lsi     f3, a5, 12     \n"  // float f3 = coeffs.a2;
  "  lsi     f4, a6, 0      \n"  // float f4 = w[0];
  "  lsi     f5, a6, 4      \n"  // float f5 = w[1];
  "  wfr     f6, a7         \n"  // float f6 = gain;
  "  const.s f10, 0         \n"  // float sum_sqr = 0;
  "  loopnez a4, 1f         \n"  // for (; len>0; len--) {
  "    lsip    f7, a2, 4    \n"  //   float f7 = *input++;
  "    madd.s  f7, f2, f4   \n"  //   f7 += f2 * f4; // coeffs.a1 * w0
  "    madd.s  f7, f3, f5   \n"  //   f7 += f3 * f5; // coeffs.a2 * w1;
  "    mov.s   f8, f7       \n"  //   f8 = f7; // b0 assumed 1.0
  "    madd.s  f8, f0, f4   \n"  //   f8 += f0 * f4; // coeffs.b1 * w0;
  "    madd.s  f8, f1, f5   \n"  //   f8 += f1 * f5; // coeffs.b2 * w1;
  "    mul.s   f9, f8, f6   \n"  //   f9 = f8 * f6;  // f8 * gain -> result
  "    ssip    f9, a3, 4    \n"  //   *output++ = f9;
  "    mov.s   f5, f4       \n"  //   f5 = f4; // w1 = w0
  "    mov.s   f4, f7       \n"  //   f4 = f7; // w0 = f7;
  "    madd.s  f10, f9, f9  \n"  //   f10 += f9 * f9; // sum_sqr += f9 * f9;
  "  1:                     \n"  // }
  "  ssi     f4, a6, 0      \n"  // w[0] = f4;
  "  ssi     f5, a6, 4      \n"  // w[1] = f5;
  "  rfr     a2, f10        \n"  // return sum_sqr;
  "  retw.n                 \n"  //
);
#else
// Portable C++ version (same maths as the assembly above).
static inline int sos_filter_f32(float *input, float *output, int len, const SOS_Coefficients &c, SOS_Delay_State &w) {
  float w0 = w.w0, w1 = w.w1;
  for (int i = 0; i < len; i++) {
    float t = input[i] + c.a1 * w0 + c.a2 * w1;
    output[i] = t + c.b1 * w0 + c.b2 * w1;
    w1 = w0;
    w0 = t;
  }
  w.w0 = w0; w.w1 = w1;
  return 0;
}

static inline float sos_filter_sum_sqr_f32(float *input, float *output, int len, const SOS_Coefficients &c, SOS_Delay_State &w, float gain) {
  float w0 = w.w0, w1 = w.w1, sum = 0;
  for (int i = 0; i < len; i++) {
    float t = input[i] + c.a1 * w0 + c.a2 * w1;
    float y = (t + c.b1 * w0 + c.b2 * w1) * gain;
    output[i] = y;
    sum += y * y;
    w1 = w0;
    w0 = t;
  }
  w.w0 = w0; w.w1 = w1;
  return sum;
}
#endif

/**
 * Envelops the filter functions into a C++ struct
 */
struct SOS_IIR_Filter {

  const int num_sos;
  const float gain;
  SOS_Coefficients* sos = NULL;
  SOS_Delay_State* w = NULL;

  // Dynamic constructor
  SOS_IIR_Filter(size_t num_sos, const float gain, const SOS_Coefficients _sos[] = NULL): num_sos(num_sos), gain(gain) {
    if (num_sos > 0) {
      sos = new SOS_Coefficients[num_sos];
      if ((sos != NULL) && (_sos != NULL)) memcpy(sos, _sos, num_sos * sizeof(SOS_Coefficients));
      w = new SOS_Delay_State[num_sos]();
    }
  };

  // Template constructor for const filter declaration
  template <size_t Array_Size>
  SOS_IIR_Filter(const float gain, const SOS_Coefficients (&sos)[Array_Size]): SOS_IIR_Filter(Array_Size, gain, sos) {};

  /**
   * Apply defined IIR Filter to input array of floats, write filtered values to output,
   * and return sum of squares of all filtered values
   */
  inline float filter(float* input, float* output, size_t len) {
    if ((num_sos < 1) || (sos == NULL) || (w == NULL)) return 0;
    float* source = input;
    // Apply all but last Second-Order-Section
    for(int i=0; i<(num_sos-1); i++) {
      sos_filter_f32(source, output, len, sos[i], w[i]);
      source = output;
    }
    // Apply last SOS with gain and return the sum of squares of all samples
    return sos_filter_sum_sqr_f32(source, output, len, sos[num_sos-1], w[num_sos-1], gain);
  }

  ~SOS_IIR_Filter() {
    if (w != NULL) delete[] w;
    if (sos != NULL) delete[] sos;
  }

};

//
// For testing only
//
struct No_IIR_Filter {
  const int num_sos = 0;
  const float gain = 1.0;

  No_IIR_Filter() {};

  inline float filter(float* input, float* output, size_t len) {
    float sum_sqr = 0;
    float s;
    for(size_t i=0; i<len; i++) {
      s = input[i];
      sum_sqr += s * s;
    }
    if (input != output) {
      for(size_t i=0; i<len; i++) output[i] = input[i];
    }
    return sum_sqr;
  };

};

No_IIR_Filter None;

#endif // SOS_IIR_FILTER_H
