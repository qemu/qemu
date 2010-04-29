/*
 * Virtio 9p backend
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "virtio.h"
#include "pc.h"
#include "qemu_socket.h"
#include "virtio-9p.h"
#include "fsdev/qemu-fsdev.h"
#include "virtio-9p-debug.h"

int dotu = 1;
int debug_9p_pdu;

static V9fsPDU *alloc_pdu(V9fsState *s)
{
    V9fsPDU *pdu = NULL;

    if (!QLIST_EMPTY(&s->free_list)) {
	pdu = QLIST_FIRST(&s->free_list);
	QLIST_REMOVE(pdu, next);
    }
    return pdu;
}

static void free_pdu(V9fsState *s, V9fsPDU *pdu)
{
    if (pdu) {
	QLIST_INSERT_HEAD(&s->free_list, pdu, next);
    }
}

size_t pdu_packunpack(void *addr, struct iovec *sg, int sg_count,
                        size_t offset, size_t size, int pack)
{
    int i = 0;
    size_t copied = 0;

    for (i = 0; size && i < sg_count; i++) {
        size_t len;
        if (offset >= sg[i].iov_len) {
            /* skip this sg */
            offset -= sg[i].iov_len;
            continue;
        } else {
            len = MIN(sg[i].iov_len - offset, size);
            if (pack) {
                memcpy(sg[i].iov_base + offset, addr, len);
            } else {
                memcpy(addr, sg[i].iov_base + offset, len);
            }
            size -= len;
            copied += len;
            addr += len;
            if (size) {
                offset = 0;
                continue;
            }
        }
    }

    return copied;
}

static void v9fs_version(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

static void v9fs_attach(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

static void v9fs_stat(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

static void v9fs_walk(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

static void v9fs_clunk(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

static void v9fs_open(V9fsState *s, V9fsPDU *pdu)
{    if (debug_9p_pdu) {
        pprint_pdu(pdu);
     }
}

static void v9fs_read(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

static void v9fs_write(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

static void v9fs_create(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

static void v9fs_flush(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

static void v9fs_remove(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

static void v9fs_wstat(V9fsState *s, V9fsPDU *pdu)
{
    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
}

typedef void (pdu_handler_t)(V9fsState *s, V9fsPDU *pdu);

static pdu_handler_t *pdu_handlers[] = {
    [P9_TVERSION] = v9fs_version,
    [P9_TATTACH] = v9fs_attach,
    [P9_TSTAT] = v9fs_stat,
    [P9_TWALK] = v9fs_walk,
    [P9_TCLUNK] = v9fs_clunk,
    [P9_TOPEN] = v9fs_open,
    [P9_TREAD] = v9fs_read,
#if 0
    [P9_TAUTH] = v9fs_auth,
#endif
    [P9_TFLUSH] = v9fs_flush,
    [P9_TCREATE] = v9fs_create,
    [P9_TWRITE] = v9fs_write,
    [P9_TWSTAT] = v9fs_wstat,
    [P9_TREMOVE] = v9fs_remove,
};

static void submit_pdu(V9fsState *s, V9fsPDU *pdu)
{
    pdu_handler_t *handler;

    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }

    BUG_ON(pdu->id >= ARRAY_SIZE(pdu_handlers));

    handler = pdu_handlers[pdu->id];
    BUG_ON(handler == NULL);

    handler(s, pdu);
}

static void handle_9p_output(VirtIODevice *vdev, VirtQueue *vq)
{
    V9fsState *s = (V9fsState *)vdev;
    V9fsPDU *pdu;
    ssize_t len;

    while ((pdu = alloc_pdu(s)) &&
            (len = virtqueue_pop(vq, &pdu->elem)) != 0) {
        uint8_t *ptr;

        BUG_ON(pdu->elem.out_num == 0 || pdu->elem.in_num == 0);
        BUG_ON(pdu->elem.out_sg[0].iov_len < 7);

        ptr = pdu->elem.out_sg[0].iov_base;

        memcpy(&pdu->size, ptr, 4);
        pdu->id = ptr[4];
        memcpy(&pdu->tag, ptr + 5, 2);

        submit_pdu(s, pdu);
    }

    free_pdu(s, pdu);
}

static uint32_t virtio_9p_get_features(VirtIODevice *vdev, uint32_t features)
{
    features |= 1 << VIRTIO_9P_MOUNT_TAG;
    return features;
}

static V9fsState *to_virtio_9p(VirtIODevice *vdev)
{
    return (V9fsState *)vdev;
}

static void virtio_9p_get_config(VirtIODevice *vdev, uint8_t *config)
{
    struct virtio_9p_config *cfg;
    V9fsState *s = to_virtio_9p(vdev);

    cfg = qemu_mallocz(sizeof(struct virtio_9p_config) +
                        s->tag_len);
    stw_raw(&cfg->tag_len, s->tag_len);
    memcpy(cfg->tag, s->tag, s->tag_len);
    memcpy(config, cfg, s->config_size);
    qemu_free(cfg);
}

VirtIODevice *virtio_9p_init(DeviceState *dev, V9fsConf *conf)
 {
    V9fsState *s;
    int i, len;
    struct stat stat;
    FsTypeEntry *fse;


    s = (V9fsState *)virtio_common_init("virtio-9p",
                                    VIRTIO_ID_9P,
                                    sizeof(struct virtio_9p_config)+
                                    MAX_TAG_LEN,
                                    sizeof(V9fsState));

    /* initialize pdu allocator */
    QLIST_INIT(&s->free_list);
    for (i = 0; i < (MAX_REQ - 1); i++) {
	QLIST_INSERT_HEAD(&s->free_list, &s->pdus[i], next);
    }

    s->vq = virtio_add_queue(&s->vdev, MAX_REQ, handle_9p_output);

    fse = get_fsdev_fsentry(conf->fsdev_id);

    if (!fse) {
        /* We don't have a fsdev identified by fsdev_id */
        fprintf(stderr, "Virtio-9p device couldn't find fsdev "
                    "with the id %s\n", conf->fsdev_id);
        exit(1);
    }

    if (!fse->path || !conf->tag) {
        /* we haven't specified a mount_tag or the path */
        fprintf(stderr, "fsdev with id %s needs path "
                "and Virtio-9p device needs mount_tag arguments\n",
                conf->fsdev_id);
        exit(1);
    }

    if (lstat(fse->path, &stat)) {
        fprintf(stderr, "share path %s does not exist\n", fse->path);
        exit(1);
    } else if (!S_ISDIR(stat.st_mode)) {
        fprintf(stderr, "share path %s is not a directory \n", fse->path);
        exit(1);
    }

    s->ctx.fs_root = qemu_strdup(fse->path);
    len = strlen(conf->tag);
    if (len > MAX_TAG_LEN) {
        len = MAX_TAG_LEN;
    }
    /* s->tag is non-NULL terminated string */
    s->tag = qemu_malloc(len);
    memcpy(s->tag, conf->tag, len);
    s->tag_len = len;
    s->ctx.uid = -1;

    s->ops = fse->ops;
    s->vdev.get_features = virtio_9p_get_features;
    s->config_size = sizeof(struct virtio_9p_config) +
                        s->tag_len;
    s->vdev.get_config = virtio_9p_get_config;

    return &s->vdev;
}
