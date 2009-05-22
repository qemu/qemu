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

#include "virtio.h"
#include "sysemu.h"

/* The alignment to use between consumer and producer parts of vring.
 * x86 pagesize again. */
#define VIRTIO_PCI_VRING_ALIGN         4096

/* QEMU doesn't strictly need write barriers since everything runs in
 * lock-step.  We'll leave the calls to wmb() in though to make it obvious for
 * KVM or if kqemu gets SMP support.
 */
#define wmb() do { } while (0)

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
    int inuse;
    void (*handle_output)(VirtIODevice *vdev, VirtQueue *vq);
};

#define VIRTIO_PCI_QUEUE_MAX        16

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

static inline uint64_t vring_desc_addr(VirtQueue *vq, int i)
{
    target_phys_addr_t pa;
    pa = vq->vring.desc + sizeof(VRingDesc) * i + offsetof(VRingDesc, addr);
    return ldq_phys(pa);
}

static inline uint32_t vring_desc_len(VirtQueue *vq, int i)
{
    target_phys_addr_t pa;
    pa = vq->vring.desc + sizeof(VRingDesc) * i + offsetof(VRingDesc, len);
    return ldl_phys(pa);
}

static inline uint16_t vring_desc_flags(VirtQueue *vq, int i)
{
    target_phys_addr_t pa;
    pa = vq->vring.desc + sizeof(VRingDesc) * i + offsetof(VRingDesc, flags);
    return lduw_phys(pa);
}

static inline uint16_t vring_desc_next(VirtQueue *vq, int i)
{
    target_phys_addr_t pa;
    pa = vq->vring.desc + sizeof(VRingDesc) * i + offsetof(VRingDesc, next);
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

static inline void vring_used_idx_increment(VirtQueue *vq, uint16_t val)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, idx);
    stw_phys(pa, vring_used_idx(vq) + val);
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

void virtio_queue_set_notification(VirtQueue *vq, int enable)
{
    if (enable)
        vring_used_flags_unset_bit(vq, VRING_USED_F_NO_NOTIFY);
    else
        vring_used_flags_set_bit(vq, VRING_USED_F_NO_NOTIFY);
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
    /* Make sure buffer is written before we update index. */
    wmb();
    vring_used_idx_increment(vq, count);
    vq->inuse -= count;
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
        fprintf(stderr, "Guest moved used index from %u to %u",
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
        fprintf(stderr, "Guest says index %u is available", head);
        exit(1);
    }

    return head;
}

static unsigned virtqueue_next_desc(VirtQueue *vq, unsigned int i)
{
    unsigned int next;

    /* If this descriptor says it doesn't chain, we're done. */
    if (!(vring_desc_flags(vq, i) & VRING_DESC_F_NEXT))
        return vq->vring.num;

    /* Check they're not leading us off end of descriptors. */
    next = vring_desc_next(vq, i);
    /* Make sure compiler knows to grab that: we don't want it changing! */
    wmb();

    if (next >= vq->vring.num) {
        fprintf(stderr, "Desc next is %u", next);
        exit(1);
    }

    return next;
}

int virtqueue_avail_bytes(VirtQueue *vq, int in_bytes, int out_bytes)
{
    unsigned int idx;
    int num_bufs, in_total, out_total;

    idx = vq->last_avail_idx;

    num_bufs = in_total = out_total = 0;
    while (virtqueue_num_heads(vq, idx)) {
        int i;

        i = virtqueue_get_head(vq, idx++);
        do {
            /* If we've got too many, that implies a descriptor loop. */
            if (++num_bufs > vq->vring.num) {
                fprintf(stderr, "Looped descriptor");
                exit(1);
            }

            if (vring_desc_flags(vq, i) & VRING_DESC_F_WRITE) {
                if (in_bytes > 0 &&
                    (in_total += vring_desc_len(vq, i)) >= in_bytes)
                    return 1;
            } else {
                if (out_bytes > 0 &&
                    (out_total += vring_desc_len(vq, i)) >= out_bytes)
                    return 1;
            }
        } while ((i = virtqueue_next_desc(vq, i)) != vq->vring.num);
    }

    return 0;
}

int virtqueue_pop(VirtQueue *vq, VirtQueueElement *elem)
{
    unsigned int i, head;
    target_phys_addr_t len;

    if (!virtqueue_num_heads(vq, vq->last_avail_idx))
        return 0;

    /* When we start there are none of either input nor output. */
    elem->out_num = elem->in_num = 0;

    i = head = virtqueue_get_head(vq, vq->last_avail_idx++);
    do {
        struct iovec *sg;
        int is_write = 0;

        if (vring_desc_flags(vq, i) & VRING_DESC_F_WRITE) {
            elem->in_addr[elem->in_num] = vring_desc_addr(vq, i);
            sg = &elem->in_sg[elem->in_num++];
            is_write = 1;
        } else
            sg = &elem->out_sg[elem->out_num++];

        /* Grab the first descriptor, and check it's OK. */
        sg->iov_len = vring_desc_len(vq, i);
        len = sg->iov_len;

        sg->iov_base = cpu_physical_memory_map(vring_desc_addr(vq, i), &len, is_write);

        if (sg->iov_base == NULL || len != sg->iov_len) {
            fprintf(stderr, "virtio: trying to map MMIO memory\n");
            exit(1);
        }

        /* If we've got too many, that implies a descriptor loop. */
        if ((elem->in_num + elem->out_num) > vq->vring.num) {
            fprintf(stderr, "Looped descriptor");
            exit(1);
        }
    } while ((i = virtqueue_next_desc(vq, i)) != vq->vring.num);

    elem->index = head;

    vq->inuse++;

    return elem->in_num + elem->out_num;
}

