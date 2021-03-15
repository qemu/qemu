/*
 * A sparse memory device. Useful for fuzzing
 *
 * Copyright Red Hat Inc., 2021
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SPARSE_MEM_H
#define SPARSE_MEM_H
#define TYPE_SPARSE_MEM "sparse-mem"

MemoryRegion *sparse_mem_init(uint64_t addr, uint64_t length);

#endif
