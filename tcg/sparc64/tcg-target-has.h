/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

/* optional instructions */
#define TCG_TARGET_HAS_extr_i64_i32     0
#define TCG_TARGET_HAS_qemu_ldst_i128   0
#define TCG_TARGET_HAS_tst              1

#define TCG_TARGET_extract_valid(type, ofs, len) \
    ((type) == TCG_TYPE_I64 && (ofs) + (len) == 32)

#define TCG_TARGET_sextract_valid  TCG_TARGET_extract_valid

#define TCG_TARGET_deposit_valid(type, ofs, len) 0

#endif
