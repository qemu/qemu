/*
 * Inter-Thread Communication Unit emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "exec/exec-all.h"
#include "hw/misc/mips_itu.h"
#include "hw/qdev-properties.h"

#define ITC_TAG_ADDRSPACE_SZ (ITC_ADDRESSMAP_NUM * 8)
/* Initialize as 4kB area to fit all 32 cells with default 128B grain.
   Storage may be resized by the software. */
#define ITC_STORAGE_ADDRSPACE_SZ 0x1000

#define ITC_FIFO_NUM_MAX 16
#define ITC_SEMAPH_NUM_MAX 16
#define ITC_AM1_NUMENTRIES_OFS 20

#define ITC_CELL_PV_MAX_VAL 0xFFFF

#define ITC_CELL_TAG_FIFO_DEPTH 28
#define ITC_CELL_TAG_FIFO_PTR 18
#define ITC_CELL_TAG_FIFO 17
#define ITC_CELL_TAG_T 16
#define ITC_CELL_TAG_F 1
#define ITC_CELL_TAG_E 0

#define ITC_AM0_BASE_ADDRESS_MASK 0xFFFFFC00ULL
#define ITC_AM0_EN_MASK 0x1

#define ITC_AM1_ADDR_MASK_MASK 0x1FC00
#define ITC_AM1_ENTRY_GRAIN_MASK 0x7

typedef enum ITCView {
    ITCVIEW_BYPASS  = 0,
    ITCVIEW_CONTROL = 1,
    ITCVIEW_EF_SYNC = 2,
    ITCVIEW_EF_TRY  = 3,
    ITCVIEW_PV_SYNC = 4,
    ITCVIEW_PV_TRY  = 5,
    ITCVIEW_PV_ICR0 = 15,
} ITCView;

#define ITC_ICR0_CELL_NUM        16
#define ITC_ICR0_BLK_GRAIN       8
#define ITC_ICR0_BLK_GRAIN_MASK  0x7
#define ITC_ICR0_ERR_AXI         2
#define ITC_ICR0_ERR_PARITY      1
#define ITC_ICR0_ERR_EXEC        0

MemoryRegion *mips_itu_get_tag_region(MIPSITUState *itu)
{
    return &itu->tag_io;
}

static uint64_t itc_tag_read(void *opaque, hwaddr addr, unsigned size)
{
    MIPSITUState *tag = (MIPSITUState *)opaque;
    uint64_t index = addr >> 3;

    if (index >= ITC_ADDRESSMAP_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "Read 0x%" PRIx64 "\n", addr);
        return 0;
    }

    return tag->ITCAddressMap[index];
}

void itc_reconfigure(MIPSITUState *tag)
{
    uint64_t *am = &tag->ITCAddressMap[0];
    MemoryRegion *mr = &tag->storage_io;
    hwaddr address = am[0] & ITC_AM0_BASE_ADDRESS_MASK;
    uint64_t size = (1 * KiB) + (am[1] & ITC_AM1_ADDR_MASK_MASK);
    bool is_enabled = (am[0] & ITC_AM0_EN_MASK) != 0;

    if (tag->saar_present) {
        address = ((*(uint64_t *) tag->saar) & 0xFFFFFFFFE000ULL) << 4;
        size = 1ULL << ((*(uint64_t *) tag->saar >> 1) & 0x1f);
        is_enabled = *(uint64_t *) tag->saar & 1;
    }

    memory_region_transaction_begin();
    if (!(size & (size - 1))) {
        memory_region_set_size(mr, size);
    }
    memory_region_set_address(mr, address);
    memory_region_set_enabled(mr, is_enabled);
    memory_region_transaction_commit();
}

static void itc_tag_write(void *opaque, hwaddr addr,
                          uint64_t data, unsigned size)
{
    MIPSITUState *tag = (MIPSITUState *)opaque;
    uint64_t *am = &tag->ITCAddressMap[0];
    uint64_t am_old, mask;
    uint64_t index = addr >> 3;

    switch (index) {
    case 0:
        mask = ITC_AM0_BASE_ADDRESS_MASK | ITC_AM0_EN_MASK;
        break;
    case 1:
        mask = ITC_AM1_ADDR_MASK_MASK | ITC_AM1_ENTRY_GRAIN_MASK;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Bad write 0x%" PRIx64 "\n", addr);
        return;
    }

    am_old = am[index];
    am[index] = (data & mask) | (am_old & ~mask);
    if (am_old != am[index]) {
        itc_reconfigure(tag);
    }
}

