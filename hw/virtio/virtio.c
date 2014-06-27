/*
 * Virtio Support
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

#include <inttypes.h>

#include "trace.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio.h"
#include "qemu/atomic.h"
#include "hw/virtio/virtio-bus.h"

/*
 * The alignment to use between consumer and producer parts of vring.
 * x86 pagesize again. This is the default, used by transports like PCI
 * which don't provide a means for the guest to tell the host the alignment.
 */
#define VIRTIO_PCI_VRING_ALIGN         4096

typedef struct VRingDesc
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} VRingDesc;

typedef struct VRingAvail
{
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[0];
} VRingAvail;

typedef struct VRingUsedElem
{
    uint32_t id;
    uint32_t len;
} VRingUsedElem;

typedef struct VRingUsed
{
    uint16_t flags;
    uint16_t idx;
    VRingUsedElem ring[0];
} VRingUsed;

typedef struct VRing
{
    unsigned int num;
    unsigned int align;
    hwaddr desc;
    hwaddr avail;
    hwaddr used;
} VRing;

struct VirtQueue
{
    VRing vring;
    hwaddr pa;
    uint16_t last_avail_idx;
    /* Last used index value we have signalled on */
    uint16_t signalled_used;

    /* Last used index value we have signalled on */
    bool signalled_used_valid;

    /* Notification enabled? */
    bool notification;

    uint16_t queue_index;

    int inuse;

    uint16_t vector;
    void (*handle_output)(VirtIODevice *vdev, VirtQueue *vq);
    VirtIODevice *vdev;
    EventNotifier guest_notifier;
    EventNotifier host_notifier;
};

/* virt queue functions */
static void virtqueue_init(VirtQueue *vq)
{
    hwaddr pa = vq->pa;

    vq->vring.desc = pa;
    vq->vring.avail = pa + vq->vring.num * sizeof(VRingDesc);
    vq->vring.used = vring_align(vq->vring.avail +
                                 offsetof(VRingAvail, ring[vq->vring.num]),
                                 vq->vring.align);
}

static inline uint64_t vring_desc_addr(hwaddr desc_pa, int i)
{
    hwaddr pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, addr);
    return ldq_phys(&address_space_memory, pa);
}

static inline uint32_t vring_desc_len(hwaddr desc_pa, int i)
{
    hwaddr pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, len);
    return ldl_phys(&address_space_memory, pa);
}

static inline uint16_t vring_desc_flags(hwaddr desc_pa, int i)
{
    hwaddr pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, flags);
    return lduw_phys(&address_space_memory, pa);
}

static inline uint16_t vring_desc_next(hwaddr desc_pa, int i)
{
    hwaddr pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, next);
    return lduw_phys(&address_space_memory, pa);
}

static inline uint16_t vring_avail_flags(VirtQueue *vq)
{
    hwaddr pa;
    pa = vq->vring.avail + offsetof(VRingAvail, flags);
    return lduw_phys(&address_space_memory, pa);
}

static inline uint16_t vring_avail_idx(VirtQueue *vq)
{
    hwaddr pa;
    pa = vq->vring.avail + offsetof(VRingAvail, idx);
    return lduw_phys(&address_space_memory, pa);
}

static inline uint16_t vring_avail_ring(VirtQueue *vq, int i)
{
    hwaddr pa;
    pa = vq->vring.avail + offsetof(VRingAvail, ring[i]);
    return lduw_phys(&address_space_memory, pa);
}

static inline uint16_t vring_used_event(VirtQueue *vq)
{
    return vring_avail_ring(vq, vq->vring.num);
}

static inline void vring_used_ring_id(VirtQueue *vq, int i, uint32_t val)
{
    hwaddr pa;
    pa = vq->vring.used + offsetof(VRingUsed, ring[i].id);
    stl_phys(&address_space_memory, pa, val);
}

static inline void vring_used_ring_len(VirtQueue *vq, int i, uint32_t val)
{
    hwaddr pa;
    pa = vq->vring.used + offsetof(VRingUsed, ring[i].len);
    stl_phys(&address_space_memory, pa, val);
}

static uint16_t vring_used_idx(VirtQueue *vq)
{
    hwaddr pa;
    pa = vq->vring.used + offsetof(VRingUsed, idx);
    return lduw_phys(&address_space_memory, pa);
}

