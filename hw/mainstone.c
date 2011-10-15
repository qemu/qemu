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
#include "flash.h"
#include "blockdev.h"
#include "sysbus.h"
#include "exec-memory.h"

/* Device addresses */
#define MST_FPGA_PHYS	0x08000000
#define MST_ETH_PHYS	0x10000300
#define MST_FLASH_0		0x00000000
#define MST_FLASH_1		0x04000000

/* IRQ definitions */
#define MMC_IRQ       0
#define USIM_IRQ      1
#define USBC_IRQ      2
#define ETHERNET_IRQ  3
#define AC97_IRQ      4
#define PEN_IRQ       5
#define MSINS_IRQ     6
#define EXBRD_IRQ     7
#define S0_CD_IRQ     9
#define S0_STSCHG_IRQ 10
#define S0_IRQ        11
#define S1_CD_IRQ     13
#define S1_STSCHG_IRQ 14
#define S1_IRQ        15

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

static void mainstone_common_init(MemoryRegion *address_space_mem,
                ram_addr_t ram_size,
                const char *kernel_filename,
                const char *kernel_cmdline, const char *initrd_filename,
                const char *cpu_model, enum mainstone_model_e model, int arm_id)
{
    uint32_t sector_len = 256 * 1024;
    target_phys_addr_t mainstone_flash_base[] = { MST_FLASH_0, MST_FLASH_1 };
    PXA2xxState *cpu;
    DeviceState *mst_irq;
    DriveInfo *dinfo;
    int i;
    int be;
    MemoryRegion *rom = g_new(MemoryRegion, 1);

    if (!cpu_model)
        cpu_model = "pxa270-c5";

    /* Setup CPU & memory */
    cpu = pxa270_init(address_space_mem, mainstone_binfo.ram_size, cpu_model);
    memory_region_init_ram(rom, NULL, "mainstone.rom", MAINSTONE_ROM);
    memory_region_set_readonly(rom, true);
    memory_region_add_subregion(address_space_mem, 0, rom);

#ifdef TARGET_WORDS_BIGENDIAN
    be = 1;
#else
    be = 0;
#endif
    /* There are two 32MiB flash devices on the board */
    for (i = 0; i < 2; i ++) {
        dinfo = drive_get(IF_PFLASH, 0, i);
        if (!dinfo) {
            fprintf(stderr, "Two flash images must be given with the "
                    "'pflash' parameter\n");
            exit(1);
        }

        if (!pflash_cfi01_register(mainstone_flash_base[i], NULL,
                                   i ? "mainstone.flash1" : "mainstone.flash0",
                                   MAINSTONE_FLASH,
                                   dinfo->bdrv, sector_len,
                                   MAINSTONE_FLASH / sector_len, 4, 0, 0, 0, 0,
                                   be)) {
            fprintf(stderr, "qemu: Error registering flash memory.\n");
            exit(1);
        }
    }

    mst_irq = sysbus_create_simple("mainstone-fpga", MST_FPGA_PHYS,
                    qdev_get_gpio_in(cpu->gpio, 0));

    /* setup keypad */
    printf("map addr %p\n", &map);
    pxa27x_register_keypad(cpu->kp, map, 0xe0);

    /* MMC/SD host */
    pxa2xx_mmci_handlers(cpu->mmc, NULL, qdev_get_gpio_in(mst_irq, MMC_IRQ));

    pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[0],
            qdev_get_gpio_in(mst_irq, S0_IRQ),
            qdev_get_gpio_in(mst_irq, S0_CD_IRQ));
    pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[1],
            qdev_get_gpio_in(mst_irq, S1_IRQ),
            qdev_get_gpio_in(mst_irq, S1_CD_IRQ));

    smc91c111_init(&nd_table[0], MST_ETH_PHYS,
                    qdev_get_gpio_in(mst_irq, ETHERNET_IRQ));

    mainstone_binfo.kernel_filename = kernel_filename;
    mainstone_binfo.kernel_cmdline = kernel_cmdline;
    mainstone_binfo.initrd_filename = initrd_filename;
    mainstone_binfo.board_id = arm_id;
    arm_load_kernel(cpu->env, &mainstone_binfo);
}

static void mainstone_init(ram_addr_t ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    mainstone_common_init(get_system_memory(), ram_size, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, mainstone, 0x196);
}

static QEMUMachine mainstone2_machine = {
    .name = "mainstone",
    .desc = "Mainstone II (PXA27x)",
    .init = mainstone_init,
};

static void mainstone_machine_init(void)
{
    qemu_register_machine(&mainstone2_machine);
}

machine_init(mainstone_machine_init);
