/**
 * QEMU vfio-user-server server object
 *
 * Copyright © 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL-v2, version 2 or later.
 *
 * See the COPYING file in the top-level directory.
 *
 */

/**
 * Usage: add options:
 *     -machine x-remote,vfio-user=on,auto-shutdown=on
 *     -device <PCI-device>,id=<pci-dev-id>
 *     -object x-vfio-user-server,id=<id>,type=unix,path=<socket-path>,
 *             device=<pci-dev-id>
 *
 * Note that x-vfio-user-server object must be used with x-remote machine only.
 * This server could only support PCI devices for now.
 *
 * type - SocketAddress type - presently "unix" alone is supported. Required
 *        option
 *
 * path - named unix socket, it will be created by the server. It is
 *        a required option
 *
 * device - id of a device on the server, a required option. PCI devices
 *          alone are supported presently.
 *
 * notes - x-vfio-user-server could block IO and monitor during the
 *         initialization phase.
 */

#include "qemu/osdep.h"

#include "qom/object.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "sysemu/runstate.h"
#include "hw/boards.h"
#include "hw/remote/machine.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qapi-events-misc.h"
#include "qemu/notify.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "sysemu/sysemu.h"
#include "libvfio-user.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "qemu/timer.h"
#include "exec/memory.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/remote/vfio-user-obj.h"

#define TYPE_VFU_OBJECT "x-vfio-user-server"
OBJECT_DECLARE_TYPE(VfuObject, VfuObjectClass, VFU_OBJECT)

/**
 * VFU_OBJECT_ERROR - reports an error message. If auto_shutdown
 * is set, it aborts the machine on error. Otherwise, it logs an
 * error message without aborting.
 */
#define VFU_OBJECT_ERROR(o, fmt, ...)                                     \
    {                                                                     \
        if (vfu_object_auto_shutdown()) {                                 \
            error_setg(&error_abort, (fmt), ## __VA_ARGS__);              \
        } else {                                                          \
            error_report((fmt), ## __VA_ARGS__);                          \
        }                                                                 \
    }                                                                     \

struct VfuObjectClass {
    ObjectClass parent_class;

    unsigned int nr_devs;
};

struct VfuObject {
    /* private */
    Object parent;

    SocketAddress *socket;

    char *device;

    Error *err;

    Notifier machine_done;

    vfu_ctx_t *vfu_ctx;

    PCIDevice *pci_dev;

    Error *unplug_blocker;

    int vfu_poll_fd;

    MSITriggerFunc *default_msi_trigger;
    MSIPrepareMessageFunc *default_msi_prepare_message;
    MSIxPrepareMessageFunc *default_msix_prepare_message;
};

static void vfu_object_init_ctx(VfuObject *o, Error **errp);

static bool vfu_object_auto_shutdown(void)
{
    bool auto_shutdown = true;
    Error *local_err = NULL;

    if (!current_machine) {
        return auto_shutdown;
    }

    auto_shutdown = object_property_get_bool(OBJECT(current_machine),
                                             "auto-shutdown",
                                             &local_err);

    /*
     * local_err would be set if no such property exists - safe to ignore.
     * Unlikely scenario as auto-shutdown is always defined for
     * TYPE_REMOTE_MACHINE, and  TYPE_VFU_OBJECT only works with
     * TYPE_REMOTE_MACHINE
     */
    if (local_err) {
        auto_shutdown = true;
        error_free(local_err);
    }

    return auto_shutdown;
}

static void vfu_object_set_socket(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    if (o->vfu_ctx) {
        error_setg(errp, "vfu: Unable to set socket property - server busy");
        return;
    }

    qapi_free_SocketAddress(o->socket);

    o->socket = NULL;

    visit_type_SocketAddress(v, name, &o->socket, errp);

    if (o->socket->type != SOCKET_ADDRESS_TYPE_UNIX) {
        error_setg(errp, "vfu: Unsupported socket type - %s",
                   SocketAddressType_str(o->socket->type));
        qapi_free_SocketAddress(o->socket);
        o->socket = NULL;
        return;
    }

    trace_vfu_prop("socket", o->socket->u.q_unix.path);

    vfu_object_init_ctx(o, errp);
}

static void vfu_object_set_device(Object *obj, const char *str, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    if (o->vfu_ctx) {
        error_setg(errp, "vfu: Unable to set device property - server busy");
        return;
    }

    g_free(o->device);

    o->device = g_strdup(str);

    trace_vfu_prop("device", str);

    vfu_object_init_ctx(o, errp);
}

static void vfu_object_ctx_run(void *opaque)
{
    VfuObject *o = opaque;
    const char *vfu_id;
    char *vfu_path, *pci_dev_path;
    int ret = -1;

    while (ret != 0) {
        ret = vfu_run_ctx(o->vfu_ctx);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ENOTCONN) {
                vfu_id = object_get_canonical_path_component(OBJECT(o));
                vfu_path = object_get_canonical_path(OBJECT(o));
                g_assert(o->pci_dev);
                pci_dev_path = object_get_canonical_path(OBJECT(o->pci_dev));
                 /* o->device is a required property and is non-NULL here */
                g_assert(o->device);
                qapi_event_send_vfu_client_hangup(vfu_id, vfu_path,
                                                  o->device, pci_dev_path);
                qemu_set_fd_handler(o->vfu_poll_fd, NULL, NULL, NULL);
                o->vfu_poll_fd = -1;
                object_unparent(OBJECT(o));
                g_free(vfu_path);
                g_free(pci_dev_path);
                break;
            } else {
                VFU_OBJECT_ERROR(o, "vfu: Failed to run device %s - %s",
                                 o->device, strerror(errno));
                break;
            }
        }
    }
}

