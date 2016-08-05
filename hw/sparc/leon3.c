/*
 * QEMU Leon3 System Emulator
 *
 * Copyright (c) 2010-2011 AdaCore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "sysemu/char.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "trace.h"
#include "exec/address-spaces.h"

#include "hw/sparc/grlib.h"

/* Default system clock.  */
#define CPU_CLK (40 * 1000 * 1000)

#define PROM_FILENAME        "u-boot.bin"

#define MAX_PILS 16

typedef struct ResetData {
    SPARCCPU *cpu;
    uint32_t  entry;            /* save kernel entry in case of reset */
    target_ulong sp;            /* initial stack pointer */
} ResetData;

static void main_cpu_reset(void *opaque)
{
    ResetData *s   = (ResetData *)opaque;
    CPUState *cpu = CPU(s->cpu);
    CPUSPARCState  *env = &s->cpu->env;

    cpu_reset(cpu);

    cpu->halted = 0;
    env->pc     = s->entry;
    env->npc    = s->entry + 4;
    env->regbase[6] = s->sp;
}

void leon3_irq_ack(void *irq_manager, int intno)
{
    grlib_irqmp_ack((DeviceState *)irq_manager, intno);
}

static void leon3_set_pil_in(void *opaque, uint32_t pil_in)
{
    CPUSPARCState *env = (CPUSPARCState *)opaque;
    CPUState *cs;

    assert(env != NULL);

    env->pil_in = pil_in;

    if (env->pil_in && (env->interrupt_index == 0 ||
                        (env->interrupt_index & ~15) == TT_EXTINT)) {
        unsigned int i;

        for (i = 15; i > 0; i--) {
            if (env->pil_in & (1 << i)) {
                int old_interrupt = env->interrupt_index;

                env->interrupt_index = TT_EXTINT | i;
                if (old_interrupt != env->interrupt_index) {
                    cs = CPU(sparc_env_get_cpu(env));
                    trace_leon3_set_irq(i);
                    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (!env->pil_in && (env->interrupt_index & ~15) == TT_EXTINT) {
        cs = CPU(sparc_env_get_cpu(env));
        trace_leon3_reset_irq(env->interrupt_index & 15);
        env->interrupt_index = 0;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

static void leon3_generic_hw_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    SPARCCPU *cpu;
    CPUSPARCState   *env;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *prom = g_new(MemoryRegion, 1);
    int         ret;
    char       *filename;
    qemu_irq   *cpu_irqs = NULL;
    int         bios_size;
    int         prom_size;
    ResetData  *reset_info;

    /* Init CPU */
    if (!cpu_model) {
        cpu_model = "LEON3";
    }

    cpu = cpu_sparc_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "qemu: Unable to find Sparc CPU definition\n");
        exit(1);
    }
    env = &cpu->env;

    cpu_sparc_set_id(env, 0);

    /* Reset data */
    reset_info        = g_malloc0(sizeof(ResetData));
    reset_info->cpu   = cpu;
    reset_info->sp    = 0x40000000 + ram_size;
    qemu_register_reset(main_cpu_reset, reset_info);

    /* Allocate IRQ manager */
    grlib_irqmp_create(0x80000200, env, &cpu_irqs, MAX_PILS, &leon3_set_pil_in);

    env->qemu_irq_ack = leon3_irq_manager;

    /* Allocate RAM */
    if ((uint64_t)ram_size > (1UL << 30)) {
        fprintf(stderr,
                "qemu: Too much memory for this machine: %d, maximum 1G\n",
                (unsigned int)(ram_size / (1024 * 1024)));
        exit(1);
    }

    memory_region_allocate_system_memory(ram, NULL, "leon3.ram", ram_size);
    memory_region_add_subregion(address_space_mem, 0x40000000, ram);

    /* Allocate BIOS */
    prom_size = 8 * 1024 * 1024; /* 8Mb */
    memory_region_init_ram(prom, NULL, "Leon3.bios", prom_size, &error_fatal);
    vmstate_register_ram_global(prom);
    memory_region_set_readonly(prom, true);
    memory_region_add_subregion(address_space_mem, 0x00000000, prom);

    /* Load boot prom */
    if (bios_name == NULL) {
        bios_name = PROM_FILENAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

    if (filename) {
        bios_size = get_image_size(filename);
    } else {
        bios_size = -1;
    }

    if (bios_size > prom_size) {
        fprintf(stderr, "qemu: could not load prom '%s': file too big\n",
                filename);
        exit(1);
    }

    if (bios_size > 0) {
        ret = load_image_targphys(filename, 0x00000000, bios_size);
        if (ret < 0 || ret > prom_size) {
            fprintf(stderr, "qemu: could not load prom '%s'\n", filename);
            exit(1);
        }
    } else if (kernel_filename == NULL && !qtest_enabled()) {
        fprintf(stderr, "Can't read bios image %s\n", filename);
        exit(1);
    }
    g_free(filename);

    /* Can directly load an application. */
    if (kernel_filename != NULL) {
        long     kernel_size;
        uint64_t entry;

        kernel_size = load_elf(kernel_filename, NULL, NULL, &entry, NULL, NULL,
                               1 /* big endian */, EM_SPARC, 0, 0);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
        if (bios_size <= 0) {
            /* If there is no bios/monitor, start the application.  */
            env->pc = entry;
            env->npc = entry + 4;
            reset_info->entry = entry;
        }
    }

    /* Allocate timers */
    grlib_gptimer_create(0x80000300, 2, CPU_CLK, cpu_irqs, 6);

    /* Allocate uart */
    if (serial_hds[0]) {
        grlib_apbuart_create(0x80000100, serial_hds[0], cpu_irqs[3]);
    }
}

static void leon3_generic_machine_init(MachineClass *mc)
{
    mc->desc = "Leon-3 generic";
    mc->init = leon3_generic_hw_init;
}

DEFINE_MACHINE("leon3_generic", leon3_generic_machine_init)
