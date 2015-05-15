/*
 * Xilinx Zynq MPSoC emulation
 *
 * Copyright (C) 2015 Xilinx Inc
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "hw/arm/xlnx-zynqmp.h"

static void xlnx_zynqmp_init(Object *obj)
{
    XlnxZynqMPState *s = XLNX_ZYNQMP(obj);
    int i;

    for (i = 0; i < XLNX_ZYNQMP_NUM_CPUS; i++) {
        object_initialize(&s->cpu[i], sizeof(s->cpu[i]),
                          "cortex-a53-" TYPE_ARM_CPU);
        object_property_add_child(obj, "cpu[*]", OBJECT(&s->cpu[i]),
                                  &error_abort);
    }
}

static void xlnx_zynqmp_realize(DeviceState *dev, Error **errp)
{
    XlnxZynqMPState *s = XLNX_ZYNQMP(dev);
    uint8_t i;
    Error *err = NULL;

    for (i = 0; i < XLNX_ZYNQMP_NUM_CPUS; i++) {
        object_property_set_int(OBJECT(&s->cpu[i]), QEMU_PSCI_CONDUIT_SMC,
                                "psci-conduit", &error_abort);
        if (i > 0) {
            /* Secondary CPUs start in PSCI powered-down state */
            object_property_set_bool(OBJECT(&s->cpu[i]), true,
                                     "start-powered-off", &error_abort);
        }

        object_property_set_bool(OBJECT(&s->cpu[i]), true, "realized", &err);
        if (err) {
            error_propagate((errp), (err));
            return;
        }
    }
}

static void xlnx_zynqmp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = xlnx_zynqmp_realize;
}

static const TypeInfo xlnx_zynqmp_type_info = {
    .name = TYPE_XLNX_ZYNQMP,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XlnxZynqMPState),
    .instance_init = xlnx_zynqmp_init,
    .class_init = xlnx_zynqmp_class_init,
};

static void xlnx_zynqmp_register_types(void)
{
    type_register_static(&xlnx_zynqmp_type_info);
}

type_init(xlnx_zynqmp_register_types)
