/*
 *  QEMU models for LatticeMico32 uclinux and evr32 boards.
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
#include "devices.h"
#include "boards.h"
#include "loader.h"
#include "blockdev.h"
#include "elf.h"
#include "lm32_hwsetup.h"
#include "lm32.h"
#include "exec-memory.h"

typedef struct {
    CPUState *env;
    target_phys_addr_t bootstrap_pc;
    target_phys_addr_t flash_base;
    target_phys_addr_t hwsetup_base;
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
    env->pc = (uint32_t)reset_info->bootstrap_pc;
    env->regs[R_R1] = (uint32_t)reset_info->hwsetup_base;
    env->regs[R_R2] = (uint32_t)reset_info->cmdline_base;
    env->regs[R_R3] = (uint32_t)reset_info->initrd_base;
    env->regs[R_R4] = (uint32_t)(reset_info->initrd_base +
        reset_info->initrd_size);
    env->eba = reset_info->flash_base;
    env->deba = reset_info->flash_base;
}

static void lm32_evr_init(ram_addr_t ram_size_not_used,
                          const char *boot_device,
                          const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    DriveInfo *dinfo;
    MemoryRegion *address_space_mem =  get_system_memory();
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_flash = g_new(MemoryRegion, 1);
    qemu_irq *cpu_irq, irq[32];
    ResetInfo *reset_info;
    int i;

    /* memory map */
    target_phys_addr_t flash_base  = 0x04000000;
    size_t flash_sector_size       = 256 * 1024;
    size_t flash_size              = 32 * 1024 * 1024;
    target_phys_addr_t ram_base    = 0x08000000;
    size_t ram_size                = 64 * 1024 * 1024;
    target_phys_addr_t timer0_base = 0x80002000;
    target_phys_addr_t uart0_base  = 0x80006000;
    target_phys_addr_t timer1_base = 0x8000a000;
    int uart0_irq                  = 0;
    int timer0_irq                 = 1;
    int timer1_irq                 = 3;

    reset_info = g_malloc0(sizeof(ResetInfo));

    if (cpu_model == NULL) {
        cpu_model = "lm32-full";
    }
    env = cpu_init(cpu_model);
    reset_info->env = env;

    reset_info->flash_base = flash_base;

    memory_region_init_ram(phys_ram, NULL, "lm32_evr.sdram", ram_size);
    memory_region_add_subregion(address_space_mem, ram_base, phys_ram);

    memory_region_init_rom_device(phys_flash, &pflash_cfi02_ops_be,
                                  NULL, "lm32_evr.flash", flash_size);
    dinfo = drive_get(IF_PFLASH, 0, 0);
    /* Spansion S29NS128P */
    pflash_cfi02_register(flash_base, phys_flash,
                          dinfo ? dinfo->bdrv : NULL, flash_sector_size,
                          flash_size / flash_sector_size, 1, 2,
                          0x01, 0x7e, 0x43, 0x00, 0x555, 0x2aa);

    /* create irq lines */
    cpu_irq = qemu_allocate_irqs(cpu_irq_handler, env, 1);
    env->pic_state = lm32_pic_init(*cpu_irq);
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(env->pic_state, i);
    }

    sysbus_create_simple("lm32-uart", uart0_base, irq[uart0_irq]);
    sysbus_create_simple("lm32-timer", timer0_base, irq[timer0_irq]);
    sysbus_create_simple("lm32-timer", timer1_base, irq[timer1_irq]);

    /* make sure juart isn't the first chardev */
    env->juart_state = lm32_juart_init();

    reset_info->bootstrap_pc = flash_base;

    if (kernel_filename) {
        uint64_t entry;
        int kernel_size;

        kernel_size = load_elf(kernel_filename, NULL, NULL, &entry, NULL, NULL,
                               1, ELF_MACHINE, 0);
        reset_info->bootstrap_pc = entry;

        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename, ram_base,
                                              ram_size);
            reset_info->bootstrap_pc = ram_base;
        }

        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }

    qemu_register_reset(main_cpu_reset, reset_info);
}

