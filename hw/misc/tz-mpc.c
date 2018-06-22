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
#include "qapi/error.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/tz-mpc.h"

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
REG32(BLK_MAX, 0x10)
REG32(BLK_CFG, 0x14)
REG32(BLK_IDX, 0x18)
REG32(BLK_LUT, 0x1c)
REG32(INT_STAT, 0x20)
REG32(INT_CLEAR, 0x24)
REG32(INT_EN, 0x28)
REG32(INT_INFO1, 0x2c)
REG32(INT_INFO2, 0x30)
REG32(INT_SET, 0x34)
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

static MemTxResult tz_mpc_reg_read(void *opaque, hwaddr addr,
                                   uint64_t *pdata,
                                   unsigned size, MemTxAttrs attrs)
{
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
            /* As we add support for registers which need expansions
             * other than zeroes we'll fill in cases here.
             */
        default:
            oldval = 0;
            break;
        }
        value = deposit32(oldval, (addr & 3) * 8, size * 8, value);
    }

    switch (offset) {
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

/* Accesses only reach these read and write functions if the MPC is
 * blocking them; non-blocked accesses go directly to the downstream
 * memory region without passing through this code.
 */
static MemTxResult tz_mpc_mem_blocked_read(void *opaque, hwaddr addr,
                                           uint64_t *pdata,
                                           unsigned size, MemTxAttrs attrs)
{
    trace_tz_mpc_mem_blocked_read(addr, size, attrs.secure);

    *pdata = 0;
    return MEMTX_OK;
}

static MemTxResult tz_mpc_mem_blocked_write(void *opaque, hwaddr addr,
                                            uint64_t value,
                                            unsigned size, MemTxAttrs attrs)
{
    trace_tz_mpc_mem_blocked_write(addr, value, size, attrs.secure);

    return MEMTX_OK;
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
     * For the moment, always permit accesses.
     */
    ok = true;

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
     * cpu_physical_memory_write_rom() for initial image load, and we want
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
}

static const VMStateDescription tz_mpc_vmstate = {
    .name = "tz-mpc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
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
    dc->props = tz_mpc_properties;
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
