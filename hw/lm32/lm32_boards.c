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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/block/flash.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "lm32_hwsetup.h"
#include "lm32.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"

typedef struct {
    LM32CPU *cpu;
    hwaddr bootstrap_pc;
    hwaddr flash_base;
    hwaddr hwsetup_base;
    hwaddr initrd_base;
    size_t initrd_size;
    hwaddr cmdline_base;
} ResetInfo;

static void cpu_irq_handler(void *opaque, int irq, int level)
{
    LM32CPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    if (level) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

static void main_cpu_reset(void *opaque)
{
    ResetInfo *reset_info = opaque;
    CPULM32State *env = &reset_info->cpu->env;

    cpu_reset(CPU(reset_info->cpu));

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

static void lm32_evr_init(MachineState *machine)
{
    const char *kernel_filename = machine->kernel_filename;
    LM32CPU *cpu;
    CPULM32State *env;
    DriveInfo *dinfo;
    MemoryRegion *address_space_mem =  get_system_memory();
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    qemu_irq irq[32];
    ResetInfo *reset_info;
    int i;

    /* memory map */
    hwaddr flash_base  = 0x04000000;
    size_t flash_sector_size       = 256 * KiB;
    size_t flash_size              = 32 * MiB;
    hwaddr ram_base    = 0x08000000;
    size_t ram_size                = 64 * MiB;
    hwaddr timer0_base = 0x80002000;
    hwaddr uart0_base  = 0x80006000;
    hwaddr timer1_base = 0x8000a000;
    int uart0_irq                  = 0;
    int timer0_irq                 = 1;
    int timer1_irq                 = 3;

    reset_info = g_malloc0(sizeof(ResetInfo));

    cpu = LM32_CPU(cpu_create(machine->cpu_type));

    env = &cpu->env;
    reset_info->cpu = cpu;

    reset_info->flash_base = flash_base;

    memory_region_allocate_system_memory(phys_ram, NULL, "lm32_evr.sdram",
                                         ram_size);
    memory_region_add_subregion(address_space_mem, ram_base, phys_ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    /* Spansion S29NS128P */
    pflash_cfi02_register(flash_base, "lm32_evr.flash", flash_size,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          flash_sector_size,
                          1, 2, 0x01, 0x7e, 0x43, 0x00, 0x555, 0x2aa, 1);

    /* create irq lines */
    env->pic_state = lm32_pic_init(qemu_allocate_irq(cpu_irq_handler, cpu, 0));
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(env->pic_state, i);
    }

    lm32_uart_create(uart0_base, irq[uart0_irq], serial_hd(0));
    sysbus_create_simple("lm32-timer", timer0_base, irq[timer0_irq]);
    sysbus_create_simple("lm32-timer", timer1_base, irq[timer1_irq]);

    /* make sure juart isn't the first chardev */
    env->juart_state = lm32_juart_init(serial_hd(1));

    reset_info->bootstrap_pc = flash_base;

    if (kernel_filename) {
        uint64_t entry;
        int kernel_size;

        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &entry, NULL, NULL,
                               1, EM_LATTICEMICO32, 0, 0);
        reset_info->bootstrap_pc = entry;

        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename, ram_base,
                                              ram_size);
            reset_info->bootstrap_pc = ram_base;
        }

        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
    }

    qemu_register_reset(main_cpu_reset, reset_info);
}

static void lm32_uclinux_init(MachineState *machine)
{
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    LM32CPU *cpu;
    CPULM32State *env;
    DriveInfo *dinfo;
    MemoryRegion *address_space_mem =  get_system_memory();
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    qemu_irq irq[32];
    HWSetup *hw;
    ResetInfo *reset_info;
    int i;

    /* memory map */
    hwaddr flash_base   = 0x04000000;
    size_t flash_sector_size        = 256 * KiB;
    size_t flash_size               = 32 * MiB;
    hwaddr ram_base     = 0x08000000;
    size_t ram_size                 = 64 * MiB;
    hwaddr uart0_base   = 0x80000000;
    hwaddr timer0_base  = 0x80002000;
    hwaddr timer1_base  = 0x80010000;
    hwaddr timer2_base  = 0x80012000;
    int uart0_irq                   = 0;
    int timer0_irq                  = 1;
    int timer1_irq                  = 20;
    int timer2_irq                  = 21;
    hwaddr hwsetup_base = 0x0bffe000;
    hwaddr cmdline_base = 0x0bfff000;
    hwaddr initrd_base  = 0x08400000;
    size_t initrd_max               = 0x01000000;

    reset_info = g_malloc0(sizeof(ResetInfo));

    cpu = LM32_CPU(cpu_create(machine->cpu_type));

    env = &cpu->env;
    reset_info->cpu = cpu;

    reset_info->flash_base = flash_base;

    memory_region_allocate_system_memory(phys_ram, NULL,
                                         "lm32_uclinux.sdram", ram_size);
    memory_region_add_subregion(address_space_mem, ram_base, phys_ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    /* Spansion S29NS128P */
    pflash_cfi02_register(flash_base, "lm32_uclinux.flash", flash_size,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          flash_sector_size,
                          1, 2, 0x01, 0x7e, 0x43, 0x00, 0x555, 0x2aa, 1);

    /* create irq lines */
    env->pic_state = lm32_pic_init(qemu_allocate_irq(cpu_irq_handler, env, 0));
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(env->pic_state, i);
    }

    lm32_uart_create(uart0_base, irq[uart0_irq], serial_hd(0));
    sysbus_create_simple("lm32-timer", timer0_base, irq[timer0_irq]);
    sysbus_create_simple("lm32-timer", timer1_base, irq[timer1_irq]);
    sysbus_create_simple("lm32-timer", timer2_base, irq[timer2_irq]);

    /* make sure juart isn't the first chardev */
    env->juart_state = lm32_juart_init(serial_hd(1));

    reset_info->bootstrap_pc = flash_base;

    if (kernel_filename) {
        uint64_t entry;
        int kernel_size;

        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &entry, NULL, NULL,
                               1, EM_LATTICEMICO32, 0, 0);
        reset_info->bootstrap_pc = entry;

        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename, ram_base,
                                              ram_size);
            reset_info->bootstrap_pc = ram_base;
        }

        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
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

static void lm32_evr_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "LatticeMico32 EVR32 eval system";
    mc->init = lm32_evr_init;
    mc->is_default = 1;
    mc->default_cpu_type = LM32_CPU_TYPE_NAME("lm32-full");
}

static const TypeInfo lm32_evr_type = {
    .name = MACHINE_TYPE_NAME("lm32-evr"),
    .parent = TYPE_MACHINE,
    .class_init = lm32_evr_class_init,
};

static void lm32_uclinux_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "lm32 platform for uClinux and u-boot by Theobroma Systems";
    mc->init = lm32_uclinux_init;
    mc->is_default = 0;
    mc->default_cpu_type = LM32_CPU_TYPE_NAME("lm32-full");
}

static const TypeInfo lm32_uclinux_type = {
    .name = MACHINE_TYPE_NAME("lm32-uclinux"),
    .parent = TYPE_MACHINE,
    .class_init = lm32_uclinux_class_init,
};

static void lm32_machine_init(void)
{
    type_register_static(&lm32_evr_type);
    type_register_static(&lm32_uclinux_type);
}

type_init(lm32_machine_init)
