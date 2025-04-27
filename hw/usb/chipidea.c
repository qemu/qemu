/*
 * Copyright (c) 2018, Impinj, Inc.
 *
 * Chipidea USB block emulation code
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/usb/chipidea.h"
#include "qemu/module.h"

enum {
    CHIPIDEA_USBx_DCIVERSION   = 0x000,
    CHIPIDEA_USBx_DCCPARAMS    = 0x004,
    CHIPIDEA_USBx_DCCPARAMS_HC = BIT(8),
};

static uint64_t chipidea_read(void *opaque, hwaddr offset,
                               unsigned size)
{
    return 0;
}

static void chipidea_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
}

static const struct MemoryRegionOps chipidea_ops = {
    .read = chipidea_read,
    .write = chipidea_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the
         * real device but in practice there is no reason for a guest
         * to access this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static uint64_t chipidea_dc_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    switch (offset) {
    case CHIPIDEA_USBx_DCIVERSION:
        return 0x1;
    case CHIPIDEA_USBx_DCCPARAMS:
        /*
         * Real hardware (at least i.MX7) will also report the
         * controller as "Device Capable" (and 8 supported endpoints),
         * but there doesn't seem to be much point in doing so, since
         * we don't emulate that part.
         */
        return CHIPIDEA_USBx_DCCPARAMS_HC;
    }

    return 0;
}

static void chipidea_dc_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
}

static const struct MemoryRegionOps chipidea_dc_ops = {
    .read = chipidea_dc_read,
    .write = chipidea_dc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void chipidea_init(Object *obj)
{
    EHCIState *ehci = &SYS_BUS_EHCI(obj)->ehci;
    ChipideaState *ci = CHIPIDEA(obj);
    int i;

    for (i = 0; i < ARRAY_SIZE(ci->iomem); i++) {
        const struct {
            const char *name;
            hwaddr offset;
            uint64_t size;
            const struct MemoryRegionOps *ops;
        } regions[ARRAY_SIZE(ci->iomem)] = {
            /*
             * Registers located between offsets 0x000 and 0xFC
             */
            {
                .name   = TYPE_CHIPIDEA ".misc",
                .offset = 0x000,
                .size   = 0x100,
                .ops    = &chipidea_ops,
            },
            /*
             * Registers located between offsets 0x1A4 and 0x1DC
             */
            {
                .name   = TYPE_CHIPIDEA ".endpoints",
                .offset = 0x1A4,
                .size   = 0x1DC - 0x1A4 + 4,
                .ops    = &chipidea_ops,
            },
            /*
             * USB_x_DCIVERSION and USB_x_DCCPARAMS
             */
            {
                .name   = TYPE_CHIPIDEA ".dc",
                .offset = 0x120,
                .size   = 8,
                .ops    = &chipidea_dc_ops,
            },
        };

        memory_region_init_io(&ci->iomem[i],
                              obj,
                              regions[i].ops,
                              ci,
                              regions[i].name,
                              regions[i].size);

        memory_region_add_subregion(&ehci->mem,
                                    regions[i].offset,
                                    &ci->iomem[i]);
    }
}

static void chipidea_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(klass);

    /*
     * Offsets used were taken from i.MX7Dual Applications Processor
     * Reference Manual, Rev 0.1, p. 3177, Table 11-59
     */
    sec->capsbase   = 0x100;
    sec->opregbase  = 0x140;
    sec->portnr     = 1;

    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    dc->desc = "Chipidea USB Module";
}

static const TypeInfo chipidea_info = {
    .name          = TYPE_CHIPIDEA,
    .parent        = TYPE_SYS_BUS_EHCI,
    .instance_size = sizeof(ChipideaState),
    .instance_init = chipidea_init,
    .class_init    = chipidea_class_init,
};

static void chipidea_register_type(void)
{
    type_register_static(&chipidea_info);
}
type_init(chipidea_register_type)
