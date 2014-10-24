// Copyright 2013, ARM Limited
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of ARM Limited nor the names of its contributors may be
//     used to endorse or promote products derived from this software without
//     specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "utils.h"
#include <stdio.h>

namespace vixl {

uint32_t float_to_rawbits(float value) {
  uint32_t bits = 0;
  memcpy(&bits, &value, 4);
  return bits;
}


uint64_t double_to_rawbits(double value) {
  uint64_t bits = 0;
  memcpy(&bits, &value, 8);
  return bits;
}


float rawbits_to_float(uint32_t bits) {
  float value = 0.0;
  memcpy(&value, &bits, 4);
  return value;
}


double rawbits_to_double(uint64_t bits) {
  double value = 0.0;
  memcpy(&value, &bits, 8);
  return value;
}


int CountLeadingZeros(uint64_t value, int width) {
  VIXL_ASSERT((width == 32) || (width == 64));
  int count = 0;
  uint64_t bit_test = UINT64_C(1) << (width - 1);
  while ((count < width) && ((bit_test & value) == 0)) {
    count++;
    bit_test >>= 1;
  }
  return count;
}


int CountLeadingSignBits(int64_t value, int width) {
  VIXL_ASSERT((width == 32) || (width == 64));
  if (value >= 0) {
    return CountLeadingZeros(value, width) - 1;
  } else {
    return CountLeadingZeros(~value, width) - 1;
  }
}


int CountTrailingZeros(uint64_t value, int width) {
  VIXL_ASSERT((width == 32) || (width == 64));
  int count = 0;
  while ((count < width) && (((value >> count) & 1) == 0)) {
    count++;
  }
  return count;
}


int CountSetBits(uint64_t value, int width) {
  // TODO: Other widths could be added here, as the implementation already
  // supports them.
  VIXL_ASSERT((width == 32) || (width == 64));

  // Mask out unused bits to ensure that they are not counted.
  value &= (UINT64_C(0xffffffffffffffff) >> (64-width));

  // Add up the set bits.
  // The algorithm works by adding pairs of bit fields together iteratively,
  // where the size of each bit field doubles each time.
  // An example for an 8-bit value:
  // Bits:  h  g  f  e  d  c  b  a
  //         \ |   \ |   \ |   \ |
  // value = h+g   f+e   d+c   b+a
  //            \    |      \    |
  // value =   h+g+f+e     d+c+b+a
  //                  \          |
  // value =       h+g+f+e+d+c+b+a
  const uint64_t kMasks[] = {
    UINT64_C(0x5555555555555555),
    UINT64_C(0x3333333333333333),
    UINT64_C(0x0f0f0f0f0f0f0f0f),
    UINT64_C(0x00ff00ff00ff00ff),
    UINT64_C(0x0000ffff0000ffff),
    UINT64_C(0x00000000ffffffff),
  };

  for (unsigned i = 0; i < (sizeof(kMasks) / sizeof(kMasks[0])); i++) {
    int shift = 1 << i;
    value = ((value >> shift) & kMasks[i]) + (value & kMasks[i]);
  }

  return value;
}


uint64_t LowestSetBit(uint64_t value) {
  return value & -value;
}


bool IsPowerOf2(int64_t value) {
  return (value != 0) && ((value & (value - 1)) == 0);
}

}  // namespace vixl
