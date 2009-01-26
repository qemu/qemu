/*
 * Virtio Console Device
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "hw.h"
#include "qemu-char.h"
#include "virtio.h"
#include "virtio-console.h"


typedef struct VirtIOConsole
{
    VirtIODevice vdev;
    VirtQueue *ivq, *dvq;
    CharDriverState *chr;
} VirtIOConsole;

static VirtIOConsole *to_virtio_console(VirtIODevice *vdev)
{
    return (VirtIOConsole *)vdev;
}

static void virtio_console_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOConsole *s = to_virtio_console(vdev);
    VirtQueueElement elem;

    while (virtqueue_pop(vq, &elem)) {
        ssize_t len = 0;
        int d;

        for (d=0; d < elem.out_num; d++)
            len += qemu_chr_write(s->chr, elem.out_sg[d].iov_base,elem.out_sg[d].iov_len);
        virtqueue_push(vq, &elem, len);
        virtio_notify(vdev, vq);
    }
}

static void virtio_console_handle_input(VirtIODevice *vdev, VirtQueue *vq)
{
}

static uint32_t virtio_console_get_features(VirtIODevice *vdev)
{
    return 0;
}

static int vcon_can_read(void *opaque)
{
    VirtIOConsole *s = (VirtIOConsole *) opaque;

    if (!virtio_queue_ready(s->ivq) ||
        !(s->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK) ||
        virtio_queue_empty(s->ivq))
        return 0;

    /* current implementations have a page sized buffer.
     * We fall back to a one byte per read if there is not enough room.
     * It would be cool to have a function that returns the available byte
     * instead of checking for a limit */
    if (virtqueue_avail_bytes(s->ivq, TARGET_PAGE_SIZE, 0))
        return TARGET_PAGE_SIZE;
    if (virtqueue_avail_bytes(s->ivq, 1, 0))
        return 1;
    return 0;
}

static void vcon_read(void *opaque, const uint8_t *buf, int size)
{
    VirtIOConsole *s = (VirtIOConsole *) opaque;
    VirtQueueElement elem;
    int offset = 0;

    /* The current kernel implementation has only one outstanding input
     * buffer of PAGE_SIZE. Nevertheless, this function is prepared to
     * handle multiple buffers with multiple sg element for input */
    while (offset < size) {
        int i = 0;
        if (!virtqueue_pop(s->ivq, &elem))
                break;
        while (offset < size && i < elem.in_num) {
            int len = MIN(elem.in_sg[i].iov_len, size - offset);
            memcpy(elem.in_sg[i].iov_base, buf + offset, len);
            offset += len;
            i++;
        }
        virtqueue_push(s->ivq, &elem, size);
    }
    virtio_notify(&s->vdev, s->ivq);
}

static void vcon_event(void *opaque, int event)
{
    /* we will ignore any event for the time being */
}

static void virtio_console_save(QEMUFile *f, void *opaque)
{
    VirtIOConsole *s = opaque;

    virtio_save(&s->vdev, f);
}

static int virtio_console_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIOConsole *s = opaque;

    if (version_id != 1)
        return -EINVAL;

    virtio_load(&s->vdev, f);
    return 0;
}

void *virtio_console_init(PCIBus *bus, CharDriverState *chr)
{
    VirtIOConsole *s;

    s = (VirtIOConsole *)virtio_init_pci(bus, "virtio-console",
                                         PCI_VENDOR_ID_REDHAT_QUMRANET,
                                         PCI_DEVICE_ID_VIRTIO_CONSOLE,
                                         PCI_VENDOR_ID_REDHAT_QUMRANET,
                                         VIRTIO_ID_CONSOLE,
                                         0x03, 0x80, 0x00,
                                         0, sizeof(VirtIOConsole));
    if (s == NULL)
        return NULL;

    s->vdev.get_features = virtio_console_get_features;

    s->ivq = virtio_add_queue(&s->vdev, 128, virtio_console_handle_input);
    s->dvq = virtio_add_queue(&s->vdev, 128, virtio_console_handle_output);

    s->chr = chr;
    qemu_chr_add_handlers(chr, vcon_can_read, vcon_read, vcon_event, s);

    register_savevm("virtio-console", -1, 1, virtio_console_save, virtio_console_load, s);

    return &s->vdev;
}
