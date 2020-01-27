/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/sockets.h"

#include "hw/virtio/virtio.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-input.h"

#include <sys/ioctl.h>
#include "standard-headers/linux/input.h"

/* ----------------------------------------------------------------- */

static struct virtio_input_config virtio_input_host_config[] = {
    { /* empty list */ },
};

static void virtio_input_host_event(void *opaque)
{
    VirtIOInputHost *vih = opaque;
    VirtIOInput *vinput = VIRTIO_INPUT(vih);
    struct virtio_input_event virtio;
    struct input_event evdev;
    int rc;

    for (;;) {
        rc = read(vih->fd, &evdev, sizeof(evdev));
        if (rc != sizeof(evdev)) {
            break;
        }

        virtio.type  = cpu_to_le16(evdev.type);
        virtio.code  = cpu_to_le16(evdev.code);
        virtio.value = cpu_to_le32(evdev.value);
        virtio_input_send(vinput, &virtio);
    }
}

static void virtio_input_bits_config(VirtIOInputHost *vih,
                                     int type, int count)
{
    virtio_input_config bits;
    int rc, i, size = 0;

    memset(&bits, 0, sizeof(bits));
    rc = ioctl(vih->fd, EVIOCGBIT(type, count/8), bits.u.bitmap);
    if (rc < 0) {
        return;
    }

    for (i = 0; i < count/8; i++) {
        if (bits.u.bitmap[i]) {
            size = i+1;
        }
    }
    if (size == 0) {
        return;
    }

    bits.select = VIRTIO_INPUT_CFG_EV_BITS;
    bits.subsel = type;
    bits.size   = size;
    virtio_input_add_config(VIRTIO_INPUT(vih), &bits);
}

static void virtio_input_abs_config(VirtIOInputHost *vih, int axis)
{
    virtio_input_config config;
    struct input_absinfo absinfo;
    int rc;

    rc = ioctl(vih->fd, EVIOCGABS(axis), &absinfo);
    if (rc < 0) {
        return;
    }

    memset(&config, 0, sizeof(config));
    config.select = VIRTIO_INPUT_CFG_ABS_INFO;
    config.subsel = axis;
    config.size   = sizeof(virtio_input_absinfo);

    config.u.abs.min  = cpu_to_le32(absinfo.minimum);
    config.u.abs.max  = cpu_to_le32(absinfo.maximum);
    config.u.abs.fuzz = cpu_to_le32(absinfo.fuzz);
    config.u.abs.flat = cpu_to_le32(absinfo.flat);
    config.u.abs.res  = cpu_to_le32(absinfo.resolution);

    virtio_input_add_config(VIRTIO_INPUT(vih), &config);
}