static void vfu_object_attach_ctx(void *opaque)
{
    VfuObject *o = opaque;
    GPollFD pfds[1];
    int ret;

    qemu_set_fd_handler(o->vfu_poll_fd, NULL, NULL, NULL);

    pfds[0].fd = o->vfu_poll_fd;
    pfds[0].events = G_IO_IN | G_IO_HUP | G_IO_ERR;

retry_attach:
    ret = vfu_attach_ctx(o->vfu_ctx);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        /**
         * vfu_object_attach_ctx can block QEMU's main loop
         * during attach - the monitor and other IO
         * could be unresponsive during this time.
         */
        (void)qemu_poll_ns(pfds, 1, 500 * (int64_t)SCALE_MS);
        goto retry_attach;
    } else if (ret < 0) {
        VFU_OBJECT_ERROR(o, "vfu: Failed to attach device %s to context - %s",
                         o->device, strerror(errno));
        return;
    }

    o->vfu_poll_fd = vfu_get_poll_fd(o->vfu_ctx);
    if (o->vfu_poll_fd < 0) {
        VFU_OBJECT_ERROR(o, "vfu: Failed to get poll fd %s", o->device);
        return;
    }

    qemu_set_fd_handler(o->vfu_poll_fd, vfu_object_ctx_run, NULL, o);
}

static ssize_t vfu_object_cfg_access(vfu_ctx_t *vfu_ctx, char * const buf,
                                     size_t count, loff_t offset,
                                     const bool is_write)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    uint32_t pci_access_width = sizeof(uint32_t);
    size_t bytes = count;
    uint32_t val = 0;
    char *ptr = buf;
    int len;

    /*
     * Writes to the BAR registers would trigger an update to the
     * global Memory and IO AddressSpaces. But the remote device
     * never uses the global AddressSpaces, therefore overlapping
     * memory regions are not a problem
     */
    while (bytes > 0) {
        len = (bytes > pci_access_width) ? pci_access_width : bytes;
        if (is_write) {
            memcpy(&val, ptr, len);
            pci_host_config_write_common(o->pci_dev, offset,
                                         pci_config_size(o->pci_dev),
                                         val, len);
            trace_vfu_cfg_write(offset, val);
        } else {
            val = pci_host_config_read_common(o->pci_dev, offset,
                                              pci_config_size(o->pci_dev), len);
            memcpy(ptr, &val, len);
            trace_vfu_cfg_read(offset, val);
        }
        offset += len;
        ptr += len;
        bytes -= len;
    }

    return count;
}

