/*
 *  Physical memory access templates
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2015 Linaro, Inc.
 *  Copyright (c) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/* warning: addr must be aligned */
static inline
uint64_t glue(address_space_ldm_internal, SUFFIX)(ARG1_DECL, MemOp mop,
                                                  hwaddr addr,
                                                  MemTxAttrs attrs,
                                                  MemTxResult *result)
{
    const unsigned size = memop_size(mop);
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = size;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, false, attrs);
    if (l < size || !memory_access_is_direct(mr, false, attrs)) {
        release_lock |= prepare_mmio_access(mr);

        /* I/O case */
        r = memory_region_dispatch_read(mr, addr1, &val, mop, attrs);
    } else {
        /* RAM case */
        fuzz_dma_read_cb(addr, size, mr);
        val = ldm_p(qemu_map_ram_ptr(mr->ram_block, addr1), mop);
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        bql_unlock();
    }
    RCU_READ_UNLOCK();
    return val;
}

uint8_t glue(address_space_ldub, SUFFIX)(ARG1_DECL, hwaddr addr,
                                         MemTxAttrs attrs, MemTxResult *result)
{
    return glue(address_space_ldm_internal, SUFFIX)(ARG1, MO_8, addr,
                                                    attrs, result);
}

/* warning: addr must be aligned */
static inline
void glue(address_space_stm_internal, SUFFIX)(ARG1_DECL, MemOp mop,
                                              hwaddr addr, uint64_t val,
                                              MemTxAttrs attrs,
                                              MemTxResult *result)
{
    const unsigned size = memop_size(mop);
    MemoryRegion *mr;
    hwaddr l = size;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, true, attrs);
    if (l < size || !memory_access_is_direct(mr, true, attrs)) {
        release_lock |= prepare_mmio_access(mr);
        r = memory_region_dispatch_write(mr, addr1, val, mop, attrs);
    } else {
        /* RAM case */
        stm_p(qemu_map_ram_ptr(mr->ram_block, addr1), mop, val);
        invalidate_and_set_dirty(mr, addr1, size);
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        bql_unlock();
    }
    RCU_READ_UNLOCK();
}

void glue(address_space_stb, SUFFIX)(ARG1_DECL, hwaddr addr, uint8_t val,
                                     MemTxAttrs attrs, MemTxResult *result)
{
    glue(address_space_stm_internal, SUFFIX)(ARG1, MO_8, addr, val,
                                             attrs, result);
}

#ifndef TARGET_NOT_USING_LEGACY_NATIVE_ENDIAN_API
#define ENDIANNESS
#define MO_ENDIAN               (target_big_endian() ? MO_BE : MO_LE)
#include "memory_ldst_endian.c.inc"
#endif /* TARGET_NOT_USING_LEGACY_NATIVE_ENDIAN_API */

#define ENDIANNESS              _le
#define MO_ENDIAN               MO_LE
#include "memory_ldst_endian.c.inc"

#define ENDIANNESS              _be
#define MO_ENDIAN               MO_BE
#include "memory_ldst_endian.c.inc"

#undef ARG1_DECL
#undef ARG1
#undef SUFFIX
#undef TRANSLATE
#undef RCU_READ_LOCK
#undef RCU_READ_UNLOCK
