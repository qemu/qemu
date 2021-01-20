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
static inline uint32_t glue(address_space_ldl_internal, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result,
    enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = 4;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, false, attrs);
    if (l < 4 || !memory_access_is_direct(mr, false)) {
        release_lock |= prepare_mmio_access(mr);

        /* I/O case */
        r = memory_region_dispatch_read(mr, addr1, &val,
                                        MO_32 | devend_memop(endian), attrs);
    } else {
        /* RAM case */
        fuzz_dma_read_cb(addr, 4, mr);
        ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = ldl_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = ldl_be_p(ptr);
            break;
        default:
            val = ldl_p(ptr);
            break;
        }
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        qemu_mutex_unlock_iothread();
    }
    RCU_READ_UNLOCK();
    return val;
}

uint32_t glue(address_space_ldl, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    return glue(address_space_ldl_internal, SUFFIX)(ARG1, addr, attrs, result,
                                                    DEVICE_NATIVE_ENDIAN);
}

uint32_t glue(address_space_ldl_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    return glue(address_space_ldl_internal, SUFFIX)(ARG1, addr, attrs, result,
                                                    DEVICE_LITTLE_ENDIAN);
}

uint32_t glue(address_space_ldl_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    return glue(address_space_ldl_internal, SUFFIX)(ARG1, addr, attrs, result,
                                                    DEVICE_BIG_ENDIAN);
}

/* warning: addr must be aligned */
static inline uint64_t glue(address_space_ldq_internal, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result,
    enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = 8;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, false, attrs);
    if (l < 8 || !memory_access_is_direct(mr, false)) {
        release_lock |= prepare_mmio_access(mr);

        /* I/O case */
        r = memory_region_dispatch_read(mr, addr1, &val,
                                        MO_64 | devend_memop(endian), attrs);
    } else {
        /* RAM case */
        fuzz_dma_read_cb(addr, 8, mr);
        ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = ldq_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = ldq_be_p(ptr);
            break;
        default:
            val = ldq_p(ptr);
            break;
        }
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        qemu_mutex_unlock_iothread();
    }
    RCU_READ_UNLOCK();
    return val;
}

uint64_t glue(address_space_ldq, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    return glue(address_space_ldq_internal, SUFFIX)(ARG1, addr, attrs, result,
                                                    DEVICE_NATIVE_ENDIAN);
}

uint64_t glue(address_space_ldq_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    return glue(address_space_ldq_internal, SUFFIX)(ARG1, addr, attrs, result,
                                                    DEVICE_LITTLE_ENDIAN);
}

uint64_t glue(address_space_ldq_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    return glue(address_space_ldq_internal, SUFFIX)(ARG1, addr, attrs, result,
                                                    DEVICE_BIG_ENDIAN);
}

uint32_t glue(address_space_ldub, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = 1;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, false, attrs);
    if (!memory_access_is_direct(mr, false)) {
        release_lock |= prepare_mmio_access(mr);

        /* I/O case */
        r = memory_region_dispatch_read(mr, addr1, &val, MO_8, attrs);
    } else {
        /* RAM case */
        fuzz_dma_read_cb(addr, 1, mr);
        ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
        val = ldub_p(ptr);
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        qemu_mutex_unlock_iothread();
    }
    RCU_READ_UNLOCK();
    return val;
}

/* warning: addr must be aligned */
static inline uint32_t glue(address_space_lduw_internal, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result,
    enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = 2;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, false, attrs);
    if (l < 2 || !memory_access_is_direct(mr, false)) {
        release_lock |= prepare_mmio_access(mr);

        /* I/O case */
        r = memory_region_dispatch_read(mr, addr1, &val,
                                        MO_16 | devend_memop(endian), attrs);
    } else {
        /* RAM case */
        fuzz_dma_read_cb(addr, 2, mr);
        ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = lduw_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = lduw_be_p(ptr);
            break;
        default:
            val = lduw_p(ptr);
            break;
        }
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        qemu_mutex_unlock_iothread();
    }
    RCU_READ_UNLOCK();
    return val;
}

uint32_t glue(address_space_lduw, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    return glue(address_space_lduw_internal, SUFFIX)(ARG1, addr, attrs, result,
                                                     DEVICE_NATIVE_ENDIAN);
}

uint32_t glue(address_space_lduw_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    return glue(address_space_lduw_internal, SUFFIX)(ARG1, addr, attrs, result,
                                                     DEVICE_LITTLE_ENDIAN);
}

uint32_t glue(address_space_lduw_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    return glue(address_space_lduw_internal, SUFFIX)(ARG1, addr, attrs, result,
                                       DEVICE_BIG_ENDIAN);
}

/* warning: addr must be aligned. The ram page is not masked as dirty
   and the code inside is not invalidated. It is useful if the dirty
   bits are used to track modified PTEs */
void glue(address_space_stl_notdirty, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 4;
    hwaddr addr1;
    MemTxResult r;
    uint8_t dirty_log_mask;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, true, attrs);
    if (l < 4 || !memory_access_is_direct(mr, true)) {
        release_lock |= prepare_mmio_access(mr);

        r = memory_region_dispatch_write(mr, addr1, val, MO_32, attrs);
    } else {
        ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
        stl_p(ptr, val);

        dirty_log_mask = memory_region_get_dirty_log_mask(mr);
        dirty_log_mask &= ~(1 << DIRTY_MEMORY_CODE);
        cpu_physical_memory_set_dirty_range(memory_region_get_ram_addr(mr) + addr,
                                            4, dirty_log_mask);
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        qemu_mutex_unlock_iothread();
    }
    RCU_READ_UNLOCK();
}

