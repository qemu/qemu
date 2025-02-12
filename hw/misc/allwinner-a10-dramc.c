/*
 * Allwinner A10 DRAM Controller emulation
 *
 * Copyright (C) 2022 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 *  This file is derived from Allwinner H3 DRAMC,
 *  by Niek Linnenbank.
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
#include "hw/misc/allwinner-a10-dramc.h"

/* DRAMC register offsets */
enum {
    REG_SDR_CCR = 0x0000,
    REG_SDR_ZQCR0 = 0x00a8,
    REG_SDR_ZQSR = 0x00b0
};

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

/* DRAMC register flags */
enum {
    REG_SDR_CCR_DATA_TRAINING = (1 << 30),
    REG_SDR_CCR_DRAM_INIT     = (1 << 31),
};
enum {
    REG_SDR_ZQSR_ZCAL         = (1 << 31),
};

/* DRAMC register reset values */
enum {
    REG_SDR_CCR_RESET   = 0x80020000,
    REG_SDR_ZQCR0_RESET = 0x07b00000,
    REG_SDR_ZQSR_RESET  = 0x80000000
};

static uint64_t allwinner_a10_dramc_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    const AwA10DramControllerState *s = AW_A10_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_SDR_CCR:
    case REG_SDR_ZQCR0:
    case REG_SDR_ZQSR:
        break;
    case 0x2e4 ... AW_A10_DRAMC_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static void allwinner_a10_dramc_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwA10DramControllerState *s = AW_A10_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_SDR_CCR:
        if (val & REG_SDR_CCR_DRAM_INIT) {
            /* Clear DRAM_INIT to indicate process is done. */
            val &= ~REG_SDR_CCR_DRAM_INIT;
        }
        if (val & REG_SDR_CCR_DATA_TRAINING) {
            /* Clear DATA_TRAINING to indicate process is done. */
            val &= ~REG_SDR_CCR_DATA_TRAINING;
        }
        break;
    case REG_SDR_ZQCR0:
        /* Set ZCAL in ZQSR to indicate calibration is done. */
        s->regs[REG_INDEX(REG_SDR_ZQSR)] |= REG_SDR_ZQSR_ZCAL;
        break;
    case 0x2e4 ... AW_A10_DRAMC_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }

    s->regs[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_a10_dramc_ops = {
    .read = allwinner_a10_dramc_read,
    .write = allwinner_a10_dramc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_a10_dramc_reset_enter(Object *obj, ResetType type)
{
    AwA10DramControllerState *s = AW_A10_DRAMC(obj);

    /* Set default values for registers */
    s->regs[REG_INDEX(REG_SDR_CCR)] = REG_SDR_CCR_RESET;
    s->regs[REG_INDEX(REG_SDR_ZQCR0)] = REG_SDR_ZQCR0_RESET;
    s->regs[REG_INDEX(REG_SDR_ZQSR)] = REG_SDR_ZQSR_RESET;
}

static void allwinner_a10_dramc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwA10DramControllerState *s = AW_A10_DRAMC(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_a10_dramc_ops, s,
                          TYPE_AW_A10_DRAMC, AW_A10_DRAMC_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_a10_dramc_vmstate = {
    .name = "allwinner-a10-dramc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwA10DramControllerState,
                             AW_A10_DRAMC_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_a10_dramc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = allwinner_a10_dramc_reset_enter;
    dc->vmsd = &allwinner_a10_dramc_vmstate;
}

static const TypeInfo allwinner_a10_dramc_info = {
    .name          = TYPE_AW_A10_DRAMC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_a10_dramc_init,
    .instance_size = sizeof(AwA10DramControllerState),
    .class_init    = allwinner_a10_dramc_class_init,
};

static void allwinner_a10_dramc_register(void)
{
    type_register_static(&allwinner_a10_dramc_info);
}

type_init(allwinner_a10_dramc_register)
