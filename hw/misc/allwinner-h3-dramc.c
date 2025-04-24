/*
 * Allwinner H3 SDRAM Controller emulation
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
#include "qemu/error-report.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/address-spaces.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "hw/misc/allwinner-h3-dramc.h"
#include "trace.h"

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

/* DRAMCOM register offsets */
enum {
    REG_DRAMCOM_CR    = 0x0000, /* Control Register */
};

/* DRAMCTL register offsets */
enum {
    REG_DRAMCTL_PIR   = 0x0000, /* PHY Initialization Register */
    REG_DRAMCTL_PGSR  = 0x0010, /* PHY General Status Register */
    REG_DRAMCTL_STATR = 0x0018, /* Status Register */
};

/* DRAMCTL register flags */
enum {
    REG_DRAMCTL_PGSR_INITDONE = (1 << 0),
};

enum {
    REG_DRAMCTL_STATR_ACTIVE  = (1 << 0),
};

static void allwinner_h3_dramc_map_rows(AwH3DramCtlState *s, uint8_t row_bits,
                                        uint8_t bank_bits, uint16_t page_size)
{
    /*
     * This function simulates row addressing behavior when bootloader
     * software attempts to detect the amount of available SDRAM. In U-Boot
     * the controller is configured with the widest row addressing available.
     * Then a pattern is written to RAM at an offset on the row boundary size.
     * If the value read back equals the value read back from the
     * start of RAM, the bootloader knows the amount of row bits.
     *
     * This function inserts a mirrored memory region when the configured row
     * bits are not matching the actual emulated memory, to simulate the
     * same behavior on hardware as expected by the bootloader.
     */
    uint8_t row_bits_actual = 0;

    /* Calculate the actual row bits using the ram_size property */
    for (uint8_t i = 8; i < 12; i++) {
        if (1 << i == s->ram_size) {
            row_bits_actual = i + 3;
            break;
        }
    }

    if (s->ram_size == (1 << (row_bits - 3))) {
        /* When row bits is the expected value, remove the mirror */
        memory_region_set_enabled(&s->row_mirror_alias, false);
        trace_allwinner_h3_dramc_rowmirror_disable();

    } else if (row_bits_actual) {
        /* Row bits not matching ram_size, install the rows mirror */
        hwaddr row_mirror = s->ram_addr + ((1ULL << (row_bits_actual +
                                                     bank_bits)) * page_size);

        memory_region_set_enabled(&s->row_mirror_alias, true);
        memory_region_set_address(&s->row_mirror_alias, row_mirror);

        trace_allwinner_h3_dramc_rowmirror_enable(row_mirror);
    }
}

