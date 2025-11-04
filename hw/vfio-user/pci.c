/*
 * vfio PCI device over a UNIX socket.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include "qapi-visit-sockets.h"
#include "qemu/error-report.h"

#include "hw/qdev-properties.h"
#include "hw/vfio/pci.h"
#include "hw/vfio-user/device.h"
#include "hw/vfio-user/proxy.h"

#define TYPE_VFIO_USER_PCI "vfio-user-pci"
OBJECT_DECLARE_SIMPLE_TYPE(VFIOUserPCIDevice, VFIO_USER_PCI)

struct VFIOUserPCIDevice {
    VFIOPCIDevice parent_obj;

    SocketAddress *socket;
    bool send_queued;   /* all sends are queued */
    uint32_t wait_time; /* timeout for message replies */
    bool no_post;       /* all region writes are sync */
};

/*
 * The server maintains the device's pending interrupts,
 * via its MSIX table and PBA, so we treat these accesses
 * like PCI config space and forward them.
 */
static uint64_t vfio_user_pba_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    VFIOPCIDevice *vdev = opaque;
    VFIORegion *region = &vdev->bars[vdev->msix->pba_bar].region;
    uint64_t data;

    /* server copy is what matters */
    data = vfio_region_read(region, addr + vdev->msix->pba_offset, size);
    return data;
}

static void vfio_user_pba_write(void *opaque, hwaddr addr,
                                  uint64_t data, unsigned size)
{
    /* dropped */
}

static const MemoryRegionOps vfio_user_pba_ops = {
    .read = vfio_user_pba_read,
    .write = vfio_user_pba_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_user_msix_setup(VFIOPCIDevice *vdev)
{
    MemoryRegion *vfio_reg, *msix_reg, *pba_reg;

    pba_reg = g_new0(MemoryRegion, 1);
    vdev->msix->pba_region = pba_reg;

    vfio_reg = vdev->bars[vdev->msix->pba_bar].mr;
    msix_reg = &PCI_DEVICE(vdev)->msix_pba_mmio;
    memory_region_init_io(pba_reg, OBJECT(vdev), &vfio_user_pba_ops, vdev,
                          "VFIO MSIX PBA", int128_get64(msix_reg->size));
    memory_region_add_subregion_overlap(vfio_reg, vdev->msix->pba_offset,
                                        pba_reg, 1);
}

static void vfio_user_msix_teardown(VFIOPCIDevice *vdev)
{
    MemoryRegion *mr, *sub;

    mr = vdev->bars[vdev->msix->pba_bar].mr;
    sub = vdev->msix->pba_region;
    memory_region_del_subregion(mr, sub);

    g_free(vdev->msix->pba_region);
    vdev->msix->pba_region = NULL;
}

static void vfio_user_dma_read(VFIOPCIDevice *vdev, VFIOUserDMARW *msg)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    VFIOUserProxy *proxy = vdev->vbasedev.proxy;
    VFIOUserDMARW *res;
    MemTxResult r;
    size_t size;

    if (msg->hdr.size < sizeof(*msg)) {
        vfio_user_send_error(proxy, &msg->hdr, EINVAL);
        return;
    }
    if (msg->count > proxy->max_xfer_size) {
        vfio_user_send_error(proxy, &msg->hdr, E2BIG);
        return;
    }

    /* switch to our own message buffer */
    size = msg->count + sizeof(VFIOUserDMARW);
    res = g_malloc0(size);
    memcpy(res, msg, sizeof(*res));
    g_free(msg);

    r = pci_dma_read(pdev, res->offset, &res->data, res->count);

    switch (r) {
    case MEMTX_OK:
        if (res->hdr.flags & VFIO_USER_NO_REPLY) {
            g_free(res);
            return;
        }
        vfio_user_send_reply(proxy, &res->hdr, size);
        break;
    case MEMTX_ERROR:
        vfio_user_send_error(proxy, &res->hdr, EFAULT);
        break;
    case MEMTX_DECODE_ERROR:
        vfio_user_send_error(proxy, &res->hdr, ENODEV);
        break;
    case MEMTX_ACCESS_ERROR:
        vfio_user_send_error(proxy, &res->hdr, EPERM);
        break;
    default:
        error_printf("vfio_user_dma_read unknown error %d\n", r);
        vfio_user_send_error(vdev->vbasedev.proxy, &res->hdr, EINVAL);
    }
}

static void vfio_user_dma_write(VFIOPCIDevice *vdev, VFIOUserDMARW *msg)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    VFIOUserProxy *proxy = vdev->vbasedev.proxy;
    MemTxResult r;

    if (msg->hdr.size < sizeof(*msg)) {
        vfio_user_send_error(proxy, &msg->hdr, EINVAL);
        return;
    }
    /* make sure transfer count isn't larger than the message data */
    if (msg->count > msg->hdr.size - sizeof(*msg)) {
        vfio_user_send_error(proxy, &msg->hdr, E2BIG);
        return;
    }

    r = pci_dma_write(pdev, msg->offset, &msg->data, msg->count);

    switch (r) {
    case MEMTX_OK:
        if ((msg->hdr.flags & VFIO_USER_NO_REPLY) == 0) {
            vfio_user_send_reply(proxy, &msg->hdr, sizeof(msg->hdr));
        } else {
            g_free(msg);
        }
        break;
    case MEMTX_ERROR:
        vfio_user_send_error(proxy, &msg->hdr, EFAULT);
        break;
    case MEMTX_DECODE_ERROR:
        vfio_user_send_error(proxy, &msg->hdr, ENODEV);
        break;
    case MEMTX_ACCESS_ERROR:
        vfio_user_send_error(proxy, &msg->hdr, EPERM);
        break;
    default:
        error_printf("vfio_user_dma_write unknown error %d\n", r);
        vfio_user_send_error(vdev->vbasedev.proxy, &msg->hdr, EINVAL);
    }
}

