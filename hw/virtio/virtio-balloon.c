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

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/madvise.h"
#include "hw/virtio/virtio.h"
#include "hw/mem/pc-dimm.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "sysemu/balloon.h"
#include "hw/virtio/virtio-balloon.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "qapi/qapi-events-machine.h"
#include "qapi/visitor.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "migration/misc.h"
#include "migration/migration.h"

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"

#define BALLOON_PAGE_SIZE  (1 << VIRTIO_BALLOON_PFN_SHIFT)

typedef struct PartiallyBalloonedPage {
    ram_addr_t base_gpa;
    unsigned long *bitmap;
} PartiallyBalloonedPage;

static void virtio_balloon_pbp_free(PartiallyBalloonedPage *pbp)
{
    if (!pbp->bitmap) {
        return;
    }
    g_free(pbp->bitmap);
    pbp->bitmap = NULL;
}

static void virtio_balloon_pbp_alloc(PartiallyBalloonedPage *pbp,
                                     ram_addr_t base_gpa,
                                     long subpages)
{
    pbp->base_gpa = base_gpa;
    pbp->bitmap = bitmap_new(subpages);
}

static bool virtio_balloon_pbp_matches(PartiallyBalloonedPage *pbp,
                                       ram_addr_t base_gpa)
{
    return pbp->base_gpa == base_gpa;
}

static bool virtio_balloon_inhibited(void)
{
    /*
     * Postcopy cannot deal with concurrent discards,
     * so it's special, as well as background snapshots.
     */
    return ram_block_discard_is_disabled() || migration_in_incoming_postcopy() ||
            migration_in_bg_snapshot();
}

static void balloon_inflate_page(VirtIOBalloon *balloon,
                                 MemoryRegion *mr, hwaddr mr_offset,
                                 PartiallyBalloonedPage *pbp)
{
    void *addr = memory_region_get_ram_ptr(mr) + mr_offset;
    ram_addr_t rb_offset, rb_aligned_offset, base_gpa;
    RAMBlock *rb;
    size_t rb_page_size;
    int subpages;

    /* XXX is there a better way to get to the RAMBlock than via a
     * host address? */
    rb = qemu_ram_block_from_host(addr, false, &rb_offset);
    rb_page_size = qemu_ram_pagesize(rb);

    if (rb_page_size == BALLOON_PAGE_SIZE) {
        /* Easy case */

        ram_block_discard_range(rb, rb_offset, rb_page_size);
        /* We ignore errors from ram_block_discard_range(), because it
         * has already reported them, and failing to discard a balloon
         * page is not fatal */
        return;
    }

    /* Hard case
     *
     * We've put a piece of a larger host page into the balloon - we
     * need to keep track until we have a whole host page to
     * discard
     */
    warn_report_once(
"Balloon used with backing page size > 4kiB, this may not be reliable");

    rb_aligned_offset = QEMU_ALIGN_DOWN(rb_offset, rb_page_size);
    subpages = rb_page_size / BALLOON_PAGE_SIZE;
    base_gpa = memory_region_get_ram_addr(mr) + mr_offset -
               (rb_offset - rb_aligned_offset);

    if (pbp->bitmap && !virtio_balloon_pbp_matches(pbp, base_gpa)) {
        /* We've partially ballooned part of a host page, but now
         * we're trying to balloon part of a different one.  Too hard,
         * give up on the old partial page */
        virtio_balloon_pbp_free(pbp);
    }

    if (!pbp->bitmap) {
        virtio_balloon_pbp_alloc(pbp, base_gpa, subpages);
    }

    set_bit((rb_offset - rb_aligned_offset) / BALLOON_PAGE_SIZE,
            pbp->bitmap);

    if (bitmap_full(pbp->bitmap, subpages)) {
        /* We've accumulated a full host page, we can actually discard
         * it now */

        ram_block_discard_range(rb, rb_aligned_offset, rb_page_size);
        /* We ignore errors from ram_block_discard_range(), because it
         * has already reported them, and failing to discard a balloon
         * page is not fatal */
        virtio_balloon_pbp_free(pbp);
    }
}