static const MemoryRegionOps itc_tag_ops = {
    .read = itc_tag_read,
    .write = itc_tag_write,
    .impl = {
        .max_access_size = 8,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static inline uint32_t get_num_cells(MIPSITUState *s)
{
    return s->num_fifo + s->num_semaphores;
}

static inline ITCView get_itc_view(hwaddr addr)
{
    return (addr >> 3) & 0xf;
}

static inline int get_cell_stride_shift(const MIPSITUState *s)
{
    /* Minimum interval (for EntryGain = 0) is 128 B */
    if (s->saar_present) {
        return 7 + ((s->icr0 >> ITC_ICR0_BLK_GRAIN) &
                    ITC_ICR0_BLK_GRAIN_MASK);
    } else {
        return 7 + (s->ITCAddressMap[1] & ITC_AM1_ENTRY_GRAIN_MASK);
    }
}

static inline ITCStorageCell *get_cell(MIPSITUState *s,
                                       hwaddr addr)
{
    uint32_t cell_idx = addr >> get_cell_stride_shift(s);
    uint32_t num_cells = get_num_cells(s);

    if (cell_idx >= num_cells) {
        cell_idx = num_cells - 1;
    }

    return &s->cell[cell_idx];
}

static void wake_blocked_threads(ITCStorageCell *c)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        if (cs->halted && (c->blocked_threads & (1ULL << cs->cpu_index))) {
            cpu_interrupt(cs, CPU_INTERRUPT_WAKE);
        }
    }
    c->blocked_threads = 0;
}

static void QEMU_NORETURN block_thread_and_exit(ITCStorageCell *c)
{
    c->blocked_threads |= 1ULL << current_cpu->cpu_index;
    current_cpu->halted = 1;
    current_cpu->exception_index = EXCP_HLT;
    cpu_loop_exit_restore(current_cpu, current_cpu->mem_io_pc);
}

/* ITC Bypass View */

static inline uint64_t view_bypass_read(ITCStorageCell *c)
{
    if (c->tag.FIFO) {
        return c->data[c->fifo_out];
    } else {
        return c->data[0];
    }
}

static inline void view_bypass_write(ITCStorageCell *c, uint64_t val)
{
    if (c->tag.FIFO && (c->tag.FIFOPtr > 0)) {
        int idx = (c->fifo_out + c->tag.FIFOPtr - 1) % ITC_CELL_DEPTH;
        c->data[idx] = val;
    }

    /* ignore a write to the semaphore cell */
}

/* ITC Control View */

static inline uint64_t view_control_read(ITCStorageCell *c)
{
    return ((uint64_t)c->tag.FIFODepth << ITC_CELL_TAG_FIFO_DEPTH) |
           (c->tag.FIFOPtr << ITC_CELL_TAG_FIFO_PTR) |
           (c->tag.FIFO << ITC_CELL_TAG_FIFO) |
           (c->tag.T << ITC_CELL_TAG_T) |
           (c->tag.E << ITC_CELL_TAG_E) |
           (c->tag.F << ITC_CELL_TAG_F);
}

static inline void view_control_write(ITCStorageCell *c, uint64_t val)
{
    c->tag.T = (val >> ITC_CELL_TAG_T) & 1;
    c->tag.E = (val >> ITC_CELL_TAG_E) & 1;
    c->tag.F = (val >> ITC_CELL_TAG_F) & 1;

    if (c->tag.E) {
        c->tag.FIFOPtr = 0;
    }
}

/* ITC Empty/Full View */

static uint64_t view_ef_common_read(ITCStorageCell *c, bool blocking)
{
    uint64_t ret = 0;

    if (!c->tag.FIFO) {
        return 0;
    }

    c->tag.F = 0;

    if (blocking && c->tag.E) {
        block_thread_and_exit(c);
    }

    if (c->blocked_threads) {
        wake_blocked_threads(c);
    }

    if (c->tag.FIFOPtr > 0) {
        ret = c->data[c->fifo_out];
        c->fifo_out = (c->fifo_out + 1) % ITC_CELL_DEPTH;
        c->tag.FIFOPtr--;
    }

    if (c->tag.FIFOPtr == 0) {
        c->tag.E = 1;
    }

    return ret;
}