static inline void vring_used_idx_set(VirtQueue *vq, uint16_t val)
{
    hwaddr pa;
    pa = vq->vring.used + offsetof(VRingUsed, idx);
    stw_phys(&address_space_memory, pa, val);
}

static inline void vring_used_flags_set_bit(VirtQueue *vq, int mask)
{
    hwaddr pa;
    pa = vq->vring.used + offsetof(VRingUsed, flags);
    stw_phys(&address_space_memory,
             pa, lduw_phys(&address_space_memory, pa) | mask);
}

static inline void vring_used_flags_unset_bit(VirtQueue *vq, int mask)
{
    hwaddr pa;
    pa = vq->vring.used + offsetof(VRingUsed, flags);
    stw_phys(&address_space_memory,
             pa, lduw_phys(&address_space_memory, pa) & ~mask);
}

static inline void vring_avail_event(VirtQueue *vq, uint16_t val)
{
    hwaddr pa;
    if (!vq->notification) {
        return;
    }
    pa = vq->vring.used + offsetof(VRingUsed, ring[vq->vring.num]);
    stw_phys(&address_space_memory, pa, val);
}

void virtio_queue_set_notification(VirtQueue *vq, int enable)
{
    vq->notification = enable;
    if (vq->vdev->guest_features & (1 << VIRTIO_RING_F_EVENT_IDX)) {
        vring_avail_event(vq, vring_avail_idx(vq));
    } else if (enable) {
        vring_used_flags_unset_bit(vq, VRING_USED_F_NO_NOTIFY);
    } else {
        vring_used_flags_set_bit(vq, VRING_USED_F_NO_NOTIFY);
    }
    if (enable) {
        /* Expose avail event/used flags before caller checks the avail idx. */
        smp_mb();
    }
}

int virtio_queue_ready(VirtQueue *vq)
{
    return vq->vring.avail != 0;
}

int virtio_queue_empty(VirtQueue *vq)
{
    return vring_avail_idx(vq) == vq->last_avail_idx;
}

void virtqueue_fill(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len, unsigned int idx)
{
    unsigned int offset;
    int i;

    trace_virtqueue_fill(vq, elem, len, idx);

    offset = 0;
    for (i = 0; i < elem->in_num; i++) {
        size_t size = MIN(len - offset, elem->in_sg[i].iov_len);

        cpu_physical_memory_unmap(elem->in_sg[i].iov_base,
                                  elem->in_sg[i].iov_len,
                                  1, size);

        offset += size;
    }

    for (i = 0; i < elem->out_num; i++)
        cpu_physical_memory_unmap(elem->out_sg[i].iov_base,
                                  elem->out_sg[i].iov_len,
                                  0, elem->out_sg[i].iov_len);

    idx = (idx + vring_used_idx(vq)) % vq->vring.num;

    /* Get a pointer to the next entry in the used ring. */
    vring_used_ring_id(vq, idx, elem->index);
    vring_used_ring_len(vq, idx, len);
}

void virtqueue_flush(VirtQueue *vq, unsigned int count)
{
    uint16_t old, new;
    /* Make sure buffer is written before we update index. */
    smp_wmb();
    trace_virtqueue_flush(vq, count);
    old = vring_used_idx(vq);
    new = old + count;
    vring_used_idx_set(vq, new);
    vq->inuse -= count;
    if (unlikely((int16_t)(new - vq->signalled_used) < (uint16_t)(new - old)))
        vq->signalled_used_valid = false;
}

void virtqueue_push(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len)
{
    virtqueue_fill(vq, elem, len, 0);
    virtqueue_flush(vq, 1);
}

static int virtqueue_num_heads(VirtQueue *vq, unsigned int idx)
{
    uint16_t num_heads = vring_avail_idx(vq) - idx;

    /* Check it isn't doing very strange things with descriptor numbers. */
    if (num_heads > vq->vring.num) {
        error_report("Guest moved used index from %u to %u",
                     idx, vring_avail_idx(vq));
        exit(1);
    }
    /* On success, callers read a descriptor at vq->last_avail_idx.
     * Make sure descriptor read does not bypass avail index read. */
    if (num_heads) {
        smp_rmb();
    }

    return num_heads;
}