/* virtio device */

void virtio_update_irq(VirtIODevice *vdev)
{
    if (vdev->binding->update_irq) {
        vdev->binding->update_irq(vdev->binding_opaque);
    }
}

void virtio_reset(void *opaque)
{
    VirtIODevice *vdev = opaque;
    int i;

    if (vdev->reset)
        vdev->reset(vdev);

    vdev->features = 0;
    vdev->queue_sel = 0;
    vdev->status = 0;
    vdev->isr = 0;
    virtio_update_irq(vdev);

    for(i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
        vdev->vq[i].vring.desc = 0;
        vdev->vq[i].vring.avail = 0;
        vdev->vq[i].vring.used = 0;
        vdev->vq[i].last_avail_idx = 0;
        vdev->vq[i].pa = 0;
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
    if (addr == 0) {
        virtio_reset(vdev);
    } else {
        vdev->vq[n].pa = addr;
        virtqueue_init(&vdev->vq[n]);
    }
}

target_phys_addr_t virtio_queue_get_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].pa;
}

int virtio_queue_get_num(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.num;
}

void virtio_queue_notify(VirtIODevice *vdev, int n)
{
    if (n < VIRTIO_PCI_QUEUE_MAX && vdev->vq[n].vring.desc) {
        vdev->vq[n].handle_output(vdev, &vdev->vq[n]);
    }
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

void virtio_notify(VirtIODevice *vdev, VirtQueue *vq)
{
    /* Always notify when queue is empty (when feature acknowledge) */
    if ((vring_avail_flags(vq) & VRING_AVAIL_F_NO_INTERRUPT) &&
        (!(vdev->features & (1 << VIRTIO_F_NOTIFY_ON_EMPTY)) ||
         (vq->inuse || vring_avail_idx(vq) != vq->last_avail_idx)))
        return;

    vdev->isr |= 0x01;
    virtio_update_irq(vdev);
}

void virtio_notify_config(VirtIODevice *vdev)
{
    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK))
        return;

    vdev->isr |= 0x03;
    virtio_update_irq(vdev);
}

void virtio_save(VirtIODevice *vdev, QEMUFile *f)
{
    int i;

    /* FIXME: load/save binding.  */
    //pci_device_save(&vdev->pci_dev, f);

    qemu_put_8s(f, &vdev->status);
    qemu_put_8s(f, &vdev->isr);
    qemu_put_be16s(f, &vdev->queue_sel);
    qemu_put_be32s(f, &vdev->features);
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
    }
}

void virtio_load(VirtIODevice *vdev, QEMUFile *f)
{
    int num, i;

    /* FIXME: load/save binding.  */
    //pci_device_load(&vdev->pci_dev, f);

    qemu_get_8s(f, &vdev->status);
    qemu_get_8s(f, &vdev->isr);
    qemu_get_be16s(f, &vdev->queue_sel);
    qemu_get_be32s(f, &vdev->features);
    vdev->config_len = qemu_get_be32(f);
    qemu_get_buffer(f, vdev->config, vdev->config_len);

    num = qemu_get_be32(f);

    for (i = 0; i < num; i++) {
        vdev->vq[i].vring.num = qemu_get_be32(f);
        vdev->vq[i].pa = qemu_get_be64(f);
        qemu_get_be16s(f, &vdev->vq[i].last_avail_idx);

        if (vdev->vq[i].pa) {
            virtqueue_init(&vdev->vq[i]);
        }
    }

    virtio_update_irq(vdev);
}

void virtio_cleanup(VirtIODevice *vdev)
{
    if (vdev->config)
        qemu_free(vdev->config);
    qemu_free(vdev->vq);
}

VirtIODevice *virtio_common_init(const char *name, uint16_t device_id,
                                 size_t config_size, size_t struct_size)
{
    VirtIODevice *vdev;

    vdev = qemu_mallocz(struct_size);

    vdev->device_id = device_id;
    vdev->status = 0;
    vdev->isr = 0;
    vdev->queue_sel = 0;
    vdev->vq = qemu_mallocz(sizeof(VirtQueue) * VIRTIO_PCI_QUEUE_MAX);

    vdev->name = name;
    vdev->config_len = config_size;
    if (vdev->config_len)
        vdev->config = qemu_mallocz(config_size);
    else
        vdev->config = NULL;

    qemu_register_reset(virtio_reset, 0, vdev);

    return vdev;
}

void virtio_bind_device(VirtIODevice *vdev, const VirtIOBindings *binding,
                        void *opaque)
{
    vdev->binding = binding;
    vdev->binding_opaque = opaque;
}
