/*
 * OpenRISC simulator for use as an IIS.
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Feng Gao <gf91597@gmail.com>
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

#include "hw.h"
#include "boards.h"
#include "elf.h"
#include "pc.h"
#include "loader.h"
#include "exec-memory.h"
#include "sysemu.h"
#include "sysbus.h"
#include "qtest.h"

#define KERNEL_LOAD_ADDR 0x100

static void main_cpu_reset(void *opaque)
{
    OpenRISCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static void openrisc_sim_net_init(MemoryRegion *address_space,
                                  target_phys_addr_t base,
                                  target_phys_addr_t descriptors,
                                  qemu_irq irq, NICInfo *nd)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "open_eth");
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);

    s = sysbus_from_qdev(dev);
    sysbus_connect_irq(s, 0, irq);
    memory_region_add_subregion(address_space, base,
                                sysbus_mmio_get_region(s, 0));
    memory_region_add_subregion(address_space, descriptors,
                                sysbus_mmio_get_region(s, 1));
}

static void cpu_openrisc_load_kernel(ram_addr_t ram_size,
                                     const char *kernel_filename,
                                     OpenRISCCPU *cpu)
{
    long kernel_size;
    uint64_t elf_entry;
    target_phys_addr_t entry;

    if (kernel_filename && !qtest_enabled()) {
        kernel_size = load_elf(kernel_filename, NULL, NULL,
                               &elf_entry, NULL, NULL, 1, ELF_MACHINE, 1);
        entry = elf_entry;
        if (kernel_size < 0) {
            kernel_size = load_uimage(kernel_filename,
                                      &entry, NULL, NULL);
        }
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              ram_size - KERNEL_LOAD_ADDR);
            entry = KERNEL_LOAD_ADDR;
        }

        if (kernel_size < 0) {
            qemu_log("QEMU: couldn't load the kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }

    cpu->env.pc = entry;
}

static void openrisc_sim_init(ram_addr_t ram_size,
                              const char *boot_device,
                              const char *kernel_filename,
                              const char *kernel_cmdline,
                              const char *initrd_filename,
                              const char *cpu_model)
{
   OpenRISCCPU *cpu = NULL;
    MemoryRegion *ram;
    int n;

    if (!cpu_model) {
        cpu_model = "or1200";
    }

    for (n = 0; n < smp_cpus; n++) {
        cpu = cpu_openrisc_init(cpu_model);
        if (cpu == NULL) {
            qemu_log("Unable to find CPU defineition!\n");
            exit(1);
        }
        qemu_register_reset(main_cpu_reset, cpu);
        main_cpu_reset(cpu);
    }

    ram = g_malloc(sizeof(*ram));
    memory_region_init_ram(ram, "openrisc.ram", ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    cpu_openrisc_pic_init(cpu);
    cpu_openrisc_clock_init(cpu);

    serial_mm_init(get_system_memory(), 0x90000000, 0, cpu->env.irq[2],
                   115200, serial_hds[0], DEVICE_NATIVE_ENDIAN);

    if (nd_table[0].used) {
        openrisc_sim_net_init(get_system_memory(), 0x92000000,
                              0x92000400, cpu->env.irq[4], nd_table);
    }

    cpu_openrisc_load_kernel(ram_size, kernel_filename, cpu);
}

static QEMUMachine openrisc_sim_machine = {
    .name = "or32-sim",
    .desc = "or32 simulation",
    .init = openrisc_sim_init,
    .max_cpus = 1,
    .is_default = 1,
};

static void openrisc_sim_machine_init(void)
{
    qemu_register_machine(&openrisc_sim_machine);
}

machine_init(openrisc_sim_machine_init);
