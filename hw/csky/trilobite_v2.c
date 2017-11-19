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
#include "hw/csky/dynsoc.h"

#define CORET_IRQ_NUM   0

static struct csky_boot_info trilobite_v2_binfo = {
    .loader_start = 0x0,
    .dtb_addr = 0x8f000000,
    .magic = 0x20150401,
    .freq = 50000000ll,
};

static void trilobite_v2_init(MachineState *machine)
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

    memory_region_allocate_system_memory(ram, NULL, "trilobite_v2.sdram",
                                         machine->ram_size);
    memory_region_add_subregion(sysmem, 0x8000000, ram);

    cpu_intc = csky_intc_init_cpu(env);

    dev = sysbus_create_simple("csky_intc", 0x10010000, cpu_intc[0]);

    for (i = 0; i < 32; i++) {
        intc[i] = qdev_get_gpio_in(dev, i);
    }

    /* create uart */
    dev = qdev_create(NULL, "csky_uart");
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", serial_hds[0]);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, 0x10015000);
    sysbus_connect_irq(s, 0, intc[16]);

    csky_timer_set_freq(trilobite_v2_binfo.freq);
    sysbus_create_varargs("csky_timer", 0x10011000, intc[12], intc[13],
                            intc[14], intc[15], NULL);

    if (nd_table[0].used) {
        csky_mac_v2_create(&nd_table[0], 0x10006000, intc[26]);
    }

    sysbus_create_simple("csky_lcdc", 0x10004000, intc[28]);

    trilobite_v2_binfo.ram_size = machine->ram_size;
    trilobite_v2_binfo.kernel_filename = machine->kernel_filename;
    trilobite_v2_binfo.kernel_cmdline = machine->kernel_cmdline;
    trilobite_v2_binfo.initrd_filename = machine->initrd_filename;
    csky_load_kernel(cpu, &trilobite_v2_binfo);
}

static void trilobite_v2_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "CSKY trilobite_v2";
    mc->init = trilobite_v2_init;
}

static const TypeInfo trilobite_v2_type = {
    .name = MACHINE_TYPE_NAME("trilobite_v2"),
    .parent = TYPE_MACHINE,
    .class_init = trilobite_v2_class_init,
};

static void trilobite_v2_machine_init(void)
{
    type_register_static(&trilobite_v2_type);
}

type_init(trilobite_v2_machine_init)