static unsigned int virtqueue_get_head(VirtQueue *vq, unsigned int idx)
{
    unsigned int head;

    /* Grab the next descriptor number they're advertising, and increment
     * the index we've seen. */
    head = vring_avail_ring(vq, idx % vq->vring.num);

    /* If their number is silly, that's a fatal mistake. */
    if (head >= vq->vring.num) {
        error_report("Guest says index %u is available", head);
        exit(1);
    }

    return head;
}

static unsigned virtqueue_next_desc(hwaddr desc_pa,
                                    unsigned int i, unsigned int max)
{
    unsigned int next;

    /* If this descriptor says it doesn't chain, we're done. */
    if (!(vring_desc_flags(desc_pa, i) & VRING_DESC_F_NEXT))
        return max;

    /* Check they're not leading us off end of descriptors. */
    next = vring_desc_next(desc_pa, i);
    /* Make sure compiler knows to grab that: we don't want it changing! */
    smp_wmb();

    if (next >= max) {
        error_report("Desc next is %u", next);
        exit(1);
    }

    return next;
}

void virtqueue_get_avail_bytes(VirtQueue *vq, unsigned int *in_bytes,
                               unsigned int *out_bytes,
                               unsigned max_in_bytes, unsigned max_out_bytes)
{
    unsigned int idx;
    unsigned int total_bufs, in_total, out_total;

    idx = vq->last_avail_idx;

    total_bufs = in_total = out_total = 0;
    while (virtqueue_num_heads(vq, idx)) {
        unsigned int max, num_bufs, indirect = 0;
        hwaddr desc_pa;
        int i;

        max = vq->vring.num;
        num_bufs = total_bufs;
        i = virtqueue_get_head(vq, idx++);
        desc_pa = vq->vring.desc;

        if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_INDIRECT) {
            if (vring_desc_len(desc_pa, i) % sizeof(VRingDesc)) {
                error_report("Invalid size for indirect buffer table");
                exit(1);
            }

            /* If we've got too many, that implies a descriptor loop. */
            if (num_bufs >= max) {
                error_report("Looped descriptor");
                exit(1);
            }

            /* loop over the indirect descriptor table */
            indirect = 1;
            max = vring_desc_len(desc_pa, i) / sizeof(VRingDesc);
            desc_pa = vring_desc_addr(desc_pa, i);
            num_bufs = i = 0;
        }

        do {
            /* If we've got too many, that implies a descriptor loop. */
            if (++num_bufs > max) {
                error_report("Looped descriptor");
                exit(1);
            }

            if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_WRITE) {
                in_total += vring_desc_len(desc_pa, i);
            } else {
                out_total += vring_desc_len(desc_pa, i);
            }
            if (in_total >= max_in_bytes && out_total >= max_out_bytes) {
                goto done;
            }
        } while ((i = virtqueue_next_desc(desc_pa, i, max)) != max);

        if (!indirect)
            total_bufs = num_bufs;
        else
            total_bufs++;
    }
done:
    if (in_bytes) {
        *in_bytes = in_total;
    }
    if (out_bytes) {
        *out_bytes = out_total;
    }
}

int virtqueue_avail_bytes(VirtQueue *vq, unsigned int in_bytes,
                          unsigned int out_bytes)
{
    unsigned int in_total, out_total;

    virtqueue_get_avail_bytes(vq, &in_total, &out_total, in_bytes, out_bytes);
    return in_bytes <= in_total && out_bytes <= out_total;
}

void virtqueue_map_sg(struct iovec *sg, hwaddr *addr,
    size_t num_sg, int is_write)
{
    unsigned int i;
    hwaddr len;

    if (num_sg > VIRTQUEUE_MAX_SIZE) {
        error_report("virtio: map attempt out of bounds: %zd > %d",
                     num_sg, VIRTQUEUE_MAX_SIZE);
        exit(1);
    }

    for (i = 0; i < num_sg; i++) {
        len = sg[i].iov_len;
        sg[i].iov_base = cpu_physical_memory_map(addr[i], &len, is_write);
        if (sg[i].iov_base == NULL || len != sg[i].iov_len) {
            error_report("virtio: trying to map MMIO memory");
            exit(1);
        }
    }
}

