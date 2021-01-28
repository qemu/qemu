/*
 * QEMU Hyper-V VMBus
 *
 * Copyright (c) 2017-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/hyperv/hyperv.h"
#include "hw/hyperv/vmbus.h"
#include "hw/hyperv/vmbus-bridge.h"
#include "hw/sysbus.h"
#include "cpu.h"
#include "trace.h"

enum {
    VMGPADL_INIT,
    VMGPADL_ALIVE,
    VMGPADL_TEARINGDOWN,
    VMGPADL_TORNDOWN,
};

struct VMBusGpadl {
    /* GPADL id */
    uint32_t id;
    /* associated channel id (rudimentary?) */
    uint32_t child_relid;

    /* number of pages in the GPADL as declared in GPADL_HEADER message */
    uint32_t num_gfns;
    /*
     * Due to limited message size, GPADL may not fit fully in a single
     * GPADL_HEADER message, and is further popluated using GPADL_BODY
     * messages.  @seen_gfns is the number of pages seen so far; once it
     * reaches @num_gfns, the GPADL is ready to use.
     */
    uint32_t seen_gfns;
    /* array of GFNs (of size @num_gfns once allocated) */
    uint64_t *gfns;

    uint8_t state;

    QTAILQ_ENTRY(VMBusGpadl) link;
    VMBus *vmbus;
    unsigned refcount;
};

/*
 * Wrap sequential read from / write to GPADL.
 */
typedef struct GpadlIter {
    VMBusGpadl *gpadl;
    AddressSpace *as;
    DMADirection dir;
    /* offset into GPADL where the next i/o will be performed */
    uint32_t off;
    /*
     * Cached mapping of the currently accessed page, up to page boundary.
     * Updated lazily on i/o.
     * Note: MemoryRegionCache can not be used here because pages in the GPADL
     * are non-contiguous and may belong to different memory regions.
     */
    void *map;
    /* offset after last i/o (i.e. not affected by seek) */
    uint32_t last_off;
    /*
     * Indicator that the iterator is active and may have a cached mapping.
     * Allows to enforce bracketing of all i/o (which may create cached
     * mappings) and thus exclude mapping leaks.
     */
    bool active;
} GpadlIter;

/*
 * Ring buffer.  There are two of them, sitting in the same GPADL, for each
 * channel.
 * Each ring buffer consists of a set of pages, with the first page containing
 * the ring buffer header, and the remaining pages being for data packets.
 */
typedef struct VMBusRingBufCommon {
    AddressSpace *as;
    /* GPA of the ring buffer header */
    dma_addr_t rb_addr;
    /* start and length of the ring buffer data area within GPADL */
    uint32_t base;
    uint32_t len;

    GpadlIter iter;
} VMBusRingBufCommon;

typedef struct VMBusSendRingBuf {
    VMBusRingBufCommon common;
    /* current write index, to be committed at the end of send */
    uint32_t wr_idx;
    /* write index at the start of send */
    uint32_t last_wr_idx;
    /* space to be requested from the guest */
    uint32_t wanted;
    /* space reserved for planned sends */
    uint32_t reserved;
    /* last seen read index */
    uint32_t last_seen_rd_idx;
} VMBusSendRingBuf;

typedef struct VMBusRecvRingBuf {
    VMBusRingBufCommon common;
    /* current read index, to be committed at the end of receive */
    uint32_t rd_idx;
    /* read index at the start of receive */
    uint32_t last_rd_idx;
    /* last seen write index */
    uint32_t last_seen_wr_idx;
} VMBusRecvRingBuf;


enum {
    VMOFFER_INIT,
    VMOFFER_SENDING,
    VMOFFER_SENT,
};

enum {
    VMCHAN_INIT,
    VMCHAN_OPENING,
    VMCHAN_OPEN,
};

struct VMBusChannel {
    VMBusDevice *dev;

    /* channel id */
    uint32_t id;
    /*
     * subchannel index within the device; subchannel #0 is "primary" and
     * always exists
     */
    uint16_t subchan_idx;
    uint32_t open_id;
    /* VP_INDEX of the vCPU to notify with (synthetic) interrupts */
    uint32_t target_vp;
    /* GPADL id to use for the ring buffers */
    uint32_t ringbuf_gpadl;
    /* start (in pages) of the send ring buffer within @ringbuf_gpadl */
    uint32_t ringbuf_send_offset;

    uint8_t offer_state;
    uint8_t state;
    bool is_open;

    /* main device worker; copied from the device class */
    VMBusChannelNotifyCb notify_cb;
    /*
     * guest->host notifications, either sent directly or dispatched via
     * interrupt page (older VMBus)
     */
    EventNotifier notifier;

    VMBus *vmbus;
    /*
     * SINT route to signal with host->guest notifications; may be shared with
     * the main VMBus SINT route
     */
    HvSintRoute *notify_route;
    VMBusGpadl *gpadl;

    VMBusSendRingBuf send_ringbuf;
    VMBusRecvRingBuf recv_ringbuf;

    QTAILQ_ENTRY(VMBusChannel) link;
};

/*
 * Hyper-V spec mandates that every message port has 16 buffers, which means
 * that the guest can post up to this many messages without blocking.
 * Therefore a queue for incoming messages has to be provided.
 * For outgoing (i.e. host->guest) messages there's no queue; the VMBus just
 * doesn't transition to a new state until the message is known to have been
 * successfully delivered to the respective SynIC message slot.
 */
#define HV_MSG_QUEUE_LEN     16

/* Hyper-V devices never use channel #0.  Must be something special. */
#define VMBUS_FIRST_CHANID      1
/* Each channel occupies one bit within a single event page sint slot. */
#define VMBUS_CHANID_COUNT      (HV_EVENT_FLAGS_COUNT - VMBUS_FIRST_CHANID)
/* Leave a few connection numbers for other purposes. */
#define VMBUS_CHAN_CONNECTION_OFFSET     16

/*
 * Since the success or failure of sending a message is reported
 * asynchronously, the VMBus state machine has effectively two entry points:
 * vmbus_run and vmbus_msg_cb (the latter is called when the host->guest
 * message delivery status becomes known).  Both are run as oneshot BHs on the
 * main aio context, ensuring serialization.
 */
enum {
    VMBUS_LISTEN,
    VMBUS_HANDSHAKE,
    VMBUS_OFFER,
    VMBUS_CREATE_GPADL,
    VMBUS_TEARDOWN_GPADL,
    VMBUS_OPEN_CHANNEL,
    VMBUS_UNLOAD,
    VMBUS_STATE_MAX
};

struct VMBus {
    BusState parent;

    uint8_t state;
    /* protection against recursive aio_poll (see vmbus_run) */
    bool in_progress;
    /* whether there's a message being delivered to the guest */
    bool msg_in_progress;
    uint32_t version;
    /* VP_INDEX of the vCPU to send messages and interrupts to */
    uint32_t target_vp;
    HvSintRoute *sint_route;
    /*
     * interrupt page for older protocol versions; newer ones use SynIC event
     * flags directly
     */
    hwaddr int_page_gpa;

    DECLARE_BITMAP(chanid_bitmap, VMBUS_CHANID_COUNT);

    /* incoming message queue */
    struct hyperv_post_message_input rx_queue[HV_MSG_QUEUE_LEN];
    uint8_t rx_queue_head;
    uint8_t rx_queue_size;
    QemuMutex rx_queue_lock;

    QTAILQ_HEAD(, VMBusGpadl) gpadl_list;
    QTAILQ_HEAD(, VMBusChannel) channel_list;

    /*
     * guest->host notifications for older VMBus, to be dispatched via
     * interrupt page
     */
    EventNotifier notifier;
};

static bool gpadl_full(VMBusGpadl *gpadl)
{
    return gpadl->seen_gfns == gpadl->num_gfns;
}

static VMBusGpadl *create_gpadl(VMBus *vmbus, uint32_t id,
                                uint32_t child_relid, uint32_t num_gfns)
{
    VMBusGpadl *gpadl = g_new0(VMBusGpadl, 1);

    gpadl->id = id;
    gpadl->child_relid = child_relid;
    gpadl->num_gfns = num_gfns;
    gpadl->gfns = g_new(uint64_t, num_gfns);
    QTAILQ_INSERT_HEAD(&vmbus->gpadl_list, gpadl, link);
    gpadl->vmbus = vmbus;
    gpadl->refcount = 1;
    return gpadl;
}

static void free_gpadl(VMBusGpadl *gpadl)
{
    QTAILQ_REMOVE(&gpadl->vmbus->gpadl_list, gpadl, link);
    g_free(gpadl->gfns);
    g_free(gpadl);
}

static VMBusGpadl *find_gpadl(VMBus *vmbus, uint32_t gpadl_id)
{
    VMBusGpadl *gpadl;
    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        if (gpadl->id == gpadl_id) {
            return gpadl;
        }
    }
    return NULL;
}

VMBusGpadl *vmbus_get_gpadl(VMBusChannel *chan, uint32_t gpadl_id)
{
    VMBusGpadl *gpadl = find_gpadl(chan->vmbus, gpadl_id);
    if (!gpadl || !gpadl_full(gpadl)) {
        return NULL;
    }
    gpadl->refcount++;
    return gpadl;
}

void vmbus_put_gpadl(VMBusGpadl *gpadl)
{
    if (!gpadl) {
        return;
    }
    if (--gpadl->refcount) {
        return;
    }
    free_gpadl(gpadl);
}

uint32_t vmbus_gpadl_len(VMBusGpadl *gpadl)
{
    return gpadl->num_gfns * TARGET_PAGE_SIZE;
}

static void gpadl_iter_init(GpadlIter *iter, VMBusGpadl *gpadl,
                            AddressSpace *as, DMADirection dir)
{
    iter->gpadl = gpadl;
    iter->as = as;
    iter->dir = dir;
    iter->active = false;
}

static inline void gpadl_iter_cache_unmap(GpadlIter *iter)
{
    uint32_t map_start_in_page = (uintptr_t)iter->map & ~TARGET_PAGE_MASK;
    uint32_t io_end_in_page = ((iter->last_off - 1) & ~TARGET_PAGE_MASK) + 1;

    /* mapping is only done to do non-zero amount of i/o */
    assert(iter->last_off > 0);
    assert(map_start_in_page < io_end_in_page);

    dma_memory_unmap(iter->as, iter->map, TARGET_PAGE_SIZE - map_start_in_page,
                     iter->dir, io_end_in_page - map_start_in_page);
}

/*
 * Copy exactly @len bytes between the GPADL pointed to by @iter and @buf.
 * The direction of the copy is determined by @iter->dir.
 * The caller must ensure the operation overflows neither @buf nor the GPADL
 * (there's an assert for the latter).
 * Reuse the currently mapped page in the GPADL if possible.
 */