static void dma_register(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    AddressSpace *dma_as = NULL;
    MemoryRegion *subregion = NULL;
    g_autofree char *name = NULL;
    struct iovec *iov = &info->iova;

    if (!info->vaddr) {
        return;
    }

    name = g_strdup_printf("mem-%s-%"PRIx64"", o->device,
                           (uint64_t)info->vaddr);

    subregion = g_new0(MemoryRegion, 1);

    memory_region_init_ram_ptr(subregion, NULL, name,
                               iov->iov_len, info->vaddr);

    dma_as = pci_device_iommu_address_space(o->pci_dev);

    memory_region_add_subregion(dma_as->root, (hwaddr)iov->iov_base, subregion);

    trace_vfu_dma_register((uint64_t)iov->iov_base, iov->iov_len);
}

static void dma_unregister(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    AddressSpace *dma_as = NULL;
    MemoryRegion *mr = NULL;
    ram_addr_t offset;

    mr = memory_region_from_host(info->vaddr, &offset);
    if (!mr) {
        return;
    }

    dma_as = pci_device_iommu_address_space(o->pci_dev);

    memory_region_del_subregion(dma_as->root, mr);

    object_unparent((OBJECT(mr)));

    trace_vfu_dma_unregister((uint64_t)info->iova.iov_base);
}

static int vfu_object_mr_rw(MemoryRegion *mr, uint8_t *buf, hwaddr offset,
                            hwaddr size, const bool is_write)
{
    uint8_t *ptr = buf;
    bool release_lock = false;
    uint8_t *ram_ptr = NULL;
    MemTxResult result;
    int access_size;
    uint64_t val;

    if (memory_access_is_direct(mr, is_write)) {
        /**
         * Some devices expose a PCI expansion ROM, which could be buffer
         * based as compared to other regions which are primarily based on
         * MemoryRegionOps. memory_region_find() would already check
         * for buffer overflow, we don't need to repeat it here.
         */
        ram_ptr = memory_region_get_ram_ptr(mr);

        if (is_write) {
            memcpy((ram_ptr + offset), buf, size);
        } else {
            memcpy(buf, (ram_ptr + offset), size);
        }

        return 0;
    }

    while (size) {
        /**
         * The read/write logic used below is similar to the ones in
         * flatview_read/write_continue()
         */
        release_lock = prepare_mmio_access(mr);

        access_size = memory_access_size(mr, size, offset);

        if (is_write) {
            val = ldn_he_p(ptr, access_size);

            result = memory_region_dispatch_write(mr, offset, val,
                                                  size_memop(access_size),
                                                  MEMTXATTRS_UNSPECIFIED);
        } else {
            result = memory_region_dispatch_read(mr, offset, &val,
                                                 size_memop(access_size),
                                                 MEMTXATTRS_UNSPECIFIED);

            stn_he_p(ptr, access_size, val);
        }

        if (release_lock) {
            qemu_mutex_unlock_iothread();
            release_lock = false;
        }

        if (result != MEMTX_OK) {
            return -1;
        }

        size -= access_size;
        ptr += access_size;
        offset += access_size;
    }

    return 0;
}

static size_t vfu_object_bar_rw(PCIDevice *pci_dev, int pci_bar,
                                hwaddr bar_offset, char * const buf,
                                hwaddr len, const bool is_write)
{
    MemoryRegionSection section = { 0 };
    uint8_t *ptr = (uint8_t *)buf;
    MemoryRegion *section_mr = NULL;
    uint64_t section_size;
    hwaddr section_offset;
    hwaddr size = 0;

    while (len) {
        section = memory_region_find(pci_dev->io_regions[pci_bar].memory,
                                     bar_offset, len);

        if (!section.mr) {
            warn_report("vfu: invalid address 0x%"PRIx64"", bar_offset);
            return size;
        }

        section_mr = section.mr;
        section_offset = section.offset_within_region;
        section_size = int128_get64(section.size);

        if (is_write && section_mr->readonly) {
            warn_report("vfu: attempting to write to readonly region in "
                        "bar %d - [0x%"PRIx64" - 0x%"PRIx64"]",
                        pci_bar, bar_offset,
                        (bar_offset + section_size));
            memory_region_unref(section_mr);
            return size;
        }

        if (vfu_object_mr_rw(section_mr, ptr, section_offset,
                             section_size, is_write)) {
            warn_report("vfu: failed to %s "
                        "[0x%"PRIx64" - 0x%"PRIx64"] in bar %d",
                        is_write ? "write to" : "read from", bar_offset,
                        (bar_offset + section_size), pci_bar);
            memory_region_unref(section_mr);
            return size;
        }

        size += section_size;
        bar_offset += section_size;
        ptr += section_size;
        len -= section_size;

        memory_region_unref(section_mr);
    }

    return size;
}