/* warning: addr must be aligned */
static inline void glue(address_space_stl_internal, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs,
    MemTxResult *result, enum device_endian endian)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 4;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, true, attrs);
    if (l < 4 || !memory_access_is_direct(mr, true)) {
        release_lock |= prepare_mmio_access(mr);
        r = memory_region_dispatch_write(mr, addr1, val,
                                         MO_32 | devend_memop(endian), attrs);
    } else {
        /* RAM case */
        ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            stl_le_p(ptr, val);
            break;
        case DEVICE_BIG_ENDIAN:
            stl_be_p(ptr, val);
            break;
        default:
            stl_p(ptr, val);
            break;
        }
        invalidate_and_set_dirty(mr, addr1, 4);
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        qemu_mutex_unlock_iothread();
    }
    RCU_READ_UNLOCK();
}

void glue(address_space_stl, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    glue(address_space_stl_internal, SUFFIX)(ARG1, addr, val, attrs,
                                             result, DEVICE_NATIVE_ENDIAN);
}

void glue(address_space_stl_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    glue(address_space_stl_internal, SUFFIX)(ARG1, addr, val, attrs,
                                             result, DEVICE_LITTLE_ENDIAN);
}

void glue(address_space_stl_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    glue(address_space_stl_internal, SUFFIX)(ARG1, addr, val, attrs,
                                             result, DEVICE_BIG_ENDIAN);
}

void glue(address_space_stb, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 1;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, true, attrs);
    if (!memory_access_is_direct(mr, true)) {
        release_lock |= prepare_mmio_access(mr);
        r = memory_region_dispatch_write(mr, addr1, val, MO_8, attrs);
    } else {
        /* RAM case */
        ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
        stb_p(ptr, val);
        invalidate_and_set_dirty(mr, addr1, 1);
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        qemu_mutex_unlock_iothread();
    }
    RCU_READ_UNLOCK();
}

/* warning: addr must be aligned */
static inline void glue(address_space_stw_internal, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs,
    MemTxResult *result, enum device_endian endian)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 2;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, true, attrs);
    if (l < 2 || !memory_access_is_direct(mr, true)) {
        release_lock |= prepare_mmio_access(mr);
        r = memory_region_dispatch_write(mr, addr1, val,
                                         MO_16 | devend_memop(endian), attrs);
    } else {
        /* RAM case */
        ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            stw_le_p(ptr, val);
            break;
        case DEVICE_BIG_ENDIAN:
            stw_be_p(ptr, val);
            break;
        default:
            stw_p(ptr, val);
            break;
        }
        invalidate_and_set_dirty(mr, addr1, 2);
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        qemu_mutex_unlock_iothread();
    }
    RCU_READ_UNLOCK();
}

void glue(address_space_stw, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    glue(address_space_stw_internal, SUFFIX)(ARG1, addr, val, attrs, result,
                                             DEVICE_NATIVE_ENDIAN);
}

void glue(address_space_stw_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    glue(address_space_stw_internal, SUFFIX)(ARG1, addr, val, attrs, result,
                                             DEVICE_LITTLE_ENDIAN);
}

void glue(address_space_stw_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    glue(address_space_stw_internal, SUFFIX)(ARG1, addr, val, attrs, result,
                               DEVICE_BIG_ENDIAN);
}

static void glue(address_space_stq_internal, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint64_t val, MemTxAttrs attrs,
    MemTxResult *result, enum device_endian endian)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 8;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    RCU_READ_LOCK();
    mr = TRANSLATE(addr, &addr1, &l, true, attrs);
    if (l < 8 || !memory_access_is_direct(mr, true)) {
        release_lock |= prepare_mmio_access(mr);
        r = memory_region_dispatch_write(mr, addr1, val,
                                         MO_64 | devend_memop(endian), attrs);
    } else {
        /* RAM case */
        ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            stq_le_p(ptr, val);
            break;
        case DEVICE_BIG_ENDIAN:
            stq_be_p(ptr, val);
            break;
        default:
            stq_p(ptr, val);
            break;
        }
        invalidate_and_set_dirty(mr, addr1, 8);
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        qemu_mutex_unlock_iothread();
    }
    RCU_READ_UNLOCK();
}

void glue(address_space_stq, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint64_t val, MemTxAttrs attrs, MemTxResult *result)
{
    glue(address_space_stq_internal, SUFFIX)(ARG1, addr, val, attrs, result,
                                             DEVICE_NATIVE_ENDIAN);
}

void glue(address_space_stq_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint64_t val, MemTxAttrs attrs, MemTxResult *result)
{
    glue(address_space_stq_internal, SUFFIX)(ARG1, addr, val, attrs, result,
                                             DEVICE_LITTLE_ENDIAN);
}

void glue(address_space_stq_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint64_t val, MemTxAttrs attrs, MemTxResult *result)
{
    glue(address_space_stq_internal, SUFFIX)(ARG1, addr, val, attrs, result,
                                             DEVICE_BIG_ENDIAN);
}

#undef ARG1_DECL
#undef ARG1
#undef SUFFIX
#undef TRANSLATE
#undef RCU_READ_LOCK
#undef RCU_READ_UNLOCK
