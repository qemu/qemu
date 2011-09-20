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
#include "qemu-error.h"
#include "virtio.h"
#include "qemu-barrier.h"

/* The alignment to use between consumer and producer parts of vring.
 * x86 pagesize again. */
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
    target_phys_addr_t desc;
    target_phys_addr_t avail;
    target_phys_addr_t used;
} VRing;

struct VirtQueue
{
    VRing vring;
    target_phys_addr_t pa;
    uint16_t last_avail_idx;
    /* Last used index value we have signalled on */
    uint16_t signalled_used;

    /* Last used index value we have signalled on */
    bool signalled_used_valid;

    /* Notification enabled? */
    bool notification;

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
    target_phys_addr_t pa = vq->pa;

    vq->vring.desc = pa;
    vq->vring.avail = pa + vq->vring.num * sizeof(VRingDesc);
    vq->vring.used = vring_align(vq->vring.avail +
                                 offsetof(VRingAvail, ring[vq->vring.num]),
                                 VIRTIO_PCI_VRING_ALIGN);
}

static inline uint64_t vring_desc_addr(target_phys_addr_t desc_pa, int i)
{
    target_phys_addr_t pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, addr);
    return ldq_phys(pa);
}

static inline uint32_t vring_desc_len(target_phys_addr_t desc_pa, int i)
{
    target_phys_addr_t pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, len);
    return ldl_phys(pa);
}

static inline uint16_t vring_desc_flags(target_phys_addr_t desc_pa, int i)
{
    target_phys_addr_t pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, flags);
    return lduw_phys(pa);
}

static inline uint16_t vring_desc_next(target_phys_addr_t desc_pa, int i)
{
    target_phys_addr_t pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, next);
    return lduw_phys(pa);
}

static inline uint16_t vring_avail_flags(VirtQueue *vq)
{
    target_phys_addr_t pa;
    pa = vq->vring.avail + offsetof(VRingAvail, flags);
    return lduw_phys(pa);
}

static inline uint16_t vring_avail_idx(VirtQueue *vq)
{
    target_phys_addr_t pa;
    pa = vq->vring.avail + offsetof(VRingAvail, idx);
    return lduw_phys(pa);
}

static inline uint16_t vring_avail_ring(VirtQueue *vq, int i)
{
    target_phys_addr_t pa;
    pa = vq->vring.avail + offsetof(VRingAvail, ring[i]);
    return lduw_phys(pa);
}

static inline uint16_t vring_used_event(VirtQueue *vq)
{
    return vring_avail_ring(vq, vq->vring.num);
}

static inline void vring_used_ring_id(VirtQueue *vq, int i, uint32_t val)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, ring[i].id);
    stl_phys(pa, val);
}

static inline void vring_used_ring_len(VirtQueue *vq, int i, uint32_t val)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, ring[i].len);
    stl_phys(pa, val);
}

static uint16_t vring_used_idx(VirtQueue *vq)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, idx);
    return lduw_phys(pa);
}

static inline void vring_used_idx_set(VirtQueue *vq, uint16_t val)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, idx);
    stw_phys(pa, val);
}

static inline void vring_used_flags_set_bit(VirtQueue *vq, int mask)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, flags);
    stw_phys(pa, lduw_phys(pa) | mask);
}

static inline void vring_used_flags_unset_bit(VirtQueue *vq, int mask)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, flags);
    stw_phys(pa, lduw_phys(pa) & ~mask);
}

static inline void vring_avail_event(VirtQueue *vq, uint16_t val)
{
    target_phys_addr_t pa;
    if (!vq->notification) {
        return;
    }
    pa = vq->vring.used + offsetof(VRingUsed, ring[vq->vring.num]);
    stw_phys(pa, val);
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

        offset += elem->in_sg[i].iov_len;
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

static unsigned virtqueue_next_desc(target_phys_addr_t desc_pa,
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

int virtqueue_avail_bytes(VirtQueue *vq, int in_bytes, int out_bytes)
{
    unsigned int idx;
    int total_bufs, in_total, out_total;

    idx = vq->last_avail_idx;

    total_bufs = in_total = out_total = 0;
    while (virtqueue_num_heads(vq, idx)) {
        unsigned int max, num_bufs, indirect = 0;
        target_phys_addr_t desc_pa;
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
            num_bufs = i = 0;
            desc_pa = vring_desc_addr(desc_pa, i);
        }

        do {
            /* If we've got too many, that implies a descriptor loop. */
            if (++num_bufs > max) {
                error_report("Looped descriptor");
                exit(1);
            }

            if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_WRITE) {
                if (in_bytes > 0 &&
                    (in_total += vring_desc_len(desc_pa, i)) >= in_bytes)
                    return 1;
            } else {
                if (out_bytes > 0 &&
                    (out_total += vring_desc_len(desc_pa, i)) >= out_bytes)
                    return 1;
            }
        } while ((i = virtqueue_next_desc(desc_pa, i, max)) != max);

        if (!indirect)
            total_bufs = num_bufs;
        else
            total_bufs++;
    }

    return 0;
}

