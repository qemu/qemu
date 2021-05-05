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
#include "qemu-common.h"

#include "hw/remote/machine.h"
#include "exec/memory.h"
#include "qapi/error.h"
#include "hw/pci/pci_host.h"
#include "hw/remote/iohub.h"

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

    remote_iohub_init(&s->iohub);

    pci_bus_irqs(pci_host->bus, remote_iohub_set_irq, remote_iohub_map_irq,
                 &s->iohub, REMOTE_IOHUB_NB_PIRQS);
}

static void remote_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = remote_machine_init;
    mc->desc = "Experimental remote machine";
}

static const TypeInfo remote_machine = {
    .name = TYPE_REMOTE_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(RemoteMachineState),
    .class_init = remote_machine_class_init,
};

static void remote_machine_register_types(void)
{
    type_register_static(&remote_machine);
}

type_init(remote_machine_register_types);
