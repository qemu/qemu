/*
 * ARM Platform Bus device tree generation helpers
 *
 * Copyright (c) 2014 Linaro Limited
 *
 * Authors:
 *  Alex Graf <agraf@suse.de>
 *  Eric Auger <eric.auger@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include <libfdt.h>
#ifdef CONFIG_LINUX
#include <linux/vfio.h>
#endif
#include "hw/core/sysbus-fdt.h"
#include "qemu/error-report.h"
#include "system/device_tree.h"
#include "system/tpm.h"
#include "hw/arm/smmuv3.h"
#include "hw/platform-bus.h"
#include "hw/display/ramfb.h"
#include "hw/uefi/var-service-api.h"
#include "hw/arm/fdt.h"

/*
 * internal struct that contains the information to create dynamic
 * sysbus device node
 */
typedef struct PlatformBusFDTData {
    void *fdt; /* device tree handle */
    int irq_start; /* index of the first IRQ usable by platform bus devices */
    const char *pbus_node_name; /* name of the platform bus node */
    PlatformBusDevice *pbus;
} PlatformBusFDTData;

/* struct that allows to match a device and create its FDT node */
typedef struct BindingEntry {
    const char *typename;
    const char *compat;
    int  (*add_fn)(SysBusDevice *sbdev, void *opaque);
    bool (*match_fn)(SysBusDevice *sbdev, const struct BindingEntry *combo);
} BindingEntry;

/* helpers */

typedef struct HostProperty {
    const char *name;
    bool optional;
} HostProperty;

#ifdef CONFIG_TPM
/*
 * add_tpm_tis_fdt_node: Create a DT node for TPM TIS
 *
 * See kernel documentation:
 * Documentation/devicetree/bindings/security/tpm/tpm_tis_mmio.txt
 * Optional interrupt for command completion is not exposed
 */
static int add_tpm_tis_fdt_node(SysBusDevice *sbdev, void *opaque)
{
    PlatformBusFDTData *data = opaque;
    PlatformBusDevice *pbus = data->pbus;
    void *fdt = data->fdt;
    const char *parent_node = data->pbus_node_name;
    char *nodename;
    uint32_t reg_attr[2];
    uint64_t mmio_base;

    mmio_base = platform_bus_get_mmio_addr(pbus, sbdev, 0);
    nodename = g_strdup_printf("%s/tpm_tis@%" PRIx64, parent_node, mmio_base);
    qemu_fdt_add_subnode(fdt, nodename);

    qemu_fdt_setprop_string(fdt, nodename, "compatible", "tcg,tpm-tis-mmio");

    reg_attr[0] = cpu_to_be32(mmio_base);
    reg_attr[1] = cpu_to_be32(0x5000);
    qemu_fdt_setprop(fdt, nodename, "reg", reg_attr, 2 * sizeof(uint32_t));

    g_free(nodename);
    return 0;
}
#endif

static int add_uefi_vars_node(SysBusDevice *sbdev, void *opaque)
{
    PlatformBusFDTData *data = opaque;
    PlatformBusDevice *pbus = data->pbus;
    const char *parent_node = data->pbus_node_name;
    void *fdt = data->fdt;
    uint64_t mmio_base;
    char *nodename;

    mmio_base = platform_bus_get_mmio_addr(pbus, sbdev, 0);
    nodename = g_strdup_printf("%s/%s@%" PRIx64, parent_node,
                               UEFI_VARS_FDT_NODE, mmio_base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename,
                            "compatible", UEFI_VARS_FDT_COMPAT);
    qemu_fdt_setprop_sized_cells(fdt, nodename, "reg",
                                 1, mmio_base,
                                 1, UEFI_VARS_REGS_SIZE);
    g_free(nodename);
    return 0;
}

static int no_fdt_node(SysBusDevice *sbdev, void *opaque)
{
    return 0;
}

/* Device type based matching */
static bool type_match(SysBusDevice *sbdev, const BindingEntry *entry)
{
    return !strcmp(object_get_typename(OBJECT(sbdev)), entry->typename);
}

#define TYPE_BINDING(type, add_fn) {(type), NULL, (add_fn), NULL}

/* list of supported dynamic sysbus bindings */
static const BindingEntry bindings[] = {
#ifdef CONFIG_TPM
    TYPE_BINDING(TYPE_TPM_TIS_SYSBUS, add_tpm_tis_fdt_node),
#endif
    /* No generic DT support for smmuv3 dev. Support added for arm virt only */
    TYPE_BINDING(TYPE_ARM_SMMUV3, no_fdt_node),
    TYPE_BINDING(TYPE_RAMFB_DEVICE, no_fdt_node),
    TYPE_BINDING(TYPE_UEFI_VARS_SYSBUS, add_uefi_vars_node),
    TYPE_BINDING("", NULL), /* last element */
};

/* Generic Code */

/**
 * add_fdt_node - add the device tree node of a dynamic sysbus device
 *
 * @sbdev: handle to the sysbus device
 * @opaque: handle to the PlatformBusFDTData
 *
 * Checks the sysbus type belongs to the list of device types that
 * are dynamically instantiable and if so call the node creation
 * function.
 */
static void add_fdt_node(SysBusDevice *sbdev, void *opaque)
{
    int i, ret;

    for (i = 0; i < ARRAY_SIZE(bindings); i++) {
        const BindingEntry *iter = &bindings[i];

        if (type_match(sbdev, iter)) {
            if (!iter->match_fn || iter->match_fn(sbdev, iter)) {
                ret = iter->add_fn(sbdev, opaque);
                assert(!ret);
                return;
            }
        }
    }
    error_report("Device %s can not be dynamically instantiated",
                     qdev_fw_name(DEVICE(sbdev)));
    exit(1);
}

void platform_bus_add_all_fdt_nodes(void *fdt, const char *intc, hwaddr addr,
                                    hwaddr bus_size, int irq_start)
{
    const char platcomp[] = "qemu,platform\0simple-bus";
    PlatformBusDevice *pbus;
    DeviceState *dev;
    gchar *node;

    assert(fdt);

    node = g_strdup_printf("/platform-bus@%"PRIx64, addr);

    /* Create a /platform node that we can put all devices into */
    qemu_fdt_add_subnode(fdt, node);
    qemu_fdt_setprop(fdt, node, "compatible", platcomp, sizeof(platcomp));

    /* Our platform bus region is less than 32bits, so 1 cell is enough for
     * address and size
     */
    qemu_fdt_setprop_cells(fdt, node, "#size-cells", 1);
    qemu_fdt_setprop_cells(fdt, node, "#address-cells", 1);
    qemu_fdt_setprop_cells(fdt, node, "ranges", 0, addr >> 32, addr, bus_size);

    qemu_fdt_setprop_phandle(fdt, node, "interrupt-parent", intc);

    dev = qdev_find_recursive(sysbus_get_default(), TYPE_PLATFORM_BUS_DEVICE);
    pbus = PLATFORM_BUS_DEVICE(dev);

    PlatformBusFDTData data = {
        .fdt = fdt,
        .irq_start = irq_start,
        .pbus_node_name = node,
        .pbus = pbus,
    };

    /* Loop through all dynamic sysbus devices and create their node */
    foreach_dynamic_sysbus_device(add_fdt_node, &data);

    g_free(node);
}
