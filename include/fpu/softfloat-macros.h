/*
 * QEMU float support macros
 *
 * The code in this source file is derived from release 2a of the SoftFloat
 * IEC/IEEE Floating-point Arithmetic Package. Those parts of the code (and
 * some later contributions) are provided under that license, as detailed below.
 * It has subsequently been modified by contributors to the QEMU Project,
 * so some portions are provided under:
 *  the SoftFloat-2a license
 *  the BSD license
 *
 * Any future contributions to this file after December 1st 2014 will be
 * taken to be licensed under the Softfloat-2a license unless specifically
 * indicated otherwise.
 */

/*
===============================================================================
This C source fragment is part of the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2a.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://HTTP.CS.Berkeley.EDU/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT
TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO
PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ANY
AND ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM ITS USE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) they include prominent notice that the work is derivative, and (2) they
include prominent notice akin to these four paragraphs for those parts of
this code that are retained.

===============================================================================
*/

/* BSD licensing:
 * Copyright (c) 2006, Fabrice Bellard
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef FPU_SOFTFLOAT_MACROS_H
#define FPU_SOFTFLOAT_MACROS_H

#include "fpu/softfloat-types.h"
#include "qemu/host-utils.h"

/**
 * shl_double: double-word merging left shift
 * @l: left or most-significant word
 * @r: right or least-significant word
 * @c: shift count
 *
 * Shift @l left by @c bits, shifting in bits from @r.
 */
static inline uint64_t shl_double(uint64_t l, uint64_t r, int c)
{
#if defined(__x86_64__)
    asm("shld %b2, %1, %0" : "+r"(l) : "r"(r), "ci"(c));
    return l;
#else
    return c ? (l << c) | (r >> (64 - c)) : l;
#endif
}

/**
 * shr_double: double-word merging right shift
 * @l: left or most-significant word
 * @r: right or least-significant word
 * @c: shift count
 *
 * Shift @r right by @c bits, shifting in bits from @l.
 */
static inline uint64_t shr_double(uint64_t l, uint64_t r, int c)
{
#if defined(__x86_64__)
    asm("shrd %b2, %1, %0" : "+r"(r) : "r"(l), "ci"(c));
    return r;
#else
    return c ? (r >> c) | (l << (64 - c)) : r;
#endif
}

/*----------------------------------------------------------------------------
| Shifts `a' right by the number of bits given in `count'.  If any nonzero
| bits are shifted off, they are ``jammed'' into the least significant bit of
| the result by setting the least significant bit to 1.  The value of `count'
| can be arbitrarily large; in particular, if `count' is greater than 32, the
| result will be either 0 or 1, depending on whether `a' is zero or nonzero.
| The result is stored in the location pointed to by `zPtr'.
*----------------------------------------------------------------------------*/

static inline void shift32RightJamming(uint32_t a, int count, uint32_t *zPtr)
{
    uint32_t z;

    if ( count == 0 ) {
        z = a;
    }
    else if ( count < 32 ) {
        z = ( a>>count ) | ( ( a<<( ( - count ) & 31 ) ) != 0 );
    }
    else {
        z = ( a != 0 );
    }
    *zPtr = z;

}

/*----------------------------------------------------------------------------
| Shifts `a' right by the number of bits given in `count'.  If any nonzero
| bits are shifted off, they are ``jammed'' into the least significant bit of
| the result by setting the least significant bit to 1.  The value of `count'
| can be arbitrarily large; in particular, if `count' is greater than 64, the
| result will be either 0 or 1, depending on whether `a' is zero or nonzero.
| The result is stored in the location pointed to by `zPtr'.
*----------------------------------------------------------------------------*/

static inline void shift64RightJamming(uint64_t a, int count, uint64_t *zPtr)
{
    uint64_t z;

    if ( count == 0 ) {
        z = a;
    }
    else if ( count < 64 ) {
        z = ( a>>count ) | ( ( a<<( ( - count ) & 63 ) ) != 0 );
    }
    else {
        z = ( a != 0 );
    }
    *zPtr = z;

}

