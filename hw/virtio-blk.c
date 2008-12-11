/*
 * Virtio Block Device
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "virtio-blk.h"
#include "block_int.h"

typedef struct VirtIOBlock
{
    VirtIODevice vdev;
    BlockDriverState *bs;
    VirtQueue *vq;
} VirtIOBlock;

static VirtIOBlock *to_virtio_blk(VirtIODevice *vdev)
{
    return (VirtIOBlock *)vdev;
}

typedef struct VirtIOBlockReq
{
    VirtIOBlock *dev;
    VirtQueueElement elem;
    struct virtio_blk_inhdr *in;
    struct virtio_blk_outhdr *out;
    size_t size;
    uint8_t *buffer;
} VirtIOBlockReq;

static void virtio_blk_rw_complete(void *opaque, int ret)
{
    VirtIOBlockReq *req = opaque;
    VirtIOBlock *s = req->dev;

    /* Copy read data to the guest */
    if (!ret && !(req->out->type & VIRTIO_BLK_T_OUT)) {
        size_t offset = 0;
        int i;

        for (i = 0; i < req->elem.in_num - 1; i++) {
            size_t len;

            /* Be pretty defensive wrt malicious guests */
            len = MIN(req->elem.in_sg[i].iov_len,
                      req->size - offset);

            memcpy(req->elem.in_sg[i].iov_base,
                   req->buffer + offset,
                   len);
            offset += len;
        }
    }

    req->in->status = ret ? VIRTIO_BLK_S_IOERR : VIRTIO_BLK_S_OK;
    virtqueue_push(s->vq, &req->elem, req->size + sizeof(*req->in));
    virtio_notify(&s->vdev, s->vq);

    qemu_free(req->buffer);
    qemu_free(req);
}

static VirtIOBlockReq *virtio_blk_get_request(VirtIOBlock *s)
{
    VirtIOBlockReq *req;

    req = qemu_mallocz(sizeof(*req));
    if (req == NULL)
        return NULL;

    req->dev = s;
    if (!virtqueue_pop(s->vq, &req->elem)) {
        qemu_free(req);
        return NULL;
    }

    return req;
}

static void virtio_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBlock *s = to_virtio_blk(vdev);
    VirtIOBlockReq *req;

    while ((req = virtio_blk_get_request(s))) {
        int i;

        if (req->elem.out_num < 1 || req->elem.in_num < 1) {
            fprintf(stderr, "virtio-blk missing headers\n");
            exit(1);
        }

        if (req->elem.out_sg[0].iov_len < sizeof(*req->out) ||
            req->elem.in_sg[req->elem.in_num - 1].iov_len < sizeof(*req->in)) {
            fprintf(stderr, "virtio-blk header not in correct element\n");
            exit(1);
        }

        req->out = (void *)req->elem.out_sg[0].iov_base;
        req->in = (void *)req->elem.in_sg[req->elem.in_num - 1].iov_base;

        if (req->out->type & VIRTIO_BLK_T_SCSI_CMD) {
            unsigned int len = sizeof(*req->in);

            req->in->status = VIRTIO_BLK_S_UNSUPP;
            virtqueue_push(vq, &req->elem, len);
            virtio_notify(vdev, vq);
            qemu_free(req);
        } else if (req->out->type & VIRTIO_BLK_T_OUT) {
            size_t offset;

            for (i = 1; i < req->elem.out_num; i++)
                req->size += req->elem.out_sg[i].iov_len;

            req->buffer = qemu_memalign(512, req->size);
            if (req->buffer == NULL) {
                qemu_free(req);
                break;
            }

            /* We copy the data from the SG list to avoid splitting up the request.  This helps
               performance a lot until we can pass full sg lists as AIO operations */
            offset = 0;
            for (i = 1; i < req->elem.out_num; i++) {
                size_t len;

                len = MIN(req->elem.out_sg[i].iov_len,
                          req->size - offset);
                memcpy(req->buffer + offset,
                       req->elem.out_sg[i].iov_base,
                       len);
                offset += len;
            }

            bdrv_aio_write(s->bs, req->out->sector,
                           req->buffer,
                           req->size / 512,
                           virtio_blk_rw_complete,
                           req);
        } else {
            for (i = 0; i < req->elem.in_num - 1; i++)
                req->size += req->elem.in_sg[i].iov_len;

            req->buffer = qemu_memalign(512, req->size);
            if (req->buffer == NULL) {
                qemu_free(req);
                break;
            }

            bdrv_aio_read(s->bs, req->out->sector,
                          req->buffer,
                          req->size / 512,
                          virtio_blk_rw_complete,
                          req);
        }
    }
    /*
     * FIXME: Want to check for completions before returning to guest mode,
     * so cached reads and writes are reported as quickly as possible. But
     * that should be done in the generic block layer.
     */
}

static void virtio_blk_reset(VirtIODevice *vdev)
{
    /*
     * This should cancel pending requests, but can't do nicely until there
     * are per-device request lists.
     */
    qemu_aio_flush();
}

static void virtio_blk_update_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOBlock *s = to_virtio_blk(vdev);
    struct virtio_blk_config blkcfg;
    uint64_t capacity;
    int cylinders, heads, secs;

    bdrv_get_geometry(s->bs, &capacity);
    bdrv_get_geometry_hint(s->bs, &cylinders, &heads, &secs);
    stq_raw(&blkcfg.capacity, capacity);
    stl_raw(&blkcfg.seg_max, 128 - 2);
    stw_raw(&blkcfg.cylinders, cylinders);
    blkcfg.heads = heads;
    blkcfg.sectors = secs;
    memcpy(config, &blkcfg, sizeof(blkcfg));
}

static uint32_t virtio_blk_get_features(VirtIODevice *vdev)
{
    return (1 << VIRTIO_BLK_F_SEG_MAX | 1 << VIRTIO_BLK_F_GEOMETRY);
}

static void virtio_blk_save(QEMUFile *f, void *opaque)
{
    VirtIOBlock *s = opaque;
    virtio_save(&s->vdev, f);
}

static int virtio_blk_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIOBlock *s = opaque;

    if (version_id != 1)
        return -EINVAL;

    virtio_load(&s->vdev, f);

    return 0;
}

void *virtio_blk_init(PCIBus *bus, BlockDriverState *bs)
{
    VirtIOBlock *s;
    int cylinders, heads, secs;
    static int virtio_blk_id;

    s = (VirtIOBlock *)virtio_init_pci(bus, "virtio-blk",
                                       PCI_VENDOR_ID_REDHAT_QUMRANET,
                                       PCI_DEVICE_ID_VIRTIO_BLOCK,
                                       0, VIRTIO_ID_BLOCK,
                                       0x01, 0x80, 0x00,
                                       sizeof(struct virtio_blk_config), sizeof(VirtIOBlock));
    if (!s)
        return NULL;

    s->vdev.get_config = virtio_blk_update_config;
    s->vdev.get_features = virtio_blk_get_features;
    s->vdev.reset = virtio_blk_reset;
    s->bs = bs;
    bdrv_guess_geometry(s->bs, &cylinders, &heads, &secs);
    bdrv_set_geometry_hint(s->bs, cylinders, heads, secs);

    s->vq = virtio_add_queue(&s->vdev, 128, virtio_blk_handle_output);

    register_savevm("virtio-blk", virtio_blk_id++, 1,
                    virtio_blk_save, virtio_blk_load, s);

    return s;
}
