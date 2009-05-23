/* hw/s3c2410x.c
 *
 * Samsung S3C2410X emulation
 *
 * Copyright 2009 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "hw.h"

#include "s3c2410x.h"

/* Integrated peripherals */

/* SRAM */
#define CPU_S3C2410X_SRAM_BASE (CPU_S3C2410X_PERIPHERAL + 0x00000000)
#define CPU_S3C2410X_SRAM_SIZE 4096

/* Memory control */
#define CPU_S3C2410X_MEMC_BASE (CPU_S3C2410X_PERIPHERAL + 0x8000000)

/* Interrupt controller */
#define CPU_S3C2410X_IRQ_BASE (CPU_S3C2410X_PERIPHERAL + 0xA000000)

/* Clock control */
#define CPU_S3C2410X_CLKCON_BASE (CPU_S3C2410X_PERIPHERAL + 0xC000000)

/* Timer controller */
#define CPU_S3C2410X_TIMERS_BASE (CPU_S3C2410X_PERIPHERAL + 0x11000000)

/* Initialise a Samsung S3C2410X SOC ARM core and internal peripherals. */
S3CState *
s3c2410x_init(int sdram_size)
{
    ram_addr_t offset;
    S3CState *s = (S3CState *)qemu_mallocz(sizeof(S3CState));

    /* Prepare the ARM 920T core */
    s->cpu_env = cpu_init("arm920t");

    /* S3C2410X SDRAM memory is always at the same physical location */
    offset = qemu_ram_alloc(NULL, "s3c2410x.sdram", sdram_size);
    cpu_register_physical_memory(CPU_S3C2410X_DRAM,
                                 ram_size,
                                 offset | IO_MEM_RAM);

    /* S3C2410X SRAM */
    offset = qemu_ram_alloc(NULL, "s3c2410x.sdram", CPU_S3C2410X_SRAM_SIZE);
    cpu_register_physical_memory(CPU_S3C2410X_SRAM_BASE,
                                 CPU_S3C2410X_SRAM_SIZE,
                                 offset | IO_MEM_RAM);

    /* SDRAM memory controller */
    s->memc = s3c24xx_memc_init(CPU_S3C2410X_MEMC_BASE);

    /* Interrupt controller */
    s->irq = s3c24xx_irq_init(s, CPU_S3C2410X_IRQ_BASE);

    /* Clock and power control */
    s->clkcon = s3c24xx_clkcon_init(s, CPU_S3C2410X_CLKCON_BASE, 12000000);

    /* Timer controller */
    s->timers = s3c24xx_timers_init(s, CPU_S3C2410X_TIMERS_BASE, 0, 12000000);

    return s;
}
