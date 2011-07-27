/*
 * Virtio Balloon Device
 *
 * Copyright IBM, Corp. 2008
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2011 Amit Shah <amit.shah@redhat.com>
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "iov.h"
#include "qemu-common.h"
#include "virtio.h"
#include "pc.h"
#include "cpu.h"
#include "monitor.h"
#include "balloon.h"
#include "virtio-balloon.h"
#include "kvm.h"
#include "qlist.h"
#include "qint.h"
#include "qstring.h"

#if defined(__linux__)
#include <sys/mman.h>
#endif

/* Disable guest-provided stats by now (https://bugzilla.redhat.com/show_bug.cgi?id=623903) */
#define ENABLE_GUEST_STATS   0


typedef struct VirtIOBalloon
{
    VirtIODevice vdev;
    VirtQueue *ivq, *dvq, *svq;
    uint32_t num_pages;
    uint32_t actual;
    uint64_t stats[VIRTIO_BALLOON_S_NR];
    VirtQueueElement stats_vq_elem;
    size_t stats_vq_offset;
    MonitorCompletion *stats_callback;
    void *stats_opaque_callback_data;
} VirtIOBalloon;

static VirtIOBalloon *to_virtio_balloon(VirtIODevice *vdev)
{
    return (VirtIOBalloon *)vdev;
}

static void balloon_page(void *addr, int deflate)
{
#if defined(__linux__)
    if (!kvm_enabled() || kvm_has_sync_mmu())
        qemu_madvise(addr, TARGET_PAGE_SIZE,
                deflate ? QEMU_MADV_WILLNEED : QEMU_MADV_DONTNEED);
#endif
}

/*
 * reset_stats - Mark all items in the stats array as unset
 *
 * This function needs to be called at device intialization and before
 * before updating to a set of newly-generated stats.  This will ensure that no
 * stale values stick around in case the guest reports a subset of the supported
 * statistics.
 */
static inline void reset_stats(VirtIOBalloon *dev)
{
    int i;
    for (i = 0; i < VIRTIO_BALLOON_S_NR; dev->stats[i++] = -1);
}

static void stat_put(QDict *dict, const char *label, uint64_t val)
{
    if (val != -1)
        qdict_put(dict, label, qint_from_int(val));
}

static QObject *get_stats_qobject(VirtIOBalloon *dev)
{
    QDict *dict = qdict_new();
    uint64_t actual = ram_size - ((uint64_t) dev->actual <<
                                  VIRTIO_BALLOON_PFN_SHIFT);

    stat_put(dict, "actual", actual);
#if ENABLE_GUEST_STATS
    stat_put(dict, "mem_swapped_in", dev->stats[VIRTIO_BALLOON_S_SWAP_IN]);
    stat_put(dict, "mem_swapped_out", dev->stats[VIRTIO_BALLOON_S_SWAP_OUT]);
    stat_put(dict, "major_page_faults", dev->stats[VIRTIO_BALLOON_S_MAJFLT]);
    stat_put(dict, "minor_page_faults", dev->stats[VIRTIO_BALLOON_S_MINFLT]);
    stat_put(dict, "free_mem", dev->stats[VIRTIO_BALLOON_S_MEMFREE]);
    stat_put(dict, "total_mem", dev->stats[VIRTIO_BALLOON_S_MEMTOT]);
#endif

    return QOBJECT(dict);
}

static void virtio_balloon_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *s = to_virtio_balloon(vdev);
    VirtQueueElement elem;

    while (virtqueue_pop(vq, &elem)) {
        size_t offset = 0;
        uint32_t pfn;

        while (iov_to_buf(elem.out_sg, elem.out_num, &pfn, offset, 4) == 4) {
            ram_addr_t pa;
            ram_addr_t addr;

            pa = (ram_addr_t)ldl_p(&pfn) << VIRTIO_BALLOON_PFN_SHIFT;
            offset += 4;

            addr = cpu_get_physical_page_desc(pa);
            if ((addr & ~TARGET_PAGE_MASK) != IO_MEM_RAM)
                continue;

            /* Using qemu_get_ram_ptr is bending the rules a bit, but
               should be OK because we only want a single page.  */
            balloon_page(qemu_get_ram_ptr(addr), !!(vq == s->dvq));
        }

        virtqueue_push(vq, &elem, offset);
        virtio_notify(vdev, vq);
    }
}

static void complete_stats_request(VirtIOBalloon *vb)
{
    QObject *stats;

    if (!vb->stats_opaque_callback_data)
        return;

    stats = get_stats_qobject(vb);
    vb->stats_callback(vb->stats_opaque_callback_data, stats);
    qobject_decref(stats);
    vb->stats_opaque_callback_data = NULL;
    vb->stats_callback = NULL;
}

