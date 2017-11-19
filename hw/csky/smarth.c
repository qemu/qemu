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

#define CORET_IRQ_NUM   0

static struct csky_boot_info smarth_binfo = {
    .loader_start = 0x0,
    .freq         = 50000000ll,
};

static void smarth_init(MachineState *machine)
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
    MemoryRegion *ram = g_new(MemoryRegion, 1);

    if (!machine->cpu_model) {
        machine->cpu_model = "ck810f";
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

    memory_region_allocate_system_memory(ram, NULL, "smarth.sdram",
                                         machine->ram_size);
    memory_region_add_subregion(sysmem, 0x8000000, ram);

    cpu_intc = csky_intc_init_cpu(env);

    dev = sysbus_create_simple("csky_intc", 0x10010000, cpu_intc[0]);

    for (i = 0; i < 32; i++) {
        intc[i] = qdev_get_gpio_in(dev, i);
    }

    csky_uart_create(0x10015000, intc[16], serial_hds[0]);

    csky_timer_set_freq(smarth_binfo.freq);
    sysbus_create_varargs("csky_timer", 0x10011000, intc[12], intc[13],
                            intc[14], intc[15], NULL);

    sysbus_create_simple("csky_exit", 0x10002000, NULL);

    smarth_binfo.ram_size = machine->ram_size;
    smarth_binfo.kernel_filename = machine->kernel_filename;
    smarth_binfo.kernel_cmdline = machine->kernel_cmdline;
    smarth_binfo.initrd_filename = machine->initrd_filename;
    csky_load_kernel(cpu, &smarth_binfo);
}

static void smarth_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "CSKY smarth";
    mc->init = smarth_init;
}

static const TypeInfo smarth_type = {
    .name = MACHINE_TYPE_NAME("smarth"),
    .parent = TYPE_MACHINE,
    .class_init = smarth_class_init,
};

static void smarth_machine_init(void)
{
    type_register_static(&smarth_type);
}

type_init(smarth_machine_init)
