/*
 * Generic device-tree-driven paravirt PPC e500 platform
 *
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "e500.h"
#include "hw/net/fsl_etsec/etsec.h"
#include "system/device_tree.h"
#include "system/kvm.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/ppc/openpic.h"
#include "kvm_ppc.h"

static void e500plat_fixup_devtree(void *fdt)
{
    const char model[] = "QEMU ppce500";
    const char compatible[] = "fsl,qemu-e500";

    qemu_fdt_setprop(fdt, "/", "model", model, sizeof(model));
    qemu_fdt_setprop(fdt, "/", "compatible", compatible,
                     sizeof(compatible));
}

static void e500plat_init(MachineState *machine)
{
    PPCE500MachineClass *pmc = PPCE500_MACHINE_GET_CLASS(machine);
    /* Older KVM versions don't support EPR which breaks guests when we announce
       MPIC variants that support EPR. Revert to an older one for those */
    if (kvm_enabled() && !kvmppc_has_cap_epr()) {
        pmc->mpic_version = OPENPIC_MODEL_FSL_MPIC_20;
    }

    ppce500_init(machine);
}

static void e500plat_machine_device_plug_cb(HotplugHandler *hotplug_dev,
                                            DeviceState *dev, Error **errp)
{
    PPCE500MachineState *pms = PPCE500_MACHINE(hotplug_dev);
    MachineClass *mc = MACHINE_GET_CLASS(pms);

    if (device_is_dynamic_sysbus(mc, dev)) {
        platform_bus_link_device(pms->pbus_dev, SYS_BUS_DEVICE(dev));
    }
}

static
HotplugHandler *e500plat_machine_get_hotpug_handler(MachineState *machine,
                                                    DeviceState *dev)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (device_is_dynamic_sysbus(mc, dev)) {
        return HOTPLUG_HANDLER(machine);
    }

    return NULL;
}

#define TYPE_E500PLAT_MACHINE  MACHINE_TYPE_NAME("ppce500")

static void e500plat_machine_class_init(ObjectClass *oc, void *data)
{
    PPCE500MachineClass *pmc = PPCE500_MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    assert(!mc->get_hotplug_handler);
    mc->get_hotplug_handler = e500plat_machine_get_hotpug_handler;
    hc->plug = e500plat_machine_device_plug_cb;

    pmc->pci_first_slot = 0x1;
    pmc->pci_nr_slots = PCI_SLOT_MAX - 1;
    pmc->fixup_devtree = e500plat_fixup_devtree;
    pmc->mpic_version = OPENPIC_MODEL_FSL_MPIC_42;
    pmc->has_mpc8xxx_gpio = true;
    pmc->has_esdhc = true;
    pmc->platform_bus_base = 0xf00000000ULL;
    pmc->platform_bus_size = 128 * MiB;
    pmc->platform_bus_first_irq = 5;
    pmc->platform_bus_num_irqs = 10;
    pmc->ccsrbar_base = 0xFE0000000ULL;
    pmc->pci_pio_base = 0xFE1000000ULL;
    pmc->pci_mmio_base = 0xC00000000ULL;
    pmc->pci_mmio_bus_base = 0xE0000000ULL;
    pmc->spin_base = 0xFEF000000ULL;

    mc->desc = "generic paravirt e500 platform";
    mc->init = e500plat_init;
    mc->max_cpus = 32;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("e500v2_v30");
    mc->default_ram_id = "mpc8544ds.ram";
    mc->default_nic = "virtio-net-pci";
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_ETSEC_COMMON);
 }

static const TypeInfo e500plat_info = {
    .name          = TYPE_E500PLAT_MACHINE,
    .parent        = TYPE_PPCE500_MACHINE,
    .class_init    = e500plat_machine_class_init,
    .interfaces    = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
    }
};

static void e500plat_register_types(void)
{
    type_register_static(&e500plat_info);
}
type_init(e500plat_register_types)
