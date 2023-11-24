/*
 * Altera 10M50 Nios2 GHRD
 *
 * Copyright (c) 2016 Marek Vasut <marek.vasut@gmail.com>
 *
 * Based on LabX device code
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "hw/sysbus.h"
#include "hw/char/serial.h"
#include "hw/intc/nios2_vic.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "qemu/config-file.h"

#include "boot.h"

struct Nios2MachineState {
    MachineState parent_obj;

    MemoryRegion phys_tcm;
    MemoryRegion phys_tcm_alias;
    MemoryRegion phys_ram;
    MemoryRegion phys_ram_alias;

    bool vic;
};

#define TYPE_NIOS2_MACHINE  MACHINE_TYPE_NAME("10m50-ghrd")
OBJECT_DECLARE_TYPE(Nios2MachineState, MachineClass, NIOS2_MACHINE)

#define BINARY_DEVICE_TREE_FILE    "10m50-devboard.dtb"

static void nios2_10m50_ghrd_init(MachineState *machine)
{
    Nios2MachineState *nms = NIOS2_MACHINE(machine);
    Nios2CPU *cpu;
    DeviceState *dev;
    MemoryRegion *address_space_mem = get_system_memory();
    ram_addr_t tcm_base = 0x0;
    ram_addr_t tcm_size = 0x1000;    /* 1 kiB, but QEMU limit is 4 kiB */
    ram_addr_t ram_base = 0x08000000;
    ram_addr_t ram_size = 0x08000000;
    qemu_irq irq[32];
    int i;

    /* Physical TCM (tb_ram_1k) with alias at 0xc0000000 */
    memory_region_init_ram(&nms->phys_tcm, NULL, "nios2.tcm", tcm_size,
                           &error_abort);
    memory_region_init_alias(&nms->phys_tcm_alias, NULL, "nios2.tcm.alias",
                             &nms->phys_tcm, 0, tcm_size);
    memory_region_add_subregion(address_space_mem, tcm_base, &nms->phys_tcm);
    memory_region_add_subregion(address_space_mem, 0xc0000000 + tcm_base,
                                &nms->phys_tcm_alias);

    /* Physical DRAM with alias at 0xc0000000 */
    memory_region_init_ram(&nms->phys_ram, NULL, "nios2.ram", ram_size,
                           &error_abort);
    memory_region_init_alias(&nms->phys_ram_alias, NULL, "nios2.ram.alias",
                             &nms->phys_ram, 0, ram_size);
    memory_region_add_subregion(address_space_mem, ram_base, &nms->phys_ram);
    memory_region_add_subregion(address_space_mem, 0xc0000000 + ram_base,
                                &nms->phys_ram_alias);

    /* Create CPU.  We need to set eic_present between init and realize. */
    cpu = NIOS2_CPU(object_new(TYPE_NIOS2_CPU));

    /* Enable the External Interrupt Controller within the CPU. */
    cpu->eic_present = nms->vic;

    /* Configure new exception vectors. */
    cpu->reset_addr = 0xd4000000;
    cpu->exception_addr = 0xc8000120;
    cpu->fast_tlb_miss_addr = 0xc0000100;

    qdev_realize_and_unref(DEVICE(cpu), NULL, &error_fatal);

    if (nms->vic) {
        dev = qdev_new(TYPE_NIOS2_VIC);
        MemoryRegion *dev_mr;
        qemu_irq cpu_irq;

        object_property_set_link(OBJECT(dev), "cpu", OBJECT(cpu), &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

        cpu_irq = qdev_get_gpio_in_named(DEVICE(cpu), "EIC", 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, cpu_irq);
        for (i = 0; i < 32; i++) {
            irq[i] = qdev_get_gpio_in(dev, i);
        }

        dev_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(address_space_mem, 0x18002000, dev_mr);
    } else {
        for (i = 0; i < 32; i++) {
            irq[i] = qdev_get_gpio_in_named(DEVICE(cpu), "IRQ", i);
        }
    }

    /* Register: Altera 16550 UART */
    serial_mm_init(address_space_mem, 0xf8001600, 2, irq[1], 115200,
                   serial_hd(0), DEVICE_NATIVE_ENDIAN);

    /* Register: Timer sys_clk_timer  */
    dev = qdev_new("ALTR.timer");
    qdev_prop_set_uint32(dev, "clock-frequency", 75 * 1000000);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xf8001440);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[0]);

    /* Register: Timer sys_clk_timer_1  */
    dev = qdev_new("ALTR.timer");
    qdev_prop_set_uint32(dev, "clock-frequency", 75 * 1000000);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xe0000880);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[5]);

    nios2_load_kernel(cpu, ram_base, ram_size, machine->initrd_filename,
                      BINARY_DEVICE_TREE_FILE, NULL);
}

static bool get_vic(Object *obj, Error **errp)
{
    Nios2MachineState *nms = NIOS2_MACHINE(obj);
    return nms->vic;
}

static void set_vic(Object *obj, bool value, Error **errp)
{
    Nios2MachineState *nms = NIOS2_MACHINE(obj);
    nms->vic = value;
}

static void nios2_10m50_ghrd_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Altera 10M50 GHRD Nios II design";
    mc->init = nios2_10m50_ghrd_init;
    mc->is_default = true;
    mc->deprecation_reason = "Nios II architecture is deprecated";

    object_class_property_add_bool(oc, "vic", get_vic, set_vic);
    object_class_property_set_description(oc, "vic",
        "Set on/off to enable/disable the Vectored Interrupt Controller");
}

static const TypeInfo nios2_10m50_ghrd_type_info = {
    .name          = TYPE_NIOS2_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(Nios2MachineState),
    .class_init    = nios2_10m50_ghrd_class_init,
};

static void nios2_10m50_ghrd_type_init(void)
{
    type_register_static(&nios2_10m50_ghrd_type_info);
}
type_init(nios2_10m50_ghrd_type_init);
