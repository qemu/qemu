/*
 * QEMU Leon3 System Emulator
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2010-2024 AdaCore
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
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/datadir.h"
#include "cpu.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "system/system.h"
#include "system/qtest.h"
#include "system/reset.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "trace.h"

#include "hw/timer/grlib_gptimer.h"
#include "hw/char/grlib_uart.h"
#include "hw/intc/grlib_irqmp.h"
#include "hw/misc/grlib_ahb_apb_pnp.h"

/* Default system clock.  */
#define CPU_CLK (40 * 1000 * 1000)

#define LEON3_PROM_FILENAME "u-boot.bin"
#define LEON3_PROM_OFFSET    (0x00000000)
#define LEON3_RAM_OFFSET     (0x40000000)

#define MAX_CPUS  4

#define LEON3_UART_OFFSET  (0x80000100)
#define LEON3_UART_IRQ     (3)

#define LEON3_IRQMP_OFFSET (0x80000200)

#define LEON3_TIMER_OFFSET (0x80000300)
#define LEON3_TIMER_IRQ    (6)
#define LEON3_TIMER_COUNT  (2)

#define LEON3_APB_PNP_OFFSET (0x800FF000)
#define LEON3_AHB_PNP_OFFSET (0xFFFFF000)

typedef struct ResetData {
    struct CPUResetData {
        int id;
        SPARCCPU *cpu;
    } info[MAX_CPUS];
    uint32_t entry;             /* save kernel entry in case of reset */
} ResetData;

static uint32_t *gen_store_u32(uint32_t *code, hwaddr addr, uint32_t val)
{
    stl_p(code++, 0x82100000); /* mov %g0, %g1                */
    stl_p(code++, 0x84100000); /* mov %g0, %g2                */
    stl_p(code++, 0x03000000 +
      extract32(addr, 10, 22));
                               /* sethi %hi(addr), %g1        */
    stl_p(code++, 0x82106000 +
      extract32(addr, 0, 10));
                               /* or %g1, addr, %g1           */
    stl_p(code++, 0x05000000 +
      extract32(val, 10, 22));
                               /* sethi %hi(val), %g2         */
    stl_p(code++, 0x8410a000 +
      extract32(val, 0, 10));
                               /* or %g2, val, %g2            */
    stl_p(code++, 0xc4204000); /* st %g2, [ %g1 ]             */

    return code;
}

/*
 * When loading a kernel in RAM the machine is expected to be in a different
 * state (eg: initialized by the bootloader).  This little code reproduces
 * this behavior.  Also this code can be executed by the secondary cpus as
 * well since it looks at the %asr17 register before doing any
 * initialization, it allows to use the same reset address for all the
 * cpus.
 */
static void write_bootloader(void *ptr, hwaddr kernel_addr)
{
    uint32_t *p = ptr;
    uint32_t *sec_cpu_branch_p = NULL;

    /* If we are running on a secondary CPU, jump directly to the kernel.  */

    stl_p(p++, 0x85444000); /* rd %asr17, %g2      */
    stl_p(p++, 0x8530a01c); /* srl  %g2, 0x1c, %g2 */
    stl_p(p++, 0x80908000); /* tst  %g2            */
    /* Filled below.  */
    sec_cpu_branch_p = p;
    stl_p(p++, 0x0BADC0DE); /* bne xxx             */
    stl_p(p++, 0x01000000); /* nop */

    /* Initialize the UARTs                                        */
    /* *UART_CONTROL = UART_RECEIVE_ENABLE | UART_TRANSMIT_ENABLE; */
    p = gen_store_u32(p, 0x80000108, 3);

    /* Initialize the TIMER 0                                      */
    /* *GPTIMER_SCALER_RELOAD = 40 - 1;                            */
    p = gen_store_u32(p, 0x80000304, 39);
    /* *GPTIMER0_COUNTER_RELOAD = 0xFFFE;                          */
    p = gen_store_u32(p, 0x80000314, 0xFFFFFFFE);
    /* *GPTIMER0_CONFIG = GPTIMER_ENABLE | GPTIMER_RESTART;        */
    p = gen_store_u32(p, 0x80000318, 3);

    /* Now, the relative branch above can be computed.  */
    stl_p(sec_cpu_branch_p, 0x12800000
          + (p - sec_cpu_branch_p));

    /* JUMP to the entry point                                     */
    stl_p(p++, 0x82100000); /* mov %g0, %g1 */
    stl_p(p++, 0x03000000 + extract32(kernel_addr, 10, 22));
                            /* sethi %hi(kernel_addr), %g1 */
    stl_p(p++, 0x82106000 + extract32(kernel_addr, 0, 10));
                            /* or kernel_addr, %g1 */
    stl_p(p++, 0x81c04000); /* jmp  %g1 */
    stl_p(p++, 0x01000000); /* nop */
}

static void leon3_cpu_reset(void *opaque)
{
    struct CPUResetData *info = (struct CPUResetData *) opaque;
    int id = info->id;
    ResetData *s = container_of(info, ResetData, info[id]);
    CPUState *cpu = CPU(s->info[id].cpu);
    CPUSPARCState *env = cpu_env(cpu);

    cpu_reset(cpu);

    cpu->halted = cpu->cpu_index != 0;
    env->pc = s->entry;
    env->npc = s->entry + 4;
}

