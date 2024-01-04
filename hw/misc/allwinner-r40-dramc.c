/*
 * Allwinner R40 SDRAM Controller emulation
 *
 * CCopyright (C) 2023 qianfan Zhao <qianfanguijin@163.com>
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
#include "exec/address-spaces.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "hw/misc/allwinner-r40-dramc.h"
#include "trace.h"

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

/* DRAMCOM register offsets */
enum {
    REG_DRAMCOM_CR    = 0x0000, /* Control Register */
};

/* DRAMCOMM register flags */
enum {
    REG_DRAMCOM_CR_DUAL_RANK = (1 << 0),
};

/* DRAMCTL register offsets */
enum {
    REG_DRAMCTL_PIR   = 0x0000, /* PHY Initialization Register */
    REG_DRAMCTL_PGSR  = 0x0010, /* PHY General Status Register */
    REG_DRAMCTL_STATR = 0x0018, /* Status Register */
    REG_DRAMCTL_PGCR  = 0x0100, /* PHY general configuration registers */
};

/* DRAMCTL register flags */
enum {
    REG_DRAMCTL_PGSR_INITDONE = (1 << 0),
    REG_DRAMCTL_PGSR_READ_TIMEOUT = (1 << 13),
    REG_DRAMCTL_PGCR_ENABLE_READ_TIMEOUT = (1 << 25),
};

enum {
    REG_DRAMCTL_STATR_ACTIVE  = (1 << 0),
};

#define DRAM_MAX_ROW_BITS       16
#define DRAM_MAX_COL_BITS       13  /* 8192 */
#define DRAM_MAX_BANK            3

static uint64_t dram_autodetect_cells[DRAM_MAX_ROW_BITS]
                                     [DRAM_MAX_BANK]
                                     [DRAM_MAX_COL_BITS];
struct VirtualDDRChip {
    uint32_t    ram_size;
    uint8_t     bank_bits;
    uint8_t     row_bits;
    uint8_t     col_bits;
};

/*
 * Only power of 2 RAM sizes from 256MiB up to 2048MiB are supported,
 * 2GiB memory is not supported due to dual rank feature.
 */
static const struct VirtualDDRChip dummy_ddr_chips[] = {
    {
        .ram_size   = 256,
        .bank_bits  = 3,
        .row_bits   = 12,
        .col_bits   = 13,
    }, {
        .ram_size   = 512,
        .bank_bits  = 3,
        .row_bits   = 13,
        .col_bits   = 13,
    }, {
        .ram_size   = 1024,
        .bank_bits  = 3,
        .row_bits   = 14,
        .col_bits   = 13,
    }, {
        0
    }
};

static const struct VirtualDDRChip *get_match_ddr(uint32_t ram_size)
{
    const struct VirtualDDRChip *ddr;

    for (ddr = &dummy_ddr_chips[0]; ddr->ram_size; ddr++) {
        if (ddr->ram_size == ram_size) {
            return ddr;
        }
    }

    return NULL;
}

static uint64_t *address_to_autodetect_cells(AwR40DramCtlState *s,
                                             const struct VirtualDDRChip *ddr,
                                             uint32_t offset)
{
    int row_index = 0, bank_index = 0, col_index = 0;
    uint32_t row_addr, bank_addr, col_addr;

    row_addr = extract32(offset, s->set_col_bits + s->set_bank_bits,
                         s->set_row_bits);
    bank_addr = extract32(offset, s->set_col_bits, s->set_bank_bits);
    col_addr = extract32(offset, 0, s->set_col_bits);

    for (int i = 0; i < ddr->row_bits; i++) {
        if (row_addr & BIT(i)) {
            row_index = i;
        }
    }

    for (int i = 0; i < ddr->bank_bits; i++) {
        if (bank_addr & BIT(i)) {
            bank_index = i;
        }
    }

    for (int i = 0; i < ddr->col_bits; i++) {
        if (col_addr & BIT(i)) {
            col_index = i;
        }
    }

    trace_allwinner_r40_dramc_offset_to_cell(offset, row_index, bank_index,
                                             col_index);
    return &dram_autodetect_cells[row_index][bank_index][col_index];
}

static void allwinner_r40_dramc_map_rows(AwR40DramCtlState *s, uint8_t row_bits,
                                         uint8_t bank_bits, uint8_t col_bits)
{
    const struct VirtualDDRChip *ddr = get_match_ddr(s->ram_size);
    bool enable_detect_cells;

    trace_allwinner_r40_dramc_map_rows(row_bits, bank_bits, col_bits);

    if (!ddr) {
        return;
    }

    s->set_row_bits = row_bits;
    s->set_bank_bits = bank_bits;
    s->set_col_bits = col_bits;

    enable_detect_cells = ddr->bank_bits != bank_bits
                        || ddr->row_bits != row_bits
                        || ddr->col_bits != col_bits;

    if (enable_detect_cells) {
        trace_allwinner_r40_dramc_detect_cells_enable();
    } else {
        trace_allwinner_r40_dramc_detect_cells_disable();
    }

    memory_region_set_enabled(&s->detect_cells, enable_detect_cells);
}

