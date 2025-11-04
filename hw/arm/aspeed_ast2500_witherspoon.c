/*
 * OpenPOWER Witherspoon
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
#include "hw/misc/led.h"
#include "hw/sensor/tmp105.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/gpio/pca9552.h"

/* Witherspoon hardware value: 0xF10AD216 */
#define WITHERSPOON_BMC_HW_STRAP1 (                                     \
        AST2500_HW_STRAP1_DEFAULTS |                                    \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_AST2500_HW_STRAP_ACPI_ENABLE |                              \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_MASTER))

static void witherspoon_bmc_i2c_init(AspeedMachineState *bmc)
{
    static const struct {
        unsigned gpio_id;
        LEDColor color;
        const char *description;
        bool gpio_polarity;
    } pca1_leds[] = {
        {13, LED_COLOR_GREEN, "front-fault-4",  GPIO_POLARITY_ACTIVE_LOW},
        {14, LED_COLOR_GREEN, "front-power-3",  GPIO_POLARITY_ACTIVE_LOW},
        {15, LED_COLOR_GREEN, "front-id-5",     GPIO_POLARITY_ACTIVE_LOW},
    };
    AspeedSoCState *soc = bmc->soc;
    uint8_t *eeprom_buf = g_malloc0(8 * 1024);
    DeviceState *dev;
    LEDState *led;

    /* Bus 3: TODO bmp280@77 */
    dev = DEVICE(i2c_slave_new(TYPE_PCA9552, 0x60));
    qdev_prop_set_string(dev, "description", "pca1");
    i2c_slave_realize_and_unref(I2C_SLAVE(dev),
                                aspeed_i2c_get_bus(&soc->i2c, 3),
                                &error_fatal);

    for (size_t i = 0; i < ARRAY_SIZE(pca1_leds); i++) {
        led = led_create_simple(OBJECT(bmc),
                                pca1_leds[i].gpio_polarity,
                                pca1_leds[i].color,
                                pca1_leds[i].description);
        qdev_connect_gpio_out(dev, pca1_leds[i].gpio_id,
                              qdev_get_gpio_in(DEVICE(led), 0));
    }
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 3), "dps310", 0x76);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 3), "max31785", 0x52);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 4), "tmp423", 0x4c);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 5), "tmp423", 0x4c);

    /* The Witherspoon expects a TMP275 but a TMP105 is compatible */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 9), TYPE_TMP105,
                     0x4a);

    /*
     * The witherspoon board expects Epson RX8900 I2C RTC but a ds1338 is
     * good enough
     */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 11), "ds1338", 0x32);

    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 11), 0x51,
                          eeprom_buf);
    dev = DEVICE(i2c_slave_new(TYPE_PCA9552, 0x60));
    qdev_prop_set_string(dev, "description", "pca0");
    i2c_slave_realize_and_unref(I2C_SLAVE(dev),
                                aspeed_i2c_get_bus(&soc->i2c, 11),
                                &error_fatal);
    /* Bus 11: TODO ucd90160@64 */
}

static void aspeed_machine_witherspoon_class_init(ObjectClass *oc,
                                                  const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "OpenPOWER Witherspoon BMC (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = WITHERSPOON_BMC_HW_STRAP1;
    amc->fmc_model = "mx25l25635f";
    amc->spi_model = "mx66l1g45g";
    amc->num_cs    = 2;
    amc->i2c_init  = witherspoon_bmc_i2c_init;
    mc->default_ram_size = 512 * MiB;
    aspeed_machine_class_init_cpus_defaults(mc);
};

static const TypeInfo aspeed_ast2500_witherspoon_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("witherspoon-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_witherspoon_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2500_witherspoon_types)
