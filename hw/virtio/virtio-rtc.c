/*
 * Virtio RTC device core
 *
 * Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/timer.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-rtc.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_rtc.h"

static void virtio_rtc_handle_request(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtQueueElement *elem;
    struct virtio_rtc_req_head req_head;
    size_t written;

    while ((elem = virtqueue_pop(vq, sizeof(VirtQueueElement)))) {
        if (elem->out_num < 1 || elem->in_num < 1) {
            virtio_error(vdev, "virtio-rtc: request missing in/out buffers");
            virtqueue_detach_element(vq, elem, 0);
            g_free(elem);
            break;
        }

        if (iov_to_buf(elem->out_sg, elem->out_num, 0, &req_head,
                       sizeof(req_head)) != sizeof(req_head)) {
            virtio_error(vdev, "virtio-rtc: request header too short");
            virtqueue_detach_element(vq, elem, 0);
            g_free(elem);
            break;
        }

        written = 0;

        switch (le16_to_cpu(req_head.msg_type)) {
        case VIRTIO_RTC_REQ_CFG: {
            struct virtio_rtc_resp_cfg resp = {0};
            resp.head.status = VIRTIO_RTC_S_OK;
            resp.num_clocks = cpu_to_le16(1);
            written = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp,
                                   sizeof(resp));
            break;
        }
        case VIRTIO_RTC_REQ_CLOCK_CAP: {
            struct virtio_rtc_req_clock_cap req;
            struct virtio_rtc_resp_clock_cap resp = {0};

            if (iov_to_buf(elem->out_sg, elem->out_num, 0, &req,
                           sizeof(req)) != sizeof(req)) {
                resp.head.status = VIRTIO_RTC_S_EINVAL;
                written = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp,
                                       sizeof(resp.head));
                break;
            }

            if (le16_to_cpu(req.clock_id) != 0) {
                resp.head.status = VIRTIO_RTC_S_ENODEV;
            } else {
                resp.head.status = VIRTIO_RTC_S_OK;
                resp.type = VIRTIO_RTC_CLOCK_UTC;
            }
            written = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp,
                                   sizeof(resp));
            break;
        }
        case VIRTIO_RTC_REQ_CROSS_CAP: {
            struct virtio_rtc_req_cross_cap req;
            struct virtio_rtc_resp_cross_cap resp = {0};

            if (iov_to_buf(elem->out_sg, elem->out_num, 0, &req,
                     sizeof(req)) != sizeof(req)) {
                resp.head.status = VIRTIO_RTC_S_EINVAL;
                written = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp,
                                       sizeof(resp.head));
                break;
            }

            if (le16_to_cpu(req.clock_id) != 0) {
                resp.head.status = VIRTIO_RTC_S_ENODEV;
            } else {
                resp.head.status = VIRTIO_RTC_S_OK;
            }
            written = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp,
                                   sizeof(resp));
            break;
        }
        case VIRTIO_RTC_REQ_READ: {
            struct virtio_rtc_req_read req;
            struct virtio_rtc_resp_read resp = {0};

            if (iov_to_buf(elem->out_sg, elem->out_num, 0, &req,
                           sizeof(req)) != sizeof(req)) {
                resp.head.status = VIRTIO_RTC_S_EINVAL;
                written = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp,
                                       sizeof(resp.head));
                break;
            }

            if (le16_to_cpu(req.clock_id) != 0) {
                resp.head.status = VIRTIO_RTC_S_ENODEV;
                written = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp,
                                       sizeof(resp.head));
            } else {
                resp.head.status = VIRTIO_RTC_S_OK;
                resp.clock_reading =
                    cpu_to_le64(qemu_clock_get_ns(QEMU_CLOCK_HOST));
                written = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp,
                                       sizeof(resp));
            }
            break;
        }
        default: {
            struct virtio_rtc_resp_head resp = {0};
            resp.status = VIRTIO_RTC_S_EOPNOTSUPP;
            written = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp,
                                   sizeof(resp));
            break;
        }
        }

        virtqueue_push(vq, elem, written);
        virtio_notify(vdev, vq);
        g_free(elem);
    }
}

static void virtio_rtc_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIORtc *vrtc = VIRTIO_RTC(dev);

    virtio_init(vdev, VIRTIO_ID_CLOCK, 0);
    vrtc->vq = virtio_add_queue(vdev, 64, virtio_rtc_handle_request);
}

static void virtio_rtc_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIORtc *vrtc = VIRTIO_RTC(dev);

    virtio_delete_queue(vrtc->vq);
    virtio_cleanup(vdev);
}

static uint64_t virtio_rtc_get_features(VirtIODevice *vdev, uint64_t f,
                                        Error **errp)
{
    return f;
}

static const VMStateDescription vmstate_virtio_rtc = {
    .name = "virtio-rtc",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static void virtio_rtc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_rtc_device_realize;
    vdc->unrealize = virtio_rtc_device_unrealize;
    vdc->get_features = virtio_rtc_get_features;
    dc->vmsd = &vmstate_virtio_rtc;
}

static const TypeInfo virtio_rtc_info = {
    .name = TYPE_VIRTIO_RTC,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIORtc),
    .class_init = virtio_rtc_class_init,
};

static void virtio_rtc_register_types(void)
{
    type_register_static(&virtio_rtc_info);
}

type_init(virtio_rtc_register_types)
