
/*============================================================================

This C source fragment is part of the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b.

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

#if defined(TARGET_MIPS) || defined(TARGET_SH4)
#define SNAN_BIT_IS_ONE		1
#else
#define SNAN_BIT_IS_ONE		0
#endif

/*----------------------------------------------------------------------------
| Raises the exceptions specified by `flags'.  Floating-point traps can be
| defined here if desired.  It is currently not possible for such a trap
| to substitute a result value.  If traps are not implemented, this routine
| should be simply `float_exception_flags |= flags;'.
*----------------------------------------------------------------------------*/

void float_raise( int8 flags STATUS_PARAM )
{
    STATUS(float_exception_flags) |= flags;
}

/*----------------------------------------------------------------------------
| Internal canonical NaN format.
*----------------------------------------------------------------------------*/
typedef struct {
    flag sign;
    bits64 high, low;
} commonNaNT;

/*----------------------------------------------------------------------------
| The pattern for a default generated single-precision NaN.
*----------------------------------------------------------------------------*/
#if defined(TARGET_SPARC)
#define float32_default_nan make_float32(0x7FFFFFFF)
#elif defined(TARGET_PPC) || defined(TARGET_ARM) || defined(TARGET_ALPHA)
#define float32_default_nan make_float32(0x7FC00000)
#elif SNAN_BIT_IS_ONE
#define float32_default_nan make_float32(0x7FBFFFFF)
#else
#define float32_default_nan make_float32(0xFFC00000)
#endif

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float32_is_quiet_nan( float32 a_ )
{
    uint32_t a = float32_val(a_);
#if SNAN_BIT_IS_ONE
    return ( ( ( a>>22 ) & 0x1FF ) == 0x1FE ) && ( a & 0x003FFFFF );
#else
    return ( 0xFF800000 <= (bits32) ( a<<1 ) );
#endif
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float32_is_signaling_nan( float32 a_ )
{
    uint32_t a = float32_val(a_);
#if SNAN_BIT_IS_ONE
    return ( 0xFF800000 <= (bits32) ( a<<1 ) );
#else
    return ( ( ( a>>22 ) & 0x1FF ) == 0x1FE ) && ( a & 0x003FFFFF );
#endif
}

/*----------------------------------------------------------------------------
| Returns a quiet NaN if the single-precision floating point value `a' is a
| signaling NaN; otherwise returns `a'.
*----------------------------------------------------------------------------*/

float32 float32_maybe_silence_nan( float32 a_ )
{
    if (float32_is_signaling_nan(a_)) {
#if SNAN_BIT_IS_ONE
#  if defined(TARGET_MIPS) || defined(TARGET_SH4)
        return float32_default_nan;
#  else
#    error Rules for silencing a signaling NaN are target-specific
#  endif
#else
        bits32 a = float32_val(a_);
        a |= (1 << 22);
        return make_float32(a);
#endif
    }
    return a_;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point NaN
| `a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT float32ToCommonNaN( float32 a STATUS_PARAM )
{
    commonNaNT z;

    if ( float32_is_signaling_nan( a ) ) float_raise( float_flag_invalid STATUS_VAR );
    z.sign = float32_val(a)>>31;
    z.low = 0;
    z.high = ( (bits64) float32_val(a) )<<41;
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the single-
| precision floating-point format.
*----------------------------------------------------------------------------*/

static float32 commonNaNToFloat32( commonNaNT a )
{
    bits32 mantissa = a.high>>41;
    if ( mantissa )
        return make_float32(
            ( ( (bits32) a.sign )<<31 ) | 0x7F800000 | ( a.high>>41 ) );
    else
        return float32_default_nan;
}

/*----------------------------------------------------------------------------
| Select which NaN to propagate for a two-input operation.
| IEEE754 doesn't specify all the details of this, so the
| algorithm is target-specific.
| The routine is passed various bits of information about the
| two NaNs and should return 0 to select NaN a and 1 for NaN b.
| Note that signalling NaNs are always squashed to quiet NaNs
| by the caller, by calling floatXX_maybe_silence_nan() before
| returning them.
|
| aIsLargerSignificand is only valid if both a and b are NaNs
| of some kind, and is true if a has the larger significand,
| or if both a and b have the same significand but a is
| positive but b is negative. It is only needed for the x87
| tie-break rule.
*----------------------------------------------------------------------------*/

#if defined(TARGET_ARM)
static int pickNaN(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                    flag aIsLargerSignificand)
{
    /* ARM mandated NaN propagation rules: take the first of:
     *  1. A if it is signaling
     *  2. B if it is signaling
     *  3. A (quiet)
     *  4. B (quiet)
     * A signaling NaN is always quietened before returning it.
     */
    if (aIsSNaN) {
        return 0;
    } else if (bIsSNaN) {
        return 1;
    } else if (aIsQNaN) {
        return 0;
    } else {
        return 1;
    }
}
#elif defined(TARGET_MIPS)
static int pickNaN(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                    flag aIsLargerSignificand)
{
    /* According to MIPS specifications, if one of the two operands is
     * a sNaN, a new qNaN has to be generated. This is done in
     * floatXX_maybe_silence_nan(). For qNaN inputs the specifications
     * says: "When possible, this QNaN result is one of the operand QNaN
     * values." In practice it seems that most implementations choose
     * the first operand if both operands are qNaN. In short this gives
     * the following rules:
     *  1. A if it is signaling
     *  2. B if it is signaling
     *  3. A (quiet)
     *  4. B (quiet)
     * A signaling NaN is always silenced before returning it.
     */
    if (aIsSNaN) {
        return 0;
    } else if (bIsSNaN) {
        return 1;
    } else if (aIsQNaN) {
        return 0;
    } else {
        return 1;
    }
}
#elif defined(TARGET_PPC)
static int pickNaN(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                   flag aIsLargerSignificand)
{
    /* PowerPC propagation rules:
     *  1. A if it sNaN or qNaN
     *  2. B if it sNaN or qNaN
     * A signaling NaN is always silenced before returning it.
     */
    if (aIsSNaN || aIsQNaN) {
        return 0;
    } else {
        return 1;
    }
}
#else
static int pickNaN(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                    flag aIsLargerSignificand)
{
    /* This implements x87 NaN propagation rules:
     * SNaN + QNaN => return the QNaN
     * two SNaNs => return the one with the larger significand, silenced
     * two QNaNs => return the one with the larger significand
     * SNaN and a non-NaN => return the SNaN, silenced
     * QNaN and a non-NaN => return the QNaN
     *
     * If we get down to comparing significands and they are the same,
     * return the NaN with the positive sign bit (if any).
     */
    if (aIsSNaN) {
        if (bIsSNaN) {
            return aIsLargerSignificand ? 0 : 1;
        }
        return bIsQNaN ? 1 : 0;
    }
    else if (aIsQNaN) {
        if (bIsSNaN || !bIsQNaN)
            return 0;
        else {
            return aIsLargerSignificand ? 0 : 1;
        }
    } else {
        return 1;
    }
}
#endif

/*----------------------------------------------------------------------------
| Takes two single-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static float32 propagateFloat32NaN( float32 a, float32 b STATUS_PARAM)
{
    flag aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN;
    flag aIsLargerSignificand;
    bits32 av, bv;

    aIsQuietNaN = float32_is_quiet_nan( a );
    aIsSignalingNaN = float32_is_signaling_nan( a );
    bIsQuietNaN = float32_is_quiet_nan( b );
    bIsSignalingNaN = float32_is_signaling_nan( b );
    av = float32_val(a);
    bv = float32_val(b);

    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid STATUS_VAR);

    if ( STATUS(default_nan_mode) )
        return float32_default_nan;

    if ((bits32)(av<<1) < (bits32)(bv<<1)) {
        aIsLargerSignificand = 0;
    } else if ((bits32)(bv<<1) < (bits32)(av<<1)) {
        aIsLargerSignificand = 1;
    } else {
        aIsLargerSignificand = (av < bv) ? 1 : 0;
    }

    if (pickNaN(aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN,
                aIsLargerSignificand)) {
        return float32_maybe_silence_nan(b);
    } else {
        return float32_maybe_silence_nan(a);
    }
}

/*----------------------------------------------------------------------------
| The pattern for a default generated double-precision NaN.
*----------------------------------------------------------------------------*/
#if defined(TARGET_SPARC)
#define float64_default_nan make_float64(LIT64( 0x7FFFFFFFFFFFFFFF ))
#elif defined(TARGET_PPC) || defined(TARGET_ARM) || defined(TARGET_ALPHA)
#define float64_default_nan make_float64(LIT64( 0x7FF8000000000000 ))
#elif SNAN_BIT_IS_ONE
#define float64_default_nan make_float64(LIT64( 0x7FF7FFFFFFFFFFFF ))
#else
#define float64_default_nan make_float64(LIT64( 0xFFF8000000000000 ))
#endif

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float64_is_quiet_nan( float64 a_ )
{
    bits64 a = float64_val(a_);
#if SNAN_BIT_IS_ONE
    return
           ( ( ( a>>51 ) & 0xFFF ) == 0xFFE )
        && ( a & LIT64( 0x0007FFFFFFFFFFFF ) );
#else
    return ( LIT64( 0xFFF0000000000000 ) <= (bits64) ( a<<1 ) );
#endif
}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float64_is_signaling_nan( float64 a_ )
{
    bits64 a = float64_val(a_);
#if SNAN_BIT_IS_ONE
    return ( LIT64( 0xFFF0000000000000 ) <= (bits64) ( a<<1 ) );
#else
    return
           ( ( ( a>>51 ) & 0xFFF ) == 0xFFE )
        && ( a & LIT64( 0x0007FFFFFFFFFFFF ) );
#endif
}

/*----------------------------------------------------------------------------
| Returns a quiet NaN if the double-precision floating point value `a' is a
| signaling NaN; otherwise returns `a'.
*----------------------------------------------------------------------------*/

float64 float64_maybe_silence_nan( float64 a_ )
{
    if (float64_is_signaling_nan(a_)) {
#if SNAN_BIT_IS_ONE
#  if defined(TARGET_MIPS) || defined(TARGET_SH4)
        return float64_default_nan;
#  else
#    error Rules for silencing a signaling NaN are target-specific
#  endif
#else
        bits64 a = float64_val(a_);
        a |= LIT64( 0x0008000000000000 );
        return make_float64(a);
#endif
    }
    return a_;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point NaN
| `a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT float64ToCommonNaN( float64 a STATUS_PARAM)
{
    commonNaNT z;

    if ( float64_is_signaling_nan( a ) ) float_raise( float_flag_invalid STATUS_VAR);
    z.sign = float64_val(a)>>63;
    z.low = 0;
    z.high = float64_val(a)<<12;
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the double-
| precision floating-point format.
*----------------------------------------------------------------------------*/

static float64 commonNaNToFloat64( commonNaNT a )
{
    bits64 mantissa = a.high>>12;

    if ( mantissa )
        return make_float64(
              ( ( (bits64) a.sign )<<63 )
            | LIT64( 0x7FF0000000000000 )
            | ( a.high>>12 ));
    else
        return float64_default_nan;
}

/*----------------------------------------------------------------------------
| Takes two double-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static float64 propagateFloat64NaN( float64 a, float64 b STATUS_PARAM)
{
    flag aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN;
    flag aIsLargerSignificand;
    bits64 av, bv;

    aIsQuietNaN = float64_is_quiet_nan( a );
    aIsSignalingNaN = float64_is_signaling_nan( a );
    bIsQuietNaN = float64_is_quiet_nan( b );
    bIsSignalingNaN = float64_is_signaling_nan( b );
    av = float64_val(a);
    bv = float64_val(b);

    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid STATUS_VAR);

    if ( STATUS(default_nan_mode) )
        return float64_default_nan;

    if ((bits64)(av<<1) < (bits64)(bv<<1)) {
        aIsLargerSignificand = 0;
    } else if ((bits64)(bv<<1) < (bits64)(av<<1)) {
        aIsLargerSignificand = 1;
    } else {
        aIsLargerSignificand = (av < bv) ? 1 : 0;
    }

    if (pickNaN(aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN,
                aIsLargerSignificand)) {
        return float64_maybe_silence_nan(b);
    } else {
        return float64_maybe_silence_nan(a);
    }
}

#ifdef FLOATX80

/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.  The
| `high' and `low' values hold the most- and least-significant bits,
| respectively.
*----------------------------------------------------------------------------*/
#if SNAN_BIT_IS_ONE
#define floatx80_default_nan_high 0x7FFF
#define floatx80_default_nan_low  LIT64( 0xBFFFFFFFFFFFFFFF )
#else
#define floatx80_default_nan_high 0xFFFF
#define floatx80_default_nan_low  LIT64( 0xC000000000000000 )
#endif

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is a
| quiet NaN; otherwise returns 0. This slightly differs from the same
| function for other types as floatx80 has an explicit bit.
*----------------------------------------------------------------------------*/

int floatx80_is_quiet_nan( floatx80 a )
{
#if SNAN_BIT_IS_ONE
    bits64 aLow;

    aLow = a.low & ~ LIT64( 0x4000000000000000 );
    return
           ( ( a.high & 0x7FFF ) == 0x7FFF )
        && (bits64) ( aLow<<1 )
        && ( a.low == aLow );
#else
    return ( ( a.high & 0x7FFF ) == 0x7FFF )
        && (LIT64( 0x8000000000000000 ) <= ((bits64) ( a.low<<1 )));
#endif
}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is a
| signaling NaN; otherwise returns 0. This slightly differs from the same
| function for other types as floatx80 has an explicit bit.
*----------------------------------------------------------------------------*/

int floatx80_is_signaling_nan( floatx80 a )
{
#if SNAN_BIT_IS_ONE
    return ( ( a.high & 0x7FFF ) == 0x7FFF )
        && (LIT64( 0x8000000000000000 ) <= ((bits64) ( a.low<<1 )));
#else
    bits64 aLow;

    aLow = a.low & ~ LIT64( 0x4000000000000000 );
    return
           ( ( a.high & 0x7FFF ) == 0x7FFF )
        && (bits64) ( aLow<<1 )
        && ( a.low == aLow );
#endif
}

/*----------------------------------------------------------------------------
| Returns a quiet NaN if the extended double-precision floating point value
| `a' is a signaling NaN; otherwise returns `a'.
*----------------------------------------------------------------------------*/

floatx80 floatx80_maybe_silence_nan( floatx80 a )
{
    if (floatx80_is_signaling_nan(a)) {
#if SNAN_BIT_IS_ONE
#  if defined(TARGET_MIPS) || defined(TARGET_SH4)
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
#  else
#    error Rules for silencing a signaling NaN are target-specific
#  endif
#else
        a.low |= LIT64( 0xC000000000000000 );
        return a;
#endif
    }
    return a;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point NaN `a' to the canonical NaN format.  If `a' is a signaling NaN, the
| invalid exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT floatx80ToCommonNaN( floatx80 a STATUS_PARAM)
{
    commonNaNT z;

    if ( floatx80_is_signaling_nan( a ) ) float_raise( float_flag_invalid STATUS_VAR);
    z.sign = a.high>>15;
    z.low = 0;
    z.high = a.low;
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the extended
| double-precision floating-point format.
*----------------------------------------------------------------------------*/

static floatx80 commonNaNToFloatx80( commonNaNT a )
{
    floatx80 z;

    if (a.high)
        z.low = a.high;
    else
        z.low = floatx80_default_nan_low;
    z.high = ( ( (bits16) a.sign )<<15 ) | 0x7FFF;
    return z;
}

/*----------------------------------------------------------------------------
| Takes two extended double-precision floating-point values `a' and `b', one
| of which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static floatx80 propagateFloatx80NaN( floatx80 a, floatx80 b STATUS_PARAM)
{
    flag aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN;
    flag aIsLargerSignificand;

    aIsQuietNaN = floatx80_is_quiet_nan( a );
    aIsSignalingNaN = floatx80_is_signaling_nan( a );
    bIsQuietNaN = floatx80_is_quiet_nan( b );
    bIsSignalingNaN = floatx80_is_signaling_nan( b );

    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid STATUS_VAR);

    if ( STATUS(default_nan_mode) ) {
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }

    if (a.low < b.low) {
        aIsLargerSignificand = 0;
    } else if (b.low < a.low) {
        aIsLargerSignificand = 1;
    } else {
        aIsLargerSignificand = (a.high < b.high) ? 1 : 0;
    }

    if (pickNaN(aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN,
                aIsLargerSignificand)) {
        return floatx80_maybe_silence_nan(b);
    } else {
        return floatx80_maybe_silence_nan(a);
    }
}

#endif

#ifdef FLOAT128

/*----------------------------------------------------------------------------
| The pattern for a default generated quadruple-precision NaN.  The `high' and
| `low' values hold the most- and least-significant bits, respectively.
*----------------------------------------------------------------------------*/
#if SNAN_BIT_IS_ONE
#define float128_default_nan_high LIT64( 0x7FFF7FFFFFFFFFFF )
#define float128_default_nan_low  LIT64( 0xFFFFFFFFFFFFFFFF )
#else
#define float128_default_nan_high LIT64( 0xFFFF800000000000 )
#define float128_default_nan_low  LIT64( 0x0000000000000000 )
#endif

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float128_is_quiet_nan( float128 a )
{
#if SNAN_BIT_IS_ONE
    return
           ( ( ( a.high>>47 ) & 0xFFFF ) == 0xFFFE )
        && ( a.low || ( a.high & LIT64( 0x00007FFFFFFFFFFF ) ) );
#else
    return
           ( LIT64( 0xFFFE000000000000 ) <= (bits64) ( a.high<<1 ) )
        && ( a.low || ( a.high & LIT64( 0x0000FFFFFFFFFFFF ) ) );
#endif
}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is a
| signaling NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float128_is_signaling_nan( float128 a )
{
#if SNAN_BIT_IS_ONE
    return
           ( LIT64( 0xFFFE000000000000 ) <= (bits64) ( a.high<<1 ) )
        && ( a.low || ( a.high & LIT64( 0x0000FFFFFFFFFFFF ) ) );
#else
    return
           ( ( ( a.high>>47 ) & 0xFFFF ) == 0xFFFE )
        && ( a.low || ( a.high & LIT64( 0x00007FFFFFFFFFFF ) ) );
#endif
}

/*----------------------------------------------------------------------------
| Returns a quiet NaN if the quadruple-precision floating point value `a' is
| a signaling NaN; otherwise returns `a'.
*----------------------------------------------------------------------------*/

float128 float128_maybe_silence_nan( float128 a )
{
    if (float128_is_signaling_nan(a)) {
#if SNAN_BIT_IS_ONE
#  if defined(TARGET_MIPS) || defined(TARGET_SH4)
        a.low = float128_default_nan_low;
        a.high = float128_default_nan_high;
#  else
#    error Rules for silencing a signaling NaN are target-specific
#  endif
#else
        a.high |= LIT64( 0x0000800000000000 );
        return a;
#endif
    }
    return a;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point NaN
| `a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT float128ToCommonNaN( float128 a STATUS_PARAM)
{
    commonNaNT z;

    if ( float128_is_signaling_nan( a ) ) float_raise( float_flag_invalid STATUS_VAR);
    z.sign = a.high>>63;
    shortShift128Left( a.high, a.low, 16, &z.high, &z.low );
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the quadruple-
| precision floating-point format.
*----------------------------------------------------------------------------*/

static float128 commonNaNToFloat128( commonNaNT a )
{
    float128 z;

    shift128Right( a.high, a.low, 16, &z.high, &z.low );
    z.high |= ( ( (bits64) a.sign )<<63 ) | LIT64( 0x7FFF000000000000 );
    return z;
}

/*----------------------------------------------------------------------------
| Takes two quadruple-precision floating-point values `a' and `b', one of
| which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static float128 propagateFloat128NaN( float128 a, float128 b STATUS_PARAM)
{
    flag aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN;
    flag aIsLargerSignificand;

    aIsQuietNaN = float128_is_quiet_nan( a );
    aIsSignalingNaN = float128_is_signaling_nan( a );
    bIsQuietNaN = float128_is_quiet_nan( b );
    bIsSignalingNaN = float128_is_signaling_nan( b );

    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid STATUS_VAR);

    if ( STATUS(default_nan_mode) ) {
        a.low = float128_default_nan_low;
        a.high = float128_default_nan_high;
        return a;
    }

    if (lt128(a.high<<1, a.low, b.high<<1, b.low)) {
        aIsLargerSignificand = 0;
    } else if (lt128(b.high<<1, b.low, a.high<<1, a.low)) {
        aIsLargerSignificand = 1;
    } else {
        aIsLargerSignificand = (a.high < b.high) ? 1 : 0;
    }

    if (pickNaN(aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN,
                aIsLargerSignificand)) {
        return float128_maybe_silence_nan(b);
    } else {
        return float128_maybe_silence_nan(a);
    }
}

#endif
