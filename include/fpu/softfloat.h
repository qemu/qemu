/*
 * QEMU float support
 *
 * Derived from SoftFloat.
 */

/*============================================================================

This C header file is part of the SoftFloat IEC/IEEE Floating-point Arithmetic
Package, Release 2b.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://www.cs.berkeley.edu/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.

=============================================================================*/

#ifndef SOFTFLOAT_H
#define SOFTFLOAT_H

#if defined(CONFIG_SOLARIS) && defined(CONFIG_NEEDS_LIBSUNMATH)
#include <sunmath.h>
#endif

#include <inttypes.h>
#include "config-host.h"
#include "qemu/osdep.h"

/*----------------------------------------------------------------------------
| Each of the following `typedef's defines the most convenient type that holds
| integers of at least as many bits as specified.  For example, `uint8' should
| be the most convenient type that can hold unsigned integers of as many as
| 8 bits.  The `flag' type must be able to hold either a 0 or 1.  For most
| implementations of C, `flag', `uint8', and `int8' should all be `typedef'ed
| to the same as `int'.
*----------------------------------------------------------------------------*/
typedef uint8_t flag;
typedef uint8_t uint8;
typedef int8_t int8;
typedef unsigned int uint32;
typedef signed int int32;
typedef uint64_t uint64;
typedef int64_t int64;

#define LIT64( a ) a##LL

#define STATUS_PARAM , float_status *status
#define STATUS(field) status->field
#define STATUS_VAR , status

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point ordering relations
*----------------------------------------------------------------------------*/
enum {
    float_relation_less      = -1,
    float_relation_equal     =  0,
    float_relation_greater   =  1,
    float_relation_unordered =  2
};

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point types.
*----------------------------------------------------------------------------*/
/* Use structures for soft-float types.  This prevents accidentally mixing
   them with native int/float types.  A sufficiently clever compiler and
   sane ABI should be able to see though these structs.  However
   x86/gcc 3.x seems to struggle a bit, so leave them disabled by default.  */
//#define USE_SOFTFLOAT_STRUCT_TYPES
#ifdef USE_SOFTFLOAT_STRUCT_TYPES
typedef struct {
    uint16_t v;
} float16;
#define float16_val(x) (((float16)(x)).v)
#define make_float16(x) __extension__ ({ float16 f16_val = {x}; f16_val; })
#define const_float16(x) { x }
typedef struct {
    uint32_t v;
} float32;
/* The cast ensures an error if the wrong type is passed.  */
#define float32_val(x) (((float32)(x)).v)
#define make_float32(x) __extension__ ({ float32 f32_val = {x}; f32_val; })
#define const_float32(x) { x }
typedef struct {
    uint64_t v;
} float64;
#define float64_val(x) (((float64)(x)).v)
#define make_float64(x) __extension__ ({ float64 f64_val = {x}; f64_val; })
#define const_float64(x) { x }
#else
typedef uint16_t float16;
typedef uint32_t float32;
typedef uint64_t float64;
#define float16_val(x) (x)
#define float32_val(x) (x)
#define float64_val(x) (x)
#define make_float16(x) (x)
#define make_float32(x) (x)
#define make_float64(x) (x)
#define const_float16(x) (x)
#define const_float32(x) (x)
#define const_float64(x) (x)
#endif
typedef struct {
    uint64_t low;
    uint16_t high;
} floatx80;
#define make_floatx80(exp, mant) ((floatx80) { mant, exp })
#define make_floatx80_init(exp, mant) { .low = mant, .high = exp }
typedef struct {
#ifdef HOST_WORDS_BIGENDIAN
    uint64_t high, low;
#else
    uint64_t low, high;
#endif
} float128;
#define make_float128(high_, low_) ((float128) { .high = high_, .low = low_ })
#define make_float128_init(high_, low_) { .high = high_, .low = low_ }

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point underflow tininess-detection mode.
*----------------------------------------------------------------------------*/
enum {
    float_tininess_after_rounding  = 0,
    float_tininess_before_rounding = 1
};

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point rounding mode.
*----------------------------------------------------------------------------*/
enum {
    float_round_nearest_even = 0,
    float_round_down         = 1,
    float_round_up           = 2,
    float_round_to_zero      = 3,
    float_round_ties_away    = 4,
};

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point exception flags.
*----------------------------------------------------------------------------*/
enum {
    float_flag_invalid   =  1,
    float_flag_divbyzero =  4,
    float_flag_overflow  =  8,
    float_flag_underflow = 16,
    float_flag_inexact   = 32,
    float_flag_input_denormal = 64,
    float_flag_output_denormal = 128
};

