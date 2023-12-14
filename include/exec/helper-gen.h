/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper file for declaring TCG helper functions.
 * This one expands generation functions for tcg opcodes.
 */

#ifndef HELPER_GEN_H
#define HELPER_GEN_H

#include "exec/helper-gen-common.h"

#define HELPER_H "helper.h"
#include "exec/helper-gen.h.inc"
#undef  HELPER_H

#endif /* HELPER_GEN_H */
