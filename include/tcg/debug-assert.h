/* SPDX-License-Identifier: MIT */
/*
 * Define tcg_debug_assert
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_DEBUG_ASSERT_H
#define TCG_DEBUG_ASSERT_H

#if defined CONFIG_DEBUG_TCG || defined QEMU_STATIC_ANALYSIS
# define tcg_debug_assert(X) do { assert(X); } while (0)
#else
# define tcg_debug_assert(X) \
    do { if (!(X)) { __builtin_unreachable(); } } while (0)
#endif

#endif
