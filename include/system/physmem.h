/*
 * QEMU physical memory interfaces (target independent).
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_SYSTEM_PHYSMEM_H
#define QEMU_SYSTEM_PHYSMEM_H

#include "exec/hwaddr.h"
#include "system/ramlist.h"

/**
 * physical_memory_map: Map guest physical memory region into host virtual
 *                      address.
 *
 * Map a memory region from the legacy global #address_space_memory address
 * space. May return %NULL and set *@plen to zero(0), if resources needed to
 * perform the mapping are exhausted.
 *
 * @addr: address within that address space
 * @len: pointer to length of buffer; updated on return
 * @is_write: whether the translation operation is for write
 */
void *physical_memory_map(hwaddr addr, hwaddr *plen, bool is_write);

/**
 * physical_memory_unmap: Unmaps a memory region previously mapped by
 *                        physical_memory_map()
 *
 * @buffer: host pointer as returned by physical_memory_map()
 * @len: buffer length as returned by physical_memory_map()
 * @is_write: whether the translation operation is for write
 * @access_len: amount of data actually transferred
 */
void physical_memory_unmap(void *buffer, hwaddr len,
                           bool is_write, hwaddr access_len);

/**
 * physical_memory_read: Read from the legacy global address space.
 *
 * This function access the legacy global #address_space_memory address
 * space and does not say whether the operation succeeded or failed.
 *
 * @addr: address within the legacy global address space
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 */
void physical_memory_read(hwaddr addr, void *buf, hwaddr len);

/**
 * physical_memory_write: Write to the legacy global address space.
 *
 * This function access the legacy global #address_space_memory address
 * space and does not say whether the operation succeeded or failed.
 *
 * @addr: address within the legacy global address space
 * @buf: buffer with the data transferred
 * @len: the number of bytes to write
 */
void physical_memory_write(hwaddr addr, const void *buf, hwaddr len);

#define DIRTY_CLIENTS_ALL     ((1 << DIRTY_MEMORY_NUM) - 1)
#define DIRTY_CLIENTS_NOCODE  (DIRTY_CLIENTS_ALL & ~(1 << DIRTY_MEMORY_CODE))

bool physical_memory_get_dirty_flag(ram_addr_t addr, unsigned client);

bool physical_memory_is_clean(ram_addr_t addr);

uint8_t physical_memory_range_includes_clean(ram_addr_t start,
                                             ram_addr_t length,
                                             uint8_t mask);

void physical_memory_set_dirty_flag(ram_addr_t addr, unsigned client);

void physical_memory_set_dirty_range(ram_addr_t start, ram_addr_t length,
                                     uint8_t mask);

/*
 * Contrary to physical_memory_sync_dirty_bitmap() this function returns
 * the number of dirty pages in @bitmap passed as argument. On the other hand,
 * physical_memory_sync_dirty_bitmap() returns newly dirtied pages that
 * weren't set in the global migration bitmap.
 */
uint64_t physical_memory_set_dirty_lebitmap(unsigned long *bitmap,
                                            ram_addr_t start,
                                            ram_addr_t pages);

void physical_memory_dirty_bits_cleared(ram_addr_t start, ram_addr_t length);

uint64_t physical_memory_test_and_clear_dirty(ram_addr_t start,
                                              ram_addr_t length,
                                              unsigned client,
                                              unsigned long *bmap);

DirtyBitmapSnapshot *
physical_memory_snapshot_and_clear_dirty(MemoryRegion *mr, hwaddr offset,
                                         hwaddr length, unsigned client);

bool physical_memory_snapshot_get_dirty(DirtyBitmapSnapshot *snap,
                                        ram_addr_t start,
                                        ram_addr_t length);
int ram_block_rebind(Error **errp);

#endif
