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
 * version 2 of the License, or (at your option) any later version.
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

#include "hw.h"
#include "sysemu.h"
#include "sysbus.h"
#include "kvm.h"

#define MAX_CPUS 32

typedef struct spin_info {
    uint64_t addr;
    uint64_t r3;
    uint32_t resv;
    uint32_t pir;
    uint64_t reserved;
} QEMU_PACKED SpinInfo;

typedef struct spin_state {
    SysBusDevice busdev;
    MemoryRegion iomem;
    SpinInfo spin[MAX_CPUS];
} SpinState;

typedef struct spin_kick {
    PowerPCCPU *cpu;
    SpinInfo *spin;
} SpinKick;

static void spin_reset(void *opaque)
{
    SpinState *s = opaque;
    int i;

    for (i = 0; i < MAX_CPUS; i++) {
        SpinInfo *info = &s->spin[i];

        info->pir = i;
        info->r3 = i;
        info->addr = 1;
    }
}

/* Create -kernel TLB entries for BookE, linearly spanning 256MB.  */
static inline hwaddr booke206_page_size_to_tlb(uint64_t size)
{
    return (ffs(size >> 10) - 1) >> 1;
}

static void mmubooke_create_initial_mapping(CPUPPCState *env,
                                     target_ulong va,
                                     hwaddr pa,
                                     hwaddr len)
{
    ppcmas_tlb_t *tlb = booke206_get_tlbm(env, 1, 0, 1);
    hwaddr size;

    size = (booke206_page_size_to_tlb(len) << MAS1_TSIZE_SHIFT);
    tlb->mas1 = MAS1_VALID | size;
    tlb->mas2 = (va & TARGET_PAGE_MASK) | MAS2_M;
    tlb->mas7_3 = pa & TARGET_PAGE_MASK;
    tlb->mas7_3 |= MAS3_UR | MAS3_UW | MAS3_UX | MAS3_SR | MAS3_SW | MAS3_SX;
    env->tlb_dirty = true;
}

static void spin_kick(void *data)
{
    SpinKick *kick = data;
    CPUState *cpu = CPU(kick->cpu);
    CPUPPCState *env = &kick->cpu->env;
    SpinInfo *curspin = kick->spin;
    hwaddr map_size = 64 * 1024 * 1024;
    hwaddr map_start;

    cpu_synchronize_state(env);
    stl_p(&curspin->pir, env->spr[SPR_PIR]);
    env->nip = ldq_p(&curspin->addr) & (map_size - 1);
    env->gpr[3] = ldq_p(&curspin->r3);
    env->gpr[4] = 0;
    env->gpr[5] = 0;
    env->gpr[6] = 0;
    env->gpr[7] = map_size;
    env->gpr[8] = 0;
    env->gpr[9] = 0;

    map_start = ldq_p(&curspin->addr) & ~(map_size - 1);
    mmubooke_create_initial_mapping(env, 0, map_start, map_size);

    env->halted = 0;
    env->exception_index = -1;
    cpu->stopped = false;
    qemu_cpu_kick(cpu);
}

static void spin_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned len)
{
    SpinState *s = opaque;
    int env_idx = addr / sizeof(SpinInfo);
    CPUPPCState *env;
    SpinInfo *curspin = &s->spin[env_idx];
    uint8_t *curspin_p = (uint8_t*)curspin;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        if (env->cpu_index == env_idx) {
            break;
        }
    }

    if (!env) {
        /* Unknown CPU */
        return;
    }

    if (!env->cpu_index) {
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
        SpinKick kick = {
            .cpu = ppc_env_get_cpu(env),
            .spin = curspin,
        };

        run_on_cpu(CPU(kick.cpu), spin_kick, &kick);
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

static int ppce500_spin_initfn(SysBusDevice *dev)
{
    SpinState *s;

    s = FROM_SYSBUS(SpinState, sysbus_from_qdev(dev));

    memory_region_init_io(&s->iomem, &spin_rw_ops, s, "e500 spin pv device",
                          sizeof(SpinInfo) * MAX_CPUS);
    sysbus_init_mmio(dev, &s->iomem);

    qemu_register_reset(spin_reset, s);

    return 0;
}

static void ppce500_spin_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = ppce500_spin_initfn;
}

static TypeInfo ppce500_spin_info = {
    .name          = "e500-spin",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SpinState),
    .class_init    = ppce500_spin_class_init,
};

static void ppce500_spin_register_types(void)
{
    type_register_static(&ppce500_spin_info);
}

type_init(ppce500_spin_register_types)
