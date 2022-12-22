/*
 * VDUSE (vDPA Device in Userspace) library
 *
 * Copyright (C) 2022 Bytedance Inc. and/or its affiliates. All rights reserved.
 *   Portions of codes and concepts borrowed from libvhost-user.c, so:
 *     Copyright IBM, Corp. 2007
 *     Copyright (c) 2016 Red Hat, Inc.
 *
 * Author:
 *   Xie Yongji <xieyongji@bytedance.com>
 *   Anthony Liguori <aliguori@us.ibm.com>
 *   Marc-André Lureau <mlureau@redhat.com>
 *   Victor Kaplansky <victork@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <endian.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <inttypes.h>

#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <sys/mman.h>

#include "include/atomic.h"
#include "linux-headers/linux/virtio_ring.h"
#include "linux-headers/linux/virtio_config.h"
#include "linux-headers/linux/vduse.h"
#include "libvduse.h"

#define VDUSE_VQ_ALIGN 4096
#define MAX_IOVA_REGIONS 256

#define LOG_ALIGNMENT 64

/* Round number down to multiple */
#define ALIGN_DOWN(n, m) ((n) / (m) * (m))

/* Round number up to multiple */
#define ALIGN_UP(n, m) ALIGN_DOWN((n) + (m) - 1, (m))

#ifndef unlikely
#define unlikely(x)   __builtin_expect(!!(x), 0)
#endif

typedef struct VduseDescStateSplit {
    uint8_t inflight;
    uint8_t padding[5];
    uint16_t next;
    uint64_t counter;
} VduseDescStateSplit;

typedef struct VduseVirtqLogInflight {
    uint64_t features;
    uint16_t version;
    uint16_t desc_num;
    uint16_t last_batch_head;
    uint16_t used_idx;
    VduseDescStateSplit desc[];
} VduseVirtqLogInflight;

typedef struct VduseVirtqLog {
    VduseVirtqLogInflight inflight;
} VduseVirtqLog;

typedef struct VduseVirtqInflightDesc {
    uint16_t index;
    uint64_t counter;
} VduseVirtqInflightDesc;

typedef struct VduseRing {
    unsigned int num;
    uint64_t desc_addr;
    uint64_t avail_addr;
    uint64_t used_addr;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
} VduseRing;

struct VduseVirtq {
    VduseRing vring;
    uint16_t last_avail_idx;
    uint16_t shadow_avail_idx;
    uint16_t used_idx;
    uint16_t signalled_used;
    bool signalled_used_valid;
    int index;
    unsigned int inuse;
    bool ready;
    int fd;
    VduseDev *dev;
    VduseVirtqInflightDesc *resubmit_list;
    uint16_t resubmit_num;
    uint64_t counter;
    VduseVirtqLog *log;
};

typedef struct VduseIovaRegion {
    uint64_t iova;
    uint64_t size;
    uint64_t mmap_offset;
    uint64_t mmap_addr;
} VduseIovaRegion;

struct VduseDev {
    VduseVirtq *vqs;
    VduseIovaRegion regions[MAX_IOVA_REGIONS];
    int num_regions;
    char *name;
    uint32_t device_id;
    uint32_t vendor_id;
    uint16_t num_queues;
    uint16_t queue_size;
    uint64_t features;
    const VduseOps *ops;
    int fd;
    int ctrl_fd;
    void *priv;
    void *log;
};

static inline size_t vduse_vq_log_size(uint16_t queue_size)
{
    return ALIGN_UP(sizeof(VduseDescStateSplit) * queue_size +
                    sizeof(VduseVirtqLogInflight), LOG_ALIGNMENT);
}

static void *vduse_log_get(const char *filename, size_t size)
{
    void *ptr = MAP_FAILED;
    int fd;

    fd = open(filename, O_RDWR | O_CREAT, 0600);
    if (fd == -1) {
        return MAP_FAILED;
    }

    if (ftruncate(fd, size) == -1) {
        goto out;
    }

    ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

out:
    close(fd);
    return ptr;
}

static inline bool has_feature(uint64_t features, unsigned int fbit)
{
    assert(fbit < 64);
    return !!(features & (1ULL << fbit));
}

static inline bool vduse_dev_has_feature(VduseDev *dev, unsigned int fbit)
{
    return has_feature(dev->features, fbit);
}

uint64_t vduse_get_virtio_features(void)
{
    return (1ULL << VIRTIO_F_IOMMU_PLATFORM) |
           (1ULL << VIRTIO_F_VERSION_1) |
           (1ULL << VIRTIO_F_NOTIFY_ON_EMPTY) |
           (1ULL << VIRTIO_RING_F_EVENT_IDX) |
           (1ULL << VIRTIO_RING_F_INDIRECT_DESC);
}

VduseDev *vduse_queue_get_dev(VduseVirtq *vq)
{
    return vq->dev;
}

int vduse_queue_get_fd(VduseVirtq *vq)
{
    return vq->fd;
}

void *vduse_dev_get_priv(VduseDev *dev)
{
    return dev->priv;
}

VduseVirtq *vduse_dev_get_queue(VduseDev *dev, int index)
{
    return &dev->vqs[index];
}

