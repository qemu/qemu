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

static struct keymap map[0xE0] = {
    [0 ... 0xDF] = { -1, -1 },
    [0x1e] = {0,0}, /* a */
    [0x30] = {0,1}, /* b */
    [0x2e] = {0,2}, /* c */
    [0x20] = {0,3}, /* d */
    [0x12] = {0,4}, /* e */
    [0x21] = {0,5}, /* f */
    [0x22] = {1,0}, /* g */
    [0x23] = {1,1}, /* h */
    [0x17] = {1,2}, /* i */
    [0x24] = {1,3}, /* j */
    [0x25] = {1,4}, /* k */
    [0x26] = {1,5}, /* l */
    [0x32] = {2,0}, /* m */
    [0x31] = {2,1}, /* n */
    [0x18] = {2,2}, /* o */
    [0x19] = {2,3}, /* p */
    [0x10] = {2,4}, /* q */
    [0x13] = {2,5}, /* r */
    [0x1f] = {3,0}, /* s */
    [0x14] = {3,1}, /* t */
    [0x16] = {3,2}, /* u */
    [0x2f] = {3,3}, /* v */
    [0x11] = {3,4}, /* w */
    [0x2d] = {3,5}, /* x */
    [0x15] = {4,2}, /* y */
    [0x2c] = {4,3}, /* z */
    [0xc7] = {5,0}, /* Home */
    [0x2a] = {5,1}, /* shift */
    [0x39] = {5,2}, /* space */
    [0x39] = {5,3}, /* space */
    [0x1c] = {5,5}, /*  enter */
    [0xc8] = {6,0}, /* up */
    [0xd0] = {6,1}, /* down */
    [0xcb] = {6,2}, /* left */
    [0xcd] = {6,3}, /* right */
};

enum mainstone_model_e { mainstone };

#define MAINSTONE_RAM	0x04000000
#define MAINSTONE_ROM	0x00800000
#define MAINSTONE_FLASH	0x02000000

static struct arm_boot_info mainstone_binfo = {
    .loader_start = PXA2XX_SDRAM_BASE,
    .ram_size = 0x04000000,
};

static void mainstone_common_init(ram_addr_t ram_size, int vga_ram_size,
                DisplayState *ds, const char *kernel_filename,
                const char *kernel_cmdline, const char *initrd_filename,
                const char *cpu_model, enum mainstone_model_e model, int arm_id)
{
    uint32_t sector_len = 256 * 1024;
    target_phys_addr_t mainstone_flash_base[] = { MST_FLASH_0, MST_FLASH_1 };
    struct pxa2xx_state_s *cpu;
    qemu_irq *mst_irq;
    int i, index;

    if (!cpu_model)
        cpu_model = "pxa270-c5";

    /* Setup CPU & memory */
    if (ram_size < MAINSTONE_RAM + MAINSTONE_ROM + 2 * MAINSTONE_FLASH +
                    PXA2XX_INTERNAL_SIZE) {
        fprintf(stderr, "This platform requires %i bytes of memory\n",
                        MAINSTONE_RAM + MAINSTONE_ROM + 2 * MAINSTONE_FLASH +
                        PXA2XX_INTERNAL_SIZE);
        exit(1);
    }

    cpu = pxa270_init(mainstone_binfo.ram_size, ds, cpu_model);
    cpu_register_physical_memory(0, MAINSTONE_ROM,
                    qemu_ram_alloc(MAINSTONE_ROM) | IO_MEM_ROM);

    /* Setup initial (reset) machine state */
    cpu->env->regs[15] = mainstone_binfo.loader_start;

    /* There are two 32MiB flash devices on the board */
    for (i = 0; i < 2; i ++) {
        index = drive_get_index(IF_PFLASH, 0, i);
        if (index == -1) {
            fprintf(stderr, "Two flash images must be given with the "
                    "'pflash' parameter\n");
            exit(1);
        }

        if (!pflash_cfi01_register(mainstone_flash_base[i],
                                qemu_ram_alloc(MAINSTONE_FLASH),
                                drives_table[index].bdrv, sector_len,
                                MAINSTONE_FLASH / sector_len, 4, 0, 0, 0, 0)) {
            fprintf(stderr, "qemu: Error registering flash memory.\n");
            exit(1);
        }
    }

    mst_irq = mst_irq_init(cpu, MST_FPGA_PHYS, PXA2XX_PIC_GPIO_0);

    /* setup keypad */
    printf("map addr %p\n", &map);
    pxa27x_register_keypad(cpu->kp, map, 0xe0);

    /* MMC/SD host */
    pxa2xx_mmci_handlers(cpu->mmc, NULL, mst_irq[MMC_IRQ]);

    smc91c111_init(&nd_table[0], MST_ETH_PHYS, mst_irq[ETHERNET_IRQ]);

    mainstone_binfo.kernel_filename = kernel_filename;
    mainstone_binfo.kernel_cmdline = kernel_cmdline;
    mainstone_binfo.initrd_filename = initrd_filename;
    mainstone_binfo.board_id = arm_id;
    arm_load_kernel(cpu->env, &mainstone_binfo);
}

static void mainstone_init(ram_addr_t ram_size, int vga_ram_size,
                const char *boot_device, DisplayState *ds,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    mainstone_common_init(ram_size, vga_ram_size, ds, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, mainstone, 0x196);
}

QEMUMachine mainstone2_machine = {
    .name = "mainstone",
    .desc = "Mainstone II (PXA27x)",
    .init = mainstone_init,
    .ram_require = (MAINSTONE_RAM + MAINSTONE_ROM + 2 * MAINSTONE_FLASH +
		    PXA2XX_INTERNAL_SIZE) | RAMSIZE_FIXED,
    .max_cpus = 1,
};