static void balloon_deflate_page(VirtIOBalloon *balloon,
                                 MemoryRegion *mr, hwaddr mr_offset)
{
    void *addr = memory_region_get_ram_ptr(mr) + mr_offset;
    ram_addr_t rb_offset;
    RAMBlock *rb;
    size_t rb_page_size;
    void *host_addr;
    int ret;

    /* XXX is there a better way to get to the RAMBlock than via a
     * host address? */
    rb = qemu_ram_block_from_host(addr, false, &rb_offset);
    rb_page_size = qemu_ram_pagesize(rb);

    host_addr = (void *)((uintptr_t)addr & ~(rb_page_size - 1));

    /* When a page is deflated, we hint the whole host page it lives
     * on, since we can't do anything smaller */
    ret = qemu_madvise(host_addr, rb_page_size, QEMU_MADV_WILLNEED);
    if (ret != 0) {
        warn_report("Couldn't MADV_WILLNEED on balloon deflate: %s",
                    strerror(errno));
        /* Otherwise ignore, failing to page hint shouldn't be fatal */
    }
}

static const char *balloon_stat_names[] = {
   [VIRTIO_BALLOON_S_SWAP_IN] = "stat-swap-in",
   [VIRTIO_BALLOON_S_SWAP_OUT] = "stat-swap-out",
   [VIRTIO_BALLOON_S_MAJFLT] = "stat-major-faults",
   [VIRTIO_BALLOON_S_MINFLT] = "stat-minor-faults",
   [VIRTIO_BALLOON_S_MEMFREE] = "stat-free-memory",
   [VIRTIO_BALLOON_S_MEMTOT] = "stat-total-memory",
   [VIRTIO_BALLOON_S_AVAIL] = "stat-available-memory",
   [VIRTIO_BALLOON_S_CACHES] = "stat-disk-caches",
   [VIRTIO_BALLOON_S_HTLB_PGALLOC] = "stat-htlb-pgalloc",
   [VIRTIO_BALLOON_S_HTLB_PGFAIL] = "stat-htlb-pgfail",
   [VIRTIO_BALLOON_S_NR] = NULL
};

/*
 * reset_stats - Mark all items in the stats array as unset
 *
 * This function needs to be called at device initialization and before
 * updating to a set of newly-generated stats.  This will ensure that no
 * stale values stick around in case the guest reports a subset of the supported
 * statistics.
 */
static inline void reset_stats(VirtIOBalloon *dev)
{
    int i;
    for (i = 0; i < VIRTIO_BALLOON_S_NR; dev->stats[i++] = -1);
}

static bool balloon_stats_supported(const VirtIOBalloon *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    return virtio_vdev_has_feature(vdev, VIRTIO_BALLOON_F_STATS_VQ);
}

static bool balloon_stats_enabled(const VirtIOBalloon *s)
{
    return s->stats_poll_interval > 0;
}

static void balloon_stats_destroy_timer(VirtIOBalloon *s)
{
    if (balloon_stats_enabled(s)) {
        timer_free(s->stats_timer);
        s->stats_timer = NULL;
        s->stats_poll_interval = 0;
    }
}

static void balloon_stats_change_timer(VirtIOBalloon *s, int64_t secs)
{
    timer_mod(s->stats_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + secs * 1000);
}

static void balloon_stats_poll_cb(void *opaque)
{
    VirtIOBalloon *s = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    if (s->stats_vq_elem == NULL || !balloon_stats_supported(s)) {
        /* re-schedule */
        balloon_stats_change_timer(s, s->stats_poll_interval);
        return;
    }

    virtqueue_push(s->svq, s->stats_vq_elem, 0);
    virtio_notify(vdev, s->svq);
    g_free(s->stats_vq_elem);
    s->stats_vq_elem = NULL;
}

static void balloon_stats_get_all(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(obj);
    bool ok = false;
    int i;

    if (!visit_start_struct(v, name, NULL, 0, errp)) {
        return;
    }
    if (!visit_type_int(v, "last-update", &s->stats_last_update, errp)) {
        goto out_end;
    }

    if (!visit_start_struct(v, "stats", NULL, 0, errp)) {
        goto out_end;
    }
    for (i = 0; i < VIRTIO_BALLOON_S_NR; i++) {
        if (!visit_type_uint64(v, balloon_stat_names[i], &s->stats[i], errp)) {
            goto out_nested;
        }
    }
    ok = visit_check_struct(v, errp);
out_nested:
    visit_end_struct(v, NULL);

    if (ok) {
        visit_check_struct(v, errp);
    }
out_end:
    visit_end_struct(v, NULL);
}