static uint64_t view_ef_sync_read(ITCStorageCell *c)
{
    return view_ef_common_read(c, true);
}

static uint64_t view_ef_try_read(ITCStorageCell *c)
{
    return view_ef_common_read(c, false);
}

static inline void view_ef_common_write(ITCStorageCell *c, uint64_t val,
                                        bool blocking)
{
    if (!c->tag.FIFO) {
        return;
    }

    c->tag.E = 0;

    if (blocking && c->tag.F) {
        block_thread_and_exit(c);
    }

    if (c->blocked_threads) {
        wake_blocked_threads(c);
    }

    if (c->tag.FIFOPtr < ITC_CELL_DEPTH) {
        int idx = (c->fifo_out + c->tag.FIFOPtr) % ITC_CELL_DEPTH;
        c->data[idx] = val;
        c->tag.FIFOPtr++;
    }

    if (c->tag.FIFOPtr == ITC_CELL_DEPTH) {
        c->tag.F = 1;
    }
}

static void view_ef_sync_write(ITCStorageCell *c, uint64_t val)
{
    view_ef_common_write(c, val, true);
}

static void view_ef_try_write(ITCStorageCell *c, uint64_t val)
{
    view_ef_common_write(c, val, false);
}

/* ITC P/V View */

static uint64_t view_pv_common_read(ITCStorageCell *c, bool blocking)
{
    uint64_t ret = c->data[0];

    if (c->tag.FIFO) {
        return 0;
    }

    if (c->data[0] > 0) {
        c->data[0]--;
    } else if (blocking) {
        block_thread_and_exit(c);
    }

    return ret;
}

static uint64_t view_pv_sync_read(ITCStorageCell *c)
{
    return view_pv_common_read(c, true);
}

static uint64_t view_pv_try_read(ITCStorageCell *c)
{
    return view_pv_common_read(c, false);
}

static inline void view_pv_common_write(ITCStorageCell *c)
{
    if (c->tag.FIFO) {
        return;
    }

    if (c->data[0] < ITC_CELL_PV_MAX_VAL) {
        c->data[0]++;
    }

    if (c->blocked_threads) {
        wake_blocked_threads(c);
    }
}

static void view_pv_sync_write(ITCStorageCell *c)
{
    view_pv_common_write(c);
}

static void view_pv_try_write(ITCStorageCell *c)
{
    view_pv_common_write(c);
}

static void raise_exception(int excp)
{
    current_cpu->exception_index = excp;
    cpu_loop_exit(current_cpu);
}

static uint64_t itc_storage_read(void *opaque, hwaddr addr, unsigned size)
{
    MIPSITUState *s = (MIPSITUState *)opaque;
    ITCStorageCell *cell = get_cell(s, addr);
    ITCView view = get_itc_view(addr);
    uint64_t ret = -1;

    switch (size) {
    case 1:
    case 2:
        s->icr0 |= 1 << ITC_ICR0_ERR_AXI;
        raise_exception(EXCP_DBE);
        return 0;
    }

    switch (view) {
    case ITCVIEW_BYPASS:
        ret = view_bypass_read(cell);
        break;
    case ITCVIEW_CONTROL:
        ret = view_control_read(cell);
        break;
    case ITCVIEW_EF_SYNC:
        ret = view_ef_sync_read(cell);
        break;
    case ITCVIEW_EF_TRY:
        ret = view_ef_try_read(cell);
        break;
    case ITCVIEW_PV_SYNC:
        ret = view_pv_sync_read(cell);
        break;
    case ITCVIEW_PV_TRY:
        ret = view_pv_try_read(cell);
        break;
    case ITCVIEW_PV_ICR0:
        ret = s->icr0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "itc_storage_read: Bad ITC View %d\n", (int)view);
        break;
    }

    return ret;
}