static uint64_t allwinner_h3_dramcom_read(void *opaque, hwaddr offset,
                                          unsigned size)
{
    const AwH3DramCtlState *s = AW_H3_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    if (idx >= AW_H3_DRAMCOM_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    trace_allwinner_h3_dramcom_read(offset, s->dramcom[idx], size);

    return s->dramcom[idx];
}

static void allwinner_h3_dramcom_write(void *opaque, hwaddr offset,
                                       uint64_t val, unsigned size)
{
    AwH3DramCtlState *s = AW_H3_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_allwinner_h3_dramcom_write(offset, val, size);

    if (idx >= AW_H3_DRAMCOM_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    switch (offset) {
    case REG_DRAMCOM_CR:   /* Control Register */
        allwinner_h3_dramc_map_rows(s, ((val >> 4) & 0xf) + 1,
                                       ((val >> 2) & 0x1) + 2,
                                       1 << (((val >> 8) & 0xf) + 3));
        break;
    default:
        break;
    };

    s->dramcom[idx] = (uint32_t) val;
}

static uint64_t allwinner_h3_dramctl_read(void *opaque, hwaddr offset,
                                          unsigned size)
{
    const AwH3DramCtlState *s = AW_H3_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    if (idx >= AW_H3_DRAMCTL_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    trace_allwinner_h3_dramctl_read(offset, s->dramctl[idx], size);

    return s->dramctl[idx];
}

static void allwinner_h3_dramctl_write(void *opaque, hwaddr offset,
                                       uint64_t val, unsigned size)
{
    AwH3DramCtlState *s = AW_H3_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_allwinner_h3_dramctl_write(offset, val, size);

    if (idx >= AW_H3_DRAMCTL_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    switch (offset) {
    case REG_DRAMCTL_PIR:    /* PHY Initialization Register */
        s->dramctl[REG_INDEX(REG_DRAMCTL_PGSR)] |= REG_DRAMCTL_PGSR_INITDONE;
        s->dramctl[REG_INDEX(REG_DRAMCTL_STATR)] |= REG_DRAMCTL_STATR_ACTIVE;
        break;
    default:
        break;
    }

    s->dramctl[idx] = (uint32_t) val;
}

static uint64_t allwinner_h3_dramphy_read(void *opaque, hwaddr offset,
                                          unsigned size)
{
    const AwH3DramCtlState *s = AW_H3_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    if (idx >= AW_H3_DRAMPHY_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    trace_allwinner_h3_dramphy_read(offset, s->dramphy[idx], size);

    return s->dramphy[idx];
}

static void allwinner_h3_dramphy_write(void *opaque, hwaddr offset,
                                       uint64_t val, unsigned size)
{
    AwH3DramCtlState *s = AW_H3_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_allwinner_h3_dramphy_write(offset, val, size);

    if (idx >= AW_H3_DRAMPHY_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    s->dramphy[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_h3_dramcom_ops = {
    .read = allwinner_h3_dramcom_read,
    .write = allwinner_h3_dramcom_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static const MemoryRegionOps allwinner_h3_dramctl_ops = {
    .read = allwinner_h3_dramctl_read,
    .write = allwinner_h3_dramctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static const MemoryRegionOps allwinner_h3_dramphy_ops = {
    .read = allwinner_h3_dramphy_read,
    .write = allwinner_h3_dramphy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_h3_dramc_reset(DeviceState *dev)
{
    AwH3DramCtlState *s = AW_H3_DRAMC(dev);

    /* Set default values for registers */
    memset(&s->dramcom, 0, sizeof(s->dramcom));
    memset(&s->dramctl, 0, sizeof(s->dramctl));
    memset(&s->dramphy, 0, sizeof(s->dramphy));
}

static void allwinner_h3_dramc_realize(DeviceState *dev, Error **errp)
{
    AwH3DramCtlState *s = AW_H3_DRAMC(dev);

    /* Only power of 2 RAM sizes from 256MiB up to 2048MiB are supported */
    for (uint8_t i = 8; i < 13; i++) {
        if (1 << i == s->ram_size) {
            break;
        } else if (i == 12) {
            error_report("%s: ram-size %u MiB is not supported",
                          __func__, s->ram_size);
            exit(1);
        }
    }

    /* Setup row mirror mappings */
    memory_region_init_ram(&s->row_mirror, OBJECT(s),
                           "allwinner-h3-dramc.row-mirror",
                            4 * KiB, &error_abort);
    memory_region_add_subregion_overlap(get_system_memory(), s->ram_addr,
                                       &s->row_mirror, 10);

    memory_region_init_alias(&s->row_mirror_alias, OBJECT(s),
                            "allwinner-h3-dramc.row-mirror-alias",
                            &s->row_mirror, 0, 4 * KiB);
    memory_region_add_subregion_overlap(get_system_memory(),
                                        s->ram_addr + 1 * MiB,
                                       &s->row_mirror_alias, 10);
    memory_region_set_enabled(&s->row_mirror_alias, false);
}

static void allwinner_h3_dramc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwH3DramCtlState *s = AW_H3_DRAMC(obj);

    /* DRAMCOM registers */
    memory_region_init_io(&s->dramcom_iomem, OBJECT(s),
                          &allwinner_h3_dramcom_ops, s,
                           TYPE_AW_H3_DRAMC, 4 * KiB);
    sysbus_init_mmio(sbd, &s->dramcom_iomem);

    /* DRAMCTL registers */
    memory_region_init_io(&s->dramctl_iomem, OBJECT(s),
                          &allwinner_h3_dramctl_ops, s,
                           TYPE_AW_H3_DRAMC, 4 * KiB);
    sysbus_init_mmio(sbd, &s->dramctl_iomem);

    /* DRAMPHY registers */
    memory_region_init_io(&s->dramphy_iomem, OBJECT(s),
                          &allwinner_h3_dramphy_ops, s,
                          TYPE_AW_H3_DRAMC, 4 * KiB);
    sysbus_init_mmio(sbd, &s->dramphy_iomem);
}

static const Property allwinner_h3_dramc_properties[] = {
    DEFINE_PROP_UINT64("ram-addr", AwH3DramCtlState, ram_addr, 0x0),
    DEFINE_PROP_UINT32("ram-size", AwH3DramCtlState, ram_size, 256 * MiB),
};

static const VMStateDescription allwinner_h3_dramc_vmstate = {
    .name = "allwinner-h3-dramc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(dramcom, AwH3DramCtlState, AW_H3_DRAMCOM_REGS_NUM),
        VMSTATE_UINT32_ARRAY(dramctl, AwH3DramCtlState, AW_H3_DRAMCTL_REGS_NUM),
        VMSTATE_UINT32_ARRAY(dramphy, AwH3DramCtlState, AW_H3_DRAMPHY_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_h3_dramc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, allwinner_h3_dramc_reset);
    dc->vmsd = &allwinner_h3_dramc_vmstate;
    dc->realize = allwinner_h3_dramc_realize;
    device_class_set_props(dc, allwinner_h3_dramc_properties);
}

static const TypeInfo allwinner_h3_dramc_info = {
    .name          = TYPE_AW_H3_DRAMC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_h3_dramc_init,
    .instance_size = sizeof(AwH3DramCtlState),
    .class_init    = allwinner_h3_dramc_class_init,
};

static void allwinner_h3_dramc_register(void)
{
    type_register_static(&allwinner_h3_dramc_info);
}

type_init(allwinner_h3_dramc_register)
