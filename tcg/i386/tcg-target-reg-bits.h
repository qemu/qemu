/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_TARGET_REG_BITS_H
#define TCG_TARGET_REG_BITS_H

#ifdef __x86_64__
# define TCG_TARGET_REG_BITS  64
#else
# define TCG_TARGET_REG_BITS  32
#endif

#endif
