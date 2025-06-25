/*
 * vfio PCI device over a UNIX socket.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/ioctl.h>
#include "qemu/osdep.h"
#include "qapi-visit-sockets.h"

#include "hw/qdev-properties.h"
#include "hw/vfio/pci.h"
#include "hw/vfio-user/device.h"
#include "hw/vfio-user/proxy.h"

#define TYPE_VFIO_USER_PCI "vfio-user-pci"
OBJECT_DECLARE_SIMPLE_TYPE(VFIOUserPCIDevice, VFIO_USER_PCI)

struct VFIOUserPCIDevice {
    VFIOPCIDevice device;
    SocketAddress *socket;
    bool send_queued;   /* all sends are queued */
};

/*
 * Incoming request message callback.
 *
 * Runs off main loop, so BQL held.
 */
static void vfio_user_pci_process_req(void *opaque, VFIOUserMsg *msg)
{

}

/*
 * Emulated devices don't use host hot reset
 */
static void vfio_user_compute_needs_reset(VFIODevice *vbasedev)
{
    vbasedev->needs_reset = false;
}

static Object *vfio_user_pci_get_object(VFIODevice *vbasedev)
{
    VFIOUserPCIDevice *vdev = container_of(vbasedev, VFIOUserPCIDevice,
                                           device.vbasedev);

    return OBJECT(vdev);
}

static VFIODeviceOps vfio_user_pci_ops = {
    .vfio_compute_needs_reset = vfio_user_compute_needs_reset,
    .vfio_eoi = vfio_pci_intx_eoi,
    .vfio_get_object = vfio_user_pci_get_object,
    /* No live migration support yet. */
    .vfio_save_config = NULL,
    .vfio_load_config = NULL,
};