typedef struct float_status {
    signed char float_detect_tininess;
    signed char float_rounding_mode;
    signed char float_exception_flags;
    signed char floatx80_rounding_precision;
    /* should denormalised results go to zero and set the inexact flag? */
    flag flush_to_zero;
    /* should denormalised inputs go to zero and set the input_denormal flag? */
    flag flush_inputs_to_zero;
    flag default_nan_mode;
} float_status;

static inline void set_float_detect_tininess(int val STATUS_PARAM)
{
    STATUS(float_detect_tininess) = val;
}
static inline void set_float_rounding_mode(int val STATUS_PARAM)
{
    STATUS(float_rounding_mode) = val;
}
static inline void set_float_exception_flags(int val STATUS_PARAM)
{
    STATUS(float_exception_flags) = val;
}
static inline void set_floatx80_rounding_precision(int val STATUS_PARAM)
{
    STATUS(floatx80_rounding_precision) = val;
}
static inline void set_flush_to_zero(flag val STATUS_PARAM)
{
    STATUS(flush_to_zero) = val;
}
static inline void set_flush_inputs_to_zero(flag val STATUS_PARAM)
{
    STATUS(flush_inputs_to_zero) = val;
}
static inline void set_default_nan_mode(flag val STATUS_PARAM)
{
    STATUS(default_nan_mode) = val;
}
static inline int get_float_detect_tininess(float_status *status)
{
    return STATUS(float_detect_tininess);
}
static inline int get_float_rounding_mode(float_status *status)
{
    return STATUS(float_rounding_mode);
}
static inline int get_float_exception_flags(float_status *status)
{
    return STATUS(float_exception_flags);
}
static inline int get_floatx80_rounding_precision(float_status *status)
{
    return STATUS(floatx80_rounding_precision);
}
static inline flag get_flush_to_zero(float_status *status)
{
    return STATUS(flush_to_zero);
}
static inline flag get_flush_inputs_to_zero(float_status *status)
{
    return STATUS(flush_inputs_to_zero);
}
static inline flag get_default_nan_mode(float_status *status)
{
    return STATUS(default_nan_mode);
}

/*----------------------------------------------------------------------------
| Routine to raise any or all of the software IEC/IEEE floating-point
| exception flags.
*----------------------------------------------------------------------------*/
void float_raise( int8 flags STATUS_PARAM);

/*----------------------------------------------------------------------------
| If `a' is denormal and we are in flush-to-zero mode then set the
| input-denormal exception and return zero. Otherwise just return the value.
*----------------------------------------------------------------------------*/
float32 float32_squash_input_denormal(float32 a STATUS_PARAM);
float64 float64_squash_input_denormal(float64 a STATUS_PARAM);

/*----------------------------------------------------------------------------
| Options to indicate which negations to perform in float*_muladd()
| Using these differs from negating an input or output before calling
| the muladd function in that this means that a NaN doesn't have its
| sign bit inverted before it is propagated.
| We also support halving the result before rounding, as a special
| case to support the ARM fused-sqrt-step instruction FRSQRTS.
*----------------------------------------------------------------------------*/
enum {
    float_muladd_negate_c = 1,
    float_muladd_negate_product = 2,
    float_muladd_negate_result = 4,
    float_muladd_halve_result = 8,
};

