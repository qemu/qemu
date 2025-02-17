/*
 * Allwinner CPU Configuration Module emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/core/cpu.h"
#include "target/arm/arm-powerctl.h"
#include "target/arm/cpu.h"
#include "hw/misc/allwinner-cpucfg.h"
#include "trace.h"

/* CPUCFG register offsets */
enum {
    REG_CPUS_RST_CTRL       = 0x0000, /* CPUs Reset Control */
    REG_CPU0_RST_CTRL       = 0x0040, /* CPU#0 Reset Control */
    REG_CPU0_CTRL           = 0x0044, /* CPU#0 Control */
    REG_CPU0_STATUS         = 0x0048, /* CPU#0 Status */
    REG_CPU1_RST_CTRL       = 0x0080, /* CPU#1 Reset Control */
    REG_CPU1_CTRL           = 0x0084, /* CPU#1 Control */
    REG_CPU1_STATUS         = 0x0088, /* CPU#1 Status */
    REG_CPU2_RST_CTRL       = 0x00C0, /* CPU#2 Reset Control */
    REG_CPU2_CTRL           = 0x00C4, /* CPU#2 Control */
    REG_CPU2_STATUS         = 0x00C8, /* CPU#2 Status */
    REG_CPU3_RST_CTRL       = 0x0100, /* CPU#3 Reset Control */
    REG_CPU3_CTRL           = 0x0104, /* CPU#3 Control */
    REG_CPU3_STATUS         = 0x0108, /* CPU#3 Status */
    REG_CPU_SYS_RST         = 0x0140, /* CPU System Reset */
    REG_CLK_GATING          = 0x0144, /* CPU Clock Gating */
    REG_GEN_CTRL            = 0x0184, /* General Control */
    REG_SUPER_STANDBY       = 0x01A0, /* Super Standby Flag */
    REG_ENTRY_ADDR          = 0x01A4, /* Reset Entry Address */
    REG_DBG_EXTERN          = 0x01E4, /* Debug External */
    REG_CNT64_CTRL          = 0x0280, /* 64-bit Counter Control */
    REG_CNT64_LOW           = 0x0284, /* 64-bit Counter Low */
    REG_CNT64_HIGH          = 0x0288, /* 64-bit Counter High */
};

/* CPUCFG register flags */
enum {
    CPUX_RESET_RELEASED     = ((1 << 1) | (1 << 0)),
    CPUX_STATUS_SMP         = (1 << 0),
    CPU_SYS_RESET_RELEASED  = (1 << 0),
    CLK_GATING_ENABLE       = ((1 << 8) | 0xF),
};

/* CPUCFG register reset values */
enum {
    REG_CLK_GATING_RST      = 0x0000010F,
    REG_GEN_CTRL_RST        = 0x00000020,
    REG_SUPER_STANDBY_RST   = 0x0,
    REG_CNT64_CTRL_RST      = 0x0,
};

/* CPUCFG constants */
enum {
    CPU_EXCEPTION_LEVEL_ON_RESET = 3, /* EL3 */
};

static void allwinner_cpucfg_cpu_reset(AwCpuCfgState *s, uint8_t cpu_id)
{
    int ret;

    trace_allwinner_cpucfg_cpu_reset(cpu_id, s->entry_addr);

    ARMCPU *target_cpu = ARM_CPU(arm_get_cpu_by_id(cpu_id));
    if (!target_cpu) {
        /*
         * Called with a bogus value for cpu_id. Guest error will
         * already have been logged, we can simply return here.
         */
        return;
    }
    bool target_aa64 = arm_feature(&target_cpu->env, ARM_FEATURE_AARCH64);

    ret = arm_set_cpu_on(cpu_id, s->entry_addr, 0,
                         CPU_EXCEPTION_LEVEL_ON_RESET, target_aa64);
    if (ret != QEMU_ARM_POWERCTL_RET_SUCCESS) {
        error_report("%s: failed to bring up CPU %d: err %d",
                     __func__, cpu_id, ret);
        return;
    }
}

