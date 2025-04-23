/*
 * QEMU Apple ParavirtualizedGraphics.framework device, PCI variant
 *
 * Copyright © 2023-2024 Phil Dennis-Jordan
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ParavirtualizedGraphics.framework is a set of libraries that macOS provides
 * which implements 3d graphics passthrough to the host as well as a
 * proprietary guest communication channel to drive it. This device model
 * implements support to drive that library from within QEMU as a PCI device
 * aimed primarily at x86-64 macOS VMs.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "apple-gfx.h"
#include "trace.h"

#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>

OBJECT_DECLARE_SIMPLE_TYPE(AppleGFXPCIState, APPLE_GFX_PCI)

struct AppleGFXPCIState {
    PCIDevice parent_obj;

    AppleGFXState common;
};

static const char *apple_gfx_pci_option_rom_path = NULL;

static void apple_gfx_init_option_rom_path(void)
{
    NSURL *option_rom_url = PGCopyOptionROMURL();
    const char *option_rom_path = option_rom_url.fileSystemRepresentation;
    apple_gfx_pci_option_rom_path = g_strdup(option_rom_path);
    [option_rom_url release];
}

static void apple_gfx_pci_init(Object *obj)
{
    AppleGFXPCIState *s = APPLE_GFX_PCI(obj);

    if (!apple_gfx_pci_option_rom_path) {
        /*
         * The following is done on device not class init to avoid running
         * ObjC code before fork() in -daemonize mode.
         */
        PCIDeviceClass *pci = PCI_DEVICE_CLASS(object_get_class(obj));
        apple_gfx_init_option_rom_path();
        pci->romfile = apple_gfx_pci_option_rom_path;
    }

    apple_gfx_common_init(obj, &s->common, TYPE_APPLE_GFX_PCI);
}

typedef struct AppleGFXPCIInterruptJob {
    PCIDevice *device;
    uint32_t vector;
} AppleGFXPCIInterruptJob;

static void apple_gfx_pci_raise_interrupt(void *opaque)
{
    AppleGFXPCIInterruptJob *job = opaque;

    if (msi_enabled(job->device)) {
        msi_notify(job->device, job->vector);
    }
    g_free(job);
}

static void apple_gfx_pci_interrupt(PCIDevice *dev, uint32_t vector)
{
    AppleGFXPCIInterruptJob *job;

    trace_apple_gfx_raise_irq(vector);
    job = g_malloc0(sizeof(*job));
    job->device = dev;
    job->vector = vector;
    aio_bh_schedule_oneshot(qemu_get_aio_context(),
                            apple_gfx_pci_raise_interrupt, job);
}

static void apple_gfx_pci_realize(PCIDevice *dev, Error **errp)
{
    AppleGFXPCIState *s = APPLE_GFX_PCI(dev);
    int ret;

    pci_register_bar(dev, PG_PCI_BAR_MMIO,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->common.iomem_gfx);

    ret = msi_init(dev, 0x0 /* config offset; 0 = find space */,
                   PG_PCI_MAX_MSI_VECTORS, true /* msi64bit */,
                   false /* msi_per_vector_mask */, errp);
    if (ret != 0) {
        return;
    }

    @autoreleasepool {
        PGDeviceDescriptor *desc = [PGDeviceDescriptor new];
        desc.raiseInterrupt = ^(uint32_t vector) {
            apple_gfx_pci_interrupt(dev, vector);
        };

        apple_gfx_common_realize(&s->common, DEVICE(dev), desc, errp);
        [desc release];
        desc = nil;
    }
}

static void apple_gfx_pci_reset(Object *obj, ResetType type)
{
    AppleGFXPCIState *s = APPLE_GFX_PCI(obj);
    [s->common.pgdev reset];
}

static const Property apple_gfx_pci_properties[] = {
    DEFINE_PROP_ARRAY("display-modes", AppleGFXPCIState,
                      common.num_display_modes, common.display_modes,
                      qdev_prop_apple_gfx_display_mode, AppleGFXDisplayMode),
};

static void apple_gfx_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pci = PCI_DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = apple_gfx_pci_reset;
    dc->desc = "macOS Paravirtualized Graphics PCI Display Controller";
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);

    pci->vendor_id = PG_PCI_VENDOR_ID;
    pci->device_id = PG_PCI_DEVICE_ID;
    pci->class_id = PCI_CLASS_DISPLAY_OTHER;
    pci->realize = apple_gfx_pci_realize;

    device_class_set_props(dc, apple_gfx_pci_properties);
}

static const TypeInfo apple_gfx_pci_types[] = {
    {
        .name          = TYPE_APPLE_GFX_PCI,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(AppleGFXPCIState),
        .class_init    = apple_gfx_pci_class_init,
        .instance_init = apple_gfx_pci_init,
        .interfaces = (const InterfaceInfo[]) {
            { INTERFACE_PCIE_DEVICE },
            { },
        },
    }
};
DEFINE_TYPES(apple_gfx_pci_types)

