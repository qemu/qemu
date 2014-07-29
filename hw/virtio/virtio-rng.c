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
#include "sysemu/rng.h"
#include "qom/object_interfaces.h"

static bool is_guest_ready(VirtIORNG *vrng)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vrng);
    if (virtio_queue_ready(vrng->vq)
        && (vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
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
    VirtIODevice *vdev = VIRTIO_DEVICE(vrng);
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
    virtio_notify(vdev, vrng->vq);
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
    VirtIORNG *vrng = VIRTIO_RNG(vdev);
    virtio_rng_process(vrng);
}

static uint32_t get_features(VirtIODevice *vdev, uint32_t f)
{
    return f;
}

static void virtio_rng_save(QEMUFile *f, void *opaque)
{
    VirtIODevice *vdev = opaque;

    virtio_save(vdev, f);
}

static int virtio_rng_load(QEMUFile *f, void *opaque, int version_id)
{
    if (version_id != 1) {
        return -EINVAL;
    }
    return virtio_load(VIRTIO_DEVICE(opaque), f, version_id);
}

static int virtio_rng_load_device(VirtIODevice *vdev, QEMUFile *f,
                                  int version_id)
{
    /* We may have an element ready but couldn't process it due to a quota
     * limit.  Make sure to try again after live migration when the quota may
     * have been reset.
     */
    virtio_rng_process(VIRTIO_RNG(vdev));

    return 0;
}

static void check_rate_limit(void *opaque)
{
    VirtIORNG *vrng = opaque;

    vrng->quota_remaining = vrng->conf.max_bytes;
    virtio_rng_process(vrng);
    timer_mod(vrng->rate_limit_timer,
                   qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + vrng->conf.period_ms);
}

static void virtio_rng_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIORNG *vrng = VIRTIO_RNG(dev);
    Error *local_err = NULL;

    if (!vrng->conf.period_ms > 0) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "period",
                  "a positive number");
        return;
    }

    /* Workaround: Property parsing does not enforce unsigned integers,
     * So this is a hack to reject such numbers. */
    if (vrng->conf.max_bytes > INT64_MAX) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "max-bytes",
                  "a non-negative integer below 2^63");
        return;
    }

    if (vrng->conf.rng == NULL) {
        vrng->conf.default_backend = RNG_RANDOM(object_new(TYPE_RNG_RANDOM));

        user_creatable_complete(OBJECT(vrng->conf.default_backend),
                                &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            object_unref(OBJECT(vrng->conf.default_backend));
            return;
        }

        object_property_add_child(OBJECT(dev),
                                  "default-backend",
                                  OBJECT(vrng->conf.default_backend),
                                  NULL);

        /* The child property took a reference, we can safely drop ours now */
        object_unref(OBJECT(vrng->conf.default_backend));

        object_property_set_link(OBJECT(dev),
                                 OBJECT(vrng->conf.default_backend),
                                 "rng", NULL);
    }

    vrng->rng = vrng->conf.rng;
    if (vrng->rng == NULL) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "rng", "a valid object");
        return;
    }

    virtio_init(vdev, "virtio-rng", VIRTIO_ID_RNG, 0);

    vrng->vq = virtio_add_queue(vdev, 8, handle_input);
    vrng->quota_remaining = vrng->conf.max_bytes;

    vrng->rate_limit_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                               check_rate_limit, vrng);

    timer_mod(vrng->rate_limit_timer,
                   qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + vrng->conf.period_ms);

    register_savevm(dev, "virtio-rng", -1, 1, virtio_rng_save,
                    virtio_rng_load, vrng);
}

static void virtio_rng_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIORNG *vrng = VIRTIO_RNG(dev);

    timer_del(vrng->rate_limit_timer);
    timer_free(vrng->rate_limit_timer);
    unregister_savevm(dev, "virtio-rng", vrng);
    virtio_cleanup(vdev);
}

static Property virtio_rng_properties[] = {
    DEFINE_VIRTIO_RNG_PROPERTIES(VirtIORNG, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = virtio_rng_properties;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_rng_device_realize;
    vdc->unrealize = virtio_rng_device_unrealize;
    vdc->get_features = get_features;
    vdc->load = virtio_rng_load_device;
}

static void virtio_rng_initfn(Object *obj)
{
    VirtIORNG *vrng = VIRTIO_RNG(obj);

    object_property_add_link(obj, "rng", TYPE_RNG_BACKEND,
                             (Object **)&vrng->conf.rng,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE, NULL);
}

static const TypeInfo virtio_rng_info = {
    .name = TYPE_VIRTIO_RNG,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIORNG),
    .instance_init = virtio_rng_initfn,
    .class_init = virtio_rng_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_rng_info);
}

type_init(virtio_register_types)
