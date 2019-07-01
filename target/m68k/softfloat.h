/*
 * Ported from a work by Andreas Grabher for Previous, NeXT Computer Emulator,
 * derived from NetBSD M68040 FPSP functions,
 * derived from release 2a of the SoftFloat IEC/IEEE Floating-point Arithmetic
 * Package. Those parts of the code (and some later contributions) are
 * provided under that license, as detailed below.
 * It has subsequently been modified by contributors to the QEMU Project,
 * so some portions are provided under:
 *  the SoftFloat-2a license
 *  the BSD license
 *  GPL-v2-or-later
 *
 * Any future contributions to this file will be taken to be licensed under
 * the Softfloat-2a license unless specifically indicated otherwise.
 */

/*
 * Portions of this work are licensed under the terms of the GNU GPL,
 * version 2 or later. See the COPYING file in the top-level directory.
 */

#ifndef TARGET_M68K_SOFTFLOAT_H
#define TARGET_M68K_SOFTFLOAT_H
#include "fpu/softfloat.h"

floatx80 floatx80_mod(floatx80 a, floatx80 b, float_status *status);
floatx80 floatx80_getman(floatx80 a, float_status *status);
floatx80 floatx80_getexp(floatx80 a, float_status *status);
floatx80 floatx80_scale(floatx80 a, floatx80 b, float_status *status);
floatx80 floatx80_move(floatx80 a, float_status *status);
floatx80 floatx80_lognp1(floatx80 a, float_status *status);
floatx80 floatx80_logn(floatx80 a, float_status *status);
floatx80 floatx80_log10(floatx80 a, float_status *status);
floatx80 floatx80_log2(floatx80 a, float_status *status);
floatx80 floatx80_etox(floatx80 a, float_status *status);
floatx80 floatx80_twotox(floatx80 a, float_status *status);
floatx80 floatx80_tentox(floatx80 a, float_status *status);
floatx80 floatx80_tan(floatx80 a, float_status *status);
floatx80 floatx80_sin(floatx80 a, float_status *status);
floatx80 floatx80_cos(floatx80 a, float_status *status);
floatx80 floatx80_atan(floatx80 a, float_status *status);
floatx80 floatx80_asin(floatx80 a, float_status *status);
floatx80 floatx80_acos(floatx80 a, float_status *status);
floatx80 floatx80_atanh(floatx80 a, float_status *status);
floatx80 floatx80_etoxm1(floatx80 a, float_status *status);
floatx80 floatx80_tanh(floatx80 a, float_status *status);
floatx80 floatx80_sinh(floatx80 a, float_status *status);
floatx80 floatx80_cosh(floatx80 a, float_status *status);
#endif
