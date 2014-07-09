/* Copyright 2012 Red Hat, Inc.
 * Copyright IBM, Corp. 2012
 *
 * Based on Linux 2.6.39 vhost code:
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2006 Rusty Russell IBM Corporation
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *         Stefan Hajnoczi <stefanha@redhat.com>
 *
 * Inspiration, some code, and most witty comments come from
 * Documentation/virtual/lguest/lguest.c, by Rusty Russell
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */

#include "trace.h"
#include "hw/hw.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/virtio/dataplane/vring.h"
#include "qemu/error-report.h"

/* vring_map can be coupled with vring_unmap or (if you still have the
 * value returned in *mr) memory_region_unref.
 */
static void *vring_map(MemoryRegion **mr, hwaddr phys, hwaddr len,
                       bool is_write)
{
    MemoryRegionSection section = memory_region_find(get_system_memory(), phys, len);

    if (!section.mr || int128_get64(section.size) < len) {
        goto out;
    }
    if (is_write && section.readonly) {
        goto out;
    }
    if (!memory_region_is_ram(section.mr)) {
        goto out;
    }

    /* Ignore regions with dirty logging, we cannot mark them dirty */
    if (memory_region_is_logging(section.mr)) {
        goto out;
    }

    *mr = section.mr;
    return memory_region_get_ram_ptr(section.mr) + section.offset_within_region;

out:
    memory_region_unref(section.mr);
    *mr = NULL;
    return NULL;
}

static void vring_unmap(void *buffer, bool is_write)
{
    ram_addr_t addr;
    MemoryRegion *mr;

    mr = qemu_ram_addr_from_host(buffer, &addr);
    memory_region_unref(mr);
}

/* Map the guest's vring to host memory */
bool vring_setup(Vring *vring, VirtIODevice *vdev, int n)
{
    hwaddr vring_addr = virtio_queue_get_ring_addr(vdev, n);
    hwaddr vring_size = virtio_queue_get_ring_size(vdev, n);
    void *vring_ptr;

    vring->broken = false;

    vring_ptr = vring_map(&vring->mr, vring_addr, vring_size, true);
    if (!vring_ptr) {
        error_report("Failed to map vring "
                     "addr %#" HWADDR_PRIx " size %" HWADDR_PRIu,
                     vring_addr, vring_size);
        vring->broken = true;
        return false;
    }

    vring_init(&vring->vr, virtio_queue_get_num(vdev, n), vring_ptr, 4096);

    vring->last_avail_idx = virtio_queue_get_last_avail_idx(vdev, n);
    vring->last_used_idx = vring->vr.used->idx;
    vring->signalled_used = 0;
    vring->signalled_used_valid = false;

    trace_vring_setup(virtio_queue_get_ring_addr(vdev, n),
                      vring->vr.desc, vring->vr.avail, vring->vr.used);
    return true;
}

void vring_teardown(Vring *vring, VirtIODevice *vdev, int n)
{
    virtio_queue_set_last_avail_idx(vdev, n, vring->last_avail_idx);
    virtio_queue_invalidate_signalled_used(vdev, n);

    memory_region_unref(vring->mr);
}

/* Disable guest->host notifies */
void vring_disable_notification(VirtIODevice *vdev, Vring *vring)
{
    if (!(vdev->guest_features & (1 << VIRTIO_RING_F_EVENT_IDX))) {
        vring->vr.used->flags |= VRING_USED_F_NO_NOTIFY;
    }
}

/* Enable guest->host notifies
 *
 * Return true if the vring is empty, false if there are more requests.
 */
bool vring_enable_notification(VirtIODevice *vdev, Vring *vring)
{
    if (vdev->guest_features & (1 << VIRTIO_RING_F_EVENT_IDX)) {
        vring_avail_event(&vring->vr) = vring->vr.avail->idx;
    } else {
        vring->vr.used->flags &= ~VRING_USED_F_NO_NOTIFY;
    }
    smp_mb(); /* ensure update is seen before reading avail_idx */
    return !vring_more_avail(vring);
}

/* This is stolen from linux/drivers/vhost/vhost.c:vhost_notify() */
bool vring_should_notify(VirtIODevice *vdev, Vring *vring)
{
    uint16_t old, new;
    bool v;
    /* Flush out used index updates. This is paired
     * with the barrier that the Guest executes when enabling
     * interrupts. */
    smp_mb();

    if ((vdev->guest_features & VIRTIO_F_NOTIFY_ON_EMPTY) &&
        unlikely(vring->vr.avail->idx == vring->last_avail_idx)) {
        return true;
    }

    if (!(vdev->guest_features & VIRTIO_RING_F_EVENT_IDX)) {
        return !(vring->vr.avail->flags & VRING_AVAIL_F_NO_INTERRUPT);
    }
    old = vring->signalled_used;
    v = vring->signalled_used_valid;
    new = vring->signalled_used = vring->last_used_idx;
    vring->signalled_used_valid = true;

    if (unlikely(!v)) {
        return true;
    }

    return vring_need_event(vring_used_event(&vring->vr), new, old);
}