static void leon3_cache_control_int(CPUSPARCState *env)
{
    uint32_t state = 0;

    if (env->cache_control & CACHE_CTRL_IF) {
        /* Instruction cache state */
        state = env->cache_control & CACHE_STATE_MASK;
        if (state == CACHE_ENABLED) {
            state = CACHE_FROZEN;
            trace_int_helper_icache_freeze();
        }

        env->cache_control &= ~CACHE_STATE_MASK;
        env->cache_control |= state;
    }

    if (env->cache_control & CACHE_CTRL_DF) {
        /* Data cache state */
        state = (env->cache_control >> 2) & CACHE_STATE_MASK;
        if (state == CACHE_ENABLED) {
            state = CACHE_FROZEN;
            trace_int_helper_dcache_freeze();
        }

        env->cache_control &= ~(CACHE_STATE_MASK << 2);
        env->cache_control |= (state << 2);
    }
}

static void leon3_irq_ack(CPUSPARCState *env, int intno)
{
    CPUState *cpu = CPU(env_cpu(env));
    grlib_irqmp_ack(env->irq_manager, cpu->cpu_index, intno);
}

/*
 * This device assumes that the incoming 'level' value on the
 * qemu_irq is the interrupt number, not just a simple 0/1 level.
 */
static void leon3_set_pil_in(void *opaque, int n, int level)
{
    DeviceState *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUSPARCState *env = cpu_env(cs);
    uint32_t pil_in = level;

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
                    trace_leon3_set_irq(i);
                    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (!env->pil_in && (env->interrupt_index & ~15) == TT_EXTINT) {
        trace_leon3_reset_irq(env->interrupt_index & 15);
        env->interrupt_index = 0;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

static void leon3_start_cpu_async_work(CPUState *cpu, run_on_cpu_data data)
{
    cpu->halted = 0;
}

static void leon3_start_cpu(void *opaque, int n, int level)
{
    DeviceState *cpu = opaque;
    CPUState *cs = CPU(cpu);

    assert(level == 1);
    async_run_on_cpu(cs, leon3_start_cpu_async_work, RUN_ON_CPU_NULL);
}

static void leon3_irq_manager(CPUSPARCState *env, int intno)
{
    leon3_irq_ack(env, intno);
    leon3_cache_control_int(env);
}

static void leon3_generic_hw_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *bios_name = machine->firmware ?: LEON3_PROM_FILENAME;
    const char *kernel_filename = machine->kernel_filename;
    SPARCCPU *cpu;
    CPUSPARCState   *env;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *prom = g_new(MemoryRegion, 1);
    int         ret;
    char       *filename;
    int         bios_size;
    int         prom_size;
    ResetData  *reset_info;
    DeviceState *dev, *irqmpdev;
    int i;
    AHBPnp *ahb_pnp;
    APBPnp *apb_pnp;

    reset_info = g_malloc0(sizeof(ResetData));

    for (i = 0; i < machine->smp.cpus; i++) {
        /* Init CPU */
        cpu = SPARC_CPU(object_new(machine->cpu_type));
        qdev_init_gpio_in_named(DEVICE(cpu), leon3_start_cpu, "start_cpu", 1);
        qdev_init_gpio_in_named(DEVICE(cpu), leon3_set_pil_in, "pil", 1);
        qdev_realize(DEVICE(cpu), NULL, &error_fatal);
        env = &cpu->env;

        cpu_sparc_set_id(env, i);

        /* Reset data */
        reset_info->info[i].id = i;
        reset_info->info[i].cpu = cpu;
        qemu_register_reset(leon3_cpu_reset, &reset_info->info[i]);
    }

    ahb_pnp = GRLIB_AHB_PNP(qdev_new(TYPE_GRLIB_AHB_PNP));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ahb_pnp), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(ahb_pnp), 0, LEON3_AHB_PNP_OFFSET);
    grlib_ahb_pnp_add_entry(ahb_pnp, 0, 0, GRLIB_VENDOR_GAISLER,
                            GRLIB_LEON3_DEV, GRLIB_AHB_MASTER,
                            GRLIB_CPU_AREA);

    apb_pnp = GRLIB_APB_PNP(qdev_new(TYPE_GRLIB_APB_PNP));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(apb_pnp), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(apb_pnp), 0, LEON3_APB_PNP_OFFSET);
    grlib_ahb_pnp_add_entry(ahb_pnp, LEON3_APB_PNP_OFFSET, 0xFFF,
                            GRLIB_VENDOR_GAISLER, GRLIB_APBMST_DEV,
                            GRLIB_AHB_SLAVE, GRLIB_AHBMEM_AREA);

    /* Allocate IRQ manager */
    irqmpdev = qdev_new(TYPE_GRLIB_IRQMP);
    object_property_set_int(OBJECT(irqmpdev), "ncpus", machine->smp.cpus,
                            &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(irqmpdev), &error_fatal);

    for (i = 0; i < machine->smp.cpus; i++) {
        cpu = reset_info->info[i].cpu;
        env = &cpu->env;
        qdev_connect_gpio_out_named(irqmpdev, "grlib-start-cpu", i,
                                    qdev_get_gpio_in_named(DEVICE(cpu),
                                                           "start_cpu", 0));
        qdev_connect_gpio_out_named(irqmpdev, "grlib-irq", i,
                                    qdev_get_gpio_in_named(DEVICE(cpu),
                                                           "pil", 0));
        env->irq_manager = irqmpdev;
        env->qemu_irq_ack = leon3_irq_manager;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(irqmpdev), 0, LEON3_IRQMP_OFFSET);
    grlib_apb_pnp_add_entry(apb_pnp, LEON3_IRQMP_OFFSET, 0xFFF,
                            GRLIB_VENDOR_GAISLER, GRLIB_IRQMP_DEV,
                            2, 0, GRLIB_APBIO_AREA);

    /* Allocate RAM */
    if (ram_size > 1 * GiB) {
        error_report("Too much memory for this machine: %" PRId64 "MB,"
                     " maximum 1G",
                     ram_size / MiB);
        exit(1);
    }

    memory_region_add_subregion(address_space_mem, LEON3_RAM_OFFSET,
                                machine->ram);

    /* Allocate BIOS */
    prom_size = 8 * MiB;
    memory_region_init_rom(prom, NULL, "Leon3.bios", prom_size, &error_fatal);
    memory_region_add_subregion(address_space_mem, LEON3_PROM_OFFSET, prom);

    /* Load boot prom */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

    if (filename) {
        bios_size = get_image_size(filename);
    } else {
        bios_size = -1;
    }

    if (bios_size > prom_size) {
        error_report("could not load prom '%s': file too big", filename);
        exit(1);
    }

    if (bios_size > 0) {
        ret = load_image_targphys(filename, LEON3_PROM_OFFSET, bios_size);
        if (ret < 0 || ret > prom_size) {
            error_report("could not load prom '%s'", filename);
            exit(1);
        }
    } else if (kernel_filename == NULL && !qtest_enabled()) {
        error_report("Can't read bios image '%s'", filename
                                                   ? filename
                                                   : LEON3_PROM_FILENAME);
        exit(1);
    }
    g_free(filename);

    /* Can directly load an application. */
    if (kernel_filename != NULL) {
        long     kernel_size;
        uint64_t entry;

        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &entry, NULL, NULL, NULL,
                               ELFDATA2MSB, EM_SPARC, 0, 0);
        if (kernel_size < 0) {
            kernel_size = load_uimage(kernel_filename, NULL, &entry,
                                      NULL, NULL, NULL);
        }
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        if (bios_size <= 0) {
            /*
             * If there is no bios/monitor just start the application but put
             * the machine in an initialized state through a little
             * bootloader.
             */
            write_bootloader(memory_region_get_ram_ptr(prom), entry);
            reset_info->entry = LEON3_PROM_OFFSET;
            for (i = 0; i < machine->smp.cpus; i++) {
                reset_info->info[i].cpu->env.pc = LEON3_PROM_OFFSET;
                reset_info->info[i].cpu->env.npc = LEON3_PROM_OFFSET + 4;
            }
        }
    }

    /* Allocate timers */
    dev = qdev_new(TYPE_GRLIB_GPTIMER);
    qdev_prop_set_uint32(dev, "nr-timers", LEON3_TIMER_COUNT);
    qdev_prop_set_uint32(dev, "frequency", CPU_CLK);
    qdev_prop_set_uint32(dev, "irq-line", LEON3_TIMER_IRQ);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, LEON3_TIMER_OFFSET);
    for (i = 0; i < LEON3_TIMER_COUNT; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                           qdev_get_gpio_in(irqmpdev, LEON3_TIMER_IRQ + i));
    }

    grlib_apb_pnp_add_entry(apb_pnp, LEON3_TIMER_OFFSET, 0xFFF,
                            GRLIB_VENDOR_GAISLER, GRLIB_GPTIMER_DEV,
                            0, LEON3_TIMER_IRQ, GRLIB_APBIO_AREA);

    /* Allocate uart */
    dev = qdev_new(TYPE_GRLIB_APB_UART);
    qdev_prop_set_chr(dev, "chrdev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, LEON3_UART_OFFSET);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(irqmpdev, LEON3_UART_IRQ));
    grlib_apb_pnp_add_entry(apb_pnp, LEON3_UART_OFFSET, 0xFFF,
                            GRLIB_VENDOR_GAISLER, GRLIB_APBUART_DEV, 1,
                            LEON3_UART_IRQ, GRLIB_APBIO_AREA);
}

static void leon3_generic_machine_init(MachineClass *mc)
{
    mc->desc = "Leon-3 generic";
    mc->init = leon3_generic_hw_init;
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("LEON3");
    mc->default_ram_id = "leon3.ram";
    mc->max_cpus = MAX_CPUS;
}

DEFINE_MACHINE("leon3_generic", leon3_generic_machine_init)