static void lm32_uclinux_init(ram_addr_t ram_size_not_used,
                          const char *boot_device,
                          const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    DriveInfo *dinfo;
    MemoryRegion *address_space_mem =  get_system_memory();
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_flash = g_new(MemoryRegion, 1);
    qemu_irq *cpu_irq, irq[32];
    HWSetup *hw;
    ResetInfo *reset_info;
    int i;

    /* memory map */
    target_phys_addr_t flash_base   = 0x04000000;
    size_t flash_sector_size        = 256 * 1024;
    size_t flash_size               = 32 * 1024 * 1024;
    target_phys_addr_t ram_base     = 0x08000000;
    size_t ram_size                 = 64 * 1024 * 1024;
    target_phys_addr_t uart0_base   = 0x80000000;
    target_phys_addr_t timer0_base  = 0x80002000;
    target_phys_addr_t timer1_base  = 0x80010000;
    target_phys_addr_t timer2_base  = 0x80012000;
    int uart0_irq                   = 0;
    int timer0_irq                  = 1;
    int timer1_irq                  = 20;
    int timer2_irq                  = 21;
    target_phys_addr_t hwsetup_base = 0x0bffe000;
    target_phys_addr_t cmdline_base = 0x0bfff000;
    target_phys_addr_t initrd_base  = 0x08400000;
    size_t initrd_max               = 0x01000000;

    reset_info = g_malloc0(sizeof(ResetInfo));

    if (cpu_model == NULL) {
        cpu_model = "lm32-full";
    }
    env = cpu_init(cpu_model);
    reset_info->env = env;

    reset_info->flash_base = flash_base;

    memory_region_init_ram(phys_ram, NULL, "lm32_uclinux.sdram", ram_size);
    memory_region_add_subregion(address_space_mem, ram_base, phys_ram);

    memory_region_init_rom_device(phys_flash, &pflash_cfi01_ops_be,
                                  NULL, "lm32_uclinux.flash", flash_size);
    dinfo = drive_get(IF_PFLASH, 0, 0);
    /* Spansion S29NS128P */
    pflash_cfi02_register(flash_base, phys_flash,
                          dinfo ? dinfo->bdrv : NULL, flash_sector_size,
                          flash_size / flash_sector_size, 1, 2,
                          0x01, 0x7e, 0x43, 0x00, 0x555, 0x2aa);

    /* create irq lines */
    cpu_irq = qemu_allocate_irqs(cpu_irq_handler, env, 1);
    env->pic_state = lm32_pic_init(*cpu_irq);
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(env->pic_state, i);
    }

    sysbus_create_simple("lm32-uart", uart0_base, irq[uart0_irq]);
    sysbus_create_simple("lm32-timer", timer0_base, irq[timer0_irq]);
    sysbus_create_simple("lm32-timer", timer1_base, irq[timer1_irq]);
    sysbus_create_simple("lm32-timer", timer2_base, irq[timer2_irq]);

    /* make sure juart isn't the first chardev */
    env->juart_state = lm32_juart_init();

    reset_info->bootstrap_pc = flash_base;

    if (kernel_filename) {
        uint64_t entry;
        int kernel_size;

        kernel_size = load_elf(kernel_filename, NULL, NULL, &entry, NULL, NULL,
                               1, ELF_MACHINE, 0);
        reset_info->bootstrap_pc = entry;

        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename, ram_base,
                                              ram_size);
            reset_info->bootstrap_pc = ram_base;
        }

        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }

    /* generate a rom with the hardware description */
    hw = hwsetup_init();
    hwsetup_add_cpu(hw, "LM32", 75000000);
    hwsetup_add_flash(hw, "flash", flash_base, flash_size);
    hwsetup_add_ddr_sdram(hw, "ddr_sdram", ram_base, ram_size);
    hwsetup_add_timer(hw, "timer0", timer0_base, timer0_irq);
    hwsetup_add_timer(hw, "timer1_dev_only", timer1_base, timer1_irq);
    hwsetup_add_timer(hw, "timer2_dev_only", timer2_base, timer2_irq);
    hwsetup_add_uart(hw, "uart", uart0_base, uart0_irq);
    hwsetup_add_trailer(hw);
    hwsetup_create_rom(hw, hwsetup_base);
    hwsetup_free(hw);

    reset_info->hwsetup_base = hwsetup_base;

    if (kernel_cmdline && strlen(kernel_cmdline)) {
        pstrcpy_targphys("cmdline", cmdline_base, TARGET_PAGE_SIZE,
                kernel_cmdline);
        reset_info->cmdline_base = cmdline_base;
    }

    if (initrd_filename) {
        size_t initrd_size;
        initrd_size = load_image_targphys(initrd_filename, initrd_base,
                initrd_max);
        reset_info->initrd_base = initrd_base;
        reset_info->initrd_size = initrd_size;
    }

    qemu_register_reset(main_cpu_reset, reset_info);
}

static QEMUMachine lm32_evr_machine = {
    .name = "lm32-evr",
    .desc = "LatticeMico32 EVR32 eval system",
    .init = lm32_evr_init,
    .is_default = 1
};

static QEMUMachine lm32_uclinux_machine = {
    .name = "lm32-uclinux",
    .desc = "lm32 platform for uClinux and u-boot by Theobroma Systems",
    .init = lm32_uclinux_init,
    .is_default = 0
};

static void lm32_machine_init(void)
{
    qemu_register_machine(&lm32_uclinux_machine);
    qemu_register_machine(&lm32_evr_machine);
}

machine_init(lm32_machine_init);
