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

#include "qemu/osdep.h"
#include "cpu.h"
#include "kvx_ieee.h"
#include "kvx_mac_reduce.c"
#include "qemu/host-utils.h"

uint32_t shiftRightJam32( uint32_t a, uint_fast16_t dist )
{
    return
        (dist < 31) ? a>>dist | ((uint32_t) (a<<(-dist & 31)) != 0) : (a != 0);
}

uint_fast8_t countLeadingZeros16( uint16_t a )
{
    return clz16(a);
}

struct exp8_sig16 normSubnormalF16Sig( uint_fast16_t sig )
{
    int_fast8_t shiftDist;
    struct exp8_sig16 z;

    shiftDist = countLeadingZeros16( sig ) - 5;
    z.exp = 1 - shiftDist;
    z.sig = sig<<shiftDist;
    return z;

}

uint16_t roundPackToF16( bool sign, int_fast16_t exp, uint_fast16_t sig )
{
    bool roundNearEven;
    uint_fast8_t roundIncrement, roundBits;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    roundNearEven  = 1;
    roundIncrement = 0x8;
    roundBits      = sig & 0xF;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if ( 0x1D <= (unsigned int) exp ) {
        if ( exp < 0 ) {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            sig = shiftRightJam32( sig, -exp );
            exp = 0;
            roundBits = sig & 0xF;
            //if ( isTiny && roundBits ) {
            //    softfloat_raiseFlags( softfloat_flag_underflow );
            //}
        } else if ( (0x1D < exp) || (0x8000 <= sig + roundIncrement) ) {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            return packToF16UI( sign, 0x1F, 0 ) - ! roundIncrement;
        }
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    sig = (sig + roundIncrement)>>4;
    sig &= ~(uint_fast16_t) (! (roundBits ^ 8) & roundNearEven);
    if ( ! sig ) exp = 0;

    return packToF16UI( sign, exp, sig );

}


uint32_t fp_mult_sf_sf (uint32_t op1, uint32_t op2)
{

    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    float a,b,rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("fp_mult_sf_sf");
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF32UI(op1) || isNaNF32UI(op2))
       return FP32_DEF_NAN;

    u_op1.ui = op1;
    u_op2.ui = op2;
    a = u_op1.f;
    b = u_op2.f;
    rslt = a*b;
    u_rslt.f = rslt;
    result = u_rslt.ui;

    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint32_t fp_add_sf_sf (uint32_t op1, uint32_t op2)
{
    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    float a,b,rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("fp_add_sf_sf");
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF32UI(op1) || isNaNF32UI(op2))
       return FP32_DEF_NAN;

    u_op1.ui = op1;
    u_op2.ui = op2;
    a = u_op1.f;
    b = u_op2.f;
    rslt = a+b;
    u_rslt.f = rslt;
    result = u_rslt.ui;
    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint32_t fp_sub_sf_sf (uint32_t op1, uint32_t op2)
{
    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    float a,b,rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF32UI(op1) || isNaNF32UI(op2))
       return FP32_DEF_NAN;

    u_op1.ui = op1;
    u_op2.ui = op2;
    a = u_op1.f;
    b = u_op2.f;
    rslt = a-b;
    u_rslt.f = rslt;
    result = u_rslt.ui;
    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

//--------------------------------------------------------------
//Function to convert FP32 to FP16
//--------------------------------------------------------------

uint16_t f32_to_f16 ( uint32_t a)
{
    bool sign;
    int_fast16_t exp;
    uint_fast32_t frac;
    uint_fast16_t frac16;

    sign = signF32UI( a );
    exp  = expF32UI ( a );
    frac = fracF32UI( a );

    // Inf and NaN case
    if ( exp == 0xFF ) {
        if ( frac ) {
           return FP16_DEF_NAN;
        } else {
           return packToF16UI( sign, 0x1F, 0 );
        }
    }

    /*------------------------------------------------------------------------
    frac>>9              : keeping 14 bit of precision out ot 23 bits in FP32
    (frac & 0x1FF) != 0) : setting the sticky bit required for rounding
    *------------------------------------------------------------------------*/
    frac16 = frac>>9 | ((frac & 0x1FF) != 0);

    //If input was a Zero
    if ( ! (exp | frac16) ) {
        return packToF16UI( sign, 0, 0 );
    }

    return roundPackToF16( sign, exp - 0x71, frac16 | 0x4000 );

}

//--------------------------------------------------------------
//Function to convert FP16 to FP32
//--------------------------------------------------------------

uint32_t f16_to_f32( uint16_t a )
{
    bool sign;
    int_fast8_t exp;
    uint_fast16_t frac;
    struct exp8_sig16 normExpSig;

    sign = signF16UI( a );
    exp  = expF16UI ( a );
    frac = fracF16UI( a );


    if ( exp == 0x1F ) {
        if ( frac ) {
           return FP32_DEF_NAN;
        } else {
            return packToF32UI( sign, 0xFF, 0 );
        }
    }


    if ( ! exp ) {
        if ( ! frac ) {
            return packToF32UI( sign, 0, 0 );
        }
        normExpSig = normSubnormalF16Sig( frac );
        exp = normExpSig.exp - 1;
        frac = normExpSig.sig;
    }


    return packToF32UI( sign, exp + 0x70, (uint_fast32_t) frac<<13 );

}

uint16_t fp_mult_hf_hf (uint16_t op1, uint16_t op2)
{

    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;

    float a,b,rslt;
    uint32_t result_f32;
    uint16_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2))
       return FP16_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    a = u_op1.f;
    b = u_op2.f;
    rslt = a*b;
    u_rslt.f = rslt;
    result_f32 = u_rslt.ui;

    result = f32_to_f16(result_f32);

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint16_t fp_add_hf_hf (uint16_t op1, uint16_t op2)
{

    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;

    float a,b,rslt;
    uint32_t result_f32;
    uint16_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2))
       return FP16_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    a = u_op1.f;
    b = u_op2.f;
    rslt = a+b;
    u_rslt.f = rslt;
    result_f32 = u_rslt.ui;

    result = f32_to_f16(result_f32);

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint16_t fp_sub_hf_hf (uint16_t op1, uint16_t op2)
{

    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;

    float a,b,rslt;
    uint32_t result_f32;
    uint16_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2))
       return FP16_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    a = u_op1.f;
    b = u_op2.f;
    rslt = a-b;
    u_rslt.f = rslt;
    result_f32 = u_rslt.ui;

    result = f32_to_f16(result_f32);

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint32_t fp_mult_sf_hf (uint16_t op1, uint16_t op2)
{

    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;

    float a,b,rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2))
       return FP32_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    a = u_op1.f;
    b = u_op2.f;
    rslt = a*b;
    u_rslt.f = rslt;
    result = u_rslt.ui;
    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint32_t fp_add_sf_hf (uint16_t op1, uint16_t op2)
{

    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;

    float a,b,rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2))
       return FP32_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    a = u_op1.f;
    b = u_op2.f;
    rslt = a+b;
    u_rslt.f = rslt;
    result = u_rslt.ui;
    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint32_t fp_sub_sf_hf (uint16_t op1, uint16_t op2)
{

    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;

    float a,b,rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2))
       return FP32_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    a = u_op1.f;
    b = u_op2.f;
    rslt = a-b;
    u_rslt.f = rslt;
    result = u_rslt.ui;
    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint32_t fp_mult_sf_bf_acc (uint16_t op1, uint16_t op2, uint32_t acc)
{
    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_acc;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;

    double a,b,facc,rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%04x\n",op1);
    printf("Debug : op2 =0x%04x\n",op2);
    printf("Debug : acc =0x%08x\n",acc);
    #endif

    op1_f32 = ((uint32_t)op1) << 16;
    op2_f32 = ((uint32_t)op2) << 16;

    if(isNaNF32UI(op1_f32) || isNaNF32UI(op2_f32) || isNaNF32UI(acc))
       return FP32_DEF_NAN;

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    u_acc.ui = acc;
    a = u_op1.f;
    b = u_op2.f;
    facc = u_acc.f;
    //rslt = fma(a,b,facc);
    rslt = (a * b) + facc;
    u_rslt.f = rslt;
    result = u_rslt.ui;
    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : facc = %f\n",facc);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%04x\n",result);
    #endif

    return result;
}

