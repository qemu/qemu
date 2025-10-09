/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 */

#ifndef TCG_TARGET_REG_BITS_H
#define TCG_TARGET_REG_BITS_H

#if !defined(_MIPS_SIM) || _MIPS_SIM != _ABI64
# error "Unknown ABI"
#endif

#define TCG_TARGET_REG_BITS 64

#endif
