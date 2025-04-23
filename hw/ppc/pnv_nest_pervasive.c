/*
 * QEMU PowerPC nest pervasive common chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_nest_pervasive.h"

/*
 * Status, configuration, and control units in POWER chips is provided
 * by the pervasive subsystem, which connects registers to the SCOM bus,
 * which can be programmed by processor cores, other units on the chip,
 * BMCs, or other POWER chips.
 *
 * A POWER10 chip is divided into logical units called chiplets. Chiplets
 * are broadly divided into "core chiplets" (with the processor cores) and
 * "nest chiplets" (with everything else). Each chiplet has an attachment
 * to the pervasive bus (PIB) and with chiplet-specific registers.
 * All nest chiplets have a common basic set of registers.
 *
 * This model will provide the registers functionality for common registers of
 * nest unit (PB Chiplet, PCI Chiplets, MC Chiplet, PAU Chiplets)
 *
 * Currently this model provide the read/write functionality of chiplet control
 * scom registers.
 */

#define CPLT_CONF0               0x08
#define CPLT_CONF0_OR            0x18
#define CPLT_CONF0_CLEAR         0x28
#define CPLT_CONF1               0x09
#define CPLT_CONF1_OR            0x19
#define CPLT_CONF1_CLEAR         0x29
#define CPLT_STAT0               0x100
#define CPLT_MASK0               0x101
#define CPLT_PROTECT_MODE        0x3FE
#define CPLT_ATOMIC_CLOCK        0x3FF

static uint64_t pnv_chiplet_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvNestChipletPervasive *nest_pervasive = PNV_NEST_CHIPLET_PERVASIVE(
                                              opaque);
    uint32_t reg = addr >> 3;
    uint64_t val = ~0ull;

    /* CPLT_CTRL0 to CPLT_CTRL5 */
    for (int i = 0; i < PNV_CPLT_CTRL_SIZE; i++) {
        if (reg == i) {
            return nest_pervasive->control_regs.cplt_ctrl[i];
        } else if ((reg == (i + 0x10)) || (reg == (i + 0x20))) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                           "xscom read at 0x%" PRIx32 "\n",
                                           __func__, reg);
            return val;
        }
    }

    switch (reg) {
    case CPLT_CONF0:
        val = nest_pervasive->control_regs.cplt_cfg0;
        break;
    case CPLT_CONF0_OR:
    case CPLT_CONF0_CLEAR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                   "xscom read at 0x%" PRIx32 "\n",
                                   __func__, reg);
        break;
    case CPLT_CONF1:
        val = nest_pervasive->control_regs.cplt_cfg1;
        break;
    case CPLT_CONF1_OR:
    case CPLT_CONF1_CLEAR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                   "xscom read at 0x%" PRIx32 "\n",
                                   __func__, reg);
        break;
    case CPLT_STAT0:
        val = nest_pervasive->control_regs.cplt_stat0;
        break;
    case CPLT_MASK0:
        val = nest_pervasive->control_regs.cplt_mask0;
        break;
    case CPLT_PROTECT_MODE:
        val = nest_pervasive->control_regs.ctrl_protect_mode;
        break;
    case CPLT_ATOMIC_CLOCK:
        val = nest_pervasive->control_regs.ctrl_atomic_lock;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Chiplet_control_regs: Invalid xscom "
                 "read at 0x%" PRIx32 "\n", __func__, reg);
    }
    return val;
}

static void pnv_chiplet_ctrl_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PnvNestChipletPervasive *nest_pervasive = PNV_NEST_CHIPLET_PERVASIVE(
                                              opaque);
    uint32_t reg = addr >> 3;

    /* CPLT_CTRL0 to CPLT_CTRL5 */
    for (int i = 0; i < PNV_CPLT_CTRL_SIZE; i++) {
        if (reg == i) {
            nest_pervasive->control_regs.cplt_ctrl[i] = val;
            return;
        } else if (reg == (i + 0x10)) {
            nest_pervasive->control_regs.cplt_ctrl[i] |= val;
            return;
        } else if (reg == (i + 0x20)) {
            nest_pervasive->control_regs.cplt_ctrl[i] &= ~val;
            return;
        }
    }

    switch (reg) {
    case CPLT_CONF0:
        nest_pervasive->control_regs.cplt_cfg0 = val;
        break;
    case CPLT_CONF0_OR:
        nest_pervasive->control_regs.cplt_cfg0 |= val;
        break;
    case CPLT_CONF0_CLEAR:
        nest_pervasive->control_regs.cplt_cfg0 &= ~val;
        break;
    case CPLT_CONF1:
        nest_pervasive->control_regs.cplt_cfg1 = val;
        break;
    case CPLT_CONF1_OR:
        nest_pervasive->control_regs.cplt_cfg1 |= val;
        break;
    case CPLT_CONF1_CLEAR:
        nest_pervasive->control_regs.cplt_cfg1 &= ~val;
        break;
    case CPLT_STAT0:
        nest_pervasive->control_regs.cplt_stat0 = val;
        break;
    case CPLT_MASK0:
        nest_pervasive->control_regs.cplt_mask0 = val;
        break;
    case CPLT_PROTECT_MODE:
        nest_pervasive->control_regs.ctrl_protect_mode = val;
        break;
    case CPLT_ATOMIC_CLOCK:
        nest_pervasive->control_regs.ctrl_atomic_lock = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Chiplet_control_regs: Invalid xscom "
                                 "write at 0x%" PRIx32 "\n",
                                 __func__, reg);
    }
}

static const MemoryRegionOps pnv_nest_pervasive_control_xscom_ops = {
    .read = pnv_chiplet_ctrl_read,
    .write = pnv_chiplet_ctrl_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_nest_pervasive_realize(DeviceState *dev, Error **errp)
{
    PnvNestChipletPervasive *nest_pervasive = PNV_NEST_CHIPLET_PERVASIVE(dev);

    /* Chiplet control scoms */
    pnv_xscom_region_init(&nest_pervasive->xscom_ctrl_regs_mr,
                          OBJECT(nest_pervasive),
                          &pnv_nest_pervasive_control_xscom_ops,
                          nest_pervasive, "xscom-pervasive-control",
                          PNV10_XSCOM_CHIPLET_CTRL_REGS_SIZE);
}

static void pnv_nest_pervasive_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PowerNV nest pervasive chiplet";
    dc->realize = pnv_nest_pervasive_realize;
}

static const TypeInfo pnv_nest_pervasive_info = {
    .name          = TYPE_PNV_NEST_CHIPLET_PERVASIVE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvNestChipletPervasive),
    .class_init    = pnv_nest_pervasive_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_nest_pervasive_register_types(void)
{
    type_register_static(&pnv_nest_pervasive_info);
}

type_init(pnv_nest_pervasive_register_types);
