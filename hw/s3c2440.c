/* hw/s3c2440.c
 *
 * Samsung S3C2440 emulation
 *
 * Copyright 2009 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "hw.h"
#include "sysemu.h"

#include "s3c2440.h"

/* Use the PXA OHCI USB mapping */
#include "pxa.h"

/* S3C2440 SoC ID */
#define CPU_S3C2440_IDENT_S3C2440A 0x32440001

/* Integrated peripherals */

/* SRAM */
#define CPU_S3C2440_SRAM_BASE (CPU_S3C2440_PERIPHERAL + 0x00000000)
#define CPU_S3C2440_SRAM_SIZE 4096

/* Memory control */
#define CPU_S3C2440_MEMC_BASE (CPU_S3C2440_PERIPHERAL + 0x8000000)

/* USB controller */
#define CPU_S3C2440_OHCI_BASE (CPU_S3C2440_PERIPHERAL + 0x9000000)

/* Interrupt controller */
#define CPU_S3C2440_IRQ_BASE (CPU_S3C2440_PERIPHERAL + 0xA000000)

/* Clock control */
#define CPU_S3C2440_CLKCON_BASE (CPU_S3C2440_PERIPHERAL + 0xC000000)

/* LCD controller */
#define CPU_S3C2440_LCD_BASE (CPU_S3C2440_PERIPHERAL + 0xD000000)

/* NAND */
#define CPU_S3C2440_NAND_BASE (CPU_S3C2440_PERIPHERAL + 0xE000000)

/* serial port bases */
#define CPU_S3C2440_SERIAL0_BASE (CPU_S3C2440_PERIPHERAL + 0x10000000)
#define CPU_S3C2440_SERIAL1_BASE (CPU_S3C2440_PERIPHERAL + 0x10004000)
#define CPU_S3C2440_SERIAL2_BASE (CPU_S3C2440_PERIPHERAL + 0x10008000)

/* Timer controller */
#define CPU_S3C2440_TIMERS_BASE (CPU_S3C2440_PERIPHERAL + 0x11000000)

/* IIC */
#define CPU_S3C2440_IIC_BASE (CPU_S3C2440_PERIPHERAL + 0x14000000)

/* GPIO */
#define CPU_S3C2440_GPIO_BASE (CPU_S3C2440_PERIPHERAL + 0x16000000)

/* Real time clock */
#define CPU_S3C2440_RTC_BASE (CPU_S3C2440_PERIPHERAL + 0x17000000)

/* Initialise a Samsung S3C2440 SOC ARM core and internal peripherals. */
S3CState *
s3c2440_init(int sdram_size)
{
    ram_addr_t offset;
    S3CState *s = (S3CState *)qemu_mallocz(sizeof(S3CState));

    /* Prepare the ARM 920T core */
    s->cpu_env = cpu_init("arm920t");

    /* S3C2440X SDRAM memory is always at the same physical location */
    offset = qemu_ram_alloc(NULL, "s3c2440.sdram", sdram_size);
    cpu_register_physical_memory(CPU_S3C2440_DRAM,
                                 ram_size,
                                 offset | IO_MEM_RAM);

    /* S3C2440 SRAM */
    offset = qemu_ram_alloc(NULL, "s3c2440.sram", CPU_S3C2440_SRAM_SIZE);
    cpu_register_physical_memory(CPU_S3C2440_SRAM_BASE,
                                 CPU_S3C2440_SRAM_SIZE,
                                 offset | IO_MEM_RAM);

    /* SDRAM memory controller */
    s->memc = s3c24xx_memc_init(CPU_S3C2440_MEMC_BASE);

    /* Interrupt controller */
    s->irq = s3c24xx_irq_init(s, CPU_S3C2440_IRQ_BASE);

    /* Clock and power control */
    s->clkcon = s3c24xx_clkcon_init(s, CPU_S3C2440_CLKCON_BASE, 12000000);

    /* Timer controller */
    s->timers = s3c24xx_timers_init(s, CPU_S3C2440_TIMERS_BASE, 0, 12000000);

    /* Serial port controllers */
    s->uart[0] = s3c24xx_serial_init(s, serial_hds[0], CPU_S3C2440_SERIAL0_BASE, 32);
    s->uart[1] = s3c24xx_serial_init(s, serial_hds[1], CPU_S3C2440_SERIAL1_BASE, 35);
    s->uart[2] = s3c24xx_serial_init(s, serial_hds[2], CPU_S3C2440_SERIAL2_BASE, 38);

    /* Real time clcok */
    s->rtc = s3c24xx_rtc_init(CPU_S3C2440_RTC_BASE);

    /* And some GPIO */
    s->gpio = s3c24xx_gpio_init(s, CPU_S3C2440_GPIO_BASE, CPU_S3C2440_IDENT_S3C2440A);

    /* I2C */
    s->iic = s3c24xx_iic_init(s3c24xx_get_irq(s->irq, 27),
                              CPU_S3C2440_IIC_BASE);

    /* LCD controller */
    s->lcd = s3c24xx_lcd_init(CPU_S3C2440_LCD_BASE,
                              s3c24xx_get_irq(s->irq, 16));

    /* NAND controller */
    s->nand = s3c24xx_nand_init(CPU_S3C2440_NAND_BASE);

    /* A two port OHCI controller */
    //~ usb_ohci_init_pxa(CPU_S3C2440_OHCI_BASE, 2, -1, s3c24xx_get_irq(s->irq, 26));

    return s;
}