int vduse_dev_get_fd(VduseDev *dev)
{
    return dev->fd;
}

static int vduse_inject_irq(VduseDev *dev, int index)
{
    return ioctl(dev->fd, VDUSE_VQ_INJECT_IRQ, &index);
}

static int inflight_desc_compare(const void *a, const void *b)
{
    VduseVirtqInflightDesc *desc0 = (VduseVirtqInflightDesc *)a,
                           *desc1 = (VduseVirtqInflightDesc *)b;

    if (desc1->counter > desc0->counter &&
        (desc1->counter - desc0->counter) < VIRTQUEUE_MAX_SIZE * 2) {
        return 1;
    }

    return -1;
}

static int vduse_queue_check_inflights(VduseVirtq *vq)
{
    int i = 0;
    VduseDev *dev = vq->dev;

    vq->used_idx = le16toh(vq->vring.used->idx);
    vq->resubmit_num = 0;
    vq->resubmit_list = NULL;
    vq->counter = 0;

    if (unlikely(vq->log->inflight.used_idx != vq->used_idx)) {
        if (vq->log->inflight.last_batch_head > VIRTQUEUE_MAX_SIZE) {
            return -1;
        }

        vq->log->inflight.desc[vq->log->inflight.last_batch_head].inflight = 0;

        barrier();

        vq->log->inflight.used_idx = vq->used_idx;
    }

    for (i = 0; i < vq->log->inflight.desc_num; i++) {
        if (vq->log->inflight.desc[i].inflight == 1) {
            vq->inuse++;
        }
    }

    vq->shadow_avail_idx = vq->last_avail_idx = vq->inuse + vq->used_idx;

    if (vq->inuse) {
        vq->resubmit_list = calloc(vq->inuse, sizeof(VduseVirtqInflightDesc));
        if (!vq->resubmit_list) {
            return -1;
        }

        for (i = 0; i < vq->log->inflight.desc_num; i++) {
            if (vq->log->inflight.desc[i].inflight) {
                vq->resubmit_list[vq->resubmit_num].index = i;
                vq->resubmit_list[vq->resubmit_num].counter =
                                        vq->log->inflight.desc[i].counter;
                vq->resubmit_num++;
            }
        }

        if (vq->resubmit_num > 1) {
            qsort(vq->resubmit_list, vq->resubmit_num,
                  sizeof(VduseVirtqInflightDesc), inflight_desc_compare);
        }
        vq->counter = vq->resubmit_list[0].counter + 1;
    }

    vduse_inject_irq(dev, vq->index);

    return 0;
}

static int vduse_queue_inflight_get(VduseVirtq *vq, int desc_idx)
{
    vq->log->inflight.desc[desc_idx].counter = vq->counter++;

    barrier();

    vq->log->inflight.desc[desc_idx].inflight = 1;

    return 0;
}

static int vduse_queue_inflight_pre_put(VduseVirtq *vq, int desc_idx)
{
    vq->log->inflight.last_batch_head = desc_idx;

    return 0;
}

static int vduse_queue_inflight_post_put(VduseVirtq *vq, int desc_idx)
{
    vq->log->inflight.desc[desc_idx].inflight = 0;

    barrier();

    vq->log->inflight.used_idx = vq->used_idx;

    return 0;
}

static void vduse_iova_remove_region(VduseDev *dev, uint64_t start,
                                     uint64_t last)
{
    int i;

    if (last == start) {
        return;
    }

    for (i = 0; i < MAX_IOVA_REGIONS; i++) {
        if (!dev->regions[i].mmap_addr) {
            continue;
        }

        if (start <= dev->regions[i].iova &&
            last >= (dev->regions[i].iova + dev->regions[i].size - 1)) {
            munmap((void *)(uintptr_t)dev->regions[i].mmap_addr,
                   dev->regions[i].mmap_offset + dev->regions[i].size);
            dev->regions[i].mmap_addr = 0;
            dev->num_regions--;
        }
    }
}

static int vduse_iova_add_region(VduseDev *dev, int fd,
                                 uint64_t offset, uint64_t start,
                                 uint64_t last, int prot)
{
    int i;
    uint64_t size = last - start + 1;
    void *mmap_addr = mmap(0, size + offset, prot, MAP_SHARED, fd, 0);

    if (mmap_addr == MAP_FAILED) {
        close(fd);
        return -EINVAL;
    }

    for (i = 0; i < MAX_IOVA_REGIONS; i++) {
        if (!dev->regions[i].mmap_addr) {
            dev->regions[i].mmap_addr = (uint64_t)(uintptr_t)mmap_addr;
            dev->regions[i].mmap_offset = offset;
            dev->regions[i].iova = start;
            dev->regions[i].size = size;
            dev->num_regions++;
            break;
        }
    }
    assert(i < MAX_IOVA_REGIONS);
    close(fd);

    return 0;
}

static int perm_to_prot(uint8_t perm)
{
    int prot = 0;

    switch (perm) {
    case VDUSE_ACCESS_WO:
        prot |= PROT_WRITE;
        break;
    case VDUSE_ACCESS_RO:
        prot |= PROT_READ;
        break;
    case VDUSE_ACCESS_RW:
        prot |= PROT_READ | PROT_WRITE;
        break;
    default:
        break;
    }

    return prot;
}