static ssize_t gpadl_iter_io(GpadlIter *iter, void *buf, uint32_t len)
{
    ssize_t ret = len;

    assert(iter->active);

    while (len) {
        uint32_t off_in_page = iter->off & ~TARGET_PAGE_MASK;
        uint32_t pgleft = TARGET_PAGE_SIZE - off_in_page;
        uint32_t cplen = MIN(pgleft, len);
        void *p;

        /* try to reuse the cached mapping */
        if (iter->map) {
            uint32_t map_start_in_page =
                (uintptr_t)iter->map & ~TARGET_PAGE_MASK;
            uint32_t off_base = iter->off & ~TARGET_PAGE_MASK;
            uint32_t mapped_base = (iter->last_off - 1) & ~TARGET_PAGE_MASK;
            if (off_base != mapped_base || off_in_page < map_start_in_page) {
                gpadl_iter_cache_unmap(iter);
                iter->map = NULL;
            }
        }

        if (!iter->map) {
            dma_addr_t maddr;
            dma_addr_t mlen = pgleft;
            uint32_t idx = iter->off >> TARGET_PAGE_BITS;
            assert(idx < iter->gpadl->num_gfns);

            maddr = (iter->gpadl->gfns[idx] << TARGET_PAGE_BITS) | off_in_page;

            iter->map = dma_memory_map(iter->as, maddr, &mlen, iter->dir);
            if (mlen != pgleft) {
                dma_memory_unmap(iter->as, iter->map, mlen, iter->dir, 0);
                iter->map = NULL;
                return -EFAULT;
            }
        }

        p = (void *)(uintptr_t)(((uintptr_t)iter->map & TARGET_PAGE_MASK) |
                off_in_page);
        if (iter->dir == DMA_DIRECTION_FROM_DEVICE) {
            memcpy(p, buf, cplen);
        } else {
            memcpy(buf, p, cplen);
        }

        buf += cplen;
        len -= cplen;
        iter->off += cplen;
        iter->last_off = iter->off;
    }

    return ret;
}

/*
 * Position the iterator @iter at new offset @new_off.
 * If this results in the cached mapping being unusable with the new offset,
 * unmap it.
 */
static inline void gpadl_iter_seek(GpadlIter *iter, uint32_t new_off)
{
    assert(iter->active);
    iter->off = new_off;
}

/*
 * Start a series of i/o on the GPADL.
 * After this i/o and seek operations on @iter become legal.
 */
static inline void gpadl_iter_start_io(GpadlIter *iter)
{
    assert(!iter->active);
    /* mapping is cached lazily on i/o */
    iter->map = NULL;
    iter->active = true;
}

/*
 * End the eariler started series of i/o on the GPADL and release the cached
 * mapping if any.
 */
static inline void gpadl_iter_end_io(GpadlIter *iter)
{
    assert(iter->active);

    if (iter->map) {
        gpadl_iter_cache_unmap(iter);
    }

    iter->active = false;
}

static void vmbus_resched(VMBus *vmbus);
static void vmbus_msg_cb(void *data, int status);

ssize_t vmbus_iov_to_gpadl(VMBusChannel *chan, VMBusGpadl *gpadl, uint32_t off,
                           const struct iovec *iov, size_t iov_cnt)
{
    GpadlIter iter;
    size_t i;
    ssize_t ret = 0;

    gpadl_iter_init(&iter, gpadl, chan->dev->dma_as,
                    DMA_DIRECTION_FROM_DEVICE);
    gpadl_iter_start_io(&iter);
    gpadl_iter_seek(&iter, off);
    for (i = 0; i < iov_cnt; i++) {
        ret = gpadl_iter_io(&iter, iov[i].iov_base, iov[i].iov_len);
        if (ret < 0) {
            goto out;
        }
    }
out:
    gpadl_iter_end_io(&iter);
    return ret;
}

int vmbus_map_sgl(VMBusChanReq *req, DMADirection dir, struct iovec *iov,
                  unsigned iov_cnt, size_t len, size_t off)
{
    int ret_cnt = 0, ret;
    unsigned i;
    QEMUSGList *sgl = &req->sgl;
    ScatterGatherEntry *sg = sgl->sg;

    for (i = 0; i < sgl->nsg; i++) {
        if (sg[i].len > off) {
            break;
        }
        off -= sg[i].len;
    }
    for (; len && i < sgl->nsg; i++) {
        dma_addr_t mlen = MIN(sg[i].len - off, len);
        dma_addr_t addr = sg[i].base + off;
        len -= mlen;
        off = 0;

        for (; mlen; ret_cnt++) {
            dma_addr_t l = mlen;
            dma_addr_t a = addr;

            if (ret_cnt == iov_cnt) {
                ret = -ENOBUFS;
                goto err;
            }

            iov[ret_cnt].iov_base = dma_memory_map(sgl->as, a, &l, dir);
            if (!l) {
                ret = -EFAULT;
                goto err;
            }
            iov[ret_cnt].iov_len = l;
            addr += l;
            mlen -= l;
        }
    }

    return ret_cnt;
err:
    vmbus_unmap_sgl(req, dir, iov, ret_cnt, 0);
    return ret;
}

void vmbus_unmap_sgl(VMBusChanReq *req, DMADirection dir, struct iovec *iov,
                     unsigned iov_cnt, size_t accessed)
{
    QEMUSGList *sgl = &req->sgl;
    unsigned i;

    for (i = 0; i < iov_cnt; i++) {
        size_t acsd = MIN(accessed, iov[i].iov_len);
        dma_memory_unmap(sgl->as, iov[i].iov_base, iov[i].iov_len, dir, acsd);
        accessed -= acsd;
    }
}