/*----------------------------------------------------------------------------
| Shifts the 128-bit value formed by concatenating `a0' and `a1' right by 64
| _plus_ the number of bits given in `count'.  The shifted result is at most
| 64 nonzero bits; this is stored at the location pointed to by `z0Ptr'.  The
| bits shifted off form a second 64-bit result as follows:  The _last_ bit
| shifted off is the most-significant bit of the extra result, and the other
| 63 bits of the extra result are all zero if and only if _all_but_the_last_
| bits shifted off were all zero.  This extra result is stored in the location
| pointed to by `z1Ptr'.  The value of `count' can be arbitrarily large.
|     (This routine makes more sense if `a0' and `a1' are considered to form a
| fixed-point value with binary point between `a0' and `a1'.  This fixed-point
| value is shifted right by the number of bits given in `count', and the
| integer part of the result is returned at the location pointed to by
| `z0Ptr'.  The fractional part of the result may be slightly corrupted as
| described above, and is returned at the location pointed to by `z1Ptr'.)
*----------------------------------------------------------------------------*/

static inline void
 shift64ExtraRightJamming(
     uint64_t a0, uint64_t a1, int count, uint64_t *z0Ptr, uint64_t *z1Ptr)
{
    uint64_t z0, z1;
    int8_t negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z1 = a1;
        z0 = a0;
    }
    else if ( count < 64 ) {
        z1 = ( a0<<negCount ) | ( a1 != 0 );
        z0 = a0>>count;
    }
    else {
        if ( count == 64 ) {
            z1 = a0 | ( a1 != 0 );
        }
        else {
            z1 = ( ( a0 | a1 ) != 0 );
        }
        z0 = 0;
    }
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Shifts the 128-bit value formed by concatenating `a0' and `a1' right by the
| number of bits given in `count'.  Any bits shifted off are lost.  The value
| of `count' can be arbitrarily large; in particular, if `count' is greater
| than 128, the result will be 0.  The result is broken into two 64-bit pieces
| which are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 shift128Right(
     uint64_t a0, uint64_t a1, int count, uint64_t *z0Ptr, uint64_t *z1Ptr)
{
    uint64_t z0, z1;
    int8_t negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z1 = a1;
        z0 = a0;
    }
    else if ( count < 64 ) {
        z1 = ( a0<<negCount ) | ( a1>>count );
        z0 = a0>>count;
    }
    else {
        z1 = (count < 128) ? (a0 >> (count & 63)) : 0;
        z0 = 0;
    }
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Shifts the 128-bit value formed by concatenating `a0' and `a1' right by the
| number of bits given in `count'.  If any nonzero bits are shifted off, they
| are ``jammed'' into the least significant bit of the result by setting the
| least significant bit to 1.  The value of `count' can be arbitrarily large;
| in particular, if `count' is greater than 128, the result will be either
| 0 or 1, depending on whether the concatenation of `a0' and `a1' is zero or
| nonzero.  The result is broken into two 64-bit pieces which are stored at
| the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 shift128RightJamming(
     uint64_t a0, uint64_t a1, int count, uint64_t *z0Ptr, uint64_t *z1Ptr)
{
    uint64_t z0, z1;
    int8_t negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z1 = a1;
        z0 = a0;
    }
    else if ( count < 64 ) {
        z1 = ( a0<<negCount ) | ( a1>>count ) | ( ( a1<<negCount ) != 0 );
        z0 = a0>>count;
    }
    else {
        if ( count == 64 ) {
            z1 = a0 | ( a1 != 0 );
        }
        else if ( count < 128 ) {
            z1 = ( a0>>( count & 63 ) ) | ( ( ( a0<<negCount ) | a1 ) != 0 );
        }
        else {
            z1 = ( ( a0 | a1 ) != 0 );
        }
        z0 = 0;
    }
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Shifts the 192-bit value formed by concatenating `a0', `a1', and `a2' right
| by 64 _plus_ the number of bits given in `count'.  The shifted result is
| at most 128 nonzero bits; these are broken into two 64-bit pieces which are
| stored at the locations pointed to by `z0Ptr' and `z1Ptr'.  The bits shifted
| off form a third 64-bit result as follows:  The _last_ bit shifted off is
| the most-significant bit of the extra result, and the other 63 bits of the
| extra result are all zero if and only if _all_but_the_last_ bits shifted off
| were all zero.  This extra result is stored in the location pointed to by
| `z2Ptr'.  The value of `count' can be arbitrarily large.
|     (This routine makes more sense if `a0', `a1', and `a2' are considered
| to form a fixed-point value with binary point between `a1' and `a2'.  This
| fixed-point value is shifted right by the number of bits given in `count',
| and the integer part of the result is returned at the locations pointed to
| by `z0Ptr' and `z1Ptr'.  The fractional part of the result may be slightly
| corrupted as described above, and is returned at the location pointed to by
| `z2Ptr'.)
*----------------------------------------------------------------------------*/

static inline void
 shift128ExtraRightJamming(
     uint64_t a0,
     uint64_t a1,
     uint64_t a2,
     int count,
     uint64_t *z0Ptr,
     uint64_t *z1Ptr,
     uint64_t *z2Ptr
 )
{
    uint64_t z0, z1, z2;
    int8_t negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z2 = a2;
        z1 = a1;
        z0 = a0;
    }
    else {
        if ( count < 64 ) {
            z2 = a1<<negCount;
            z1 = ( a0<<negCount ) | ( a1>>count );
            z0 = a0>>count;
        }
        else {
            if ( count == 64 ) {
                z2 = a1;
                z1 = a0;
            }
            else {
                a2 |= a1;
                if ( count < 128 ) {
                    z2 = a0<<negCount;
                    z1 = a0>>( count & 63 );
                }
                else {
                    z2 = ( count == 128 ) ? a0 : ( a0 != 0 );
                    z1 = 0;
                }
            }
            z0 = 0;
        }
        z2 |= ( a2 != 0 );
    }
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Shifts the 128-bit value formed by concatenating `a0' and `a1' left by the
| number of bits given in `count'.  Any bits shifted off are lost.  The value
| of `count' must be less than 64.  The result is broken into two 64-bit
| pieces which are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void shortShift128Left(uint64_t a0, uint64_t a1, int count,
                                     uint64_t *z0Ptr, uint64_t *z1Ptr)
{
    *z1Ptr = a1 << count;
    *z0Ptr = count == 0 ? a0 : (a0 << count) | (a1 >> (-count & 63));
}

/*----------------------------------------------------------------------------
| Shifts the 128-bit value formed by concatenating `a0' and `a1' left by the
| number of bits given in `count'.  Any bits shifted off are lost.  The value
| of `count' may be greater than 64.  The result is broken into two 64-bit
| pieces which are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void shift128Left(uint64_t a0, uint64_t a1, int count,
                                uint64_t *z0Ptr, uint64_t *z1Ptr)
{
    if (count < 64) {
        *z1Ptr = a1 << count;
        *z0Ptr = count == 0 ? a0 : (a0 << count) | (a1 >> (-count & 63));
    } else {
        *z1Ptr = 0;
        *z0Ptr = a1 << (count - 64);
    }
}

/*----------------------------------------------------------------------------
| Shifts the 192-bit value formed by concatenating `a0', `a1', and `a2' left
| by the number of bits given in `count'.  Any bits shifted off are lost.
| The value of `count' must be less than 64.  The result is broken into three
| 64-bit pieces which are stored at the locations pointed to by `z0Ptr',
| `z1Ptr', and `z2Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 shortShift192Left(
     uint64_t a0,
     uint64_t a1,
     uint64_t a2,
     int count,
     uint64_t *z0Ptr,
     uint64_t *z1Ptr,
     uint64_t *z2Ptr
 )
{
    uint64_t z0, z1, z2;
    int8_t negCount;

    z2 = a2<<count;
    z1 = a1<<count;
    z0 = a0<<count;
    if ( 0 < count ) {
        negCount = ( ( - count ) & 63 );
        z1 |= a2>>negCount;
        z0 |= a1>>negCount;
    }
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Adds the 128-bit value formed by concatenating `a0' and `a1' to the 128-bit
| value formed by concatenating `b0' and `b1'.  Addition is modulo 2^128, so
| any carry out is lost.  The result is broken into two 64-bit pieces which
| are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void add128(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1,
                          uint64_t *z0Ptr, uint64_t *z1Ptr)
{
    bool c = 0;
    *z1Ptr = uadd64_carry(a1, b1, &c);
    *z0Ptr = uadd64_carry(a0, b0, &c);
}

/*----------------------------------------------------------------------------
| Adds the 192-bit value formed by concatenating `a0', `a1', and `a2' to the
| 192-bit value formed by concatenating `b0', `b1', and `b2'.  Addition is
| modulo 2^192, so any carry out is lost.  The result is broken into three
| 64-bit pieces which are stored at the locations pointed to by `z0Ptr',
| `z1Ptr', and `z2Ptr'.
*----------------------------------------------------------------------------*/

static inline void add192(uint64_t a0, uint64_t a1, uint64_t a2,
                          uint64_t b0, uint64_t b1, uint64_t b2,
                          uint64_t *z0Ptr, uint64_t *z1Ptr, uint64_t *z2Ptr)
{
    bool c = 0;
    *z2Ptr = uadd64_carry(a2, b2, &c);
    *z1Ptr = uadd64_carry(a1, b1, &c);
    *z0Ptr = uadd64_carry(a0, b0, &c);
}

/*----------------------------------------------------------------------------
| Subtracts the 128-bit value formed by concatenating `b0' and `b1' from the
| 128-bit value formed by concatenating `a0' and `a1'.  Subtraction is modulo
| 2^128, so any borrow out (carry out) is lost.  The result is broken into two
| 64-bit pieces which are stored at the locations pointed to by `z0Ptr' and
| `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void sub128(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1,
                          uint64_t *z0Ptr, uint64_t *z1Ptr)
{
    bool c = 0;
    *z1Ptr = usub64_borrow(a1, b1, &c);
    *z0Ptr = usub64_borrow(a0, b0, &c);
}

/*----------------------------------------------------------------------------
| Subtracts the 192-bit value formed by concatenating `b0', `b1', and `b2'
| from the 192-bit value formed by concatenating `a0', `a1', and `a2'.
| Subtraction is modulo 2^192, so any borrow out (carry out) is lost.  The
| result is broken into three 64-bit pieces which are stored at the locations
| pointed to by `z0Ptr', `z1Ptr', and `z2Ptr'.
*----------------------------------------------------------------------------*/

static inline void sub192(uint64_t a0, uint64_t a1, uint64_t a2,
                          uint64_t b0, uint64_t b1, uint64_t b2,
                          uint64_t *z0Ptr, uint64_t *z1Ptr, uint64_t *z2Ptr)
{
    bool c = 0;
    *z2Ptr = usub64_borrow(a2, b2, &c);
    *z1Ptr = usub64_borrow(a1, b1, &c);
    *z0Ptr = usub64_borrow(a0, b0, &c);
}

/*----------------------------------------------------------------------------
| Multiplies `a' by `b' to obtain a 128-bit product.  The product is broken
| into two 64-bit pieces which are stored at the locations pointed to by
| `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void
mul64To128(uint64_t a, uint64_t b, uint64_t *z0Ptr, uint64_t *z1Ptr)
{
    mulu64(z1Ptr, z0Ptr, a, b);
}

/*----------------------------------------------------------------------------
| Multiplies the 128-bit value formed by concatenating `a0' and `a1' by
| `b' to obtain a 192-bit product.  The product is broken into three 64-bit
| pieces which are stored at the locations pointed to by `z0Ptr', `z1Ptr', and
| `z2Ptr'.
*----------------------------------------------------------------------------*/

static inline void
mul128By64To192(uint64_t a0, uint64_t a1, uint64_t b,
                uint64_t *z0Ptr, uint64_t *z1Ptr, uint64_t *z2Ptr)
{
    uint64_t z0, z1, m1;

    mul64To128(a1, b, &m1, z2Ptr);
    mul64To128(a0, b, &z0, &z1);
    add128(z0, z1, 0, m1, z0Ptr, z1Ptr);
}

/*----------------------------------------------------------------------------
| Multiplies the 128-bit value formed by concatenating `a0' and `a1' to the
| 128-bit value formed by concatenating `b0' and `b1' to obtain a 256-bit
| product.  The product is broken into four 64-bit pieces which are stored at
| the locations pointed to by `z0Ptr', `z1Ptr', `z2Ptr', and `z3Ptr'.
*----------------------------------------------------------------------------*/

static inline void mul128To256(uint64_t a0, uint64_t a1,
                               uint64_t b0, uint64_t b1,
                               uint64_t *z0Ptr, uint64_t *z1Ptr,
                               uint64_t *z2Ptr, uint64_t *z3Ptr)
{
    uint64_t z0, z1, z2;
    uint64_t m0, m1, m2, n1, n2;

    mul64To128(a1, b0, &m1, &m2);
    mul64To128(a0, b1, &n1, &n2);
    mul64To128(a1, b1, &z2, z3Ptr);
    mul64To128(a0, b0, &z0, &z1);

    add192( 0, m1, m2,  0, n1, n2, &m0, &m1, &m2);
    add192(m0, m1, m2, z0, z1, z2, z0Ptr, z1Ptr, z2Ptr);
}

/*----------------------------------------------------------------------------
| Returns an approximation to the 64-bit integer quotient obtained by dividing
| `b' into the 128-bit value formed by concatenating `a0' and `a1'.  The
| divisor `b' must be at least 2^63.  If q is the exact quotient truncated
| toward zero, the approximation returned lies between q and q + 2 inclusive.
| If the exact quotient q is larger than 64 bits, the maximum positive 64-bit
| unsigned integer is returned.
*----------------------------------------------------------------------------*/

static inline uint64_t estimateDiv128To64(uint64_t a0, uint64_t a1, uint64_t b)
{
    uint64_t b0, b1;
    uint64_t rem0, rem1, term0, term1;
    uint64_t z;

    if ( b <= a0 ) return UINT64_C(0xFFFFFFFFFFFFFFFF);
    b0 = b>>32;
    z = ( b0<<32 <= a0 ) ? UINT64_C(0xFFFFFFFF00000000) : ( a0 / b0 )<<32;
    mul64To128( b, z, &term0, &term1 );
    sub128( a0, a1, term0, term1, &rem0, &rem1 );
    while ( ( (int64_t) rem0 ) < 0 ) {
        z -= UINT64_C(0x100000000);
        b1 = b<<32;
        add128( rem0, rem1, b0, b1, &rem0, &rem1 );
    }
    rem0 = ( rem0<<32 ) | ( rem1>>32 );
    z |= ( b0<<32 <= rem0 ) ? 0xFFFFFFFF : rem0 / b0;
    return z;

}

/*----------------------------------------------------------------------------
| Returns an approximation to the square root of the 32-bit significand given
| by `a'.  Considered as an integer, `a' must be at least 2^31.  If bit 0 of
| `aExp' (the least significant bit) is 1, the integer returned approximates
| 2^31*sqrt(`a'/2^31), where `a' is considered an integer.  If bit 0 of `aExp'
| is 0, the integer returned approximates 2^31*sqrt(`a'/2^30).  In either
| case, the approximation returned lies strictly within +/-2 of the exact
| value.
*----------------------------------------------------------------------------*/

static inline uint32_t estimateSqrt32(int aExp, uint32_t a)
{
    static const uint16_t sqrtOddAdjustments[] = {
        0x0004, 0x0022, 0x005D, 0x00B1, 0x011D, 0x019F, 0x0236, 0x02E0,
        0x039C, 0x0468, 0x0545, 0x0631, 0x072B, 0x0832, 0x0946, 0x0A67
    };
    static const uint16_t sqrtEvenAdjustments[] = {
        0x0A2D, 0x08AF, 0x075A, 0x0629, 0x051A, 0x0429, 0x0356, 0x029E,
        0x0200, 0x0179, 0x0109, 0x00AF, 0x0068, 0x0034, 0x0012, 0x0002
    };
    int8_t index;
    uint32_t z;

    index = ( a>>27 ) & 15;
    if ( aExp & 1 ) {
        z = 0x4000 + ( a>>17 ) - sqrtOddAdjustments[ (int)index ];
        z = ( ( a / z )<<14 ) + ( z<<15 );
        a >>= 1;
    }
    else {
        z = 0x8000 + ( a>>17 ) - sqrtEvenAdjustments[ (int)index ];
        z = a / z + z;
        z = ( 0x20000 <= z ) ? 0xFFFF8000 : ( z<<15 );
        if ( z <= a ) return (uint32_t) ( ( (int32_t) a )>>1 );
    }
    return ( (uint32_t) ( ( ( (uint64_t) a )<<31 ) / z ) ) + ( z>>1 );

}

/*----------------------------------------------------------------------------
| Returns 1 if the 128-bit value formed by concatenating `a0' and `a1'
| is equal to the 128-bit value formed by concatenating `b0' and `b1'.
| Otherwise, returns 0.
*----------------------------------------------------------------------------*/

static inline bool eq128(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
{
    return a0 == b0 && a1 == b1;
}

/*----------------------------------------------------------------------------
| Returns 1 if the 128-bit value formed by concatenating `a0' and `a1' is less
| than or equal to the 128-bit value formed by concatenating `b0' and `b1'.
| Otherwise, returns 0.
*----------------------------------------------------------------------------*/

static inline bool le128(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
{
    return a0 < b0 || (a0 == b0 && a1 <= b1);
}

/*----------------------------------------------------------------------------
| Returns 1 if the 128-bit value formed by concatenating `a0' and `a1' is less
| than the 128-bit value formed by concatenating `b0' and `b1'.  Otherwise,
| returns 0.
*----------------------------------------------------------------------------*/

static inline bool lt128(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
{
    return a0 < b0 || (a0 == b0 && a1 < b1);
}

/*----------------------------------------------------------------------------
| Returns 1 if the 128-bit value formed by concatenating `a0' and `a1' is
| not equal to the 128-bit value formed by concatenating `b0' and `b1'.
| Otherwise, returns 0.
*----------------------------------------------------------------------------*/

static inline bool ne128(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
{
    return a0 != b0 || a1 != b1;
}

/*
 * Similarly, comparisons of 192-bit values.
 */

static inline bool eq192(uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t b0, uint64_t b1, uint64_t b2)
{
    return ((a0 ^ b0) | (a1 ^ b1) | (a2 ^ b2)) == 0;
}

static inline bool le192(uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t b0, uint64_t b1, uint64_t b2)
{
    if (a0 != b0) {
        return a0 < b0;
    }
    if (a1 != b1) {
        return a1 < b1;
    }
    return a2 <= b2;
}

static inline bool lt192(uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t b0, uint64_t b1, uint64_t b2)
{
    if (a0 != b0) {
        return a0 < b0;
    }
    if (a1 != b1) {
        return a1 < b1;
    }
    return a2 < b2;
}

#endif