int virtqueue_pop(VirtQueue *vq, VirtQueueElement *elem)
{
    unsigned int i, head, max;
    hwaddr desc_pa = vq->vring.desc;

    if (!virtqueue_num_heads(vq, vq->last_avail_idx))
        return 0;

    /* When we start there are none of either input nor output. */
    elem->out_num = elem->in_num = 0;

    max = vq->vring.num;

    i = head = virtqueue_get_head(vq, vq->last_avail_idx++);
    if (vq->vdev->guest_features & (1 << VIRTIO_RING_F_EVENT_IDX)) {
        vring_avail_event(vq, vring_avail_idx(vq));
    }

    if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_INDIRECT) {
        if (vring_desc_len(desc_pa, i) % sizeof(VRingDesc)) {
            error_report("Invalid size for indirect buffer table");
            exit(1);
        }

        /* loop over the indirect descriptor table */
        max = vring_desc_len(desc_pa, i) / sizeof(VRingDesc);
        desc_pa = vring_desc_addr(desc_pa, i);
        i = 0;
    }

    /* Collect all the descriptors */
    do {
        struct iovec *sg;

        if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_WRITE) {
            if (elem->in_num >= ARRAY_SIZE(elem->in_sg)) {
                error_report("Too many write descriptors in indirect table");
                exit(1);
            }
            elem->in_addr[elem->in_num] = vring_desc_addr(desc_pa, i);
            sg = &elem->in_sg[elem->in_num++];
        } else {
            if (elem->out_num >= ARRAY_SIZE(elem->out_sg)) {
                error_report("Too many read descriptors in indirect table");
                exit(1);
            }
            elem->out_addr[elem->out_num] = vring_desc_addr(desc_pa, i);
            sg = &elem->out_sg[elem->out_num++];
        }

        sg->iov_len = vring_desc_len(desc_pa, i);

        /* If we've got too many, that implies a descriptor loop. */
        if ((elem->in_num + elem->out_num) > max) {
            error_report("Looped descriptor");
            exit(1);
        }
    } while ((i = virtqueue_next_desc(desc_pa, i, max)) != max);

    /* Now map what we have collected */
    virtqueue_map_sg(elem->in_sg, elem->in_addr, elem->in_num, 1);
    virtqueue_map_sg(elem->out_sg, elem->out_addr, elem->out_num, 0);

    elem->index = head;

    vq->inuse++;

    trace_virtqueue_pop(vq, elem, elem->in_num, elem->out_num);
    return elem->in_num + elem->out_num;
}

/* virtio device */
static void virtio_notify_vector(VirtIODevice *vdev, uint16_t vector)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (k->notify) {
        k->notify(qbus->parent, vector);
    }
}

void virtio_update_irq(VirtIODevice *vdev)
{
    virtio_notify_vector(vdev, VIRTIO_NO_VECTOR);
}

void virtio_set_status(VirtIODevice *vdev, uint8_t val)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    trace_virtio_set_status(vdev, val);

    if (k->set_status) {
        k->set_status(vdev, val);
    }
    vdev->status = val;
}

void virtio_reset(void *opaque)
{
    VirtIODevice *vdev = opaque;
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    int i;

    virtio_set_status(vdev, 0);

    if (k->reset) {
        k->reset(vdev);
    }

    vdev->guest_features = 0;
    vdev->queue_sel = 0;
    vdev->status = 0;
    vdev->isr = 0;
    vdev->config_vector = VIRTIO_NO_VECTOR;
    virtio_notify_vector(vdev, vdev->config_vector);

    for(i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
        vdev->vq[i].vring.desc = 0;
        vdev->vq[i].vring.avail = 0;
        vdev->vq[i].vring.used = 0;
        vdev->vq[i].last_avail_idx = 0;
        vdev->vq[i].pa = 0;
        vdev->vq[i].vector = VIRTIO_NO_VECTOR;
        vdev->vq[i].signalled_used = 0;
        vdev->vq[i].signalled_used_valid = false;
        vdev->vq[i].notification = true;
    }
}

uint32_t virtio_config_readb(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint8_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = ldub_p(vdev->config + addr);
    return val;
}