static void virtio_input_host_realize(DeviceState *dev, Error **errp)
{
    VirtIOInputHost *vih = VIRTIO_INPUT_HOST(dev);
    VirtIOInput *vinput = VIRTIO_INPUT(dev);
    virtio_input_config id, *abs;
    struct input_id ids;
    int rc, ver, i, axis;
    uint8_t byte;

    if (!vih->evdev) {
        error_setg(errp, "evdev property is required");
        return;
    }

    vih->fd = open(vih->evdev, O_RDWR);
    if (vih->fd < 0)  {
        error_setg_file_open(errp, errno, vih->evdev);
        return;
    }
    qemu_set_nonblock(vih->fd);

    rc = ioctl(vih->fd, EVIOCGVERSION, &ver);
    if (rc < 0) {
        error_setg(errp, "%s: is not an evdev device", vih->evdev);
        goto err_close;
    }

    rc = ioctl(vih->fd, EVIOCGRAB, 1);
    if (rc < 0) {
        error_setg_errno(errp, errno, "%s: failed to get exclusive access",
                         vih->evdev);
        goto err_close;
    }

    memset(&id, 0, sizeof(id));
    ioctl(vih->fd, EVIOCGNAME(sizeof(id.u.string)-1), id.u.string);
    id.select = VIRTIO_INPUT_CFG_ID_NAME;
    id.size = strlen(id.u.string);
    virtio_input_add_config(vinput, &id);

    if (ioctl(vih->fd, EVIOCGID, &ids) == 0) {
        memset(&id, 0, sizeof(id));
        id.select = VIRTIO_INPUT_CFG_ID_DEVIDS;
        id.size = sizeof(struct virtio_input_devids);
        id.u.ids.bustype = cpu_to_le16(ids.bustype);
        id.u.ids.vendor  = cpu_to_le16(ids.vendor);
        id.u.ids.product = cpu_to_le16(ids.product);
        id.u.ids.version = cpu_to_le16(ids.version);
        virtio_input_add_config(vinput, &id);
    }

    virtio_input_bits_config(vih, EV_KEY, KEY_CNT);
    virtio_input_bits_config(vih, EV_REL, REL_CNT);
    virtio_input_bits_config(vih, EV_ABS, ABS_CNT);
    virtio_input_bits_config(vih, EV_MSC, MSC_CNT);
    virtio_input_bits_config(vih, EV_SW,  SW_CNT);
    virtio_input_bits_config(vih, EV_LED, LED_CNT);

    abs = virtio_input_find_config(VIRTIO_INPUT(vih),
        VIRTIO_INPUT_CFG_EV_BITS, EV_ABS);
    if (abs) {
        for (i = 0; i < abs->size; i++) {
            byte = abs->u.bitmap[i];
            axis = 8 * i;
            while (byte) {
                if (byte & 1) {
                    virtio_input_abs_config(vih, axis);
                }
                axis++;
                byte >>= 1;
            }
        }
    }

    qemu_set_fd_handler(vih->fd, virtio_input_host_event, NULL, vih);
    return;

err_close:
    close(vih->fd);
    vih->fd = -1;
    return;
}

static void virtio_input_host_unrealize(DeviceState *dev, Error **errp)
{
    VirtIOInputHost *vih = VIRTIO_INPUT_HOST(dev);

    if (vih->fd > 0) {
        qemu_set_fd_handler(vih->fd, NULL, NULL, NULL);
        close(vih->fd);
    }
}

static void virtio_input_host_handle_status(VirtIOInput *vinput,
                                            virtio_input_event *event)
{
    VirtIOInputHost *vih = VIRTIO_INPUT_HOST(vinput);
    struct input_event evdev;
    int rc;

    if (gettimeofday(&evdev.time, NULL)) {
        perror("virtio_input_host_handle_status: gettimeofday");
        return;
    }

    evdev.type = le16_to_cpu(event->type);
    evdev.code = le16_to_cpu(event->code);
    evdev.value = le32_to_cpu(event->value);

    rc = write(vih->fd, &evdev, sizeof(evdev));
    if (rc == -1) {
        perror("virtio_input_host_handle_status: write");
    }
}

static const VMStateDescription vmstate_virtio_input_host = {
    .name = "virtio-input-host",
    .unmigratable = 1,
};

static Property virtio_input_host_properties[] = {
    DEFINE_PROP_STRING("evdev", VirtIOInputHost, evdev),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_input_host_class_init(ObjectClass *klass, void *data)
{
    VirtIOInputClass *vic = VIRTIO_INPUT_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd           = &vmstate_virtio_input_host;
    device_class_set_props(dc, virtio_input_host_properties);
    vic->realize       = virtio_input_host_realize;
    vic->unrealize     = virtio_input_host_unrealize;
    vic->handle_status = virtio_input_host_handle_status;
}

static void virtio_input_host_init(Object *obj)
{
    VirtIOInput *vinput = VIRTIO_INPUT(obj);

    virtio_input_init_config(vinput, virtio_input_host_config);
}

static const TypeInfo virtio_input_host_info = {
    .name          = TYPE_VIRTIO_INPUT_HOST,
    .parent        = TYPE_VIRTIO_INPUT,
    .instance_size = sizeof(VirtIOInputHost),
    .instance_init = virtio_input_host_init,
    .class_init    = virtio_input_host_class_init,
};

/* ----------------------------------------------------------------- */

static void virtio_register_types(void)
{
    type_register_static(&virtio_input_host_info);
}

type_init(virtio_register_types)