/*----------------------------------------------------------------------------
| Software IEC/IEEE integer-to-floating-point conversion routines.
*----------------------------------------------------------------------------*/
float32 int32_to_float32(int32_t STATUS_PARAM);
float64 int32_to_float64(int32_t STATUS_PARAM);
float32 uint32_to_float32(uint32_t STATUS_PARAM);
float64 uint32_to_float64(uint32_t STATUS_PARAM);
floatx80 int32_to_floatx80(int32_t STATUS_PARAM);
float128 int32_to_float128(int32_t STATUS_PARAM);
float32 int64_to_float32(int64_t STATUS_PARAM);
float32 uint64_to_float32(uint64_t STATUS_PARAM);
float64 int64_to_float64(int64_t STATUS_PARAM);
float64 uint64_to_float64(uint64_t STATUS_PARAM);
floatx80 int64_to_floatx80(int64_t STATUS_PARAM);
float128 int64_to_float128(int64_t STATUS_PARAM);
float128 uint64_to_float128(uint64_t STATUS_PARAM);

/* We provide the int16 versions for symmetry of API with float-to-int */
static inline float32 int16_to_float32(int16_t v STATUS_PARAM)
{
    return int32_to_float32(v STATUS_VAR);
}

static inline float32 uint16_to_float32(uint16_t v STATUS_PARAM)
{
    return uint32_to_float32(v STATUS_VAR);
}

static inline float64 int16_to_float64(int16_t v STATUS_PARAM)
{
    return int32_to_float64(v STATUS_VAR);
}

static inline float64 uint16_to_float64(uint16_t v STATUS_PARAM)
{
    return uint32_to_float64(v STATUS_VAR);
}

/*----------------------------------------------------------------------------
| Software half-precision conversion routines.
*----------------------------------------------------------------------------*/
float16 float32_to_float16( float32, flag STATUS_PARAM );
float32 float16_to_float32( float16, flag STATUS_PARAM );
float16 float64_to_float16(float64 a, flag ieee STATUS_PARAM);
float64 float16_to_float64(float16 a, flag ieee STATUS_PARAM);

/*----------------------------------------------------------------------------
| Software half-precision operations.
*----------------------------------------------------------------------------*/
int float16_is_quiet_nan( float16 );
int float16_is_signaling_nan( float16 );
float16 float16_maybe_silence_nan( float16 );

static inline int float16_is_any_nan(float16 a)
{
    return ((float16_val(a) & ~0x8000) > 0x7c00);
}

/*----------------------------------------------------------------------------
| The pattern for a default generated half-precision NaN.
*----------------------------------------------------------------------------*/
extern const float16 float16_default_nan;

/*----------------------------------------------------------------------------
| Software IEC/IEEE single-precision conversion routines.
*----------------------------------------------------------------------------*/
int_fast16_t float32_to_int16(float32 STATUS_PARAM);
uint_fast16_t float32_to_uint16(float32 STATUS_PARAM);
int_fast16_t float32_to_int16_round_to_zero(float32 STATUS_PARAM);
uint_fast16_t float32_to_uint16_round_to_zero(float32 STATUS_PARAM);
int32 float32_to_int32( float32 STATUS_PARAM );
int32 float32_to_int32_round_to_zero( float32 STATUS_PARAM );
uint32 float32_to_uint32( float32 STATUS_PARAM );
uint32 float32_to_uint32_round_to_zero( float32 STATUS_PARAM );
int64 float32_to_int64( float32 STATUS_PARAM );
uint64 float32_to_uint64(float32 STATUS_PARAM);
uint64 float32_to_uint64_round_to_zero(float32 STATUS_PARAM);
int64 float32_to_int64_round_to_zero( float32 STATUS_PARAM );
float64 float32_to_float64( float32 STATUS_PARAM );
floatx80 float32_to_floatx80( float32 STATUS_PARAM );
float128 float32_to_float128( float32 STATUS_PARAM );

