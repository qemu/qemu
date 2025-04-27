/*
 * Aspeed PECI Controller
 *
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * This code is licensed under the GPL version 2 or later. See the COPYING
 * file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/misc/aspeed_peci.h"
#include "hw/registerfields.h"
#include "trace.h"

#define ASPEED_PECI_CC_RSP_SUCCESS (0x40U)

/* Command Register */
REG32(PECI_CMD, 0x08)
    FIELD(PECI_CMD, FIRE, 0, 1)

/* Interrupt Control Register */
REG32(PECI_INT_CTRL, 0x18)

/* Interrupt Status Register */
REG32(PECI_INT_STS, 0x1C)
    FIELD(PECI_INT_STS, CMD_DONE, 0, 1)

/* Rx/Tx Data Buffer Registers */
REG32(PECI_WR_DATA0, 0x20)
REG32(PECI_RD_DATA0, 0x30)

static void aspeed_peci_raise_interrupt(AspeedPECIState *s, uint32_t status)
{
    trace_aspeed_peci_raise_interrupt(s->regs[R_PECI_INT_CTRL], status);

    s->regs[R_PECI_INT_STS] = s->regs[R_PECI_INT_CTRL] & status;
    if (!s->regs[R_PECI_INT_STS]) {
        return;
    }
    qemu_irq_raise(s->irq);
}

static uint64_t aspeed_peci_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedPECIState *s = ASPEED_PECI(opaque);
    uint64_t data;

    if (offset >= ASPEED_PECI_NR_REGS << 2) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
    data = s->regs[offset >> 2];

    trace_aspeed_peci_read(offset, data);
    return data;
}

static void aspeed_peci_write(void *opaque, hwaddr offset, uint64_t data,
                              unsigned size)
{
    AspeedPECIState *s = ASPEED_PECI(opaque);

    trace_aspeed_peci_write(offset, data);

    if (offset >= ASPEED_PECI_NR_REGS << 2) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    switch (offset) {
    case A_PECI_INT_STS:
        s->regs[R_PECI_INT_STS] &= ~data;
        if (!s->regs[R_PECI_INT_STS]) {
            qemu_irq_lower(s->irq);
        }
        break;
    case A_PECI_CMD:
        /*
         * Only the FIRE bit is writable. Once the command is complete, it
         * should be cleared. Since we complete the command immediately, the
         * value is not stored in the register array.
         */
        if (!FIELD_EX32(data, PECI_CMD, FIRE)) {
            break;
        }
        if (s->regs[R_PECI_INT_STS]) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Interrupt status must be "
                          "cleared before firing another command: 0x%08x\n",
                          __func__, s->regs[R_PECI_INT_STS]);
            break;
        }
        s->regs[R_PECI_RD_DATA0] = ASPEED_PECI_CC_RSP_SUCCESS;
        s->regs[R_PECI_WR_DATA0] = ASPEED_PECI_CC_RSP_SUCCESS;
        aspeed_peci_raise_interrupt(s,
                                    FIELD_DP32(0, PECI_INT_STS, CMD_DONE, 1));
        break;
    default:
        s->regs[offset / sizeof(s->regs[0])] = data;
        break;
    }
}

static const MemoryRegionOps aspeed_peci_ops = {
    .read = aspeed_peci_read,
    .write = aspeed_peci_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void aspeed_peci_realize(DeviceState *dev, Error **errp)
{
    AspeedPECIState *s = ASPEED_PECI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &aspeed_peci_ops, s,
                          TYPE_ASPEED_PECI, 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static void aspeed_peci_reset(DeviceState *dev)
{
    AspeedPECIState *s = ASPEED_PECI(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void aspeed_peci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_peci_realize;
    device_class_set_legacy_reset(dc, aspeed_peci_reset);
    dc->desc = "Aspeed PECI Controller";
}

static const TypeInfo aspeed_peci_types[] = {
    {
        .name = TYPE_ASPEED_PECI,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AspeedPECIState),
        .class_init = aspeed_peci_class_init,
        .abstract = false,
    },
};

DEFINE_TYPES(aspeed_peci_types);