static inline void *iova_to_va(VduseDev *dev, uint64_t *plen, uint64_t iova)
{
    int i, ret;
    struct vduse_iotlb_entry entry;

    for (i = 0; i < MAX_IOVA_REGIONS; i++) {
        VduseIovaRegion *r = &dev->regions[i];

        if (!r->mmap_addr) {
            continue;
        }

        if ((iova >= r->iova) && (iova < (r->iova + r->size))) {
            if ((iova + *plen) > (r->iova + r->size)) {
                *plen = r->iova + r->size - iova;
            }
            return (void *)(uintptr_t)(iova - r->iova +
                   r->mmap_addr + r->mmap_offset);
        }
    }

    entry.start = iova;
    entry.last = iova + 1;
    ret = ioctl(dev->fd, VDUSE_IOTLB_GET_FD, &entry);
    if (ret < 0) {
        return NULL;
    }

    if (!vduse_iova_add_region(dev, ret, entry.offset, entry.start,
                               entry.last, perm_to_prot(entry.perm))) {
        return iova_to_va(dev, plen, iova);
    }

    return NULL;
}

static inline uint16_t vring_avail_flags(VduseVirtq *vq)
{
    return le16toh(vq->vring.avail->flags);
}

static inline uint16_t vring_avail_idx(VduseVirtq *vq)
{
    vq->shadow_avail_idx = le16toh(vq->vring.avail->idx);

    return vq->shadow_avail_idx;
}

static inline uint16_t vring_avail_ring(VduseVirtq *vq, int i)
{
    return le16toh(vq->vring.avail->ring[i]);
}

static inline uint16_t vring_get_used_event(VduseVirtq *vq)
{
    return vring_avail_ring(vq, vq->vring.num);
}

static bool vduse_queue_get_head(VduseVirtq *vq, unsigned int idx,
                                 unsigned int *head)
{
    /*
     * Grab the next descriptor number they're advertising, and increment
     * the index we've seen.
     */
    *head = vring_avail_ring(vq, idx % vq->vring.num);

    /* If their number is silly, that's a fatal mistake. */
    if (*head >= vq->vring.num) {
        fprintf(stderr, "Guest says index %u is available\n", *head);
        return false;
    }

    return true;
}

static int
vduse_queue_read_indirect_desc(VduseDev *dev, struct vring_desc *desc,
                               uint64_t addr, size_t len)
{
    struct vring_desc *ori_desc;
    uint64_t read_len;

    if (len > (VIRTQUEUE_MAX_SIZE * sizeof(struct vring_desc))) {
        return -1;
    }

    if (len == 0) {
        return -1;
    }

    while (len) {
        read_len = len;
        ori_desc = iova_to_va(dev, &read_len, addr);
        if (!ori_desc) {
            return -1;
        }

        memcpy(desc, ori_desc, read_len);
        len -= read_len;
        addr += read_len;
        desc += read_len;
    }

    return 0;
}

enum {
    VIRTQUEUE_READ_DESC_ERROR = -1,
    VIRTQUEUE_READ_DESC_DONE = 0,   /* end of chain */
    VIRTQUEUE_READ_DESC_MORE = 1,   /* more buffers in chain */
};

static int vduse_queue_read_next_desc(struct vring_desc *desc, int i,
                                      unsigned int max, unsigned int *next)
{
    /* If this descriptor says it doesn't chain, we're done. */
    if (!(le16toh(desc[i].flags) & VRING_DESC_F_NEXT)) {
        return VIRTQUEUE_READ_DESC_DONE;
    }

    /* Check they're not leading us off end of descriptors. */
    *next = desc[i].next;
    /* Make sure compiler knows to grab that: we don't want it changing! */
    smp_wmb();

    if (*next >= max) {
        fprintf(stderr, "Desc next is %u\n", *next);
        return VIRTQUEUE_READ_DESC_ERROR;
    }

    return VIRTQUEUE_READ_DESC_MORE;
}

/*
 * Fetch avail_idx from VQ memory only when we really need to know if
 * guest has added some buffers.
 */
static bool vduse_queue_empty(VduseVirtq *vq)
{
    if (unlikely(!vq->vring.avail)) {
        return true;
    }

    if (vq->shadow_avail_idx != vq->last_avail_idx) {
        return false;
    }

    return vring_avail_idx(vq) == vq->last_avail_idx;
}

static bool vduse_queue_should_notify(VduseVirtq *vq)
{
    VduseDev *dev = vq->dev;
    uint16_t old, new;
    bool v;

    /* We need to expose used array entries before checking used event. */
    smp_mb();

    /* Always notify when queue is empty (when feature acknowledge) */
    if (vduse_dev_has_feature(dev, VIRTIO_F_NOTIFY_ON_EMPTY) &&
        !vq->inuse && vduse_queue_empty(vq)) {
        return true;
    }

    if (!vduse_dev_has_feature(dev, VIRTIO_RING_F_EVENT_IDX)) {
        return !(vring_avail_flags(vq) & VRING_AVAIL_F_NO_INTERRUPT);
    }

    v = vq->signalled_used_valid;
    vq->signalled_used_valid = true;
    old = vq->signalled_used;
    new = vq->signalled_used = vq->used_idx;
    return !v || vring_need_event(vring_get_used_event(vq), new, old);
}

