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
#include "hw/core/eif.h"
#include <zlib.h> /* for crc32 */

#define EIF_LOAD_ADDR   (8 * 1024 * 1024)

static bool is_eif(char *eif, gsize len)
{
    const char eif_magic[] = EIF_MAGIC;

    return len >= sizeof(eif_magic) &&
           !memcmp(eif, eif_magic, sizeof(eif_magic));
}

static void build_eif_section(EifHeader *hdr, GByteArray *buf, uint16_t type,
                              const char *data, uint64_t size)
{
    uint16_t section = be16_to_cpu(hdr->section_cnt);
    EifSectionHeader shdr = {
        .section_type = cpu_to_be16(type),
        .flags = 0,
        .section_size = cpu_to_be64(size),
    };

    hdr->section_offsets[section] = cpu_to_be64(buf->len);
    hdr->section_sizes[section] = cpu_to_be64(size);

    g_byte_array_append(buf, (const uint8_t *)&shdr, sizeof(shdr));
    if (size) {
        g_byte_array_append(buf, (const uint8_t *)data, size);
    }

    hdr->section_cnt = cpu_to_be16(section + 1);
}

/*
 * Nitro Enclaves only support loading EIF files. When the user provides
 * a Linux kernel, initrd and cmdline, convert them into EIF format.
 */
static char *build_eif(const char *kernel_data, gsize kernel_size,
                       const char *initrd_path, const char *cmdline,
                       gsize *out_size, Error **errp)
{
    g_autofree char *initrd_data = NULL;
    static const char metadata[] = "{}";
    size_t metadata_len = sizeof(metadata) - 1;
    gsize initrd_size = 0;
    GByteArray *buf;
    EifHeader hdr;
    uint32_t crc = 0;
    size_t cmdline_len;

    if (initrd_path) {
        if (!g_file_get_contents(initrd_path, &initrd_data,
                                 &initrd_size, NULL)) {
            error_setg(errp, "Failed to read initrd '%s'", initrd_path);
            return NULL;
        }
    }

    buf = g_byte_array_new();

    cmdline_len = cmdline ? strlen(cmdline) : 0;

    hdr = (EifHeader) {
        .magic = EIF_MAGIC,
        .version = cpu_to_be16(4),
        .flags = cpu_to_be16(target_aarch64() ? EIF_HDR_ARCH_ARM64 : 0),
    };

    g_byte_array_append(buf, (const uint8_t *)&hdr, sizeof(hdr));

    /* Kernel */
    build_eif_section(&hdr, buf, EIF_SECTION_KERNEL, kernel_data, kernel_size);

    /* Command line */
    build_eif_section(&hdr, buf, EIF_SECTION_CMDLINE, cmdline, cmdline_len);

    /* Initramfs */
    build_eif_section(&hdr, buf, EIF_SECTION_RAMDISK, initrd_data, initrd_size);

    /* Metadata */
    build_eif_section(&hdr, buf, EIF_SECTION_METADATA, metadata, metadata_len);

    /*
     * Patch the header into the buffer first (with real section offsets
     * and sizes), then compute CRC over everything except the CRC field.
     */
    memcpy(buf->data, &hdr, sizeof(hdr));
    crc = crc32(crc, buf->data, offsetof(EifHeader, eif_crc32));
    crc = crc32(crc, &buf->data[sizeof(hdr)], buf->len - sizeof(hdr));

    /* Finally write the CRC into the in-buffer header */
    ((EifHeader *)buf->data)->eif_crc32 = cpu_to_be32(crc);

    *out_size = buf->len;
    return (char *)g_byte_array_free(buf, false);
}

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

    if (!is_eif(eif_data, eif_size)) {
        char *kernel_data = eif_data;
        gsize kernel_size = eif_size;
        Error *err = NULL;

        /*
         * The user gave us a non-EIF kernel, likely a Linux kernel image.
         * Assemble an EIF file from it, the -initrd and the -append arguments,
         * so that users can perform a natural direct kernel boot.
         */
        eif_data = build_eif(kernel_data, kernel_size, machine->initrd_filename,
                             machine->kernel_cmdline, &eif_size, &err);
        if (!eif_data) {
            error_report_err(err);
            exit(1);
        }

        g_free(kernel_data);
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
