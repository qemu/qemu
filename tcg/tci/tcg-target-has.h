/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2009, 2011 Stefan Weil
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

#define TCG_TARGET_HAS_extract2_i32     0
#define TCG_TARGET_HAS_qemu_st8_i32     0

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_extr_i64_i32     0
#define TCG_TARGET_HAS_extract2_i64     0
#define TCG_TARGET_HAS_add2_i32         1
#define TCG_TARGET_HAS_sub2_i32         1
#define TCG_TARGET_HAS_add2_i64         1
#define TCG_TARGET_HAS_sub2_i64         1
#endif /* TCG_TARGET_REG_BITS == 64 */

#define TCG_TARGET_HAS_qemu_ldst_i128   0

#define TCG_TARGET_HAS_tst              1

#define TCG_TARGET_extract_valid(type, ofs, len)   1
#define TCG_TARGET_sextract_valid(type, ofs, len)  1
#define TCG_TARGET_deposit_valid(type, ofs, len)   1

#endif