/**
 * VFU_OBJECT_BAR_HANDLER - macro for defining handlers for PCI BARs.
 *
 * To create handler for BAR number 2, VFU_OBJECT_BAR_HANDLER(2) would
 * define vfu_object_bar2_handler
 */
#define VFU_OBJECT_BAR_HANDLER(BAR_NO)                                         \
    static ssize_t vfu_object_bar##BAR_NO##_handler(vfu_ctx_t *vfu_ctx,        \
                                        char * const buf, size_t count,        \
                                        loff_t offset, const bool is_write)    \
    {                                                                          \
        VfuObject *o = vfu_get_private(vfu_ctx);                               \
        PCIDevice *pci_dev = o->pci_dev;                                       \
                                                                               \
        return vfu_object_bar_rw(pci_dev, BAR_NO, offset,                      \
                                 buf, count, is_write);                        \
    }                                                                          \

VFU_OBJECT_BAR_HANDLER(0)
VFU_OBJECT_BAR_HANDLER(1)
VFU_OBJECT_BAR_HANDLER(2)
VFU_OBJECT_BAR_HANDLER(3)
VFU_OBJECT_BAR_HANDLER(4)
VFU_OBJECT_BAR_HANDLER(5)
VFU_OBJECT_BAR_HANDLER(6)

static vfu_region_access_cb_t *vfu_object_bar_handlers[PCI_NUM_REGIONS] = {
    &vfu_object_bar0_handler,
    &vfu_object_bar1_handler,
    &vfu_object_bar2_handler,
    &vfu_object_bar3_handler,
    &vfu_object_bar4_handler,
    &vfu_object_bar5_handler,
    &vfu_object_bar6_handler,
};

/**
 * vfu_object_register_bars - Identify active BAR regions of pdev and setup
 *                            callbacks to handle read/write accesses
 */
static void vfu_object_register_bars(vfu_ctx_t *vfu_ctx, PCIDevice *pdev)
{
    int flags = VFU_REGION_FLAG_RW;
    int i;

    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        if (!pdev->io_regions[i].size) {
            continue;
        }

        if ((i == VFU_PCI_DEV_ROM_REGION_IDX) ||
            pdev->io_regions[i].memory->readonly) {
            flags &= ~VFU_REGION_FLAG_WRITE;
        }

        vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR0_REGION_IDX + i,
                         (size_t)pdev->io_regions[i].size,
                         vfu_object_bar_handlers[i],
                         flags, NULL, 0, -1, 0);

        trace_vfu_bar_register(i, pdev->io_regions[i].addr,
                               pdev->io_regions[i].size);
    }
}

static int vfu_object_map_irq(PCIDevice *pci_dev, int intx)
{
    int pci_bdf = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(pci_dev)),
                                pci_dev->devfn);

    return pci_bdf;
}

static void vfu_object_set_irq(void *opaque, int pirq, int level)
{
    PCIBus *pci_bus = opaque;
    PCIDevice *pci_dev = NULL;
    vfu_ctx_t *vfu_ctx = NULL;
    int pci_bus_num, devfn;

    if (level) {
        pci_bus_num = PCI_BUS_NUM(pirq);
        devfn = PCI_BDF_TO_DEVFN(pirq);

        /*
         * pci_find_device() performs at O(1) if the device is attached
         * to the root PCI bus. Whereas, if the device is attached to a
         * secondary PCI bus (such as when a root port is involved),
         * finding the parent PCI bus could take O(n)
         */
        pci_dev = pci_find_device(pci_bus, pci_bus_num, devfn);

        vfu_ctx = pci_dev->irq_opaque;

        g_assert(vfu_ctx);

        vfu_irq_trigger(vfu_ctx, 0);
    }
}