/*----------------------------------------------------------------------------
| Software IEC/IEEE single-precision operations.
*----------------------------------------------------------------------------*/
float32 float32_round_to_int( float32 STATUS_PARAM );
float32 float32_add( float32, float32 STATUS_PARAM );
float32 float32_sub( float32, float32 STATUS_PARAM );
float32 float32_mul( float32, float32 STATUS_PARAM );
float32 float32_div( float32, float32 STATUS_PARAM );
float32 float32_rem( float32, float32 STATUS_PARAM );
float32 float32_muladd(float32, float32, float32, int STATUS_PARAM);
float32 float32_sqrt( float32 STATUS_PARAM );
float32 float32_exp2( float32 STATUS_PARAM );
float32 float32_log2( float32 STATUS_PARAM );
int float32_eq( float32, float32 STATUS_PARAM );
int float32_le( float32, float32 STATUS_PARAM );
int float32_lt( float32, float32 STATUS_PARAM );
int float32_unordered( float32, float32 STATUS_PARAM );
int float32_eq_quiet( float32, float32 STATUS_PARAM );
int float32_le_quiet( float32, float32 STATUS_PARAM );
int float32_lt_quiet( float32, float32 STATUS_PARAM );
int float32_unordered_quiet( float32, float32 STATUS_PARAM );
int float32_compare( float32, float32 STATUS_PARAM );
int float32_compare_quiet( float32, float32 STATUS_PARAM );
float32 float32_min(float32, float32 STATUS_PARAM);
float32 float32_max(float32, float32 STATUS_PARAM);
float32 float32_minnum(float32, float32 STATUS_PARAM);
float32 float32_maxnum(float32, float32 STATUS_PARAM);
int float32_is_quiet_nan( float32 );
int float32_is_signaling_nan( float32 );
float32 float32_maybe_silence_nan( float32 );
float32 float32_scalbn( float32, int STATUS_PARAM );

static inline float32 float32_abs(float32 a)
{
    /* Note that abs does *not* handle NaN specially, nor does
     * it flush denormal inputs to zero.
     */
    return make_float32(float32_val(a) & 0x7fffffff);
}

static inline float32 float32_chs(float32 a)
{
    /* Note that chs does *not* handle NaN specially, nor does
     * it flush denormal inputs to zero.
     */
    return make_float32(float32_val(a) ^ 0x80000000);
}

static inline int float32_is_infinity(float32 a)
{
    return (float32_val(a) & 0x7fffffff) == 0x7f800000;
}

static inline int float32_is_neg(float32 a)
{
    return float32_val(a) >> 31;
}

static inline int float32_is_zero(float32 a)
{
    return (float32_val(a) & 0x7fffffff) == 0;
}

static inline int float32_is_any_nan(float32 a)
{
    return ((float32_val(a) & ~(1 << 31)) > 0x7f800000UL);
}

static inline int float32_is_zero_or_denormal(float32 a)
{
    return (float32_val(a) & 0x7f800000) == 0;
}

static inline float32 float32_set_sign(float32 a, int sign)
{
    return make_float32((float32_val(a) & 0x7fffffff) | (sign << 31));
}

#define float32_zero make_float32(0)
#define float32_one make_float32(0x3f800000)
#define float32_ln2 make_float32(0x3f317218)
#define float32_pi make_float32(0x40490fdb)
#define float32_half make_float32(0x3f000000)
#define float32_infinity make_float32(0x7f800000)


