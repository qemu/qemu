/*
 * QEMU page protection declarations.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#ifndef USER_PAGE_PROTECTION_H
#define USER_PAGE_PROTECTION_H

#ifndef CONFIG_USER_ONLY
#error Cannot include this header from system emulation
#endif

#include "exec/vaddr.h"
#include "exec/translation-block.h"

int page_unprotect(CPUState *cpu, tb_page_addr_t address, uintptr_t pc);

int page_get_flags(vaddr address);

/**
 * page_set_flags:
 * @start: first byte of range
 * @last: last byte of range
 * @set_flags: flags to set
 * @clr_flags: flags to clear
 * Context: holding mmap lock
 *
 * Modify the flags of a page and invalidate the code if necessary.
 * The flag PAGE_WRITE_ORG is positioned automatically depending
 * on PAGE_WRITE.  The mmap_lock should already be held.
 *
 * For each page, flags = (flags & ~clr_flags) | set_flags.
 * If clr_flags includes PAGE_VALID, this indicates a new mapping
 * and page_reset_target_data will be called as well.
 */
void page_set_flags(vaddr start, vaddr last, int set_flags, int clr_flags);

void page_reset_target_data(vaddr start, vaddr last);

/**
 * page_check_range
 * @start: first byte of range
 * @len: length of range
 * @flags: flags required for each page
 *
 * Return true if every page in [@start, @start+@len) has @flags set.
 * Return false if any page is unmapped.  Thus testing flags == 0 is
 * equivalent to testing for flags == PAGE_VALID.
 */
bool page_check_range(vaddr start, vaddr last, int flags);

/**
 * page_check_range_empty:
 * @start: first byte of range
 * @last: last byte of range
 * Context: holding mmap lock
 *
 * Return true if the entire range [@start, @last] is unmapped.
 * The memory lock must be held so that the caller will can ensure
 * the result stays true until a new mapping can be installed.
 */
bool page_check_range_empty(vaddr start, vaddr last);

/**
 * page_find_range_empty
 * @min: first byte of search range
 * @max: last byte of search range
 * @len: size of the hole required
 * @align: alignment of the hole required (power of 2)
 *
 * If there is a range [x, x+@len) within [@min, @max] such that
 * x % @align == 0, then return x.  Otherwise return -1.
 * The memory lock must be held, as the caller will want to ensure
 * the returned range stays empty until a new mapping can be installed.
 */
vaddr page_find_range_empty(vaddr min, vaddr max, vaddr len, vaddr align);

/**
 * page_get_target_data
 * @address: guest virtual address
 * @size: per-page size
 *
 * Return @size bytes of out-of-band data to associate
 * with the guest page at @address, allocating it if necessary.  The
 * caller should already have verified that the address is valid.
 * The value of @size must be the same for every call.
 *
 * The memory will be freed when the guest page is deallocated,
 * e.g. with the munmap system call.
 */
__attribute__((returns_nonnull))
void *page_get_target_data(vaddr address, size_t size);

typedef int (*walk_memory_regions_fn)(void *, vaddr, vaddr, int);
int walk_memory_regions(void *, walk_memory_regions_fn);

void page_dump(FILE *f);

#endif