static MSIMessage vfu_object_msi_prepare_msg(PCIDevice *pci_dev,
                                             unsigned int vector)
{
    MSIMessage msg;

    msg.address = 0;
    msg.data = vector;

    return msg;
}

static void vfu_object_msi_trigger(PCIDevice *pci_dev, MSIMessage msg)
{
    vfu_ctx_t *vfu_ctx = pci_dev->irq_opaque;

    vfu_irq_trigger(vfu_ctx, msg.data);
}

static void vfu_object_setup_msi_cbs(VfuObject *o)
{
    o->default_msi_trigger = o->pci_dev->msi_trigger;
    o->default_msi_prepare_message = o->pci_dev->msi_prepare_message;
    o->default_msix_prepare_message = o->pci_dev->msix_prepare_message;

    o->pci_dev->msi_trigger = vfu_object_msi_trigger;
    o->pci_dev->msi_prepare_message = vfu_object_msi_prepare_msg;
    o->pci_dev->msix_prepare_message = vfu_object_msi_prepare_msg;
}

static void vfu_object_restore_msi_cbs(VfuObject *o)
{
    o->pci_dev->msi_trigger = o->default_msi_trigger;
    o->pci_dev->msi_prepare_message = o->default_msi_prepare_message;
    o->pci_dev->msix_prepare_message = o->default_msix_prepare_message;
}

static void vfu_msix_irq_state(vfu_ctx_t *vfu_ctx, uint32_t start,
                               uint32_t count, bool mask)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    uint32_t vector;

    for (vector = start; vector < count; vector++) {
        msix_set_mask(o->pci_dev, vector, mask);
    }
}

static void vfu_msi_irq_state(vfu_ctx_t *vfu_ctx, uint32_t start,
                              uint32_t count, bool mask)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    Error *err = NULL;
    uint32_t vector;

    for (vector = start; vector < count; vector++) {
        msi_set_mask(o->pci_dev, vector, mask, &err);
        if (err) {
            VFU_OBJECT_ERROR(o, "vfu: %s: %s", o->device,
                             error_get_pretty(err));
            error_free(err);
            err = NULL;
        }
    }
}

static int vfu_object_setup_irqs(VfuObject *o, PCIDevice *pci_dev)
{
    vfu_ctx_t *vfu_ctx = o->vfu_ctx;
    int ret;

    ret = vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_INTX_IRQ, 1);
    if (ret < 0) {
        return ret;
    }

    if (msix_nr_vectors_allocated(pci_dev)) {
        ret = vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_MSIX_IRQ,
                                       msix_nr_vectors_allocated(pci_dev));
        vfu_setup_irq_state_callback(vfu_ctx, VFU_DEV_MSIX_IRQ,
                                     &vfu_msix_irq_state);
    } else if (msi_nr_vectors_allocated(pci_dev)) {
        ret = vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_MSI_IRQ,
                                       msi_nr_vectors_allocated(pci_dev));
        vfu_setup_irq_state_callback(vfu_ctx, VFU_DEV_MSI_IRQ,
                                     &vfu_msi_irq_state);
    }

    if (ret < 0) {
        return ret;
    }

    vfu_object_setup_msi_cbs(o);

    pci_dev->irq_opaque = vfu_ctx;

    return 0;
}

void vfu_object_set_bus_irq(PCIBus *pci_bus)
{
    int bus_num = pci_bus_num(pci_bus);
    int max_bdf = PCI_BUILD_BDF(bus_num, PCI_DEVFN_MAX - 1);

    pci_bus_irqs(pci_bus, vfu_object_set_irq, vfu_object_map_irq, pci_bus,
                 max_bdf);
}

static int vfu_object_device_reset(vfu_ctx_t *vfu_ctx, vfu_reset_type_t type)
{
    VfuObject *o = vfu_get_private(vfu_ctx);

    /* vfu_object_ctx_run() handles lost connection */
    if (type == VFU_RESET_LOST_CONN) {
        return 0;
    }

    device_cold_reset(DEVICE(o->pci_dev));

    return 0;
}

/*
 * TYPE_VFU_OBJECT depends on the availability of the 'socket' and 'device'
 * properties. It also depends on devices instantiated in QEMU. These
 * dependencies are not available during the instance_init phase of this
 * object's life-cycle. As such, the server is initialized after the
 * machine is setup. machine_init_done_notifier notifies TYPE_VFU_OBJECT
 * when the machine is setup, and the dependencies are available.
 */
