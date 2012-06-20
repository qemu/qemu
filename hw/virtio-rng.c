/*
 * A virtio device implementing a hardware random number generator.
 *
 * Copyright 2012 Red Hat, Inc.
 * Copyright 2012 Amit Shah <amit.shah@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "iov.h"
#include "qdev.h"
#include "virtio.h"
#include "virtio-rng.h"
#include "qemu/rng.h"

typedef struct VirtIORNG {
    VirtIODevice vdev;

    DeviceState *qdev;

    /* Only one vq - guest puts buffer(s) on it when it needs entropy */
    VirtQueue *vq;
    VirtQueueElement elem;

    /* Config data for the device -- currently only chardev */
    VirtIORNGConf *conf;

    /* Whether we've popped a vq element into 'elem' above */
    bool popped;

    RngBackend *rng;
} VirtIORNG;

static bool is_guest_ready(VirtIORNG *vrng)
{
    if (virtio_queue_ready(vrng->vq)
        && (vrng->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return true;
    }
    return false;
}

static size_t pop_an_elem(VirtIORNG *vrng)
{
    size_t size;

    if (!vrng->popped && !virtqueue_pop(vrng->vq, &vrng->elem)) {
        return 0;
    }
    vrng->popped = true;

    size = iov_size(vrng->elem.in_sg, vrng->elem.in_num);
    return size;
}

/* Send data from a char device over to the guest */
static void chr_read(void *opaque, const void *buf, size_t size)
{
    VirtIORNG *vrng = opaque;
    size_t len;
    int offset;

    if (!is_guest_ready(vrng)) {
        return;
    }

    offset = 0;
    while (offset < size) {
        if (!pop_an_elem(vrng)) {
            break;
        }
        len = iov_from_buf(vrng->elem.in_sg, vrng->elem.in_num,
                           0, buf + offset, size - offset);
        offset += len;

        virtqueue_push(vrng->vq, &vrng->elem, len);
        vrng->popped = false;
    }
    virtio_notify(&vrng->vdev, vrng->vq);

    /*
     * Lastly, if we had multiple elems queued by the guest, and we
     * didn't have enough data to fill them all, indicate we want more
     * data.
     */
    len = pop_an_elem(vrng);
    if (len) {
        rng_backend_request_entropy(vrng->rng, size, chr_read, vrng);
    }
}

static void handle_input(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIORNG *vrng = DO_UPCAST(VirtIORNG, vdev, vdev);
    size_t size;

    size = pop_an_elem(vrng);
    if (size) {
        rng_backend_request_entropy(vrng->rng, size, chr_read, vrng);
    }
}

static uint32_t get_features(VirtIODevice *vdev, uint32_t f)
{
    return f;
}

static void virtio_rng_save(QEMUFile *f, void *opaque)
{
    VirtIORNG *vrng = opaque;

    virtio_save(&vrng->vdev, f);

    qemu_put_byte(f, vrng->popped);
    if (vrng->popped) {
        int i;

        qemu_put_be32(f, vrng->elem.index);

        qemu_put_be32(f, vrng->elem.in_num);
        for (i = 0; i < vrng->elem.in_num; i++) {
            qemu_put_be64(f, vrng->elem.in_addr[i]);
        }

        qemu_put_be32(f, vrng->elem.out_num);
        for (i = 0; i < vrng->elem.out_num; i++) {
            qemu_put_be64(f, vrng->elem.out_addr[i]);
        }
    }
}

static int virtio_rng_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIORNG *vrng = opaque;

    if (version_id != 1) {
        return -EINVAL;
    }
    virtio_load(&vrng->vdev, f);

    vrng->popped = qemu_get_byte(f);
    if (vrng->popped) {
        int i;

        vrng->elem.index = qemu_get_be32(f);

        vrng->elem.in_num = qemu_get_be32(f);
        g_assert(vrng->elem.in_num < VIRTQUEUE_MAX_SIZE);
        for (i = 0; i < vrng->elem.in_num; i++) {
            vrng->elem.in_addr[i] = qemu_get_be64(f);
        }

        vrng->elem.out_num = qemu_get_be32(f);
        g_assert(vrng->elem.out_num < VIRTQUEUE_MAX_SIZE);
        for (i = 0; i < vrng->elem.out_num; i++) {
            vrng->elem.out_addr[i] = qemu_get_be64(f);
        }

        virtqueue_map_sg(vrng->elem.in_sg, vrng->elem.in_addr,
                         vrng->elem.in_num, 1);
        virtqueue_map_sg(vrng->elem.out_sg, vrng->elem.out_addr,
                         vrng->elem.out_num, 0);
    }
    return 0;
}

VirtIODevice *virtio_rng_init(DeviceState *dev, VirtIORNGConf *conf)
{
    VirtIORNG *vrng;
    VirtIODevice *vdev;
    Error *local_err = NULL;

    vdev = virtio_common_init("virtio-rng", VIRTIO_ID_RNG, 0,
                              sizeof(VirtIORNG));

    vrng = DO_UPCAST(VirtIORNG, vdev, vdev);

    vrng->rng = conf->rng;
    if (vrng->rng == NULL) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "rng", "a valid object");
        return NULL;
    }

    rng_backend_open(vrng->rng, &local_err);
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return NULL;
    }

    vrng->vq = virtio_add_queue(vdev, 8, handle_input);
    vrng->vdev.get_features = get_features;

    vrng->qdev = dev;
    vrng->conf = conf;
    vrng->popped = false;
    register_savevm(dev, "virtio-rng", -1, 1, virtio_rng_save,
                    virtio_rng_load, vrng);

    return vdev;
}

void virtio_rng_exit(VirtIODevice *vdev)
{
    VirtIORNG *vrng = DO_UPCAST(VirtIORNG, vdev, vdev);

    unregister_savevm(vrng->qdev, "virtio-rng", vrng);
    virtio_cleanup(vdev);
}
