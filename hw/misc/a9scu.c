/*
 * Cortex-A9MPCore Snoop Control Unit (SCU) emulation.
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2011 Linaro Limited.
 * Written by Paul Brook, Peter Maydell.
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/misc/a9scu.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define A9_SCU_CPU_MAX  4

static uint64_t a9_scu_read(void *opaque, hwaddr offset,
                            unsigned size)
{
    A9SCUState *s = (A9SCUState *)opaque;
    switch (offset) {
    case 0x00: /* Control */
        return s->control;
    case 0x04: /* Configuration */
        return (((1 << s->num_cpu) - 1) << 4) | (s->num_cpu - 1);
    case 0x08: /* CPU Power Status */
        return s->status;
    case 0x0c: /* Invalidate All Registers In Secure State */
        return 0;
    case 0x40: /* Filtering Start Address Register */
    case 0x44: /* Filtering End Address Register */
        /* RAZ/WI, like an implementation with only one AXI master */
        return 0;
    case 0x50: /* SCU Access Control Register */
    case 0x54: /* SCU Non-secure Access Control Register */
        /* unimplemented, fall through */
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unsupported offset 0x%"HWADDR_PRIx"\n",
                      __func__, offset);
        return 0;
    }
}

static void a9_scu_write(void *opaque, hwaddr offset,
                         uint64_t value, unsigned size)
{
    A9SCUState *s = (A9SCUState *)opaque;

    switch (offset) {
    case 0x00: /* Control */
        s->control = value & 1;
        break;
    case 0x4: /* Configuration: RO */
        break;
    case 0x08: case 0x09: case 0x0A: case 0x0B: /* Power Control */
        s->status = value;
        break;
    case 0x0c: /* Invalidate All Registers In Secure State */
        /* no-op as we do not implement caches */
        break;
    case 0x40: /* Filtering Start Address Register */
    case 0x44: /* Filtering End Address Register */
        /* RAZ/WI, like an implementation with only one AXI master */
        break;
    case 0x50: /* SCU Access Control Register */
    case 0x54: /* SCU Non-secure Access Control Register */
        /* unimplemented, fall through */
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unsupported offset 0x%"HWADDR_PRIx
                                 " value 0x%"PRIx64"\n",
                      __func__, offset, value);
        break;
    }
}

static const MemoryRegionOps a9_scu_ops = {
    .read = a9_scu_read,
    .write = a9_scu_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void a9_scu_reset(DeviceState *dev)
{
    A9SCUState *s = A9_SCU(dev);
    s->control = 0;
}

static void a9_scu_realize(DeviceState *dev, Error **errp)
{
    A9SCUState *s = A9_SCU(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (!s->num_cpu || s->num_cpu > A9_SCU_CPU_MAX) {
        error_setg(errp, "Illegal CPU count: %u", s->num_cpu);
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &a9_scu_ops, s,
                          "a9-scu", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_a9_scu = {
    .name = "a9-scu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(control, A9SCUState),
        VMSTATE_UINT32(status, A9SCUState),
        VMSTATE_END_OF_LIST()
    }
};

static Property a9_scu_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", A9SCUState, num_cpu, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void a9_scu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, a9_scu_properties);
    dc->vmsd = &vmstate_a9_scu;
    dc->reset = a9_scu_reset;
    dc->realize = a9_scu_realize;
}

static const TypeInfo a9_scu_info = {
    .name          = TYPE_A9_SCU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(A9SCUState),
    .class_init    = a9_scu_class_init,
};

static void a9mp_register_types(void)
{
    type_register_static(&a9_scu_info);
}

type_init(a9mp_register_types)
