/*
 * MIPS emulation for QEMU - microMIPS translation routines
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

/* Include the auto-generated decoders.  */
#include "decode-micromips16.c.inc"
#include "decode-micromips32.c.inc"
#include "decode-micromips32_r6.c.inc"