void vduse_queue_notify(VduseVirtq *vq)
{
    VduseDev *dev = vq->dev;

    if (unlikely(!vq->vring.avail)) {
        return;
    }

    if (!vduse_queue_should_notify(vq)) {
        return;
    }

    if (vduse_inject_irq(dev, vq->index) < 0) {
        fprintf(stderr, "Error inject irq for vq %d: %s\n",
                vq->index, strerror(errno));
    }
}

static inline void vring_set_avail_event(VduseVirtq *vq, uint16_t val)
{
    uint16_t val_le = htole16(val);
    memcpy(&vq->vring.used->ring[vq->vring.num], &val_le, sizeof(uint16_t));
}

static bool vduse_queue_map_single_desc(VduseVirtq *vq, unsigned int *p_num_sg,
                                   struct iovec *iov, unsigned int max_num_sg,
                                   bool is_write, uint64_t pa, size_t sz)
{
    unsigned num_sg = *p_num_sg;
    VduseDev *dev = vq->dev;

    assert(num_sg <= max_num_sg);

    if (!sz) {
        fprintf(stderr, "virtio: zero sized buffers are not allowed\n");
        return false;
    }

    while (sz) {
        uint64_t len = sz;

        if (num_sg == max_num_sg) {
            fprintf(stderr,
                    "virtio: too many descriptors in indirect table\n");
            return false;
        }

        iov[num_sg].iov_base = iova_to_va(dev, &len, pa);
        if (iov[num_sg].iov_base == NULL) {
            fprintf(stderr, "virtio: invalid address for buffers\n");
            return false;
        }
        iov[num_sg++].iov_len = len;
        sz -= len;
        pa += len;
    }

    *p_num_sg = num_sg;
    return true;
}

static void *vduse_queue_alloc_element(size_t sz, unsigned out_num,
                                       unsigned in_num)
{
    VduseVirtqElement *elem;
    size_t in_sg_ofs = ALIGN_UP(sz, __alignof__(elem->in_sg[0]));
    size_t out_sg_ofs = in_sg_ofs + in_num * sizeof(elem->in_sg[0]);
    size_t out_sg_end = out_sg_ofs + out_num * sizeof(elem->out_sg[0]);

    assert(sz >= sizeof(VduseVirtqElement));
    elem = malloc(out_sg_end);
    if (!elem) {
        return NULL;
    }
    elem->out_num = out_num;
    elem->in_num = in_num;
    elem->in_sg = (void *)elem + in_sg_ofs;
    elem->out_sg = (void *)elem + out_sg_ofs;
    return elem;
}

static void *vduse_queue_map_desc(VduseVirtq *vq, unsigned int idx, size_t sz)
{
    struct vring_desc *desc = vq->vring.desc;
    VduseDev *dev = vq->dev;
    uint64_t desc_addr, read_len;
    unsigned int desc_len;
    unsigned int max = vq->vring.num;
    unsigned int i = idx;
    VduseVirtqElement *elem;
    struct iovec iov[VIRTQUEUE_MAX_SIZE];
    struct vring_desc desc_buf[VIRTQUEUE_MAX_SIZE];
    unsigned int out_num = 0, in_num = 0;
    int rc;

    if (le16toh(desc[i].flags) & VRING_DESC_F_INDIRECT) {
        if (le32toh(desc[i].len) % sizeof(struct vring_desc)) {
            fprintf(stderr, "Invalid size for indirect buffer table\n");
            return NULL;
        }

        /* loop over the indirect descriptor table */
        desc_addr = le64toh(desc[i].addr);
        desc_len = le32toh(desc[i].len);
        max = desc_len / sizeof(struct vring_desc);
        read_len = desc_len;
        desc = iova_to_va(dev, &read_len, desc_addr);
        if (unlikely(desc && read_len != desc_len)) {
            /* Failed to use zero copy */
            desc = NULL;
            if (!vduse_queue_read_indirect_desc(dev, desc_buf,
                                                desc_addr,
                                                desc_len)) {
                desc = desc_buf;
            }
        }
        if (!desc) {
            fprintf(stderr, "Invalid indirect buffer table\n");
            return NULL;
        }
        i = 0;
    }

    /* Collect all the descriptors */
    do {
        if (le16toh(desc[i].flags) & VRING_DESC_F_WRITE) {
            if (!vduse_queue_map_single_desc(vq, &in_num, iov + out_num,
                                             VIRTQUEUE_MAX_SIZE - out_num,
                                             true, le64toh(desc[i].addr),
                                             le32toh(desc[i].len))) {
                return NULL;
            }
        } else {
            if (in_num) {
                fprintf(stderr, "Incorrect order for descriptors\n");
                return NULL;
            }
            if (!vduse_queue_map_single_desc(vq, &out_num, iov,
                                             VIRTQUEUE_MAX_SIZE, false,
                                             le64toh(desc[i].addr),
                                             le32toh(desc[i].len))) {
                return NULL;
            }
        }

        /* If we've got too many, that implies a descriptor loop. */
        if ((in_num + out_num) > max) {
            fprintf(stderr, "Looped descriptor\n");
            return NULL;
        }
        rc = vduse_queue_read_next_desc(desc, i, max, &i);
    } while (rc == VIRTQUEUE_READ_DESC_MORE);

    if (rc == VIRTQUEUE_READ_DESC_ERROR) {
        fprintf(stderr, "read descriptor error\n");
        return NULL;
    }

    /* Now copy what we have collected and mapped */
    elem = vduse_queue_alloc_element(sz, out_num, in_num);
    if (!elem) {
        fprintf(stderr, "read descriptor error\n");
        return NULL;
    }
    elem->index = idx;
    for (i = 0; i < out_num; i++) {
        elem->out_sg[i] = iov[i];
    }
    for (i = 0; i < in_num; i++) {
        elem->in_sg[i] = iov[out_num + i];
    }

    return elem;
}