uint32_t fp_mult_sf_bf (uint16_t op1, uint16_t op2)
{
    uint32_t op1_f32;
    uint32_t op2_f32;
    op1_f32 = ((uint32_t)op1) << 16;
    op2_f32 = ((uint32_t)op2) << 16;
    return fp_mult_sf_sf(op1_f32, op2_f32);
}

uint32_t fp_add_sf_bf (uint16_t op1, uint16_t op2)
{
    uint32_t op1_f32;
    uint32_t op2_f32;
    op1_f32 = ((uint32_t)op1) << 16;
    op2_f32 = ((uint32_t)op2) << 16;
    return fp_add_sf_sf(op1_f32, op2_f32);
}

uint32_t fp_sub_sf_bf (uint16_t op1, uint16_t op2)
{
    uint32_t op1_f32;
    uint32_t op2_f32;
    op1_f32 = ((uint32_t)op1) << 16;
    op2_f32 = ((uint32_t)op2) << 16;
    return fp_sub_sf_sf(op1_f32, op2_f32);
}

uint16_t f16_to_uh( uint16_t op1)
{
    union ui32_f32 u_op1;

    float a,frac;
    uint32_t op1_f32;
    uint16_t result;

    //converting a NaN to an integral ----> Vx4Rslt is +MAX_INT
    if(isNaNF16UI(op1))
    {
       result = UHW_MAX;
       goto end;
    }
    //converting a negative floating-point value to
    //unsigned integer U(h|b) ----> (Vx4Rslt is 0)
    if(signF16UI(op1))
    {
       result = 0x0;
       goto end;
    }
    //converting ±Inf to an integral ----> Vx4Rslt is ±MAX_INT
    if(isInfF16UI(op1))
    {
       result = UHW_MAX;
       goto end;
    }
    //out of range FP to integer ------> Vx4Rslt is ±MAX_INT

    //The default float-to-integer conversion in C does not
    //round to the nearest integer, but instead truncates toward zero.
    op1_f32 = f16_to_f32(op1);
    u_op1.ui = op1_f32;
    a = u_op1.f;
    frac = a - (float)((uint16_t) a);
    //round to the nearest
    result = (uint16_t) (a + 0.5);
    //Ties to Even
    if(frac == 0.5)
    {
       if((result % 2)) result--;
    }
    #ifdef DEBUG
    printf("Debug : a      = %f\n",a);
    printf("Debug : a frac = %f\n",frac);
    #endif

 end:
    #ifdef DEBUG
    printf("Debug : result =0x%x\n",result);
    #endif
    return result;
}

