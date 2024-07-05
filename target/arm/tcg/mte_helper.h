/*
 * ARM MemTag operation helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef TARGET_ARM_MTE_H
#define TARGET_ARM_MTE_H

#include "exec/mmu-access-type.h"

/**
 * allocation_tag_mem_probe:
 * @env: the cpu environment
 * @ptr_mmu_idx: the addressing regime to use for the virtual address
 * @ptr: the virtual address for which to look up tag memory
 * @ptr_access: the access to use for the virtual address
 * @ptr_size: the number of bytes in the normal memory access
 * @tag_access: the access to use for the tag memory
 * @probe: true to merely probe, never taking an exception
 * @ra: the return address for exception handling
 *
 * Our tag memory is formatted as a sequence of little-endian nibbles.
 * That is, the byte at (addr >> (LOG2_TAG_GRANULE + 1)) contains two
 * tags, with the tag at [3:0] for the lower addr and the tag at [7:4]
 * for the higher addr.
 *
 * Here, resolve the physical address from the virtual address, and return
 * a pointer to the corresponding tag byte.
 *
 * If there is no tag storage corresponding to @ptr, return NULL.
 *
 * If the page is inaccessible for @ptr_access, or has a watchpoint, there are
 * three options:
 * (1) probe = true, ra = 0 : pure probe -- we return NULL if the page is not
 *     accessible, and do not take watchpoint traps. The calling code must
 *     handle those cases in the right priority compared to MTE traps.
 * (2) probe = false, ra = 0 : probe, no fault expected -- the caller guarantees
 *     that the page is going to be accessible. We will take watchpoint traps.
 * (3) probe = false, ra != 0 : non-probe -- we will take both memory access
 *     traps and watchpoint traps.
 * (probe = true, ra != 0 is invalid and will assert.)
 */
uint8_t *allocation_tag_mem_probe(CPUARMState *env, int ptr_mmu_idx,
                                  uint64_t ptr, MMUAccessType ptr_access,
                                  int ptr_size, MMUAccessType tag_access,
                                  bool probe, uintptr_t ra);

/**
 * load_tag1 - Load 1 tag (nibble) from byte
 * @ptr: The tagged address
 * @mem: The tag address (packed, 2 tags in byte)
 */
int load_tag1(uint64_t ptr, uint8_t *mem);

/**
 * store_tag1 - Store 1 tag (nibble) into byte
 * @ptr: The tagged address
 * @mem: The tag address (packed, 2 tags in byte)
 * @tag: The tag to be stored in the nibble
 */
void store_tag1(uint64_t ptr, uint8_t *mem, int tag);

#endif /* TARGET_ARM_MTE_H */