void *vduse_queue_pop(VduseVirtq *vq, size_t sz)
{
    unsigned int head;
    VduseVirtqElement *elem;
    VduseDev *dev = vq->dev;
    int i;

    if (unlikely(!vq->vring.avail)) {
        return NULL;
    }

    if (unlikely(vq->resubmit_list && vq->resubmit_num > 0)) {
        i = (--vq->resubmit_num);
        elem = vduse_queue_map_desc(vq, vq->resubmit_list[i].index, sz);

        if (!vq->resubmit_num) {
            free(vq->resubmit_list);
            vq->resubmit_list = NULL;
        }

        return elem;
    }

    if (vduse_queue_empty(vq)) {
        return NULL;
    }
    /* Needed after virtio_queue_empty() */
    smp_rmb();

    if (vq->inuse >= vq->vring.num) {
        fprintf(stderr, "Virtqueue size exceeded: %d\n", vq->inuse);
        return NULL;
    }

    if (!vduse_queue_get_head(vq, vq->last_avail_idx++, &head)) {
        return NULL;
    }

    if (vduse_dev_has_feature(dev, VIRTIO_RING_F_EVENT_IDX)) {
        vring_set_avail_event(vq, vq->last_avail_idx);
    }

    elem = vduse_queue_map_desc(vq, head, sz);

    if (!elem) {
        return NULL;
    }

    vq->inuse++;

    vduse_queue_inflight_get(vq, head);

    return elem;
}

static inline void vring_used_write(VduseVirtq *vq,
                                    struct vring_used_elem *uelem, int i)
{
    struct vring_used *used = vq->vring.used;

    used->ring[i] = *uelem;
}

static void vduse_queue_fill(VduseVirtq *vq, const VduseVirtqElement *elem,
                             unsigned int len, unsigned int idx)
{
    struct vring_used_elem uelem;

    if (unlikely(!vq->vring.used)) {
        return;
    }

    idx = (idx + vq->used_idx) % vq->vring.num;

    uelem.id = htole32(elem->index);
    uelem.len = htole32(len);
    vring_used_write(vq, &uelem, idx);
}

static inline void vring_used_idx_set(VduseVirtq *vq, uint16_t val)
{
    vq->vring.used->idx = htole16(val);
    vq->used_idx = val;
}

static void vduse_queue_flush(VduseVirtq *vq, unsigned int count)
{
    uint16_t old, new;

    if (unlikely(!vq->vring.used)) {
        return;
    }

    /* Make sure buffer is written before we update index. */
    smp_wmb();

    old = vq->used_idx;
    new = old + count;
    vring_used_idx_set(vq, new);
    vq->inuse -= count;
    if (unlikely((int16_t)(new - vq->signalled_used) < (uint16_t)(new - old))) {
        vq->signalled_used_valid = false;
    }
}

void vduse_queue_push(VduseVirtq *vq, const VduseVirtqElement *elem,
                      unsigned int len)
{
    vduse_queue_fill(vq, elem, len, 0);
    vduse_queue_inflight_pre_put(vq, elem->index);
    vduse_queue_flush(vq, 1);
    vduse_queue_inflight_post_put(vq, elem->index);
}

static int vduse_queue_update_vring(VduseVirtq *vq, uint64_t desc_addr,
                                    uint64_t avail_addr, uint64_t used_addr)
{
    struct VduseDev *dev = vq->dev;
    uint64_t len;

    len = sizeof(struct vring_desc);
    vq->vring.desc = iova_to_va(dev, &len, desc_addr);
    if (len != sizeof(struct vring_desc)) {
        return -EINVAL;
    }

    len = sizeof(struct vring_avail);
    vq->vring.avail = iova_to_va(dev, &len, avail_addr);
    if (len != sizeof(struct vring_avail)) {
        return -EINVAL;
    }

    len = sizeof(struct vring_used);
    vq->vring.used = iova_to_va(dev, &len, used_addr);
    if (len != sizeof(struct vring_used)) {
        return -EINVAL;
    }

    if (!vq->vring.desc || !vq->vring.avail || !vq->vring.used) {
        fprintf(stderr, "Failed to get vq[%d] iova mapping\n", vq->index);
        return -EINVAL;
    }

    return 0;
}

