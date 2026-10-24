/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper file for declaring TCG helper functions.
 * This one expands generation functions for tcg opcodes.
 */

#ifndef HELPER_GEN_COMMON_H
#define HELPER_GEN_COMMON_H

#include "exec/helper-head.h.inc"

/* Common helpers are built once, without a target prefix. */
#pragma push_macro("HELPER_PREFIX")
#undef HELPER_PREFIX
#define HELPER_PREFIX
#define HELPER_H "accel/tcg/tcg-runtime.h"
#include "exec/helper-gen.h.inc"
#undef  HELPER_H
#pragma pop_macro("HELPER_PREFIX")

#endif /* HELPER_GEN_COMMON_H */
