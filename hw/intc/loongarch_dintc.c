/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch direct interrupt controller.
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/intc/loongarch_pch_msi.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "hw/intc/loongarch_dintc.h"
#include "hw/pci/msi.h"
#include "hw/misc/unimp.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "hw/qdev-properties.h"


static void loongarch_dintc_realize(DeviceState *dev, Error **errp)
{
    LoongArchDINTCClass *lac = LOONGARCH_DINTC_GET_CLASS(dev);

    Error *local_err = NULL;
    lac->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    return;
}

static void loongarch_dintc_unrealize(DeviceState *dev)
{
    return;
}

static void loongarch_dintc_init(Object *obj)
{
    return;
}

static void loongarch_dintc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongArchDINTCClass *lac = LOONGARCH_DINTC_CLASS(klass);

    dc->unrealize = loongarch_dintc_unrealize;
    device_class_set_parent_realize(dc, loongarch_dintc_realize,
                                    &lac->parent_realize);
}

static const TypeInfo loongarch_dintc_info = {
    .name          = TYPE_LOONGARCH_DINTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchDINTCState),
    .instance_init = loongarch_dintc_init,
    .class_init    = loongarch_dintc_class_init,
};

static void loongarch_dintc_register_types(void)
{
    type_register_static(&loongarch_dintc_info);
}

type_init(loongarch_dintc_register_types)