static void vduse_queue_enable(VduseVirtq *vq)
{
    struct VduseDev *dev = vq->dev;
    struct vduse_vq_info vq_info;
    struct vduse_vq_eventfd vq_eventfd;
    int fd;

    vq_info.index = vq->index;
    if (ioctl(dev->fd, VDUSE_VQ_GET_INFO, &vq_info)) {
        fprintf(stderr, "Failed to get vq[%d] info: %s\n",
                vq->index, strerror(errno));
        return;
    }

    if (!vq_info.ready) {
        return;
    }

    vq->vring.num = vq_info.num;
    vq->vring.desc_addr = vq_info.desc_addr;
    vq->vring.avail_addr = vq_info.driver_addr;
    vq->vring.used_addr = vq_info.device_addr;

    if (vduse_queue_update_vring(vq, vq_info.desc_addr,
                                 vq_info.driver_addr, vq_info.device_addr)) {
        fprintf(stderr, "Failed to update vring for vq[%d]\n", vq->index);
        return;
    }

    fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Failed to init eventfd for vq[%d]\n", vq->index);
        return;
    }

    vq_eventfd.index = vq->index;
    vq_eventfd.fd = fd;
    if (ioctl(dev->fd, VDUSE_VQ_SETUP_KICKFD, &vq_eventfd)) {
        fprintf(stderr, "Failed to setup kick fd for vq[%d]\n", vq->index);
        close(fd);
        return;
    }

    vq->fd = fd;
    vq->signalled_used_valid = false;
    vq->ready = true;

    if (vduse_queue_check_inflights(vq)) {
        fprintf(stderr, "Failed to check inflights for vq[%d]\n", vq->index);
        close(fd);
        return;
    }

    dev->ops->enable_queue(dev, vq);
}

static void vduse_queue_disable(VduseVirtq *vq)
{
    struct VduseDev *dev = vq->dev;
    struct vduse_vq_eventfd eventfd;

    if (!vq->ready) {
        return;
    }

    dev->ops->disable_queue(dev, vq);

    eventfd.index = vq->index;
    eventfd.fd = VDUSE_EVENTFD_DEASSIGN;
    ioctl(dev->fd, VDUSE_VQ_SETUP_KICKFD, &eventfd);
    close(vq->fd);

    assert(vq->inuse == 0);

    vq->vring.num = 0;
    vq->vring.desc_addr = 0;
    vq->vring.avail_addr = 0;
    vq->vring.used_addr = 0;
    vq->vring.desc = 0;
    vq->vring.avail = 0;
    vq->vring.used = 0;
    vq->ready = false;
    vq->fd = -1;
}

static void vduse_dev_start_dataplane(VduseDev *dev)
{
    int i;

    if (ioctl(dev->fd, VDUSE_DEV_GET_FEATURES, &dev->features)) {
        fprintf(stderr, "Failed to get features: %s\n", strerror(errno));
        return;
    }
    assert(vduse_dev_has_feature(dev, VIRTIO_F_VERSION_1));

    for (i = 0; i < dev->num_queues; i++) {
        vduse_queue_enable(&dev->vqs[i]);
    }
}

static void vduse_dev_stop_dataplane(VduseDev *dev)
{
    size_t log_size = dev->num_queues * vduse_vq_log_size(VIRTQUEUE_MAX_SIZE);
    int i;

    for (i = 0; i < dev->num_queues; i++) {
        vduse_queue_disable(&dev->vqs[i]);
    }
    if (dev->log) {
        memset(dev->log, 0, log_size);
    }
    dev->features = 0;
    vduse_iova_remove_region(dev, 0, ULONG_MAX);
}

int vduse_dev_handler(VduseDev *dev)
{
    struct vduse_dev_request req;
    struct vduse_dev_response resp = { 0 };
    VduseVirtq *vq;
    int i, ret;

    ret = read(dev->fd, &req, sizeof(req));
    if (ret != sizeof(req)) {
        fprintf(stderr, "Read request error [%d]: %s\n",
                ret, strerror(errno));
        return -errno;
    }
    resp.request_id = req.request_id;

    switch (req.type) {
    case VDUSE_GET_VQ_STATE:
        vq = &dev->vqs[req.vq_state.index];
        resp.vq_state.split.avail_index = vq->last_avail_idx;
        resp.result = VDUSE_REQ_RESULT_OK;
        break;
    case VDUSE_SET_STATUS:
        if (req.s.status & VIRTIO_CONFIG_S_DRIVER_OK) {
            vduse_dev_start_dataplane(dev);
        } else if (req.s.status == 0) {
            vduse_dev_stop_dataplane(dev);
        }
        resp.result = VDUSE_REQ_RESULT_OK;
        break;
    case VDUSE_UPDATE_IOTLB:
        /* The iova will be updated by iova_to_va() later, so just remove it */
        vduse_iova_remove_region(dev, req.iova.start, req.iova.last);
        for (i = 0; i < dev->num_queues; i++) {
            VduseVirtq *vq = &dev->vqs[i];
            if (vq->ready) {
                if (vduse_queue_update_vring(vq, vq->vring.desc_addr,
                                             vq->vring.avail_addr,
                                             vq->vring.used_addr)) {
                    fprintf(stderr, "Failed to update vring for vq[%d]\n",
                            vq->index);
                }
            }
        }
        resp.result = VDUSE_REQ_RESULT_OK;
        break;
    default:
        resp.result = VDUSE_REQ_RESULT_FAILED;
        break;
    }

    ret = write(dev->fd, &resp, sizeof(resp));
    if (ret != sizeof(resp)) {
        fprintf(stderr, "Write request %d error [%d]: %s\n",
                req.type, ret, strerror(errno));
        return -errno;
    }
    return 0;
}