uint32_t virtio_config_readw(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint16_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = lduw_p(vdev->config + addr);
    return val;
}

uint32_t virtio_config_readl(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = ldl_p(vdev->config + addr);
    return val;
}

void virtio_config_writeb(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint8_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stb_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_config_writew(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint16_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stw_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_config_writel(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stl_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_queue_set_addr(VirtIODevice *vdev, int n, hwaddr addr)
{
    vdev->vq[n].pa = addr;
    virtqueue_init(&vdev->vq[n]);
}

hwaddr virtio_queue_get_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].pa;
}

void virtio_queue_set_num(VirtIODevice *vdev, int n, int num)
{
    /* Don't allow guest to flip queue between existent and
     * nonexistent states, or to set it to an invalid size.
     */
    if (!!num != !!vdev->vq[n].vring.num ||
        num > VIRTQUEUE_MAX_SIZE ||
        num < 0) {
        return;
    }
    vdev->vq[n].vring.num = num;
    virtqueue_init(&vdev->vq[n]);
}

int virtio_queue_get_num(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.num;
}

int virtio_queue_get_id(VirtQueue *vq)
{
    VirtIODevice *vdev = vq->vdev;
    assert(vq >= &vdev->vq[0] && vq < &vdev->vq[VIRTIO_PCI_QUEUE_MAX]);
    return vq - &vdev->vq[0];
}

void virtio_queue_set_align(VirtIODevice *vdev, int n, int align)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    /* Check that the transport told us it was going to do this
     * (so a buggy transport will immediately assert rather than
     * silently failing to migrate this state)
     */
    assert(k->has_variable_vring_alignment);

    vdev->vq[n].vring.align = align;
    virtqueue_init(&vdev->vq[n]);
}

void virtio_queue_notify_vq(VirtQueue *vq)
{
    if (vq->vring.desc) {
        VirtIODevice *vdev = vq->vdev;
        trace_virtio_queue_notify(vdev, vq - vdev->vq, vq);
        vq->handle_output(vdev, vq);
    }
}

void virtio_queue_notify(VirtIODevice *vdev, int n)
{
    virtio_queue_notify_vq(&vdev->vq[n]);
}

uint16_t virtio_queue_vector(VirtIODevice *vdev, int n)
{
    return n < VIRTIO_PCI_QUEUE_MAX ? vdev->vq[n].vector :
        VIRTIO_NO_VECTOR;
}

void virtio_queue_set_vector(VirtIODevice *vdev, int n, uint16_t vector)
{
    if (n < VIRTIO_PCI_QUEUE_MAX)
        vdev->vq[n].vector = vector;
}

VirtQueue *virtio_add_queue(VirtIODevice *vdev, int queue_size,
                            void (*handle_output)(VirtIODevice *, VirtQueue *))
{
    int i;

    for (i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num == 0)
            break;
    }

    if (i == VIRTIO_PCI_QUEUE_MAX || queue_size > VIRTQUEUE_MAX_SIZE)
        abort();

    vdev->vq[i].vring.num = queue_size;
    vdev->vq[i].vring.align = VIRTIO_PCI_VRING_ALIGN;
    vdev->vq[i].handle_output = handle_output;

    return &vdev->vq[i];
}

void virtio_del_queue(VirtIODevice *vdev, int n)
{
    if (n < 0 || n >= VIRTIO_PCI_QUEUE_MAX) {
        abort();
    }

    vdev->vq[n].vring.num = 0;
}

void virtio_irq(VirtQueue *vq)
{
    trace_virtio_irq(vq);
    vq->vdev->isr |= 0x01;
    virtio_notify_vector(vq->vdev, vq->vector);
}

/* Assuming a given event_idx value from the other size, if
 * we have just incremented index from old to new_idx,
 * should we trigger an event? */
static inline int vring_need_event(uint16_t event, uint16_t new, uint16_t old)
{
	/* Note: Xen has similar logic for notification hold-off
	 * in include/xen/interface/io/ring.h with req_event and req_prod
	 * corresponding to event_idx + 1 and new respectively.
	 * Note also that req_event and req_prod in Xen start at 1,
	 * event indexes in virtio start at 0. */
	return (uint16_t)(new - event - 1) < (uint16_t)(new - old);
}

