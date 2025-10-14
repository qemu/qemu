/*
 * QEMU page protection definitions.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#ifndef EXEC_PAGE_PROT_COMMON_H
#define EXEC_PAGE_PROT_COMMON_H

/* same as PROT_xxx */
#define PAGE_READ      0x0001
#define PAGE_WRITE     0x0002
#define PAGE_EXEC      0x0004
#define PAGE_RWX       (PAGE_READ | PAGE_WRITE | PAGE_EXEC)
#define PAGE_VALID     0x0008
/*
 * Original state of the write flag (used when tracking self-modifying code)
 */
#define PAGE_WRITE_ORG 0x0010
/*
 * Invalidate the TLB entry immediately, helpful for s390x
 * Low-Address-Protection. Used with PAGE_WRITE in tlb_set_page_with_attrs()
 */
#define PAGE_WRITE_INV 0x0020
/*
 * For linux-user, indicates that the page is mapped with the same semantics
 * in both guest and host.
 */
#define PAGE_PASSTHROUGH 0x40
/* For linux-user, indicates that the page is MAP_ANON. */
#define PAGE_ANON      0x0080
/*
 * For linux-user, indicates that the page should not be
 * included in a core dump.
 */
#define PAGE_DONTDUMP  0x0100
/* Target-specific bits that will be used via page_get_flags().  */
#define PAGE_TARGET_1  0x0200
#define PAGE_TARGET_2  0x0400

#endif