static void balloon_stats_get_poll_interval(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(obj);
    visit_type_int(v, name, &s->stats_poll_interval, errp);
}

static void balloon_stats_set_poll_interval(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(obj);
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    if (value < 0) {
        error_setg(errp, "timer value must be greater than zero");
        return;
    }

    if (value > UINT32_MAX) {
        error_setg(errp, "timer value is too big");
        return;
    }

    if (value == s->stats_poll_interval) {
        return;
    }

    if (value == 0) {
        /* timer=0 disables the timer */
        balloon_stats_destroy_timer(s);
        return;
    }

    if (balloon_stats_enabled(s)) {
        /* timer interval change */
        s->stats_poll_interval = value;
        balloon_stats_change_timer(s, value);
        return;
    }

    /* create a new timer */
    g_assert(s->stats_timer == NULL);
    s->stats_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, balloon_stats_poll_cb, s);
    s->stats_poll_interval = value;
    balloon_stats_change_timer(s, 0);
}

static void virtio_balloon_handle_report(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *dev = VIRTIO_BALLOON(vdev);
    VirtQueueElement *elem;

    while ((elem = virtqueue_pop(vq, sizeof(VirtQueueElement)))) {
        unsigned int i;

        /*
         * When we discard the page it has the effect of removing the page
         * from the hypervisor itself and causing it to be zeroed when it
         * is returned to us. So we must not discard the page if it is
         * accessible by another device or process, or if the guest is
         * expecting it to retain a non-zero value.
         */
        if (virtio_balloon_inhibited() || dev->poison_val) {
            goto skip_element;
        }

        for (i = 0; i < elem->in_num; i++) {
            void *addr = elem->in_sg[i].iov_base;
            size_t size = elem->in_sg[i].iov_len;
            ram_addr_t ram_offset;
            RAMBlock *rb;

            /*
             * There is no need to check the memory section to see if
             * it is ram/readonly/romd like there is for handle_output
             * below. If the region is not meant to be written to then
             * address_space_map will have allocated a bounce buffer
             * and it will be freed in address_space_unmap and trigger
             * and unassigned_mem_write before failing to copy over the
             * buffer. If more than one bad descriptor is provided it
             * will return NULL after the first bounce buffer and fail
             * to map any resources.
             */
            rb = qemu_ram_block_from_host(addr, false, &ram_offset);
            if (!rb) {
                trace_virtio_balloon_bad_addr(elem->in_addr[i]);
                continue;
            }

            /*
             * For now we will simply ignore unaligned memory regions, or
             * regions that overrun the end of the RAMBlock.
             */
            if (!QEMU_IS_ALIGNED(ram_offset | size, qemu_ram_pagesize(rb)) ||
                (ram_offset + size) > qemu_ram_get_used_length(rb)) {
                continue;
            }

            ram_block_discard_range(rb, ram_offset, size);
        }

skip_element:
        virtqueue_push(vq, elem, 0);
        virtio_notify(vdev, vq);
        g_free(elem);
    }
}

static void virtio_balloon_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);
    VirtQueueElement *elem;
    MemoryRegionSection section;

    for (;;) {
        PartiallyBalloonedPage pbp = {};
        size_t offset = 0;
        uint32_t pfn;

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }

        while (iov_to_buf(elem->out_sg, elem->out_num, offset, &pfn, 4) == 4) {
            unsigned int p = virtio_ldl_p(vdev, &pfn);
            hwaddr pa;

            pa = (hwaddr) p << VIRTIO_BALLOON_PFN_SHIFT;
            offset += 4;

            section = memory_region_find(get_system_memory(), pa,
                                         BALLOON_PAGE_SIZE);
            if (!section.mr) {
                trace_virtio_balloon_bad_addr(pa);
                continue;
            }
            if (!memory_region_is_ram(section.mr) ||
                memory_region_is_rom(section.mr) ||
                memory_region_is_romd(section.mr)) {
                trace_virtio_balloon_bad_addr(pa);
                memory_region_unref(section.mr);
                continue;
            }

            trace_virtio_balloon_handle_output(memory_region_name(section.mr),
                                               pa);
            if (!virtio_balloon_inhibited()) {
                if (vq == s->ivq) {
                    balloon_inflate_page(s, section.mr,
                                         section.offset_within_region, &pbp);
                } else if (vq == s->dvq) {
                    balloon_deflate_page(s, section.mr, section.offset_within_region);
                } else {
                    g_assert_not_reached();
                }
            }
            memory_region_unref(section.mr);
        }

        virtqueue_push(vq, elem, 0);
        virtio_notify(vdev, vq);
        g_free(elem);
        virtio_balloon_pbp_free(&pbp);
    }
}