static bool vring_notify(VirtIODevice *vdev, VirtQueue *vq)
{
    uint16_t old, new;
    bool v;
    /* We need to expose used array entries before checking used event. */
    smp_mb();
    /* Always notify when queue is empty (when feature acknowledge) */
    if (((vdev->guest_features & (1 << VIRTIO_F_NOTIFY_ON_EMPTY)) &&
         !vq->inuse && vring_avail_idx(vq) == vq->last_avail_idx)) {
        return true;
    }

    if (!(vdev->guest_features & (1 << VIRTIO_RING_F_EVENT_IDX))) {
        return !(vring_avail_flags(vq) & VRING_AVAIL_F_NO_INTERRUPT);
    }

    v = vq->signalled_used_valid;
    vq->signalled_used_valid = true;
    old = vq->signalled_used;
    new = vq->signalled_used = vring_used_idx(vq);
    return !v || vring_need_event(vring_used_event(vq), new, old);
}

void virtio_notify(VirtIODevice *vdev, VirtQueue *vq)
{
    if (!vring_notify(vdev, vq)) {
        return;
    }

    trace_virtio_notify(vdev, vq);
    vdev->isr |= 0x01;
    virtio_notify_vector(vdev, vq->vector);
}

void virtio_notify_config(VirtIODevice *vdev)
{
    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK))
        return;

    vdev->isr |= 0x03;
    virtio_notify_vector(vdev, vdev->config_vector);
}

void virtio_save(VirtIODevice *vdev, QEMUFile *f)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int i;

    if (k->save_config) {
        k->save_config(qbus->parent, f);
    }

    qemu_put_8s(f, &vdev->status);
    qemu_put_8s(f, &vdev->isr);
    qemu_put_be16s(f, &vdev->queue_sel);
    qemu_put_be32s(f, &vdev->guest_features);
    qemu_put_be32(f, vdev->config_len);
    qemu_put_buffer(f, vdev->config, vdev->config_len);

    for (i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num == 0)
            break;
    }

    qemu_put_be32(f, i);

    for (i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num == 0)
            break;

        qemu_put_be32(f, vdev->vq[i].vring.num);
        if (k->has_variable_vring_alignment) {
            qemu_put_be32(f, vdev->vq[i].vring.align);
        }
        qemu_put_be64(f, vdev->vq[i].pa);
        qemu_put_be16s(f, &vdev->vq[i].last_avail_idx);
        if (k->save_queue) {
            k->save_queue(qbus->parent, i, f);
        }
    }
}

int virtio_set_features(VirtIODevice *vdev, uint32_t val)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *vbusk = VIRTIO_BUS_GET_CLASS(qbus);
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t supported_features = vbusk->get_features(qbus->parent);
    bool bad = (val & ~supported_features) != 0;

    val &= supported_features;
    if (k->set_features) {
        k->set_features(vdev, val);
    }
    vdev->guest_features = val;
    return bad ? -1 : 0;
}