int vduse_dev_update_config(VduseDev *dev, uint32_t size,
                            uint32_t offset, char *buffer)
{
    int ret;
    struct vduse_config_data *data;

    data = malloc(offsetof(struct vduse_config_data, buffer) + size);
    if (!data) {
        return -ENOMEM;
    }

    data->offset = offset;
    data->length = size;
    memcpy(data->buffer, buffer, size);

    ret = ioctl(dev->fd, VDUSE_DEV_SET_CONFIG, data);
    free(data);

    if (ret) {
        return -errno;
    }

    if (ioctl(dev->fd, VDUSE_DEV_INJECT_CONFIG_IRQ)) {
        return -errno;
    }

    return 0;
}

int vduse_dev_setup_queue(VduseDev *dev, int index, int max_size)
{
    VduseVirtq *vq = &dev->vqs[index];
    struct vduse_vq_config vq_config = { 0 };

    if (max_size > VIRTQUEUE_MAX_SIZE) {
        return -EINVAL;
    }

    vq_config.index = vq->index;
    vq_config.max_size = max_size;

    if (ioctl(dev->fd, VDUSE_VQ_SETUP, &vq_config)) {
        return -errno;
    }

    vduse_queue_enable(vq);

    return 0;
}

int vduse_set_reconnect_log_file(VduseDev *dev, const char *filename)
{

    size_t log_size = dev->num_queues * vduse_vq_log_size(VIRTQUEUE_MAX_SIZE);
    void *log;
    int i;

    dev->log = log = vduse_log_get(filename, log_size);
    if (log == MAP_FAILED) {
        fprintf(stderr, "Failed to get vduse log\n");
        return -EINVAL;
    }

    for (i = 0; i < dev->num_queues; i++) {
        dev->vqs[i].log = log;
        dev->vqs[i].log->inflight.desc_num = VIRTQUEUE_MAX_SIZE;
        log = (void *)((char *)log + vduse_vq_log_size(VIRTQUEUE_MAX_SIZE));
    }

    return 0;
}

static int vduse_dev_init_vqs(VduseDev *dev, uint16_t num_queues)
{
    VduseVirtq *vqs;
    int i;

    vqs = calloc(sizeof(VduseVirtq), num_queues);
    if (!vqs) {
        return -ENOMEM;
    }

    for (i = 0; i < num_queues; i++) {
        vqs[i].index = i;
        vqs[i].dev = dev;
        vqs[i].fd = -1;
    }
    dev->vqs = vqs;

    return 0;
}

static int vduse_dev_init(VduseDev *dev, const char *name,
                          uint16_t num_queues, const VduseOps *ops,
                          void *priv)
{
    char *dev_path, *dev_name;
    int ret, fd;

    dev_path = malloc(strlen(name) + strlen("/dev/vduse/") + 1);
    if (!dev_path) {
        return -ENOMEM;
    }
    sprintf(dev_path, "/dev/vduse/%s", name);

    fd = open(dev_path, O_RDWR);
    free(dev_path);
    if (fd < 0) {
        fprintf(stderr, "Failed to open vduse dev %s: %s\n",
                name, strerror(errno));
        return -errno;
    }

    if (ioctl(fd, VDUSE_DEV_GET_FEATURES, &dev->features)) {
        fprintf(stderr, "Failed to get features: %s\n", strerror(errno));
        close(fd);
        return -errno;
    }

    dev_name = strdup(name);
    if (!dev_name) {
        close(fd);
        return -ENOMEM;
    }

    ret = vduse_dev_init_vqs(dev, num_queues);
    if (ret) {
        free(dev_name);
        close(fd);
        return ret;
    }

    dev->name = dev_name;
    dev->num_queues = num_queues;
    dev->fd = fd;
    dev->ops = ops;
    dev->priv = priv;

    return 0;
}

static inline bool vduse_name_is_invalid(const char *name)
{
    return strlen(name) >= VDUSE_NAME_MAX || strstr(name, "..");
}

