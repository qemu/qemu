/*
 * QEMU PowerPC e500v2 ePAPR spinning code
 *
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Alexander Graf, <agraf@suse.de>
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
 *
 * This code is not really a device, but models an interface that usually
 * firmware takes care of. It's used when QEMU plays the role of firmware.
 *
 * Specification:
 *
 * https://www.power.org/resources/downloads/Power_ePAPR_APPROVED_v1.1.pdf
 *
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "system/hw_accel.h"
#include "hw/ppc/ppc.h"
#include "e500.h"
#include "qom/object.h"

#define MAX_CPUS 32

typedef struct spin_info {
    uint64_t addr;
    uint64_t r3;
    uint32_t resv;
    uint32_t pir;
    uint64_t reserved;
} QEMU_PACKED SpinInfo;

#define TYPE_E500_SPIN "e500-spin"
OBJECT_DECLARE_SIMPLE_TYPE(SpinState, E500_SPIN)

struct SpinState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    SpinInfo spin[MAX_CPUS];
};

static void spin_reset(DeviceState *dev)
{
    SpinState *s = E500_SPIN(dev);
    int i;

    for (i = 0; i < MAX_CPUS; i++) {
        SpinInfo *info = &s->spin[i];

        stl_p(&info->pir, i);
        stq_p(&info->r3, i);
        stq_p(&info->addr, 1);
    }
}

static void spin_kick(CPUState *cs, run_on_cpu_data data)
{
    CPUPPCState *env = cpu_env(cs);
    SpinInfo *curspin = data.host_ptr;
    hwaddr map_start, map_size = 64 * MiB;
    ppcmas_tlb_t *tlb = booke206_get_tlbm(env, 1, 0, 1);

    cpu_synchronize_state(cs);
    stl_p(&curspin->pir, env->spr[SPR_BOOKE_PIR]);
    env->nip = ldq_p(&curspin->addr) & (map_size - 1);
    env->gpr[3] = ldq_p(&curspin->r3);
    env->gpr[4] = 0;
    env->gpr[5] = 0;
    env->gpr[6] = 0;
    env->gpr[7] = map_size;
    env->gpr[8] = 0;
    env->gpr[9] = 0;

    map_start = ldq_p(&curspin->addr) & ~(map_size - 1);
    /* create initial mapping */
    booke206_set_tlb(tlb, 0, map_start, map_size);
    tlb->mas2 |= MAS2_M;
#ifdef CONFIG_KVM
    env->tlb_dirty = true;
#endif

    cs->halted = 0;
    cs->exception_index = -1;
    cs->stopped = false;
    qemu_cpu_kick(cs);
}

static void spin_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned len)
{
    SpinState *s = opaque;
    int env_idx = addr / sizeof(SpinInfo);
    CPUState *cpu;
    SpinInfo *curspin = &s->spin[env_idx];
    uint8_t *curspin_p = (uint8_t*)curspin;

    cpu = qemu_get_cpu(env_idx);
    if (cpu == NULL) {
        /* Unknown CPU */
        return;
    }

    if (cpu->cpu_index == 0) {
        /* primary CPU doesn't spin */
        return;
    }

    curspin_p = &curspin_p[addr % sizeof(SpinInfo)];
    switch (len) {
    case 1:
        stb_p(curspin_p, value);
        break;
    case 2:
        stw_p(curspin_p, value);
        break;
    case 4:
        stl_p(curspin_p, value);
        break;
    }

    if (!(ldq_p(&curspin->addr) & 1)) {
        /* run CPU */
        run_on_cpu(cpu, spin_kick, RUN_ON_CPU_HOST_PTR(curspin));
    }
}

static uint64_t spin_read(void *opaque, hwaddr addr, unsigned len)
{
    SpinState *s = opaque;
    uint8_t *spin_p = &((uint8_t*)s->spin)[addr];

    switch (len) {
    case 1:
        return ldub_p(spin_p);
    case 2:
        return lduw_p(spin_p);
    case 4:
        return ldl_p(spin_p);
    default:
        hw_error("ppce500: unexpected %s with len = %u", __func__, len);
    }
}

static const MemoryRegionOps spin_rw_ops = {
    .read = spin_read,
    .write = spin_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void ppce500_spin_initfn(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    SpinState *s = E500_SPIN(dev);

    memory_region_init_io(&s->iomem, obj, &spin_rw_ops, s,
                          "e500 spin pv device", sizeof(SpinInfo) * MAX_CPUS);
    sysbus_init_mmio(dev, &s->iomem);
}

static void ppce500_spin_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, spin_reset);
}

static const TypeInfo ppce500_spin_info = {
    .name          = TYPE_E500_SPIN,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SpinState),
    .instance_init = ppce500_spin_initfn,
    .class_init    = ppce500_spin_class_init,
};

static void ppce500_spin_register_types(void)
{
    type_register_static(&ppce500_spin_info);
}

type_init(ppce500_spin_register_types)
