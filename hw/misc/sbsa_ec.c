/*
 * ARM SBSA Reference Platform Embedded Controller
 *
 * A device to allow PSCI running in the secure side of sbsa-ref machine
 * to communicate platform power states to qemu.
 *
 * Copyright (c) 2020 Nuvia Inc
 * Written by Graeme Gregory <graeme@nuviainc.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "sysemu/runstate.h"

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
} SECUREECState;

#define TYPE_SBSA_EC      "sbsa-ec"
#define SECURE_EC(obj) OBJECT_CHECK(SECUREECState, (obj), TYPE_SBSA_EC)

enum sbsa_ec_powerstates {
    SBSA_EC_CMD_POWEROFF = 0x01,
    SBSA_EC_CMD_REBOOT = 0x02,
};

static uint64_t sbsa_ec_read(void *opaque, hwaddr offset, unsigned size)
{
    /* No use for this currently */
    qemu_log_mask(LOG_GUEST_ERROR, "sbsa-ec: no readable registers");
    return 0;
}

static void sbsa_ec_write(void *opaque, hwaddr offset,
                     uint64_t value, unsigned size)
{
    if (offset == 0) { /* PSCI machine power command register */
        switch (value) {
        case SBSA_EC_CMD_POWEROFF:
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            break;
        case SBSA_EC_CMD_REBOOT:
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "sbsa-ec: unknown power command");
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "sbsa-ec: unknown EC register");
    }
}

static const MemoryRegionOps sbsa_ec_ops = {
    .read = sbsa_ec_read,
    .write = sbsa_ec_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void sbsa_ec_init(Object *obj)
{
    SECUREECState *s = SECURE_EC(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &sbsa_ec_ops, s, "sbsa-ec",
                          0x1000);
    sysbus_init_mmio(dev, &s->iomem);
}

static void sbsa_ec_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* No vmstate or reset required: device has no internal state */
    dc->user_creatable = false;
}

static const TypeInfo sbsa_ec_info = {
    .name          = TYPE_SBSA_EC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SECUREECState),
    .instance_init = sbsa_ec_init,
    .class_init    = sbsa_ec_class_init,
};

static void sbsa_ec_register_type(void)
{
    type_register_static(&sbsa_ec_info);
}

type_init(sbsa_ec_register_type);
