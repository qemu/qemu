/* SPDX-License-Identifier: MIT */
/*
 * Define TCG_OVERSIZED_GUEST
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef EXEC_TCG_OVERSIZED_GUEST_H
#define EXEC_TCG_OVERSIZED_GUEST_H

#include "tcg-target-reg-bits.h"
#include "cpu-param.h"

/*
 * Oversized TCG guests make things like MTTCG hard
 * as we can't use atomics for cputlb updates.
 */
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
#define TCG_OVERSIZED_GUEST 1
#else
#define TCG_OVERSIZED_GUEST 0
#endif

#endif