static void vfio_user_pci_realize(PCIDevice *pdev, Error **errp)
{
    ERRP_GUARD();
    VFIOUserPCIDevice *udev = VFIO_USER_PCI(pdev);
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(pdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    const char *sock_name;
    AddressSpace *as;
    SocketAddress addr;
    VFIOUserProxy *proxy;

    if (!udev->socket) {
        error_setg(errp, "No socket specified");
        error_append_hint(errp, "e.g. -device '{"
            "\"driver\":\"vfio-user-pci\", "
            "\"socket\": {\"path\": \"/tmp/vfio-user.sock\", "
            "\"type\": \"unix\"}'"
            "}'\n");
        return;
    }

    sock_name = udev->socket->u.q_unix.path;

    vbasedev->name = g_strdup_printf("vfio-user:%s", sock_name);

    memset(&addr, 0, sizeof(addr));
    addr.type = SOCKET_ADDRESS_TYPE_UNIX;
    addr.u.q_unix.path = (char *)sock_name;
    proxy = vfio_user_connect_dev(&addr, errp);
    if (!proxy) {
        return;
    }
    vbasedev->proxy = proxy;
    vfio_user_set_handler(vbasedev, vfio_user_pci_process_req, vdev);

    vbasedev->name = g_strdup_printf("vfio-user:%s", sock_name);

    if (udev->send_queued) {
        proxy->flags |= VFIO_PROXY_FORCE_QUEUED;
    }

    if (!vfio_user_validate_version(proxy, errp)) {
        goto error;
    }

    /*
     * Use socket-based device I/O instead of vfio kernel driver.
     */
    vbasedev->io_ops = &vfio_user_device_io_ops_sock;

    /*
     * vfio-user devices are effectively mdevs (don't use a host iommu).
     */
    vbasedev->mdev = true;

    /*
     * Enable per-region fds.
     */
    vbasedev->use_region_fds = true;

    as = pci_device_iommu_address_space(pdev);
    if (!vfio_device_attach_by_iommu_type(TYPE_VFIO_IOMMU_USER,
                                          vbasedev->name, vbasedev,
                                          as, errp)) {
        goto error;
    }

    if (!vfio_pci_populate_device(vdev, errp)) {
        goto error;
    }

    if (!vfio_pci_config_setup(vdev, errp)) {
        goto error;
    }

    /*
     * vfio_pci_config_setup will have registered the device's BARs
     * and setup any MSIX BARs, so errors after it succeeds must
     * use out_teardown
     */

    if (!vfio_pci_add_capabilities(vdev, errp)) {
        goto out_teardown;
    }

    if (!vfio_pci_interrupt_setup(vdev, errp)) {
        goto out_teardown;
    }

    vfio_pci_register_err_notifier(vdev);
    vfio_pci_register_req_notifier(vdev);

    return;

out_teardown:
    vfio_pci_teardown_msi(vdev);
    vfio_pci_bars_exit(vdev);
error:
    error_prepend(errp, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    vfio_pci_put_device(vdev);
}

static void vfio_user_instance_init(Object *obj)
{
    PCIDevice *pci_dev = PCI_DEVICE(obj);
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(obj);
    VFIODevice *vbasedev = &vdev->vbasedev;

    device_add_bootindex_property(obj, &vdev->bootindex,
                                  "bootindex", NULL,
                                  &pci_dev->qdev);
    vdev->host.domain = ~0U;
    vdev->host.bus = ~0U;
    vdev->host.slot = ~0U;
    vdev->host.function = ~0U;

    vfio_device_init(vbasedev, VFIO_DEVICE_TYPE_PCI, &vfio_user_pci_ops,
                     DEVICE(vdev), false);

    vdev->nv_gpudirect_clique = 0xFF;

    /*
     * QEMU_PCI_CAP_EXPRESS initialization does not depend on QEMU command
     * line, therefore, no need to wait to realize like other devices.
     */
    pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static void vfio_user_instance_finalize(Object *obj)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(obj);
    VFIODevice *vbasedev = &vdev->vbasedev;

    vfio_pci_put_device(vdev);

    if (vbasedev->proxy != NULL) {
        vfio_user_disconnect(vbasedev->proxy);
    }
}

static const Property vfio_user_pci_dev_properties[] = {
    DEFINE_PROP_UINT32("x-pci-vendor-id", VFIOPCIDevice,
                       vendor_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-device-id", VFIOPCIDevice,
                       device_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-sub-vendor-id", VFIOPCIDevice,
                       sub_vendor_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-sub-device-id", VFIOPCIDevice,
                       sub_device_id, PCI_ANY_ID),
    DEFINE_PROP_BOOL("x-send-queued", VFIOUserPCIDevice, send_queued, false),
};

static void vfio_user_pci_set_socket(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    VFIOUserPCIDevice *udev = VFIO_USER_PCI(obj);
    bool success;

    if (udev->device.vbasedev.proxy) {
        error_setg(errp, "Proxy is connected");
        return;
    }

    qapi_free_SocketAddress(udev->socket);

    udev->socket = NULL;

    success = visit_type_SocketAddress(v, name, &udev->socket, errp);

    if (!success) {
        return;
    }

    if (udev->socket->type != SOCKET_ADDRESS_TYPE_UNIX) {
        error_setg(errp, "Unsupported socket type %s",
                   SocketAddressType_str(udev->socket->type));
        qapi_free_SocketAddress(udev->socket);
        udev->socket = NULL;
        return;
    }
}

static void vfio_user_pci_dev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);

    device_class_set_props(dc, vfio_user_pci_dev_properties);

    object_class_property_add(klass, "socket", "SocketAddress", NULL,
                              vfio_user_pci_set_socket, NULL, NULL);
    object_class_property_set_description(klass, "socket",
                                          "SocketAddress (UNIX sockets only)");

    dc->desc = "VFIO over socket PCI device assignment";
    pdc->realize = vfio_user_pci_realize;
}

static const TypeInfo vfio_user_pci_dev_info = {
    .name = TYPE_VFIO_USER_PCI,
    .parent = TYPE_VFIO_PCI_BASE,
    .instance_size = sizeof(VFIOUserPCIDevice),
    .class_init = vfio_user_pci_dev_class_init,
    .instance_init = vfio_user_instance_init,
    .instance_finalize = vfio_user_instance_finalize,
};

static void register_vfio_user_dev_type(void)
{
    type_register_static(&vfio_user_pci_dev_info);
}

 type_init(register_vfio_user_dev_type)