void virtqueue_map_sg(struct iovec *sg, target_phys_addr_t *addr,
    size_t num_sg, int is_write)
{
    unsigned int i;
    target_phys_addr_t len;

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
    target_phys_addr_t desc_pa = vq->vring.desc;

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
    if (vdev->binding->notify) {
        vdev->binding->notify(vdev->binding_opaque, vector);
    }
}

void virtio_update_irq(VirtIODevice *vdev)
{
    virtio_notify_vector(vdev, VIRTIO_NO_VECTOR);
}

void virtio_set_status(VirtIODevice *vdev, uint8_t val)
{
    trace_virtio_set_status(vdev, val);

    if (vdev->set_status) {
        vdev->set_status(vdev, val);
    }
    vdev->status = val;
}

void virtio_reset(void *opaque)
{
    VirtIODevice *vdev = opaque;
    int i;

    virtio_set_status(vdev, 0);

    if (vdev->reset)
        vdev->reset(vdev);

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
    uint8_t val;

    vdev->get_config(vdev, vdev->config);

    if (addr > (vdev->config_len - sizeof(val)))
        return (uint32_t)-1;

    memcpy(&val, vdev->config + addr, sizeof(val));
    return val;
}

uint32_t virtio_config_readw(VirtIODevice *vdev, uint32_t addr)
{
    uint16_t val;

    vdev->get_config(vdev, vdev->config);

    if (addr > (vdev->config_len - sizeof(val)))
        return (uint32_t)-1;

    memcpy(&val, vdev->config + addr, sizeof(val));
    return val;
}

uint32_t virtio_config_readl(VirtIODevice *vdev, uint32_t addr)
{
    uint32_t val;

    vdev->get_config(vdev, vdev->config);

    if (addr > (vdev->config_len - sizeof(val)))
        return (uint32_t)-1;

    memcpy(&val, vdev->config + addr, sizeof(val));
    return val;
}

void virtio_config_writeb(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    uint8_t val = data;

    if (addr > (vdev->config_len - sizeof(val)))
        return;

    memcpy(vdev->config + addr, &val, sizeof(val));

    if (vdev->set_config)
        vdev->set_config(vdev, vdev->config);
}

void virtio_config_writew(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    uint16_t val = data;

    if (addr > (vdev->config_len - sizeof(val)))
        return;

    memcpy(vdev->config + addr, &val, sizeof(val));

    if (vdev->set_config)
        vdev->set_config(vdev, vdev->config);
}

void virtio_config_writel(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    uint32_t val = data;

    if (addr > (vdev->config_len - sizeof(val)))
        return;

    memcpy(vdev->config + addr, &val, sizeof(val));

    if (vdev->set_config)
        vdev->set_config(vdev, vdev->config);
}

void virtio_queue_set_addr(VirtIODevice *vdev, int n, target_phys_addr_t addr)
{
    vdev->vq[n].pa = addr;
    virtqueue_init(&vdev->vq[n]);
}

target_phys_addr_t virtio_queue_get_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].pa;
}

int virtio_queue_get_num(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.num;
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
    vdev->vq[i].handle_output = handle_output;

    return &vdev->vq[i];
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
    int i;

    if (vdev->binding->save_config)
        vdev->binding->save_config(vdev->binding_opaque, f);

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
        qemu_put_be64(f, vdev->vq[i].pa);
        qemu_put_be16s(f, &vdev->vq[i].last_avail_idx);
        if (vdev->binding->save_queue)
            vdev->binding->save_queue(vdev->binding_opaque, i, f);
    }
}