static uint64_t allwinner_r40_dramcom_read(void *opaque, hwaddr offset,
                                           unsigned size)
{
    const AwR40DramCtlState *s = AW_R40_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    if (idx >= AW_R40_DRAMCOM_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    trace_allwinner_r40_dramcom_read(offset, s->dramcom[idx], size);
    return s->dramcom[idx];
}

static void allwinner_r40_dramcom_write(void *opaque, hwaddr offset,
                                        uint64_t val, unsigned size)
{
    AwR40DramCtlState *s = AW_R40_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_allwinner_r40_dramcom_write(offset, val, size);

    if (idx >= AW_R40_DRAMCOM_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    switch (offset) {
    case REG_DRAMCOM_CR:   /* Control Register */
        if (!(val & REG_DRAMCOM_CR_DUAL_RANK)) {
            allwinner_r40_dramc_map_rows(s, ((val >> 4) & 0xf) + 1,
                                         ((val >> 2) & 0x1) + 2,
                                         (((val >> 8) & 0xf) + 3));
        }
        break;
    };

    s->dramcom[idx] = (uint32_t) val;
}

static uint64_t allwinner_r40_dramctl_read(void *opaque, hwaddr offset,
                                           unsigned size)
{
    const AwR40DramCtlState *s = AW_R40_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    if (idx >= AW_R40_DRAMCTL_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    trace_allwinner_r40_dramctl_read(offset, s->dramctl[idx], size);
    return s->dramctl[idx];
}

static void allwinner_r40_dramctl_write(void *opaque, hwaddr offset,
                                        uint64_t val, unsigned size)
{
    AwR40DramCtlState *s = AW_R40_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_allwinner_r40_dramctl_write(offset, val, size);

    if (idx >= AW_R40_DRAMCTL_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    switch (offset) {
    case REG_DRAMCTL_PIR:    /* PHY Initialization Register */
        s->dramctl[REG_INDEX(REG_DRAMCTL_PGSR)] |= REG_DRAMCTL_PGSR_INITDONE;
        s->dramctl[REG_INDEX(REG_DRAMCTL_STATR)] |= REG_DRAMCTL_STATR_ACTIVE;
        break;
    }

    s->dramctl[idx] = (uint32_t) val;
}

static uint64_t allwinner_r40_dramphy_read(void *opaque, hwaddr offset,
                                           unsigned size)
{
    const AwR40DramCtlState *s = AW_R40_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    if (idx >= AW_R40_DRAMPHY_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    trace_allwinner_r40_dramphy_read(offset, s->dramphy[idx], size);
    return s->dramphy[idx];
}

static void allwinner_r40_dramphy_write(void *opaque, hwaddr offset,
                                        uint64_t val, unsigned size)
{
    AwR40DramCtlState *s = AW_R40_DRAMC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_allwinner_r40_dramphy_write(offset, val, size);

    if (idx >= AW_R40_DRAMPHY_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    s->dramphy[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_r40_dramcom_ops = {
    .read = allwinner_r40_dramcom_read,
    .write = allwinner_r40_dramcom_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static const MemoryRegionOps allwinner_r40_dramctl_ops = {
    .read = allwinner_r40_dramctl_read,
    .write = allwinner_r40_dramctl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static const MemoryRegionOps allwinner_r40_dramphy_ops = {
    .read = allwinner_r40_dramphy_read,
    .write = allwinner_r40_dramphy_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static uint64_t allwinner_r40_detect_read(void *opaque, hwaddr offset,
                                          unsigned size)
{
    AwR40DramCtlState *s = AW_R40_DRAMC(opaque);
    const struct VirtualDDRChip *ddr = get_match_ddr(s->ram_size);
    uint64_t data = 0;

    if (ddr) {
        data = *address_to_autodetect_cells(s, ddr, (uint32_t)offset);
    }

    trace_allwinner_r40_dramc_detect_cell_read(offset, data);
    return data;
}

static void allwinner_r40_detect_write(void *opaque, hwaddr offset,
                                       uint64_t data, unsigned size)
{
    AwR40DramCtlState *s = AW_R40_DRAMC(opaque);
    const struct VirtualDDRChip *ddr = get_match_ddr(s->ram_size);

    if (ddr) {
        uint64_t *cell = address_to_autodetect_cells(s, ddr, (uint32_t)offset);
        trace_allwinner_r40_dramc_detect_cell_write(offset, data);
        *cell = data;
    }
}

static const MemoryRegionOps allwinner_r40_detect_ops = {
    .read = allwinner_r40_detect_read,
    .write = allwinner_r40_detect_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

/*
 * mctl_r40_detect_rank_count in u-boot will write the high 1G of DDR
 * to detect whether the board support dual_rank or not. Create a virtual memory
 * if the board's ram_size less or equal than 1G, and set read time out flag of
 * REG_DRAMCTL_PGSR when the user touch this high dram.
 */
static uint64_t allwinner_r40_dualrank_detect_read(void *opaque, hwaddr offset,
                                                   unsigned size)
{
    AwR40DramCtlState *s = AW_R40_DRAMC(opaque);
    uint32_t reg;

    reg = s->dramctl[REG_INDEX(REG_DRAMCTL_PGCR)];
    if (reg & REG_DRAMCTL_PGCR_ENABLE_READ_TIMEOUT) { /* Enable read time out */
        /*
         * this driver only support one rank, mark READ_TIMEOUT when try
         * read the second rank.
         */
        s->dramctl[REG_INDEX(REG_DRAMCTL_PGSR)]
                                |= REG_DRAMCTL_PGSR_READ_TIMEOUT;
    }

    return 0;
}

static const MemoryRegionOps allwinner_r40_dualrank_detect_ops = {
    .read = allwinner_r40_dualrank_detect_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_r40_dramc_reset(DeviceState *dev)
{
    AwR40DramCtlState *s = AW_R40_DRAMC(dev);

    /* Set default values for registers */
    memset(&s->dramcom, 0, sizeof(s->dramcom));
    memset(&s->dramctl, 0, sizeof(s->dramctl));
    memset(&s->dramphy, 0, sizeof(s->dramphy));
}

static void allwinner_r40_dramc_realize(DeviceState *dev, Error **errp)
{
    AwR40DramCtlState *s = AW_R40_DRAMC(dev);

    if (!get_match_ddr(s->ram_size)) {
        error_report("%s: ram-size %u MiB is not supported",
                        __func__, s->ram_size);
        exit(1);
    }

    /* R40 support max 2G memory but we only support up to 1G now. */
    memory_region_init_io(&s->detect_cells, OBJECT(s),
                          &allwinner_r40_detect_ops, s,
                          "DRAMCELLS", 1 * GiB);
    memory_region_add_subregion_overlap(get_system_memory(), s->ram_addr,
                                        &s->detect_cells, 10);
    memory_region_set_enabled(&s->detect_cells, false);

    /*
     * We only support DRAM size up to 1G now, so prepare a high memory page
     * after 1G for dualrank detect.
     */
    memory_region_init_io(&s->dram_high, OBJECT(s),
                            &allwinner_r40_dualrank_detect_ops, s,
                            "DRAMHIGH", KiB);
    memory_region_add_subregion(get_system_memory(), s->ram_addr + GiB,
                                &s->dram_high);
}

static void allwinner_r40_dramc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwR40DramCtlState *s = AW_R40_DRAMC(obj);

    /* DRAMCOM registers, index 0 */
    memory_region_init_io(&s->dramcom_iomem, OBJECT(s),
                          &allwinner_r40_dramcom_ops, s,
                          "DRAMCOM", 4 * KiB);
    sysbus_init_mmio(sbd, &s->dramcom_iomem);

    /* DRAMCTL registers, index 1 */
    memory_region_init_io(&s->dramctl_iomem, OBJECT(s),
                          &allwinner_r40_dramctl_ops, s,
                          "DRAMCTL", 4 * KiB);
    sysbus_init_mmio(sbd, &s->dramctl_iomem);

    /* DRAMPHY registers. index 2 */
    memory_region_init_io(&s->dramphy_iomem, OBJECT(s),
                          &allwinner_r40_dramphy_ops, s,
                          "DRAMPHY", 4 * KiB);
    sysbus_init_mmio(sbd, &s->dramphy_iomem);
}

static Property allwinner_r40_dramc_properties[] = {
    DEFINE_PROP_UINT64("ram-addr", AwR40DramCtlState, ram_addr, 0x0),
    DEFINE_PROP_UINT32("ram-size", AwR40DramCtlState, ram_size, 256), /* MiB */
    DEFINE_PROP_END_OF_LIST()
};

static const VMStateDescription allwinner_r40_dramc_vmstate = {
    .name = "allwinner-r40-dramc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(dramcom, AwR40DramCtlState,
                             AW_R40_DRAMCOM_REGS_NUM),
        VMSTATE_UINT32_ARRAY(dramctl, AwR40DramCtlState,
                             AW_R40_DRAMCTL_REGS_NUM),
        VMSTATE_UINT32_ARRAY(dramphy, AwR40DramCtlState,
                             AW_R40_DRAMPHY_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_r40_dramc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = allwinner_r40_dramc_reset;
    dc->vmsd = &allwinner_r40_dramc_vmstate;
    dc->realize = allwinner_r40_dramc_realize;
    device_class_set_props(dc, allwinner_r40_dramc_properties);
}

static const TypeInfo allwinner_r40_dramc_info = {
    .name          = TYPE_AW_R40_DRAMC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_r40_dramc_init,
    .instance_size = sizeof(AwR40DramCtlState),
    .class_init    = allwinner_r40_dramc_class_init,
};

static void allwinner_r40_dramc_register(void)
{
    type_register_static(&allwinner_r40_dramc_info);
}

type_init(allwinner_r40_dramc_register)