static void virtio_balloon_receive_stats(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);
    VirtQueueElement *elem;
    VirtIOBalloonStat stat;
    size_t offset = 0;

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!elem) {
        goto out;
    }

    if (s->stats_vq_elem != NULL) {
        /* This should never happen if the driver follows the spec. */
        virtqueue_push(vq, s->stats_vq_elem, 0);
        virtio_notify(vdev, vq);
        g_free(s->stats_vq_elem);
    }

    s->stats_vq_elem = elem;

    /* Initialize the stats to get rid of any stale values.  This is only
     * needed to handle the case where a guest supports fewer stats than it
     * used to (ie. it has booted into an old kernel).
     */
    reset_stats(s);

    while (iov_to_buf(elem->out_sg, elem->out_num, offset, &stat, sizeof(stat))
           == sizeof(stat)) {
        uint16_t tag = virtio_tswap16(vdev, stat.tag);
        uint64_t val = virtio_tswap64(vdev, stat.val);

        offset += sizeof(stat);
        if (tag < VIRTIO_BALLOON_S_NR)
            s->stats[tag] = val;
    }
    s->stats_vq_offset = offset;
    s->stats_last_update = g_get_real_time() / G_USEC_PER_SEC;

out:
    if (balloon_stats_enabled(s)) {
        balloon_stats_change_timer(s, s->stats_poll_interval);
    }
}

static void virtio_balloon_handle_free_page_vq(VirtIODevice *vdev,
                                               VirtQueue *vq)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);
    qemu_bh_schedule(s->free_page_bh);
}

static bool get_free_page_hints(VirtIOBalloon *dev)
{
    VirtQueueElement *elem;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtQueue *vq = dev->free_page_vq;
    bool ret = true;
    int i;

    while (dev->block_iothread) {
        qemu_cond_wait(&dev->free_page_cond, &dev->free_page_lock);
    }

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!elem) {
        return false;
    }

    if (elem->out_num) {
        uint32_t id;
        size_t size = iov_to_buf(elem->out_sg, elem->out_num, 0,
                                 &id, sizeof(id));

        virtio_tswap32s(vdev, &id);
        if (unlikely(size != sizeof(id))) {
            virtio_error(vdev, "received an incorrect cmd id");
            ret = false;
            goto out;
        }
        if (dev->free_page_hint_status == FREE_PAGE_HINT_S_REQUESTED &&
            id == dev->free_page_hint_cmd_id) {
            dev->free_page_hint_status = FREE_PAGE_HINT_S_START;
        } else if (dev->free_page_hint_status == FREE_PAGE_HINT_S_START) {
            /*
             * Stop the optimization only when it has started. This
             * avoids a stale stop sign for the previous command.
             */
            dev->free_page_hint_status = FREE_PAGE_HINT_S_STOP;
        }
    }

    if (elem->in_num && dev->free_page_hint_status == FREE_PAGE_HINT_S_START) {
        for (i = 0; i < elem->in_num; i++) {
            qemu_guest_free_page_hint(elem->in_sg[i].iov_base,
                                      elem->in_sg[i].iov_len);
        }
    }

out:
    virtqueue_push(vq, elem, 0);
    g_free(elem);
    return ret;
}

static void virtio_ballloon_get_free_page_hints(void *opaque)
{
    VirtIOBalloon *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtQueue *vq = dev->free_page_vq;
    bool continue_to_get_hints;

    do {
        qemu_mutex_lock(&dev->free_page_lock);
        virtio_queue_set_notification(vq, 0);
        continue_to_get_hints = get_free_page_hints(dev);
        qemu_mutex_unlock(&dev->free_page_lock);
        virtio_notify(vdev, vq);
      /*
       * Start to poll the vq once the hinting started. Otherwise, continue
       * only when there are entries on the vq, which need to be given back.
       */
    } while (continue_to_get_hints ||
             dev->free_page_hint_status == FREE_PAGE_HINT_S_START);
    virtio_queue_set_notification(vq, 1);
}

