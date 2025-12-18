/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_TARGET_REG_BITS_H
#define TCG_TARGET_REG_BITS_H

#ifndef _ARCH_PPC64
# error Expecting 64-bit host architecture
#endif

#define TCG_TARGET_REG_BITS  64

#endif