static int get_desc(Vring *vring, VirtQueueElement *elem,
                    struct vring_desc *desc)
{
    unsigned *num;
    struct iovec *iov;
    hwaddr *addr;
    MemoryRegion *mr;

    if (desc->flags & VRING_DESC_F_WRITE) {
        num = &elem->in_num;
        iov = &elem->in_sg[*num];
        addr = &elem->in_addr[*num];
    } else {
        num = &elem->out_num;
        iov = &elem->out_sg[*num];
        addr = &elem->out_addr[*num];

        /* If it's an output descriptor, they're all supposed
         * to come before any input descriptors. */
        if (unlikely(elem->in_num)) {
            error_report("Descriptor has out after in");
            return -EFAULT;
        }
    }

    /* Stop for now if there are not enough iovecs available. */
    if (*num >= VIRTQUEUE_MAX_SIZE) {
        return -ENOBUFS;
    }

    /* TODO handle non-contiguous memory across region boundaries */
    iov->iov_base = vring_map(&mr, desc->addr, desc->len,
                              desc->flags & VRING_DESC_F_WRITE);
    if (!iov->iov_base) {
        error_report("Failed to map descriptor addr %#" PRIx64 " len %u",
                     (uint64_t)desc->addr, desc->len);
        return -EFAULT;
    }

    /* The MemoryRegion is looked up again and unref'ed later, leave the
     * ref in place.  */
    iov->iov_len = desc->len;
    *addr = desc->addr;
    *num += 1;
    return 0;
}

/* This is stolen from linux/drivers/vhost/vhost.c. */
static int get_indirect(Vring *vring, VirtQueueElement *elem,
                        struct vring_desc *indirect)
{
    struct vring_desc desc;
    unsigned int i = 0, count, found = 0;
    int ret;

    /* Sanity check */
    if (unlikely(indirect->len % sizeof(desc))) {
        error_report("Invalid length in indirect descriptor: "
                     "len %#x not multiple of %#zx",
                     indirect->len, sizeof(desc));
        vring->broken = true;
        return -EFAULT;
    }

    count = indirect->len / sizeof(desc);
    /* Buffers are chained via a 16 bit next field, so
     * we can have at most 2^16 of these. */
    if (unlikely(count > USHRT_MAX + 1)) {
        error_report("Indirect buffer length too big: %d", indirect->len);
        vring->broken = true;
        return -EFAULT;
    }

    do {
        struct vring_desc *desc_ptr;
        MemoryRegion *mr;

        /* Translate indirect descriptor */
        desc_ptr = vring_map(&mr,
                             indirect->addr + found * sizeof(desc),
                             sizeof(desc), false);
        if (!desc_ptr) {
            error_report("Failed to map indirect descriptor "
                         "addr %#" PRIx64 " len %zu",
                         (uint64_t)indirect->addr + found * sizeof(desc),
                         sizeof(desc));
            vring->broken = true;
            return -EFAULT;
        }
        desc = *desc_ptr;
        memory_region_unref(mr);

        /* Ensure descriptor has been loaded before accessing fields */
        barrier(); /* read_barrier_depends(); */

        if (unlikely(++found > count)) {
            error_report("Loop detected: last one at %u "
                         "indirect size %u", i, count);
            vring->broken = true;
            return -EFAULT;
        }

        if (unlikely(desc.flags & VRING_DESC_F_INDIRECT)) {
            error_report("Nested indirect descriptor");
            vring->broken = true;
            return -EFAULT;
        }

        ret = get_desc(vring, elem, &desc);
        if (ret < 0) {
            vring->broken |= (ret == -EFAULT);
            return ret;
        }
        i = desc.next;
    } while (desc.flags & VRING_DESC_F_NEXT);
    return 0;
}

static void vring_unmap_element(VirtQueueElement *elem)
{
    int i;

    /* This assumes that the iovecs, if changed, are never moved past
     * the end of the valid area.  This is true if iovec manipulations
     * are done with iov_discard_front and iov_discard_back.
     */
    for (i = 0; i < elem->out_num; i++) {
        vring_unmap(elem->out_sg[i].iov_base, false);
    }

    for (i = 0; i < elem->in_num; i++) {
        vring_unmap(elem->in_sg[i].iov_base, true);
    }
}