static uint64_t allwinner_cpucfg_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const AwCpuCfgState *s = AW_CPUCFG(opaque);
    uint64_t val = 0;

    switch (offset) {
    case REG_CPUS_RST_CTRL:     /* CPUs Reset Control */
    case REG_CPU_SYS_RST:       /* CPU System Reset */
        val = CPU_SYS_RESET_RELEASED;
        break;
    case REG_CPU0_RST_CTRL:     /* CPU#0 Reset Control */
    case REG_CPU1_RST_CTRL:     /* CPU#1 Reset Control */
    case REG_CPU2_RST_CTRL:     /* CPU#2 Reset Control */
    case REG_CPU3_RST_CTRL:     /* CPU#3 Reset Control */
        val = CPUX_RESET_RELEASED;
        break;
    case REG_CPU0_CTRL:         /* CPU#0 Control */
    case REG_CPU1_CTRL:         /* CPU#1 Control */
    case REG_CPU2_CTRL:         /* CPU#2 Control */
    case REG_CPU3_CTRL:         /* CPU#3 Control */
        val = 0;
        break;
    case REG_CPU0_STATUS:       /* CPU#0 Status */
    case REG_CPU1_STATUS:       /* CPU#1 Status */
    case REG_CPU2_STATUS:       /* CPU#2 Status */
    case REG_CPU3_STATUS:       /* CPU#3 Status */
        val = CPUX_STATUS_SMP;
        break;
    case REG_CLK_GATING:        /* CPU Clock Gating */
        val = CLK_GATING_ENABLE;
        break;
    case REG_GEN_CTRL:          /* General Control */
        val = s->gen_ctrl;
        break;
    case REG_SUPER_STANDBY:     /* Super Standby Flag */
        val = s->super_standby;
        break;
    case REG_ENTRY_ADDR:        /* Reset Entry Address */
        val = s->entry_addr;
        break;
    case REG_DBG_EXTERN:        /* Debug External */
    case REG_CNT64_CTRL:        /* 64-bit Counter Control */
    case REG_CNT64_LOW:         /* 64-bit Counter Low */
    case REG_CNT64_HIGH:        /* 64-bit Counter High */
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register at 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }

    trace_allwinner_cpucfg_read(offset, val, size);

    return val;
}

static void allwinner_cpucfg_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwCpuCfgState *s = AW_CPUCFG(opaque);

    trace_allwinner_cpucfg_write(offset, val, size);

    switch (offset) {
    case REG_CPUS_RST_CTRL:     /* CPUs Reset Control */
    case REG_CPU_SYS_RST:       /* CPU System Reset */
        break;
    case REG_CPU0_RST_CTRL:     /* CPU#0 Reset Control */
    case REG_CPU1_RST_CTRL:     /* CPU#1 Reset Control */
    case REG_CPU2_RST_CTRL:     /* CPU#2 Reset Control */
    case REG_CPU3_RST_CTRL:     /* CPU#3 Reset Control */
        if (val) {
            allwinner_cpucfg_cpu_reset(s, (offset - REG_CPU0_RST_CTRL) >> 6);
        }
        break;
    case REG_CPU0_CTRL:         /* CPU#0 Control */
    case REG_CPU1_CTRL:         /* CPU#1 Control */
    case REG_CPU2_CTRL:         /* CPU#2 Control */
    case REG_CPU3_CTRL:         /* CPU#3 Control */
    case REG_CPU0_STATUS:       /* CPU#0 Status */
    case REG_CPU1_STATUS:       /* CPU#1 Status */
    case REG_CPU2_STATUS:       /* CPU#2 Status */
    case REG_CPU3_STATUS:       /* CPU#3 Status */
    case REG_CLK_GATING:        /* CPU Clock Gating */
        break;
    case REG_GEN_CTRL:          /* General Control */
        s->gen_ctrl = val;
        break;
    case REG_SUPER_STANDBY:     /* Super Standby Flag */
        s->super_standby = val;
        break;
    case REG_ENTRY_ADDR:        /* Reset Entry Address */
        s->entry_addr = val;
        break;
    case REG_DBG_EXTERN:        /* Debug External */
    case REG_CNT64_CTRL:        /* 64-bit Counter Control */
    case REG_CNT64_LOW:         /* 64-bit Counter Low */
    case REG_CNT64_HIGH:        /* 64-bit Counter High */
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register at 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }
}

static const MemoryRegionOps allwinner_cpucfg_ops = {
    .read = allwinner_cpucfg_read,
    .write = allwinner_cpucfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_cpucfg_reset(DeviceState *dev)
{
    AwCpuCfgState *s = AW_CPUCFG(dev);

    /* Set default values for registers */
    s->gen_ctrl = REG_GEN_CTRL_RST;
    s->super_standby = REG_SUPER_STANDBY_RST;
    s->entry_addr = 0;
}

static void allwinner_cpucfg_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwCpuCfgState *s = AW_CPUCFG(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_cpucfg_ops, s,
                          TYPE_AW_CPUCFG, 1 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_cpucfg_vmstate = {
    .name = "allwinner-cpucfg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(gen_ctrl, AwCpuCfgState),
        VMSTATE_UINT32(super_standby, AwCpuCfgState),
        VMSTATE_UINT32(entry_addr, AwCpuCfgState),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_cpucfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, allwinner_cpucfg_reset);
    dc->vmsd = &allwinner_cpucfg_vmstate;
}

static const TypeInfo allwinner_cpucfg_info = {
    .name          = TYPE_AW_CPUCFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_cpucfg_init,
    .instance_size = sizeof(AwCpuCfgState),
    .class_init    = allwinner_cpucfg_class_init,
};

static void allwinner_cpucfg_register(void)
{
    type_register_static(&allwinner_cpucfg_info);
}

type_init(allwinner_cpucfg_register)
