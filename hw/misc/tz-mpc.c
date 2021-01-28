/*
 * ARM AHB5 TrustZone Memory Protection Controller emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/misc/tz-mpc.h"
#include "hw/qdev-properties.h"

/* Our IOMMU has two IOMMU indexes, one for secure transactions and one for
 * non-secure transactions.
 */
enum {
    IOMMU_IDX_S,
    IOMMU_IDX_NS,
    IOMMU_NUM_INDEXES,
};

/* Config registers */
REG32(CTRL, 0x00)
    FIELD(CTRL, SEC_RESP, 4, 1)
    FIELD(CTRL, AUTOINC, 8, 1)
    FIELD(CTRL, LOCKDOWN, 31, 1)
REG32(BLK_MAX, 0x10)
REG32(BLK_CFG, 0x14)
REG32(BLK_IDX, 0x18)
REG32(BLK_LUT, 0x1c)
REG32(INT_STAT, 0x20)
    FIELD(INT_STAT, IRQ, 0, 1)
REG32(INT_CLEAR, 0x24)
    FIELD(INT_CLEAR, IRQ, 0, 1)
REG32(INT_EN, 0x28)
    FIELD(INT_EN, IRQ, 0, 1)
REG32(INT_INFO1, 0x2c)
REG32(INT_INFO2, 0x30)
    FIELD(INT_INFO2, HMASTER, 0, 16)
    FIELD(INT_INFO2, HNONSEC, 16, 1)
    FIELD(INT_INFO2, CFG_NS, 17, 1)
REG32(INT_SET, 0x34)
    FIELD(INT_SET, IRQ, 0, 1)
REG32(PIDR4, 0xfd0)
REG32(PIDR5, 0xfd4)
REG32(PIDR6, 0xfd8)
REG32(PIDR7, 0xfdc)
REG32(PIDR0, 0xfe0)
REG32(PIDR1, 0xfe4)
REG32(PIDR2, 0xfe8)
REG32(PIDR3, 0xfec)
REG32(CIDR0, 0xff0)
REG32(CIDR1, 0xff4)
REG32(CIDR2, 0xff8)
REG32(CIDR3, 0xffc)

static const uint8_t tz_mpc_idregs[] = {
    0x04, 0x00, 0x00, 0x00,
    0x60, 0xb8, 0x1b, 0x00,
    0x0d, 0xf0, 0x05, 0xb1,
};

static void tz_mpc_irq_update(TZMPC *s)
{
    qemu_set_irq(s->irq, s->int_stat && s->int_en);
}

static void tz_mpc_iommu_notify(TZMPC *s, uint32_t lutidx,
                                uint32_t oldlut, uint32_t newlut)
{
    /* Called when the LUT word at lutidx has changed from oldlut to newlut;
     * must call the IOMMU notifiers for the changed blocks.
     */
    IOMMUTLBEvent event = {
        .entry = {
            .addr_mask = s->blocksize - 1,
        }
    };
    hwaddr addr = lutidx * s->blocksize * 32;
    int i;

    for (i = 0; i < 32; i++, addr += s->blocksize) {
        bool block_is_ns;

        if (!((oldlut ^ newlut) & (1 << i))) {
            continue;
        }
        /* This changes the mappings for both the S and the NS space,
         * so we need to do four notifies: an UNMAP then a MAP for each.
         */
        block_is_ns = newlut & (1 << i);

        trace_tz_mpc_iommu_notify(addr);
        event.entry.iova = addr;
        event.entry.translated_addr = addr;

        event.type = IOMMU_NOTIFIER_UNMAP;
        event.entry.perm = IOMMU_NONE;
        memory_region_notify_iommu(&s->upstream, IOMMU_IDX_S, event);
        memory_region_notify_iommu(&s->upstream, IOMMU_IDX_NS, event);

        event.type = IOMMU_NOTIFIER_MAP;
        event.entry.perm = IOMMU_RW;
        if (block_is_ns) {
            event.entry.target_as = &s->blocked_io_as;
        } else {
            event.entry.target_as = &s->downstream_as;
        }
        memory_region_notify_iommu(&s->upstream, IOMMU_IDX_S, event);
        if (block_is_ns) {
            event.entry.target_as = &s->downstream_as;
        } else {
            event.entry.target_as = &s->blocked_io_as;
        }
        memory_region_notify_iommu(&s->upstream, IOMMU_IDX_NS, event);
    }
}