static bool virtio_balloon_free_page_support(void *opaque)
{
    VirtIOBalloon *s = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    return virtio_vdev_has_feature(vdev, VIRTIO_BALLOON_F_FREE_PAGE_HINT);
}

static void virtio_balloon_free_page_start(VirtIOBalloon *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    qemu_mutex_lock(&s->free_page_lock);

    if (s->free_page_hint_cmd_id == UINT_MAX) {
        s->free_page_hint_cmd_id = VIRTIO_BALLOON_FREE_PAGE_HINT_CMD_ID_MIN;
    } else {
        s->free_page_hint_cmd_id++;
    }

    s->free_page_hint_status = FREE_PAGE_HINT_S_REQUESTED;
    qemu_mutex_unlock(&s->free_page_lock);

    virtio_notify_config(vdev);
}

static void virtio_balloon_free_page_stop(VirtIOBalloon *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    if (s->free_page_hint_status != FREE_PAGE_HINT_S_STOP) {
        /*
         * The lock also guarantees us that the
         * virtio_ballloon_get_free_page_hints exits after the
         * free_page_hint_status is set to S_STOP.
         */
        qemu_mutex_lock(&s->free_page_lock);
        /*
         * The guest isn't done hinting, so send a notification
         * to the guest to actively stop the hinting.
         */
        s->free_page_hint_status = FREE_PAGE_HINT_S_STOP;
        qemu_mutex_unlock(&s->free_page_lock);
        virtio_notify_config(vdev);
    }
}

static void virtio_balloon_free_page_done(VirtIOBalloon *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    if (s->free_page_hint_status != FREE_PAGE_HINT_S_DONE) {
        /* See virtio_balloon_free_page_stop() */
        qemu_mutex_lock(&s->free_page_lock);
        s->free_page_hint_status = FREE_PAGE_HINT_S_DONE;
        qemu_mutex_unlock(&s->free_page_lock);
        virtio_notify_config(vdev);
    }
}

static int
virtio_balloon_free_page_hint_notify(NotifierWithReturn *n, void *data)
{
    VirtIOBalloon *dev = container_of(n, VirtIOBalloon, free_page_hint_notify);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    PrecopyNotifyData *pnd = data;

    if (!virtio_balloon_free_page_support(dev)) {
        /*
         * This is an optimization provided to migration, so just return 0 to
         * have the normal migration process not affected when this feature is
         * not supported.
         */
        return 0;
    }

    /*
     * Pages hinted via qemu_guest_free_page_hint() are cleared from the dirty
     * bitmap and will not get migrated, especially also not when the postcopy
     * destination starts using them and requests migration from the source; the
     * faulting thread will stall until postcopy migration finishes and
     * all threads are woken up. Let's not start free page hinting if postcopy
     * is possible.
     */
    if (migrate_postcopy_ram()) {
        return 0;
    }

    switch (pnd->reason) {
    case PRECOPY_NOTIFY_BEFORE_BITMAP_SYNC:
        virtio_balloon_free_page_stop(dev);
        break;
    case PRECOPY_NOTIFY_AFTER_BITMAP_SYNC:
        if (vdev->vm_running) {
            virtio_balloon_free_page_start(dev);
            break;
        }
        /*
         * Set S_DONE before migrating the vmstate, so the guest will reuse
         * all hinted pages once running on the destination. Fall through.
         */
    case PRECOPY_NOTIFY_CLEANUP:
        /*
         * Especially, if something goes wrong during precopy or if migration
         * is canceled, we have to properly communicate S_DONE to the VM.
         */
        virtio_balloon_free_page_done(dev);
        break;
    case PRECOPY_NOTIFY_SETUP:
    case PRECOPY_NOTIFY_COMPLETE:
        break;
    default:
        virtio_error(vdev, "%s: %d reason unknown", __func__, pnd->reason);
    }

    return 0;
}