int16_t f16_to_h( uint16_t op1)
{
    union ui32_f32 u_op1;

    float a,frac;
    uint32_t op1_f32;
    int16_t  result;

    //converting a NaN to an integral ----> Vx4Rslt is +MAX_INT
    if(isNaNF16UI(op1))
    {
       result = HW_MAX;
       goto end;
    }
    //converting ±Inf to an integral ----> Vx4Rslt is ±MAX_INT
    if(isInfF16UI(op1))
    {
       result = signF16UI(op1) ? HW_MIN : HW_MAX;
       goto end;
    }

    //The default float-to-integer conversion in C does not round
    //to the nearest integer, but instead truncates toward zero.
    op1_f32 = f16_to_f32(op1);
    u_op1.ui = op1_f32;
    a = u_op1.f;

    //out of range FP to integer ------> Vx4Rslt is ±MAX_INT
    if(a > (float)(HW_MAX))
    {
       result = HW_MAX;
       goto end;
    }
    if(a < (float)(HW_MIN))
    {
       result = HW_MIN;
       goto end;
    }

    frac = fabs(a - (float)((int16_t) a));
    //round to the nearest
    result = (a > 0) ? ((int16_t) (a + 0.5)) : ((int16_t) (a - 0.5));
    //Ties to Even
    if(frac == 0.5)
    {
       if((result % 2))
       {
          if(a > 0) result--;
          if(a < 0) result++;
       }
    }
    #ifdef DEBUG
    printf("Debug : a      = %f\n",a);
    printf("Debug : a frac = %f\n",frac);
    #endif

 end:
    #ifdef DEBUG
    printf("Debug : result =0x%04x\n",result);
    #endif
    return result;
}

