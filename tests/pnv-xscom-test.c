/*
 * QTest testcase for PowerNV XSCOM bus
 *
 * Copyright (c) 2016, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"

#include "libqtest.h"

typedef enum PnvChipType {
    PNV_CHIP_POWER8E,     /* AKA Murano (default) */
    PNV_CHIP_POWER8,      /* AKA Venice */
    PNV_CHIP_POWER8NVL,   /* AKA Naples */
    PNV_CHIP_POWER9,      /* AKA Nimbus */
} PnvChipType;

typedef struct PnvChip {
    PnvChipType chip_type;
    const char *cpu_model;
    uint64_t    xscom_base;
    uint64_t    cfam_id;
    uint32_t    first_core;
} PnvChip;

static const PnvChip pnv_chips[] = {
    {
        .chip_type  = PNV_CHIP_POWER8,
        .cpu_model  = "POWER8",
        .xscom_base = 0x0003fc0000000000ull,
        .cfam_id    = 0x220ea04980000000ull,
        .first_core = 0x1,
    }, {
        .chip_type  = PNV_CHIP_POWER8NVL,
        .cpu_model  = "POWER8NVL",
        .xscom_base = 0x0003fc0000000000ull,
        .cfam_id    = 0x120d304980000000ull,
        .first_core = 0x1,
    },
#if 0 /* POWER9 support is not ready yet */
    {
        .chip_type  = PNV_CHIP_POWER9,
        .cpu_model  = "POWER9",
        .xscom_base = 0x000603fc00000000ull,
        .cfam_id    = 0x220d104900008000ull,
        .first_core = 0x0,
    },
#endif
};

static uint64_t pnv_xscom_addr(const PnvChip *chip, uint32_t pcba)
{
    uint64_t addr = chip->xscom_base;

    if (chip->chip_type == PNV_CHIP_POWER9) {
        addr |= ((uint64_t) pcba << 3);
    } else {
        addr |= (((uint64_t) pcba << 4) & ~0xffull) |
            (((uint64_t) pcba << 3) & 0x78);
    }
    return addr;
}

static uint64_t pnv_xscom_read(const PnvChip *chip, uint32_t pcba)
{
    return readq(pnv_xscom_addr(chip, pcba));
}

static void test_xscom_cfam_id(const PnvChip *chip)
{
    uint64_t f000f = pnv_xscom_read(chip, 0xf000f);

    g_assert_cmphex(f000f, ==, chip->cfam_id);
}

static void test_cfam_id(const void *data)
{
    const PnvChip *chip = data;

    global_qtest = qtest_initf("-M powernv,accel=tcg -cpu %s",
                               chip->cpu_model);
    test_xscom_cfam_id(chip);
    qtest_quit(global_qtest);
}


#define PNV_XSCOM_EX_CORE_BASE    0x10000000ull
#define PNV_XSCOM_EX_BASE(core) \
    (PNV_XSCOM_EX_CORE_BASE | ((uint64_t)(core) << 24))
#define PNV_XSCOM_P9_EC_BASE(core) \
    ((uint64_t)(((core) & 0x1F) + 0x20) << 24)

#define PNV_XSCOM_EX_DTS_RESULT0     0x50000

static void test_xscom_core(const PnvChip *chip)
{
    uint32_t first_core_dts0 = PNV_XSCOM_EX_DTS_RESULT0;
    uint64_t dts0;

    if (chip->chip_type != PNV_CHIP_POWER9) {
        first_core_dts0 |= PNV_XSCOM_EX_BASE(chip->first_core);
    } else {
        first_core_dts0 |= PNV_XSCOM_P9_EC_BASE(chip->first_core);
    }

    dts0 = pnv_xscom_read(chip, first_core_dts0);

    g_assert_cmphex(dts0, ==, 0x26f024f023f0000ull);
}

static void test_core(const void *data)
{
    const PnvChip *chip = data;

    global_qtest = qtest_initf("-M powernv,accel=tcg -cpu %s",
                               chip->cpu_model);
    test_xscom_core(chip);
    qtest_quit(global_qtest);
}

static void add_test(const char *name, void (*test)(const void *data))
{
    int i;

    for (i = 0; i < ARRAY_SIZE(pnv_chips); i++) {
        char *tname = g_strdup_printf("pnv-xscom/%s/%s", name,
                                      pnv_chips[i].cpu_model);
        qtest_add_data_func(tname, &pnv_chips[i], test);
        g_free(tname);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    add_test("cfam_id", test_cfam_id);
    add_test("core", test_core);
    return g_test_run();
}
