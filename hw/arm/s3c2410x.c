/* hw/s3c2410x.c
 *
 * Samsung S3C2410X emulation
 *
 * Copyright 2009 Daniel Silverstone and Vincent Sanders
 *
 * Copyright 2010, 2013 Stefan Weil
 *
 * This file is under the terms of the GNU General Public License Version 2.
 */

#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h" /* get_system_memory */

#include "s3c2410x.h"

#define logout(fmt, ...) \
    fprintf(stderr, "S3C24xx\t%-24s" fmt, __func__, ##__VA_ARGS__)

#define TODO() logout("%s:%u: missing\n", __FILE__, __LINE__)

/* S3C2410 SoC IDs */
#define CPU_S3C2410X_IDENT_S3C2410X 0x32410000
#define CPU_S3C2410X_IDENT_S3C2410A 0x32410002

/* Integrated peripherals */

/* SRAM */
#define CPU_S3C2410X_SRAM_BASE (CPU_S3C2410X_PERIPHERAL + 0x00000000)
#define CPU_S3C2410X_SRAM_SIZE 4096

/* Memory control */
#define CPU_S3C2410X_MEMC_BASE (CPU_S3C2410X_PERIPHERAL + 0x8000000)

/* USB controller */
#define CPU_S3C2410X_OHCI_BASE (CPU_S3C2410X_PERIPHERAL + 0x9000000)

/* Interrupt controller */
#define CPU_S3C2410X_IRQ_BASE (CPU_S3C2410X_PERIPHERAL + 0xA000000)

/* Clock control */
#define CPU_S3C2410X_CLKCON_BASE (CPU_S3C2410X_PERIPHERAL + 0xC000000)

/* LCD controller */
#define CPU_S3C2410X_LCD_BASE (CPU_S3C2410X_PERIPHERAL + 0xD000000)

/* NAND */
#define CPU_S3C2410X_NAND_BASE (CPU_S3C2410X_PERIPHERAL + 0xE000000)

/* serial port bases */
#define CPU_S3C2410X_SERIAL0_BASE (CPU_S3C2410X_PERIPHERAL + 0x10000000)
#define CPU_S3C2410X_SERIAL1_BASE (CPU_S3C2410X_PERIPHERAL + 0x10004000)
#define CPU_S3C2410X_SERIAL2_BASE (CPU_S3C2410X_PERIPHERAL + 0x10008000)

/* Timer controller */
#define CPU_S3C2410X_TIMERS_BASE        (CPU_S3C2410X_PERIPHERAL + 0x11000000)
#define CPU_S3C24XX_WDG_BASE            (CPU_S3C2410X_PERIPHERAL + 0x13000000)

/* IIC */
#define CPU_S3C2410X_IIC_BASE           (CPU_S3C2410X_PERIPHERAL + 0x14000000)

/* GPIO */
#define CPU_S3C2410X_GPIO_BASE          (CPU_S3C2410X_PERIPHERAL + 0x16000000)

/* Real time clock */
#define CPU_S3C2410X_RTC_BASE           (CPU_S3C2410X_PERIPHERAL + 0x17000000)
#define CPU_S3C24XX_ADC_BASE            (CPU_S3C2410X_PERIPHERAL + 0x18000000)

/*----------------------------------------------------------------------------*/

/* Initialise a Samsung S3C2410X SOC ARM core and internal peripherals. */
S3CState *
s3c2410x_init(int sdram_size)
{
    DeviceState *dev;
    MemoryRegion *sysmem = get_system_memory();
    S3CState *s = g_new0(S3CState, 1);

    /* Prepare the ARM 920T core. */
    s->cpu = cpu_arm_init("arm920t");

    /* S3C2410X SDRAM memory is always at the same physical location. */
    memory_region_init_ram(&s->sdram0, OBJECT(s),
                           "s3c2410x.sdram0", sdram_size);
    memory_region_init_alias(&s->sdram1, NULL, "s3c2410x.sdram1",
                             &s->sdram0, 0, sdram_size);
    memory_region_init_alias(&s->sdram2, NULL, "s3c2410x.sdram2",
                             &s->sdram0, 0, sdram_size);
    memory_region_add_subregion(sysmem, CPU_S3C2410X_DRAM, &s->sdram0);
    memory_region_add_subregion(sysmem,
                                CPU_S3C2410X_DRAM + 0x80000000, &s->sdram1);
    memory_region_add_subregion(sysmem,
                                CPU_S3C2410X_DRAM + 0x90000000, &s->sdram2);

    /* S3C2410X SRAM */
    memory_region_init_ram(&s->sram, OBJECT(s),
                           "s3c2410x.sram", CPU_S3C2410X_SRAM_SIZE);
    memory_region_add_subregion(sysmem, CPU_S3C2410X_SRAM_BASE, &s->sram);

    /* SDRAM memory controller */
    s->memc = s3c24xx_memc_init(CPU_S3C2410X_MEMC_BASE);

    /* Interrupt controller */
    s->irq = s3c24xx_irq_init(s, CPU_S3C2410X_IRQ_BASE);

    /* Clock and power control */
    s->clkcon = s3c24xx_clkcon_init(s, CPU_S3C2410X_CLKCON_BASE, 12000000);

    /* Timer controller */
    s->timers = s3c24xx_timers_init(s, CPU_S3C2410X_TIMERS_BASE, 0, 12000000);

    /* Serial port controllers */
    s->uart[0] = s3c24xx_serial_init(s, serial_hds[0], CPU_S3C2410X_SERIAL0_BASE, 32);
    s->uart[1] = s3c24xx_serial_init(s, serial_hds[1], CPU_S3C2410X_SERIAL1_BASE, 35);
    s->uart[2] = s3c24xx_serial_init(s, serial_hds[2], CPU_S3C2410X_SERIAL2_BASE, 38);

    /* Real time clock */
    s->rtc = s3c24xx_rtc_init(CPU_S3C2410X_RTC_BASE);

    /* GPIO */
    dev = sysbus_create_simple("s3c24xx_gpio", CPU_S3C2410X_GPIO_BASE, NULL);
    s->gpio = s3c24xx_gpio_init(s, CPU_S3C2410X_GPIO_BASE, CPU_S3C2410X_IDENT_S3C2410A);

    /* I2C */
    s->iic = s3c24xx_iic_init(s3c24xx_get_irq(s->irq, 27),
                              CPU_S3C2410X_IIC_BASE);

    /* LCD controller */
    dev = sysbus_create_simple("s3c24xx_lcd", CPU_S3C2410X_LCD_BASE,
                               s3c24xx_get_irq(s->irq, 16));

    /* NAND controller */
    s->nand = s3c24xx_nand_init(CPU_S3C2410X_NAND_BASE);

    /* A two port OHCI controller */
    dev = qdev_create(NULL, "sysbus-ohci");
    qdev_prop_set_uint32(dev, "num-ports", 2);
    //~ qdev_prop_set_taddr(dev, "dma-offset", base);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CPU_S3C2410X_OHCI_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, s3c24xx_get_irq(s->irq, 26));

    dev = sysbus_create_simple("s3c24xx_wdg", CPU_S3C24XX_WDG_BASE, NULL);
    dev = sysbus_create_simple("s3c24xx_adc", CPU_S3C24XX_ADC_BASE, NULL);

    return s;
}