uint8_t f16_to_ub( uint16_t op1)
{
    union ui32_f32 u_op1;

    float a,frac;
    uint32_t op1_f32;
    uint8_t result;

    //converting a NaN to an integral ----> Vx4Rslt is +MAX_INT
    if(isNaNF16UI(op1))
    {
       result = UBYTE_MAX;
       goto end;
    }
    //converting a negative floating-point value to
    //unsigned integer U(h|b) ----> (Vx4Rslt is 0)
    if(signF16UI(op1))
    {
       result = 0x0;
       goto end;
    }
    //converting ±Inf to an integral ----> Vx4Rslt is ±MAX_INT
    if(isInfF16UI(op1))
    {
       result = UBYTE_MAX;
       goto end;
    }

    //The default float-to-integer conversion in C does
    //not round to the nearest integer, but instead truncates toward zero.
    op1_f32 = f16_to_f32(op1);
    u_op1.ui = op1_f32;
    a = u_op1.f;

    //out of range FP to integer ------> Vx4Rslt is ±MAX_INT
    if( a  > (float)(UBYTE_MAX))
    {
       result = UBYTE_MAX;
       goto end;
    }

    frac = a - (float)((uint16_t) a);
    //round to the nearest
    result = (uint8_t) (a + 0.5);
    //Ties to Even
    if(frac == 0.5)
    {
       if((result % 2))
       {
          if(a > 0) result--;
          if(a < 0) result++;
       }
    }
    #ifdef DEBUG
    printf("Debug : a      = %f\n",a);
    printf("Debug : a frac = %f\n",frac);
    #endif

 end:
    #ifdef DEBUG
    printf("Debug : result =0x%x\n",result);
    #endif
    return result;
}

int8_t f16_to_b( uint16_t op1)
{
    union ui32_f32 u_op1;

    float a,frac;
    uint32_t op1_f32;
    int16_t  result;

    //converting a NaN to an integral ----> Vx4Rslt is +MAX_INT
    if(isNaNF16UI(op1))
    {
       result = BYTE_MAX;
       goto end;
    }
    //converting ±Inf to an integral ----> Vx4Rslt is ±MAX_INT
    if(isInfF16UI(op1))
    {
       result = signF16UI(op1) ? BYTE_MIN : BYTE_MAX;
       goto end;
    }

    //The default float-to-integer conversion in C does not
    //round to the nearest integer, but instead truncates toward zero.
    op1_f32 = f16_to_f32(op1);
    u_op1.ui = op1_f32;
    a = u_op1.f;

    //out of range FP to integer ------> Vx4Rslt is ±MAX_INT
    if(a > (float)(BYTE_MAX))
    {
       result = BYTE_MAX;
       goto end;
    }
    if(a < (float)(BYTE_MIN))
    {
       result = BYTE_MIN;
       goto end;
    }

    frac = fabs(a - (float)((int16_t) a));
    //round to the nearest
    result = (a > 0) ? ((int16_t) (a + 0.5)) : ((int16_t) (a - 0.5));
    //Ties to Even
    if(frac == 0.5)
    {
       if((result % 2))
       {
          if(a > 0) result--;
          if(a < 0) result++;
       }
    }
    #ifdef DEBUG
    printf("Debug : a      = %f\n",a);
    printf("Debug : a frac = %f\n",frac);
    #endif

 end:
    #ifdef DEBUG
    printf("Debug : result =0x%04x\n",result);
    #endif
    return result;
}

uint16_t uh_to_f16(uint16_t op1)
{
    union ui32_f32 u_op1;

    float a;
    uint32_t rslt;
    uint16_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    #endif

    a = (float) op1;
    u_op1.f = a;
    rslt = u_op1.ui;
    result = f32_to_f16(rslt);

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : rslt = 0x%08x\n",rslt);
    printf("Debug : result =0x%04x\n",result);
    #endif

    return result;
}

uint16_t h_to_f16 (int16_t op1)
{
    union ui32_f32 u_op1;

    float a;
    uint32_t rslt;
    uint16_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    #endif

    a = (float) op1;
    u_op1.f = a;
    rslt = u_op1.ui;
    result = f32_to_f16(rslt);

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : rslt = 0x%08x\n",rslt);
    printf("Debug : result =0x%04x\n",result);
    #endif

    return result;
}