/*----------------------------------------------------------------------------
| The pattern for a default generated single-precision NaN.
*----------------------------------------------------------------------------*/
extern const float32 float32_default_nan;

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision conversion routines.
*----------------------------------------------------------------------------*/
int_fast16_t float64_to_int16(float64 STATUS_PARAM);
uint_fast16_t float64_to_uint16(float64 STATUS_PARAM);
int_fast16_t float64_to_int16_round_to_zero(float64 STATUS_PARAM);
uint_fast16_t float64_to_uint16_round_to_zero(float64 STATUS_PARAM);
int32 float64_to_int32( float64 STATUS_PARAM );
int32 float64_to_int32_round_to_zero( float64 STATUS_PARAM );
uint32 float64_to_uint32( float64 STATUS_PARAM );
uint32 float64_to_uint32_round_to_zero( float64 STATUS_PARAM );
int64 float64_to_int64( float64 STATUS_PARAM );
int64 float64_to_int64_round_to_zero( float64 STATUS_PARAM );
uint64 float64_to_uint64 (float64 a STATUS_PARAM);
uint64 float64_to_uint64_round_to_zero (float64 a STATUS_PARAM);
float32 float64_to_float32( float64 STATUS_PARAM );
floatx80 float64_to_floatx80( float64 STATUS_PARAM );
float128 float64_to_float128( float64 STATUS_PARAM );

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision operations.
*----------------------------------------------------------------------------*/
float64 float64_round_to_int( float64 STATUS_PARAM );
float64 float64_trunc_to_int( float64 STATUS_PARAM );
float64 float64_add( float64, float64 STATUS_PARAM );
float64 float64_sub( float64, float64 STATUS_PARAM );
float64 float64_mul( float64, float64 STATUS_PARAM );
float64 float64_div( float64, float64 STATUS_PARAM );
float64 float64_rem( float64, float64 STATUS_PARAM );
float64 float64_muladd(float64, float64, float64, int STATUS_PARAM);
float64 float64_sqrt( float64 STATUS_PARAM );
float64 float64_log2( float64 STATUS_PARAM );
int float64_eq( float64, float64 STATUS_PARAM );
int float64_le( float64, float64 STATUS_PARAM );
int float64_lt( float64, float64 STATUS_PARAM );
int float64_unordered( float64, float64 STATUS_PARAM );
int float64_eq_quiet( float64, float64 STATUS_PARAM );
int float64_le_quiet( float64, float64 STATUS_PARAM );
int float64_lt_quiet( float64, float64 STATUS_PARAM );
int float64_unordered_quiet( float64, float64 STATUS_PARAM );
int float64_compare( float64, float64 STATUS_PARAM );
int float64_compare_quiet( float64, float64 STATUS_PARAM );
float64 float64_min(float64, float64 STATUS_PARAM);
float64 float64_max(float64, float64 STATUS_PARAM);
float64 float64_minnum(float64, float64 STATUS_PARAM);
float64 float64_maxnum(float64, float64 STATUS_PARAM);
int float64_is_quiet_nan( float64 a );
int float64_is_signaling_nan( float64 );
float64 float64_maybe_silence_nan( float64 );
float64 float64_scalbn( float64, int STATUS_PARAM );

static inline float64 float64_abs(float64 a)
{
    /* Note that abs does *not* handle NaN specially, nor does
     * it flush denormal inputs to zero.
     */
    return make_float64(float64_val(a) & 0x7fffffffffffffffLL);
}

static inline float64 float64_chs(float64 a)
{
    /* Note that chs does *not* handle NaN specially, nor does
     * it flush denormal inputs to zero.
     */
    return make_float64(float64_val(a) ^ 0x8000000000000000LL);
}

static inline int float64_is_infinity(float64 a)
{
    return (float64_val(a) & 0x7fffffffffffffffLL ) == 0x7ff0000000000000LL;
}

static inline int float64_is_neg(float64 a)
{
    return float64_val(a) >> 63;
}

static inline int float64_is_zero(float64 a)
{
    return (float64_val(a) & 0x7fffffffffffffffLL) == 0;
}

static inline int float64_is_any_nan(float64 a)
{
    return ((float64_val(a) & ~(1ULL << 63)) > 0x7ff0000000000000ULL);
}

static inline int float64_is_zero_or_denormal(float64 a)
{
    return (float64_val(a) & 0x7ff0000000000000LL) == 0;
}

