/*
 * Guest Loader
 *
 * Copyright (C) 2020 Linaro
 * Written by Alex Bennée <alex.bennee@linaro.org>
 * (based on the generic-loader by Li Guang <lig.fnst@cn.fujitsu.com>)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Much like the generic-loader this is treated as a special device
 * inside QEMU. However unlike the generic-loader this device is used
 * to load guest images for hypervisors. As part of that process the
 * hypervisor needs to have platform information passed to it by the
 * lower levels of the stack (e.g. firmware/bootloader). If you boot
 * the hypervisor directly you use the guest-loader to load the Dom0
 * or equivalent guest images in the right place in the same way a
 * boot loader would.
 *
 * This is only relevant for full system emulation.
 */

#include "qemu/osdep.h"
#include "hw/core/cpu.h"
#include "sysemu/dma.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "guest-loader.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"

/*
 * Insert some FDT nodes for the loaded blob.
 */
static void loader_insert_platform_data(GuestLoaderState *s, int size,
                                        Error **errp)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    void *fdt = machine->fdt;
    g_autofree char *node = g_strdup_printf("/chosen/module@0x%08" PRIx64,
                                            s->addr);
    uint64_t reg_attr[2] = {cpu_to_be64(s->addr), cpu_to_be64(size)};

    if (!fdt) {
        error_setg(errp, "Cannot modify FDT fields if the machine has none");
        return;
    }

    qemu_fdt_add_subnode(fdt, node);
    qemu_fdt_setprop(fdt, node, "reg", &reg_attr, sizeof(reg_attr));

    if (s->kernel) {
        const char *compat[2] = { "multiboot,module", "multiboot,kernel" };
        if (qemu_fdt_setprop_string_array(fdt, node, "compatible",
                                          (char **) &compat,
                                          ARRAY_SIZE(compat)) < 0) {
            error_setg(errp, "couldn't set %s/compatible", node);
            return;
        }
        if (s->args) {
            if (qemu_fdt_setprop_string(fdt, node, "bootargs", s->args) < 0) {
                error_setg(errp, "couldn't set %s/bootargs", node);
            }
        }
    } else if (s->initrd) {
        const char *compat[2] = { "multiboot,module", "multiboot,ramdisk" };
        if (qemu_fdt_setprop_string_array(fdt, node, "compatible",
                                          (char **) &compat,
                                          ARRAY_SIZE(compat)) < 0) {
            error_setg(errp, "couldn't set %s/compatible", node);
            return;
        }
    }
}

static void guest_loader_realize(DeviceState *dev, Error **errp)
{
    GuestLoaderState *s = GUEST_LOADER(dev);
    char *file = s->kernel ? s->kernel : s->initrd;
    int size = 0;

    /* Perform some error checking on the user's options */
    if (s->kernel && s->initrd) {
        error_setg(errp, "Cannot specify a kernel and initrd in same stanza");
        return;
    } else if (!s->kernel && !s->initrd)  {
        error_setg(errp, "Need to specify a kernel or initrd image");
        return;
    } else if (!s->addr) {
        error_setg(errp, "Need to specify the address of guest blob");
        return;
    } else if (s->args && !s->kernel) {
        error_setg(errp, "Boot args only relevant to kernel blobs");
    }

    /* Default to the maximum size being the machine's ram size */
    size = load_image_targphys_as(file, s->addr, current_machine->ram_size,
                                  NULL);
    if (size < 0) {
        error_setg(errp, "Cannot load specified image %s", file);
        return;
    }

    /* Now the image is loaded we need to update the platform data */
    loader_insert_platform_data(s, size, errp);
}

static Property guest_loader_props[] = {
    DEFINE_PROP_UINT64("addr", GuestLoaderState, addr, 0),
    DEFINE_PROP_STRING("kernel", GuestLoaderState, kernel),
    DEFINE_PROP_STRING("bootargs", GuestLoaderState, args),
    DEFINE_PROP_STRING("initrd", GuestLoaderState, initrd),
    DEFINE_PROP_END_OF_LIST(),
};

static void guest_loader_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = guest_loader_realize;
    device_class_set_props(dc, guest_loader_props);
    dc->desc = "Guest Loader";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo guest_loader_info = {
    .name = TYPE_GUEST_LOADER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(GuestLoaderState),
    .class_init = guest_loader_class_init,
};

static void guest_loader_register_type(void)
{
    type_register_static(&guest_loader_info);
}

type_init(guest_loader_register_type)
