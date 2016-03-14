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
#include <libfdt.h>
#include "qemu-common.h"
#ifdef CONFIG_LINUX
#include <linux/vfio.h>
#endif
#include "hw/arm/sysbus-fdt.h"
#include "qemu/error-report.h"
#include "sysemu/device_tree.h"
#include "hw/platform-bus.h"
#include "sysemu/sysemu.h"
#include "hw/vfio/vfio-platform.h"
#include "hw/vfio/vfio-calxeda-xgmac.h"
#include "hw/vfio/vfio-amd-xgbe.h"
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

/*
 * struct used when calling the machine init done notifier
 * that constructs the fdt nodes of platform bus devices
 */
typedef struct PlatformBusFDTNotifierParams {
    Notifier notifier;
    ARMPlatformBusFDTParams *fdt_params;
} PlatformBusFDTNotifierParams;

/* struct that associates a device type name and a node creation function */
typedef struct NodeCreationPair {
    const char *typename;
    int (*add_fdt_node_fn)(SysBusDevice *sbdev, void *opaque);
} NodeCreationPair;

/* helpers */

typedef struct HostProperty {
    const char *name;
    bool optional;
} HostProperty;

#ifdef CONFIG_LINUX

/**
 * copy_properties_from_host
 *
 * copies properties listed in an array from host device tree to
 * guest device tree. If a non optional property is not found, the
 * function asserts. An optional property is ignored if not found
 * in the host device tree.
 * @props: array of HostProperty to copy
 * @nb_props: number of properties in the array
 * @host_dt: host device tree blob
 * @guest_dt: guest device tree blob
 * @node_path: host dt node path where the property is supposed to be
              found
 * @nodename: guest node name the properties should be added to
 */
static void copy_properties_from_host(HostProperty *props, int nb_props,
                                      void *host_fdt, void *guest_fdt,
                                      char *node_path, char *nodename)
{
    int i, prop_len;
    const void *r;
    Error *err = NULL;

    for (i = 0; i < nb_props; i++) {
        r = qemu_fdt_getprop(host_fdt, node_path,
                             props[i].name,
                             &prop_len,
                             props[i].optional ? &err : &error_fatal);
        if (r) {
            qemu_fdt_setprop(guest_fdt, nodename,
                             props[i].name, r, prop_len);
        } else {
            if (prop_len != -FDT_ERR_NOTFOUND) {
                /* optional property not returned although property exists */
                error_report_err(err);
            } else {
                error_free(err);
            }
        }
    }
}

/* clock properties whose values are copied/pasted from host */
static HostProperty clock_copied_properties[] = {
    {"compatible", false},
    {"#clock-cells", false},
    {"clock-frequency", true},
    {"clock-output-names", true},
};

/**
 * fdt_build_clock_node
 *
 * Build a guest clock node, used as a dependency from a passthrough'ed
 * device. Most information are retrieved from the host clock node.
 * Also check the host clock is a fixed one.
 *
 * @host_fdt: host device tree blob from which info are retrieved
 * @guest_fdt: guest device tree blob where the clock node is added
 * @host_phandle: phandle of the clock in host device tree
 * @guest_phandle: phandle to assign to the guest node
 */
static void fdt_build_clock_node(void *host_fdt, void *guest_fdt,
                                uint32_t host_phandle,
                                uint32_t guest_phandle)
{
    char *node_path = NULL;
    char *nodename;
    const void *r;
    int ret, node_offset, prop_len, path_len = 16;

    node_offset = fdt_node_offset_by_phandle(host_fdt, host_phandle);
    if (node_offset <= 0) {
        error_setg(&error_fatal,
                   "not able to locate clock handle %d in host device tree",
                   host_phandle);
    }
    node_path = g_malloc(path_len);
    while ((ret = fdt_get_path(host_fdt, node_offset, node_path, path_len))
            == -FDT_ERR_NOSPACE) {
        path_len += 16;
        node_path = g_realloc(node_path, path_len);
    }
    if (ret < 0) {
        error_setg(&error_fatal,
                   "not able to retrieve node path for clock handle %d",
                   host_phandle);
    }

    r = qemu_fdt_getprop(host_fdt, node_path, "compatible", &prop_len,
                         &error_fatal);
    if (strcmp(r, "fixed-clock")) {
        error_setg(&error_fatal,
                   "clock handle %d is not a fixed clock", host_phandle);
    }

    nodename = strrchr(node_path, '/');
    qemu_fdt_add_subnode(guest_fdt, nodename);

    copy_properties_from_host(clock_copied_properties,
                              ARRAY_SIZE(clock_copied_properties),
                              host_fdt, guest_fdt,
                              node_path, nodename);

    qemu_fdt_setprop_cell(guest_fdt, nodename, "phandle", guest_phandle);

    g_free(node_path);
}