static inline float64 float64_set_sign(float64 a, int sign)
{
    return make_float64((float64_val(a) & 0x7fffffffffffffffULL)
                        | ((int64_t)sign << 63));
}

#define float64_zero make_float64(0)
#define float64_one make_float64(0x3ff0000000000000LL)
#define float64_ln2 make_float64(0x3fe62e42fefa39efLL)
#define float64_pi make_float64(0x400921fb54442d18LL)
#define float64_half make_float64(0x3fe0000000000000LL)
#define float64_infinity make_float64(0x7ff0000000000000LL)

/*----------------------------------------------------------------------------
| The pattern for a default generated double-precision NaN.
*----------------------------------------------------------------------------*/
extern const float64 float64_default_nan;

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision conversion routines.
*----------------------------------------------------------------------------*/
int32 floatx80_to_int32( floatx80 STATUS_PARAM );
int32 floatx80_to_int32_round_to_zero( floatx80 STATUS_PARAM );
int64 floatx80_to_int64( floatx80 STATUS_PARAM );
int64 floatx80_to_int64_round_to_zero( floatx80 STATUS_PARAM );
float32 floatx80_to_float32( floatx80 STATUS_PARAM );
float64 floatx80_to_float64( floatx80 STATUS_PARAM );
float128 floatx80_to_float128( floatx80 STATUS_PARAM );

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision operations.
*----------------------------------------------------------------------------*/
floatx80 floatx80_round_to_int( floatx80 STATUS_PARAM );
floatx80 floatx80_add( floatx80, floatx80 STATUS_PARAM );
floatx80 floatx80_sub( floatx80, floatx80 STATUS_PARAM );
floatx80 floatx80_mul( floatx80, floatx80 STATUS_PARAM );
floatx80 floatx80_div( floatx80, floatx80 STATUS_PARAM );
floatx80 floatx80_rem( floatx80, floatx80 STATUS_PARAM );
floatx80 floatx80_sqrt( floatx80 STATUS_PARAM );
int floatx80_eq( floatx80, floatx80 STATUS_PARAM );
int floatx80_le( floatx80, floatx80 STATUS_PARAM );
int floatx80_lt( floatx80, floatx80 STATUS_PARAM );
int floatx80_unordered( floatx80, floatx80 STATUS_PARAM );
int floatx80_eq_quiet( floatx80, floatx80 STATUS_PARAM );
int floatx80_le_quiet( floatx80, floatx80 STATUS_PARAM );
int floatx80_lt_quiet( floatx80, floatx80 STATUS_PARAM );
int floatx80_unordered_quiet( floatx80, floatx80 STATUS_PARAM );
int floatx80_compare( floatx80, floatx80 STATUS_PARAM );
int floatx80_compare_quiet( floatx80, floatx80 STATUS_PARAM );
int floatx80_is_quiet_nan( floatx80 );
int floatx80_is_signaling_nan( floatx80 );
floatx80 floatx80_maybe_silence_nan( floatx80 );
floatx80 floatx80_scalbn( floatx80, int STATUS_PARAM );

static inline floatx80 floatx80_abs(floatx80 a)
{
    a.high &= 0x7fff;
    return a;
}

static inline floatx80 floatx80_chs(floatx80 a)
{
    a.high ^= 0x8000;
    return a;
}

static inline int floatx80_is_infinity(floatx80 a)
{
    return (a.high & 0x7fff) == 0x7fff && a.low == 0x8000000000000000LL;
}

static inline int floatx80_is_neg(floatx80 a)
{
    return a.high >> 15;
}

static inline int floatx80_is_zero(floatx80 a)
{
    return (a.high & 0x7fff) == 0 && a.low == 0;
}

static inline int floatx80_is_zero_or_denormal(floatx80 a)
{
    return (a.high & 0x7fff) == 0;
}

static inline int floatx80_is_any_nan(floatx80 a)
{
    return ((a.high & 0x7fff) == 0x7fff) && (a.low<<1);
}