static void virtio_balloon_receive_stats(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *s = DO_UPCAST(VirtIOBalloon, vdev, vdev);
    VirtQueueElement *elem = &s->stats_vq_elem;
    VirtIOBalloonStat stat;
    size_t offset = 0;

    if (!virtqueue_pop(vq, elem)) {
        return;
    }

    /* Initialize the stats to get rid of any stale values.  This is only
     * needed to handle the case where a guest supports fewer stats than it
     * used to (ie. it has booted into an old kernel).
     */
    reset_stats(s);

    while (iov_to_buf(elem->out_sg, elem->out_num, &stat, offset, sizeof(stat))
           == sizeof(stat)) {
        uint16_t tag = tswap16(stat.tag);
        uint64_t val = tswap64(stat.val);

        offset += sizeof(stat);
        if (tag < VIRTIO_BALLOON_S_NR)
            s->stats[tag] = val;
    }
    s->stats_vq_offset = offset;

    complete_stats_request(s);
}

static void virtio_balloon_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOBalloon *dev = to_virtio_balloon(vdev);
    struct virtio_balloon_config config;

    config.num_pages = cpu_to_le32(dev->num_pages);
    config.actual = cpu_to_le32(dev->actual);

    memcpy(config_data, &config, 8);
}

static void virtio_balloon_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
    VirtIOBalloon *dev = to_virtio_balloon(vdev);
    struct virtio_balloon_config config;
    memcpy(&config, config_data, 8);
    dev->actual = le32_to_cpu(config.actual);
}

static uint32_t virtio_balloon_get_features(VirtIODevice *vdev, uint32_t f)
{
    f |= (1 << VIRTIO_BALLOON_F_STATS_VQ);
    return f;
}

static void virtio_balloon_stat(void *opaque, MonitorCompletion cb,
                                void *cb_data)
{
    VirtIOBalloon *dev = opaque;

    /* For now, only allow one request at a time.  This restriction can be
     * removed later by queueing callback and data pairs.
     */
    if (dev->stats_callback != NULL) {
        return;
    }
    dev->stats_callback = cb;
    dev->stats_opaque_callback_data = cb_data;

    if (ENABLE_GUEST_STATS
        && (dev->vdev.guest_features & (1 << VIRTIO_BALLOON_F_STATS_VQ))) {
        virtqueue_push(dev->svq, &dev->stats_vq_elem, dev->stats_vq_offset);
        virtio_notify(&dev->vdev, dev->svq);
        return;
    }

    /* Stats are not supported.  Clear out any stale values that might
     * have been set by a more featureful guest kernel.
     */
    reset_stats(dev);
    complete_stats_request(dev);
}

static void virtio_balloon_to_target(void *opaque, ram_addr_t target)
{
    VirtIOBalloon *dev = opaque;

    if (target > ram_size) {
        target = ram_size;
    }
    if (target) {
        dev->num_pages = (ram_size - target) >> VIRTIO_BALLOON_PFN_SHIFT;
        virtio_notify_config(&dev->vdev);
    }
}

static void virtio_balloon_save(QEMUFile *f, void *opaque)
{
    VirtIOBalloon *s = opaque;

    virtio_save(&s->vdev, f);

    qemu_put_be32(f, s->num_pages);
    qemu_put_be32(f, s->actual);
}

static int virtio_balloon_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIOBalloon *s = opaque;

    if (version_id != 1)
        return -EINVAL;

    virtio_load(&s->vdev, f);

    s->num_pages = qemu_get_be32(f);
    s->actual = qemu_get_be32(f);
    return 0;
}

VirtIODevice *virtio_balloon_init(DeviceState *dev)
{
    VirtIOBalloon *s;
    int ret;

    s = (VirtIOBalloon *)virtio_common_init("virtio-balloon",
                                            VIRTIO_ID_BALLOON,
                                            8, sizeof(VirtIOBalloon));

    s->vdev.get_config = virtio_balloon_get_config;
    s->vdev.set_config = virtio_balloon_set_config;
    s->vdev.get_features = virtio_balloon_get_features;

    ret = qemu_add_balloon_handler(virtio_balloon_to_target,
                                   virtio_balloon_stat, s);
    if (ret < 0) {
        virtio_cleanup(&s->vdev);
        return NULL;
    }

    s->ivq = virtio_add_queue(&s->vdev, 128, virtio_balloon_handle_output);
    s->dvq = virtio_add_queue(&s->vdev, 128, virtio_balloon_handle_output);
    s->svq = virtio_add_queue(&s->vdev, 128, virtio_balloon_receive_stats);

    reset_stats(s);

    register_savevm(dev, "virtio-balloon", -1, 1,
                    virtio_balloon_save, virtio_balloon_load, s);

    return &s->vdev;
}

void virtio_balloon_exit(VirtIODevice *vdev)
{
    virtio_cleanup(vdev);
}
