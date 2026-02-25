/*
 * Nitro Enclaves (accel) machine
 *
 * Copyright © 2026 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors:
 *   Alexander Graf <graf@amazon.com>
 *
 * Nitro Enclaves machine model for -accel nitro. This machine behaves
 * like the nitro-enclave machine, but uses the real Nitro Enclaves
 * backend to launch the virtual machine. It requires use of the -accel
 * nitro.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "chardev/char.h"
#include "hw/core/boards.h"
#include "hw/core/cpu.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/nitro/heartbeat.h"
#include "hw/nitro/machine.h"
#include "hw/nitro/nitro-vsock-bus.h"
#include "hw/nitro/serial-vsock.h"
#include "system/address-spaces.h"
#include "system/hostmem.h"
#include "system/system.h"
#include "system/nitro-accel.h"
#include "qemu/accel.h"
#include "hw/arm/machines-qom.h"

#define EIF_LOAD_ADDR   (8 * 1024 * 1024)

static void nitro_machine_init(MachineState *machine)
{
    const char *eif_path = machine->kernel_filename;
    const char *cpu_type = machine->cpu_type;
    g_autofree char *eif_data = NULL;
    gsize eif_size;

    if (!nitro_enabled()) {
        error_report("The 'nitro' machine requires -accel nitro");
        exit(1);
    }

    if (!cpu_type) {
        ObjectClass *oc = cpu_class_by_name(target_cpu_type(), "host");

        if (!oc) {
            error_report("nitro: no 'host' CPU available");
            exit(1);
        }
        cpu_type = object_class_get_name(oc);
    }

    if (!eif_path) {
        error_report("nitro: -kernel <eif-file> is required");
        exit(1);
    }

    /* Expose memory as normal QEMU RAM. Needs to be huge page backed. */
    memory_region_add_subregion(get_system_memory(), 0, machine->ram);

    /*
     * Load EIF (-kernel) as raw blob at the EIF_LOAD_ADDR into guest RAM.
     * The Nitro Hypervisor will extract its contents and bootstrap the
     * Enclave from it.
     */
    if (!g_file_get_contents(eif_path, &eif_data, &eif_size, NULL)) {
        error_report("nitro: failed to read EIF '%s'", eif_path);
        exit(1);
    }
    address_space_write(&address_space_memory, EIF_LOAD_ADDR,
                        MEMTXATTRS_UNSPECIFIED, eif_data, eif_size);

    if (defaults_enabled()) {
        NitroVsockBridge *bridge = nitro_vsock_bridge_create();

        /* Nitro Enclaves require a heartbeat device. Provide one. */
        qdev_realize(qdev_new(TYPE_NITRO_HEARTBEAT),
                     BUS(&bridge->bus), &error_fatal);

        /*
         * In debug mode, Nitro Enclaves expose the guest's serial output via
         * vsock. When the accel is in debug mode, wire the vsock serial to
         * the machine's serial port so that -nographic automatically works
         */
        if (object_property_get_bool(OBJECT(current_accel()), "debug-mode", NULL)) {
            Chardev *chr = serial_hd(0);

            if (chr) {
                DeviceState *dev = qdev_new(TYPE_NITRO_SERIAL_VSOCK);

                qdev_prop_set_chr(dev, "chardev", chr);
                qdev_realize(dev, BUS(&bridge->bus), &error_fatal);
            }
        }
    }
}

static bool nitro_create_memfd_backend(MachineState *ms, const char *path,
                                       Error **errp)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    Object *root = object_get_objects_root();
    Object *obj;
    bool r = false;

    obj = object_new(TYPE_MEMORY_BACKEND_MEMFD);

    /* Nitro Enclaves require huge page backing */
    if (!object_property_set_int(obj, "size", ms->ram_size, errp) ||
        !object_property_set_bool(obj, "hugetlb", true, errp)) {
        goto out;
    }

    object_property_add_child(root, mc->default_ram_id, obj);

    if (!user_creatable_complete(USER_CREATABLE(obj), errp)) {
        goto out;
    }
    r = object_property_set_link(OBJECT(ms), "memory-backend", obj, errp);

out:
    object_unref(obj);
    return r;
}

static void nitro_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Nitro Enclave";
    mc->init = nitro_machine_init;
    mc->create_default_memdev = nitro_create_memfd_backend;
    mc->default_ram_id = "ram";
    mc->max_cpus = 4096;
}

static const TypeInfo nitro_machine_info = {
    .name = TYPE_NITRO_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(NitroMachineState),
    .class_init = nitro_machine_class_init,
    .interfaces = (const InterfaceInfo[]) {
        /* x86_64 and aarch64 only */
        { TYPE_TARGET_AARCH64_MACHINE },
        { }
    },
};

static void nitro_machine_register(void)
{
    type_register_static(&nitro_machine_info);
}

type_init(nitro_machine_register);
