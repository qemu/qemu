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

qemu_irq get_cps_irq(MIPSCPSState *s, int pin_number)
{
    MIPSCPU *cpu = MIPS_CPU(first_cpu);
    CPUMIPSState *env = &cpu->env;

    assert(pin_number < s->num_irq);

    /* TODO: return GIC pins once implemented */
    return env->irq[pin_number];
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

static void mips_cps_realize(DeviceState *dev, Error **errp)
{
    MIPSCPSState *s = MIPS_CPS(dev);
    CPUMIPSState *env;
    MIPSCPU *cpu;
    int i;

    for (i = 0; i < s->num_vp; i++) {
        cpu = cpu_mips_init(s->cpu_model);
        if (cpu == NULL) {
            error_setg(errp, "%s: CPU initialization failed\n",  __func__);
            return;
        }
        env = &cpu->env;

        /* Init internal devices */
        cpu_mips_irq_init_cpu(env);
        cpu_mips_clock_init(env);
        qemu_register_reset(main_cpu_reset, cpu);
    }
}

static Property mips_cps_properties[] = {
    DEFINE_PROP_UINT32("num-vp", MIPSCPSState, num_vp, 1),
    DEFINE_PROP_UINT32("num-irq", MIPSCPSState, num_irq, 8),
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
