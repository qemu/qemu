/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2021 WANG Xuerui <git@xen0n.name>
 */

#ifndef TCG_TARGET_REG_BITS_H
#define TCG_TARGET_REG_BITS_H

/*
 * Loongson removed the (incomplete) 32-bit support from kernel and toolchain
 * for the initial upstreaming of this architecture, so don't bother and just
 * support the LP64* ABI for now.
 */
#if defined(__loongarch64)
# define TCG_TARGET_REG_BITS 64
#else
# error unsupported LoongArch register size
#endif

#endif