static const VMStateDescription vmstate_gpadl = {
    .name = "vmbus/gpadl",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(id, VMBusGpadl),
        VMSTATE_UINT32(child_relid, VMBusGpadl),
        VMSTATE_UINT32(num_gfns, VMBusGpadl),
        VMSTATE_UINT32(seen_gfns, VMBusGpadl),
        VMSTATE_VARRAY_UINT32_ALLOC(gfns, VMBusGpadl, num_gfns, 0,
                                    vmstate_info_uint64, uint64_t),
        VMSTATE_UINT8(state, VMBusGpadl),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * Wrap the index into a ring buffer of @len bytes.
 * @idx is assumed not to exceed twice the size of the ringbuffer, so only
 * single wraparound is considered.
 */
static inline uint32_t rb_idx_wrap(uint32_t idx, uint32_t len)
{
    if (idx >= len) {
        idx -= len;
    }
    return idx;
}

/*
 * Circular difference between two indices into a ring buffer of @len bytes.
 * @allow_catchup - whether @idx1 may catch up @idx2; e.g. read index may catch
 * up write index but not vice versa.
 */
static inline uint32_t rb_idx_delta(uint32_t idx1, uint32_t idx2, uint32_t len,
                                    bool allow_catchup)
{
    return rb_idx_wrap(idx2 + len - idx1 - !allow_catchup, len);
}

static vmbus_ring_buffer *ringbuf_map_hdr(VMBusRingBufCommon *ringbuf)
{
    vmbus_ring_buffer *rb;
    dma_addr_t mlen = sizeof(*rb);

    rb = dma_memory_map(ringbuf->as, ringbuf->rb_addr, &mlen,
                        DMA_DIRECTION_FROM_DEVICE);
    if (mlen != sizeof(*rb)) {
        dma_memory_unmap(ringbuf->as, rb, mlen,
                         DMA_DIRECTION_FROM_DEVICE, 0);
        return NULL;
    }
    return rb;
}

static void ringbuf_unmap_hdr(VMBusRingBufCommon *ringbuf,
                              vmbus_ring_buffer *rb, bool dirty)
{
    assert(rb);

    dma_memory_unmap(ringbuf->as, rb, sizeof(*rb), DMA_DIRECTION_FROM_DEVICE,
                     dirty ? sizeof(*rb) : 0);
}

static void ringbuf_init_common(VMBusRingBufCommon *ringbuf, VMBusGpadl *gpadl,
                                AddressSpace *as, DMADirection dir,
                                uint32_t begin, uint32_t end)
{
    ringbuf->as = as;
    ringbuf->rb_addr = gpadl->gfns[begin] << TARGET_PAGE_BITS;
    ringbuf->base = (begin + 1) << TARGET_PAGE_BITS;
    ringbuf->len = (end - begin - 1) << TARGET_PAGE_BITS;
    gpadl_iter_init(&ringbuf->iter, gpadl, as, dir);
}

static int ringbufs_init(VMBusChannel *chan)
{
    vmbus_ring_buffer *rb;
    VMBusSendRingBuf *send_ringbuf = &chan->send_ringbuf;
    VMBusRecvRingBuf *recv_ringbuf = &chan->recv_ringbuf;

    if (chan->ringbuf_send_offset <= 1 ||
        chan->gpadl->num_gfns <= chan->ringbuf_send_offset + 1) {
        return -EINVAL;
    }

    ringbuf_init_common(&recv_ringbuf->common, chan->gpadl, chan->dev->dma_as,
                        DMA_DIRECTION_TO_DEVICE, 0, chan->ringbuf_send_offset);
    ringbuf_init_common(&send_ringbuf->common, chan->gpadl, chan->dev->dma_as,
                        DMA_DIRECTION_FROM_DEVICE, chan->ringbuf_send_offset,
                        chan->gpadl->num_gfns);
    send_ringbuf->wanted = 0;
    send_ringbuf->reserved = 0;

    rb = ringbuf_map_hdr(&recv_ringbuf->common);
    if (!rb) {
        return -EFAULT;
    }
    recv_ringbuf->rd_idx = recv_ringbuf->last_rd_idx = rb->read_index;
    ringbuf_unmap_hdr(&recv_ringbuf->common, rb, false);

    rb = ringbuf_map_hdr(&send_ringbuf->common);
    if (!rb) {
        return -EFAULT;
    }
    send_ringbuf->wr_idx = send_ringbuf->last_wr_idx = rb->write_index;
    send_ringbuf->last_seen_rd_idx = rb->read_index;
    rb->feature_bits |= VMBUS_RING_BUFFER_FEAT_PENDING_SZ;
    ringbuf_unmap_hdr(&send_ringbuf->common, rb, true);

    if (recv_ringbuf->rd_idx >= recv_ringbuf->common.len ||
        send_ringbuf->wr_idx >= send_ringbuf->common.len) {
        return -EOVERFLOW;
    }

    return 0;
}

/*
 * Perform io between the GPADL-backed ringbuffer @ringbuf and @buf, wrapping
 * around if needed.
 * @len is assumed not to exceed the size of the ringbuffer, so only single
 * wraparound is considered.
 */
static ssize_t ringbuf_io(VMBusRingBufCommon *ringbuf, void *buf, uint32_t len)
{
    ssize_t ret1 = 0, ret2 = 0;
    uint32_t remain = ringbuf->len + ringbuf->base - ringbuf->iter.off;

    if (len >= remain) {
        ret1 = gpadl_iter_io(&ringbuf->iter, buf, remain);
        if (ret1 < 0) {
            return ret1;
        }
        gpadl_iter_seek(&ringbuf->iter, ringbuf->base);
        buf += remain;
        len -= remain;
    }
    ret2 = gpadl_iter_io(&ringbuf->iter, buf, len);
    if (ret2 < 0) {
        return ret2;
    }
    return ret1 + ret2;
}

/*
 * Position the circular iterator within @ringbuf to offset @new_off, wrapping
 * around if needed.
 * @new_off is assumed not to exceed twice the size of the ringbuffer, so only
 * single wraparound is considered.
 */
static inline void ringbuf_seek(VMBusRingBufCommon *ringbuf, uint32_t new_off)
{
    gpadl_iter_seek(&ringbuf->iter,
                    ringbuf->base + rb_idx_wrap(new_off, ringbuf->len));
}

static inline uint32_t ringbuf_tell(VMBusRingBufCommon *ringbuf)
{
    return ringbuf->iter.off - ringbuf->base;
}

static inline void ringbuf_start_io(VMBusRingBufCommon *ringbuf)
{
    gpadl_iter_start_io(&ringbuf->iter);
}

static inline void ringbuf_end_io(VMBusRingBufCommon *ringbuf)
{
    gpadl_iter_end_io(&ringbuf->iter);
}

VMBusDevice *vmbus_channel_device(VMBusChannel *chan)
{
    return chan->dev;
}

VMBusChannel *vmbus_device_channel(VMBusDevice *dev, uint32_t chan_idx)
{
    if (chan_idx >= dev->num_channels) {
        return NULL;
    }
    return &dev->channels[chan_idx];
}

uint32_t vmbus_channel_idx(VMBusChannel *chan)
{
    return chan - chan->dev->channels;
}

void vmbus_channel_notify_host(VMBusChannel *chan)
{
    event_notifier_set(&chan->notifier);
}

bool vmbus_channel_is_open(VMBusChannel *chan)
{
    return chan->is_open;
}

/*
 * Notify the guest side about the data to work on in the channel ring buffer.
 * The notification is done by signaling a dedicated per-channel SynIC event
 * flag (more recent guests) or setting a bit in the interrupt page and firing
 * the VMBus SINT (older guests).
 */
static int vmbus_channel_notify_guest(VMBusChannel *chan)
{
    int res = 0;
    unsigned long *int_map, mask;
    unsigned idx;
    hwaddr addr = chan->vmbus->int_page_gpa;
    hwaddr len = TARGET_PAGE_SIZE / 2, dirty = 0;

    trace_vmbus_channel_notify_guest(chan->id);

    if (!addr) {
        return hyperv_set_event_flag(chan->notify_route, chan->id);
    }

    int_map = cpu_physical_memory_map(addr, &len, 1);
    if (len != TARGET_PAGE_SIZE / 2) {
        res = -ENXIO;
        goto unmap;
    }

    idx = BIT_WORD(chan->id);
    mask = BIT_MASK(chan->id);
    if ((qatomic_fetch_or(&int_map[idx], mask) & mask) != mask) {
        res = hyperv_sint_route_set_sint(chan->notify_route);
        dirty = len;
    }

unmap:
    cpu_physical_memory_unmap(int_map, len, 1, dirty);
    return res;
}

#define VMBUS_PKT_TRAILER      sizeof(uint64_t)

static uint32_t vmbus_pkt_hdr_set_offsets(vmbus_packet_hdr *hdr,
                                          uint32_t desclen, uint32_t msglen)
{
    hdr->offset_qwords = sizeof(*hdr) / sizeof(uint64_t) +
        DIV_ROUND_UP(desclen, sizeof(uint64_t));
    hdr->len_qwords = hdr->offset_qwords +
        DIV_ROUND_UP(msglen, sizeof(uint64_t));
    return hdr->len_qwords * sizeof(uint64_t) + VMBUS_PKT_TRAILER;
}

/*
 * Simplified ring buffer operation with paired barriers annotations in the
 * producer and consumer loops:
 *
 * producer                           * consumer
 * ~~~~~~~~                           * ~~~~~~~~
 * write pending_send_sz              * read write_index
 * smp_mb                       [A]   * smp_mb                       [C]
 * read read_index                    * read packet
 * smp_mb                       [B]   * read/write out-of-band data
 * read/write out-of-band data        * smp_mb                       [B]
 * write packet                       * write read_index
 * smp_mb                       [C]   * smp_mb                       [A]
 * write write_index                  * read pending_send_sz
 * smp_wmb                      [D]   * smp_rmb                      [D]
 * write pending_send_sz              * read write_index
 * ...                                * ...
 */

static inline uint32_t ringbuf_send_avail(VMBusSendRingBuf *ringbuf)
{
    /* don't trust guest data */
    if (ringbuf->last_seen_rd_idx >= ringbuf->common.len) {
        return 0;
    }
    return rb_idx_delta(ringbuf->wr_idx, ringbuf->last_seen_rd_idx,
                        ringbuf->common.len, false);
}

static ssize_t ringbuf_send_update_idx(VMBusChannel *chan)
{
    VMBusSendRingBuf *ringbuf = &chan->send_ringbuf;
    vmbus_ring_buffer *rb;
    uint32_t written;

    written = rb_idx_delta(ringbuf->last_wr_idx, ringbuf->wr_idx,
                           ringbuf->common.len, true);
    if (!written) {
        return 0;
    }

    rb = ringbuf_map_hdr(&ringbuf->common);
    if (!rb) {
        return -EFAULT;
    }

    ringbuf->reserved -= written;

    /* prevent reorder with the data operation and packet write */
    smp_mb();                   /* barrier pair [C] */
    rb->write_index = ringbuf->wr_idx;

    /*
     * If the producer earlier indicated that it wants to be notified when the
     * consumer frees certain amount of space in the ring buffer, that amount
     * is reduced by the size of the completed write.
     */
    if (ringbuf->wanted) {
        /* otherwise reservation would fail */
        assert(ringbuf->wanted < written);
        ringbuf->wanted -= written;
        /* prevent reorder with write_index write */
        smp_wmb();              /* barrier pair [D] */
        rb->pending_send_sz = ringbuf->wanted;
    }

    /* prevent reorder with write_index or pending_send_sz write */
    smp_mb();                   /* barrier pair [A] */
    ringbuf->last_seen_rd_idx = rb->read_index;

    /*
     * The consumer may have missed the reduction of pending_send_sz and skip
     * notification, so re-check the blocking condition, and, if it's no longer
     * true, ensure processing another iteration by simulating consumer's
     * notification.
     */
    if (ringbuf_send_avail(ringbuf) >= ringbuf->wanted) {
        vmbus_channel_notify_host(chan);
    }

    /* skip notification by consumer's request */
    if (rb->interrupt_mask) {
        goto out;
    }

    /*
     * The consumer hasn't caught up with the producer's previous state so it's
     * not blocked.
     * (last_seen_rd_idx comes from the guest but it's safe to use w/o
     * validation here as it only affects notification.)
     */
    if (rb_idx_delta(ringbuf->last_seen_rd_idx, ringbuf->wr_idx,
                     ringbuf->common.len, true) > written) {
        goto out;
    }

    vmbus_channel_notify_guest(chan);
out:
    ringbuf_unmap_hdr(&ringbuf->common, rb, true);
    ringbuf->last_wr_idx = ringbuf->wr_idx;
    return written;
}

int vmbus_channel_reserve(VMBusChannel *chan,
                          uint32_t desclen, uint32_t msglen)
{
    VMBusSendRingBuf *ringbuf = &chan->send_ringbuf;
    vmbus_ring_buffer *rb = NULL;
    vmbus_packet_hdr hdr;
    uint32_t needed = ringbuf->reserved +
        vmbus_pkt_hdr_set_offsets(&hdr, desclen, msglen);

    /* avoid touching the guest memory if possible */
    if (likely(needed <= ringbuf_send_avail(ringbuf))) {
        goto success;
    }

    rb = ringbuf_map_hdr(&ringbuf->common);
    if (!rb) {
        return -EFAULT;
    }

    /* fetch read index from guest memory and try again */
    ringbuf->last_seen_rd_idx = rb->read_index;

    if (likely(needed <= ringbuf_send_avail(ringbuf))) {
        goto success;
    }

    rb->pending_send_sz = needed;

    /*
     * The consumer may have made progress and freed up some space before
     * seeing updated pending_send_sz, so re-read read_index (preventing
     * reorder with the pending_send_sz write) and try again.
     */
    smp_mb();                   /* barrier pair [A] */
    ringbuf->last_seen_rd_idx = rb->read_index;

    if (needed > ringbuf_send_avail(ringbuf)) {
        goto out;
    }

success:
    ringbuf->reserved = needed;
    needed = 0;

    /* clear pending_send_sz if it was set */
    if (ringbuf->wanted) {
        if (!rb) {
            rb = ringbuf_map_hdr(&ringbuf->common);
            if (!rb) {
                /* failure to clear pending_send_sz is non-fatal */
                goto out;
            }
        }

        rb->pending_send_sz = 0;
    }

    /* prevent reorder of the following data operation with read_index read */
    smp_mb();                   /* barrier pair [B] */

out:
    if (rb) {
        ringbuf_unmap_hdr(&ringbuf->common, rb, ringbuf->wanted == needed);
    }
    ringbuf->wanted = needed;
    return needed ? -ENOSPC : 0;
}

ssize_t vmbus_channel_send(VMBusChannel *chan, uint16_t pkt_type,
                           void *desc, uint32_t desclen,
                           void *msg, uint32_t msglen,
                           bool need_comp, uint64_t transaction_id)
{
    ssize_t ret = 0;
    vmbus_packet_hdr hdr;
    uint32_t totlen;
    VMBusSendRingBuf *ringbuf = &chan->send_ringbuf;

    if (!vmbus_channel_is_open(chan)) {
        return -EINVAL;
    }

    totlen = vmbus_pkt_hdr_set_offsets(&hdr, desclen, msglen);
    hdr.type = pkt_type;
    hdr.flags = need_comp ? VMBUS_PACKET_FLAG_REQUEST_COMPLETION : 0;
    hdr.transaction_id = transaction_id;

    assert(totlen <= ringbuf->reserved);

    ringbuf_start_io(&ringbuf->common);
    ringbuf_seek(&ringbuf->common, ringbuf->wr_idx);
    ret = ringbuf_io(&ringbuf->common, &hdr, sizeof(hdr));
    if (ret < 0) {
        goto out;
    }
    if (desclen) {
        assert(desc);
        ret = ringbuf_io(&ringbuf->common, desc, desclen);
        if (ret < 0) {
            goto out;
        }
        ringbuf_seek(&ringbuf->common,
                     ringbuf->wr_idx + hdr.offset_qwords * sizeof(uint64_t));
    }
    ret = ringbuf_io(&ringbuf->common, msg, msglen);
    if (ret < 0) {
        goto out;
    }
    ringbuf_seek(&ringbuf->common, ringbuf->wr_idx + totlen);
    ringbuf->wr_idx = ringbuf_tell(&ringbuf->common);
    ret = 0;
out:
    ringbuf_end_io(&ringbuf->common);
    if (ret) {
        return ret;
    }
    return ringbuf_send_update_idx(chan);
}

ssize_t vmbus_channel_send_completion(VMBusChanReq *req,
                                      void *msg, uint32_t msglen)
{
    assert(req->need_comp);
    return vmbus_channel_send(req->chan, VMBUS_PACKET_COMP, NULL, 0,
                              msg, msglen, false, req->transaction_id);
}

static int sgl_from_gpa_ranges(QEMUSGList *sgl, VMBusDevice *dev,
                               VMBusRingBufCommon *ringbuf, uint32_t len)
{
    int ret;
    vmbus_pkt_gpa_direct hdr;
    hwaddr curaddr = 0;
    hwaddr curlen = 0;
    int num;

    if (len < sizeof(hdr)) {
        return -EIO;
    }
    ret = ringbuf_io(ringbuf, &hdr, sizeof(hdr));
    if (ret < 0) {
        return ret;
    }
    len -= sizeof(hdr);

    num = (len - hdr.rangecount * sizeof(vmbus_gpa_range)) / sizeof(uint64_t);
    if (num < 0) {
        return -EIO;
    }
    qemu_sglist_init(sgl, DEVICE(dev), num, ringbuf->as);

    for (; hdr.rangecount; hdr.rangecount--) {
        vmbus_gpa_range range;

        if (len < sizeof(range)) {
            goto eio;
        }
        ret = ringbuf_io(ringbuf, &range, sizeof(range));
        if (ret < 0) {
            goto err;
        }
        len -= sizeof(range);

        if (range.byte_offset & TARGET_PAGE_MASK) {
            goto eio;
        }

        for (; range.byte_count; range.byte_offset = 0) {
            uint64_t paddr;
            uint32_t plen = MIN(range.byte_count,
                                TARGET_PAGE_SIZE - range.byte_offset);

            if (len < sizeof(uint64_t)) {
                goto eio;
            }
            ret = ringbuf_io(ringbuf, &paddr, sizeof(paddr));
            if (ret < 0) {
                goto err;
            }
            len -= sizeof(uint64_t);
            paddr <<= TARGET_PAGE_BITS;
            paddr |= range.byte_offset;
            range.byte_count -= plen;

            if (curaddr + curlen == paddr) {
                /* consecutive fragments - join */
                curlen += plen;
            } else {
                if (curlen) {
                    qemu_sglist_add(sgl, curaddr, curlen);
                }

                curaddr = paddr;
                curlen = plen;
            }
        }
    }

    if (curlen) {
        qemu_sglist_add(sgl, curaddr, curlen);
    }

    return 0;
eio:
    ret = -EIO;
err:
    qemu_sglist_destroy(sgl);
    return ret;
}

static VMBusChanReq *vmbus_alloc_req(VMBusChannel *chan,
                                     uint32_t size, uint16_t pkt_type,
                                     uint32_t msglen, uint64_t transaction_id,
                                     bool need_comp)
{
    VMBusChanReq *req;
    uint32_t msgoff = QEMU_ALIGN_UP(size, __alignof__(*req->msg));
    uint32_t totlen = msgoff + msglen;

    req = g_malloc0(totlen);
    req->chan = chan;
    req->pkt_type = pkt_type;
    req->msg = (void *)req + msgoff;
    req->msglen = msglen;
    req->transaction_id = transaction_id;
    req->need_comp = need_comp;
    return req;
}

int vmbus_channel_recv_start(VMBusChannel *chan)
{
    VMBusRecvRingBuf *ringbuf = &chan->recv_ringbuf;
    vmbus_ring_buffer *rb;

    rb = ringbuf_map_hdr(&ringbuf->common);
    if (!rb) {
        return -EFAULT;
    }
    ringbuf->last_seen_wr_idx = rb->write_index;
    ringbuf_unmap_hdr(&ringbuf->common, rb, false);

    if (ringbuf->last_seen_wr_idx >= ringbuf->common.len) {
        return -EOVERFLOW;
    }

    /* prevent reorder of the following data operation with write_index read */
    smp_mb();                   /* barrier pair [C] */
    return 0;
}

void *vmbus_channel_recv_peek(VMBusChannel *chan, uint32_t size)
{
    VMBusRecvRingBuf *ringbuf = &chan->recv_ringbuf;
    vmbus_packet_hdr hdr = {};
    VMBusChanReq *req;
    uint32_t avail;
    uint32_t totlen, pktlen, msglen, msgoff, desclen;

    assert(size >= sizeof(*req));

    /* safe as last_seen_wr_idx is validated in vmbus_channel_recv_start */
    avail = rb_idx_delta(ringbuf->rd_idx, ringbuf->last_seen_wr_idx,
                         ringbuf->common.len, true);
    if (avail < sizeof(hdr)) {
        return NULL;
    }

    ringbuf_seek(&ringbuf->common, ringbuf->rd_idx);
    if (ringbuf_io(&ringbuf->common, &hdr, sizeof(hdr)) < 0) {
        return NULL;
    }

    pktlen = hdr.len_qwords * sizeof(uint64_t);
    totlen = pktlen + VMBUS_PKT_TRAILER;
    if (totlen > avail) {
        return NULL;
    }

    msgoff = hdr.offset_qwords * sizeof(uint64_t);
    if (msgoff > pktlen || msgoff < sizeof(hdr)) {
        error_report("%s: malformed packet: %u %u", __func__, msgoff, pktlen);
        return NULL;
    }

    msglen = pktlen - msgoff;

    req = vmbus_alloc_req(chan, size, hdr.type, msglen, hdr.transaction_id,
                          hdr.flags & VMBUS_PACKET_FLAG_REQUEST_COMPLETION);

    switch (hdr.type) {
    case VMBUS_PACKET_DATA_USING_GPA_DIRECT:
        desclen = msgoff - sizeof(hdr);
        if (sgl_from_gpa_ranges(&req->sgl, chan->dev, &ringbuf->common,
                                desclen) < 0) {
            error_report("%s: failed to convert GPA ranges to SGL", __func__);
            goto free_req;
        }
        break;
    case VMBUS_PACKET_DATA_INBAND:
    case VMBUS_PACKET_COMP:
        break;
    default:
        error_report("%s: unexpected msg type: %x", __func__, hdr.type);
        goto free_req;
    }

    ringbuf_seek(&ringbuf->common, ringbuf->rd_idx + msgoff);
    if (ringbuf_io(&ringbuf->common, req->msg, msglen) < 0) {
        goto free_req;
    }
    ringbuf_seek(&ringbuf->common, ringbuf->rd_idx + totlen);

    return req;
free_req:
    vmbus_free_req(req);
    return NULL;
}

void vmbus_channel_recv_pop(VMBusChannel *chan)
{
    VMBusRecvRingBuf *ringbuf = &chan->recv_ringbuf;
    ringbuf->rd_idx = ringbuf_tell(&ringbuf->common);
}

ssize_t vmbus_channel_recv_done(VMBusChannel *chan)
{
    VMBusRecvRingBuf *ringbuf = &chan->recv_ringbuf;
    vmbus_ring_buffer *rb;
    uint32_t read;

    read = rb_idx_delta(ringbuf->last_rd_idx, ringbuf->rd_idx,
                        ringbuf->common.len, true);
    if (!read) {
        return 0;
    }

    rb = ringbuf_map_hdr(&ringbuf->common);
    if (!rb) {
        return -EFAULT;
    }

    /* prevent reorder with the data operation and packet read */
    smp_mb();                   /* barrier pair [B] */
    rb->read_index = ringbuf->rd_idx;

    /* prevent reorder of the following pending_send_sz read */
    smp_mb();                   /* barrier pair [A] */

    if (rb->interrupt_mask) {
        goto out;
    }

    if (rb->feature_bits & VMBUS_RING_BUFFER_FEAT_PENDING_SZ) {
        uint32_t wr_idx, wr_avail;
        uint32_t wanted = rb->pending_send_sz;

        if (!wanted) {
            goto out;
        }

        /* prevent reorder with pending_send_sz read */
        smp_rmb();              /* barrier pair [D] */
        wr_idx = rb->write_index;

        wr_avail = rb_idx_delta(wr_idx, ringbuf->rd_idx, ringbuf->common.len,
                                true);

        /* the producer wasn't blocked on the consumer state */
        if (wr_avail >= read + wanted) {
            goto out;
        }
        /* there's not enough space for the producer to make progress */
        if (wr_avail < wanted) {
            goto out;
        }
    }

    vmbus_channel_notify_guest(chan);
out:
    ringbuf_unmap_hdr(&ringbuf->common, rb, true);
    ringbuf->last_rd_idx = ringbuf->rd_idx;
    return read;
}

void vmbus_free_req(void *req)
{
    VMBusChanReq *r = req;

    if (!req) {
        return;
    }

    if (r->sgl.dev) {
        qemu_sglist_destroy(&r->sgl);
    }
    g_free(req);
}

static const VMStateDescription vmstate_sgent = {
    .name = "vmbus/sgentry",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(base, ScatterGatherEntry),
        VMSTATE_UINT64(len, ScatterGatherEntry),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct VMBusChanReqSave {
    uint16_t chan_idx;
    uint16_t pkt_type;
    uint32_t msglen;
    void *msg;
    uint64_t transaction_id;
    bool need_comp;
    uint32_t num;
    ScatterGatherEntry *sgl;
} VMBusChanReqSave;

static const VMStateDescription vmstate_vmbus_chan_req = {
    .name = "vmbus/vmbus_chan_req",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(chan_idx, VMBusChanReqSave),
        VMSTATE_UINT16(pkt_type, VMBusChanReqSave),
        VMSTATE_UINT32(msglen, VMBusChanReqSave),
        VMSTATE_VBUFFER_ALLOC_UINT32(msg, VMBusChanReqSave, 0, NULL, msglen),
        VMSTATE_UINT64(transaction_id, VMBusChanReqSave),
        VMSTATE_BOOL(need_comp, VMBusChanReqSave),
        VMSTATE_UINT32(num, VMBusChanReqSave),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(sgl, VMBusChanReqSave, num,
                                             vmstate_sgent, ScatterGatherEntry),
        VMSTATE_END_OF_LIST()
    }
};

void vmbus_save_req(QEMUFile *f, VMBusChanReq *req)
{
    VMBusChanReqSave req_save;

    req_save.chan_idx = req->chan->subchan_idx;
    req_save.pkt_type = req->pkt_type;
    req_save.msglen = req->msglen;
    req_save.msg = req->msg;
    req_save.transaction_id = req->transaction_id;
    req_save.need_comp = req->need_comp;
    req_save.num = req->sgl.nsg;
    req_save.sgl = g_memdup(req->sgl.sg,
                            req_save.num * sizeof(ScatterGatherEntry));

    vmstate_save_state(f, &vmstate_vmbus_chan_req, &req_save, NULL);

    g_free(req_save.sgl);
}

void *vmbus_load_req(QEMUFile *f, VMBusDevice *dev, uint32_t size)
{
    VMBusChanReqSave req_save;
    VMBusChanReq *req = NULL;
    VMBusChannel *chan = NULL;
    uint32_t i;

    vmstate_load_state(f, &vmstate_vmbus_chan_req, &req_save, 0);

    if (req_save.chan_idx >= dev->num_channels) {
        error_report("%s: %u(chan_idx) > %u(num_channels)", __func__,
                     req_save.chan_idx, dev->num_channels);
        goto out;
    }
    chan = &dev->channels[req_save.chan_idx];

    if (vmbus_channel_reserve(chan, 0, req_save.msglen)) {
        goto out;
    }

    req = vmbus_alloc_req(chan, size, req_save.pkt_type, req_save.msglen,
                          req_save.transaction_id, req_save.need_comp);
    if (req_save.msglen) {
        memcpy(req->msg, req_save.msg, req_save.msglen);
    }

    for (i = 0; i < req_save.num; i++) {
        qemu_sglist_add(&req->sgl, req_save.sgl[i].base, req_save.sgl[i].len);
    }

out:
    if (req_save.msglen) {
        g_free(req_save.msg);
    }
    if (req_save.num) {
        g_free(req_save.sgl);
    }
    return req;
}

static void channel_event_cb(EventNotifier *e)
{
    VMBusChannel *chan = container_of(e, VMBusChannel, notifier);
    if (event_notifier_test_and_clear(e)) {
        /*
         * All receives are supposed to happen within the device worker, so
         * bracket it with ringbuf_start/end_io on the receive ringbuffer, and
         * potentially reuse the cached mapping throughout the worker.
         * Can't do this for sends as they may happen outside the device
         * worker.
         */
        VMBusRecvRingBuf *ringbuf = &chan->recv_ringbuf;
        ringbuf_start_io(&ringbuf->common);
        chan->notify_cb(chan);
        ringbuf_end_io(&ringbuf->common);

    }
}

static int alloc_chan_id(VMBus *vmbus)
{
    int ret;

    ret = find_next_zero_bit(vmbus->chanid_bitmap, VMBUS_CHANID_COUNT, 0);
    if (ret == VMBUS_CHANID_COUNT) {
        return -ENOMEM;
    }
    return ret + VMBUS_FIRST_CHANID;
}

static int register_chan_id(VMBusChannel *chan)
{
    return test_and_set_bit(chan->id - VMBUS_FIRST_CHANID,
                            chan->vmbus->chanid_bitmap) ? -EEXIST : 0;
}

static void unregister_chan_id(VMBusChannel *chan)
{
    clear_bit(chan->id - VMBUS_FIRST_CHANID, chan->vmbus->chanid_bitmap);
}

static uint32_t chan_connection_id(VMBusChannel *chan)
{
    return VMBUS_CHAN_CONNECTION_OFFSET + chan->id;
}

static void init_channel(VMBus *vmbus, VMBusDevice *dev, VMBusDeviceClass *vdc,
                         VMBusChannel *chan, uint16_t idx, Error **errp)
{
    int res;

    chan->dev = dev;
    chan->notify_cb = vdc->chan_notify_cb;
    chan->subchan_idx = idx;
    chan->vmbus = vmbus;

    res = alloc_chan_id(vmbus);
    if (res < 0) {
        error_setg(errp, "no spare channel id");
        return;
    }
    chan->id = res;
    register_chan_id(chan);

    /*
     * The guest drivers depend on the device subchannels (idx #1+) to be
     * offered after the primary channel (idx #0) of that device.  To ensure
     * that, record the channels on the channel list in the order they appear
     * within the device.
     */
    QTAILQ_INSERT_TAIL(&vmbus->channel_list, chan, link);
}

static void deinit_channel(VMBusChannel *chan)
{
    assert(chan->state == VMCHAN_INIT);
    QTAILQ_REMOVE(&chan->vmbus->channel_list, chan, link);
    unregister_chan_id(chan);
}

static void create_channels(VMBus *vmbus, VMBusDevice *dev, Error **errp)
{
    uint16_t i;
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(dev);
    Error *err = NULL;

    dev->num_channels = vdc->num_channels ? vdc->num_channels(dev) : 1;
    if (dev->num_channels < 1) {
        error_setg(errp, "invalid #channels: %u", dev->num_channels);
        return;
    }

    dev->channels = g_new0(VMBusChannel, dev->num_channels);
    for (i = 0; i < dev->num_channels; i++) {
        init_channel(vmbus, dev, vdc, &dev->channels[i], i, &err);
        if (err) {
            goto err_init;
        }
    }

    return;

err_init:
    while (i--) {
        deinit_channel(&dev->channels[i]);
    }
    error_propagate(errp, err);
}

static void free_channels(VMBusDevice *dev)
{
    uint16_t i;
    for (i = 0; i < dev->num_channels; i++) {
        deinit_channel(&dev->channels[i]);
    }
    g_free(dev->channels);
}

static HvSintRoute *make_sint_route(VMBus *vmbus, uint32_t vp_index)
{
    VMBusChannel *chan;

    if (vp_index == vmbus->target_vp) {
        hyperv_sint_route_ref(vmbus->sint_route);
        return vmbus->sint_route;
    }

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->target_vp == vp_index && vmbus_channel_is_open(chan)) {
            hyperv_sint_route_ref(chan->notify_route);
            return chan->notify_route;
        }
    }

    return hyperv_sint_route_new(vp_index, VMBUS_SINT, NULL, NULL);
}

