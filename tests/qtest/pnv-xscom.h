/*
 * PowerNV XSCOM Bus
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PNV_XSCOM_H
#define PNV_XSCOM_H

#define SMT                     4 /* some tests will break if less than 4 */

typedef enum PnvChipType {
    PNV_CHIP_POWER8E,     /* AKA Murano (default) */
    PNV_CHIP_POWER8,      /* AKA Venice */
    PNV_CHIP_POWER8NVL,   /* AKA Naples */
    PNV_CHIP_POWER9,      /* AKA Nimbus */
    PNV_CHIP_POWER10,
} PnvChipType;

typedef struct PnvChip {
    PnvChipType chip_type;
    const char *cpu_model;
    uint64_t    xscom_base;
    uint64_t    cfam_id;
    uint32_t    first_core;
    uint32_t    num_i2c;
} PnvChip;

static const PnvChip pnv_chips[] = {
    {
        .chip_type  = PNV_CHIP_POWER8,
        .cpu_model  = "POWER8",
        .xscom_base = 0x0003fc0000000000ull,
        .cfam_id    = 0x220ea04980000000ull,
        .first_core = 0x1,
        .num_i2c    = 0,
    }, {
        .chip_type  = PNV_CHIP_POWER8NVL,
        .cpu_model  = "POWER8NVL",
        .xscom_base = 0x0003fc0000000000ull,
        .cfam_id    = 0x120d304980000000ull,
        .first_core = 0x1,
        .num_i2c    = 0,
    },
    {
        .chip_type  = PNV_CHIP_POWER9,
        .cpu_model  = "POWER9",
        .xscom_base = 0x000603fc00000000ull,
        .cfam_id    = 0x220d104900008000ull,
        .first_core = 0x0,
        .num_i2c    = 4,
    },
    {
        .chip_type  = PNV_CHIP_POWER10,
        .cpu_model  = "POWER10",
        .xscom_base = 0x000603fc00000000ull,
        .cfam_id    = 0x220da04980000000ull,
        .first_core = 0x0,
        .num_i2c    = 4,
    },
};

static inline uint64_t pnv_xscom_addr(const PnvChip *chip, uint32_t pcba)
{
    uint64_t addr = chip->xscom_base;

    if (chip->chip_type == PNV_CHIP_POWER10) {
        addr |= ((uint64_t) pcba << 3);
    } else if (chip->chip_type == PNV_CHIP_POWER9) {
        addr |= ((uint64_t) pcba << 3);
    } else {
        addr |= (((uint64_t) pcba << 4) & ~0xffull) |
            (((uint64_t) pcba << 3) & 0x78);
    }
    return addr;
}

#endif /* PNV_XSCOM_H */
