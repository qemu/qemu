/*
 * QEMU Hyper-V VMBus
 *
 * Copyright (c) 2017-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_VMBUS_H
#define HW_HYPERV_VMBUS_H

#include "sysemu/sysemu.h"
#include "sysemu/dma.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"
#include "hw/hyperv/vmbus-proto.h"
#include "qemu/uuid.h"
#include "qom/object.h"

#define TYPE_VMBUS_DEVICE "vmbus-dev"

OBJECT_DECLARE_TYPE(VMBusDevice, VMBusDeviceClass,
                    VMBUS_DEVICE)

#define TYPE_VMBUS "vmbus"
OBJECT_DECLARE_SIMPLE_TYPE(VMBus, VMBUS)

/*
 * Object wrapping a GPADL -- GPA Descriptor List -- an array of guest physical
 * pages, to be used for various buffers shared between the host and the guest.
 */
typedef struct VMBusGpadl VMBusGpadl;
/*
 * VMBus channel -- a pair of ring buffers for either direction, placed within
 * one GPADL, and the associated notification means.
 */
typedef struct VMBusChannel VMBusChannel;
/*
 * Base class for VMBus devices.  Includes one or more channels.  Identified by
 * class GUID and instance GUID.
 */

typedef void(*VMBusChannelNotifyCb)(struct VMBusChannel *chan);

struct VMBusDeviceClass {
    DeviceClass parent;

    QemuUUID classid;
    QemuUUID instanceid;     /* Fixed UUID for singleton devices */
    uint16_t channel_flags;
    uint16_t mmio_size_mb;

    /* Extentions to standard device callbacks */
    void (*vmdev_realize)(VMBusDevice *vdev, Error **errp);
    void (*vmdev_unrealize)(VMBusDevice *vdev);
    void (*vmdev_reset)(VMBusDevice *vdev);
    /*
     * Calculate the number of channels based on the device properties.  Called
     * at realize time.
     **/
    uint16_t (*num_channels)(VMBusDevice *vdev);
    /*
     * Device-specific actions to complete the otherwise successful process of
     * opening a channel.
     * Return 0 on success, -errno on failure.
     */
    int (*open_channel)(VMBusChannel *chan);
    /*
     * Device-specific actions to perform before closing a channel.
     */
    void (*close_channel)(VMBusChannel *chan);
    /*
     * Main device worker; invoked in response to notifications from either
     * side, when there's work to do with the data in the channel ring buffers.
     */
    VMBusChannelNotifyCb chan_notify_cb;
};

struct VMBusDevice {
    DeviceState parent;
    QemuUUID instanceid;
    uint16_t num_channels;
    VMBusChannel *channels;
    AddressSpace *dma_as;
};

extern const VMStateDescription vmstate_vmbus_dev;

/*
 * A unit of work parsed out of a message in the receive (i.e. guest->host)
 * ring buffer of a channel.  It's supposed to be subclassed (through
 * embedding) by the specific devices.
 */
typedef struct VMBusChanReq {
    VMBusChannel *chan;
    uint16_t pkt_type;
    uint32_t msglen;
    void *msg;
    uint64_t transaction_id;
    bool need_comp;
    QEMUSGList sgl;
} VMBusChanReq;

VMBusDevice *vmbus_channel_device(VMBusChannel *chan);
VMBusChannel *vmbus_device_channel(VMBusDevice *dev, uint32_t chan_idx);
uint32_t vmbus_channel_idx(VMBusChannel *chan);
bool vmbus_channel_is_open(VMBusChannel *chan);

/*
 * Notify (on guest's behalf) the host side of the channel that there's data in
 * the ringbuffer to process.
 */
void vmbus_channel_notify_host(VMBusChannel *chan);

/*
 * Reserve space for a packet in the send (i.e. host->guest) ringbuffer.  If
 * there isn't enough room, indicate that to the guest, to be notified when it
 * becomes available.
 * Return 0 on success, negative errno on failure.
 * The ringbuffer indices are NOT updated, the requested space indicator may.
 */