int virtio_load(VirtIODevice *vdev, QEMUFile *f)
{
    int i, ret;
    int32_t config_len;
    uint32_t num;
    uint32_t features;
    uint32_t supported_features;
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (k->load_config) {
        ret = k->load_config(qbus->parent, f);
        if (ret)
            return ret;
    }

    qemu_get_8s(f, &vdev->status);
    qemu_get_8s(f, &vdev->isr);
    qemu_get_be16s(f, &vdev->queue_sel);
    if (vdev->queue_sel >= VIRTIO_PCI_QUEUE_MAX) {
        return -1;
    }
    qemu_get_be32s(f, &features);

    if (virtio_set_features(vdev, features) < 0) {
        supported_features = k->get_features(qbus->parent);
        error_report("Features 0x%x unsupported. Allowed features: 0x%x",
                     features, supported_features);
        return -1;
    }
    config_len = qemu_get_be32(f);

    /*
     * There are cases where the incoming config can be bigger or smaller
     * than what we have; so load what we have space for, and skip
     * any excess that's in the stream.
     */
    qemu_get_buffer(f, vdev->config, MIN(config_len, vdev->config_len));

    while (config_len > vdev->config_len) {
        qemu_get_byte(f);
        config_len--;
    }

    num = qemu_get_be32(f);

    if (num > VIRTIO_PCI_QUEUE_MAX) {
        error_report("Invalid number of PCI queues: 0x%x", num);
        return -1;
    }

    for (i = 0; i < num; i++) {
        vdev->vq[i].vring.num = qemu_get_be32(f);
        if (k->has_variable_vring_alignment) {
            vdev->vq[i].vring.align = qemu_get_be32(f);
        }
        vdev->vq[i].pa = qemu_get_be64(f);
        qemu_get_be16s(f, &vdev->vq[i].last_avail_idx);
        vdev->vq[i].signalled_used_valid = false;
        vdev->vq[i].notification = true;

        if (vdev->vq[i].pa) {
            uint16_t nheads;
            virtqueue_init(&vdev->vq[i]);
            nheads = vring_avail_idx(&vdev->vq[i]) - vdev->vq[i].last_avail_idx;
            /* Check it isn't doing very strange things with descriptor numbers. */
            if (nheads > vdev->vq[i].vring.num) {
                error_report("VQ %d size 0x%x Guest index 0x%x "
                             "inconsistent with Host index 0x%x: delta 0x%x",
                             i, vdev->vq[i].vring.num,
                             vring_avail_idx(&vdev->vq[i]),
                             vdev->vq[i].last_avail_idx, nheads);
                return -1;
            }
        } else if (vdev->vq[i].last_avail_idx) {
            error_report("VQ %d address 0x0 "
                         "inconsistent with Host index 0x%x",
                         i, vdev->vq[i].last_avail_idx);
                return -1;
	}
        if (k->load_queue) {
            ret = k->load_queue(qbus->parent, i, f);
            if (ret)
                return ret;
        }
    }

    virtio_notify_vector(vdev, VIRTIO_NO_VECTOR);
    return 0;
}

void virtio_cleanup(VirtIODevice *vdev)
{
    qemu_del_vm_change_state_handler(vdev->vmstate);
    g_free(vdev->config);
    g_free(vdev->vq);
}

static void virtio_vmstate_change(void *opaque, int running, RunState state)
{
    VirtIODevice *vdev = opaque;
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    bool backend_run = running && (vdev->status & VIRTIO_CONFIG_S_DRIVER_OK);
    vdev->vm_running = running;

    if (backend_run) {
        virtio_set_status(vdev, vdev->status);
    }

    if (k->vmstate_change) {
        k->vmstate_change(qbus->parent, backend_run);
    }

    if (!backend_run) {
        virtio_set_status(vdev, vdev->status);
    }
}

void virtio_init(VirtIODevice *vdev, const char *name,
                 uint16_t device_id, size_t config_size)
{
    int i;
    vdev->device_id = device_id;
    vdev->status = 0;
    vdev->isr = 0;
    vdev->queue_sel = 0;
    vdev->config_vector = VIRTIO_NO_VECTOR;
    vdev->vq = g_malloc0(sizeof(VirtQueue) * VIRTIO_PCI_QUEUE_MAX);
    vdev->vm_running = runstate_is_running();
    for (i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
        vdev->vq[i].vector = VIRTIO_NO_VECTOR;
        vdev->vq[i].vdev = vdev;
        vdev->vq[i].queue_index = i;
    }

    vdev->name = name;
    vdev->config_len = config_size;
    if (vdev->config_len) {
        vdev->config = g_malloc0(config_size);
    } else {
        vdev->config = NULL;
    }
    vdev->vmstate = qemu_add_vm_change_state_handler(virtio_vmstate_change,
                                                     vdev);
}

hwaddr virtio_queue_get_desc_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.desc;
}

hwaddr virtio_queue_get_avail_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.avail;
}

hwaddr virtio_queue_get_used_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.used;
}

hwaddr virtio_queue_get_ring_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.desc;
}

hwaddr virtio_queue_get_desc_size(VirtIODevice *vdev, int n)
{
    return sizeof(VRingDesc) * vdev->vq[n].vring.num;
}

hwaddr virtio_queue_get_avail_size(VirtIODevice *vdev, int n)
{
    return offsetof(VRingAvail, ring) +
        sizeof(uint64_t) * vdev->vq[n].vring.num;
}

