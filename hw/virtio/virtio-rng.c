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

#include "qemu/iov.h"
#include "hw/qdev.h"
#include "qapi/qmp/qerror.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-rng.h"
#include "qemu/rng.h"

static bool is_guest_ready(VirtIORNG *vrng)
{
    if (virtio_queue_ready(vrng->vq)
        && (vrng->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return true;
    }
    return false;
}

static size_t get_request_size(VirtQueue *vq, unsigned quota)
{
    unsigned int in, out;

    virtqueue_get_avail_bytes(vq, &in, &out, quota, 0);
    return in;
}

static void virtio_rng_process(VirtIORNG *vrng);

/* Send data from a char device over to the guest */
static void chr_read(void *opaque, const void *buf, size_t size)
{
    VirtIORNG *vrng = opaque;
    VirtQueueElement elem;
    size_t len;
    int offset;

    if (!is_guest_ready(vrng)) {
        return;
    }

    vrng->quota_remaining -= size;

    offset = 0;
    while (offset < size) {
        if (!virtqueue_pop(vrng->vq, &elem)) {
            break;
        }
        len = iov_from_buf(elem.in_sg, elem.in_num,
                           0, buf + offset, size - offset);
        offset += len;

        virtqueue_push(vrng->vq, &elem, len);
    }
    virtio_notify(&vrng->vdev, vrng->vq);
}

static void virtio_rng_process(VirtIORNG *vrng)
{
    size_t size;
    unsigned quota;

    if (!is_guest_ready(vrng)) {
        return;
    }

    if (vrng->quota_remaining < 0) {
        quota = 0;
    } else {
        quota = MIN((uint64_t)vrng->quota_remaining, (uint64_t)UINT32_MAX);
    }
    size = get_request_size(vrng->vq, quota);
    size = MIN(vrng->quota_remaining, size);
    if (size) {
        rng_backend_request_entropy(vrng->rng, size, chr_read, vrng);
    }
}

static void handle_input(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIORNG *vrng = DO_UPCAST(VirtIORNG, vdev, vdev);
    virtio_rng_process(vrng);
}

static uint32_t get_features(VirtIODevice *vdev, uint32_t f)
{
    return f;
}

static void virtio_rng_save(QEMUFile *f, void *opaque)
{
    VirtIORNG *vrng = opaque;

    virtio_save(&vrng->vdev, f);
}

static int virtio_rng_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIORNG *vrng = opaque;

    if (version_id != 1) {
        return -EINVAL;
    }
    virtio_load(&vrng->vdev, f);

    /* We may have an element ready but couldn't process it due to a quota
     * limit.  Make sure to try again after live migration when the quota may
     * have been reset.
     */
    virtio_rng_process(vrng);

    return 0;
}

static void check_rate_limit(void *opaque)
{
    VirtIORNG *s = opaque;

    s->quota_remaining = s->conf->max_bytes;
    virtio_rng_process(s);
    qemu_mod_timer(s->rate_limit_timer,
                   qemu_get_clock_ms(vm_clock) + s->conf->period_ms);
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

    assert(vrng->conf->max_bytes <= INT64_MAX);
    vrng->quota_remaining = vrng->conf->max_bytes;

    vrng->rate_limit_timer = qemu_new_timer_ms(vm_clock,
                                               check_rate_limit, vrng);

    qemu_mod_timer(vrng->rate_limit_timer,
                   qemu_get_clock_ms(vm_clock) + vrng->conf->period_ms);

    register_savevm(dev, "virtio-rng", -1, 1, virtio_rng_save,
                    virtio_rng_load, vrng);

    return vdev;
}

void virtio_rng_exit(VirtIODevice *vdev)
{
    VirtIORNG *vrng = DO_UPCAST(VirtIORNG, vdev, vdev);

    qemu_del_timer(vrng->rate_limit_timer);
    qemu_free_timer(vrng->rate_limit_timer);
    unregister_savevm(vrng->qdev, "virtio-rng", vrng);
    virtio_cleanup(vdev);
}
