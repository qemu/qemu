/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2009 Ulrich Hecht <uli@suse.de>
 */

#ifndef TCG_TARGET_REG_BITS_H
#define TCG_TARGET_REG_BITS_H

/* We only support generating code for 64-bit mode.  */
#if UINTPTR_MAX == UINT64_MAX
# define TCG_TARGET_REG_BITS 64
#else
# error "unsupported code generation mode"
#endif

#endif