uint16_t ub_to_f16(uint8_t op1)
{
    union ui32_f32 u_op1;

    float a;
    uint32_t rslt;
    uint16_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    #endif

    a = (float) op1;
    u_op1.f = a;
    rslt = u_op1.ui;
    result = f32_to_f16(rslt);

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : rslt = 0x%08x\n",rslt);
    printf("Debug : result =0x%04x\n",result);
    #endif

    return result;
}

uint16_t b_to_f16 (int8_t op1)
{
    union ui32_f32 u_op1;

    float a;
    uint32_t rslt;
    uint16_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    #endif

    a = (float) op1;
    u_op1.f = a;
    rslt = u_op1.ui;
    result = f32_to_f16(rslt);

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : rslt = 0x%08x\n",rslt);
    printf("Debug : result =0x%04x\n",result);
    #endif

    return result;
}

uint16_t sf_to_bf (int32_t op1)
{
    uint32_t rslt = op1;
    if((rslt & 0x1FFFF) == 0x08000){
        //break; // do not round up if exactly .5 and even already
    }
    else if ((rslt & 0x8000) == 0x8000){
        rslt += 0x8000; //rounding to nearest number
    }
    rslt = isNaNF32UI(op1) ? FP32_DEF_NAN : rslt;
    uint16_t result = (rslt >> 16);
    return result;
}

uint32_t fp_vdmpy (uint16_t op1_u,uint16_t op1_l,uint16_t op2_u,uint16_t op2_l)
{
    union ui32_f32 u_op;
    union ui32_f32 u_rslt;

    uint32_t op1_u_f32, op1_l_f32, op2_u_f32, op2_l_f32;
    float f_op1_u, f_op1_l, f_op2_u, f_op2_l;
    double f_prod_l, f_prod_u, rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("Debug : op1_u =0x%04x\n",op1_u);
    printf("Debug : op1_l =0x%04x\n",op1_l);
    printf("Debug : op2_u =0x%04x\n",op2_u);
    printf("Debug : op2_l =0x%04x\n",op2_l);
    #endif

    if(isNaNF16UI(op1_u) || isNaNF16UI(op1_l) || isNaNF16UI(op2_u) ||
       isNaNF16UI(op2_l))
    {   result = FP32_DEF_NAN;
        goto end;
    }

    op1_u_f32 = f16_to_f32(op1_u);
    op1_l_f32 = f16_to_f32(op1_l);
    op2_u_f32 = f16_to_f32(op2_u);
    op2_l_f32 = f16_to_f32(op2_l);

    u_op.ui = op1_u_f32;
    f_op1_u = u_op.f;

    u_op.ui = op1_l_f32;
    f_op1_l = u_op.f;

    u_op.ui = op2_l_f32;
    f_op2_l = u_op.f;

    u_op.ui = op2_u_f32;
    f_op2_u = u_op.f;

    f_prod_l = f_op1_l * f_op2_l;
    f_prod_u = f_op1_u * f_op2_u;
    rslt     = f_prod_u + f_prod_l;

    u_rslt.f = rslt;
    result = u_rslt.ui;
    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

    #ifdef DEBUG
    printf("Debug : f_op1_u = %f\n",f_op1_u);
    printf("Debug : f_op1_l = %f\n",f_op1_l);
    printf("Debug : f_op2_u = %f\n",f_op2_u);
    printf("Debug : f_op2_l = %f\n",f_op2_l);
    printf("Debug : f_prod_l = %f\n",f_prod_l);
    printf("Debug : f_prod_u = %f\n",f_prod_u);
    printf("Debug : rslt = %f\n",rslt);
    #endif

end:
    #ifdef DEBUG
    printf("Debug : result =0x%08x\n",result);
    #endif
    return result;
}