static void vfu_object_machine_done(Notifier *notifier, void *data)
{
    VfuObject *o = container_of(notifier, VfuObject, machine_done);
    Error *err = NULL;

    vfu_object_init_ctx(o, &err);

    if (err) {
        error_propagate(&error_abort, err);
    }
}

/**
 * vfu_object_init_ctx: Create and initialize libvfio-user context. Add
 *     an unplug blocker for the associated PCI device. Setup a FD handler
 *     to process incoming messages in the context's socket.
 *
 *     The socket and device properties are mandatory, and this function
 *     will not create the context without them - the setters for these
 *     properties should call this function when the property is set. The
 *     machine should also be ready when this function is invoked - it is
 *     because QEMU objects are initialized before devices, and the
 *     associated PCI device wouldn't be available at the object
 *     initialization time. Until these conditions are satisfied, this
 *     function would return early without performing any task.
 */
static void vfu_object_init_ctx(VfuObject *o, Error **errp)
{
    DeviceState *dev = NULL;
    vfu_pci_type_t pci_type = VFU_PCI_TYPE_CONVENTIONAL;
    int ret;

    if (o->vfu_ctx || !o->socket || !o->device ||
            !phase_check(PHASE_MACHINE_READY)) {
        return;
    }

    if (o->err) {
        error_propagate(errp, o->err);
        o->err = NULL;
        return;
    }

    o->vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, o->socket->u.q_unix.path,
                                LIBVFIO_USER_FLAG_ATTACH_NB,
                                o, VFU_DEV_TYPE_PCI);
    if (o->vfu_ctx == NULL) {
        error_setg(errp, "vfu: Failed to create context - %s", strerror(errno));
        return;
    }

    dev = qdev_find_recursive(sysbus_get_default(), o->device);
    if (dev == NULL) {
        error_setg(errp, "vfu: Device %s not found", o->device);
        goto fail;
    }

    if (!object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        error_setg(errp, "vfu: %s not a PCI device", o->device);
        goto fail;
    }

    o->pci_dev = PCI_DEVICE(dev);

    object_ref(OBJECT(o->pci_dev));

    if (pci_is_express(o->pci_dev)) {
        pci_type = VFU_PCI_TYPE_EXPRESS;
    }

    ret = vfu_pci_init(o->vfu_ctx, pci_type, PCI_HEADER_TYPE_NORMAL, 0);
    if (ret < 0) {
        error_setg(errp,
                   "vfu: Failed to attach PCI device %s to context - %s",
                   o->device, strerror(errno));
        goto fail;
    }

    error_setg(&o->unplug_blocker,
               "vfu: %s for %s must be deleted before unplugging",
               TYPE_VFU_OBJECT, o->device);
    qdev_add_unplug_blocker(DEVICE(o->pci_dev), o->unplug_blocker);

    ret = vfu_setup_region(o->vfu_ctx, VFU_PCI_DEV_CFG_REGION_IDX,
                           pci_config_size(o->pci_dev), &vfu_object_cfg_access,
                           VFU_REGION_FLAG_RW | VFU_REGION_FLAG_ALWAYS_CB,
                           NULL, 0, -1, 0);
    if (ret < 0) {
        error_setg(errp,
                   "vfu: Failed to setup config space handlers for %s- %s",
                   o->device, strerror(errno));
        goto fail;
    }

    ret = vfu_setup_device_dma(o->vfu_ctx, &dma_register, &dma_unregister);
    if (ret < 0) {
        error_setg(errp, "vfu: Failed to setup DMA handlers for %s",
                   o->device);
        goto fail;
    }

    vfu_object_register_bars(o->vfu_ctx, o->pci_dev);

    ret = vfu_object_setup_irqs(o, o->pci_dev);
    if (ret < 0) {
        error_setg(errp, "vfu: Failed to setup interrupts for %s",
                   o->device);
        goto fail;
    }

    ret = vfu_setup_device_reset_cb(o->vfu_ctx, &vfu_object_device_reset);
    if (ret < 0) {
        error_setg(errp, "vfu: Failed to setup reset callback");
        goto fail;
    }

    ret = vfu_realize_ctx(o->vfu_ctx);
    if (ret < 0) {
        error_setg(errp, "vfu: Failed to realize device %s- %s",
                   o->device, strerror(errno));
        goto fail;
    }

    o->vfu_poll_fd = vfu_get_poll_fd(o->vfu_ctx);
    if (o->vfu_poll_fd < 0) {
        error_setg(errp, "vfu: Failed to get poll fd %s", o->device);
        goto fail;
    }

    qemu_set_fd_handler(o->vfu_poll_fd, vfu_object_attach_ctx, NULL, o);

    return;

