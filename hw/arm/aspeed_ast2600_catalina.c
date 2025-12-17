/*
 * Facebook Catalina
 *
 * Copyright 2016 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/aspeed.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/i2c/i2c_mux_pca954x.h"
#include "hw/gpio/pca9552.h"
#include "hw/gpio/pca9554.h"
#include "hw/nvram/eeprom_at24c.h"
#include "hw/sensor/tmp105.h"

/* Catalina hardware value */
#define CATALINA_BMC_HW_STRAP1 0x00002002
#define CATALINA_BMC_HW_STRAP2 0x00000800
#define CATALINA_BMC_RAM_SIZE ASPEED_RAM_SIZE(2 * GiB)

#define TYPE_TMP75 TYPE_TMP105
#define TYPE_TMP421 "tmp421"
#define TYPE_DS1338 "ds1338"

/*
 * "BMC Storage Module" FRU data.  Generated with frugen.
 *
 *    {
 *      "board": {
 *        "mfg": "Quanta",
 *        "pname": "BMC Storage Module (QEMU)",
 *        "pn": "00000000000",
 *        "serial": "00000000000000",
 *        "date": "01/12/2025 00:00",
 *        "custom": ["09-100183"]
 *      },
 *      "product": {
 *        "mfg": "Quanta",
 *        "pname": "CI-Catalina",
 *        "pn": "10000000001",
 *        "ver": "MP",
 *        "serial": "10000000000000",
 *        "atag": "QEMU"
 *      }
 *    }
 */
static const uint8_t bsm_eeprom[] = {
    0x01, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0xf4, 0x01, 0x09, 0x19, 0x8c,
    0x19, 0xf0, 0xc6, 0x51, 0x75, 0x61, 0x6e, 0x74, 0x61, 0xd9, 0x42, 0x4d,
    0x43, 0x20, 0x53, 0x74, 0x6f, 0x72, 0x61, 0x67, 0x65, 0x20, 0x4d, 0x6f,
    0x64, 0x75, 0x6c, 0x65, 0x20, 0x28, 0x51, 0x45, 0x4d, 0x55, 0x29, 0x8b,
    0x10, 0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x89,
    0x10, 0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x01, 0xc0, 0x87, 0x50,
    0xd6, 0x44, 0x10, 0x14, 0x61, 0x13, 0xc1, 0x59, 0x01, 0x07, 0x19, 0xc6,
    0x51, 0x75, 0x61, 0x6e, 0x74, 0x61, 0xcb, 0x43, 0x49, 0x2d, 0x43, 0x61,
    0x74, 0x61, 0x6c, 0x69, 0x6e, 0x61, 0x89, 0x11, 0x04, 0x41, 0x10, 0x04,
    0x41, 0x10, 0x14, 0x01, 0x82, 0x2d, 0x0c, 0x8b, 0x11, 0x04, 0x41, 0x10,
    0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x83, 0x71, 0xd9, 0xd6, 0xc0,
    0xc1, 0x00, 0x00, 0x37
};
static const size_t bsm_eeprom_len = sizeof(bsm_eeprom);

/*
 * "Secure Control Module" FRU data. Generated with frugen.
 *
 *    {
 *      "board": {
 *        "mfg": "Quanta",
 *        "pname": "Catalina SCM MP (QEMU)",
 *        "pn": "00000000000",
 *        "serial": "00000000000000",
 *        "date": "01/12/2025 00:00",
 *        "custom": ["19-100325"]
 *      },
 *      "product": {
 *        "mfg": "Quanta",
 *        "pname": "CI-Catalina",
 *        "pn": "10000000001",
 *        "ver": "MP",
 *        "serial": "10000000000000",
 *        "atag": "QEMU"
 *      }
 *    }
 *
 */
static const uint8_t scm_eeprom[] = {
    0x01, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0xf4, 0x01, 0x09, 0x19, 0x8c,
    0x19, 0xf0, 0xc6, 0x51, 0x75, 0x61, 0x6e, 0x74, 0x61, 0xd6, 0x43, 0x61,
    0x74, 0x61, 0x6c, 0x69, 0x6e, 0x61, 0x20, 0x53, 0x43, 0x4d, 0x20, 0x4d,
    0x50, 0x20, 0x28, 0x51, 0x45, 0x4d, 0x55, 0x29, 0x8b, 0x10, 0x04, 0x41,
    0x10, 0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x89, 0x10, 0x04, 0x41,
    0x10, 0x04, 0x41, 0x10, 0x04, 0x01, 0xc0, 0x87, 0x51, 0xd6, 0x44, 0x10,
    0x34, 0x49, 0x15, 0xc1, 0x00, 0x00, 0x00, 0xc1, 0x01, 0x07, 0x19, 0xc6,
    0x51, 0x75, 0x61, 0x6e, 0x74, 0x61, 0xcb, 0x43, 0x49, 0x2d, 0x43, 0x61,
    0x74, 0x61, 0x6c, 0x69, 0x6e, 0x61, 0x89, 0x11, 0x04, 0x41, 0x10, 0x04,
    0x41, 0x10, 0x14, 0x01, 0x82, 0x2d, 0x0c, 0x8b, 0x11, 0x04, 0x41, 0x10,
    0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x83, 0x71, 0xd9, 0xd6, 0xc0,
    0xc1, 0x00, 0x00, 0x37
};
static const size_t scm_eeprom_len = sizeof(scm_eeprom);

