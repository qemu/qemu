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

/* Portions of this work are licensed under the terms of the GNU GPL,
 * version 2 or later. See the COPYING file in the top-level directory.
 */

#ifndef TARGET_M68K_SOFTFLOAT_H
#define TARGET_M68K_SOFTFLOAT_H
#include "fpu/softfloat.h"

floatx80 floatx80_mod(floatx80 a, floatx80 b, float_status *status);
floatx80 floatx80_getman(floatx80 a, float_status *status);
floatx80 floatx80_getexp(floatx80 a, float_status *status);
floatx80 floatx80_scale(floatx80 a, floatx80 b, float_status *status);
#endif
