/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * ASPEED APB-OPB FSI interface
 * IBM On-chip Peripheral Bus
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "trace.h"

#include "hw/fsi/aspeed_apb2opb.h"
#include "hw/qdev-core.h"

#define TO_REG(x) (x >> 2)

#define APB2OPB_VERSION                    TO_REG(0x00)
#define APB2OPB_TRIGGER                    TO_REG(0x04)

#define APB2OPB_CONTROL                    TO_REG(0x08)
#define   APB2OPB_CONTROL_OFF              BE_GENMASK(31, 13)

#define APB2OPB_OPB2FSI                    TO_REG(0x0c)
#define   APB2OPB_OPB2FSI_OFF              BE_GENMASK(31, 22)

#define APB2OPB_OPB0_SEL                   TO_REG(0x10)
#define APB2OPB_OPB1_SEL                   TO_REG(0x28)
#define   APB2OPB_OPB_SEL_EN               BIT(0)

#define APB2OPB_OPB0_MODE                  TO_REG(0x14)
#define APB2OPB_OPB1_MODE                  TO_REG(0x2c)
#define   APB2OPB_OPB_MODE_RD              BIT(0)

#define APB2OPB_OPB0_XFER                  TO_REG(0x18)
#define APB2OPB_OPB1_XFER                  TO_REG(0x30)
#define   APB2OPB_OPB_XFER_FULL            BIT(1)
#define   APB2OPB_OPB_XFER_HALF            BIT(0)

#define APB2OPB_OPB0_ADDR                  TO_REG(0x1c)
#define APB2OPB_OPB0_WRITE_DATA            TO_REG(0x20)

#define APB2OPB_OPB1_ADDR                  TO_REG(0x34)
#define APB2OPB_OPB1_WRITE_DATA                  TO_REG(0x38)

#define APB2OPB_IRQ_STS                    TO_REG(0x48)
#define   APB2OPB_IRQ_STS_OPB1_TX_ACK      BIT(17)
#define   APB2OPB_IRQ_STS_OPB0_TX_ACK      BIT(16)

#define APB2OPB_OPB0_WRITE_WORD_ENDIAN     TO_REG(0x4c)
#define   APB2OPB_OPB0_WRITE_WORD_ENDIAN_BE 0x0011101b
#define APB2OPB_OPB0_WRITE_BYTE_ENDIAN     TO_REG(0x50)
#define   APB2OPB_OPB0_WRITE_BYTE_ENDIAN_BE 0x0c330f3f
#define APB2OPB_OPB1_WRITE_WORD_ENDIAN     TO_REG(0x54)
#define APB2OPB_OPB1_WRITE_BYTE_ENDIAN     TO_REG(0x58)
#define APB2OPB_OPB0_READ_BYTE_ENDIAN      TO_REG(0x5c)
#define APB2OPB_OPB1_READ_BYTE_ENDIAN      TO_REG(0x60)
#define   APB2OPB_OPB0_READ_WORD_ENDIAN_BE  0x00030b1b

#define APB2OPB_OPB0_READ_DATA         TO_REG(0x84)
#define APB2OPB_OPB1_READ_DATA         TO_REG(0x90)

/*
 * The following magic values came from AST2600 data sheet
 * The register values are defined under section "FSI controller"
 * as initial values.
 */
static const uint32_t aspeed_apb2opb_reset[ASPEED_APB2OPB_NR_REGS] = {
     [APB2OPB_VERSION]                = 0x000000a1,
     [APB2OPB_OPB0_WRITE_WORD_ENDIAN] = 0x0044eee4,
     [APB2OPB_OPB0_WRITE_BYTE_ENDIAN] = 0x0055aaff,
     [APB2OPB_OPB1_WRITE_WORD_ENDIAN] = 0x00117717,
     [APB2OPB_OPB1_WRITE_BYTE_ENDIAN] = 0xffaa5500,
     [APB2OPB_OPB0_READ_BYTE_ENDIAN]  = 0x0044eee4,
     [APB2OPB_OPB1_READ_BYTE_ENDIAN]  = 0x00117717
};

static void fsi_opb_fsi_master_address(FSIMasterState *fsi, hwaddr addr)
{
    memory_region_transaction_begin();
    memory_region_set_address(&fsi->iomem, addr);
    memory_region_transaction_commit();
}

static void fsi_opb_opb2fsi_address(FSIMasterState *fsi, hwaddr addr)
{
    memory_region_transaction_begin();
    memory_region_set_address(&fsi->opb2fsi, addr);
    memory_region_transaction_commit();
}