static void tz_mpc_autoinc_idx(TZMPC *s, unsigned access_size)
{
    /* Auto-increment BLK_IDX if necessary */
    if (access_size == 4 && (s->ctrl & R_CTRL_AUTOINC_MASK)) {
        s->blk_idx++;
        s->blk_idx %= s->blk_max;
    }
}

static MemTxResult tz_mpc_reg_read(void *opaque, hwaddr addr,
                                   uint64_t *pdata,
                                   unsigned size, MemTxAttrs attrs)
{
    TZMPC *s = TZ_MPC(opaque);
    uint64_t r;
    uint32_t offset = addr & ~0x3;

    if (!attrs.secure && offset < A_PIDR4) {
        /* NS accesses can only see the ID registers */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TZ MPC register read: NS access to offset 0x%x\n",
                      offset);
        r = 0;
        goto read_out;
    }

    switch (offset) {
    case A_CTRL:
        r = s->ctrl;
        break;
    case A_BLK_MAX:
        r = s->blk_max - 1;
        break;
    case A_BLK_CFG:
        /* We are never in "init in progress state", so this just indicates
         * the block size. s->blocksize == (1 << BLK_CFG + 5), so
         * BLK_CFG == ctz32(s->blocksize) - 5
         */
        r = ctz32(s->blocksize) - 5;
        break;
    case A_BLK_IDX:
        r = s->blk_idx;
        break;
    case A_BLK_LUT:
        r = s->blk_lut[s->blk_idx];
        tz_mpc_autoinc_idx(s, size);
        break;
    case A_INT_STAT:
        r = s->int_stat;
        break;
    case A_INT_EN:
        r = s->int_en;
        break;
    case A_INT_INFO1:
        r = s->int_info1;
        break;
    case A_INT_INFO2:
        r = s->int_info2;
        break;
    case A_PIDR4:
    case A_PIDR5:
    case A_PIDR6:
    case A_PIDR7:
    case A_PIDR0:
    case A_PIDR1:
    case A_PIDR2:
    case A_PIDR3:
    case A_CIDR0:
    case A_CIDR1:
    case A_CIDR2:
    case A_CIDR3:
        r = tz_mpc_idregs[(offset - A_PIDR4) / 4];
        break;
    case A_INT_CLEAR:
    case A_INT_SET:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TZ MPC register read: write-only offset 0x%x\n",
                      offset);
        r = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TZ MPC register read: bad offset 0x%x\n", offset);
        r = 0;
        break;
    }

    if (size != 4) {
        /* None of our registers are read-sensitive (except BLK_LUT,
         * which can special case the "size not 4" case), so just
         * pull the right bytes out of the word read result.
         */
        r = extract32(r, (addr & 3) * 8, size * 8);
    }

read_out:
    trace_tz_mpc_reg_read(addr, r, size);
    *pdata = r;
    return MEMTX_OK;
}