VduseDev *vduse_dev_create_by_fd(int fd, uint16_t num_queues,
                                 const VduseOps *ops, void *priv)
{
    VduseDev *dev;
    int ret;

    if (!ops || !ops->enable_queue || !ops->disable_queue) {
        fprintf(stderr, "Invalid parameter for vduse\n");
        return NULL;
    }

    dev = calloc(sizeof(VduseDev), 1);
    if (!dev) {
        fprintf(stderr, "Failed to allocate vduse device\n");
        return NULL;
    }

    if (ioctl(fd, VDUSE_DEV_GET_FEATURES, &dev->features)) {
        fprintf(stderr, "Failed to get features: %s\n", strerror(errno));
        free(dev);
        return NULL;
    }

    ret = vduse_dev_init_vqs(dev, num_queues);
    if (ret) {
        fprintf(stderr, "Failed to init vqs\n");
        free(dev);
        return NULL;
    }

    dev->num_queues = num_queues;
    dev->fd = fd;
    dev->ops = ops;
    dev->priv = priv;

    return dev;
}

VduseDev *vduse_dev_create_by_name(const char *name, uint16_t num_queues,
                                   const VduseOps *ops, void *priv)
{
    VduseDev *dev;
    int ret;

    if (!name || vduse_name_is_invalid(name) || !ops ||
        !ops->enable_queue || !ops->disable_queue) {
        fprintf(stderr, "Invalid parameter for vduse\n");
        return NULL;
    }

    dev = calloc(sizeof(VduseDev), 1);
    if (!dev) {
        fprintf(stderr, "Failed to allocate vduse device\n");
        return NULL;
    }

    ret = vduse_dev_init(dev, name, num_queues, ops, priv);
    if (ret < 0) {
        fprintf(stderr, "Failed to init vduse device %s: %s\n",
                name, strerror(-ret));
        free(dev);
        return NULL;
    }

    return dev;
}

VduseDev *vduse_dev_create(const char *name, uint32_t device_id,
                           uint32_t vendor_id, uint64_t features,
                           uint16_t num_queues, uint32_t config_size,
                           char *config, const VduseOps *ops, void *priv)
{
    VduseDev *dev;
    int ret, ctrl_fd;
    uint64_t version;
    struct vduse_dev_config *dev_config;
    size_t size = offsetof(struct vduse_dev_config, config);

    if (!name || vduse_name_is_invalid(name) ||
        !has_feature(features,  VIRTIO_F_VERSION_1) || !config ||
        !config_size || !ops || !ops->enable_queue || !ops->disable_queue) {
        fprintf(stderr, "Invalid parameter for vduse\n");
        return NULL;
    }

    dev = calloc(sizeof(VduseDev), 1);
    if (!dev) {
        fprintf(stderr, "Failed to allocate vduse device\n");
        return NULL;
    }

    ctrl_fd = open("/dev/vduse/control", O_RDWR);
    if (ctrl_fd < 0) {
        fprintf(stderr, "Failed to open /dev/vduse/control: %s\n",
                strerror(errno));
        goto err_ctrl;
    }

    version = VDUSE_API_VERSION;
    if (ioctl(ctrl_fd, VDUSE_SET_API_VERSION, &version)) {
        fprintf(stderr, "Failed to set api version %" PRIu64 ": %s\n",
                version, strerror(errno));
        goto err_dev;
    }

    dev_config = calloc(size + config_size, 1);
    if (!dev_config) {
        fprintf(stderr, "Failed to allocate config space\n");
        goto err_dev;
    }

    assert(!vduse_name_is_invalid(name));
    strcpy(dev_config->name, name);
    dev_config->device_id = device_id;
    dev_config->vendor_id = vendor_id;
    dev_config->features = features;
    dev_config->vq_num = num_queues;
    dev_config->vq_align = VDUSE_VQ_ALIGN;
    dev_config->config_size = config_size;
    memcpy(dev_config->config, config, config_size);

    ret = ioctl(ctrl_fd, VDUSE_CREATE_DEV, dev_config);
    free(dev_config);
    if (ret && errno != EEXIST) {
        fprintf(stderr, "Failed to create vduse device %s: %s\n",
                name, strerror(errno));
        goto err_dev;
    }
    dev->ctrl_fd = ctrl_fd;

    ret = vduse_dev_init(dev, name, num_queues, ops, priv);
    if (ret < 0) {
        fprintf(stderr, "Failed to init vduse device %s: %s\n",
                name, strerror(-ret));
        goto err;
    }

    return dev;
err:
    ioctl(ctrl_fd, VDUSE_DESTROY_DEV, name);
err_dev:
    close(ctrl_fd);
err_ctrl:
    free(dev);

    return NULL;
}

int vduse_dev_destroy(VduseDev *dev)
{
    size_t log_size = dev->num_queues * vduse_vq_log_size(VIRTQUEUE_MAX_SIZE);
    int i, ret = 0;

    if (dev->log) {
        munmap(dev->log, log_size);
    }
    for (i = 0; i < dev->num_queues; i++) {
        free(dev->vqs[i].resubmit_list);
    }
    free(dev->vqs);
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
    if (dev->ctrl_fd >= 0) {
        if (ioctl(dev->ctrl_fd, VDUSE_DESTROY_DEV, dev->name)) {
            ret = -errno;
        }
        close(dev->ctrl_fd);
        dev->ctrl_fd = -1;
    }
    free(dev->name);
    free(dev);

    return ret;
}
