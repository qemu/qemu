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
#include "elf.h"
#include "hw/char/serial.h"
#include "net/net.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "hw/core/split-irq.h"

#define KERNEL_LOAD_ADDR 0x100

#define OR1KSIM_CPUS_MAX 4

#define TYPE_OR1KSIM_MACHINE MACHINE_TYPE_NAME("or1k-sim")
#define OR1KSIM_MACHINE(obj) \
    OBJECT_CHECK(Or1ksimState, (obj), TYPE_OR1KSIM_MACHINE)

typedef struct Or1ksimState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/

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

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} or1ksim_memmap[] = {
    [OR1KSIM_DRAM] =      { 0x00000000,          0 },
    [OR1KSIM_UART] =      { 0x90000000,      0x100 },
    [OR1KSIM_ETHOC] =     { 0x92000000,      0x800 },
    [OR1KSIM_OMPIC] =     { 0x98000000,         16 },
};

static struct openrisc_boot_info {
    uint32_t bootstrap_pc;
} boot_info;

static void main_cpu_reset(void *opaque)
{
    OpenRISCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    cpu_reset(CPU(cpu));

    cpu_set_pc(cs, boot_info.bootstrap_pc);
}

static qemu_irq get_cpu_irq(OpenRISCCPU *cpus[], int cpunum, int irq_pin)
{
    return qdev_get_gpio_in_named(DEVICE(cpus[cpunum]), "IRQ", irq_pin);
}

static void openrisc_sim_net_init(hwaddr base, hwaddr descriptors,
                                  int num_cpus, OpenRISCCPU *cpus[],
                                  int irq_pin, NICInfo *nd)
{
    DeviceState *dev;
    SysBusDevice *s;
    int i;

    dev = qdev_new("open_eth");
    qdev_set_nic_properties(dev, nd);

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
    sysbus_mmio_map(s, 1, descriptors);
}

static void openrisc_sim_ompic_init(hwaddr base, int num_cpus,
                                    OpenRISCCPU *cpus[], int irq_pin)
{
    DeviceState *dev;
    SysBusDevice *s;
    int i;

    dev = qdev_new("or1k-ompic");
    qdev_prop_set_uint32(dev, "num-cpus", num_cpus);

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    for (i = 0; i < num_cpus; i++) {
        sysbus_connect_irq(s, i, get_cpu_irq(cpus, i, irq_pin));
    }
    sysbus_mmio_map(s, 0, base);
}

static void openrisc_sim_serial_init(hwaddr base, int num_cpus,
                                     OpenRISCCPU *cpus[], int irq_pin)
{
    qemu_irq serial_irq;
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
                   serial_hd(0), DEVICE_NATIVE_ENDIAN);
}


static void openrisc_load_kernel(ram_addr_t ram_size,
                                 const char *kernel_filename)
{
    long kernel_size;
    uint64_t elf_entry;
    hwaddr entry;

    if (kernel_filename && !qtest_enabled()) {
        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &elf_entry, NULL, NULL, NULL, 1, EM_OPENRISC,
                               1, 0);
        entry = elf_entry;
        if (kernel_size < 0) {
            kernel_size = load_uimage(kernel_filename,
                                      &entry, NULL, NULL, NULL, NULL);
        }
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              ram_size - KERNEL_LOAD_ADDR);
        }

        if (entry <= 0) {
            entry = KERNEL_LOAD_ADDR;
        }

        if (kernel_size < 0) {
            error_report("couldn't load the kernel '%s'", kernel_filename);
            exit(1);
        }
        boot_info.bootstrap_pc = entry;
    }
}

static void openrisc_sim_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    OpenRISCCPU *cpus[OR1KSIM_CPUS_MAX] = {};
    MemoryRegion *ram;
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

    if (nd_table[0].used) {
        openrisc_sim_net_init(or1ksim_memmap[OR1KSIM_ETHOC].base,
                              or1ksim_memmap[OR1KSIM_ETHOC].base + 0x400,
                              smp_cpus, cpus,
                              OR1KSIM_ETHOC_IRQ, nd_table);
    }

    if (smp_cpus > 1) {
        openrisc_sim_ompic_init(or1ksim_memmap[OR1KSIM_OMPIC].base, smp_cpus,
                                cpus, OR1KSIM_OMPIC_IRQ);
    }

    openrisc_sim_serial_init(or1ksim_memmap[OR1KSIM_UART].base, smp_cpus, cpus,
                             OR1KSIM_UART_IRQ);

    openrisc_load_kernel(ram_size, kernel_filename);
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