/*
 * "Power Distribution Board" FRU data. Generated with frugen.
 *
 *    {
 *        "board": {
 *            "mfg": "Quanta",
 *            "pname": "Catalina PDB MP (QEMU)",
 *            "pn": "00000000000",
 *            "serial": "00000000000000",
 *            "date": "01/12/2025 00:00",
 *            "custom": [
 *                "19-100579",
 *                "",
 *                "",
 *                "hsc-ltc fsc-max vr-delta gndsen-ina p12vsen-ina p12vfan-mps"
 *            ]
 *        },
 *        "product": {
 *            "mfg": "Quanta",
 *            "pname": "CI-Catalina",
 *            "pn": "10000000001",
 *            "ver": "MP",
 *            "serial": "10000000000000",
 *            "atag": "QEMU"
 *        }
 *    }
 */
static const uint8_t pdb_eeprom[] = {
    0x01, 0x00, 0x00, 0x01, 0x12, 0x00, 0x00, 0xec, 0x01, 0x11, 0x19, 0x8c,
    0x19, 0xf0, 0xc6, 0x51, 0x75, 0x61, 0x6e, 0x74, 0x61, 0xd6, 0x43, 0x61,
    0x74, 0x61, 0x6c, 0x69, 0x6e, 0x61, 0x20, 0x50, 0x44, 0x42, 0x20, 0x4d,
    0x50, 0x20, 0x28, 0x51, 0x45, 0x4d, 0x55, 0x29, 0x8b, 0x10, 0x04, 0x41,
    0x10, 0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x89, 0x10, 0x04, 0x41,
    0x10, 0x04, 0x41, 0x10, 0x04, 0x01, 0xc0, 0x87, 0x51, 0xd6, 0x44, 0x10,
    0x54, 0x5d, 0x19, 0xc0, 0xc0, 0xfb, 0x68, 0x73, 0x63, 0x2d, 0x6c, 0x74,
    0x63, 0x20, 0x66, 0x73, 0x63, 0x2d, 0x6d, 0x61, 0x78, 0x20, 0x76, 0x72,
    0x2d, 0x64, 0x65, 0x6c, 0x74, 0x61, 0x20, 0x67, 0x6e, 0x64, 0x73, 0x65,
    0x6e, 0x2d, 0x69, 0x6e, 0x61, 0x20, 0x70, 0x31, 0x32, 0x76, 0x73, 0x65,
    0x6e, 0x2d, 0x69, 0x6e, 0x61, 0x20, 0x70, 0x31, 0x32, 0x76, 0x66, 0x61,
    0x6e, 0x2d, 0x6d, 0x70, 0x73, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24,
    0x01, 0x07, 0x19, 0xc6, 0x51, 0x75, 0x61, 0x6e, 0x74, 0x61, 0xcb, 0x43,
    0x49, 0x2d, 0x43, 0x61, 0x74, 0x61, 0x6c, 0x69, 0x6e, 0x61, 0x89, 0x11,
    0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x14, 0x01, 0x82, 0x2d, 0x0c, 0x8b,
    0x11, 0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x83,
    0x71, 0xd9, 0xd6, 0xc0, 0xc1, 0x00, 0x00, 0x37
};
static const size_t pdb_eeprom_len = sizeof(pdb_eeprom);

/*
 * OSFP Carrier Board FRU data. Generated with frugen.
 *
 *    {
 *        "board": {
 *            "mfg": "Quanta",
 *            "pname": "Catalina OSFP MP (QEMU)",
 *            "pn": "00000000000",
 *            "serial": "00000000000000",
 *            "date": "01/12/2025 00:00",
 *            "custom": ["19-100316"]
 *        },
 *        "product": {
 *            "mfg": "Quanta",
 *            "pname": "CI-Catalina",
 *            "pn": "10000000001",
 *            "ver": "MP",
 *            "serial": "10000000000000",
 *            "atag": "QEMU"
 *        }
 *    }
 */