static void open_channel(VMBusChannel *chan)
{
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(chan->dev);

    chan->gpadl = vmbus_get_gpadl(chan, chan->ringbuf_gpadl);
    if (!chan->gpadl) {
        return;
    }

    if (ringbufs_init(chan)) {
        goto put_gpadl;
    }

    if (event_notifier_init(&chan->notifier, 0)) {
        goto put_gpadl;
    }

    event_notifier_set_handler(&chan->notifier, channel_event_cb);

    if (hyperv_set_event_flag_handler(chan_connection_id(chan),
                                      &chan->notifier)) {
        goto cleanup_notifier;
    }

    chan->notify_route = make_sint_route(chan->vmbus, chan->target_vp);
    if (!chan->notify_route) {
        goto clear_event_flag_handler;
    }

    if (vdc->open_channel && vdc->open_channel(chan)) {
        goto unref_sint_route;
    }

    chan->is_open = true;
    return;

unref_sint_route:
    hyperv_sint_route_unref(chan->notify_route);
clear_event_flag_handler:
    hyperv_set_event_flag_handler(chan_connection_id(chan), NULL);
cleanup_notifier:
    event_notifier_set_handler(&chan->notifier, NULL);
    event_notifier_cleanup(&chan->notifier);
put_gpadl:
    vmbus_put_gpadl(chan->gpadl);
}

