
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

#if defined(TARGET_MIPS) || defined(TARGET_HPPA)
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
#elif defined(TARGET_POWERPC) || defined(TARGET_ARM)
#define float32_default_nan make_float32(0x7FC00000)
#elif defined(TARGET_HPPA)
#define float32_default_nan make_float32(0x7FA00000)
#elif SNAN_BIT_IS_ONE
#define float32_default_nan make_float32(0x7FBFFFFF)
#else
#define float32_default_nan make_float32(0xFFC00000)
#endif

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float32_is_nan( float32 a_ )
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
| Takes two single-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static float32 propagateFloat32NaN( float32 a, float32 b STATUS_PARAM)
{
    flag aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;
    bits32 av, bv, res;

    if ( STATUS(default_nan_mode) )
        return float32_default_nan;

    aIsNaN = float32_is_nan( a );
    aIsSignalingNaN = float32_is_signaling_nan( a );
    bIsNaN = float32_is_nan( b );
    bIsSignalingNaN = float32_is_signaling_nan( b );
    av = float32_val(a);
    bv = float32_val(b);
#if SNAN_BIT_IS_ONE
    av &= ~0x00400000;
    bv &= ~0x00400000;
#else
    av |= 0x00400000;
    bv |= 0x00400000;
#endif
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid STATUS_VAR);
    if ( aIsSignalingNaN ) {
        if ( bIsSignalingNaN ) goto returnLargerSignificand;
        res = bIsNaN ? bv : av;
    }
    else if ( aIsNaN ) {
        if ( bIsSignalingNaN || ! bIsNaN )
            res = av;
        else {
 returnLargerSignificand:
            if ( (bits32) ( av<<1 ) < (bits32) ( bv<<1 ) )
                res = bv;
            else if ( (bits32) ( bv<<1 ) < (bits32) ( av<<1 ) )
                res = av;
            else
                res = ( av < bv ) ? av : bv;
        }
    }
    else {
        res = bv;
    }
    return make_float32(res);
}

/*----------------------------------------------------------------------------
| The pattern for a default generated double-precision NaN.
*----------------------------------------------------------------------------*/
#if defined(TARGET_SPARC)
#define float64_default_nan make_float64(LIT64( 0x7FFFFFFFFFFFFFFF ))
#elif defined(TARGET_POWERPC) || defined(TARGET_ARM)
#define float64_default_nan make_float64(LIT64( 0x7FF8000000000000 ))
#elif defined(TARGET_HPPA)
#define float64_default_nan make_float64(LIT64( 0x7FF4000000000000 ))
#elif SNAN_BIT_IS_ONE
#define float64_default_nan make_float64(LIT64( 0x7FF7FFFFFFFFFFFF ))
#else
#define float64_default_nan make_float64(LIT64( 0xFFF8000000000000 ))
#endif

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float64_is_nan( float64 a_ )
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
    flag aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;
    bits64 av, bv, res;

    if ( STATUS(default_nan_mode) )
        return float64_default_nan;

    aIsNaN = float64_is_nan( a );
    aIsSignalingNaN = float64_is_signaling_nan( a );
    bIsNaN = float64_is_nan( b );
    bIsSignalingNaN = float64_is_signaling_nan( b );
    av = float64_val(a);
    bv = float64_val(b);
#if SNAN_BIT_IS_ONE
    av &= ~LIT64( 0x0008000000000000 );
    bv &= ~LIT64( 0x0008000000000000 );
#else
    av |= LIT64( 0x0008000000000000 );
    bv |= LIT64( 0x0008000000000000 );
#endif
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid STATUS_VAR);
    if ( aIsSignalingNaN ) {
        if ( bIsSignalingNaN ) goto returnLargerSignificand;
        res = bIsNaN ? bv : av;
    }
    else if ( aIsNaN ) {
        if ( bIsSignalingNaN || ! bIsNaN )
            res = av;
        else {
 returnLargerSignificand:
            if ( (bits64) ( av<<1 ) < (bits64) ( bv<<1 ) )
                res = bv;
            else if ( (bits64) ( bv<<1 ) < (bits64) ( av<<1 ) )
                res = av;
            else
                res = ( av < bv ) ? av : bv;
        }
    }
    else {
        res = bv;
    }
    return make_float64(res);
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
| quiet NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int floatx80_is_nan( floatx80 a )
{
#if SNAN_BIT_IS_ONE
    bits64 aLow;

    aLow = a.low & ~ LIT64( 0x4000000000000000 );
    return
           ( ( a.high & 0x7FFF ) == 0x7FFF )
        && (bits64) ( aLow<<1 )
        && ( a.low == aLow );
#else
    return ( ( a.high & 0x7FFF ) == 0x7FFF ) && (bits64) ( a.low<<1 );
#endif
}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is a
| signaling NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int floatx80_is_signaling_nan( floatx80 a )
{
#if SNAN_BIT_IS_ONE
    return ( ( a.high & 0x7FFF ) == 0x7FFF ) && (bits64) ( a.low<<1 );
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
    flag aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    if ( STATUS(default_nan_mode) ) {
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }

    aIsNaN = floatx80_is_nan( a );
    aIsSignalingNaN = floatx80_is_signaling_nan( a );
    bIsNaN = floatx80_is_nan( b );
    bIsSignalingNaN = floatx80_is_signaling_nan( b );
#if SNAN_BIT_IS_ONE
    a.low &= ~LIT64( 0xC000000000000000 );
    b.low &= ~LIT64( 0xC000000000000000 );
#else
    a.low |= LIT64( 0xC000000000000000 );
    b.low |= LIT64( 0xC000000000000000 );
#endif
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid STATUS_VAR);
    if ( aIsSignalingNaN ) {
        if ( bIsSignalingNaN ) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if ( aIsNaN ) {
        if ( bIsSignalingNaN || ! bIsNaN ) return a;
 returnLargerSignificand:
        if ( a.low < b.low ) return b;
        if ( b.low < a.low ) return a;
        return ( a.high < b.high ) ? a : b;
    }
    else {
        return b;
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

int float128_is_nan( float128 a )
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
    flag aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    if ( STATUS(default_nan_mode) ) {
        a.low = float128_default_nan_low;
        a.high = float128_default_nan_high;
        return a;
    }

    aIsNaN = float128_is_nan( a );
    aIsSignalingNaN = float128_is_signaling_nan( a );
    bIsNaN = float128_is_nan( b );
    bIsSignalingNaN = float128_is_signaling_nan( b );
#if SNAN_BIT_IS_ONE
    a.high &= ~LIT64( 0x0000800000000000 );
    b.high &= ~LIT64( 0x0000800000000000 );
#else
    a.high |= LIT64( 0x0000800000000000 );
    b.high |= LIT64( 0x0000800000000000 );
#endif
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid STATUS_VAR);
    if ( aIsSignalingNaN ) {
        if ( bIsSignalingNaN ) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if ( aIsNaN ) {
        if ( bIsSignalingNaN || ! bIsNaN ) return a;
 returnLargerSignificand:
        if ( lt128( a.high<<1, a.low, b.high<<1, b.low ) ) return b;
        if ( lt128( b.high<<1, b.low, a.high<<1, a.low ) ) return a;
        return ( a.high < b.high ) ? a : b;
    }
    else {
        return b;
    }
}

#endif