static const uint8_t osfp_eeprom[] = {
    0x01, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0xf4, 0x01, 0x09, 0x19, 0x8c,
    0x19, 0xf0, 0xc6, 0x51, 0x75, 0x61, 0x6e, 0x74, 0x61, 0xd7, 0x43, 0x61,
    0x74, 0x61, 0x6c, 0x69, 0x6e, 0x61, 0x20, 0x4f, 0x53, 0x46, 0x50, 0x20,
    0x4d, 0x50, 0x20, 0x28, 0x51, 0x45, 0x4d, 0x55, 0x29, 0x8b, 0x10, 0x04,
    0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x89, 0x10, 0x04,
    0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x01, 0xc0, 0x87, 0x51, 0xd6, 0x44,
    0x10, 0x34, 0x45, 0x16, 0xc1, 0x00, 0x00, 0x6e, 0x01, 0x07, 0x19, 0xc6,
    0x51, 0x75, 0x61, 0x6e, 0x74, 0x61, 0xcb, 0x43, 0x49, 0x2d, 0x43, 0x61,
    0x74, 0x61, 0x6c, 0x69, 0x6e, 0x61, 0x89, 0x11, 0x04, 0x41, 0x10, 0x04,
    0x41, 0x10, 0x14, 0x01, 0x82, 0x2d, 0x0c, 0x8b, 0x11, 0x04, 0x41, 0x10,
    0x04, 0x41, 0x10, 0x04, 0x41, 0x10, 0x04, 0x83, 0x71, 0xd9, 0xd6, 0xc0,
    0xc1, 0x00, 0x00, 0x37
};
static const size_t osfp_eeprom_len = sizeof(osfp_eeprom);