static void close_channel(VMBusChannel *chan)
{
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(chan->dev);

    if (!chan->is_open) {
        return;
    }

    if (vdc->close_channel) {
        vdc->close_channel(chan);
    }

    hyperv_sint_route_unref(chan->notify_route);
    hyperv_set_event_flag_handler(chan_connection_id(chan), NULL);
    event_notifier_set_handler(&chan->notifier, NULL);
    event_notifier_cleanup(&chan->notifier);
    vmbus_put_gpadl(chan->gpadl);
    chan->is_open = false;
}

static int channel_post_load(void *opaque, int version_id)
{
    VMBusChannel *chan = opaque;

    return register_chan_id(chan);
}

static const VMStateDescription vmstate_channel = {
    .name = "vmbus/channel",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = channel_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(id, VMBusChannel),
        VMSTATE_UINT16(subchan_idx, VMBusChannel),
        VMSTATE_UINT32(open_id, VMBusChannel),
        VMSTATE_UINT32(target_vp, VMBusChannel),
        VMSTATE_UINT32(ringbuf_gpadl, VMBusChannel),
        VMSTATE_UINT32(ringbuf_send_offset, VMBusChannel),
        VMSTATE_UINT8(offer_state, VMBusChannel),
        VMSTATE_UINT8(state, VMBusChannel),
        VMSTATE_END_OF_LIST()
    }
};

static VMBusChannel *find_channel(VMBus *vmbus, uint32_t id)
{
    VMBusChannel *chan;
    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->id == id) {
            return chan;
        }
    }
    return NULL;
}

static int enqueue_incoming_message(VMBus *vmbus,
                                    const struct hyperv_post_message_input *msg)
{
    int ret = 0;
    uint8_t idx, prev_size;

    qemu_mutex_lock(&vmbus->rx_queue_lock);

    if (vmbus->rx_queue_size == HV_MSG_QUEUE_LEN) {
        ret = -ENOBUFS;
        goto out;
    }

    prev_size = vmbus->rx_queue_size;
    idx = (vmbus->rx_queue_head + vmbus->rx_queue_size) % HV_MSG_QUEUE_LEN;
    memcpy(&vmbus->rx_queue[idx], msg, sizeof(*msg));
    vmbus->rx_queue_size++;

    /* only need to resched if the queue was empty before */
    if (!prev_size) {
        vmbus_resched(vmbus);
    }
out:
    qemu_mutex_unlock(&vmbus->rx_queue_lock);
    return ret;
}

static uint16_t vmbus_recv_message(const struct hyperv_post_message_input *msg,
                                   void *data)
{
    VMBus *vmbus = data;
    struct vmbus_message_header *vmbus_msg;

    if (msg->message_type != HV_MESSAGE_VMBUS) {
        return HV_STATUS_INVALID_HYPERCALL_INPUT;
    }

    if (msg->payload_size < sizeof(struct vmbus_message_header)) {
        return HV_STATUS_INVALID_HYPERCALL_INPUT;
    }

    vmbus_msg = (struct vmbus_message_header *)msg->payload;

    trace_vmbus_recv_message(vmbus_msg->message_type, msg->payload_size);

    if (vmbus_msg->message_type == VMBUS_MSG_INVALID ||
        vmbus_msg->message_type >= VMBUS_MSG_COUNT) {
        error_report("vmbus: unknown message type %#x",
                     vmbus_msg->message_type);
        return HV_STATUS_INVALID_HYPERCALL_INPUT;
    }

    if (enqueue_incoming_message(vmbus, msg)) {
        return HV_STATUS_INSUFFICIENT_BUFFERS;
    }
    return HV_STATUS_SUCCESS;
}