/* This looks in the virtqueue and for the first available buffer, and converts
 * it to an iovec for convenient access.  Since descriptors consist of some
 * number of output then some number of input descriptors, it's actually two
 * iovecs, but we pack them into one and note how many of each there were.
 *
 * This function returns the descriptor number found, or vq->num (which is
 * never a valid descriptor number) if none was found.  A negative code is
 * returned on error.
 *
 * Stolen from linux/drivers/vhost/vhost.c.
 */
int vring_pop(VirtIODevice *vdev, Vring *vring,
              VirtQueueElement **p_elem)
{
    struct vring_desc desc;
    unsigned int i, head, found = 0, num = vring->vr.num;
    uint16_t avail_idx, last_avail_idx;
    VirtQueueElement *elem = NULL;
    int ret;

    /* If there was a fatal error then refuse operation */
    if (vring->broken) {
        ret = -EFAULT;
        goto out;
    }

    /* Check it isn't doing very strange things with descriptor numbers. */
    last_avail_idx = vring->last_avail_idx;
    avail_idx = vring->vr.avail->idx;
    barrier(); /* load indices now and not again later */

    if (unlikely((uint16_t)(avail_idx - last_avail_idx) > num)) {
        error_report("Guest moved used index from %u to %u",
                     last_avail_idx, avail_idx);
        ret = -EFAULT;
        goto out;
    }

    /* If there's nothing new since last we looked. */
    if (avail_idx == last_avail_idx) {
        ret = -EAGAIN;
        goto out;
    }

    /* Only get avail ring entries after they have been exposed by guest. */
    smp_rmb();

    /* Grab the next descriptor number they're advertising, and increment
     * the index we've seen. */
    head = vring->vr.avail->ring[last_avail_idx % num];

    elem = g_slice_new(VirtQueueElement);
    elem->index = head;
    elem->in_num = elem->out_num = 0;
    
    /* If their number is silly, that's an error. */
    if (unlikely(head >= num)) {
        error_report("Guest says index %u > %u is available", head, num);
        ret = -EFAULT;
        goto out;
    }

    if (vdev->guest_features & (1 << VIRTIO_RING_F_EVENT_IDX)) {
        vring_avail_event(&vring->vr) = vring->vr.avail->idx;
    }

    i = head;
    do {
        if (unlikely(i >= num)) {
            error_report("Desc index is %u > %u, head = %u", i, num, head);
            ret = -EFAULT;
            goto out;
        }
        if (unlikely(++found > num)) {
            error_report("Loop detected: last one at %u vq size %u head %u",
                         i, num, head);
            ret = -EFAULT;
            goto out;
        }
        desc = vring->vr.desc[i];

        /* Ensure descriptor is loaded before accessing fields */
        barrier();

        if (desc.flags & VRING_DESC_F_INDIRECT) {
            ret = get_indirect(vring, elem, &desc);
            if (ret < 0) {
                goto out;
            }
            continue;
        }

        ret = get_desc(vring, elem, &desc);
        if (ret < 0) {
            goto out;
        }

        i = desc.next;
    } while (desc.flags & VRING_DESC_F_NEXT);

    /* On success, increment avail index. */
    vring->last_avail_idx++;
    *p_elem = elem;
    return head;

out:
    assert(ret < 0);
    if (ret == -EFAULT) {
        vring->broken = true;
    }
    if (elem) {
        vring_unmap_element(elem);
        g_slice_free(VirtQueueElement, elem);
    }
    *p_elem = NULL;
    return ret;
}

/* After we've used one of their buffers, we tell them about it.
 *
 * Stolen from linux/drivers/vhost/vhost.c.
 */
void vring_push(Vring *vring, VirtQueueElement *elem, int len)
{
    struct vring_used_elem *used;
    unsigned int head = elem->index;
    uint16_t new;

    vring_unmap_element(elem);

    /* Don't touch vring if a fatal error occurred */
    if (vring->broken) {
        return;
    }

    /* The virtqueue contains a ring of used buffers.  Get a pointer to the
     * next entry in that used ring. */
    used = &vring->vr.used->ring[vring->last_used_idx % vring->vr.num];
    used->id = head;
    used->len = len;

    /* Make sure buffer is written before we update index. */
    smp_wmb();

    new = vring->vr.used->idx = ++vring->last_used_idx;
    if (unlikely((int16_t)(new - vring->signalled_used) < (uint16_t)1)) {
        vring->signalled_used_valid = false;
    }
}
