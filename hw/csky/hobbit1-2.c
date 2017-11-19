/*
 * CSKY Trilobite V2 System emulation.
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

#undef NEED_CPU_H
#define NEED_CPU_H

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "target/csky/cpu.h"
#include "hw/csky/csky.h"
#include "hw/sysbus.h"
#include "hw/devices.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "sysemu/block-backend.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/csky/cskydev.h"
#include "hw/char/csky_uart.h"

#define CORET_IRQ_NUM   1
#define HOBBIT1_2_SRAM0    (1024 * 1024)
#define HOBBIT1_2_SRAM1    (1024 * 1024)
#define HOBBIT1_2_SRAM2    (1024 * 1024)

static struct csky_boot_info hobbit1_2_binfo = {
    .loader_start = 0x0,
    .freq         = 1000000000ll,
};

static void hobbit1_2_init(MachineState *machine)
{
    ObjectClass *cpu_oc;
    Object *cpuobj;
    CSKYCPU *cpu;
    CPUCSKYState *env;
    qemu_irq *cpu_intc;
    qemu_irq intc[32];
    DeviceState *dev;
    int i;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram0 = g_new(MemoryRegion, 1);
    MemoryRegion *ram1 = g_new(MemoryRegion, 1);
    MemoryRegion *ram2 = g_new(MemoryRegion, 1);
    if (!machine->cpu_model) {
        machine->cpu_model = "ck802";
    }

    cpu_oc = cpu_class_by_name(TYPE_CSKY_CPU, machine->cpu_model);
    if (!cpu_oc) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    cpuobj = object_new(object_class_get_name(cpu_oc));

    object_property_set_bool(cpuobj, true, "realized", &error_fatal);

    cpu = CSKY_CPU(cpuobj);
    env = &cpu->env;

    memory_region_allocate_system_memory(ram0, NULL, "hobbit1_2.sdram0",
                                         HOBBIT1_2_SRAM0);
    memory_region_add_subregion(sysmem, 0x0, ram0);
    memory_region_allocate_system_memory(ram1, NULL, "hobbit1_2.sdram1",
                                         HOBBIT1_2_SRAM1);
    memory_region_add_subregion(sysmem, 0x10000000, ram1);
    memory_region_allocate_system_memory(ram2, NULL, "hobbit1_2.sdram2",
                                         HOBBIT1_2_SRAM2);
    memory_region_add_subregion(sysmem, 0x20000000, ram2);

    cpu_intc = csky_vic_v1_init_cpu(env, CORET_IRQ_NUM);

    csky_tcip_v1_set_freq(hobbit1_2_binfo.freq);
    dev = sysbus_create_simple("csky_tcip_v1", 0xE000E000, cpu_intc[0]);

    for (i = 0; i < 32; i++) {
        intc[i] = qdev_get_gpio_in(dev, i);
    }

    /* if config uart 0, uart address = 0x50010000,
     * if config uart 1, uart address = 0x50010400. */

    /* csky_uart_create(0x50010000, intc[6], serial_hds[0]); */
    csky_uart_create(0x50010400, intc[7], serial_hds[0]);

    hobbit1_2_binfo.ram_size = machine->ram_size;
    hobbit1_2_binfo.kernel_filename = machine->kernel_filename;
    hobbit1_2_binfo.kernel_cmdline = machine->kernel_cmdline;
    hobbit1_2_binfo.initrd_filename = machine->initrd_filename;
    csky_load_kernel(cpu, &hobbit1_2_binfo);
}

static void hobbit1_2_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "CSKY hobbit1_2";
    mc->init = hobbit1_2_init;
}

static const TypeInfo hobbit1_2_type = {
    .name = MACHINE_TYPE_NAME("hobbit1_2"),
    .parent = TYPE_MACHINE,
    .class_init = hobbit1_2_class_init,
};

static void hobbit1_2_machine_init(void)
{
    type_register_static(&hobbit1_2_type);
}

type_init(hobbit1_2_machine_init)