static bool vmbus_initialized(VMBus *vmbus)
{
    return vmbus->version > 0 && vmbus->version <= VMBUS_VERSION_CURRENT;
}

static void vmbus_reset_all(VMBus *vmbus)
{
    qbus_reset_all(BUS(vmbus));
}

static void post_msg(VMBus *vmbus, void *msgdata, uint32_t msglen)
{
    int ret;
    struct hyperv_message msg = {
        .header.message_type = HV_MESSAGE_VMBUS,
    };

    assert(!vmbus->msg_in_progress);
    assert(msglen <= sizeof(msg.payload));
    assert(msglen >= sizeof(struct vmbus_message_header));

    vmbus->msg_in_progress = true;

    trace_vmbus_post_msg(((struct vmbus_message_header *)msgdata)->message_type,
                         msglen);

    memcpy(msg.payload, msgdata, msglen);
    msg.header.payload_size = ROUND_UP(msglen, VMBUS_MESSAGE_SIZE_ALIGN);

    ret = hyperv_post_msg(vmbus->sint_route, &msg);
    if (ret == 0 || ret == -EAGAIN) {
        return;
    }

    error_report("message delivery fatal failure: %d; aborting vmbus", ret);
    vmbus_reset_all(vmbus);
}

static int vmbus_init(VMBus *vmbus)
{
    if (vmbus->target_vp != (uint32_t)-1) {
        vmbus->sint_route = hyperv_sint_route_new(vmbus->target_vp, VMBUS_SINT,
                                                  vmbus_msg_cb, vmbus);
        if (!vmbus->sint_route) {
            error_report("failed to set up SINT route");
            return -ENOMEM;
        }
    }
    return 0;
}

static void vmbus_deinit(VMBus *vmbus)
{
    VMBusGpadl *gpadl, *tmp_gpadl;
    VMBusChannel *chan;

    QTAILQ_FOREACH_SAFE(gpadl, &vmbus->gpadl_list, link, tmp_gpadl) {
        if (gpadl->state == VMGPADL_TORNDOWN) {
            continue;
        }
        vmbus_put_gpadl(gpadl);
    }

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        chan->offer_state = VMOFFER_INIT;
    }

    hyperv_sint_route_unref(vmbus->sint_route);
    vmbus->sint_route = NULL;
    vmbus->int_page_gpa = 0;
    vmbus->target_vp = (uint32_t)-1;
    vmbus->version = 0;
    vmbus->state = VMBUS_LISTEN;
    vmbus->msg_in_progress = false;
}

static void handle_initiate_contact(VMBus *vmbus,
                                    vmbus_message_initiate_contact *msg,
                                    uint32_t msglen)
{
    if (msglen < sizeof(*msg)) {
        return;
    }

    trace_vmbus_initiate_contact(msg->version_requested >> 16,
                                 msg->version_requested & 0xffff,
                                 msg->target_vcpu, msg->monitor_page1,
                                 msg->monitor_page2, msg->interrupt_page);

    /*
     * Reset vmbus on INITIATE_CONTACT regardless of its previous state.
     * Useful, in particular, with vmbus-aware BIOS which can't shut vmbus down
     * before handing over to OS loader.
     */
    vmbus_reset_all(vmbus);

    vmbus->target_vp = msg->target_vcpu;
    vmbus->version = msg->version_requested;
    if (vmbus->version < VMBUS_VERSION_WIN8) {
        /* linux passes interrupt page even when it doesn't need it */
        vmbus->int_page_gpa = msg->interrupt_page;
    }
    vmbus->state = VMBUS_HANDSHAKE;

    if (vmbus_init(vmbus)) {
        error_report("failed to init vmbus; aborting");
        vmbus_deinit(vmbus);
        return;
    }
}

static void send_handshake(VMBus *vmbus)
{
    struct vmbus_message_version_response msg = {
        .header.message_type = VMBUS_MSG_VERSION_RESPONSE,
        .version_supported = vmbus_initialized(vmbus),
    };

    post_msg(vmbus, &msg, sizeof(msg));
}

static void handle_request_offers(VMBus *vmbus, void *msgdata, uint32_t msglen)
{
    VMBusChannel *chan;

    if (!vmbus_initialized(vmbus)) {
        return;
    }

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->offer_state == VMOFFER_INIT) {
            chan->offer_state = VMOFFER_SENDING;
            break;
        }
    }

    vmbus->state = VMBUS_OFFER;
}

static void send_offer(VMBus *vmbus)
{
    VMBusChannel *chan;
    struct vmbus_message_header alloffers_msg = {
        .message_type = VMBUS_MSG_ALLOFFERS_DELIVERED,
    };

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->offer_state == VMOFFER_SENDING) {
            VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(chan->dev);
            /* Hyper-V wants LE GUIDs */
            QemuUUID classid = qemu_uuid_bswap(vdc->classid);
            QemuUUID instanceid = qemu_uuid_bswap(chan->dev->instanceid);
            struct vmbus_message_offer_channel msg = {
                .header.message_type = VMBUS_MSG_OFFERCHANNEL,
                .child_relid = chan->id,
                .connection_id = chan_connection_id(chan),
                .channel_flags = vdc->channel_flags,
                .mmio_size_mb = vdc->mmio_size_mb,
                .sub_channel_index = vmbus_channel_idx(chan),
                .interrupt_flags = VMBUS_OFFER_INTERRUPT_DEDICATED,
            };

            memcpy(msg.type_uuid, &classid, sizeof(classid));
            memcpy(msg.instance_uuid, &instanceid, sizeof(instanceid));

            trace_vmbus_send_offer(chan->id, chan->dev);

            post_msg(vmbus, &msg, sizeof(msg));
            return;
        }
    }

    /* no more offers, send terminator message */
    trace_vmbus_terminate_offers();
    post_msg(vmbus, &alloffers_msg, sizeof(alloffers_msg));
}

static bool complete_offer(VMBus *vmbus)
{
    VMBusChannel *chan;

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->offer_state == VMOFFER_SENDING) {
            chan->offer_state = VMOFFER_SENT;
            goto next_offer;
        }
    }
    /*
     * no transitioning channels found so this is completing the terminator
     * message, and vmbus can move to the next state
     */
    return true;

next_offer:
    /* try to mark another channel for offering */
    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->offer_state == VMOFFER_INIT) {
            chan->offer_state = VMOFFER_SENDING;
            break;
        }
    }
    /*
     * if an offer has been sent there are more offers or the terminator yet to
     * send, so no state transition for vmbus
     */
    return false;
}


static void handle_gpadl_header(VMBus *vmbus, vmbus_message_gpadl_header *msg,
                                uint32_t msglen)
{
    VMBusGpadl *gpadl;
    uint32_t num_gfns, i;

    /* must include at least one gpa range */
    if (msglen < sizeof(*msg) + sizeof(msg->range[0]) ||
        !vmbus_initialized(vmbus)) {
        return;
    }

    num_gfns = (msg->range_buflen - msg->rangecount * sizeof(msg->range[0])) /
               sizeof(msg->range[0].pfn_array[0]);

    trace_vmbus_gpadl_header(msg->gpadl_id, num_gfns);

    /*
     * In theory the GPADL_HEADER message can define a GPADL with multiple GPA
     * ranges each with arbitrary size and alignment.  However in practice only
     * single-range page-aligned GPADLs have been observed so just ignore
     * anything else and simplify things greatly.
     */
    if (msg->rangecount != 1 || msg->range[0].byte_offset ||
        (msg->range[0].byte_count != (num_gfns << TARGET_PAGE_BITS))) {
        return;
    }

    /* ignore requests to create already existing GPADLs */
    if (find_gpadl(vmbus, msg->gpadl_id)) {
        return;
    }

    gpadl = create_gpadl(vmbus, msg->gpadl_id, msg->child_relid, num_gfns);

    for (i = 0; i < num_gfns &&
         (void *)&msg->range[0].pfn_array[i + 1] <= (void *)msg + msglen;
         i++) {
        gpadl->gfns[gpadl->seen_gfns++] = msg->range[0].pfn_array[i];
    }

    if (gpadl_full(gpadl)) {
        vmbus->state = VMBUS_CREATE_GPADL;
    }
}

static void handle_gpadl_body(VMBus *vmbus, vmbus_message_gpadl_body *msg,
                              uint32_t msglen)
{
    VMBusGpadl *gpadl;
    uint32_t num_gfns_left, i;

    if (msglen < sizeof(*msg) || !vmbus_initialized(vmbus)) {
        return;
    }

    trace_vmbus_gpadl_body(msg->gpadl_id);

    gpadl = find_gpadl(vmbus, msg->gpadl_id);
    if (!gpadl) {
        return;
    }

    num_gfns_left = gpadl->num_gfns - gpadl->seen_gfns;
    assert(num_gfns_left);

    for (i = 0; i < num_gfns_left &&
         (void *)&msg->pfn_array[i + 1] <= (void *)msg + msglen; i++) {
        gpadl->gfns[gpadl->seen_gfns++] = msg->pfn_array[i];
    }

    if (gpadl_full(gpadl)) {
        vmbus->state = VMBUS_CREATE_GPADL;
    }
}

static void send_create_gpadl(VMBus *vmbus)
{
    VMBusGpadl *gpadl;

    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        if (gpadl_full(gpadl) && gpadl->state == VMGPADL_INIT) {
            struct vmbus_message_gpadl_created msg = {
                .header.message_type = VMBUS_MSG_GPADL_CREATED,
                .gpadl_id = gpadl->id,
                .child_relid = gpadl->child_relid,
            };

            trace_vmbus_gpadl_created(gpadl->id);
            post_msg(vmbus, &msg, sizeof(msg));
            return;
        }
    }

    assert(false);
}

static bool complete_create_gpadl(VMBus *vmbus)
{
    VMBusGpadl *gpadl;

    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        if (gpadl_full(gpadl) && gpadl->state == VMGPADL_INIT) {
            gpadl->state = VMGPADL_ALIVE;

            return true;
        }
    }

    assert(false);
    return false;
}

static void handle_gpadl_teardown(VMBus *vmbus,
                                  vmbus_message_gpadl_teardown *msg,
                                  uint32_t msglen)
{
    VMBusGpadl *gpadl;

    if (msglen < sizeof(*msg) || !vmbus_initialized(vmbus)) {
        return;
    }

    trace_vmbus_gpadl_teardown(msg->gpadl_id);

    gpadl = find_gpadl(vmbus, msg->gpadl_id);
    if (!gpadl || gpadl->state == VMGPADL_TORNDOWN) {
        return;
    }

    gpadl->state = VMGPADL_TEARINGDOWN;
    vmbus->state = VMBUS_TEARDOWN_GPADL;
}

