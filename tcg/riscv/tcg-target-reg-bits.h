/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2018 SiFive, Inc
 */

#ifndef TCG_TARGET_REG_BITS_H
#define TCG_TARGET_REG_BITS_H

/*
 * We don't support oversize guests.
 * Since we will only build tcg once, this in turn requires a 64-bit host.
 */
#if __riscv_xlen != 64
#error "unsupported code generation mode"
#endif
#define TCG_TARGET_REG_BITS 64

#endif