static size_t virtio_balloon_config_size(VirtIOBalloon *s)
{
    uint64_t features = s->host_features;

    if (s->qemu_4_0_config_size) {
        return sizeof(struct virtio_balloon_config);
    }
    if (virtio_has_feature(features, VIRTIO_BALLOON_F_PAGE_POISON)) {
        return sizeof(struct virtio_balloon_config);
    }
    if (virtio_has_feature(features, VIRTIO_BALLOON_F_FREE_PAGE_HINT)) {
        return offsetof(struct virtio_balloon_config, poison_val);
    }
    return offsetof(struct virtio_balloon_config, free_page_hint_cmd_id);
}

static void virtio_balloon_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOBalloon *dev = VIRTIO_BALLOON(vdev);
    struct virtio_balloon_config config = {};

    config.num_pages = cpu_to_le32(dev->num_pages);
    config.actual = cpu_to_le32(dev->actual);
    config.poison_val = cpu_to_le32(dev->poison_val);

    if (dev->free_page_hint_status == FREE_PAGE_HINT_S_REQUESTED) {
        config.free_page_hint_cmd_id =
                       cpu_to_le32(dev->free_page_hint_cmd_id);
    } else if (dev->free_page_hint_status == FREE_PAGE_HINT_S_STOP) {
        config.free_page_hint_cmd_id =
                       cpu_to_le32(VIRTIO_BALLOON_CMD_ID_STOP);
    } else if (dev->free_page_hint_status == FREE_PAGE_HINT_S_DONE) {
        config.free_page_hint_cmd_id =
                       cpu_to_le32(VIRTIO_BALLOON_CMD_ID_DONE);
    }

    trace_virtio_balloon_get_config(config.num_pages, config.actual);
    memcpy(config_data, &config, virtio_balloon_config_size(dev));
}

static int build_dimm_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_PC_DIMM)) {
        DeviceState *dev = DEVICE(obj);
        if (dev->realized) { /* only realized DIMMs matter */
            *list = g_slist_prepend(*list, dev);
        }
    }

    object_child_foreach(obj, build_dimm_list, opaque);
    return 0;
}

static ram_addr_t get_current_ram_size(void)
{
    GSList *list = NULL, *item;
    ram_addr_t size = current_machine->ram_size;

    build_dimm_list(qdev_get_machine(), &list);
    for (item = list; item; item = g_slist_next(item)) {
        Object *obj = OBJECT(item->data);
        if (!strcmp(object_get_typename(obj), TYPE_PC_DIMM)) {
            size += object_property_get_int(obj, PC_DIMM_SIZE_PROP,
                                            &error_abort);
        }
    }
    g_slist_free(list);

    return size;
}

static bool virtio_balloon_page_poison_support(void *opaque)
{
    VirtIOBalloon *s = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    return virtio_vdev_has_feature(vdev, VIRTIO_BALLOON_F_PAGE_POISON);
}

static void virtio_balloon_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
    VirtIOBalloon *dev = VIRTIO_BALLOON(vdev);
    struct virtio_balloon_config config;
    uint32_t oldactual = dev->actual;
    ram_addr_t vm_ram_size = get_current_ram_size();

    memcpy(&config, config_data, virtio_balloon_config_size(dev));
    dev->actual = le32_to_cpu(config.actual);
    if (dev->actual != oldactual) {
        qapi_event_send_balloon_change(vm_ram_size -
                        ((ram_addr_t) dev->actual << VIRTIO_BALLOON_PFN_SHIFT));
    }
    dev->poison_val = 0;
    if (virtio_balloon_page_poison_support(dev)) {
        dev->poison_val = le32_to_cpu(config.poison_val);
    }
    trace_virtio_balloon_set_config(dev->actual, oldactual);
}

static uint64_t virtio_balloon_get_features(VirtIODevice *vdev, uint64_t f,
                                            Error **errp)
{
    VirtIOBalloon *dev = VIRTIO_BALLOON(vdev);
    f |= dev->host_features;
    virtio_add_feature(&f, VIRTIO_BALLOON_F_STATS_VQ);

    return f;
}

static void virtio_balloon_stat(void *opaque, BalloonInfo *info)
{
    VirtIOBalloon *dev = opaque;
    info->actual = get_current_ram_size() - ((uint64_t) dev->actual <<
                                             VIRTIO_BALLOON_PFN_SHIFT);
}