static void send_teardown_gpadl(VMBus *vmbus)
{
    VMBusGpadl *gpadl;

    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        if (gpadl->state == VMGPADL_TEARINGDOWN) {
            struct vmbus_message_gpadl_torndown msg = {
                .header.message_type = VMBUS_MSG_GPADL_TORNDOWN,
                .gpadl_id = gpadl->id,
            };

            trace_vmbus_gpadl_torndown(gpadl->id);
            post_msg(vmbus, &msg, sizeof(msg));
            return;
        }
    }

    assert(false);
}

static bool complete_teardown_gpadl(VMBus *vmbus)
{
    VMBusGpadl *gpadl;

    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        if (gpadl->state == VMGPADL_TEARINGDOWN) {
            gpadl->state = VMGPADL_TORNDOWN;
            vmbus_put_gpadl(gpadl);
            return true;
        }
    }

    assert(false);
    return false;
}

static void handle_open_channel(VMBus *vmbus, vmbus_message_open_channel *msg,
                                uint32_t msglen)
{
    VMBusChannel *chan;

    if (msglen < sizeof(*msg) || !vmbus_initialized(vmbus)) {
        return;
    }

    trace_vmbus_open_channel(msg->child_relid, msg->ring_buffer_gpadl_id,
                             msg->target_vp);
    chan = find_channel(vmbus, msg->child_relid);
    if (!chan || chan->state != VMCHAN_INIT) {
        return;
    }

    chan->ringbuf_gpadl = msg->ring_buffer_gpadl_id;
    chan->ringbuf_send_offset = msg->ring_buffer_offset;
    chan->target_vp = msg->target_vp;
    chan->open_id = msg->open_id;

    open_channel(chan);

    chan->state = VMCHAN_OPENING;
    vmbus->state = VMBUS_OPEN_CHANNEL;
}

static void send_open_channel(VMBus *vmbus)
{
    VMBusChannel *chan;

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->state == VMCHAN_OPENING) {
            struct vmbus_message_open_result msg = {
                .header.message_type = VMBUS_MSG_OPENCHANNEL_RESULT,
                .child_relid = chan->id,
                .open_id = chan->open_id,
                .status = !vmbus_channel_is_open(chan),
            };

            trace_vmbus_channel_open(chan->id, msg.status);
            post_msg(vmbus, &msg, sizeof(msg));
            return;
        }
    }

    assert(false);
}

static bool complete_open_channel(VMBus *vmbus)
{
    VMBusChannel *chan;

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->state == VMCHAN_OPENING) {
            if (vmbus_channel_is_open(chan)) {
                chan->state = VMCHAN_OPEN;
                /*
                 * simulate guest notification of ringbuffer space made
                 * available, for the channel protocols where the host
                 * initiates the communication
                 */
                vmbus_channel_notify_host(chan);
            } else {
                chan->state = VMCHAN_INIT;
            }
            return true;
        }
    }

    assert(false);
    return false;
}

static void vdev_reset_on_close(VMBusDevice *vdev)
{
    uint16_t i;

    for (i = 0; i < vdev->num_channels; i++) {
        if (vmbus_channel_is_open(&vdev->channels[i])) {
            return;
        }
    }

    /* all channels closed -- reset device */
    qdev_reset_all(DEVICE(vdev));
}

static void handle_close_channel(VMBus *vmbus, vmbus_message_close_channel *msg,
                                 uint32_t msglen)
{
    VMBusChannel *chan;

    if (msglen < sizeof(*msg) || !vmbus_initialized(vmbus)) {
        return;
    }

    trace_vmbus_close_channel(msg->child_relid);

    chan = find_channel(vmbus, msg->child_relid);
    if (!chan) {
        return;
    }

    close_channel(chan);
    chan->state = VMCHAN_INIT;

    vdev_reset_on_close(chan->dev);
}

static void handle_unload(VMBus *vmbus, void *msg, uint32_t msglen)
{
    vmbus->state = VMBUS_UNLOAD;
}

static void send_unload(VMBus *vmbus)
{
    vmbus_message_header msg = {
        .message_type = VMBUS_MSG_UNLOAD_RESPONSE,
    };

    qemu_mutex_lock(&vmbus->rx_queue_lock);
    vmbus->rx_queue_size = 0;
    qemu_mutex_unlock(&vmbus->rx_queue_lock);

    post_msg(vmbus, &msg, sizeof(msg));
    return;
}

static bool complete_unload(VMBus *vmbus)
{
    vmbus_reset_all(vmbus);
    return true;
}

