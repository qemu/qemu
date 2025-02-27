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

#ifndef KVX_COMPACT_H
#define KVX_COMPACT_H 1

#include <math.h>
#include "hex_arch_types.h"

//Double precision
#define signF64UI( a ) ((bool) ((uint64_t) (a)>>63))
#define expF64UI( a ) ((int_fast16_t) ((a)>>52) & 0x7FF)
#define fracF64UI( a ) ((a) & UINT64_C( 0x000FFFFFFFFFFFFF ))
#define packToF64UI( sign, exp, sig ) ((uint64_t) (((uint_fast64_t) (sign)<<63) + ((uint_fast64_t) (exp)<<52) + (sig)))
#define isNaNF64UI( a ) (((~(a) & UINT64_C( 0x7FF0000000000000 )) == 0) && ((a) & UINT64_C( 0x000FFFFFFFFFFFFF )))

//SF defines
#define FP32_DEF_NAN      0x7FFFFFFF
#define isNaNF32UI( a ) (((~(a) & 0x7F800000) == 0) && ((a) & 0x007FFFFF))
#define isInfF32UI( a ) (((~(a) & 0x7F800000) == 0) && (((a) & 0x007FFFFF) == 0))
#define signF32UI( a ) ((bool) ((uint32_t) (a)>>31))
#define expF32UI( a ) ((int_fast16_t) ((a)>>23) & 0xFF)
#define fracF32UI( a ) ((a) & 0x007FFFFF)
#define packToF32UI( sign, exp, sig ) (((uint32_t) (sign)<<31) + ((uint32_t) (exp)<<23) + (sig))

//HF defines
#define FP16_DEF_NAN      0x7FFF
#define isNaNF16UI( a ) (((~(a) & 0x7C00) == 0) && ((a) & 0x03FF))
#define isInfF16UI( a ) (((~(a) & 0x7C00) == 0) && (((a) & 0x03FF) == 0))
#define signF16UI( a ) ((bool) ((uint16_t) (a)>>15))
#define expF16UI( a ) ((int_fast8_t) ((a)>>10) & 0x1F)
#define fracF16UI( a ) ((a) & 0x03FF)
#define packToF16UI( sign, exp, sig ) (((uint16_t) (sign)<<15) + ((uint16_t) (exp)<<10) + (sig))

#define UHW_MIN           0
#define UHW_MAX           65535
#define HW_MIN            -32768
#define HW_MAX            32767

#define UBYTE_MIN         0
#define UBYTE_MAX         255
#define BYTE_MIN          -128
#define BYTE_MAX          127

//union ui16_f16 { uint16_t ui; float16_t f; };
union ui32_f32 { uint32_t ui; float  f; };
union ui64_f64 { uint64_t ui; double f; };
struct exp8_sig16 { int_fast8_t exp; uint_fast16_t sig; };

uint32_t shiftRightJam32( uint32_t a, uint_fast16_t dist );
uint_fast8_t countLeadingZeros16( uint16_t a );
struct exp8_sig16 normSubnormalF16Sig( uint_fast16_t sig );
uint16_t roundPackToF16( bool sign, int_fast16_t exp, uint_fast16_t sig );

//--------------------------------------------------------------------------
// IEEE - FP Convert instructions
//--------------------------------------------------------------------------
uint16_t f32_to_f16 ( uint32_t a);
uint32_t f16_to_f32( uint16_t a );

uint16_t f16_to_uh( uint16_t op1);
int16_t  f16_to_h ( uint16_t op1);
uint8_t  f16_to_ub( uint16_t op1);
int8_t   f16_to_b ( uint16_t op1);

uint16_t uh_to_f16(uint16_t op1);
uint16_t h_to_f16 (int16_t op1);
uint16_t ub_to_f16(uint8_t op1);
uint16_t b_to_f16 (int8_t op1);

uint16_t sf_to_bf (int32_t op1);

//--------------------------------------------------------------------------
// IEEE - FP ADD/SUB/MPY instructions
//--------------------------------------------------------------------------

//size4s_t fp_mult(size4s_t input_1, size4s_t input_2);
uint32_t fp_mult_sf_sf (uint32_t op1, uint32_t op2);
uint32_t fp_add_sf_sf  (uint32_t op1, uint32_t op2);
uint32_t fp_sub_sf_sf  (uint32_t op1, uint32_t op2);

uint16_t fp_mult_hf_hf (uint16_t op1, uint16_t op2);
uint16_t fp_add_hf_hf  (uint16_t op1, uint16_t op2);
uint16_t fp_sub_hf_hf  (uint16_t op1, uint16_t op2);

uint32_t fp_mult_sf_hf (uint16_t op1, uint16_t op2);
uint32_t fp_add_sf_hf  (uint16_t op1, uint16_t op2);
uint32_t fp_sub_sf_hf  (uint16_t op1, uint16_t op2);

uint32_t fp_mult_sf_bf (uint16_t op1, uint16_t op2);
uint32_t fp_add_sf_bf  (uint16_t op1, uint16_t op2);
uint32_t fp_sub_sf_bf  (uint16_t op1, uint16_t op2);

//--------------------------------------------------------------------------
// IEEE - FP Accumulate instructions
//--------------------------------------------------------------------------

uint16_t fp_mult_hf_hf_acc (uint16_t op1, uint16_t op2, uint16_t acc);
uint32_t fp_mult_sf_bf_acc (uint16_t op1, uint16_t op2, uint32_t acc);
uint32_t fp_mult_sf_hf_acc (uint16_t op1, uint16_t op2, uint32_t acc);

//--------------------------------------------------------------------------
// IEEE - FP Reduce instructions
//--------------------------------------------------------------------------

uint32_t fp_vdmpy      (uint16_t op1_u,uint16_t op1_l,uint16_t op2_u,uint16_t op2_l);
uint32_t fp_vdmpy_acc  (uint32_t acc,uint16_t op1_u,uint16_t op1_l,uint16_t op2_u,uint16_t op2_l);

//--------------------------------------------------------------------------
// IEEE - FP Select instructions
//--------------------------------------------------------------------------

uint16_t fp_min_hf(uint16_t op1,uint16_t op2);
uint16_t fp_max_hf(uint16_t op1,uint16_t op2);
uint32_t fp_min_sf(uint32_t op1,uint32_t op2);
uint32_t fp_max_sf(uint32_t op1,uint32_t op2);
uint16_t fp_min_bf(uint16_t op1,uint16_t op2);
uint16_t fp_max_bf(uint16_t op1,uint16_t op2);
uint16_t fp_abs_bf(uint16_t op1);
uint16_t fp_neg_bf(uint16_t op1);

//--------------------------------------------------------------------------
// IEEE - FP Experiment Implementations
//--------------------------------------------------------------------------
uint16_t fp_mult_hf_hf_acc_dumb (uint16_t op1, uint16_t op2, uint16_t acc);
uint32_t fp_vdmpy_acc_dumb  (uint32_t acc,uint16_t op1_u,uint16_t op1_l,uint16_t op2_u,uint16_t op2_l);
#endif