fail:
    vfu_destroy_ctx(o->vfu_ctx);
    if (o->unplug_blocker && o->pci_dev) {
        qdev_del_unplug_blocker(DEVICE(o->pci_dev), o->unplug_blocker);
        error_free(o->unplug_blocker);
        o->unplug_blocker = NULL;
    }
    if (o->pci_dev) {
        vfu_object_restore_msi_cbs(o);
        o->pci_dev->irq_opaque = NULL;
        object_unref(OBJECT(o->pci_dev));
        o->pci_dev = NULL;
    }
    o->vfu_ctx = NULL;
}

static void vfu_object_init(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    k->nr_devs++;

    if (!object_dynamic_cast(OBJECT(current_machine), TYPE_REMOTE_MACHINE)) {
        error_setg(&o->err, "vfu: %s only compatible with %s machine",
                   TYPE_VFU_OBJECT, TYPE_REMOTE_MACHINE);
        return;
    }

    if (!phase_check(PHASE_MACHINE_READY)) {
        o->machine_done.notify = vfu_object_machine_done;
        qemu_add_machine_init_done_notifier(&o->machine_done);
    }

    o->vfu_poll_fd = -1;
}

static void vfu_object_finalize(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    k->nr_devs--;

    qapi_free_SocketAddress(o->socket);

    o->socket = NULL;

    if (o->vfu_poll_fd != -1) {
        qemu_set_fd_handler(o->vfu_poll_fd, NULL, NULL, NULL);
        o->vfu_poll_fd = -1;
    }

    if (o->vfu_ctx) {
        vfu_destroy_ctx(o->vfu_ctx);
        o->vfu_ctx = NULL;
    }

    g_free(o->device);

    o->device = NULL;

    if (o->unplug_blocker && o->pci_dev) {
        qdev_del_unplug_blocker(DEVICE(o->pci_dev), o->unplug_blocker);
        error_free(o->unplug_blocker);
        o->unplug_blocker = NULL;
    }

    if (o->pci_dev) {
        vfu_object_restore_msi_cbs(o);
        o->pci_dev->irq_opaque = NULL;
        object_unref(OBJECT(o->pci_dev));
        o->pci_dev = NULL;
    }

    if (!k->nr_devs && vfu_object_auto_shutdown()) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }

    if (o->machine_done.notify) {
        qemu_remove_machine_init_done_notifier(&o->machine_done);
        o->machine_done.notify = NULL;
    }
}

static void vfu_object_class_init(ObjectClass *klass, void *data)
{
    VfuObjectClass *k = VFU_OBJECT_CLASS(klass);

    k->nr_devs = 0;

    object_class_property_add(klass, "socket", "SocketAddress", NULL,
                              vfu_object_set_socket, NULL, NULL);
    object_class_property_set_description(klass, "socket",
                                          "SocketAddress "
                                          "(ex: type=unix,path=/tmp/sock). "
                                          "Only UNIX is presently supported");
    object_class_property_add_str(klass, "device", NULL,
                                  vfu_object_set_device);
    object_class_property_set_description(klass, "device",
                                          "device ID - only PCI devices "
                                          "are presently supported");
}

static const TypeInfo vfu_object_info = {
    .name = TYPE_VFU_OBJECT,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(VfuObject),
    .instance_init = vfu_object_init,
    .instance_finalize = vfu_object_finalize,
    .class_size = sizeof(VfuObjectClass),
    .class_init = vfu_object_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void vfu_register_types(void)
{
    type_register_static(&vfu_object_info);
}

type_init(vfu_register_types);