static void virtio_balloon_to_target(void *opaque, ram_addr_t target)
{
    VirtIOBalloon *dev = VIRTIO_BALLOON(opaque);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    ram_addr_t vm_ram_size = get_current_ram_size();

    if (target > vm_ram_size) {
        target = vm_ram_size;
    }
    if (target) {
        dev->num_pages = (vm_ram_size - target) >> VIRTIO_BALLOON_PFN_SHIFT;
        virtio_notify_config(vdev);
    }
    trace_virtio_balloon_to_target(target, dev->num_pages);
}

static int virtio_balloon_post_load_device(void *opaque, int version_id)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(opaque);

    if (balloon_stats_enabled(s)) {
        balloon_stats_change_timer(s, s->stats_poll_interval);
    }
    return 0;
}

static const VMStateDescription vmstate_virtio_balloon_free_page_hint = {
    .name = "virtio-balloon-device/free-page-report",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = virtio_balloon_free_page_support,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(free_page_hint_cmd_id, VirtIOBalloon),
        VMSTATE_UINT32(free_page_hint_status, VirtIOBalloon),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_balloon_page_poison = {
    .name = "virtio-balloon-device/page-poison",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = virtio_balloon_page_poison_support,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(poison_val, VirtIOBalloon),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_balloon_device = {
    .name = "virtio-balloon-device",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = virtio_balloon_post_load_device,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(num_pages, VirtIOBalloon),
        VMSTATE_UINT32(actual, VirtIOBalloon),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_virtio_balloon_free_page_hint,
        &vmstate_virtio_balloon_page_poison,
        NULL
    }
};

static void virtio_balloon_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOBalloon *s = VIRTIO_BALLOON(dev);
    int ret;

    virtio_init(vdev, VIRTIO_ID_BALLOON, virtio_balloon_config_size(s));

    ret = qemu_add_balloon_handler(virtio_balloon_to_target,
                                   virtio_balloon_stat, s);

    if (ret < 0) {
        error_setg(errp, "Only one balloon device is supported");
        virtio_cleanup(vdev);
        return;
    }

    if (virtio_has_feature(s->host_features, VIRTIO_BALLOON_F_FREE_PAGE_HINT) &&
        !s->iothread) {
        error_setg(errp, "'free-page-hint' requires 'iothread' to be set");
        virtio_cleanup(vdev);
        return;
    }

    s->ivq = virtio_add_queue(vdev, 128, virtio_balloon_handle_output);
    s->dvq = virtio_add_queue(vdev, 128, virtio_balloon_handle_output);
    s->svq = virtio_add_queue(vdev, 128, virtio_balloon_receive_stats);

    if (virtio_has_feature(s->host_features, VIRTIO_BALLOON_F_FREE_PAGE_HINT)) {
        s->free_page_vq = virtio_add_queue(vdev, VIRTQUEUE_MAX_SIZE,
                                           virtio_balloon_handle_free_page_vq);
        precopy_add_notifier(&s->free_page_hint_notify);

        object_ref(OBJECT(s->iothread));
        s->free_page_bh = aio_bh_new(iothread_get_aio_context(s->iothread),
                                     virtio_ballloon_get_free_page_hints, s);
    }

    if (virtio_has_feature(s->host_features, VIRTIO_BALLOON_F_REPORTING)) {
        s->reporting_vq = virtio_add_queue(vdev, 32,
                                           virtio_balloon_handle_report);
    }

    reset_stats(s);
}

static void virtio_balloon_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOBalloon *s = VIRTIO_BALLOON(dev);

    if (s->free_page_bh) {
        qemu_bh_delete(s->free_page_bh);
        object_unref(OBJECT(s->iothread));
        virtio_balloon_free_page_stop(s);
        precopy_remove_notifier(&s->free_page_hint_notify);
    }
    balloon_stats_destroy_timer(s);
    qemu_remove_balloon_handler(s);

    virtio_delete_queue(s->ivq);
    virtio_delete_queue(s->dvq);
    virtio_delete_queue(s->svq);
    if (s->free_page_vq) {
        virtio_delete_queue(s->free_page_vq);
    }
    if (s->reporting_vq) {
        virtio_delete_queue(s->reporting_vq);
    }
    virtio_cleanup(vdev);
}

