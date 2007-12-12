/*
 * PXA270-based Intel Mainstone platforms.
 *
 * Copyright (c) 2007 by Armin Kuster <akuster@kama-aina.net> or
 *                                    <akuster@mvista.com>
 *
 * Code based on spitz platform by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 */
#include "hw.h"
#include "pxa.h"
#include "arm-misc.h"
#include "net.h"
#include "devices.h"
#include "boards.h"
#include "mainstone.h"
#include "sysemu.h"
#include "flash.h"

enum mainstone_model_e { mainstone };

static void mainstone_common_init(int ram_size, int vga_ram_size,
                DisplayState *ds, const char *kernel_filename,
                const char *kernel_cmdline, const char *initrd_filename,
                const char *cpu_model, enum mainstone_model_e model, int arm_id)
{
    uint32_t mainstone_ram = 0x04000000;
    uint32_t mainstone_rom = 0x00800000;
    struct pxa2xx_state_s *cpu;
    qemu_irq *mst_irq;
    int index;

    if (!cpu_model)
        cpu_model = "pxa270-c5";

    /* Setup CPU & memory */
    if (ram_size < mainstone_ram + mainstone_rom + PXA2XX_INTERNAL_SIZE) {
        fprintf(stderr, "This platform requires %i bytes of memory\n",
                        mainstone_ram + mainstone_rom + PXA2XX_INTERNAL_SIZE);
        exit(1);
    }

    cpu = pxa270_init(mainstone_ram, ds, cpu_model);
    cpu_register_physical_memory(0, mainstone_rom,
                    qemu_ram_alloc(mainstone_rom) | IO_MEM_ROM);

    /* Setup initial (reset) machine state */
    cpu->env->regs[15] = PXA2XX_SDRAM_BASE;

    /* There are two 32MiB flash devices on the board */
    index = drive_get_index(IF_PFLASH, 0, 0);
    if (index == -1) {
        fprintf(stderr, "Two flash images must be given with the "
                "'pflash' parameter\n");
        exit(1);
    }
    if (!pflash_cfi01_register(MST_FLASH_0,
                         mainstone_ram + PXA2XX_INTERNAL_SIZE,
                         drives_table[index].bdrv,
                         256 * 1024, 128, 4, 0, 0, 0, 0)) {
        fprintf(stderr, "qemu: Error registering flash memory.\n");
        exit(1);
    }

    index = drive_get_index(IF_PFLASH, 0, 1);
    if (index == -1) {
        fprintf(stderr, "Two flash images must be given with the "
                "'pflash' parameter\n");
        exit(1);
    }
    if (!pflash_cfi01_register(MST_FLASH_1,
                         mainstone_ram + PXA2XX_INTERNAL_SIZE,
                         drives_table[index].bdrv,
                         256 * 1024, 128, 4, 0, 0, 0, 0)) {
        fprintf(stderr, "qemu: Error registering flash memory.\n");
        exit(1);
    }

    mst_irq = mst_irq_init(cpu, MST_FPGA_PHYS, PXA2XX_PIC_GPIO_0);

    /* MMC/SD host */
    pxa2xx_mmci_handlers(cpu->mmc, NULL, mst_irq[MMC_IRQ]);

    smc91c111_init(&nd_table[0], MST_ETH_PHYS, mst_irq[ETHERNET_IRQ]);

    arm_load_kernel(cpu->env, mainstone_ram, kernel_filename, kernel_cmdline,
                    initrd_filename, arm_id, PXA2XX_SDRAM_BASE);
}

static void mainstone_init(int ram_size, int vga_ram_size,
                const char *boot_device, DisplayState *ds,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    mainstone_common_init(ram_size, vga_ram_size, ds, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, mainstone, 0x196);
}

QEMUMachine mainstone2_machine = {
    "mainstone",
    "Mainstone II (PXA27x)",
    mainstone_init,
};
