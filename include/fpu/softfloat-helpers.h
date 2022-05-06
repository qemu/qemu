/*
 * QEMU float support - standalone helpers
 *
 * This is provided for files that don't need the access to the full
 * set of softfloat functions. Typically this is cpu initialisation
 * code which wants to set default rounding and exceptions modes.
 *
 * The code in this source file is derived from release 2a of the SoftFloat
 * IEC/IEEE Floating-point Arithmetic Package. Those parts of the code (and
 * some later contributions) are provided under that license, as detailed below.
 * It has subsequently been modified by contributors to the QEMU Project,
 * so some portions are provided under:
 *  the SoftFloat-2a license
 *  the BSD license
 *  GPL-v2-or-later
 *
 * Any future contributions to this file after December 1st 2014 will be
 * taken to be licensed under the Softfloat-2a license unless specifically
 * indicated otherwise.
 */

/*
===============================================================================
This C header file is part of the SoftFloat IEC/IEEE Floating-point
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

#ifndef SOFTFLOAT_HELPERS_H
#define SOFTFLOAT_HELPERS_H

#include "fpu/softfloat-types.h"

static inline void set_float_detect_tininess(bool val, float_status *status)
{
    status->tininess_before_rounding = val;
}

static inline void set_float_rounding_mode(FloatRoundMode val,
                                           float_status *status)
{
    status->float_rounding_mode = val;
}

static inline void set_float_exception_flags(int val, float_status *status)
{
    status->float_exception_flags = val;
}

static inline void set_floatx80_rounding_precision(FloatX80RoundPrec val,
                                                   float_status *status)
{
    status->floatx80_rounding_precision = val;
}

static inline void set_flush_to_zero(bool val, float_status *status)
{
    status->flush_to_zero = val;
}

static inline void set_flush_inputs_to_zero(bool val, float_status *status)
{
    status->flush_inputs_to_zero = val;
}

static inline void set_default_nan_mode(bool val, float_status *status)
{
    status->default_nan_mode = val;
}

static inline void set_snan_bit_is_one(bool val, float_status *status)
{
    status->snan_bit_is_one = val;
}

static inline void set_use_first_nan(bool val, float_status *status)
{
    status->use_first_nan = val;
}

static inline void set_no_signaling_nans(bool val, float_status *status)
{
    status->no_signaling_nans = val;
}

static inline bool get_float_detect_tininess(float_status *status)
{
    return status->tininess_before_rounding;
}

static inline FloatRoundMode get_float_rounding_mode(float_status *status)
{
    return status->float_rounding_mode;
}

static inline int get_float_exception_flags(float_status *status)
{
    return status->float_exception_flags;
}

static inline FloatX80RoundPrec
get_floatx80_rounding_precision(float_status *status)
{
    return status->floatx80_rounding_precision;
}

static inline bool get_flush_to_zero(float_status *status)
{
    return status->flush_to_zero;
}

static inline bool get_flush_inputs_to_zero(float_status *status)
{
    return status->flush_inputs_to_zero;
}

static inline bool get_default_nan_mode(float_status *status)
{
    return status->default_nan_mode;
}

#endif /* SOFTFLOAT_HELPERS_H */