static MemTxResult tz_mpc_reg_write(void *opaque, hwaddr addr,
                                    uint64_t value,
                                    unsigned size, MemTxAttrs attrs)
{
    TZMPC *s = TZ_MPC(opaque);
    uint32_t offset = addr & ~0x3;

    trace_tz_mpc_reg_write(addr, value, size);

    if (!attrs.secure && offset < A_PIDR4) {
        /* NS accesses can only see the ID registers */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TZ MPC register write: NS access to offset 0x%x\n",
                      offset);
        return MEMTX_OK;
    }

    if (size != 4) {
        /* Expand the byte or halfword write to a full word size.
         * In most cases we can do this with zeroes; the exceptions
         * are CTRL, BLK_IDX and BLK_LUT.
         */
        uint32_t oldval;

        switch (offset) {
        case A_CTRL:
            oldval = s->ctrl;
            break;
        case A_BLK_IDX:
            oldval = s->blk_idx;
            break;
        case A_BLK_LUT:
            oldval = s->blk_lut[s->blk_idx];
            break;
        default:
            oldval = 0;
            break;
        }
        value = deposit32(oldval, (addr & 3) * 8, size * 8, value);
    }

    if ((s->ctrl & R_CTRL_LOCKDOWN_MASK) &&
        (offset == A_CTRL || offset == A_BLK_LUT || offset == A_INT_EN)) {
        /* Lockdown mode makes these three registers read-only, and
         * the only way out of it is to reset the device.
         */
        qemu_log_mask(LOG_GUEST_ERROR, "TZ MPC register write to offset 0x%x "
                      "while MPC is in lockdown mode\n", offset);
        return MEMTX_OK;
    }

    switch (offset) {
    case A_CTRL:
        /* We don't implement the 'data gating' feature so all other bits
         * are reserved and we make them RAZ/WI.
         */
        s->ctrl = value & (R_CTRL_SEC_RESP_MASK |
                           R_CTRL_AUTOINC_MASK |
                           R_CTRL_LOCKDOWN_MASK);
        break;
    case A_BLK_IDX:
        s->blk_idx = value % s->blk_max;
        break;
    case A_BLK_LUT:
        tz_mpc_iommu_notify(s, s->blk_idx, s->blk_lut[s->blk_idx], value);
        s->blk_lut[s->blk_idx] = value;
        tz_mpc_autoinc_idx(s, size);
        break;
    case A_INT_CLEAR:
        if (value & R_INT_CLEAR_IRQ_MASK) {
            s->int_stat = 0;
            tz_mpc_irq_update(s);
        }
        break;
    case A_INT_EN:
        s->int_en = value & R_INT_EN_IRQ_MASK;
        tz_mpc_irq_update(s);
        break;
    case A_INT_SET:
        if (value & R_INT_SET_IRQ_MASK) {
            s->int_stat = R_INT_STAT_IRQ_MASK;
            tz_mpc_irq_update(s);
        }
        break;
    case A_PIDR4:
    case A_PIDR5:
    case A_PIDR6:
    case A_PIDR7:
    case A_PIDR0:
    case A_PIDR1:
    case A_PIDR2:
    case A_PIDR3:
    case A_CIDR0:
    case A_CIDR1:
    case A_CIDR2:
    case A_CIDR3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TZ MPC register write: read-only offset 0x%x\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TZ MPC register write: bad offset 0x%x\n", offset);
        break;
    }

    return MEMTX_OK;
}

