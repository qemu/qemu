/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MMVEC_QFLOAT_H
#define MMVEC_QFLOAT_H 1

#define HF_MAX 131008  //pow(2,17)-pow(2,6) =(2-1.0/pow(2,10))*pow(2,16)
#define HF_MIN 1.0/pow(2,24)
#define SF_MAX pow(2,129)-pow(2,105) //(2-1.0/pow(2,23))*pow(2,128)
#define SF_MIN 1.0/pow(2,149)

#define E_MAX_QF32 128
#define E_MIN_QF32 -127
#define E_MAX_QF16 16
#define E_MIN_QF16 -15
#define E_MAX_SF 128
#define E_MIN_SF -126
#define E_MAX_HF 16
#define E_MIN_HF -14
#define BIAS_QF32 127
#define BIAS_QF16 15
#define BIAS_DF 1023
#define BIAS_SF 127
#define BIAS_HF 15
#define FRAC_HF 10
#define FRAC_SF 23
#define isNaNF32( a ) (((~(a) & 0x7F800000) == 0) && ((a) & 0x007FFFFF))
#define isInfF32( a ) (((~(a) & 0x7F800000) == 0) && (((a) & 0x007FFFFF) == 0))
#define isNaNF16( a ) (((~(a) & 0x7C00) == 0) && ((a) & 0x03FF))
#define isInfF16( a ) (((~(a) & 0x7C00) == 0) && (((a) & 0x03FF) == 0))

//#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
//#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

#include "cpu.h"
#include "hex_arch_types.h"

#define epsilon 1.0/pow(2,23)
#define units 1.0*pow(2,23)
#define epsilon_hf 1.0/pow(2,10)
#define units_hf 1.0*pow(2,10)

typedef struct{
  int sign;
  int exp;
  double sig;
} unfloat; //Un-Normalized Float

typedef struct{
  int sign;
  int sig;
  int exp;
} qf_t;

typedef struct{
  int32_t sig : 24;
  uint32_t exp : 8;
} qf32_t;

typedef struct{
  int32_t sig : 11;
  uint32_t exp : 5;
} qf16_t;

typedef enum float_type{
  QF32,
  QF16,
  SF,
  HF
} f_type;

typedef union {
	float f;
	size4u_t i;
	struct {
		size4u_t mant:23;
		size4u_t exp:8;
		size4u_t sign:1;
	} x;
} sf_union;

//MPY
size4s_t mpy_qf32(size4s_t a, size4s_t b);
size4s_t mpy_qf32_sf(size4s_t a, size4s_t b);
size4s_t mpy_qf32_mix_sf(size4s_t a, size4s_t b);
size2s_t mpy_qf16(size2s_t a, size2s_t b);
size2s_t mpy_qf16_hf(size2s_t a, size2s_t b);
size2s_t mpy_qf16_mix_hf(size2s_t a, size2s_t b);
size8s_t mpy_qf32_qf16(size4s_t a, size4s_t b);
size8s_t mpy_qf32_hf(size4s_t a, size4s_t b);
size8s_t mpy_qf32_mix_hf(size4s_t a, size4s_t b);

unfloat parse_qf32(size4s_t a);
unfloat parse_qf16(size2s_t a);
unfloat parse_sf(size4s_t a);
unfloat parse_hf(size2s_t a);
size4s_t rnd_sat_qf32(int exp, double sig, double sig_low);
size2s_t rnd_sat_qf16(int exp, double sig, double sig_low);
size4s_t rnd_sat_sf(int exp, double sig);
size2s_t rnd_sat_hf(int exp, double sig);
size4s_t rnd_sat_w(int exp, double sig);
size4u_t rnd_sat_uw(int exp, double sig);
size2s_t rnd_sat_h(int exp, double sig);
size2u_t rnd_sat_uh(int exp, double sig);
size1s_t rnd_sat_b(int exp, double sig);
size1u_t rnd_sat_ub(int exp, double sig);
size4s_t negate32(size4s_t);
size2s_t negate16(size2s_t);
size4s_t negate_sf(size4s_t);
size2s_t negate_hf(size2s_t);

