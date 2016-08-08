/*
 * Coherent Processing System emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "hw/mips/cps.h"
#include "hw/mips/mips.h"
#include "hw/mips/cpudevs.h"
#include "sysemu/kvm.h"

qemu_irq get_cps_irq(MIPSCPSState *s, int pin_number)
{
    assert(pin_number < s->num_irq);
    return s->gic.irq_state[pin_number].irq;
}

static void mips_cps_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MIPSCPSState *s = MIPS_CPS(obj);

    /* Cover entire address space as there do not seem to be any
     * constraints for the base address of CPC and GIC. */
    memory_region_init(&s->container, obj, "mips-cps-container", UINT64_MAX);
    sysbus_init_mmio(sbd, &s->container);
}

static void main_cpu_reset(void *opaque)
{
    MIPSCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);

    /* All VPs are halted on reset. Leave powering up to CPC. */
    cs->halted = 1;
}

static bool cpu_mips_itu_supported(CPUMIPSState *env)
{
    bool is_mt = (env->CP0_Config5 & (1 << CP0C5_VP)) ||
                 (env->CP0_Config3 & (1 << CP0C3_MT));

    return is_mt && !kvm_enabled();
}

static void mips_cps_realize(DeviceState *dev, Error **errp)
{
    MIPSCPSState *s = MIPS_CPS(dev);
    CPUMIPSState *env;
    MIPSCPU *cpu;
    int i;
    Error *err = NULL;
    target_ulong gcr_base;
    bool itu_present = false;

    for (i = 0; i < s->num_vp; i++) {
        cpu = cpu_mips_init(s->cpu_model);
        if (cpu == NULL) {
            error_setg(errp, "%s: CPU initialization failed",  __func__);
            return;
        }

        /* Init internal devices */
        cpu_mips_irq_init_cpu(cpu);
        cpu_mips_clock_init(cpu);

        env = &cpu->env;
        if (cpu_mips_itu_supported(env)) {
            itu_present = true;
            /* Attach ITC Tag to the VP */
            env->itc_tag = mips_itu_get_tag_region(&s->itu);
        }
        qemu_register_reset(main_cpu_reset, cpu);
    }

    cpu = MIPS_CPU(first_cpu);
    env = &cpu->env;

    /* Inter-Thread Communication Unit */
    if (itu_present) {
        object_initialize(&s->itu, sizeof(s->itu), TYPE_MIPS_ITU);
        qdev_set_parent_bus(DEVICE(&s->itu), sysbus_get_default());

        object_property_set_int(OBJECT(&s->itu), 16, "num-fifo", &err);
        object_property_set_int(OBJECT(&s->itu), 16, "num-semaphores", &err);
        object_property_set_bool(OBJECT(&s->itu), true, "realized", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }

        memory_region_add_subregion(&s->container, 0,
                           sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->itu), 0));
    }

    /* Cluster Power Controller */
    object_initialize(&s->cpc, sizeof(s->cpc), TYPE_MIPS_CPC);
    qdev_set_parent_bus(DEVICE(&s->cpc), sysbus_get_default());

    object_property_set_int(OBJECT(&s->cpc), s->num_vp, "num-vp", &err);
    object_property_set_int(OBJECT(&s->cpc), 1, "vp-start-running", &err);
    object_property_set_bool(OBJECT(&s->cpc), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&s->container, 0,
                            sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->cpc), 0));

    /* Global Interrupt Controller */
    object_initialize(&s->gic, sizeof(s->gic), TYPE_MIPS_GIC);
    qdev_set_parent_bus(DEVICE(&s->gic), sysbus_get_default());

    object_property_set_int(OBJECT(&s->gic), s->num_vp, "num-vp", &err);
    object_property_set_int(OBJECT(&s->gic), 128, "num-irq", &err);
    object_property_set_bool(OBJECT(&s->gic), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&s->container, 0,
                            sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gic), 0));

    /* Global Configuration Registers */
    gcr_base = env->CP0_CMGCRBase << 4;

    object_initialize(&s->gcr, sizeof(s->gcr), TYPE_MIPS_GCR);
    qdev_set_parent_bus(DEVICE(&s->gcr), sysbus_get_default());

    object_property_set_int(OBJECT(&s->gcr), s->num_vp, "num-vp", &err);
    object_property_set_int(OBJECT(&s->gcr), 0x800, "gcr-rev", &err);
    object_property_set_int(OBJECT(&s->gcr), gcr_base, "gcr-base", &err);
    object_property_set_link(OBJECT(&s->gcr), OBJECT(&s->gic.mr), "gic", &err);
    object_property_set_link(OBJECT(&s->gcr), OBJECT(&s->cpc.mr), "cpc", &err);
    object_property_set_bool(OBJECT(&s->gcr), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&s->container, gcr_base,
                            sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gcr), 0));
}

static Property mips_cps_properties[] = {
    DEFINE_PROP_UINT32("num-vp", MIPSCPSState, num_vp, 1),
    DEFINE_PROP_UINT32("num-irq", MIPSCPSState, num_irq, 256),
    DEFINE_PROP_STRING("cpu-model", MIPSCPSState, cpu_model),
    DEFINE_PROP_END_OF_LIST()
};

static void mips_cps_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mips_cps_realize;
    dc->props = mips_cps_properties;
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
