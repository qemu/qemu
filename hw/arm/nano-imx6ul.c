/*
 * Copyright (c) 2020 Nigel Po <n.pod@nanosonics.com.au>
 *
 * Nanosonics IMX6UL System emulation.
 *
 * This is based on the mcimx6ul-evk.c
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates nanosonics platform with a Freescale
 * i.MX6ul SoC
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/fsl-imx6ul.h"
#include "hw/boards.h"
#include "hw/i2c/i2c.h"
#include "hw/ssi/ssi.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"
#include "hw/display/nano_fb.h"
#include "hw/pwm/nano_pwm.h"
#include "hw/adc/adc_samples_simulator.h"

// ToDo: Put this in a config file
#define RTC_I2C_BUS 0
#define RTC_I2C_ADDRESS 0x68

#define L6470_SPI_BUS   0
#define FRAM_SPI_BUS    1

#define PCF8574_I2C_ADDRESS 0x20
#define PCF8575_I2C_ADDRESS 0x21

#define ADS7953_SPI_BUS     3

#define FSL_IMX6UL_IOMUXC_SNVS  0x02290000
#define FSL_IMX6UL_IOMUXC_SNVS_SIZE 0x00004000

#define NANO_FSL_IMX6UL_NUM_PWM 8
#define NANO_DEV_NAME_SIZE 20

typedef struct {
    FslIMX6ULState soc;
    NANOPWMState pwm[NANO_FSL_IMX6UL_NUM_PWM];
    NANOFbState  nano_lcd;
    ADCSAMPLESIMState adc_sample_sim;
    MemoryRegion ram;
    MemoryRegion iomuxc_snvs;
} NANOIMX6UL;

static void nano_imx6ul_init(MachineState *machine)
{
    static struct arm_boot_info boot_info;
    NANOIMX6UL *s = g_new0(NANOIMX6UL, 1);
    int i;
    char name[NANO_DEV_NAME_SIZE];

    // ADC sample simulator device, initialise it first so that other devices can use it
    object_initialize_child(OBJECT(machine), NAME_ADCSAMPLESIM, &s->adc_sample_sim, TYPE_ADCSAMPLESIM);
    qdev_realize(DEVICE(&s->adc_sample_sim), NULL, &error_abort);

    if (machine->ram_size > FSL_IMX6UL_MMDC_SIZE) {
        error_report("RAM size " RAM_ADDR_FMT " above max supported (%08x)",
                     machine->ram_size, FSL_IMX6UL_MMDC_SIZE);
        exit(1);
    }

    boot_info = (struct arm_boot_info) {
        .loader_start = FSL_IMX6UL_MMDC_ADDR,
        .board_id = -1,
        .ram_size = machine->ram_size,
        .nb_cpus = machine->smp.cpus,
    };

    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_FSL_IMX6UL);

    object_property_set_uint(OBJECT(&s->soc), "fec1-phy-num", 2, &error_fatal);
    object_property_set_uint(OBJECT(&s->soc), "fec2-phy-num", 1, &error_fatal);
    object_property_set_bool(OBJECT(&s->soc), "realized", true, &error_fatal);

    memory_region_init_ram(&s->ram, NULL, "nano-imx6ul.ram",
                           machine->ram_size, &error_fatal);

    memory_region_add_subregion(get_system_memory(),
                                FSL_IMX6UL_MMDC_ADDR, &s->ram);

    i2c_slave_create_simple(s->soc.i2c[RTC_I2C_BUS].bus, "ds3231", RTC_I2C_ADDRESS);
    i2c_slave_create_simple(s->soc.i2c[RTC_I2C_BUS].bus, "pcf8574", PCF8574_I2C_ADDRESS);
    i2c_slave_create_simple(s->soc.i2c[RTC_I2C_BUS].bus, "pcf8575", PCF8575_I2C_ADDRESS);

    ssi_create_slave(s->soc.spi[L6470_SPI_BUS].bus, "l6470");
    ssi_create_slave(s->soc.spi[FRAM_SPI_BUS].bus, "mb85rs");
    ssi_create_slave(s->soc.spi[ADS7953_SPI_BUS].bus, "ads7953");

    //IOMUXC_SNVS memory
    memory_region_init_rom(&s->iomuxc_snvs, NULL, "imx6ul.iomux_snvs", FSL_IMX6UL_IOMUXC_SNVS_SIZE, &error_abort);
    memory_region_add_subregion(get_system_memory(), FSL_IMX6UL_IOMUXC_SNVS, &s->iomuxc_snvs);

    //LCD
    object_initialize_child(OBJECT(s), NANO_LCD_DEV_NAME, &s->nano_lcd, TYPE_NANOFB);
    /* Initialize the lcd */
    sysbus_realize(SYS_BUS_DEVICE(&s->nano_lcd), &error_abort);

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->nano_lcd), 0, FSL_IMX6UL_LCDIF_ADDR);

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->nano_lcd), 0,
                        qdev_get_gpio_in(DEVICE(&s->soc.a7mpcore),
                                        FSL_IMX6UL_LCDIF_IRQ));

    //PWM
    for(i = 0; i < NANO_FSL_IMX6UL_NUM_PWM; ++i)
    {
        static const hwaddr NANO_FSL_IMX6UL_PWMn_ADDR[NANO_FSL_IMX6UL_NUM_PWM] = {
            FSL_IMX6UL_PWM1_ADDR,
            FSL_IMX6UL_PWM2_ADDR,
            FSL_IMX6UL_PWM3_ADDR,
            FSL_IMX6UL_PWM4_ADDR,
            FSL_IMX6UL_PWM5_ADDR,
            FSL_IMX6UL_PWM6_ADDR,
            0,                      //p5 board doesn't use pwm 7
            FSL_IMX6UL_PWM8_ADDR,
        };
        static const int NANO_FSL_IMX6UL_PWMn_IRQ[NANO_FSL_IMX6UL_NUM_PWM] = {
            FSL_IMX6UL_PWM1_IRQ,
            FSL_IMX6UL_PWM2_IRQ,
            FSL_IMX6UL_PWM3_IRQ,
            FSL_IMX6UL_PWM4_IRQ,
            FSL_IMX6UL_PWM5_IRQ,
            FSL_IMX6UL_PWM6_IRQ,
            0,                      //p5 board doesn't use pwm 7
            FSL_IMX6UL_PWM8_IRQ,
        };
        if(NANO_FSL_IMX6UL_PWMn_ADDR[i] == 0) {
            continue;
        }
        snprintf(name, NANO_DEV_NAME_SIZE, "pwm%d", i + 1);
        object_initialize_child(OBJECT(s), name, &s->pwm[i], TYPE_NANOPWM);
        (&(s->pwm[i]))->pwm_index = (i+1);
        /* Initialize the pwm */
        sysbus_realize(SYS_BUS_DEVICE(&s->pwm[i]), &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->pwm[i]), 0,
                        NANO_FSL_IMX6UL_PWMn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->pwm[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->soc.a7mpcore),
                                            NANO_FSL_IMX6UL_PWMn_IRQ[i]));
    }

    if (!qtest_enabled()) {
        arm_load_kernel(&s->soc.cpu, machine, &boot_info);
    }
}

static void nano_imx6ul_machine_init(MachineClass *mc)
{
    mc->desc = "Nanosonics Platform Freescale i.MX6UL (Cortex A7)";
    mc->init = nano_imx6ul_init;
    mc->max_cpus = FSL_IMX6UL_NUM_CPUS;
}
DEFINE_MACHINE("nano-imx6ul", nano_imx6ul_machine_init)