//ADD
size4s_t add_qf32(size4s_t a, size4s_t b);
size4s_t add_sf(size4s_t a, size4s_t b);
size4s_t add_qf32_mix(size4s_t a, size4s_t b);
size2s_t add_qf16(size2s_t a, size2s_t b);
size2s_t add_hf(size2s_t a, size2s_t b);
size2s_t add_qf16_mix(size2s_t a, size2s_t b);

//SUB
size4s_t sub_qf32(size4s_t a, size4s_t b);
size4s_t sub_sf(size4s_t a, size4s_t b);
size4s_t sub_qf32_mix(size4s_t a, size4s_t b);
size2s_t sub_qf16(size2s_t a, size2s_t b);
size2s_t sub_hf(size2s_t a, size2s_t b);
size2s_t sub_qf16_mix(size2s_t a, size2s_t b);

//Convert
size4s_t conv_sf_qf32(size4s_t a);
size4s_t conv_sf_w(size4s_t a);
size4s_t conv_sf_uw(size4u_t a);
size2s_t conv_hf_qf16(size2s_t a);
size2s_t conv_hf_h(size2s_t a);
size2s_t conv_hf_uh(size2u_t a);
size4s_t conv_hf_qf32(size8s_t a);
size4s_t conv_hf_w(size8s_t a);
size4s_t conv_hf_uw(size8u_t a);

size4s_t conv_w_qf32(size4s_t a);
size4u_t conv_uw_qf32(size4s_t a);
size2s_t conv_h_qf16(size2s_t a);
size2u_t conv_uh_qf16(size2s_t a);
size4s_t conv_h_qf32(size8s_t a);
size4u_t conv_uh_qf32(size8s_t a);
size2s_t conv_b_qf16(size4s_t a);
size2u_t conv_ub_qf16(size4s_t a);

size4s_t conv_w_sf(size4s_t a);
// size4u_t conv_uw_sf(size4s_t a);
size2s_t conv_h_hf(size2s_t a);
// size2u_t conv_uh_sf(size2s_t a);

//Neg/Abs
size4s_t neg_qf32(size4s_t a);
size4s_t abs_qf32(size4s_t a);
size2s_t neg_qf16(size2s_t a);
size2s_t abs_qf16(size2s_t a);
size4s_t neg_sf(size4s_t a);
size4s_t abs_sf(size4s_t a);
size2s_t neg_hf(size2s_t a);
size2s_t abs_hf(size2s_t a);

//Compare
int cmpgt_fp(unfloat a,  unfloat b);
int cmpgt_qf32(size4s_t a,  size4s_t b);
int cmpgt_qf16(size2s_t a,  size2s_t b);
int cmpgt_sf(size4s_t a,  size4s_t b);
int cmpgt_hf(size2s_t a,  size2s_t b);
int cmpgt_qf32_sf(size4s_t a,  size4s_t b);
int cmpgt_qf16_hf(size2s_t a,  size2s_t b);

//max/min
size4s_t max_qf32(size4s_t a, size4s_t b);
size4s_t min_qf32(size4s_t a, size4s_t b);
size4s_t max_qf32_sf(size4s_t a, size4s_t b);
size4s_t min_qf32_sf(size4s_t a, size4s_t b);
size4s_t max_sf(size4s_t a, size4s_t b);
size4s_t min_sf(size4s_t a, size4s_t b);
size2s_t max_qf16(size2s_t a, size2s_t b);
size2s_t min_qf16(size2s_t a, size2s_t b);
size2s_t max_qf16_hf(size2s_t a, size2s_t b);
size2s_t min_qf16_hf(size2s_t a, size2s_t b);
size2s_t max_hf(size2s_t a, size2s_t b);
size2s_t min_hf(size2s_t a, size2s_t b);
#endif
