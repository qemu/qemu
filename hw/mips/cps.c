/*
 * Coherent Processing System emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/mips/cps.h"
#include "hw/mips/mips.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "sysemu/kvm.h"
#include "sysemu/reset.h"

qemu_irq get_cps_irq(MIPSCPSState *s, int pin_number)
{
    assert(pin_number < s->num_irq);
    return s->gic.irq_state[pin_number].irq;
}

static void mips_cps_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MIPSCPSState *s = MIPS_CPS(obj);

    s->clock = qdev_init_clock_in(DEVICE(obj), "clk-in", NULL, NULL, 0);
    /*
     * Cover entire address space as there do not seem to be any
     * constraints for the base address of CPC and GIC.
     */
    memory_region_init(&s->container, obj, "mips-cps-container", UINT64_MAX);
    sysbus_init_mmio(sbd, &s->container);
}

static void main_cpu_reset(void *opaque)
{
    MIPSCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);
}

static bool cpu_mips_itu_supported(CPUMIPSState *env)
{
    bool is_mt = (env->CP0_Config5 & (1 << CP0C5_VP)) || ase_mt_available(env);

    return is_mt && !kvm_enabled();
}

static void mips_cps_realize(DeviceState *dev, Error **errp)
{
    MIPSCPSState *s = MIPS_CPS(dev);
    target_ulong gcr_base;
    bool itu_present = false;

    if (!clock_get(s->clock)) {
        error_setg(errp, "CPS input clock is not connected to an output clock");
        return;
    }

    for (int i = 0; i < s->num_vp; i++) {
        MIPSCPU *cpu = MIPS_CPU(object_new(s->cpu_type));
        CPUMIPSState *env = &cpu->env;

        /* All VPs are halted on reset. Leave powering up to CPC. */
        if (!object_property_set_bool(OBJECT(cpu), "start-powered-off", true,
                                      errp)) {
            return;
        }
        /* All cores use the same clock tree */
        qdev_connect_clock_in(DEVICE(cpu), "clk-in", s->clock);

        if (!qdev_realize_and_unref(DEVICE(cpu), NULL, errp)) {
            return;
        }

        /* Init internal devices */
        cpu_mips_irq_init_cpu(cpu);
        cpu_mips_clock_init(cpu);

        if (cpu_mips_itu_supported(env)) {
            itu_present = true;
            /* Attach ITC Tag to the VP */
            env->itc_tag = mips_itu_get_tag_region(&s->itu);
            env->itu = &s->itu;
        }
        qemu_register_reset(main_cpu_reset, cpu);
    }

    /* Inter-Thread Communication Unit */
    if (itu_present) {
        object_initialize_child(OBJECT(dev), "itu", &s->itu, TYPE_MIPS_ITU);
        object_property_set_link(OBJECT(&s->itu), "cpu[0]",
                                 OBJECT(first_cpu), &error_abort);
        object_property_set_uint(OBJECT(&s->itu), "num-fifo", 16,
                                &error_abort);
        object_property_set_uint(OBJECT(&s->itu), "num-semaphores", 16,
                                &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->itu), errp)) {
            return;
        }

        memory_region_add_subregion(&s->container, 0,
                           sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->itu), 0));
    }

    /* Cluster Power Controller */
    object_initialize_child(OBJECT(dev), "cpc", &s->cpc, TYPE_MIPS_CPC);
    object_property_set_uint(OBJECT(&s->cpc), "num-vp", s->num_vp,
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpc), "vp-start-running", 1,
                            &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->cpc), errp)) {
        return;
    }

    memory_region_add_subregion(&s->container, 0,
                            sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->cpc), 0));

    /* Global Interrupt Controller */
    object_initialize_child(OBJECT(dev), "gic", &s->gic, TYPE_MIPS_GIC);
    object_property_set_uint(OBJECT(&s->gic), "num-vp", s->num_vp,
                            &error_abort);
    object_property_set_uint(OBJECT(&s->gic), "num-irq", 128,
                            &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }

    memory_region_add_subregion(&s->container, 0,
                            sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gic), 0));

    /* Global Configuration Registers */
    gcr_base = MIPS_CPU(first_cpu)->env.CP0_CMGCRBase << 4;

    object_initialize_child(OBJECT(dev), "gcr", &s->gcr, TYPE_MIPS_GCR);
    object_property_set_uint(OBJECT(&s->gcr), "num-vp", s->num_vp,
                            &error_abort);
    object_property_set_int(OBJECT(&s->gcr), "gcr-rev", 0x800,
                            &error_abort);
    object_property_set_int(OBJECT(&s->gcr), "gcr-base", gcr_base,
                            &error_abort);
    object_property_set_link(OBJECT(&s->gcr), "gic", OBJECT(&s->gic.mr),
                             &error_abort);
    object_property_set_link(OBJECT(&s->gcr), "cpc", OBJECT(&s->cpc.mr),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gcr), errp)) {
        return;
    }

    memory_region_add_subregion(&s->container, gcr_base,
                            sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gcr), 0));
}

static Property mips_cps_properties[] = {
    DEFINE_PROP_UINT32("num-vp", MIPSCPSState, num_vp, 1),
    DEFINE_PROP_UINT32("num-irq", MIPSCPSState, num_irq, 256),
    DEFINE_PROP_STRING("cpu-type", MIPSCPSState, cpu_type),
    DEFINE_PROP_END_OF_LIST()
};

static void mips_cps_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mips_cps_realize;
    device_class_set_props(dc, mips_cps_properties);
}

static const TypeInfo mips_cps_info = {
    .name = TYPE_MIPS_CPS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MIPSCPSState),
    .instance_init = mips_cps_init,
    .class_init = mips_cps_class_init,
};

static void mips_cps_register_types(void)
{
    type_register_static(&mips_cps_info);
}

type_init(mips_cps_register_types)
