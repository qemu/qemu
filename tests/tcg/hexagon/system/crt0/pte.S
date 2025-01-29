/*
 *  Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

	.section .start, "awx", @progbits
	.p2align 3
	.subsection 1
/* This is the translation table    */
/* We make a table of 2^12 entries  */
/* Each entry is a .hword (16 bits) */
/* Each entry is initialized to 0 in the 4 LSBs (cached WB, see system spec) */
/* Each entry is initialized to 1:1 Virtual:Physical in the upper 12 bits.   */
/* We use the preprocessor to avoid copy-paste errors and to avoid  */
/* an 8192-line addition  to the file.                              */

	.set __UPTE_START, UPTE_START
	.weak __UPTE_START, UPTE_START
UPTE_START:
#define TLBENTRY(X) .hword ((((X) >> 16) & (0xfff0)) | 0x7);

#define TLB_1M(X)  TLBENTRY ((X) << 20)
#define TLB_16M(X) \
	TLB_1M (((X) << 4) + 0) \
	TLB_1M (((X) << 4) + 1) \
	TLB_1M (((X) << 4) + 2) \
	TLB_1M (((X) << 4) + 3) \
	TLB_1M (((X) << 4) + 4) \
	TLB_1M (((X) << 4) + 5) \
	TLB_1M (((X) << 4) + 6) \
	TLB_1M (((X) << 4) + 7) \
	TLB_1M (((X) << 4) + 8) \
	TLB_1M (((X) << 4) + 9) \
	TLB_1M (((X) << 4) + 10) \
	TLB_1M (((X) << 4) + 11) \
	TLB_1M (((X) << 4) + 12) \
	TLB_1M (((X) << 4) + 13) \
	TLB_1M (((X) << 4) + 14) \
	TLB_1M (((X) << 4) + 15)

#define TLB_256M(X) \
	TLB_16M (((X) << 4) + 0) \
	TLB_16M (((X) << 4) + 1) \
	TLB_16M (((X) << 4) + 2) \
	TLB_16M (((X) << 4) + 3) \
	TLB_16M (((X) << 4) + 4) \
	TLB_16M (((X) << 4) + 5) \
	TLB_16M (((X) << 4) + 6) \
	TLB_16M (((X) << 4) + 7) \
	TLB_16M (((X) << 4) + 8) \
	TLB_16M (((X) << 4) + 9) \
	TLB_16M (((X) << 4) + 10) \
	TLB_16M (((X) << 4) + 11) \
	TLB_16M (((X) << 4) + 12) \
	TLB_16M (((X) << 4) + 13) \
	TLB_16M (((X) << 4) + 14) \
	TLB_16M (((X) << 4) + 15)

#define TLB_4G  \
	TLB_256M (0) \
	TLB_256M (1) \
	TLB_256M (2) \
	TLB_256M (3) \
	TLB_256M (4) \
	TLB_256M (5) \
	TLB_256M (6) \
	TLB_256M (7) \
	TLB_256M (8) \
	TLB_256M (9) \
	TLB_256M (10) \
	TLB_256M (11) \
	TLB_256M (12) \
	TLB_256M (13) \
	TLB_256M (14) \
	TLB_256M (15)

TLB_4G

	.size UPTE_START, . - UPTE_START