static uint64_t fsi_aspeed_apb2opb_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(opaque);
    unsigned int reg = TO_REG(addr);

    trace_fsi_aspeed_apb2opb_read(addr, size);

    if (reg >= ASPEED_APB2OPB_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds read: 0x%"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return 0;
    }

    return s->regs[reg];
}

static MemTxResult fsi_aspeed_apb2opb_rw(AddressSpace *as, hwaddr addr,
                                         MemTxAttrs attrs, uint32_t *data,
                                         uint32_t size, bool is_write)
{
    MemTxResult res;

    if (is_write) {
        switch (size) {
        case 4:
            address_space_stl_le(as, addr, *data, attrs, &res);
            break;
        case 2:
            address_space_stw_le(as, addr, *data, attrs, &res);
            break;
        case 1:
            address_space_stb(as, addr, *data, attrs, &res);
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        switch (size) {
        case 4:
            *data = address_space_ldl_le(as, addr, attrs, &res);
            break;
        case 2:
            *data = address_space_lduw_le(as, addr, attrs, &res);
            break;
        case 1:
            *data = address_space_ldub(as, addr, attrs, &res);
            break;
        default:
            g_assert_not_reached();
        }
    }
    return res;
}

static void fsi_aspeed_apb2opb_write(void *opaque, hwaddr addr, uint64_t data,
                                     unsigned size)
{
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(opaque);
    unsigned int reg = TO_REG(addr);

    trace_fsi_aspeed_apb2opb_write(addr, size, data);

    if (reg >= ASPEED_APB2OPB_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds write: %"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return;
    }

    switch (reg) {
    case APB2OPB_CONTROL:
        fsi_opb_fsi_master_address(&s->fsi[0],
                data & APB2OPB_CONTROL_OFF);
        break;
    case APB2OPB_OPB2FSI:
        fsi_opb_opb2fsi_address(&s->fsi[0],
                data & APB2OPB_OPB2FSI_OFF);
        break;
    case APB2OPB_OPB0_WRITE_WORD_ENDIAN:
        if (data != APB2OPB_OPB0_WRITE_WORD_ENDIAN_BE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bridge needs to be driven as BE (0x%x)\n",
                          __func__, APB2OPB_OPB0_WRITE_WORD_ENDIAN_BE);
        }
        break;
    case APB2OPB_OPB0_WRITE_BYTE_ENDIAN:
        if (data != APB2OPB_OPB0_WRITE_BYTE_ENDIAN_BE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bridge needs to be driven as BE (0x%x)\n",
                          __func__, APB2OPB_OPB0_WRITE_BYTE_ENDIAN_BE);
        }
        break;
    case APB2OPB_OPB0_READ_BYTE_ENDIAN:
        if (data != APB2OPB_OPB0_READ_WORD_ENDIAN_BE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bridge needs to be driven as BE (0x%x)\n",
                          __func__, APB2OPB_OPB0_READ_WORD_ENDIAN_BE);
        }
        break;
    case APB2OPB_TRIGGER:
    {
        uint32_t opb, op_mode, op_size, op_addr, op_data;
        MemTxResult result;
        bool is_write;
        int index;
        AddressSpace *as;

        assert((s->regs[APB2OPB_OPB0_SEL] & APB2OPB_OPB_SEL_EN) ^
               (s->regs[APB2OPB_OPB1_SEL] & APB2OPB_OPB_SEL_EN));

        if (s->regs[APB2OPB_OPB0_SEL] & APB2OPB_OPB_SEL_EN) {
            opb = 0;
            op_mode = s->regs[APB2OPB_OPB0_MODE];
            op_size = s->regs[APB2OPB_OPB0_XFER];
            op_addr = s->regs[APB2OPB_OPB0_ADDR];
            op_data = s->regs[APB2OPB_OPB0_WRITE_DATA];
        } else if (s->regs[APB2OPB_OPB1_SEL] & APB2OPB_OPB_SEL_EN) {
            opb = 1;
            op_mode = s->regs[APB2OPB_OPB1_MODE];
            op_size = s->regs[APB2OPB_OPB1_XFER];
            op_addr = s->regs[APB2OPB_OPB1_ADDR];
            op_data = s->regs[APB2OPB_OPB1_WRITE_DATA];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Invalid operation: 0x%"HWADDR_PRIx" for %u\n",
                          __func__, addr, size);
            return;
        }

        if (op_size & ~(APB2OPB_OPB_XFER_HALF | APB2OPB_OPB_XFER_FULL)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "OPB transaction failed: Unrecognized access width: %d\n",
                          op_size);
            return;
        }

        op_size += 1;
        is_write = !(op_mode & APB2OPB_OPB_MODE_RD);
        index = opb ? APB2OPB_OPB1_READ_DATA : APB2OPB_OPB0_READ_DATA;
        as = &s->opb[opb].as;

        result = fsi_aspeed_apb2opb_rw(as, op_addr, MEMTXATTRS_UNSPECIFIED,
                                       &op_data, op_size, is_write);
        if (result != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: OPB %s failed @%08x\n",
                          __func__, is_write ? "write" : "read", op_addr);
            return;
        }

        if (!is_write) {
            s->regs[index] = op_data;
        }

        s->regs[APB2OPB_IRQ_STS] |= opb ? APB2OPB_IRQ_STS_OPB1_TX_ACK
            : APB2OPB_IRQ_STS_OPB0_TX_ACK;
        break;
    }
    }

    s->regs[reg] = data;
}

