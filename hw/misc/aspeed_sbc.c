/*
 * ASPEED Secure Boot Controller
 *
 * Copyright (C) 2021-2022 IBM Corp.
 *
 * Joel Stanley <joel@jms.id.au>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/misc/aspeed_sbc.h"
#include "qapi/error.h"
#include "migration/vmstate.h"

#define R_PROT          (0x000 / 4)
#define R_STATUS        (0x014 / 4)

static uint64_t aspeed_sbc_read(void *opaque, hwaddr addr, unsigned int size)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);

    addr >>= 2;

    if (addr >= ASPEED_SBC_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return 0;
    }

    return s->regs[addr];
}

static void aspeed_sbc_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned int size)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);

    addr >>= 2;

    if (addr >= ASPEED_SBC_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    }

    switch (addr) {
    case R_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read only register 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    default:
        break;
    }

    s->regs[addr] = data;
}

static const MemoryRegionOps aspeed_sbc_ops = {
    .read = aspeed_sbc_read,
    .write = aspeed_sbc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_sbc_reset(DeviceState *dev)
{
    struct AspeedSBCState *s = ASPEED_SBC(dev);

    memset(s->regs, 0, sizeof(s->regs));

    /* Set secure boot enabled, and boot from emmc/spi */
    s->regs[R_STATUS] = 1 << 6 | 1 << 5;
}

static void aspeed_sbc_realize(DeviceState *dev, Error **errp)
{
    AspeedSBCState *s = ASPEED_SBC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_sbc_ops, s,
            TYPE_ASPEED_SBC, 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aspeed_sbc = {
    .name = TYPE_ASPEED_SBC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedSBCState, ASPEED_SBC_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static void aspeed_sbc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_sbc_realize;
    dc->reset = aspeed_sbc_reset;
    dc->vmsd = &vmstate_aspeed_sbc;
}

static const TypeInfo aspeed_sbc_info = {
    .name = TYPE_ASPEED_SBC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedSBCState),
    .class_init = aspeed_sbc_class_init,
    .class_size = sizeof(AspeedSBCClass)
};

static void aspeed_ast2600_sbc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "AST2600 Secure Boot Controller";
}

static const TypeInfo aspeed_ast2600_sbc_info = {
    .name = TYPE_ASPEED_AST2600_SBC,
    .parent = TYPE_ASPEED_SBC,
    .class_init = aspeed_ast2600_sbc_class_init,
};

static void aspeed_sbc_register_types(void)
{
    type_register_static(&aspeed_ast2600_sbc_info);
    type_register_static(&aspeed_sbc_info);
}

type_init(aspeed_sbc_register_types);
