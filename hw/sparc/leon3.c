/*
 * QEMU Leon3 System Emulator
 *
 * Copyright (c) 2010-2019 AdaCore
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
#include "qemu-common.h"
#include "cpu.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "trace.h"
#include "exec/address-spaces.h"

#include "hw/sparc/grlib.h"
#include "hw/misc/grlib_ahb_apb_pnp.h"

/* Default system clock.  */
#define CPU_CLK (40 * 1000 * 1000)

#define LEON3_PROM_FILENAME "u-boot.bin"
#define LEON3_PROM_OFFSET    (0x00000000)
#define LEON3_RAM_OFFSET     (0x40000000)

#define MAX_PILS 16

#define LEON3_UART_OFFSET  (0x80000100)
#define LEON3_UART_IRQ     (3)

#define LEON3_IRQMP_OFFSET (0x80000200)

#define LEON3_TIMER_OFFSET (0x80000300)
#define LEON3_TIMER_IRQ    (6)
#define LEON3_TIMER_COUNT  (2)

#define LEON3_APB_PNP_OFFSET (0x800FF000)
#define LEON3_AHB_PNP_OFFSET (0xFFFFF000)

typedef struct ResetData {
    SPARCCPU *cpu;
    uint32_t  entry;            /* save kernel entry in case of reset */
    target_ulong sp;            /* initial stack pointer */
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
 * state (eg: initialized by the bootloader). This little code reproduces
 * this behavior.
 */
static void write_bootloader(CPUSPARCState *env, uint8_t *base,
                             hwaddr kernel_addr)
{
    uint32_t *p = (uint32_t *) base;

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

    /* JUMP to the entry point                                     */
    stl_p(p++, 0x82100000); /* mov %g0, %g1 */
    stl_p(p++, 0x03000000 + extract32(kernel_addr, 10, 22));
                            /* sethi %hi(kernel_addr), %g1 */
    stl_p(p++, 0x82106000 + extract32(kernel_addr, 0, 10));
                            /* or kernel_addr, %g1 */
    stl_p(p++, 0x81c04000); /* jmp  %g1 */
    stl_p(p++, 0x01000000); /* nop */
}

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

/*
 * This device assumes that the incoming 'level' value on the
 * qemu_irq is the interrupt number, not just a simple 0/1 level.
 */
static void leon3_set_pil_in(void *opaque, int n, int level)
{
    CPUSPARCState *env = opaque;
    uint32_t pil_in = level;
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
                    cs = env_cpu(env);
                    trace_leon3_set_irq(i);
                    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (!env->pil_in && (env->interrupt_index & ~15) == TT_EXTINT) {
        cs = env_cpu(env);
        trace_leon3_reset_irq(env->interrupt_index & 15);
        env->interrupt_index = 0;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

static void leon3_generic_hw_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    SPARCCPU *cpu;
    CPUSPARCState   *env;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *prom = g_new(MemoryRegion, 1);
    int         ret;
    char       *filename;
    qemu_irq   *cpu_irqs = NULL;
    int         bios_size;
    int         prom_size;
    ResetData  *reset_info;
    DeviceState *dev;
    int i;
    AHBPnp *ahb_pnp;
    APBPnp *apb_pnp;

    /* Init CPU */
    cpu = SPARC_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;

    cpu_sparc_set_id(env, 0);

    /* Reset data */
    reset_info        = g_malloc0(sizeof(ResetData));
    reset_info->cpu   = cpu;
    reset_info->sp    = LEON3_RAM_OFFSET + ram_size;
    qemu_register_reset(main_cpu_reset, reset_info);

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
    dev = qdev_new(TYPE_GRLIB_IRQMP);
    qdev_init_gpio_in_named_with_opaque(DEVICE(cpu), leon3_set_pil_in,
                                        env, "pil", 1);
    qdev_connect_gpio_out_named(dev, "grlib-irq", 0,
                                qdev_get_gpio_in_named(DEVICE(cpu), "pil", 0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, LEON3_IRQMP_OFFSET);
    env->irq_manager = dev;
    env->qemu_irq_ack = leon3_irq_manager;
    cpu_irqs = qemu_allocate_irqs(grlib_irqmp_set_irq, dev, MAX_PILS);
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
    if (bios_name == NULL) {
        bios_name = LEON3_PROM_FILENAME;
    }
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
                               1 /* big endian */, EM_SPARC, 0, 0);
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
            uint8_t *bootloader_entry;

            bootloader_entry = memory_region_get_ram_ptr(prom);
            write_bootloader(env, bootloader_entry, entry);
            env->pc = LEON3_PROM_OFFSET;
            env->npc = LEON3_PROM_OFFSET + 4;
            reset_info->entry = LEON3_PROM_OFFSET;
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
                           cpu_irqs[LEON3_TIMER_IRQ + i]);
    }

    grlib_apb_pnp_add_entry(apb_pnp, LEON3_TIMER_OFFSET, 0xFFF,
                            GRLIB_VENDOR_GAISLER, GRLIB_GPTIMER_DEV,
                            0, LEON3_TIMER_IRQ, GRLIB_APBIO_AREA);

    /* Allocate uart */
    dev = qdev_new(TYPE_GRLIB_APB_UART);
    qdev_prop_set_chr(dev, "chrdev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, LEON3_UART_OFFSET);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, cpu_irqs[LEON3_UART_IRQ]);
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
}

DEFINE_MACHINE("leon3_generic", leon3_generic_machine_init)
