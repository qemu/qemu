/*
 *  QEMU model for the Milkymist board.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sysbus.h"
#include "hw.h"
#include "net.h"
#include "flash.h"
#include "sysemu.h"
#include "devices.h"
#include "boards.h"
#include "loader.h"
#include "elf.h"
#include "blockdev.h"
#include "milkymist-hw.h"
#include "lm32.h"

#define BIOS_FILENAME    "mmone-bios.bin"
#define BIOS_OFFSET      0x00860000
#define BIOS_SIZE        (512*1024)
#define KERNEL_LOAD_ADDR 0x40000000

typedef struct {
    CPUState *env;
    target_phys_addr_t bootstrap_pc;
    target_phys_addr_t flash_base;
    target_phys_addr_t initrd_base;
    size_t initrd_size;
    target_phys_addr_t cmdline_base;
} ResetInfo;

static void cpu_irq_handler(void *opaque, int irq, int level)
{
    CPUState *env = opaque;

    if (level) {
        cpu_interrupt(env, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
    }
}

static void main_cpu_reset(void *opaque)
{
    ResetInfo *reset_info = opaque;
    CPUState *env = reset_info->env;

    cpu_reset(env);

    /* init defaults */
    env->pc = reset_info->bootstrap_pc;
    env->regs[R_R1] = reset_info->cmdline_base;
    env->regs[R_R2] = reset_info->initrd_base;
    env->regs[R_R3] = reset_info->initrd_base + reset_info->initrd_size;
    env->eba = reset_info->flash_base;
    env->deba = reset_info->flash_base;
}

static void
milkymist_init(ram_addr_t ram_size_not_used,
                          const char *boot_device,
                          const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    int kernel_size;
    DriveInfo *dinfo;
    ram_addr_t phys_sdram;
    ram_addr_t phys_flash;
    qemu_irq irq[32], *cpu_irq;
    int i;
    char *bios_filename;
    ResetInfo *reset_info;

    /* memory map */
    target_phys_addr_t flash_base   = 0x00000000;
    size_t flash_sector_size        = 128 * 1024;
    size_t flash_size               = 32 * 1024 * 1024;
    target_phys_addr_t sdram_base   = 0x40000000;
    size_t sdram_size               = 128 * 1024 * 1024;

    target_phys_addr_t initrd_base  = sdram_base + 0x1002000;
    target_phys_addr_t cmdline_base = sdram_base + 0x1000000;
    size_t initrd_max = sdram_size - 0x1002000;

    reset_info = qemu_mallocz(sizeof(ResetInfo));

    if (cpu_model == NULL) {
        cpu_model = "lm32-full";
    }
    env = cpu_init(cpu_model);
    reset_info->env = env;

    cpu_lm32_set_phys_msb_ignore(env, 1);

    phys_sdram = qemu_ram_alloc(NULL, "milkymist.sdram", sdram_size);
    cpu_register_physical_memory(sdram_base, sdram_size,
            phys_sdram | IO_MEM_RAM);

    phys_flash = qemu_ram_alloc(NULL, "milkymist.flash", flash_size);
    dinfo = drive_get(IF_PFLASH, 0, 0);
    /* Numonyx JS28F256J3F105 */
    pflash_cfi01_register(flash_base, phys_flash,
                          dinfo ? dinfo->bdrv : NULL, flash_sector_size,
                          flash_size / flash_sector_size, 2,
                          0x00, 0x89, 0x00, 0x1d, 1);

    /* create irq lines */
    cpu_irq = qemu_allocate_irqs(cpu_irq_handler, env, 1);
    env->pic_state = lm32_pic_init(*cpu_irq);
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(env->pic_state, i);
    }

    /* load bios rom */
    if (bios_name == NULL) {
        bios_name = BIOS_FILENAME;
    }
    bios_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

    if (bios_filename) {
        load_image_targphys(bios_filename, BIOS_OFFSET, BIOS_SIZE);
    }

    reset_info->bootstrap_pc = BIOS_OFFSET;

    /* if no kernel is given no valid bios rom is a fatal error */
    if (!kernel_filename && !dinfo && !bios_filename) {
        fprintf(stderr, "qemu: could not load Milkymist One bios '%s'\n",
                bios_name);
        exit(1);
    }

    milkymist_uart_create(0x60000000, irq[0], irq[1]);
    milkymist_sysctl_create(0x60001000, irq[2], irq[3], irq[4],
            80000000, 0x10014d31, 0x0000041f, 0x00000001);
    milkymist_hpdmc_create(0x60002000);
    milkymist_vgafb_create(0x60003000, 0x40000000, 0x0fffffff);
    milkymist_memcard_create(0x60004000);
    milkymist_ac97_create(0x60005000, irq[5], irq[6], irq[7], irq[8]);
    milkymist_pfpu_create(0x60006000, irq[9]);
    milkymist_tmu2_create(0x60007000, irq[10]);
    milkymist_minimac_create(0x60008000, irq[11], irq[12]);
    milkymist_softusb_create(0x6000f000, irq[17],
            0x20000000, 0x1000, 0x20020000, 0x2000);

    /* make sure juart isn't the first chardev */
    env->juart_state = lm32_juart_init();

    if (kernel_filename) {
        uint64_t entry;

        /* Boots a kernel elf binary.  */
        kernel_size = load_elf(kernel_filename, NULL, NULL, &entry, NULL, NULL,
                               1, ELF_MACHINE, 0);
        reset_info->bootstrap_pc = entry;

        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename, sdram_base,
                                              sdram_size);
            reset_info->bootstrap_pc = sdram_base;
        }

        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }

    if (kernel_cmdline && strlen(kernel_cmdline)) {
        pstrcpy_targphys("cmdline", cmdline_base, TARGET_PAGE_SIZE,
                kernel_cmdline);
        reset_info->cmdline_base = (uint32_t)cmdline_base;
    }

    if (initrd_filename) {
        size_t initrd_size;
        initrd_size = load_image_targphys(initrd_filename, initrd_base,
                initrd_max);
        reset_info->initrd_base = (uint32_t)initrd_base;
        reset_info->initrd_size = (uint32_t)initrd_size;
    }

    qemu_register_reset(main_cpu_reset, reset_info);
}

static QEMUMachine milkymist_machine = {
    .name = "milkymist",
    .desc = "Milkymist One",
    .init = milkymist_init,
    .is_default = 0
};

static void milkymist_machine_init(void)
{
    qemu_register_machine(&milkymist_machine);
}

machine_init(milkymist_machine_init);
