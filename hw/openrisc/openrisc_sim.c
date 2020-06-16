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
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"

#define KERNEL_LOAD_ADDR 0x100

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

static void openrisc_sim_net_init(hwaddr base, hwaddr descriptors,
                                  int num_cpus, qemu_irq **cpu_irqs,
                                  int irq_pin, NICInfo *nd)
{
    DeviceState *dev;
    SysBusDevice *s;
    int i;

    dev = qdev_new("open_eth");
    qdev_set_nic_properties(dev, nd);

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    for (i = 0; i < num_cpus; i++) {
        sysbus_connect_irq(s, 0, cpu_irqs[i][irq_pin]);
    }
    sysbus_mmio_map(s, 0, base);
    sysbus_mmio_map(s, 1, descriptors);
}

static void openrisc_sim_ompic_init(hwaddr base, int num_cpus,
                                    qemu_irq **cpu_irqs, int irq_pin)
{
    DeviceState *dev;
    SysBusDevice *s;
    int i;

    dev = qdev_new("or1k-ompic");
    qdev_prop_set_uint32(dev, "num-cpus", num_cpus);

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    for (i = 0; i < num_cpus; i++) {
        sysbus_connect_irq(s, i, cpu_irqs[i][irq_pin]);
    }
    sysbus_mmio_map(s, 0, base);
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
    OpenRISCCPU *cpu = NULL;
    MemoryRegion *ram;
    qemu_irq *cpu_irqs[2];
    qemu_irq serial_irq;
    int n;
    unsigned int smp_cpus = machine->smp.cpus;

    assert(smp_cpus >= 1 && smp_cpus <= 2);
    for (n = 0; n < smp_cpus; n++) {
        cpu = OPENRISC_CPU(cpu_create(machine->cpu_type));
        if (cpu == NULL) {
            fprintf(stderr, "Unable to find CPU definition!\n");
            exit(1);
        }
        cpu_openrisc_pic_init(cpu);
        cpu_irqs[n] = (qemu_irq *) cpu->env.irq;

        cpu_openrisc_clock_init(cpu);

        qemu_register_reset(main_cpu_reset, cpu);
    }

    ram = g_malloc(sizeof(*ram));
    memory_region_init_ram(ram, NULL, "openrisc.ram", ram_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    if (nd_table[0].used) {
        openrisc_sim_net_init(0x92000000, 0x92000400, smp_cpus,
                              cpu_irqs, 4, nd_table);
    }

    if (smp_cpus > 1) {
        openrisc_sim_ompic_init(0x98000000, smp_cpus, cpu_irqs, 1);

        serial_irq = qemu_irq_split(cpu_irqs[0][2], cpu_irqs[1][2]);
    } else {
        serial_irq = cpu_irqs[0][2];
    }

    serial_mm_init(get_system_memory(), 0x90000000, 0, serial_irq,
                   115200, serial_hd(0), DEVICE_NATIVE_ENDIAN);

    openrisc_load_kernel(ram_size, kernel_filename);
}

static void openrisc_sim_machine_init(MachineClass *mc)
{
    mc->desc = "or1k simulation";
    mc->init = openrisc_sim_init;
    mc->max_cpus = 2;
    mc->is_default = true;
    mc->default_cpu_type = OPENRISC_CPU_TYPE_NAME("or1200");
}

DEFINE_MACHINE("or1k-sim", openrisc_sim_machine_init)
