/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper file for declaring TCG helper functions.
 * This one expands prototypes for the helper functions.
 */

#ifndef HELPER_PROTO_COMMON_H
#define HELPER_PROTO_COMMON_H

#include "qemu/atomic128.h"  /* for HAVE_CMPXCHG128 */

#include "exec/helper-head.h.inc"

/* Common helpers are built once, without a target prefix. */
#pragma push_macro("HELPER_PREFIX")
#undef HELPER_PREFIX
#define HELPER_PREFIX
#define HELPER_H "accel/tcg/tcg-runtime.h"
#include "exec/helper-proto.h.inc"
#undef  HELPER_H
#pragma pop_macro("HELPER_PREFIX")

#include "accel/tcg/getpc.h"

#endif /* HELPER_PROTO_COMMON_H */