static void virtio_balloon_device_reset(VirtIODevice *vdev)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);

    if (virtio_balloon_free_page_support(s)) {
        virtio_balloon_free_page_stop(s);
    }

    if (s->stats_vq_elem != NULL) {
        virtqueue_unpop(s->svq, s->stats_vq_elem, 0);
        g_free(s->stats_vq_elem);
        s->stats_vq_elem = NULL;
    }

    s->poison_val = 0;
}

static void virtio_balloon_set_status(VirtIODevice *vdev, uint8_t status)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);

    if (!s->stats_vq_elem && vdev->vm_running &&
        (status & VIRTIO_CONFIG_S_DRIVER_OK) && virtqueue_rewind(s->svq, 1)) {
        /* poll stats queue for the element we have discarded when the VM
         * was stopped */
        virtio_balloon_receive_stats(vdev, s->svq);
    }

    if (virtio_balloon_free_page_support(s)) {
        /*
         * The VM is woken up and the iothread was blocked, so signal it to
         * continue.
         */
        if (vdev->vm_running && s->block_iothread) {
            qemu_mutex_lock(&s->free_page_lock);
            s->block_iothread = false;
            qemu_cond_signal(&s->free_page_cond);
            qemu_mutex_unlock(&s->free_page_lock);
        }

        /* The VM is stopped, block the iothread. */
        if (!vdev->vm_running) {
            qemu_mutex_lock(&s->free_page_lock);
            s->block_iothread = true;
            qemu_mutex_unlock(&s->free_page_lock);
        }
    }
}

static void virtio_balloon_instance_init(Object *obj)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(obj);

    qemu_mutex_init(&s->free_page_lock);
    qemu_cond_init(&s->free_page_cond);
    s->free_page_hint_cmd_id = VIRTIO_BALLOON_FREE_PAGE_HINT_CMD_ID_MIN;
    s->free_page_hint_notify.notify = virtio_balloon_free_page_hint_notify;

    object_property_add(obj, "guest-stats", "guest statistics",
                        balloon_stats_get_all, NULL, NULL, NULL);

    object_property_add(obj, "guest-stats-polling-interval", "int",
                        balloon_stats_get_poll_interval,
                        balloon_stats_set_poll_interval,
                        NULL, NULL);
}

static const VMStateDescription vmstate_virtio_balloon = {
    .name = "virtio-balloon",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_balloon_properties[] = {
    DEFINE_PROP_BIT("deflate-on-oom", VirtIOBalloon, host_features,
                    VIRTIO_BALLOON_F_DEFLATE_ON_OOM, false),
    DEFINE_PROP_BIT("free-page-hint", VirtIOBalloon, host_features,
                    VIRTIO_BALLOON_F_FREE_PAGE_HINT, false),
    DEFINE_PROP_BIT("page-poison", VirtIOBalloon, host_features,
                    VIRTIO_BALLOON_F_PAGE_POISON, true),
    DEFINE_PROP_BIT("free-page-reporting", VirtIOBalloon, host_features,
                    VIRTIO_BALLOON_F_REPORTING, false),
    /* QEMU 4.0 accidentally changed the config size even when free-page-hint
     * is disabled, resulting in QEMU 3.1 migration incompatibility.  This
     * property retains this quirk for QEMU 4.1 machine types.
     */
    DEFINE_PROP_BOOL("qemu-4-0-config-size", VirtIOBalloon,
                     qemu_4_0_config_size, false),
    DEFINE_PROP_LINK("iothread", VirtIOBalloon, iothread, TYPE_IOTHREAD,
                     IOThread *),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_balloon_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_balloon_properties);
    dc->vmsd = &vmstate_virtio_balloon;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_balloon_device_realize;
    vdc->unrealize = virtio_balloon_device_unrealize;
    vdc->reset = virtio_balloon_device_reset;
    vdc->get_config = virtio_balloon_get_config;
    vdc->set_config = virtio_balloon_set_config;
    vdc->get_features = virtio_balloon_get_features;
    vdc->set_status = virtio_balloon_set_status;
    vdc->vmsd = &vmstate_virtio_balloon_device;
}

static const TypeInfo virtio_balloon_info = {
    .name = TYPE_VIRTIO_BALLOON,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOBalloon),
    .instance_init = virtio_balloon_instance_init,
    .class_init = virtio_balloon_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_balloon_info);
}

type_init(virtio_register_types)