hwaddr virtio_queue_get_used_size(VirtIODevice *vdev, int n)
{
    return offsetof(VRingUsed, ring) +
        sizeof(VRingUsedElem) * vdev->vq[n].vring.num;
}

hwaddr virtio_queue_get_ring_size(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.used - vdev->vq[n].vring.desc +
	    virtio_queue_get_used_size(vdev, n);
}

uint16_t virtio_queue_get_last_avail_idx(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].last_avail_idx;
}

void virtio_queue_set_last_avail_idx(VirtIODevice *vdev, int n, uint16_t idx)
{
    vdev->vq[n].last_avail_idx = idx;
}

void virtio_queue_invalidate_signalled_used(VirtIODevice *vdev, int n)
{
    vdev->vq[n].signalled_used_valid = false;
}

VirtQueue *virtio_get_queue(VirtIODevice *vdev, int n)
{
    return vdev->vq + n;
}

uint16_t virtio_get_queue_index(VirtQueue *vq)
{
    return vq->queue_index;
}

static void virtio_queue_guest_notifier_read(EventNotifier *n)
{
    VirtQueue *vq = container_of(n, VirtQueue, guest_notifier);
    if (event_notifier_test_and_clear(n)) {
        virtio_irq(vq);
    }
}

void virtio_queue_set_guest_notifier_fd_handler(VirtQueue *vq, bool assign,
                                                bool with_irqfd)
{
    if (assign && !with_irqfd) {
        event_notifier_set_handler(&vq->guest_notifier,
                                   virtio_queue_guest_notifier_read);
    } else {
        event_notifier_set_handler(&vq->guest_notifier, NULL);
    }
    if (!assign) {
        /* Test and clear notifier before closing it,
         * in case poll callback didn't have time to run. */
        virtio_queue_guest_notifier_read(&vq->guest_notifier);
    }
}

EventNotifier *virtio_queue_get_guest_notifier(VirtQueue *vq)
{
    return &vq->guest_notifier;
}

static void virtio_queue_host_notifier_read(EventNotifier *n)
{
    VirtQueue *vq = container_of(n, VirtQueue, host_notifier);
    if (event_notifier_test_and_clear(n)) {
        virtio_queue_notify_vq(vq);
    }
}

void virtio_queue_set_host_notifier_fd_handler(VirtQueue *vq, bool assign,
                                               bool set_handler)
{
    if (assign && set_handler) {
        event_notifier_set_handler(&vq->host_notifier,
                                   virtio_queue_host_notifier_read);
    } else {
        event_notifier_set_handler(&vq->host_notifier, NULL);
    }
    if (!assign) {
        /* Test and clear notifier before after disabling event,
         * in case poll callback didn't have time to run. */
        virtio_queue_host_notifier_read(&vq->host_notifier);
    }
}

EventNotifier *virtio_queue_get_host_notifier(VirtQueue *vq)
{
    return &vq->host_notifier;
}

void virtio_device_set_child_bus_name(VirtIODevice *vdev, char *bus_name)
{
    if (vdev->bus_name) {
        g_free(vdev->bus_name);
        vdev->bus_name = NULL;
    }

    if (bus_name) {
        vdev->bus_name = g_strdup(bus_name);
    }
}

static void virtio_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(dev);
    Error *err = NULL;

    if (vdc->realize != NULL) {
        vdc->realize(dev, &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
    }
    virtio_bus_device_plugged(vdev);
}

static void virtio_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(dev);
    Error *err = NULL;

    virtio_bus_device_unplugged(vdev);

    if (vdc->unrealize != NULL) {
        vdc->unrealize(dev, &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
    }

    if (vdev->bus_name) {
        g_free(vdev->bus_name);
        vdev->bus_name = NULL;
    }
}

static void virtio_device_class_init(ObjectClass *klass, void *data)
{
    /* Set the default value here. */
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = virtio_device_realize;
    dc->unrealize = virtio_device_unrealize;
    dc->bus_type = TYPE_VIRTIO_BUS;
}

static const TypeInfo virtio_device_info = {
    .name = TYPE_VIRTIO_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(VirtIODevice),
    .class_init = virtio_device_class_init,
    .abstract = true,
    .class_size = sizeof(VirtioDeviceClass),
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_device_info);
}

type_init(virtio_register_types)