uint32_t fp_vdmpy_acc_dumb  (uint32_t acc,uint16_t op1_u,uint16_t op1_l,
    uint16_t op2_u,uint16_t op2_l)
{
    union ui32_f32 u_op;
    union ui32_f32 u_acc;
    union ui32_f32 u_rslt;

    uint32_t op1_u_f32, op1_l_f32, op2_u_f32, op2_l_f32;
    float f_op1_u, f_op1_l, f_op2_u, f_op2_l, f_acc;
    long double f_prod_l, f_prod_u, rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("Debug : op1_u =0x%04x\n",op1_u);
    printf("Debug : op1_l =0x%04x\n",op1_l);
    printf("Debug : op2_u =0x%04x\n",op2_u);
    printf("Debug : op2_l =0x%04x\n",op2_l);
    printf("Debug : acc   =0x%08x\n",acc);
    #endif

    op1_u_f32 = f16_to_f32(op1_u);
    op1_l_f32 = f16_to_f32(op1_l);
    op2_u_f32 = f16_to_f32(op2_u);
    op2_l_f32 = f16_to_f32(op2_l);

    u_op.ui = op1_u_f32;
    f_op1_u = u_op.f;

    u_op.ui = op1_l_f32;
    f_op1_l = u_op.f;

    u_op.ui = op2_l_f32;
    f_op2_l = u_op.f;

    u_op.ui = op2_u_f32;
    f_op2_u = u_op.f;

    u_acc.ui = acc;
    f_acc   = u_acc.f;

    f_prod_l =  (long double)(f_op1_l * f_op2_l);
    f_prod_u =  (long double)(f_op1_u * f_op2_u);
    rslt     =  (long double)((long double)f_acc + f_prod_u + f_prod_l);

    u_rslt.f = rslt;
    result = u_rslt.ui;
    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

    #ifdef DEBUG
    printf("Debug : f_op1_u = %f\n",f_op1_u);
    printf("Debug : f_op1_l = %f\n",f_op1_l);
    printf("Debug : f_op2_u = %f\n",f_op2_u);
    printf("Debug : f_op2_l = %f\n",f_op2_l);
    printf("Debug : f_acc   = %f\n",f_acc);
    printf("Debug : f_prod_l = %Lf\n",f_prod_l);
    printf("Debug : f_prod_u = %Lf\n",f_prod_u);
    printf("Debug : rslt = %Lf\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint16_t fp_min_hf(uint16_t op1,uint16_t op2)
{
    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;

    float a,b,rslt;
    uint32_t result_f32;
    uint16_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2))
       return FP16_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    a = u_op1.f;
    b = u_op2.f;

    rslt = (a>b) ? b : a;
    // +0 is evaluated equal to -0 in C. Handeling that case separatly
    if( (fabs(a) == 0.0f) && (fabs(b) == 0.0f) && (signF16UI(op1) !=
        signF16UI(op2)) )
    {
       rslt = signF16UI(op1) ? a : b;
    }
    u_rslt.f = rslt;
    result_f32 = u_rslt.ui;

    result = f32_to_f16(result_f32);

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;

}

uint32_t fp_min_sf(uint32_t op1,uint32_t op2)
{
    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    float a,b,rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF32UI(op1) || isNaNF32UI(op2))
       return FP32_DEF_NAN;

    u_op1.ui = op1;
    u_op2.ui = op2;
    a = u_op1.f;
    b = u_op2.f;
    rslt = (a>b) ? b : a;
    // +0 is evaluated equal to -0 in C. Handeling that case separatly
    if( (fabs(a) == 0.0f) && (fabs(b) == 0.0f) &&
         (signF32UI(op1) != signF32UI(op2)) )
    {
       rslt = signF32UI(op1) ? a : b;
    }
    u_rslt.f = rslt;
    result = u_rslt.ui;

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint16_t fp_min_bf(uint16_t op1,uint16_t op2)
{
    uint32_t op1_f32;
    uint32_t op2_f32;

    uint32_t result_f32;
    uint16_t result;

    op1_f32 = ((uint32_t)op1) << 16;
    op2_f32 = ((uint32_t)op2) << 16;

    result_f32 = fp_min_sf(op1_f32, op2_f32);
    result_f32 = result_f32 >> 16;
    result = result_f32 & 0xFFFF;
    return result;
}


uint16_t fp_max_hf(uint16_t op1,uint16_t op2)
{
    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;

    float a,b,rslt;
    uint32_t result_f32;
    uint16_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2))
       return FP16_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    a = u_op1.f;
    b = u_op2.f;

    rslt = (a>b) ? a : b;
    // +0 is evaluated equal to -0 in C. Handeling that case separatly
    if( (fabs(a) == 0.0f) &&
        (fabs(b) == 0.0f) && (signF16UI(op1) != signF16UI(op2)) )
    {
       rslt = signF16UI(op1) ? b : a;
    }
    u_rslt.f = rslt;
    result_f32 = u_rslt.ui;

    result = f32_to_f16(result_f32);

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;

}