static void itc_storage_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned size)
{
    MIPSITUState *s = (MIPSITUState *)opaque;
    ITCStorageCell *cell = get_cell(s, addr);
    ITCView view = get_itc_view(addr);

    switch (size) {
    case 1:
    case 2:
        s->icr0 |= 1 << ITC_ICR0_ERR_AXI;
        raise_exception(EXCP_DBE);
        return;
    }

    switch (view) {
    case ITCVIEW_BYPASS:
        view_bypass_write(cell, data);
        break;
    case ITCVIEW_CONTROL:
        view_control_write(cell, data);
        break;
    case ITCVIEW_EF_SYNC:
        view_ef_sync_write(cell, data);
        break;
    case ITCVIEW_EF_TRY:
        view_ef_try_write(cell, data);
        break;
    case ITCVIEW_PV_SYNC:
        view_pv_sync_write(cell);
        break;
    case ITCVIEW_PV_TRY:
        view_pv_try_write(cell);
        break;
    case ITCVIEW_PV_ICR0:
        if (data & 0x7) {
            /* clear ERROR bits */
            s->icr0 &= ~(data & 0x7);
        }
        /* set BLK_GRAIN */
        s->icr0 &= ~0x700;
        s->icr0 |= data & 0x700;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "itc_storage_write: Bad ITC View %d\n", (int)view);
        break;
    }

}

static const MemoryRegionOps itc_storage_ops = {
    .read = itc_storage_read,
    .write = itc_storage_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void itc_reset_cells(MIPSITUState *s)
{
    int i;

    memset(s->cell, 0, get_num_cells(s) * sizeof(s->cell[0]));

    for (i = 0; i < s->num_fifo; i++) {
        s->cell[i].tag.E = 1;
        s->cell[i].tag.FIFO = 1;
        s->cell[i].tag.FIFODepth = ITC_CELL_DEPTH_SHIFT;
    }
}

static void mips_itu_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MIPSITUState *s = MIPS_ITU(obj);

    memory_region_init_io(&s->storage_io, OBJECT(s), &itc_storage_ops, s,
                          "mips-itc-storage", ITC_STORAGE_ADDRSPACE_SZ);
    sysbus_init_mmio(sbd, &s->storage_io);

    memory_region_init_io(&s->tag_io, OBJECT(s), &itc_tag_ops, s,
                          "mips-itc-tag", ITC_TAG_ADDRSPACE_SZ);
}

static void mips_itu_realize(DeviceState *dev, Error **errp)
{
    MIPSITUState *s = MIPS_ITU(dev);

    if (s->num_fifo > ITC_FIFO_NUM_MAX) {
        error_setg(errp, "Exceed maximum number of FIFO cells: %d",
                   s->num_fifo);
        return;
    }
    if (s->num_semaphores > ITC_SEMAPH_NUM_MAX) {
        error_setg(errp, "Exceed maximum number of Semaphore cells: %d",
                   s->num_semaphores);
        return;
    }

    s->cell = g_new(ITCStorageCell, get_num_cells(s));
}

static void mips_itu_reset(DeviceState *dev)
{
    MIPSITUState *s = MIPS_ITU(dev);

    if (s->saar_present) {
        *(uint64_t *) s->saar = 0x11 << 1;
        s->icr0 = get_num_cells(s) << ITC_ICR0_CELL_NUM;
    } else {
        s->ITCAddressMap[0] = 0;
        s->ITCAddressMap[1] =
            ((ITC_STORAGE_ADDRSPACE_SZ - 1) & ITC_AM1_ADDR_MASK_MASK) |
            (get_num_cells(s) << ITC_AM1_NUMENTRIES_OFS);
    }
    itc_reconfigure(s);

    itc_reset_cells(s);
}

static Property mips_itu_properties[] = {
    DEFINE_PROP_INT32("num-fifo", MIPSITUState, num_fifo,
                      ITC_FIFO_NUM_MAX),
    DEFINE_PROP_INT32("num-semaphores", MIPSITUState, num_semaphores,
                      ITC_SEMAPH_NUM_MAX),
    DEFINE_PROP_BOOL("saar-present", MIPSITUState, saar_present, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void mips_itu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, mips_itu_properties);
    dc->realize = mips_itu_realize;
    dc->reset = mips_itu_reset;
}

static const TypeInfo mips_itu_info = {
    .name          = TYPE_MIPS_ITU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MIPSITUState),
    .instance_init = mips_itu_init,
    .class_init    = mips_itu_class_init,
};

static void mips_itu_register_types(void)
{
    type_register_static(&mips_itu_info);
}

type_init(mips_itu_register_types)