/**
 * sysfs_to_dt_name: convert the name found in sysfs into the node name
 * for instance e0900000.xgmac is converted into xgmac@e0900000
 * @sysfs_name: directory name in sysfs
 *
 * returns the device tree name upon success or NULL in case the sysfs name
 * does not match the expected format
 */
static char *sysfs_to_dt_name(const char *sysfs_name)
{
    gchar **substrings =  g_strsplit(sysfs_name, ".", 2);
    char *dt_name = NULL;

    if (!substrings || !substrings[0] || !substrings[1]) {
        goto out;
    }
    dt_name = g_strdup_printf("%s@%s", substrings[1], substrings[0]);
out:
    g_strfreev(substrings);
    return dt_name;
}

/* Device Specific Code */

/**
 * add_calxeda_midway_xgmac_fdt_node
 *
 * Generates a simple node with following properties:
 * compatible string, regs, interrupts, dma-coherent
 */
static int add_calxeda_midway_xgmac_fdt_node(SysBusDevice *sbdev, void *opaque)
{
    PlatformBusFDTData *data = opaque;
    PlatformBusDevice *pbus = data->pbus;
    void *fdt = data->fdt;
    const char *parent_node = data->pbus_node_name;
    int compat_str_len, i;
    char *nodename;
    uint32_t *irq_attr, *reg_attr;
    uint64_t mmio_base, irq_number;
    VFIOPlatformDevice *vdev = VFIO_PLATFORM_DEVICE(sbdev);
    VFIODevice *vbasedev = &vdev->vbasedev;

    mmio_base = platform_bus_get_mmio_addr(pbus, sbdev, 0);
    nodename = g_strdup_printf("%s/%s@%" PRIx64, parent_node,
                               vbasedev->name, mmio_base);
    qemu_fdt_add_subnode(fdt, nodename);

    compat_str_len = strlen(vdev->compat) + 1;
    qemu_fdt_setprop(fdt, nodename, "compatible",
                          vdev->compat, compat_str_len);

    qemu_fdt_setprop(fdt, nodename, "dma-coherent", "", 0);

    reg_attr = g_new(uint32_t, vbasedev->num_regions * 2);
    for (i = 0; i < vbasedev->num_regions; i++) {
        mmio_base = platform_bus_get_mmio_addr(pbus, sbdev, i);
        reg_attr[2 * i] = cpu_to_be32(mmio_base);
        reg_attr[2 * i + 1] = cpu_to_be32(
                                memory_region_size(vdev->regions[i]->mem));
    }
    qemu_fdt_setprop(fdt, nodename, "reg", reg_attr,
                     vbasedev->num_regions * 2 * sizeof(uint32_t));

    irq_attr = g_new(uint32_t, vbasedev->num_irqs * 3);
    for (i = 0; i < vbasedev->num_irqs; i++) {
        irq_number = platform_bus_get_irqn(pbus, sbdev , i)
                         + data->irq_start;
        irq_attr[3 * i] = cpu_to_be32(GIC_FDT_IRQ_TYPE_SPI);
        irq_attr[3 * i + 1] = cpu_to_be32(irq_number);
        irq_attr[3 * i + 2] = cpu_to_be32(GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    }
    qemu_fdt_setprop(fdt, nodename, "interrupts",
                     irq_attr, vbasedev->num_irqs * 3 * sizeof(uint32_t));
    g_free(irq_attr);
    g_free(reg_attr);
    g_free(nodename);
    return 0;
}

/* AMD xgbe properties whose values are copied/pasted from host */
static HostProperty amd_xgbe_copied_properties[] = {
    {"compatible", false},
    {"dma-coherent", true},
    {"amd,per-channel-interrupt", true},
    {"phy-mode", false},
    {"mac-address", true},
    {"amd,speed-set", false},
    {"amd,serdes-blwc", true},
    {"amd,serdes-cdr-rate", true},
    {"amd,serdes-pq-skew", true},
    {"amd,serdes-tx-amp", true},
    {"amd,serdes-dfe-tap-config", true},
    {"amd,serdes-dfe-tap-enable", true},
    {"clock-names", false},
};

/**
 * add_amd_xgbe_fdt_node
 *
 * Generates the combined xgbe/phy node following kernel >=4.2
 * binding documentation:
 * Documentation/devicetree/bindings/net/amd-xgbe.txt:
 * Also 2 clock nodes are created (dma and ptp)
 *
 * Asserts in case of error
 */
static int add_amd_xgbe_fdt_node(SysBusDevice *sbdev, void *opaque)
{
    PlatformBusFDTData *data = opaque;
    PlatformBusDevice *pbus = data->pbus;
    VFIOPlatformDevice *vdev = VFIO_PLATFORM_DEVICE(sbdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    VFIOINTp *intp;
    const char *parent_node = data->pbus_node_name;
    char **node_path, *nodename, *dt_name;
    void *guest_fdt = data->fdt, *host_fdt;
    const void *r;
    int i, prop_len;
    uint32_t *irq_attr, *reg_attr, *host_clock_phandles;
    uint64_t mmio_base, irq_number;
    uint32_t guest_clock_phandles[2];

    host_fdt = load_device_tree_from_sysfs();

    dt_name = sysfs_to_dt_name(vbasedev->name);
    if (!dt_name) {
        error_setg(&error_fatal, "%s incorrect sysfs device name %s",
                    __func__, vbasedev->name);
    }
    node_path = qemu_fdt_node_path(host_fdt, dt_name, vdev->compat,
                                   &error_fatal);
    if (!node_path || !node_path[0]) {
        error_setg(&error_fatal, "%s unable to retrieve node path for %s/%s",
                   __func__, dt_name, vdev->compat);
    }

    if (node_path[1]) {
        error_setg(&error_fatal, "%s more than one node matching %s/%s!",
                   __func__, dt_name, vdev->compat);
    }

    g_free(dt_name);

    if (vbasedev->num_regions != 5) {
        error_setg(&error_fatal, "%s Does the host dt node combine XGBE/PHY?",
                   __func__);
    }

    /* generate nodes for DMA_CLK and PTP_CLK */
    r = qemu_fdt_getprop(host_fdt, node_path[0], "clocks",
                         &prop_len, &error_fatal);
    if (prop_len != 8) {
        error_setg(&error_fatal, "%s clocks property should contain 2 handles",
                   __func__);
    }
    host_clock_phandles = (uint32_t *)r;
    guest_clock_phandles[0] = qemu_fdt_alloc_phandle(guest_fdt);
    guest_clock_phandles[1] = qemu_fdt_alloc_phandle(guest_fdt);

    /**
     * clock handles fetched from host dt are in be32 layout whereas
     * rest of the code uses cpu layout. Also guest clock handles are
     * in cpu layout.
     */
    fdt_build_clock_node(host_fdt, guest_fdt,
                         be32_to_cpu(host_clock_phandles[0]),
                         guest_clock_phandles[0]);

    fdt_build_clock_node(host_fdt, guest_fdt,
                         be32_to_cpu(host_clock_phandles[1]),
                         guest_clock_phandles[1]);

    /* combined XGBE/PHY node */
    mmio_base = platform_bus_get_mmio_addr(pbus, sbdev, 0);
    nodename = g_strdup_printf("%s/%s@%" PRIx64, parent_node,
                               vbasedev->name, mmio_base);
    qemu_fdt_add_subnode(guest_fdt, nodename);

    copy_properties_from_host(amd_xgbe_copied_properties,
                       ARRAY_SIZE(amd_xgbe_copied_properties),
                       host_fdt, guest_fdt,
                       node_path[0], nodename);

    qemu_fdt_setprop_cells(guest_fdt, nodename, "clocks",
                           guest_clock_phandles[0],
                           guest_clock_phandles[1]);

    reg_attr = g_new(uint32_t, vbasedev->num_regions * 2);
    for (i = 0; i < vbasedev->num_regions; i++) {
        mmio_base = platform_bus_get_mmio_addr(pbus, sbdev, i);
        reg_attr[2 * i] = cpu_to_be32(mmio_base);
        reg_attr[2 * i + 1] = cpu_to_be32(
                                memory_region_size(vdev->regions[i]->mem));
    }
    qemu_fdt_setprop(guest_fdt, nodename, "reg", reg_attr,
                     vbasedev->num_regions * 2 * sizeof(uint32_t));

    irq_attr = g_new(uint32_t, vbasedev->num_irqs * 3);
    for (i = 0; i < vbasedev->num_irqs; i++) {
        irq_number = platform_bus_get_irqn(pbus, sbdev , i)
                         + data->irq_start;
        irq_attr[3 * i] = cpu_to_be32(GIC_FDT_IRQ_TYPE_SPI);
        irq_attr[3 * i + 1] = cpu_to_be32(irq_number);
        /*
          * General device interrupt and PCS auto-negotiation interrupts are
          * level-sensitive while the 4 per-channel interrupts are edge
          * sensitive
          */
        QLIST_FOREACH(intp, &vdev->intp_list, next) {
            if (intp->pin == i) {
                break;
            }
        }
        if (intp->flags & VFIO_IRQ_INFO_AUTOMASKED) {
            irq_attr[3 * i + 2] = cpu_to_be32(GIC_FDT_IRQ_FLAGS_LEVEL_HI);
        } else {
            irq_attr[3 * i + 2] = cpu_to_be32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI);
        }
    }
    qemu_fdt_setprop(guest_fdt, nodename, "interrupts",
                     irq_attr, vbasedev->num_irqs * 3 * sizeof(uint32_t));

    g_free(host_fdt);
    g_strfreev(node_path);
    g_free(irq_attr);
    g_free(reg_attr);
    g_free(nodename);
    return 0;
}

#endif /* CONFIG_LINUX */

/* list of supported dynamic sysbus devices */
static const NodeCreationPair add_fdt_node_functions[] = {
#ifdef CONFIG_LINUX
    {TYPE_VFIO_CALXEDA_XGMAC, add_calxeda_midway_xgmac_fdt_node},
    {TYPE_VFIO_AMD_XGBE, add_amd_xgbe_fdt_node},
#endif
    {"", NULL}, /* last element */
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
static int add_fdt_node(SysBusDevice *sbdev, void *opaque)
{
    int i, ret;

    for (i = 0; i < ARRAY_SIZE(add_fdt_node_functions); i++) {
        if (!strcmp(object_get_typename(OBJECT(sbdev)),
                    add_fdt_node_functions[i].typename)) {
            ret = add_fdt_node_functions[i].add_fdt_node_fn(sbdev, opaque);
            assert(!ret);
            return 0;
        }
    }
    error_report("Device %s can not be dynamically instantiated",
                     qdev_fw_name(DEVICE(sbdev)));
    exit(1);
}

/**
 * add_all_platform_bus_fdt_nodes - create all the platform bus nodes
 *
 * builds the parent platform bus node and all the nodes of dynamic
 * sysbus devices attached to it.
 */
static void add_all_platform_bus_fdt_nodes(ARMPlatformBusFDTParams *fdt_params)
{
    const char platcomp[] = "qemu,platform\0simple-bus";
    PlatformBusDevice *pbus;
    DeviceState *dev;
    gchar *node;
    uint64_t addr, size;
    int irq_start, dtb_size;
    struct arm_boot_info *info = fdt_params->binfo;
    const ARMPlatformBusSystemParams *params = fdt_params->system_params;
    const char *intc = fdt_params->intc;
    void *fdt = info->get_dtb(info, &dtb_size);

    /*
     * If the user provided a dtb, we assume the dynamic sysbus nodes
     * already are integrated there. This corresponds to a use case where
     * the dynamic sysbus nodes are complex and their generation is not yet
     * supported. In that case the user can take charge of the guest dt
     * while qemu takes charge of the qom stuff.
     */
    if (info->dtb_filename) {
        return;
    }

    assert(fdt);

    node = g_strdup_printf("/platform@%"PRIx64, params->platform_bus_base);
    addr = params->platform_bus_base;
    size = params->platform_bus_size;
    irq_start = params->platform_bus_first_irq;

    /* Create a /platform node that we can put all devices into */
    qemu_fdt_add_subnode(fdt, node);
    qemu_fdt_setprop(fdt, node, "compatible", platcomp, sizeof(platcomp));

    /* Our platform bus region is less than 32bits, so 1 cell is enough for
     * address and size
     */
    qemu_fdt_setprop_cells(fdt, node, "#size-cells", 1);
    qemu_fdt_setprop_cells(fdt, node, "#address-cells", 1);
    qemu_fdt_setprop_cells(fdt, node, "ranges", 0, addr >> 32, addr, size);

    qemu_fdt_setprop_phandle(fdt, node, "interrupt-parent", intc);

    dev = qdev_find_recursive(sysbus_get_default(), TYPE_PLATFORM_BUS_DEVICE);
    pbus = PLATFORM_BUS_DEVICE(dev);

    /* We can only create dt nodes for dynamic devices when they're ready */
    assert(pbus->done_gathering);

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

static void platform_bus_fdt_notify(Notifier *notifier, void *data)
{
    PlatformBusFDTNotifierParams *p = DO_UPCAST(PlatformBusFDTNotifierParams,
                                                notifier, notifier);

    add_all_platform_bus_fdt_nodes(p->fdt_params);
    g_free(p->fdt_params);
    g_free(p);
}

void arm_register_platform_bus_fdt_creator(ARMPlatformBusFDTParams *fdt_params)
{
    PlatformBusFDTNotifierParams *p = g_new(PlatformBusFDTNotifierParams, 1);

    p->fdt_params = fdt_params;
    p->notifier.notify = platform_bus_fdt_notify;
    qemu_add_machine_init_done_notifier(&p->notifier);
}