static void catalina_bmc_i2c_init(AspeedMachineState *bmc)
{
    /* Reference from v6.16-rc2 aspeed-bmc-facebook-catalina.dts */

    AspeedSoCState *soc = bmc->soc;
    I2CBus *i2c[16] = {};
    I2CSlave *i2c_mux;

    /* busses 0-15 are all used. */
    for (int i = 0; i < ARRAY_SIZE(i2c); i++) {
        i2c[i] = aspeed_i2c_get_bus(&soc->i2c, i);
    }

    /* &i2c0 */
    /* i2c-mux@71 (PCA9546) on i2c0 */
    i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x71);

    /* i2c-mux@72 (PCA9546) on i2c0 */
    i2c_mux = i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x72);

    /* i2c0mux1ch1 */
    /* io_expander7 - pca9535@20 */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 1),
                            TYPE_PCA9552, 0x20);
    /* eeprom@50 */
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 1), 0x50, 8 * KiB);

    /* i2c-mux@73 (PCA9546) on i2c0 */
    i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x73);

    /* i2c-mux@75 (PCA9546) on i2c0 */
    i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x75);

    /* i2c-mux@76 (PCA9546) on i2c0 */
    i2c_mux = i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x76);

    /* i2c0mux4ch1 */
    /* io_expander8 - pca9535@21 */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 1),
                            TYPE_PCA9552, 0x21);
    /* eeprom@50 */
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 1), 0x50, 8 * KiB);

    /* i2c-mux@77 (PCA9546) on i2c0 */
    i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x77);


    /* &i2c1 */
    /* i2c-mux@70 (PCA9548) on i2c1 */
    i2c_mux = i2c_slave_create_simple(i2c[1], TYPE_PCA9548, 0x70);
    /* i2c1mux0ch0 */
    /* ina238@41 - no model */
    /* ina238@42 - no model */
    /* ina238@44 - no model */
    /* i2c1mux0ch1 */
    /* ina238@41 - no model */
    /* ina238@43 - no model */
    /* i2c1mux0ch4 */
    /* ltc4287@42 - no model */
    /* ltc4287@43 - no model */

    /* i2c1mux0ch5 */
    /* eeprom@54 */
    at24c_eeprom_init_rom(pca954x_i2c_get_bus(i2c_mux, 5), 0x54, 8 * KiB,
                          pdb_eeprom, pdb_eeprom_len);
    /* tpm75@4f */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 5), TYPE_TMP75, 0x4f);

    /* i2c1mux0ch6 */
    /* io_expander5 - pca9554@27 */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 6),
                            TYPE_PCA9554, 0x27);
    /* io_expander6 - pca9555@25 */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 6),
                            TYPE_PCA9552, 0x25);
    /* eeprom@51 */
    at24c_eeprom_init_rom(pca954x_i2c_get_bus(i2c_mux, 6), 0x51, 8 * KiB,
                          osfp_eeprom, osfp_eeprom_len);

    /* i2c1mux0ch7 */
    /* eeprom@53 */
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 7), 0x53, 8 * KiB);
    /* temperature-sensor@4b - tmp75 */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 7), TYPE_TMP75, 0x4b);

    /* &i2c2 */
    /* io_expander0 - pca9555@20 */
    i2c_slave_create_simple(i2c[2], TYPE_PCA9552, 0x20);
    /* io_expander0 - pca9555@21 */
    i2c_slave_create_simple(i2c[2], TYPE_PCA9552, 0x21);
    /* io_expander0 - pca9555@27 */
    i2c_slave_create_simple(i2c[2], TYPE_PCA9552, 0x27);
    /* eeprom@50 */
    at24c_eeprom_init(i2c[2], 0x50, 8 * KiB);
    /* eeprom@51 */
    at24c_eeprom_init(i2c[2], 0x51, 8 * KiB);

    /* &i2c5 */
    /* i2c-mux@70 (PCA9548) on i2c5 */
    i2c_mux = i2c_slave_create_simple(i2c[5], TYPE_PCA9548, 0x70);
    /* i2c5mux0ch6 */
    /* eeprom@52 */
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 6), 0x52, 8 * KiB);
    /* i2c5mux0ch7 */
    /* ina230@40 - no model */
    /* ina230@41 - no model */
    /* ina230@44 - no model */
    /* ina230@45 - no model */

    /* &i2c6 */
    /* io_expander3 - pca9555@21 */
    i2c_slave_create_simple(i2c[6], TYPE_PCA9552, 0x21);
    /* rtc@6f - nct3018y */
    i2c_slave_create_simple(i2c[6], TYPE_DS1338, 0x6f);

    /* &i2c9 */
    /* io_expander4 - pca9555@4f */
    i2c_slave_create_simple(i2c[9], TYPE_PCA9552, 0x4f);
    /* temperature-sensor@4b - tpm75 */
    i2c_slave_create_simple(i2c[9], TYPE_TMP75, 0x4b);
    /* eeprom@50 */
    at24c_eeprom_init_rom(i2c[9], 0x50, 8 * KiB, scm_eeprom, scm_eeprom_len);
    /* eeprom@56 */
    at24c_eeprom_init_rom(i2c[9], 0x56, 8 * KiB, bsm_eeprom, bsm_eeprom_len);

    /* &i2c10 */
    /* temperature-sensor@1f - tpm421 */
    i2c_slave_create_simple(i2c[10], TYPE_TMP421, 0x1f);
    /* eeprom@50 */
    at24c_eeprom_init(i2c[10], 0x50, 8 * KiB);

    /* &i2c11 */
    /* ssif-bmc@10 - no model */

    /* &i2c12 */
    /* eeprom@50 */
    at24c_eeprom_init(i2c[12], 0x50, 8 * KiB);

    /* &i2c13 */
    /* eeprom@50 */
    at24c_eeprom_init(i2c[13], 0x50, 8 * KiB);
    /* eeprom@54 */
    at24c_eeprom_init(i2c[13], 0x54, 256);
    /* eeprom@55 */
    at24c_eeprom_init(i2c[13], 0x55, 256);
    /* eeprom@57 */
    at24c_eeprom_init(i2c[13], 0x57, 256);

    /* &i2c14 */
    /* io_expander9 - pca9555@10 */
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x10);
    /* io_expander10 - pca9555@11 */
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x11);
    /* io_expander11 - pca9555@12 */
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x12);
    /* io_expander12 - pca9555@13 */
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x13);
    /* io_expander13 - pca9555@14 */
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x14);
    /* io_expander14 - pca9555@15 */
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x15);

    /* &i2c15 */
    /* temperature-sensor@1f - tmp421 */
    i2c_slave_create_simple(i2c[15], TYPE_TMP421, 0x1f);
    /* eeprom@52 */
    at24c_eeprom_init(i2c[15], 0x52, 8 * KiB);
}

static void aspeed_machine_catalina_class_init(ObjectClass *oc,
                                               const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Facebook Catalina BMC (Cortex-A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = CATALINA_BMC_HW_STRAP1;
    amc->hw_strap2 = CATALINA_BMC_HW_STRAP2;
    amc->fmc_model = "w25q01jvq";
    amc->spi_model = NULL;
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC2_ON;
    amc->i2c_init  = catalina_bmc_i2c_init;
    mc->default_ram_size = CATALINA_BMC_RAM_SIZE;
    aspeed_machine_class_init_cpus_defaults(mc);
    aspeed_machine_ast2600_class_emmc_init(oc);
}

static const TypeInfo aspeed_ast2600_catalina_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("catalina-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_catalina_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2600_catalina_types)
