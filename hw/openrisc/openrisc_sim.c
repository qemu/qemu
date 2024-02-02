/*
 * OpenRISC simulator for use as an IIS.
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Feng Gao <gf91597@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/irq.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "net/net.h"
#include "hw/openrisc/boot.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "hw/core/split-irq.h"

#include <libfdt.h>

#define KERNEL_LOAD_ADDR 0x100

#define OR1KSIM_CPUS_MAX 4
#define OR1KSIM_CLK_MHZ 20000000

#define TYPE_OR1KSIM_MACHINE MACHINE_TYPE_NAME("or1k-sim")
#define OR1KSIM_MACHINE(obj) \
    OBJECT_CHECK(Or1ksimState, (obj), TYPE_OR1KSIM_MACHINE)

typedef struct Or1ksimState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    void *fdt;
    int fdt_size;

} Or1ksimState;

enum {
    OR1KSIM_DRAM,
    OR1KSIM_UART,
    OR1KSIM_ETHOC,
    OR1KSIM_OMPIC,
};

enum {
    OR1KSIM_OMPIC_IRQ = 1,
    OR1KSIM_UART_IRQ = 2,
    OR1KSIM_ETHOC_IRQ = 4,
};

enum {
    OR1KSIM_UART_COUNT = 4
};

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} or1ksim_memmap[] = {
    [OR1KSIM_DRAM] =      { 0x00000000,          0 },
    [OR1KSIM_UART] =      { 0x90000000,      0x100 },
    [OR1KSIM_ETHOC] =     { 0x92000000,      0x800 },
    [OR1KSIM_OMPIC] =     { 0x98000000, OR1KSIM_CPUS_MAX * 8 },
};

static struct openrisc_boot_info {
    uint32_t bootstrap_pc;
    uint32_t fdt_addr;
} boot_info;

static void main_cpu_reset(void *opaque)
{
    OpenRISCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    cpu_reset(CPU(cpu));

    cpu_set_pc(cs, boot_info.bootstrap_pc);
    cpu_set_gpr(&cpu->env, 3, boot_info.fdt_addr);
}

static qemu_irq get_cpu_irq(OpenRISCCPU *cpus[], int cpunum, int irq_pin)
{
    return qdev_get_gpio_in_named(DEVICE(cpus[cpunum]), "IRQ", irq_pin);
}

static void openrisc_create_fdt(Or1ksimState *state,
                                const struct MemmapEntry *memmap,
                                int num_cpus, uint64_t mem_size,
                                const char *cmdline)
{
    void *fdt;
    int cpu;
    char *nodename;
    int pic_ph;

    fdt = state->fdt = create_device_tree(&state->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "compatible", "opencores,or1ksim");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x1);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x1);

    nodename = g_strdup_printf("/memory@%" HWADDR_PRIx,
                               memmap[OR1KSIM_DRAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           memmap[OR1KSIM_DRAM].base, mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = 0; cpu < num_cpus; cpu++) {
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "compatible",
                                "opencores,or1200-rtlsvn481");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency",
                              OR1KSIM_CLK_MHZ);
        g_free(nodename);
    }

    nodename = (char *)"/pic";
    qemu_fdt_add_subnode(fdt, nodename);
    pic_ph = qemu_fdt_alloc_phandle(fdt);
    qemu_fdt_setprop_string(fdt, nodename, "compatible",
                            "opencores,or1k-pic-level");
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", pic_ph);

    qemu_fdt_setprop_cell(fdt, "/", "interrupt-parent", pic_ph);

    qemu_fdt_add_subnode(fdt, "/chosen");
    if (cmdline) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }

    /* Create aliases node for use by devices. */
    qemu_fdt_add_subnode(fdt, "/aliases");
}

static void openrisc_sim_net_init(Or1ksimState *state, hwaddr base, hwaddr size,
                                  int num_cpus, OpenRISCCPU *cpus[],
                                  int irq_pin)
{
    void *fdt = state->fdt;
    DeviceState *dev;
    SysBusDevice *s;
    char *nodename;
    int i;

    dev = qemu_create_nic_device("open_eth", true, NULL);
    if (!dev) {
        return;
    }

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    if (num_cpus > 1) {
        DeviceState *splitter = qdev_new(TYPE_SPLIT_IRQ);
        qdev_prop_set_uint32(splitter, "num-lines", num_cpus);
        qdev_realize_and_unref(splitter, NULL, &error_fatal);
        for (i = 0; i < num_cpus; i++) {
            qdev_connect_gpio_out(splitter, i, get_cpu_irq(cpus, i, irq_pin));
        }
        sysbus_connect_irq(s, 0, qdev_get_gpio_in(splitter, 0));
    } else {
        sysbus_connect_irq(s, 0, get_cpu_irq(cpus, 0, irq_pin));
    }
    sysbus_mmio_map(s, 0, base);
    sysbus_mmio_map(s, 1, base + 0x400);

    /* Init device tree node for ethoc. */
    nodename = g_strdup_printf("/ethoc@%" HWADDR_PRIx, base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "opencores,ethoc");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", base, size);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", irq_pin);
    qemu_fdt_setprop(fdt, nodename, "big-endian", NULL, 0);

    qemu_fdt_setprop_string(fdt, "/aliases", "enet0", nodename);
    g_free(nodename);
}