#define floatx80_zero make_floatx80(0x0000, 0x0000000000000000LL)
#define floatx80_one make_floatx80(0x3fff, 0x8000000000000000LL)
#define floatx80_ln2 make_floatx80(0x3ffe, 0xb17217f7d1cf79acLL)
#define floatx80_pi make_floatx80(0x4000, 0xc90fdaa22168c235LL)
#define floatx80_half make_floatx80(0x3ffe, 0x8000000000000000LL)
#define floatx80_infinity make_floatx80(0x7fff, 0x8000000000000000LL)

/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
extern const floatx80 floatx80_default_nan;

/*----------------------------------------------------------------------------
| Software IEC/IEEE quadruple-precision conversion routines.
*----------------------------------------------------------------------------*/
int32 float128_to_int32( float128 STATUS_PARAM );
int32 float128_to_int32_round_to_zero( float128 STATUS_PARAM );
int64 float128_to_int64( float128 STATUS_PARAM );
int64 float128_to_int64_round_to_zero( float128 STATUS_PARAM );
float32 float128_to_float32( float128 STATUS_PARAM );
float64 float128_to_float64( float128 STATUS_PARAM );
floatx80 float128_to_floatx80( float128 STATUS_PARAM );

/*----------------------------------------------------------------------------
| Software IEC/IEEE quadruple-precision operations.
*----------------------------------------------------------------------------*/
float128 float128_round_to_int( float128 STATUS_PARAM );
float128 float128_add( float128, float128 STATUS_PARAM );
float128 float128_sub( float128, float128 STATUS_PARAM );
float128 float128_mul( float128, float128 STATUS_PARAM );
float128 float128_div( float128, float128 STATUS_PARAM );
float128 float128_rem( float128, float128 STATUS_PARAM );
float128 float128_sqrt( float128 STATUS_PARAM );
int float128_eq( float128, float128 STATUS_PARAM );
int float128_le( float128, float128 STATUS_PARAM );
int float128_lt( float128, float128 STATUS_PARAM );
int float128_unordered( float128, float128 STATUS_PARAM );
int float128_eq_quiet( float128, float128 STATUS_PARAM );
int float128_le_quiet( float128, float128 STATUS_PARAM );
int float128_lt_quiet( float128, float128 STATUS_PARAM );
int float128_unordered_quiet( float128, float128 STATUS_PARAM );
int float128_compare( float128, float128 STATUS_PARAM );
int float128_compare_quiet( float128, float128 STATUS_PARAM );
int float128_is_quiet_nan( float128 );
int float128_is_signaling_nan( float128 );
float128 float128_maybe_silence_nan( float128 );
float128 float128_scalbn( float128, int STATUS_PARAM );

static inline float128 float128_abs(float128 a)
{
    a.high &= 0x7fffffffffffffffLL;
    return a;
}

static inline float128 float128_chs(float128 a)
{
    a.high ^= 0x8000000000000000LL;
    return a;
}

static inline int float128_is_infinity(float128 a)
{
    return (a.high & 0x7fffffffffffffffLL) == 0x7fff000000000000LL && a.low == 0;
}

static inline int float128_is_neg(float128 a)
{
    return a.high >> 63;
}

static inline int float128_is_zero(float128 a)
{
    return (a.high & 0x7fffffffffffffffLL) == 0 && a.low == 0;
}

static inline int float128_is_zero_or_denormal(float128 a)
{
    return (a.high & 0x7fff000000000000LL) == 0;
}

static inline int float128_is_any_nan(float128 a)
{
    return ((a.high >> 48) & 0x7fff) == 0x7fff &&
        ((a.low != 0) || ((a.high & 0xffffffffffffLL) != 0));
}

#define float128_zero make_float128(0, 0)

/*----------------------------------------------------------------------------
| The pattern for a default generated quadruple-precision NaN.
*----------------------------------------------------------------------------*/
extern const float128 float128_default_nan;

#endif /* !SOFTFLOAT_H */