static void process_message(VMBus *vmbus)
{
    struct hyperv_post_message_input *hv_msg;
    struct vmbus_message_header *msg;
    void *msgdata;
    uint32_t msglen;

    qemu_mutex_lock(&vmbus->rx_queue_lock);

    if (!vmbus->rx_queue_size) {
        goto unlock;
    }

    hv_msg = &vmbus->rx_queue[vmbus->rx_queue_head];
    msglen =  hv_msg->payload_size;
    if (msglen < sizeof(*msg)) {
        goto out;
    }
    msgdata = hv_msg->payload;
    msg = (struct vmbus_message_header *)msgdata;

    trace_vmbus_process_incoming_message(msg->message_type);

    switch (msg->message_type) {
    case VMBUS_MSG_INITIATE_CONTACT:
        handle_initiate_contact(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_REQUESTOFFERS:
        handle_request_offers(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_GPADL_HEADER:
        handle_gpadl_header(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_GPADL_BODY:
        handle_gpadl_body(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_GPADL_TEARDOWN:
        handle_gpadl_teardown(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_OPENCHANNEL:
        handle_open_channel(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_CLOSECHANNEL:
        handle_close_channel(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_UNLOAD:
        handle_unload(vmbus, msgdata, msglen);
        break;
    default:
        error_report("unknown message type %#x", msg->message_type);
        break;
    }

out:
    vmbus->rx_queue_size--;
    vmbus->rx_queue_head++;
    vmbus->rx_queue_head %= HV_MSG_QUEUE_LEN;

    vmbus_resched(vmbus);
unlock:
    qemu_mutex_unlock(&vmbus->rx_queue_lock);
}

static const struct {
    void (*run)(VMBus *vmbus);
    bool (*complete)(VMBus *vmbus);
} state_runner[] = {
    [VMBUS_LISTEN]         = {process_message,     NULL},
    [VMBUS_HANDSHAKE]      = {send_handshake,      NULL},
    [VMBUS_OFFER]          = {send_offer,          complete_offer},
    [VMBUS_CREATE_GPADL]   = {send_create_gpadl,   complete_create_gpadl},
    [VMBUS_TEARDOWN_GPADL] = {send_teardown_gpadl, complete_teardown_gpadl},
    [VMBUS_OPEN_CHANNEL]   = {send_open_channel,   complete_open_channel},
    [VMBUS_UNLOAD]         = {send_unload,         complete_unload},
};

static void vmbus_do_run(VMBus *vmbus)
{
    if (vmbus->msg_in_progress) {
        return;
    }

    assert(vmbus->state < VMBUS_STATE_MAX);
    assert(state_runner[vmbus->state].run);
    state_runner[vmbus->state].run(vmbus);
}

static void vmbus_run(void *opaque)
{
    VMBus *vmbus = opaque;

    /* make sure no recursion happens (e.g. due to recursive aio_poll()) */
    if (vmbus->in_progress) {
        return;
    }

    vmbus->in_progress = true;
    /*
     * FIXME: if vmbus_resched() is called from within vmbus_do_run(), it
     * should go *after* the code that can result in aio_poll; otherwise
     * reschedules can be missed.  No idea how to enforce that.
     */
    vmbus_do_run(vmbus);
    vmbus->in_progress = false;
}

static void vmbus_msg_cb(void *data, int status)
{
    VMBus *vmbus = data;
    bool (*complete)(VMBus *vmbus);

    assert(vmbus->msg_in_progress);

    trace_vmbus_msg_cb(status);

    if (status == -EAGAIN) {
        goto out;
    }
    if (status) {
        error_report("message delivery fatal failure: %d; aborting vmbus",
                     status);
        vmbus_reset_all(vmbus);
        return;
    }

    assert(vmbus->state < VMBUS_STATE_MAX);
    complete = state_runner[vmbus->state].complete;
    if (!complete || complete(vmbus)) {
        vmbus->state = VMBUS_LISTEN;
    }
out:
    vmbus->msg_in_progress = false;
    vmbus_resched(vmbus);
}

static void vmbus_resched(VMBus *vmbus)
{
    aio_bh_schedule_oneshot(qemu_get_aio_context(), vmbus_run, vmbus);
}

static void vmbus_signal_event(EventNotifier *e)
{
    VMBusChannel *chan;
    VMBus *vmbus = container_of(e, VMBus, notifier);
    unsigned long *int_map;
    hwaddr addr, len;
    bool is_dirty = false;

    if (!event_notifier_test_and_clear(e)) {
        return;
    }

    trace_vmbus_signal_event();

    if (!vmbus->int_page_gpa) {
        return;
    }

    addr = vmbus->int_page_gpa + TARGET_PAGE_SIZE / 2;
    len = TARGET_PAGE_SIZE / 2;
    int_map = cpu_physical_memory_map(addr, &len, 1);
    if (len != TARGET_PAGE_SIZE / 2) {
        goto unmap;
    }

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (bitmap_test_and_clear_atomic(int_map, chan->id, 1)) {
            if (!vmbus_channel_is_open(chan)) {
                continue;
            }
            vmbus_channel_notify_host(chan);
            is_dirty = true;
        }
    }

unmap:
    cpu_physical_memory_unmap(int_map, len, 1, is_dirty);
}

static void vmbus_dev_realize(DeviceState *dev, Error **errp)
{
    VMBusDevice *vdev = VMBUS_DEVICE(dev);
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(vdev);
    VMBus *vmbus = VMBUS(qdev_get_parent_bus(dev));
    BusChild *child;
    Error *err = NULL;
    char idstr[UUID_FMT_LEN + 1];

    assert(!qemu_uuid_is_null(&vdev->instanceid));

    /* Check for instance id collision for this class id */
    QTAILQ_FOREACH(child, &BUS(vmbus)->children, sibling) {
        VMBusDevice *child_dev = VMBUS_DEVICE(child->child);

        if (child_dev == vdev) {
            continue;
        }

        if (qemu_uuid_is_equal(&child_dev->instanceid, &vdev->instanceid)) {
            qemu_uuid_unparse(&vdev->instanceid, idstr);
            error_setg(&err, "duplicate vmbus device instance id %s", idstr);
            goto error_out;
        }
    }

    vdev->dma_as = &address_space_memory;

    create_channels(vmbus, vdev, &err);
    if (err) {
        goto error_out;
    }

    if (vdc->vmdev_realize) {
        vdc->vmdev_realize(vdev, &err);
        if (err) {
            goto err_vdc_realize;
        }
    }
    return;

err_vdc_realize:
    free_channels(vdev);
error_out:
    error_propagate(errp, err);
}

static void vmbus_dev_reset(DeviceState *dev)
{
    uint16_t i;
    VMBusDevice *vdev = VMBUS_DEVICE(dev);
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(vdev);

    if (vdev->channels) {
        for (i = 0; i < vdev->num_channels; i++) {
            VMBusChannel *chan = &vdev->channels[i];
            close_channel(chan);
            chan->state = VMCHAN_INIT;
        }
    }

    if (vdc->vmdev_reset) {
        vdc->vmdev_reset(vdev);
    }
}

static void vmbus_dev_unrealize(DeviceState *dev)
{
    VMBusDevice *vdev = VMBUS_DEVICE(dev);
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(vdev);

    if (vdc->vmdev_unrealize) {
        vdc->vmdev_unrealize(vdev);
    }
    free_channels(vdev);
}

static void vmbus_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *kdev = DEVICE_CLASS(klass);
    kdev->bus_type = TYPE_VMBUS;
    kdev->realize = vmbus_dev_realize;
    kdev->unrealize = vmbus_dev_unrealize;
    kdev->reset = vmbus_dev_reset;
}

static Property vmbus_dev_instanceid =
                        DEFINE_PROP_UUID("instanceid", VMBusDevice, instanceid);

static void vmbus_dev_instance_init(Object *obj)
{
    VMBusDevice *vdev = VMBUS_DEVICE(obj);
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(vdev);

    if (!qemu_uuid_is_null(&vdc->instanceid)) {
        /* Class wants to only have a single instance with a fixed UUID */
        vdev->instanceid = vdc->instanceid;
    } else {
        qdev_property_add_static(DEVICE(vdev), &vmbus_dev_instanceid);
    }
}

const VMStateDescription vmstate_vmbus_dev = {
    .name = TYPE_VMBUS_DEVICE,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(instanceid.data, VMBusDevice, 16),
        VMSTATE_UINT16(num_channels, VMBusDevice),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT16(channels, VMBusDevice,
                                             num_channels, vmstate_channel,
                                             VMBusChannel),
        VMSTATE_END_OF_LIST()
    }
};

/* vmbus generic device base */
static const TypeInfo vmbus_dev_type_info = {
    .name = TYPE_VMBUS_DEVICE,
    .parent = TYPE_DEVICE,
    .abstract = true,
    .instance_size = sizeof(VMBusDevice),
    .class_size = sizeof(VMBusDeviceClass),
    .class_init = vmbus_dev_class_init,
    .instance_init = vmbus_dev_instance_init,
};

static void vmbus_realize(BusState *bus, Error **errp)
{
    int ret = 0;
    Error *local_err = NULL;
    VMBus *vmbus = VMBUS(bus);

    qemu_mutex_init(&vmbus->rx_queue_lock);

    QTAILQ_INIT(&vmbus->gpadl_list);
    QTAILQ_INIT(&vmbus->channel_list);

    ret = hyperv_set_msg_handler(VMBUS_MESSAGE_CONNECTION_ID,
                                 vmbus_recv_message, vmbus);
    if (ret != 0) {
        error_setg(&local_err, "hyperv set message handler failed: %d", ret);
        goto error_out;
    }

    ret = event_notifier_init(&vmbus->notifier, 0);
    if (ret != 0) {
        error_setg(&local_err, "event notifier failed to init with %d", ret);
        goto remove_msg_handler;
    }

    event_notifier_set_handler(&vmbus->notifier, vmbus_signal_event);
    ret = hyperv_set_event_flag_handler(VMBUS_EVENT_CONNECTION_ID,
                                        &vmbus->notifier);
    if (ret != 0) {
        error_setg(&local_err, "hyperv set event handler failed with %d", ret);
        goto clear_event_notifier;
    }

    return;

clear_event_notifier:
    event_notifier_cleanup(&vmbus->notifier);
remove_msg_handler:
    hyperv_set_msg_handler(VMBUS_MESSAGE_CONNECTION_ID, NULL, NULL);
error_out:
    qemu_mutex_destroy(&vmbus->rx_queue_lock);
    error_propagate(errp, local_err);
}

static void vmbus_unrealize(BusState *bus)
{
    VMBus *vmbus = VMBUS(bus);

    hyperv_set_msg_handler(VMBUS_MESSAGE_CONNECTION_ID, NULL, NULL);
    hyperv_set_event_flag_handler(VMBUS_EVENT_CONNECTION_ID, NULL);
    event_notifier_cleanup(&vmbus->notifier);

    qemu_mutex_destroy(&vmbus->rx_queue_lock);
}

static void vmbus_reset(BusState *bus)
{
    vmbus_deinit(VMBUS(bus));
}

static char *vmbus_get_dev_path(DeviceState *dev)
{
    BusState *bus = qdev_get_parent_bus(dev);
    return qdev_get_dev_path(bus->parent);
}

static char *vmbus_get_fw_dev_path(DeviceState *dev)
{
    VMBusDevice *vdev = VMBUS_DEVICE(dev);
    char uuid[UUID_FMT_LEN + 1];

    qemu_uuid_unparse(&vdev->instanceid, uuid);
    return g_strdup_printf("%s@%s", qdev_fw_name(dev), uuid);
}

static void vmbus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->get_dev_path = vmbus_get_dev_path;
    k->get_fw_dev_path = vmbus_get_fw_dev_path;
    k->realize = vmbus_realize;
    k->unrealize = vmbus_unrealize;
    k->reset = vmbus_reset;
}

static int vmbus_pre_load(void *opaque)
{
    VMBusChannel *chan;
    VMBus *vmbus = VMBUS(opaque);

    /*
     * channel IDs allocated by the source will come in the migration stream
     * for each channel, so clean up the ones allocated at realize
     */
    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        unregister_chan_id(chan);
    }

    return 0;
}
static int vmbus_post_load(void *opaque, int version_id)
{
    int ret;
    VMBus *vmbus = VMBUS(opaque);
    VMBusGpadl *gpadl;
    VMBusChannel *chan;

    ret = vmbus_init(vmbus);
    if (ret) {
        return ret;
    }

    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        gpadl->vmbus = vmbus;
        gpadl->refcount = 1;
    }

    /*
     * reopening channels depends on initialized vmbus so it's done here
     * instead of channel_post_load()
     */
    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {

        if (chan->state == VMCHAN_OPENING || chan->state == VMCHAN_OPEN) {
            open_channel(chan);
        }

        if (chan->state != VMCHAN_OPEN) {
            continue;
        }

        if (!vmbus_channel_is_open(chan)) {
            /* reopen failed, abort loading */
            return -1;
        }

        /* resume processing on the guest side if it missed the notification */
        hyperv_sint_route_set_sint(chan->notify_route);
        /* ditto on the host side */
        vmbus_channel_notify_host(chan);
    }

    vmbus_resched(vmbus);
    return 0;
}

static const VMStateDescription vmstate_post_message_input = {
    .name = "vmbus/hyperv_post_message_input",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        /*
         * skip connection_id and message_type as they are validated before
         * queueing and ignored on dequeueing
         */
        VMSTATE_UINT32(payload_size, struct hyperv_post_message_input),
        VMSTATE_UINT8_ARRAY(payload, struct hyperv_post_message_input,
                            HV_MESSAGE_PAYLOAD_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmbus_rx_queue_needed(void *opaque)
{
    VMBus *vmbus = VMBUS(opaque);
    return vmbus->rx_queue_size;
}

static const VMStateDescription vmstate_rx_queue = {
    .name = "vmbus/rx_queue",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = vmbus_rx_queue_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(rx_queue_head, VMBus),
        VMSTATE_UINT8(rx_queue_size, VMBus),
        VMSTATE_STRUCT_ARRAY(rx_queue, VMBus,
                             HV_MSG_QUEUE_LEN, 0,
                             vmstate_post_message_input,
                             struct hyperv_post_message_input),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vmbus = {
    .name = TYPE_VMBUS,
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_load = vmbus_pre_load,
    .post_load = vmbus_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(state, VMBus),
        VMSTATE_UINT32(version, VMBus),
        VMSTATE_UINT32(target_vp, VMBus),
        VMSTATE_UINT64(int_page_gpa, VMBus),
        VMSTATE_QTAILQ_V(gpadl_list, VMBus, 0,
                         vmstate_gpadl, VMBusGpadl, link),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_rx_queue,
        NULL
    }
};

static const TypeInfo vmbus_type_info = {
    .name = TYPE_VMBUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VMBus),
    .class_init = vmbus_class_init,
};

static void vmbus_bridge_realize(DeviceState *dev, Error **errp)
{
    VMBusBridge *bridge = VMBUS_BRIDGE(dev);

    /*
     * here there's at least one vmbus bridge that is being realized, so
     * vmbus_bridge_find can only return NULL if it's not unique
     */
    if (!vmbus_bridge_find()) {
        error_setg(errp, "there can be at most one %s in the system",
                   TYPE_VMBUS_BRIDGE);
        return;
    }

    if (!hyperv_is_synic_enabled()) {
        error_report("VMBus requires usable Hyper-V SynIC and VP_INDEX");
        return;
    }

    bridge->bus = VMBUS(qbus_create(TYPE_VMBUS, dev, "vmbus"));
}

static char *vmbus_bridge_ofw_unit_address(const SysBusDevice *dev)
{
    /* there can be only one VMBus */
    return g_strdup("0");
}

static const VMStateDescription vmstate_vmbus_bridge = {
    .name = TYPE_VMBUS_BRIDGE,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_POINTER(bus, VMBusBridge, vmstate_vmbus, VMBus),
        VMSTATE_END_OF_LIST()
    },
};

static Property vmbus_bridge_props[] = {
    DEFINE_PROP_UINT8("irq", VMBusBridge, irq, 7),
    DEFINE_PROP_END_OF_LIST()
};

static void vmbus_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    SysBusDeviceClass *sk = SYS_BUS_DEVICE_CLASS(klass);

    k->realize = vmbus_bridge_realize;
    k->fw_name = "vmbus";
    sk->explicit_ofw_unit_address = vmbus_bridge_ofw_unit_address;
    set_bit(DEVICE_CATEGORY_BRIDGE, k->categories);
    k->vmsd = &vmstate_vmbus_bridge;
    device_class_set_props(k, vmbus_bridge_props);
    /* override SysBusDevice's default */
    k->user_creatable = true;
}

static const TypeInfo vmbus_bridge_type_info = {
    .name = TYPE_VMBUS_BRIDGE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VMBusBridge),
    .class_init = vmbus_bridge_class_init,
};

static void vmbus_register_types(void)
{
    type_register_static(&vmbus_bridge_type_info);
    type_register_static(&vmbus_dev_type_info);
    type_register_static(&vmbus_type_info);
}

type_init(vmbus_register_types)