static const MemoryRegionOps tz_mpc_reg_ops = {
    .read_with_attrs = tz_mpc_reg_read,
    .write_with_attrs = tz_mpc_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static inline bool tz_mpc_cfg_ns(TZMPC *s, hwaddr addr)
{
    /* Return the cfg_ns bit from the LUT for the specified address */
    hwaddr blknum = addr / s->blocksize;
    hwaddr blkword = blknum / 32;
    uint32_t blkbit = 1U << (blknum % 32);

    /* This would imply the address was larger than the size we
     * defined this memory region to be, so it can't happen.
     */
    assert(blkword < s->blk_max);
    return s->blk_lut[blkword] & blkbit;
}

static MemTxResult tz_mpc_handle_block(TZMPC *s, hwaddr addr, MemTxAttrs attrs)
{
    /* Handle a blocked transaction: raise IRQ, capture info, etc */
    if (!s->int_stat) {
        /* First blocked transfer: capture information into INT_INFO1 and
         * INT_INFO2. Subsequent transfers are still blocked but don't
         * capture information until the guest clears the interrupt.
         */

        s->int_info1 = addr;
        s->int_info2 = 0;
        s->int_info2 = FIELD_DP32(s->int_info2, INT_INFO2, HMASTER,
                                  attrs.requester_id & 0xffff);
        s->int_info2 = FIELD_DP32(s->int_info2, INT_INFO2, HNONSEC,
                                  ~attrs.secure);
        s->int_info2 = FIELD_DP32(s->int_info2, INT_INFO2, CFG_NS,
                                  tz_mpc_cfg_ns(s, addr));
        s->int_stat |= R_INT_STAT_IRQ_MASK;
        tz_mpc_irq_update(s);
    }

    /* Generate bus error if desired; otherwise RAZ/WI */
    return (s->ctrl & R_CTRL_SEC_RESP_MASK) ? MEMTX_ERROR : MEMTX_OK;
}

/* Accesses only reach these read and write functions if the MPC is
 * blocking them; non-blocked accesses go directly to the downstream
 * memory region without passing through this code.
 */
static MemTxResult tz_mpc_mem_blocked_read(void *opaque, hwaddr addr,
                                           uint64_t *pdata,
                                           unsigned size, MemTxAttrs attrs)
{
    TZMPC *s = TZ_MPC(opaque);

    trace_tz_mpc_mem_blocked_read(addr, size, attrs.secure);

    *pdata = 0;
    return tz_mpc_handle_block(s, addr, attrs);
}

static MemTxResult tz_mpc_mem_blocked_write(void *opaque, hwaddr addr,
                                            uint64_t value,
                                            unsigned size, MemTxAttrs attrs)
{
    TZMPC *s = TZ_MPC(opaque);

    trace_tz_mpc_mem_blocked_write(addr, value, size, attrs.secure);

    return tz_mpc_handle_block(s, addr, attrs);
}

static const MemoryRegionOps tz_mpc_mem_blocked_ops = {
    .read_with_attrs = tz_mpc_mem_blocked_read,
    .write_with_attrs = tz_mpc_mem_blocked_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
};

static IOMMUTLBEntry tz_mpc_translate(IOMMUMemoryRegion *iommu,
                                      hwaddr addr, IOMMUAccessFlags flags,
                                      int iommu_idx)
{
    TZMPC *s = TZ_MPC(container_of(iommu, TZMPC, upstream));
    bool ok;

    IOMMUTLBEntry ret = {
        .iova = addr & ~(s->blocksize - 1),
        .translated_addr = addr & ~(s->blocksize - 1),
        .addr_mask = s->blocksize - 1,
        .perm = IOMMU_RW,
    };

    /* Look at the per-block configuration for this address, and
     * return a TLB entry directing the transaction at either
     * downstream_as or blocked_io_as, as appropriate.
     * If the LUT cfg_ns bit is 1, only non-secure transactions
     * may pass. If the bit is 0, only secure transactions may pass.
     */
    ok = tz_mpc_cfg_ns(s, addr) == (iommu_idx == IOMMU_IDX_NS);

    trace_tz_mpc_translate(addr, flags,
                           iommu_idx == IOMMU_IDX_S ? "S" : "NS",
                           ok ? "pass" : "block");

    ret.target_as = ok ? &s->downstream_as : &s->blocked_io_as;
    return ret;
}

static int tz_mpc_attrs_to_index(IOMMUMemoryRegion *iommu, MemTxAttrs attrs)
{
    /* We treat unspecified attributes like secure. Transactions with
     * unspecified attributes come from places like
     * rom_reset() for initial image load, and we want
     * those to pass through the from-reset "everything is secure" config.
     * All the real during-emulation transactions from the CPU will
     * specify attributes.
     */
    return (attrs.unspecified || attrs.secure) ? IOMMU_IDX_S : IOMMU_IDX_NS;
}

static int tz_mpc_num_indexes(IOMMUMemoryRegion *iommu)
{
    return IOMMU_NUM_INDEXES;
}

static void tz_mpc_reset(DeviceState *dev)
{
    TZMPC *s = TZ_MPC(dev);

    s->ctrl = 0x00000100;
    s->blk_idx = 0;
    s->int_stat = 0;
    s->int_en = 1;
    s->int_info1 = 0;
    s->int_info2 = 0;

    memset(s->blk_lut, 0, s->blk_max * sizeof(uint32_t));
}

static void tz_mpc_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    TZMPC *s = TZ_MPC(obj);

    qdev_init_gpio_out_named(dev, &s->irq, "irq", 1);
}

