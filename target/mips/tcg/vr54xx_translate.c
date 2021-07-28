/*
 * VR5432 extensions translation routines
 *
 * Reference: VR5432 Microprocessor User’s Manual
 *            (Document Number U13751EU5V0UM00)
 *
 *  Copyright (c) 2021 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "exec/helper-gen.h"
#include "translate.h"
#include "internal.h"

/* Include the auto-generated decoder. */
#include "decode-vr54xx.c.inc"
