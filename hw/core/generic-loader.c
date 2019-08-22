/*
 * Generic Loader
 *
 * Copyright (C) 2014 Li Guang
 * Copyright (C) 2016 Xilinx Inc.
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 */

/*
 * Internally inside QEMU this is a device. It is a strange device that
 * provides no hardware interface but allows QEMU to monkey patch memory
 * specified when it is created. To be able to do this it has a reset
 * callback that does the memory operations.

 * This device allows the user to monkey patch memory. To be able to do
 * this it needs a backend to manage the datas, the same as other
 * memory-related devices. In this case as the backend is so trivial we
 * have merged it with the frontend instead of creating and maintaining a
 * separate backend.
 */

#include "qemu/osdep.h"
#include "hw/core/cpu.h"
#include "hw/sysbus.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/core/generic-loader.h"

#define CPU_NONE 0xFFFFFFFF

static void generic_loader_reset(void *opaque)
{
    GenericLoaderState *s = GENERIC_LOADER(opaque);

    if (s->set_pc) {
        CPUClass *cc = CPU_GET_CLASS(s->cpu);
        cpu_reset(s->cpu);
        if (cc) {
            cc->set_pc(s->cpu, s->addr);
        }
    }

    if (s->data_len) {
        assert(s->data_len < sizeof(s->data));
        dma_memory_write(s->cpu->as, s->addr, &s->data, s->data_len);
    }
}

static void generic_loader_realize(DeviceState *dev, Error **errp)
{
    GenericLoaderState *s = GENERIC_LOADER(dev);
    hwaddr entry;
    int big_endian;
    int size = 0;

    s->set_pc = false;

    /* Perform some error checking on the user's options */
    if (s->data || s->data_len  || s->data_be) {
        /* User is loading memory values */
        if (s->file) {
            error_setg(errp, "Specifying a file is not supported when loading "
                       "memory values");
            return;
        } else if (s->force_raw) {
            error_setg(errp, "Specifying force-raw is not supported when "
                       "loading memory values");
            return;
        } else if (!s->data_len) {
            /* We can't check for !data here as a value of 0 is still valid. */
            error_setg(errp, "Both data and data-len must be specified");
            return;
        } else if (s->data_len > 8) {
            error_setg(errp, "data-len cannot be greater then 8 bytes");
            return;
        }
    } else if (s->file || s->force_raw)  {
        /* User is loading an image */
        if (s->data || s->data_len || s->data_be) {
            error_setg(errp, "data can not be specified when loading an "
                       "image");
            return;
        }
        /* The user specified a file, only set the PC if they also specified
         * a CPU to use.
         */
        if (s->cpu_num != CPU_NONE) {
            s->set_pc = true;
        }
    } else if (s->addr) {
        /* User is setting the PC */
        if (s->data || s->data_len || s->data_be) {
            error_setg(errp, "data can not be specified when setting a "
                       "program counter");
            return;
        } else if (s->cpu_num == CPU_NONE) {
            error_setg(errp, "cpu_num must be specified when setting a "
                       "program counter");
            return;
        }
        s->set_pc = true;
    } else {
        /* Did the user specify anything? */
        error_setg(errp, "please include valid arguments");
        return;
    }

    qemu_register_reset(generic_loader_reset, dev);

    if (s->cpu_num != CPU_NONE) {
        s->cpu = qemu_get_cpu(s->cpu_num);
        if (!s->cpu) {
            error_setg(errp, "Specified boot CPU#%d is nonexistent",
                       s->cpu_num);
            return;
        }
    } else {
        s->cpu = first_cpu;
    }

    big_endian = target_words_bigendian();

    if (s->file) {
        AddressSpace *as = s->cpu ? s->cpu->as :  NULL;

        if (!s->force_raw) {
            size = load_elf_as(s->file, NULL, NULL, NULL, &entry, NULL, NULL,
                               big_endian, 0, 0, 0, as);

            if (size < 0) {
                size = load_uimage_as(s->file, &entry, NULL, NULL, NULL, NULL,
                                      as);
            }

            if (size < 0) {
                size = load_targphys_hex_as(s->file, &entry, as);
            }
        }

        if (size < 0 || s->force_raw) {
            /* Default to the maximum size being the machine's ram size */
            size = load_image_targphys_as(s->file, s->addr, ram_size, as);
        } else {
            s->addr = entry;
        }

        if (size < 0) {
            error_setg(errp, "Cannot load specified image %s", s->file);
            return;
        }
    }

    /* Convert the data endiannes */
    if (s->data_be) {
        s->data = cpu_to_be64(s->data);
    } else {
        s->data = cpu_to_le64(s->data);
    }
}

static void generic_loader_unrealize(DeviceState *dev, Error **errp)
{
    qemu_unregister_reset(generic_loader_reset, dev);
}

static Property generic_loader_props[] = {
    DEFINE_PROP_UINT64("addr", GenericLoaderState, addr, 0),
    DEFINE_PROP_UINT64("data", GenericLoaderState, data, 0),
    DEFINE_PROP_UINT8("data-len", GenericLoaderState, data_len, 0),
    DEFINE_PROP_BOOL("data-be", GenericLoaderState, data_be, false),
    DEFINE_PROP_UINT32("cpu-num", GenericLoaderState, cpu_num, CPU_NONE),
    DEFINE_PROP_BOOL("force-raw", GenericLoaderState, force_raw, false),
    DEFINE_PROP_STRING("file", GenericLoaderState, file),
    DEFINE_PROP_END_OF_LIST(),
};

static void generic_loader_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* The reset function is not registered here and is instead registered in
     * the realize function to allow this device to be added via the device_add
     * command in the QEMU monitor.
     * TODO: Improve the device_add functionality to allow resets to be
     * connected
     */
    dc->realize = generic_loader_realize;
    dc->unrealize = generic_loader_unrealize;
    dc->props = generic_loader_props;
    dc->desc = "Generic Loader";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static TypeInfo generic_loader_info = {
    .name = TYPE_GENERIC_LOADER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(GenericLoaderState),
    .class_init = generic_loader_class_init,
};

static void generic_loader_register_type(void)
{
    type_register_static(&generic_loader_info);
}

type_init(generic_loader_register_type)