static void tz_mpc_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    TZMPC *s = TZ_MPC(dev);
    uint64_t size;

    /* We can't create the upstream end of the port until realize,
     * as we don't know the size of the MR used as the downstream until then.
     * We insist on having a downstream, to avoid complicating the code
     * with handling the "don't know how big this is" case. It's easy
     * enough for the user to create an unimplemented_device as downstream
     * if they have nothing else to plug into this.
     */
    if (!s->downstream) {
        error_setg(errp, "MPC 'downstream' link not set");
        return;
    }

    size = memory_region_size(s->downstream);

    memory_region_init_iommu(&s->upstream, sizeof(s->upstream),
                             TYPE_TZ_MPC_IOMMU_MEMORY_REGION,
                             obj, "tz-mpc-upstream", size);

    /* In real hardware the block size is configurable. In QEMU we could
     * make it configurable but will need it to be at least as big as the
     * target page size so we can execute out of the resulting MRs. Guest
     * software is supposed to check the block size using the BLK_CFG
     * register, so make it fixed at the page size.
     */
    s->blocksize = memory_region_iommu_get_min_page_size(&s->upstream);
    if (size % s->blocksize != 0) {
        error_setg(errp,
                   "MPC 'downstream' size %" PRId64
                   " is not a multiple of %" HWADDR_PRIx " bytes",
                   size, s->blocksize);
        object_unref(OBJECT(&s->upstream));
        return;
    }

    /* BLK_MAX is the max value of BLK_IDX, which indexes an array of 32-bit
     * words, each bit of which indicates one block.
     */
    s->blk_max = DIV_ROUND_UP(size / s->blocksize, 32);

    memory_region_init_io(&s->regmr, obj, &tz_mpc_reg_ops,
                          s, "tz-mpc-regs", 0x1000);
    sysbus_init_mmio(sbd, &s->regmr);

    sysbus_init_mmio(sbd, MEMORY_REGION(&s->upstream));

    /* This memory region is not exposed to users of this device as a
     * sysbus MMIO region, but is instead used internally as something
     * that our IOMMU translate function might direct accesses to.
     */
    memory_region_init_io(&s->blocked_io, obj, &tz_mpc_mem_blocked_ops,
                          s, "tz-mpc-blocked-io", size);

    address_space_init(&s->downstream_as, s->downstream,
                       "tz-mpc-downstream");
    address_space_init(&s->blocked_io_as, &s->blocked_io,
                       "tz-mpc-blocked-io");

    s->blk_lut = g_new0(uint32_t, s->blk_max);
}

static int tz_mpc_post_load(void *opaque, int version_id)
{
    TZMPC *s = TZ_MPC(opaque);

    /* Check the incoming data doesn't point blk_idx off the end of blk_lut. */
    if (s->blk_idx >= s->blk_max) {
        return -1;
    }
    return 0;
}

static const VMStateDescription tz_mpc_vmstate = {
    .name = "tz-mpc",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = tz_mpc_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl, TZMPC),
        VMSTATE_UINT32(blk_idx, TZMPC),
        VMSTATE_UINT32(int_stat, TZMPC),
        VMSTATE_UINT32(int_en, TZMPC),
        VMSTATE_UINT32(int_info1, TZMPC),
        VMSTATE_UINT32(int_info2, TZMPC),
        VMSTATE_VARRAY_UINT32(blk_lut, TZMPC, blk_max,
                              0, vmstate_info_uint32, uint32_t),
        VMSTATE_END_OF_LIST()
    }
};

static Property tz_mpc_properties[] = {
    DEFINE_PROP_LINK("downstream", TZMPC, downstream,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void tz_mpc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tz_mpc_realize;
    dc->vmsd = &tz_mpc_vmstate;
    dc->reset = tz_mpc_reset;
    device_class_set_props(dc, tz_mpc_properties);
}

static const TypeInfo tz_mpc_info = {
    .name = TYPE_TZ_MPC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TZMPC),
    .instance_init = tz_mpc_init,
    .class_init = tz_mpc_class_init,
};

static void tz_mpc_iommu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = tz_mpc_translate;
    imrc->attrs_to_index = tz_mpc_attrs_to_index;
    imrc->num_indexes = tz_mpc_num_indexes;
}

static const TypeInfo tz_mpc_iommu_memory_region_info = {
    .name = TYPE_TZ_MPC_IOMMU_MEMORY_REGION,
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .class_init = tz_mpc_iommu_memory_region_class_init,
};

static void tz_mpc_register_types(void)
{
    type_register_static(&tz_mpc_info);
    type_register_static(&tz_mpc_iommu_memory_region_info);
}

type_init(tz_mpc_register_types);