static const struct MemoryRegionOps aspeed_apb2opb_ops = {
    .read = fsi_aspeed_apb2opb_read,
    .write = fsi_aspeed_apb2opb_write,
    .valid.max_access_size = 4,
    .valid.min_access_size = 4,
    .impl.max_access_size = 4,
    .impl.min_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void fsi_aspeed_apb2opb_init(Object *o)
{
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(o);
    int i;

    for (i = 0; i < ASPEED_FSI_NUM; i++) {
        object_initialize_child(o, "fsi-master[*]", &s->fsi[i],
                                TYPE_FSI_MASTER);
    }
}

static void fsi_aspeed_apb2opb_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(dev);
    int i;

    /*
     * TODO: The OPBus model initializes the OPB address space in
     * the .instance_init handler and this is problematic for test
     * device-introspect-test. To avoid a memory corruption and a QEMU
     * crash, qbus_init() should be called from realize(). Something to
     * improve. Possibly, OPBus could also be removed.
     */
    for (i = 0; i < ASPEED_FSI_NUM; i++) {
        qbus_init(&s->opb[i], sizeof(s->opb[i]), TYPE_OP_BUS, DEVICE(s),
                  NULL);
    }

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_apb2opb_ops, s,
                          TYPE_ASPEED_APB2OPB, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    for (i = 0; i < ASPEED_FSI_NUM; i++) {
        if (!qdev_realize(DEVICE(&s->fsi[i]), BUS(&s->opb[i]), errp)) {
            return;
        }

        memory_region_add_subregion(&s->opb[i].mr, 0x80000000,
                &s->fsi[i].iomem);

        memory_region_add_subregion(&s->opb[i].mr, 0xa0000000,
                &s->fsi[i].opb2fsi);
    }
}

static void fsi_aspeed_apb2opb_reset(DeviceState *dev)
{
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(dev);

    memcpy(s->regs, aspeed_apb2opb_reset, ASPEED_APB2OPB_NR_REGS);
}

static void fsi_aspeed_apb2opb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "ASPEED APB2OPB Bridge";
    dc->realize = fsi_aspeed_apb2opb_realize;
    device_class_set_legacy_reset(dc, fsi_aspeed_apb2opb_reset);
}

static const TypeInfo aspeed_apb2opb_info = {
    .name = TYPE_ASPEED_APB2OPB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = fsi_aspeed_apb2opb_init,
    .instance_size = sizeof(AspeedAPB2OPBState),
    .class_init = fsi_aspeed_apb2opb_class_init,
};

static void aspeed_apb2opb_register_types(void)
{
    type_register_static(&aspeed_apb2opb_info);
}

type_init(aspeed_apb2opb_register_types);

static void fsi_opb_init(Object *o)
{
    OPBus *opb = OP_BUS(o);

    memory_region_init(&opb->mr, 0, TYPE_FSI_OPB, UINT32_MAX);
    address_space_init(&opb->as, &opb->mr, TYPE_FSI_OPB);
}

static const TypeInfo opb_info = {
    .name = TYPE_OP_BUS,
    .parent = TYPE_BUS,
    .instance_init = fsi_opb_init,
    .instance_size = sizeof(OPBus),
};

static void fsi_opb_register_types(void)
{
    type_register_static(&opb_info);
}

type_init(fsi_opb_register_types);
