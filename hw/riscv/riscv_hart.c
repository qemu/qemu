/*
 * QEMU RISCV Hart Array
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * Holds the state of a homogeneous array of RISC-V harts
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "sysemu/reset.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/riscv_hart.h"

static Property riscv_harts_props[] = {
    DEFINE_PROP_UINT32("num-harts", RISCVHartArrayState, num_harts, 1),
    DEFINE_PROP_UINT32("hartid-base", RISCVHartArrayState, hartid_base, 0),
    DEFINE_PROP_STRING("cpu-type", RISCVHartArrayState, cpu_type),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_harts_cpu_reset(void *opaque)
{
    RISCVCPU *cpu = opaque;
    cpu_reset(CPU(cpu));
}

static void riscv_hart_realize(RISCVHartArrayState *s, int idx,
                               char *cpu_type, Error **errp)
{
    Error *err = NULL;

    object_initialize_child(OBJECT(s), "harts[*]", &s->harts[idx],
                            sizeof(RISCVCPU), cpu_type,
                            &error_abort, NULL);
    s->harts[idx].env.mhartid = s->hartid_base + idx;
    qemu_register_reset(riscv_harts_cpu_reset, &s->harts[idx]);
    object_property_set_bool(OBJECT(&s->harts[idx]), true,
                             "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
}

static void riscv_harts_realize(DeviceState *dev, Error **errp)
{
    RISCVHartArrayState *s = RISCV_HART_ARRAY(dev);
    int n;

    s->harts = g_new0(RISCVCPU, s->num_harts);

    for (n = 0; n < s->num_harts; n++) {
        riscv_hart_realize(s, n, s->cpu_type, errp);
    }
}

static void riscv_harts_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = riscv_harts_props;
    dc->realize = riscv_harts_realize;
}

static const TypeInfo riscv_harts_info = {
    .name          = TYPE_RISCV_HART_ARRAY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVHartArrayState),
    .class_init    = riscv_harts_class_init,
};

static void riscv_harts_register_types(void)
{
    type_register_static(&riscv_harts_info);
}

type_init(riscv_harts_register_types)