int virtio_load(VirtIODevice *vdev, QEMUFile *f)
{
    int num, i, ret;
    uint32_t features;
    uint32_t supported_features =
        vdev->binding->get_features(vdev->binding_opaque);

    if (vdev->binding->load_config) {
        ret = vdev->binding->load_config(vdev->binding_opaque, f);
        if (ret)
            return ret;
    }

    qemu_get_8s(f, &vdev->status);
    qemu_get_8s(f, &vdev->isr);
    qemu_get_be16s(f, &vdev->queue_sel);
    qemu_get_be32s(f, &features);
    if (features & ~supported_features) {
        error_report("Features 0x%x unsupported. Allowed features: 0x%x",
                     features, supported_features);
        return -1;
    }
    if (vdev->set_features)
        vdev->set_features(vdev, features);
    vdev->guest_features = features;
    vdev->config_len = qemu_get_be32(f);
    qemu_get_buffer(f, vdev->config, vdev->config_len);

    num = qemu_get_be32(f);

    for (i = 0; i < num; i++) {
        vdev->vq[i].vring.num = qemu_get_be32(f);
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
        if (vdev->binding->load_queue) {
            ret = vdev->binding->load_queue(vdev->binding_opaque, i, f);
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
    if (vdev->config)
        g_free(vdev->config);
    g_free(vdev->vq);
    g_free(vdev);
}

static void virtio_vmstate_change(void *opaque, int running, RunState state)
{
    VirtIODevice *vdev = opaque;
    bool backend_run = running && (vdev->status & VIRTIO_CONFIG_S_DRIVER_OK);
    vdev->vm_running = running;

    if (backend_run) {
        virtio_set_status(vdev, vdev->status);
    }

    if (vdev->binding->vmstate_change) {
        vdev->binding->vmstate_change(vdev->binding_opaque, backend_run);
    }

    if (!backend_run) {
        virtio_set_status(vdev, vdev->status);
    }
}

VirtIODevice *virtio_common_init(const char *name, uint16_t device_id,
                                 size_t config_size, size_t struct_size)
{
    VirtIODevice *vdev;
    int i;

    vdev = g_malloc0(struct_size);

    vdev->device_id = device_id;
    vdev->status = 0;
    vdev->isr = 0;
    vdev->queue_sel = 0;
    vdev->config_vector = VIRTIO_NO_VECTOR;
    vdev->vq = g_malloc0(sizeof(VirtQueue) * VIRTIO_PCI_QUEUE_MAX);
    vdev->vm_running = runstate_is_running();
    for(i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
        vdev->vq[i].vector = VIRTIO_NO_VECTOR;
        vdev->vq[i].vdev = vdev;
    }

    vdev->name = name;
    vdev->config_len = config_size;
    if (vdev->config_len)
        vdev->config = g_malloc0(config_size);
    else
        vdev->config = NULL;

    vdev->vmstate = qemu_add_vm_change_state_handler(virtio_vmstate_change, vdev);

    return vdev;
}

void virtio_bind_device(VirtIODevice *vdev, const VirtIOBindings *binding,
                        void *opaque)
{
    vdev->binding = binding;
    vdev->binding_opaque = opaque;
}

target_phys_addr_t virtio_queue_get_desc_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.desc;
}

target_phys_addr_t virtio_queue_get_avail_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.avail;
}

target_phys_addr_t virtio_queue_get_used_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.used;
}

target_phys_addr_t virtio_queue_get_ring_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.desc;
}

target_phys_addr_t virtio_queue_get_desc_size(VirtIODevice *vdev, int n)
{
    return sizeof(VRingDesc) * vdev->vq[n].vring.num;
}

target_phys_addr_t virtio_queue_get_avail_size(VirtIODevice *vdev, int n)
{
    return offsetof(VRingAvail, ring) +
        sizeof(uint64_t) * vdev->vq[n].vring.num;
}

target_phys_addr_t virtio_queue_get_used_size(VirtIODevice *vdev, int n)
{
    return offsetof(VRingUsed, ring) +
        sizeof(VRingUsedElem) * vdev->vq[n].vring.num;
}

target_phys_addr_t virtio_queue_get_ring_size(VirtIODevice *vdev, int n)
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

VirtQueue *virtio_get_queue(VirtIODevice *vdev, int n)
{
    return vdev->vq + n;
}

EventNotifier *virtio_queue_get_guest_notifier(VirtQueue *vq)
{
    return &vq->guest_notifier;
}
EventNotifier *virtio_queue_get_host_notifier(VirtQueue *vq)
{
    return &vq->host_notifier;
}
