/*
 * Machine for remote device
 *
 *  This machine type is used by the remote device process in multi-process
 *  QEMU. QEMU device models depend on parent busses, interrupt controllers,
 *  memory regions, etc. The remote machine type offers this environment so
 *  that QEMU device models can be used as remote devices.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "hw/remote/machine.h"
#include "exec/memory.h"
#include "qapi/error.h"
#include "hw/pci/pci_host.h"
#include "hw/remote/iohub.h"
#include "hw/remote/iommu.h"
#include "hw/qdev-core.h"
#include "hw/remote/vfio-user-obj.h"
#include "hw/pci/msi.h"

static void remote_machine_init(MachineState *machine)
{
    MemoryRegion *system_memory, *system_io, *pci_memory;
    RemoteMachineState *s = REMOTE_MACHINE(machine);
    RemotePCIHost *rem_host;
    PCIHostState *pci_host;

    system_memory = get_system_memory();
    system_io = get_system_io();

    pci_memory = g_new(MemoryRegion, 1);
    memory_region_init(pci_memory, NULL, "pci", UINT64_MAX);

    rem_host = REMOTE_PCIHOST(qdev_new(TYPE_REMOTE_PCIHOST));

    rem_host->mr_pci_mem = pci_memory;
    rem_host->mr_sys_mem = system_memory;
    rem_host->mr_sys_io = system_io;

    s->host = rem_host;

    object_property_add_child(OBJECT(s), "remote-pcihost", OBJECT(rem_host));
    memory_region_add_subregion_overlap(system_memory, 0x0, pci_memory, -1);

    qdev_realize(DEVICE(rem_host), sysbus_get_default(), &error_fatal);

    pci_host = PCI_HOST_BRIDGE(rem_host);

    if (s->vfio_user) {
        remote_iommu_setup(pci_host->bus);

        msi_nonbroken = true;

        vfu_object_set_bus_irq(pci_host->bus);
    } else {
        remote_iohub_init(&s->iohub);

        pci_bus_irqs(pci_host->bus, remote_iohub_set_irq,
                     &s->iohub, REMOTE_IOHUB_NB_PIRQS);
        pci_bus_map_irqs(pci_host->bus, remote_iohub_map_irq);
    }

    qbus_set_hotplug_handler(BUS(pci_host->bus), OBJECT(s));
}

static bool remote_machine_get_vfio_user(Object *obj, Error **errp)
{
    RemoteMachineState *s = REMOTE_MACHINE(obj);

    return s->vfio_user;
}

static void remote_machine_set_vfio_user(Object *obj, bool value, Error **errp)
{
    RemoteMachineState *s = REMOTE_MACHINE(obj);

    if (phase_check(PHASE_MACHINE_CREATED)) {
        error_setg(errp, "Error enabling vfio-user - machine already created");
        return;
    }

    s->vfio_user = value;
}

static bool remote_machine_get_auto_shutdown(Object *obj, Error **errp)
{
    RemoteMachineState *s = REMOTE_MACHINE(obj);

    return s->auto_shutdown;
}

static void remote_machine_set_auto_shutdown(Object *obj, bool value,
                                             Error **errp)
{
    RemoteMachineState *s = REMOTE_MACHINE(obj);

    s->auto_shutdown = value;
}

static void remote_machine_instance_init(Object *obj)
{
    RemoteMachineState *s = REMOTE_MACHINE(obj);

    s->auto_shutdown = true;
}

static void remote_machine_dev_unplug_cb(HotplugHandler *hotplug_dev,
                                         DeviceState *dev, Error **errp)
{
    qdev_unrealize(dev);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        remote_iommu_unplug_dev(PCI_DEVICE(dev));
    }
}

static void remote_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->init = remote_machine_init;
    mc->desc = "Experimental remote machine";

    hc->unplug = remote_machine_dev_unplug_cb;

    object_class_property_add_bool(oc, "vfio-user",
                                   remote_machine_get_vfio_user,
                                   remote_machine_set_vfio_user);

    object_class_property_add_bool(oc, "auto-shutdown",
                                   remote_machine_get_auto_shutdown,
                                   remote_machine_set_auto_shutdown);
}

static const TypeInfo remote_machine = {
    .name = TYPE_REMOTE_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(RemoteMachineState),
    .instance_init = remote_machine_instance_init,
    .class_init = remote_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void remote_machine_register_types(void)
{
    type_register_static(&remote_machine);
}

type_init(remote_machine_register_types);