uint32_t fp_max_sf(uint32_t op1,uint32_t op2)
{
    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_rslt;

    float a,b,rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%08x\n",op1);
    printf("Debug : op2 =0x%08x\n",op2);
    #endif

    if(isNaNF32UI(op1) || isNaNF32UI(op2))
       return FP32_DEF_NAN;

    u_op1.ui = op1;
    u_op2.ui = op2;
    a = u_op1.f;
    b = u_op2.f;
    rslt = (a>b) ? a : b;
    // +0 is evaluated equal to -0 in C. Handeling that case separatly
    if( (fabs(a) == 0.0f) && (fabs(b) == 0.0f) &&
         (signF32UI(op1) != signF32UI(op2)) )
    {
       rslt = signF32UI(op1) ? b : a;
    }
    u_rslt.f = rslt;
    result = u_rslt.ui;

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
    #endif

    return result;
}

uint16_t fp_max_bf(uint16_t op1,uint16_t op2)
{
    uint32_t op1_f32;
    uint32_t op2_f32;

    uint32_t result_f32;
    uint16_t result;

    op1_f32 = ((uint32_t)op1) << 16;
    op2_f32 = ((uint32_t)op2) << 16;

    result_f32 = fp_max_sf(op1_f32, op2_f32);
    result_f32 = result_f32 >> 16;
    result = result_f32 & 0xFFFF;
    return result;
}

uint16_t fp_abs_bf(uint16_t op1)
{
    union ui32_f32 u_op1;

    float result_f;
    uint32_t result_f32;
    uint16_t result;

    u_op1.ui = ((uint32_t)op1) << 16;

    result_f = fabs(u_op1.f);
    u_op1.f = result_f;
    result_f32 = u_op1.ui >> 16;
    result = result_f32 & 0xFFFF;
    return result;
}

uint16_t fp_neg_bf(uint16_t op1)
{
    union ui32_f32 u_op1;

    float result_f;
    uint32_t result_f32;
    uint16_t result;

    u_op1.ui = ((uint32_t)op1) << 16;

    result_f = -(u_op1.f);
    u_op1.f = result_f;
    result_f32 = u_op1.ui >> 16;
    result = result_f32 & 0xFFFF;
    return result;
}

//float fmaf( float x, float y, float z );
uint16_t fp_mult_hf_hf_acc_dumb (uint16_t op1, uint16_t op2, uint16_t acc)
{
    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_acc;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;
    uint32_t acc_f32;

    float a,b,facc,rslt;
    uint32_t result_f32;
    uint16_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%04x\n",op1);
    printf("Debug : op2 =0x%04x\n",op2);
    printf("Debug : acc =0x%04x\n",acc);
    #endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2) || isNaNF16UI(acc))
       return FP16_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);
    acc_f32 = f16_to_f32(acc);

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    u_acc.ui = acc_f32;
    a = u_op1.f;
    b = u_op2.f;
    facc = u_acc.f;
    //rslt = fma(a,b,facc);
    rslt = (a * b) + facc;
    u_rslt.f = rslt;
    result_f32 = u_rslt.ui;

    result = f32_to_f16(result_f32);

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : facc = %f\n",facc);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%04x\n",result);
    #endif

    return result;
}

uint32_t fp_mult_sf_hf_acc (uint16_t op1, uint16_t op2, uint32_t acc)
{
    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_acc;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;

    float a,b,facc,rslt;
    uint32_t result;

    #ifdef DEBUG
    printf("Debug : op1 =0x%04x\n",op1);
    printf("Debug : op2 =0x%04x\n",op2);
    printf("Debug : acc =0x%08x\n",acc);
    #endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2) || isNaNF32UI(acc))
       return FP32_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    u_acc.ui = acc;
    a = u_op1.f;
    b = u_op2.f;
    facc = u_acc.f;
    //rslt = fma(a,b,facc);
    rslt = (a * b) + facc;
    u_rslt.f = rslt;
    result = u_rslt.ui;
    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

    #ifdef DEBUG
    printf("Debug : a = %f\n",a);
    printf("Debug : b = %f\n",b);
    printf("Debug : facc = %f\n",facc);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%04x\n",result);
    #endif

    return result;
}