/*
 * Incoming request message callback.
 *
 * Runs off main loop, so BQL held.
 */
static void vfio_user_pci_process_req(void *opaque, VFIOUserMsg *msg)
{
    VFIOPCIDevice *vdev = opaque;
    VFIOUserHdr *hdr = msg->hdr;

    /* no incoming PCI requests pass FDs */
    if (msg->fds != NULL) {
        vfio_user_send_error(vdev->vbasedev.proxy, hdr, EINVAL);
        vfio_user_putfds(msg);
        return;
    }

    switch (hdr->command) {
    case VFIO_USER_DMA_READ:
        vfio_user_dma_read(vdev, (VFIOUserDMARW *)hdr);
        break;
    case VFIO_USER_DMA_WRITE:
        vfio_user_dma_write(vdev, (VFIOUserDMARW *)hdr);
        break;
    default:
        error_printf("vfio_user_pci_process_req unknown cmd %d\n",
                     hdr->command);
        vfio_user_send_error(vdev->vbasedev.proxy, hdr, ENOSYS);
    }
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
    VFIOUserPCIDevice *vdev = VFIO_USER_PCI(container_of(vbasedev,
                                                         VFIOPCIDevice,
                                                         vbasedev));

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
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(pdev);
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

    if (udev->no_post) {
        proxy->flags |= VFIO_PROXY_NO_POST;
    }

    /* user specified or 5 sec default */
    proxy->wait_time = udev->wait_time;

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

    if (vdev->msix != NULL) {
        vfio_user_msix_setup(vdev);
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

static void vfio_user_pci_init(Object *obj)
{
    PCIDevice *pci_dev = PCI_DEVICE(obj);
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(obj);
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

static void vfio_user_pci_finalize(Object *obj)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(obj);
    VFIODevice *vbasedev = &vdev->vbasedev;

    if (vdev->msix != NULL) {
        vfio_user_msix_teardown(vdev);
    }

    vfio_pci_put_device(vdev);

    if (vbasedev->proxy != NULL) {
        vfio_user_disconnect(vbasedev->proxy);
    }
}

static void vfio_user_pci_reset(DeviceState *dev)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(dev);
    VFIODevice *vbasedev = &vdev->vbasedev;

    vfio_pci_pre_reset(vdev);

    if (vbasedev->reset_works) {
        vfio_user_device_reset(vbasedev->proxy);
    }

    vfio_pci_post_reset(vdev);
}

static const Property vfio_user_pci_properties[] = {
    DEFINE_PROP_UINT32("x-pci-vendor-id", VFIOPCIDevice,
                       vendor_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-device-id", VFIOPCIDevice,
                       device_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-sub-vendor-id", VFIOPCIDevice,
                       sub_vendor_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-sub-device-id", VFIOPCIDevice,
                       sub_device_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-class-code", VFIOPCIDevice,
                       class_code, PCI_ANY_ID),
    DEFINE_PROP_BOOL("x-send-queued", VFIOUserPCIDevice, send_queued, false),
    DEFINE_PROP_UINT32("x-msg-timeout", VFIOUserPCIDevice, wait_time, 5000),
    DEFINE_PROP_BOOL("x-no-posted-writes", VFIOUserPCIDevice, no_post, false),
};

static void vfio_user_pci_set_socket(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    VFIOUserPCIDevice *udev = VFIO_USER_PCI(obj);
    bool success;

    if (VFIO_PCI_DEVICE(udev)->vbasedev.proxy) {
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

static void vfio_user_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, vfio_user_pci_reset);
    device_class_set_props(dc, vfio_user_pci_properties);

    object_class_property_add(klass, "socket", "SocketAddress", NULL,
                              vfio_user_pci_set_socket, NULL, NULL);
    object_class_property_set_description(klass, "socket",
                                          "SocketAddress (UNIX sockets only)");

    dc->desc = "VFIO over socket PCI device assignment";
    pdc->realize = vfio_user_pci_realize;
}

static const TypeInfo vfio_user_pci_info = {
    .name = TYPE_VFIO_USER_PCI,
    .parent = TYPE_VFIO_PCI_DEVICE,
    .instance_size = sizeof(VFIOUserPCIDevice),
    .class_init = vfio_user_pci_class_init,
    .instance_init = vfio_user_pci_init,
    .instance_finalize = vfio_user_pci_finalize,
};

static void register_vfio_user_dev_type(void)
{
    type_register_static(&vfio_user_pci_info);
}

type_init(register_vfio_user_dev_type)