static void openrisc_sim_ompic_init(Or1ksimState *state, hwaddr base,
                                    hwaddr size, int num_cpus,
                                    OpenRISCCPU *cpus[], int irq_pin)
{
    void *fdt = state->fdt;
    DeviceState *dev;
    SysBusDevice *s;
    char *nodename;
    int i;

    dev = qdev_new("or1k-ompic");
    qdev_prop_set_uint32(dev, "num-cpus", num_cpus);

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    for (i = 0; i < num_cpus; i++) {
        sysbus_connect_irq(s, i, get_cpu_irq(cpus, i, irq_pin));
    }
    sysbus_mmio_map(s, 0, base);

    /* Add device tree node for ompic. */
    nodename = g_strdup_printf("/ompic@%" HWADDR_PRIx, base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "openrisc,ompic");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", base, size);
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 0);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", irq_pin);
    g_free(nodename);
}

static void openrisc_sim_serial_init(Or1ksimState *state, hwaddr base,
                                     hwaddr size, int num_cpus,
                                     OpenRISCCPU *cpus[], int irq_pin,
                                     int uart_idx)
{
    void *fdt = state->fdt;
    char *nodename;
    qemu_irq serial_irq;
    char alias[sizeof("uart0")];
    int i;

    if (num_cpus > 1) {
        DeviceState *splitter = qdev_new(TYPE_SPLIT_IRQ);
        qdev_prop_set_uint32(splitter, "num-lines", num_cpus);
        qdev_realize_and_unref(splitter, NULL, &error_fatal);
        for (i = 0; i < num_cpus; i++) {
            qdev_connect_gpio_out(splitter, i, get_cpu_irq(cpus, i, irq_pin));
        }
        serial_irq = qdev_get_gpio_in(splitter, 0);
    } else {
        serial_irq = get_cpu_irq(cpus, 0, irq_pin);
    }
    serial_mm_init(get_system_memory(), base, 0, serial_irq, 115200,
                   serial_hd(OR1KSIM_UART_COUNT - uart_idx - 1),
                   DEVICE_NATIVE_ENDIAN);

    /* Add device tree node for serial. */
    nodename = g_strdup_printf("/serial@%" HWADDR_PRIx, base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", base, size);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", irq_pin);
    qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency", OR1KSIM_CLK_MHZ);
    qemu_fdt_setprop(fdt, nodename, "big-endian", NULL, 0);

    /* The /chosen node is created during fdt creation. */
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", nodename);
    snprintf(alias, sizeof(alias), "uart%d", uart_idx);
    qemu_fdt_setprop_string(fdt, "/aliases", alias, nodename);
    g_free(nodename);
}

static void openrisc_sim_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    OpenRISCCPU *cpus[OR1KSIM_CPUS_MAX] = {};
    Or1ksimState *state = OR1KSIM_MACHINE(machine);
    MemoryRegion *ram;
    hwaddr load_addr;
    int n;
    unsigned int smp_cpus = machine->smp.cpus;

    assert(smp_cpus >= 1 && smp_cpus <= OR1KSIM_CPUS_MAX);
    for (n = 0; n < smp_cpus; n++) {
        cpus[n] = OPENRISC_CPU(cpu_create(machine->cpu_type));
        if (cpus[n] == NULL) {
            fprintf(stderr, "Unable to find CPU definition!\n");
            exit(1);
        }

        cpu_openrisc_clock_init(cpus[n]);

        qemu_register_reset(main_cpu_reset, cpus[n]);
    }

    ram = g_malloc(sizeof(*ram));
    memory_region_init_ram(ram, NULL, "openrisc.ram", ram_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    openrisc_create_fdt(state, or1ksim_memmap, smp_cpus, machine->ram_size,
                        machine->kernel_cmdline);

    openrisc_sim_net_init(state, or1ksim_memmap[OR1KSIM_ETHOC].base,
                          or1ksim_memmap[OR1KSIM_ETHOC].size,
                          smp_cpus, cpus,
                          OR1KSIM_ETHOC_IRQ);

    if (smp_cpus > 1) {
        openrisc_sim_ompic_init(state, or1ksim_memmap[OR1KSIM_OMPIC].base,
                                or1ksim_memmap[OR1KSIM_OMPIC].size,
                                smp_cpus, cpus, OR1KSIM_OMPIC_IRQ);
    }

    for (n = 0; n < OR1KSIM_UART_COUNT; ++n)
        openrisc_sim_serial_init(state, or1ksim_memmap[OR1KSIM_UART].base +
                                        or1ksim_memmap[OR1KSIM_UART].size * n,
                                 or1ksim_memmap[OR1KSIM_UART].size,
                                 smp_cpus, cpus, OR1KSIM_UART_IRQ, n);

    load_addr = openrisc_load_kernel(ram_size, kernel_filename,
                                     &boot_info.bootstrap_pc);
    if (load_addr > 0) {
        if (machine->initrd_filename) {
            load_addr = openrisc_load_initrd(state->fdt,
                                             machine->initrd_filename,
                                             load_addr, machine->ram_size);
        }
        boot_info.fdt_addr = openrisc_load_fdt(state->fdt, load_addr,
                                               machine->ram_size);
    }
}

static void openrisc_sim_machine_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "or1k simulation";
    mc->init = openrisc_sim_init;
    mc->max_cpus = OR1KSIM_CPUS_MAX;
    mc->is_default = true;
    mc->default_cpu_type = OPENRISC_CPU_TYPE_NAME("or1200");
}

static const TypeInfo or1ksim_machine_typeinfo = {
    .name       = TYPE_OR1KSIM_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = openrisc_sim_machine_init,
    .instance_size = sizeof(Or1ksimState),
};

static void or1ksim_machine_init_register_types(void)
{
    type_register_static(&or1ksim_machine_typeinfo);
}

type_init(or1ksim_machine_init_register_types)