int vmbus_channel_reserve(VMBusChannel *chan,
                          uint32_t desclen, uint32_t msglen);

/*
 * Send a packet to the guest.  The space for the packet MUST be reserved
 * first.
 * Return total number of bytes placed in the send ringbuffer on success,
 * negative errno on failure.
 * The ringbuffer indices are updated on success, and the guest is signaled if
 * needed.
 */
ssize_t vmbus_channel_send(VMBusChannel *chan, uint16_t pkt_type,
                           void *desc, uint32_t desclen,
                           void *msg, uint32_t msglen,
                           bool need_comp, uint64_t transaction_id);

/*
 * Prepare to fetch a batch of packets from the receive ring buffer.
 * Return 0 on success, negative errno on failure.
 */
int vmbus_channel_recv_start(VMBusChannel *chan);

/*
 * Shortcut for a common case of sending a simple completion packet with no
 * auxiliary descriptors.
 */
ssize_t vmbus_channel_send_completion(VMBusChanReq *req,
                                      void *msg, uint32_t msglen);

/*
 * Peek at the receive (i.e. guest->host) ring buffer and extract a unit of
 * work (a device-specific subclass of VMBusChanReq) from a packet if there's
 * one.
 * Return an allocated buffer, containing the request of @size with filled
 * VMBusChanReq at the beginning, followed by the message payload, or NULL on
 * failure.
 * The ringbuffer indices are NOT updated, nor is the private copy of the read
 * index.
 */
void *vmbus_channel_recv_peek(VMBusChannel *chan, uint32_t size);

/*
 * Update the private copy of the read index once the preceding peek is deemed
 * successful.
 * The ringbuffer indices are NOT updated.
 */
void vmbus_channel_recv_pop(VMBusChannel *chan);

/*
 * Propagate the private copy of the read index into the receive ring buffer,
 * and thus complete the reception of a series of packets.  Notify guest if
 * needed.
 * Return the number of bytes popped off the receive ring buffer by the
 * preceding recv_peek/recv_pop calls on success, negative errno on failure.
 */
ssize_t vmbus_channel_recv_done(VMBusChannel *chan);

/*
 * Free the request allocated by vmbus_channel_recv_peek, together with its
 * fields.
 */
void vmbus_free_req(void *req);

/*
 * Find and reference a GPADL by @gpadl_id.
 * If not found return NULL.
 */
VMBusGpadl *vmbus_get_gpadl(VMBusChannel *chan, uint32_t gpadl_id);

/*
 * Unreference @gpadl.  If the reference count drops to zero, free it.
 * @gpadl may be NULL, in which case nothing is done.
 */
void vmbus_put_gpadl(VMBusGpadl *gpadl);

/*
 * Calculate total length in bytes of @gpadl.
 * @gpadl must be valid.
 */
uint32_t vmbus_gpadl_len(VMBusGpadl *gpadl);

/*
 * Copy data from @iov to @gpadl at offset @off.
 * Return the number of bytes copied, or a negative status on failure.
 */
ssize_t vmbus_iov_to_gpadl(VMBusChannel *chan, VMBusGpadl *gpadl, uint32_t off,
                           const struct iovec *iov, size_t iov_cnt);

/*
 * Map SGList contained in the request @req, at offset @off and no more than
 * @len bytes, for io in direction @dir, and populate @iov with the mapped
 * iovecs.
 * Return the number of iovecs mapped, or negative status on failure.
 */
int vmbus_map_sgl(VMBusChanReq *req, DMADirection dir, struct iovec *iov,
                  unsigned iov_cnt, size_t len, size_t off);

/*
 * Unmap *iov mapped with vmbus_map_sgl, marking the number of bytes @accessed.
 */
void vmbus_unmap_sgl(VMBusChanReq *req, DMADirection dir, struct iovec *iov,
                     unsigned iov_cnt, size_t accessed);

#endif
