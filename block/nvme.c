/*
 * NVMe block driver based on vfio
 *
 * Copyright 2016 - 2018 Red Hat, Inc.
 *
 * Authors:
 *   Fam Zheng <famz@redhat.com>
 *   Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "qemu/option.h"
#include "qemu/vfio-helpers.h"
#include "block/block_int.h"
#include "sysemu/replay.h"
#include "trace.h"

#include "block/nvme.h"

#define NVME_SQ_ENTRY_BYTES 64
#define NVME_CQ_ENTRY_BYTES 16
#define NVME_QUEUE_SIZE 128
#define NVME_BAR_SIZE 8192

typedef struct {
    int32_t  head, tail;
    uint8_t  *queue;
    uint64_t iova;
    /* Hardware MMIO register */
    volatile uint32_t *doorbell;
} NVMeQueue;

typedef struct {
    BlockCompletionFunc *cb;
    void *opaque;
    int cid;
    void *prp_list_page;
    uint64_t prp_list_iova;
    bool busy;
} NVMeRequest;

typedef struct {
    CoQueue     free_req_queue;
    QemuMutex   lock;

    /* Fields protected by BQL */
    int         index;
    uint8_t     *prp_list_pages;

    /* Fields protected by @lock */
    NVMeQueue   sq, cq;
    int         cq_phase;
    NVMeRequest reqs[NVME_QUEUE_SIZE];
    bool        busy;
    int         need_kick;
    int         inflight;
} NVMeQueuePair;

/* Memory mapped registers */
typedef volatile struct {
    uint64_t cap;
    uint32_t vs;
    uint32_t intms;
    uint32_t intmc;
    uint32_t cc;
    uint32_t reserved0;
    uint32_t csts;
    uint32_t nssr;
    uint32_t aqa;
    uint64_t asq;
    uint64_t acq;
    uint32_t cmbloc;
    uint32_t cmbsz;
    uint8_t  reserved1[0xec0];
    uint8_t  cmd_set_specfic[0x100];
    uint32_t doorbells[];
} NVMeRegs;

QEMU_BUILD_BUG_ON(offsetof(NVMeRegs, doorbells) != 0x1000);

typedef struct {
    AioContext *aio_context;
    QEMUVFIOState *vfio;
    NVMeRegs *regs;
    /* The submission/completion queue pairs.
     * [0]: admin queue.
     * [1..]: io queues.
     */
    NVMeQueuePair **queues;
    int nr_queues;
    size_t page_size;
    /* How many uint32_t elements does each doorbell entry take. */
    size_t doorbell_scale;
    bool write_cache_supported;
    EventNotifier irq_notifier;

    uint64_t nsze; /* Namespace size reported by identify command */
    int nsid;      /* The namespace id to read/write data. */
    int blkshift;

    uint64_t max_transfer;
    bool plugged;

    bool supports_write_zeroes;
    bool supports_discard;

    CoMutex dma_map_lock;
    CoQueue dma_flush_queue;

    /* Total size of mapped qiov, accessed under dma_map_lock */
    int dma_map_count;

    /* PCI address (required for nvme_refresh_filename()) */
    char *device;
} BDRVNVMeState;

#define NVME_BLOCK_OPT_DEVICE "device"
#define NVME_BLOCK_OPT_NAMESPACE "namespace"

static QemuOptsList runtime_opts = {
    .name = "nvme",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = NVME_BLOCK_OPT_DEVICE,
            .type = QEMU_OPT_STRING,
            .help = "NVMe PCI device address",
        },
        {
            .name = NVME_BLOCK_OPT_NAMESPACE,
            .type = QEMU_OPT_NUMBER,
            .help = "NVMe namespace",
        },
        { /* end of list */ }
    },
};

static void nvme_init_queue(BlockDriverState *bs, NVMeQueue *q,
                            int nentries, int entry_bytes, Error **errp)
{
    BDRVNVMeState *s = bs->opaque;
    size_t bytes;
    int r;

    bytes = ROUND_UP(nentries * entry_bytes, s->page_size);
    q->head = q->tail = 0;
    q->queue = qemu_try_blockalign0(bs, bytes);

    if (!q->queue) {
        error_setg(errp, "Cannot allocate queue");
        return;
    }
    r = qemu_vfio_dma_map(s->vfio, q->queue, bytes, false, &q->iova);
    if (r) {
        error_setg(errp, "Cannot map queue");
    }
}

static void nvme_free_queue_pair(BlockDriverState *bs, NVMeQueuePair *q)
{
    qemu_vfree(q->prp_list_pages);
    qemu_vfree(q->sq.queue);
    qemu_vfree(q->cq.queue);
    qemu_mutex_destroy(&q->lock);
    g_free(q);
}

static void nvme_free_req_queue_cb(void *opaque)
{
    NVMeQueuePair *q = opaque;

    qemu_mutex_lock(&q->lock);
    while (qemu_co_enter_next(&q->free_req_queue, &q->lock)) {
        /* Retry all pending requests */
    }
    qemu_mutex_unlock(&q->lock);
}

static NVMeQueuePair *nvme_create_queue_pair(BlockDriverState *bs,
                                             int idx, int size,
                                             Error **errp)
{
    int i, r;
    BDRVNVMeState *s = bs->opaque;
    Error *local_err = NULL;
    NVMeQueuePair *q = g_new0(NVMeQueuePair, 1);
    uint64_t prp_list_iova;

    qemu_mutex_init(&q->lock);
    q->index = idx;
    qemu_co_queue_init(&q->free_req_queue);
    q->prp_list_pages = qemu_blockalign0(bs, s->page_size * NVME_QUEUE_SIZE);
    r = qemu_vfio_dma_map(s->vfio, q->prp_list_pages,
                          s->page_size * NVME_QUEUE_SIZE,
                          false, &prp_list_iova);
    if (r) {
        goto fail;
    }
    for (i = 0; i < NVME_QUEUE_SIZE; i++) {
        NVMeRequest *req = &q->reqs[i];
        req->cid = i + 1;
        req->prp_list_page = q->prp_list_pages + i * s->page_size;
        req->prp_list_iova = prp_list_iova + i * s->page_size;
    }
    nvme_init_queue(bs, &q->sq, size, NVME_SQ_ENTRY_BYTES, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto fail;
    }
    q->sq.doorbell = &s->regs->doorbells[idx * 2 * s->doorbell_scale];

    nvme_init_queue(bs, &q->cq, size, NVME_CQ_ENTRY_BYTES, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto fail;
    }
    q->cq.doorbell = &s->regs->doorbells[(idx * 2 + 1) * s->doorbell_scale];

    return q;
fail:
    nvme_free_queue_pair(bs, q);
    return NULL;
}

/* With q->lock */
static void nvme_kick(BDRVNVMeState *s, NVMeQueuePair *q)
{
    if (s->plugged || !q->need_kick) {
        return;
    }
    trace_nvme_kick(s, q->index);
    assert(!(q->sq.tail & 0xFF00));
    /* Fence the write to submission queue entry before notifying the device. */
    smp_wmb();
    *q->sq.doorbell = cpu_to_le32(q->sq.tail);
    q->inflight += q->need_kick;
    q->need_kick = 0;
}

/* Find a free request element if any, otherwise:
 * a) if in coroutine context, try to wait for one to become available;
 * b) if not in coroutine, return NULL;
 */
static NVMeRequest *nvme_get_free_req(NVMeQueuePair *q)
{
    int i;
    NVMeRequest *req = NULL;

    qemu_mutex_lock(&q->lock);
    while (q->inflight + q->need_kick > NVME_QUEUE_SIZE - 2) {
        /* We have to leave one slot empty as that is the full queue case (head
         * == tail + 1). */
        if (qemu_in_coroutine()) {
            trace_nvme_free_req_queue_wait(q);
            qemu_co_queue_wait(&q->free_req_queue, &q->lock);
        } else {
            qemu_mutex_unlock(&q->lock);
            return NULL;
        }
    }
    for (i = 0; i < NVME_QUEUE_SIZE; i++) {
        if (!q->reqs[i].busy) {
            q->reqs[i].busy = true;
            req = &q->reqs[i];
            break;
        }
    }
    /* We have checked inflight and need_kick while holding q->lock, so one
     * free req must be available. */
    assert(req);
    qemu_mutex_unlock(&q->lock);
    return req;
}

static inline int nvme_translate_error(const NvmeCqe *c)
{
    uint16_t status = (le16_to_cpu(c->status) >> 1) & 0xFF;
    if (status) {
        trace_nvme_error(le32_to_cpu(c->result),
                         le16_to_cpu(c->sq_head),
                         le16_to_cpu(c->sq_id),
                         le16_to_cpu(c->cid),
                         le16_to_cpu(status));
    }
    switch (status) {
    case 0:
        return 0;
    case 1:
        return -ENOSYS;
    case 2:
        return -EINVAL;
    default:
        return -EIO;
    }
}

/* With q->lock */
static bool nvme_process_completion(BDRVNVMeState *s, NVMeQueuePair *q)
{
    bool progress = false;
    NVMeRequest *preq;
    NVMeRequest req;
    NvmeCqe *c;

    trace_nvme_process_completion(s, q->index, q->inflight);
    if (q->busy || s->plugged) {
        trace_nvme_process_completion_queue_busy(s, q->index);
        return false;
    }
    q->busy = true;
    assert(q->inflight >= 0);
    while (q->inflight) {
        int16_t cid;
        c = (NvmeCqe *)&q->cq.queue[q->cq.head * NVME_CQ_ENTRY_BYTES];
        if ((le16_to_cpu(c->status) & 0x1) == q->cq_phase) {
            break;
        }
        q->cq.head = (q->cq.head + 1) % NVME_QUEUE_SIZE;
        if (!q->cq.head) {
            q->cq_phase = !q->cq_phase;
        }
        cid = le16_to_cpu(c->cid);
        if (cid == 0 || cid > NVME_QUEUE_SIZE) {
            fprintf(stderr, "Unexpected CID in completion queue: %" PRIu32 "\n",
                    cid);
            continue;
        }
        assert(cid <= NVME_QUEUE_SIZE);
        trace_nvme_complete_command(s, q->index, cid);
        preq = &q->reqs[cid - 1];
        req = *preq;
        assert(req.cid == cid);
        assert(req.cb);
        preq->busy = false;
        preq->cb = preq->opaque = NULL;
        qemu_mutex_unlock(&q->lock);
        req.cb(req.opaque, nvme_translate_error(c));
        qemu_mutex_lock(&q->lock);
        q->inflight--;
        progress = true;
    }
    if (progress) {
        /* Notify the device so it can post more completions. */
        smp_mb_release();
        *q->cq.doorbell = cpu_to_le32(q->cq.head);
        if (!qemu_co_queue_empty(&q->free_req_queue)) {
            replay_bh_schedule_oneshot_event(s->aio_context,
                                             nvme_free_req_queue_cb, q);
        }
    }
    q->busy = false;
    return progress;
}

static void nvme_trace_command(const NvmeCmd *cmd)
{
    int i;

    for (i = 0; i < 8; ++i) {
        uint8_t *cmdp = (uint8_t *)cmd + i * 8;
        trace_nvme_submit_command_raw(cmdp[0], cmdp[1], cmdp[2], cmdp[3],
                                      cmdp[4], cmdp[5], cmdp[6], cmdp[7]);
    }
}

static void nvme_submit_command(BDRVNVMeState *s, NVMeQueuePair *q,
                                NVMeRequest *req,
                                NvmeCmd *cmd, BlockCompletionFunc cb,
                                void *opaque)
{
    assert(!req->cb);
    req->cb = cb;
    req->opaque = opaque;
    cmd->cid = cpu_to_le32(req->cid);

    trace_nvme_submit_command(s, q->index, req->cid);
    nvme_trace_command(cmd);
    qemu_mutex_lock(&q->lock);
    memcpy((uint8_t *)q->sq.queue +
           q->sq.tail * NVME_SQ_ENTRY_BYTES, cmd, sizeof(*cmd));
    q->sq.tail = (q->sq.tail + 1) % NVME_QUEUE_SIZE;
    q->need_kick++;
    nvme_kick(s, q);
    nvme_process_completion(s, q);
    qemu_mutex_unlock(&q->lock);
}

static void nvme_cmd_sync_cb(void *opaque, int ret)
{
    int *pret = opaque;
    *pret = ret;
    aio_wait_kick();
}

static int nvme_cmd_sync(BlockDriverState *bs, NVMeQueuePair *q,
                         NvmeCmd *cmd)
{
    NVMeRequest *req;
    BDRVNVMeState *s = bs->opaque;
    int ret = -EINPROGRESS;
    req = nvme_get_free_req(q);
    if (!req) {
        return -EBUSY;
    }
    nvme_submit_command(s, q, req, cmd, nvme_cmd_sync_cb, &ret);

    BDRV_POLL_WHILE(bs, ret == -EINPROGRESS);
    return ret;
}

static void nvme_identify(BlockDriverState *bs, int namespace, Error **errp)
{
    BDRVNVMeState *s = bs->opaque;
    NvmeIdCtrl *idctrl;
    NvmeIdNs *idns;
    NvmeLBAF *lbaf;
    uint8_t *resp;
    uint16_t oncs;
    int r;
    uint64_t iova;
    NvmeCmd cmd = {
        .opcode = NVME_ADM_CMD_IDENTIFY,
        .cdw10 = cpu_to_le32(0x1),
    };

    resp = qemu_try_blockalign0(bs, sizeof(NvmeIdCtrl));
    if (!resp) {
        error_setg(errp, "Cannot allocate buffer for identify response");
        goto out;
    }
    idctrl = (NvmeIdCtrl *)resp;
    idns = (NvmeIdNs *)resp;
    r = qemu_vfio_dma_map(s->vfio, resp, sizeof(NvmeIdCtrl), true, &iova);
    if (r) {
        error_setg(errp, "Cannot map buffer for DMA");
        goto out;
    }
    cmd.prp1 = cpu_to_le64(iova);

    if (nvme_cmd_sync(bs, s->queues[0], &cmd)) {
        error_setg(errp, "Failed to identify controller");
        goto out;
    }

    if (le32_to_cpu(idctrl->nn) < namespace) {
        error_setg(errp, "Invalid namespace");
        goto out;
    }
    s->write_cache_supported = le32_to_cpu(idctrl->vwc) & 0x1;
    s->max_transfer = (idctrl->mdts ? 1 << idctrl->mdts : 0) * s->page_size;
    /* For now the page list buffer per command is one page, to hold at most
     * s->page_size / sizeof(uint64_t) entries. */
    s->max_transfer = MIN_NON_ZERO(s->max_transfer,
                          s->page_size / sizeof(uint64_t) * s->page_size);

    oncs = le16_to_cpu(idctrl->oncs);
    s->supports_write_zeroes = !!(oncs & NVME_ONCS_WRITE_ZEROS);
    s->supports_discard = !!(oncs & NVME_ONCS_DSM);

    memset(resp, 0, 4096);

    cmd.cdw10 = 0;
    cmd.nsid = cpu_to_le32(namespace);
    if (nvme_cmd_sync(bs, s->queues[0], &cmd)) {
        error_setg(errp, "Failed to identify namespace");
        goto out;
    }

    s->nsze = le64_to_cpu(idns->nsze);
    lbaf = &idns->lbaf[NVME_ID_NS_FLBAS_INDEX(idns->flbas)];

    if (NVME_ID_NS_DLFEAT_WRITE_ZEROES(idns->dlfeat) &&
            NVME_ID_NS_DLFEAT_READ_BEHAVIOR(idns->dlfeat) ==
                    NVME_ID_NS_DLFEAT_READ_BEHAVIOR_ZEROES) {
        bs->supported_write_flags |= BDRV_REQ_MAY_UNMAP;
    }

    if (lbaf->ms) {
        error_setg(errp, "Namespaces with metadata are not yet supported");
        goto out;
    }

    if (lbaf->ds < BDRV_SECTOR_BITS || lbaf->ds > 12 ||
        (1 << lbaf->ds) > s->page_size)
    {
        error_setg(errp, "Namespace has unsupported block size (2^%d)",
                   lbaf->ds);
        goto out;
    }

    s->blkshift = lbaf->ds;
out:
    qemu_vfio_dma_unmap(s->vfio, resp);
    qemu_vfree(resp);
}

static bool nvme_poll_queues(BDRVNVMeState *s)
{
    bool progress = false;
    int i;

    for (i = 0; i < s->nr_queues; i++) {
        NVMeQueuePair *q = s->queues[i];
        qemu_mutex_lock(&q->lock);
        while (nvme_process_completion(s, q)) {
            /* Keep polling */
            progress = true;
        }
        qemu_mutex_unlock(&q->lock);
    }
    return progress;
}

static void nvme_handle_event(EventNotifier *n)
{
    BDRVNVMeState *s = container_of(n, BDRVNVMeState, irq_notifier);

    trace_nvme_handle_event(s);
    event_notifier_test_and_clear(n);
    nvme_poll_queues(s);
}

static bool nvme_add_io_queue(BlockDriverState *bs, Error **errp)
{
    BDRVNVMeState *s = bs->opaque;
    int n = s->nr_queues;
    NVMeQueuePair *q;
    NvmeCmd cmd;
    int queue_size = NVME_QUEUE_SIZE;

    q = nvme_create_queue_pair(bs, n, queue_size, errp);
    if (!q) {
        return false;
    }
    cmd = (NvmeCmd) {
        .opcode = NVME_ADM_CMD_CREATE_CQ,
        .prp1 = cpu_to_le64(q->cq.iova),
        .cdw10 = cpu_to_le32(((queue_size - 1) << 16) | (n & 0xFFFF)),
        .cdw11 = cpu_to_le32(0x3),
    };
    if (nvme_cmd_sync(bs, s->queues[0], &cmd)) {
        error_setg(errp, "Failed to create io queue [%d]", n);
        nvme_free_queue_pair(bs, q);
        return false;
    }
    cmd = (NvmeCmd) {
        .opcode = NVME_ADM_CMD_CREATE_SQ,
        .prp1 = cpu_to_le64(q->sq.iova),
        .cdw10 = cpu_to_le32(((queue_size - 1) << 16) | (n & 0xFFFF)),
        .cdw11 = cpu_to_le32(0x1 | (n << 16)),
    };
    if (nvme_cmd_sync(bs, s->queues[0], &cmd)) {
        error_setg(errp, "Failed to create io queue [%d]", n);
        nvme_free_queue_pair(bs, q);
        return false;
    }
    s->queues = g_renew(NVMeQueuePair *, s->queues, n + 1);
    s->queues[n] = q;
    s->nr_queues++;
    return true;
}

static bool nvme_poll_cb(void *opaque)
{
    EventNotifier *e = opaque;
    BDRVNVMeState *s = container_of(e, BDRVNVMeState, irq_notifier);
    bool progress = false;

    trace_nvme_poll_cb(s);
    progress = nvme_poll_queues(s);
    return progress;
}

static int nvme_init(BlockDriverState *bs, const char *device, int namespace,
                     Error **errp)
{
    BDRVNVMeState *s = bs->opaque;
    int ret;
    uint64_t cap;
    uint64_t timeout_ms;
    uint64_t deadline, now;
    Error *local_err = NULL;

    qemu_co_mutex_init(&s->dma_map_lock);
    qemu_co_queue_init(&s->dma_flush_queue);
    s->device = g_strdup(device);
    s->nsid = namespace;
    s->aio_context = bdrv_get_aio_context(bs);
    ret = event_notifier_init(&s->irq_notifier, 0);
    if (ret) {
        error_setg(errp, "Failed to init event notifier");
        return ret;
    }

    s->vfio = qemu_vfio_open_pci(device, errp);
    if (!s->vfio) {
        ret = -EINVAL;
        goto out;
    }

    s->regs = qemu_vfio_pci_map_bar(s->vfio, 0, 0, NVME_BAR_SIZE, errp);
    if (!s->regs) {
        ret = -EINVAL;
        goto out;
    }

    /* Perform initialize sequence as described in NVMe spec "7.6.1
     * Initialization". */

    cap = le64_to_cpu(s->regs->cap);
    if (!(cap & (1ULL << 37))) {
        error_setg(errp, "Device doesn't support NVMe command set");
        ret = -EINVAL;
        goto out;
    }

    s->page_size = MAX(4096, 1 << (12 + ((cap >> 48) & 0xF)));
    s->doorbell_scale = (4 << (((cap >> 32) & 0xF))) / sizeof(uint32_t);
    bs->bl.opt_mem_alignment = s->page_size;
    timeout_ms = MIN(500 * ((cap >> 24) & 0xFF), 30000);

    /* Reset device to get a clean state. */
    s->regs->cc = cpu_to_le32(le32_to_cpu(s->regs->cc) & 0xFE);
    /* Wait for CSTS.RDY = 0. */
    deadline = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + timeout_ms * 1000000ULL;
    while (le32_to_cpu(s->regs->csts) & 0x1) {
        if (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) > deadline) {
            error_setg(errp, "Timeout while waiting for device to reset (%"
                             PRId64 " ms)",
                       timeout_ms);
            ret = -ETIMEDOUT;
            goto out;
        }
    }

    /* Set up admin queue. */
    s->queues = g_new(NVMeQueuePair *, 1);
    s->queues[0] = nvme_create_queue_pair(bs, 0, NVME_QUEUE_SIZE, errp);
    if (!s->queues[0]) {
        ret = -EINVAL;
        goto out;
    }
    s->nr_queues = 1;
    QEMU_BUILD_BUG_ON(NVME_QUEUE_SIZE & 0xF000);
    s->regs->aqa = cpu_to_le32((NVME_QUEUE_SIZE << 16) | NVME_QUEUE_SIZE);
    s->regs->asq = cpu_to_le64(s->queues[0]->sq.iova);
    s->regs->acq = cpu_to_le64(s->queues[0]->cq.iova);

    /* After setting up all control registers we can enable device now. */
    s->regs->cc = cpu_to_le32((ctz32(NVME_CQ_ENTRY_BYTES) << 20) |
                              (ctz32(NVME_SQ_ENTRY_BYTES) << 16) |
                              0x1);
    /* Wait for CSTS.RDY = 1. */
    now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    deadline = now + timeout_ms * 1000000;
    while (!(le32_to_cpu(s->regs->csts) & 0x1)) {
        if (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) > deadline) {
            error_setg(errp, "Timeout while waiting for device to start (%"
                             PRId64 " ms)",
                       timeout_ms);
            ret = -ETIMEDOUT;
            goto out;
        }
    }

    ret = qemu_vfio_pci_init_irq(s->vfio, &s->irq_notifier,
                                 VFIO_PCI_MSIX_IRQ_INDEX, errp);
    if (ret) {
        goto out;
    }
    aio_set_event_notifier(bdrv_get_aio_context(bs), &s->irq_notifier,
                           false, nvme_handle_event, nvme_poll_cb);

    nvme_identify(bs, namespace, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EIO;
        goto out;
    }

    /* Set up command queues. */
    if (!nvme_add_io_queue(bs, errp)) {
        ret = -EIO;
    }
out:
    /* Cleaning up is done in nvme_file_open() upon error. */
    return ret;
}

/* Parse a filename in the format of nvme://XXXX:XX:XX.X/X. Example:
 *
 *     nvme://0000:44:00.0/1
 *
 * where the "nvme://" is a fixed form of the protocol prefix, the middle part
 * is the PCI address, and the last part is the namespace number starting from
 * 1 according to the NVMe spec. */
static void nvme_parse_filename(const char *filename, QDict *options,
                                Error **errp)
{
    int pref = strlen("nvme://");

    if (strlen(filename) > pref && !strncmp(filename, "nvme://", pref)) {
        const char *tmp = filename + pref;
        char *device;
        const char *namespace;
        unsigned long ns;
        const char *slash = strchr(tmp, '/');
        if (!slash) {
            qdict_put_str(options, NVME_BLOCK_OPT_DEVICE, tmp);
            return;
        }
        device = g_strndup(tmp, slash - tmp);
        qdict_put_str(options, NVME_BLOCK_OPT_DEVICE, device);
        g_free(device);
        namespace = slash + 1;
        if (*namespace && qemu_strtoul(namespace, NULL, 10, &ns)) {
            error_setg(errp, "Invalid namespace '%s', positive number expected",
                       namespace);
            return;
        }
        qdict_put_str(options, NVME_BLOCK_OPT_NAMESPACE,
                      *namespace ? namespace : "1");
    }
}

static int nvme_enable_disable_write_cache(BlockDriverState *bs, bool enable,
                                           Error **errp)
{
    int ret;
    BDRVNVMeState *s = bs->opaque;
    NvmeCmd cmd = {
        .opcode = NVME_ADM_CMD_SET_FEATURES,
        .nsid = cpu_to_le32(s->nsid),
        .cdw10 = cpu_to_le32(0x06),
        .cdw11 = cpu_to_le32(enable ? 0x01 : 0x00),
    };

    ret = nvme_cmd_sync(bs, s->queues[0], &cmd);
    if (ret) {
        error_setg(errp, "Failed to configure NVMe write cache");
    }
    return ret;
}

static void nvme_close(BlockDriverState *bs)
{
    int i;
    BDRVNVMeState *s = bs->opaque;

    for (i = 0; i < s->nr_queues; ++i) {
        nvme_free_queue_pair(bs, s->queues[i]);
    }
    g_free(s->queues);
    aio_set_event_notifier(bdrv_get_aio_context(bs), &s->irq_notifier,
                           false, NULL, NULL);
    event_notifier_cleanup(&s->irq_notifier);
    qemu_vfio_pci_unmap_bar(s->vfio, 0, (void *)s->regs, 0, NVME_BAR_SIZE);
    qemu_vfio_close(s->vfio);

    g_free(s->device);
}

static int nvme_file_open(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp)
{
    const char *device;
    QemuOpts *opts;
    int namespace;
    int ret;
    BDRVNVMeState *s = bs->opaque;

    bs->supported_write_flags = BDRV_REQ_FUA;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &error_abort);
    device = qemu_opt_get(opts, NVME_BLOCK_OPT_DEVICE);
    if (!device) {
        error_setg(errp, "'" NVME_BLOCK_OPT_DEVICE "' option is required");
        qemu_opts_del(opts);
        return -EINVAL;
    }

    namespace = qemu_opt_get_number(opts, NVME_BLOCK_OPT_NAMESPACE, 1);
    ret = nvme_init(bs, device, namespace, errp);
    qemu_opts_del(opts);
    if (ret) {
        goto fail;
    }
    if (flags & BDRV_O_NOCACHE) {
        if (!s->write_cache_supported) {
            error_setg(errp,
                       "NVMe controller doesn't support write cache configuration");
            ret = -EINVAL;
        } else {
            ret = nvme_enable_disable_write_cache(bs, !(flags & BDRV_O_NOCACHE),
                                                  errp);
        }
        if (ret) {
            goto fail;
        }
    }
    return 0;
fail:
    nvme_close(bs);
    return ret;
}

static int64_t nvme_getlength(BlockDriverState *bs)
{
    BDRVNVMeState *s = bs->opaque;
    return s->nsze << s->blkshift;
}

static uint32_t nvme_get_blocksize(BlockDriverState *bs)
{
    BDRVNVMeState *s = bs->opaque;
    assert(s->blkshift >= BDRV_SECTOR_BITS && s->blkshift <= 12);
    return UINT32_C(1) << s->blkshift;
}

static int nvme_probe_blocksizes(BlockDriverState *bs, BlockSizes *bsz)
{
    uint32_t blocksize = nvme_get_blocksize(bs);
    bsz->phys = blocksize;
    bsz->log = blocksize;
    return 0;
}

/* Called with s->dma_map_lock */
static coroutine_fn int nvme_cmd_unmap_qiov(BlockDriverState *bs,
                                            QEMUIOVector *qiov)
{
    int r = 0;
    BDRVNVMeState *s = bs->opaque;

    s->dma_map_count -= qiov->size;
    if (!s->dma_map_count && !qemu_co_queue_empty(&s->dma_flush_queue)) {
        r = qemu_vfio_dma_reset_temporary(s->vfio);
        if (!r) {
            qemu_co_queue_restart_all(&s->dma_flush_queue);
        }
    }
    return r;
}

/* Called with s->dma_map_lock */
static coroutine_fn int nvme_cmd_map_qiov(BlockDriverState *bs, NvmeCmd *cmd,
                                          NVMeRequest *req, QEMUIOVector *qiov)
{
    BDRVNVMeState *s = bs->opaque;
    uint64_t *pagelist = req->prp_list_page;
    int i, j, r;
    int entries = 0;

    assert(qiov->size);
    assert(QEMU_IS_ALIGNED(qiov->size, s->page_size));
    assert(qiov->size / s->page_size <= s->page_size / sizeof(uint64_t));
    for (i = 0; i < qiov->niov; ++i) {
        bool retry = true;
        uint64_t iova;
try_map:
        r = qemu_vfio_dma_map(s->vfio,
                              qiov->iov[i].iov_base,
                              qiov->iov[i].iov_len,
                              true, &iova);
        if (r == -ENOMEM && retry) {
            retry = false;
            trace_nvme_dma_flush_queue_wait(s);
            if (s->dma_map_count) {
                trace_nvme_dma_map_flush(s);
                qemu_co_queue_wait(&s->dma_flush_queue, &s->dma_map_lock);
            } else {
                r = qemu_vfio_dma_reset_temporary(s->vfio);
                if (r) {
                    goto fail;
                }
            }
            goto try_map;
        }
        if (r) {
            goto fail;
        }

        for (j = 0; j < qiov->iov[i].iov_len / s->page_size; j++) {
            pagelist[entries++] = cpu_to_le64(iova + j * s->page_size);
        }
        trace_nvme_cmd_map_qiov_iov(s, i, qiov->iov[i].iov_base,
                                    qiov->iov[i].iov_len / s->page_size);
    }

    s->dma_map_count += qiov->size;

    assert(entries <= s->page_size / sizeof(uint64_t));
    switch (entries) {
    case 0:
        abort();
    case 1:
        cmd->prp1 = pagelist[0];
        cmd->prp2 = 0;
        break;
    case 2:
        cmd->prp1 = pagelist[0];
        cmd->prp2 = pagelist[1];
        break;
    default:
        cmd->prp1 = pagelist[0];
        cmd->prp2 = cpu_to_le64(req->prp_list_iova + sizeof(uint64_t));
        break;
    }
    trace_nvme_cmd_map_qiov(s, cmd, req, qiov, entries);
    for (i = 0; i < entries; ++i) {
        trace_nvme_cmd_map_qiov_pages(s, i, pagelist[i]);
    }
    return 0;
fail:
    /* No need to unmap [0 - i) iovs even if we've failed, since we don't
     * increment s->dma_map_count. This is okay for fixed mapping memory areas
     * because they are already mapped before calling this function; for
     * temporary mappings, a later nvme_cmd_(un)map_qiov will reclaim by
     * calling qemu_vfio_dma_reset_temporary when necessary. */
    return r;
}

typedef struct {
    Coroutine *co;
    int ret;
    AioContext *ctx;
} NVMeCoData;

static void nvme_rw_cb_bh(void *opaque)
{
    NVMeCoData *data = opaque;
    qemu_coroutine_enter(data->co);
}

static void nvme_rw_cb(void *opaque, int ret)
{
    NVMeCoData *data = opaque;
    data->ret = ret;
    if (!data->co) {
        /* The rw coroutine hasn't yielded, don't try to enter. */
        return;
    }
    replay_bh_schedule_oneshot_event(data->ctx, nvme_rw_cb_bh, data);
}

static coroutine_fn int nvme_co_prw_aligned(BlockDriverState *bs,
                                            uint64_t offset, uint64_t bytes,
                                            QEMUIOVector *qiov,
                                            bool is_write,
                                            int flags)
{
    int r;
    BDRVNVMeState *s = bs->opaque;
    NVMeQueuePair *ioq = s->queues[1];
    NVMeRequest *req;

    uint32_t cdw12 = (((bytes >> s->blkshift) - 1) & 0xFFFF) |
                       (flags & BDRV_REQ_FUA ? 1 << 30 : 0);
    NvmeCmd cmd = {
        .opcode = is_write ? NVME_CMD_WRITE : NVME_CMD_READ,
        .nsid = cpu_to_le32(s->nsid),
        .cdw10 = cpu_to_le32((offset >> s->blkshift) & 0xFFFFFFFF),
        .cdw11 = cpu_to_le32(((offset >> s->blkshift) >> 32) & 0xFFFFFFFF),
        .cdw12 = cpu_to_le32(cdw12),
    };
    NVMeCoData data = {
        .ctx = bdrv_get_aio_context(bs),
        .ret = -EINPROGRESS,
    };

    trace_nvme_prw_aligned(s, is_write, offset, bytes, flags, qiov->niov);
    assert(s->nr_queues > 1);
    req = nvme_get_free_req(ioq);
    assert(req);

    qemu_co_mutex_lock(&s->dma_map_lock);
    r = nvme_cmd_map_qiov(bs, &cmd, req, qiov);
    qemu_co_mutex_unlock(&s->dma_map_lock);
    if (r) {
        req->busy = false;
        return r;
    }
    nvme_submit_command(s, ioq, req, &cmd, nvme_rw_cb, &data);

    data.co = qemu_coroutine_self();
    while (data.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }

    qemu_co_mutex_lock(&s->dma_map_lock);
    r = nvme_cmd_unmap_qiov(bs, qiov);
    qemu_co_mutex_unlock(&s->dma_map_lock);
    if (r) {
        return r;
    }

    trace_nvme_rw_done(s, is_write, offset, bytes, data.ret);
    return data.ret;
}

static inline bool nvme_qiov_aligned(BlockDriverState *bs,
                                     const QEMUIOVector *qiov)
{
    int i;
    BDRVNVMeState *s = bs->opaque;

    for (i = 0; i < qiov->niov; ++i) {
        if (!QEMU_PTR_IS_ALIGNED(qiov->iov[i].iov_base, s->page_size) ||
            !QEMU_IS_ALIGNED(qiov->iov[i].iov_len, s->page_size)) {
            trace_nvme_qiov_unaligned(qiov, i, qiov->iov[i].iov_base,
                                      qiov->iov[i].iov_len, s->page_size);
            return false;
        }
    }
    return true;
}

static int nvme_co_prw(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
                       QEMUIOVector *qiov, bool is_write, int flags)
{
    BDRVNVMeState *s = bs->opaque;
    int r;
    uint8_t *buf = NULL;
    QEMUIOVector local_qiov;

    assert(QEMU_IS_ALIGNED(offset, s->page_size));
    assert(QEMU_IS_ALIGNED(bytes, s->page_size));
    assert(bytes <= s->max_transfer);
    if (nvme_qiov_aligned(bs, qiov)) {
        return nvme_co_prw_aligned(bs, offset, bytes, qiov, is_write, flags);
    }
    trace_nvme_prw_buffered(s, offset, bytes, qiov->niov, is_write);
    buf = qemu_try_blockalign(bs, bytes);

    if (!buf) {
        return -ENOMEM;
    }
    qemu_iovec_init(&local_qiov, 1);
    if (is_write) {
        qemu_iovec_to_buf(qiov, 0, buf, bytes);
    }
    qemu_iovec_add(&local_qiov, buf, bytes);
    r = nvme_co_prw_aligned(bs, offset, bytes, &local_qiov, is_write, flags);
    qemu_iovec_destroy(&local_qiov);
    if (!r && !is_write) {
        qemu_iovec_from_buf(qiov, 0, buf, bytes);
    }
    qemu_vfree(buf);
    return r;
}

static coroutine_fn int nvme_co_preadv(BlockDriverState *bs,
                                       uint64_t offset, uint64_t bytes,
                                       QEMUIOVector *qiov, int flags)
{
    return nvme_co_prw(bs, offset, bytes, qiov, false, flags);
}

static coroutine_fn int nvme_co_pwritev(BlockDriverState *bs,
                                        uint64_t offset, uint64_t bytes,
                                        QEMUIOVector *qiov, int flags)
{
    return nvme_co_prw(bs, offset, bytes, qiov, true, flags);
}

static coroutine_fn int nvme_co_flush(BlockDriverState *bs)
{
    BDRVNVMeState *s = bs->opaque;
    NVMeQueuePair *ioq = s->queues[1];
    NVMeRequest *req;
    NvmeCmd cmd = {
        .opcode = NVME_CMD_FLUSH,
        .nsid = cpu_to_le32(s->nsid),
    };
    NVMeCoData data = {
        .ctx = bdrv_get_aio_context(bs),
        .ret = -EINPROGRESS,
    };

    assert(s->nr_queues > 1);
    req = nvme_get_free_req(ioq);
    assert(req);
    nvme_submit_command(s, ioq, req, &cmd, nvme_rw_cb, &data);

    data.co = qemu_coroutine_self();
    if (data.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }

    return data.ret;
}


static coroutine_fn int nvme_co_pwrite_zeroes(BlockDriverState *bs,
                                              int64_t offset,
                                              int bytes,
                                              BdrvRequestFlags flags)
{
    BDRVNVMeState *s = bs->opaque;
    NVMeQueuePair *ioq = s->queues[1];
    NVMeRequest *req;

    uint32_t cdw12 = ((bytes >> s->blkshift) - 1) & 0xFFFF;

    if (!s->supports_write_zeroes) {
        return -ENOTSUP;
    }

    NvmeCmd cmd = {
        .opcode = NVME_CMD_WRITE_ZEROS,
        .nsid = cpu_to_le32(s->nsid),
        .cdw10 = cpu_to_le32((offset >> s->blkshift) & 0xFFFFFFFF),
        .cdw11 = cpu_to_le32(((offset >> s->blkshift) >> 32) & 0xFFFFFFFF),
    };

    NVMeCoData data = {
        .ctx = bdrv_get_aio_context(bs),
        .ret = -EINPROGRESS,
    };

    if (flags & BDRV_REQ_MAY_UNMAP) {
        cdw12 |= (1 << 25);
    }

    if (flags & BDRV_REQ_FUA) {
        cdw12 |= (1 << 30);
    }

    cmd.cdw12 = cpu_to_le32(cdw12);

    trace_nvme_write_zeroes(s, offset, bytes, flags);
    assert(s->nr_queues > 1);
    req = nvme_get_free_req(ioq);
    assert(req);

    nvme_submit_command(s, ioq, req, &cmd, nvme_rw_cb, &data);

    data.co = qemu_coroutine_self();
    while (data.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }

    trace_nvme_rw_done(s, true, offset, bytes, data.ret);
    return data.ret;
}


static int coroutine_fn nvme_co_pdiscard(BlockDriverState *bs,
                                         int64_t offset,
                                         int bytes)
{
    BDRVNVMeState *s = bs->opaque;
    NVMeQueuePair *ioq = s->queues[1];
    NVMeRequest *req;
    NvmeDsmRange *buf;
    QEMUIOVector local_qiov;
    int ret;

    NvmeCmd cmd = {
        .opcode = NVME_CMD_DSM,
        .nsid = cpu_to_le32(s->nsid),
        .cdw10 = cpu_to_le32(0), /*number of ranges - 0 based*/
        .cdw11 = cpu_to_le32(1 << 2), /*deallocate bit*/
    };

    NVMeCoData data = {
        .ctx = bdrv_get_aio_context(bs),
        .ret = -EINPROGRESS,
    };

    if (!s->supports_discard) {
        return -ENOTSUP;
    }

    assert(s->nr_queues > 1);

    buf = qemu_try_blockalign0(bs, s->page_size);
    if (!buf) {
        return -ENOMEM;
    }

    buf->nlb = cpu_to_le32(bytes >> s->blkshift);
    buf->slba = cpu_to_le64(offset >> s->blkshift);
    buf->cattr = 0;

    qemu_iovec_init(&local_qiov, 1);
    qemu_iovec_add(&local_qiov, buf, 4096);

    req = nvme_get_free_req(ioq);
    assert(req);

    qemu_co_mutex_lock(&s->dma_map_lock);
    ret = nvme_cmd_map_qiov(bs, &cmd, req, &local_qiov);
    qemu_co_mutex_unlock(&s->dma_map_lock);

    if (ret) {
        req->busy = false;
        goto out;
    }

    trace_nvme_dsm(s, offset, bytes);

    nvme_submit_command(s, ioq, req, &cmd, nvme_rw_cb, &data);

    data.co = qemu_coroutine_self();
    while (data.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }

    qemu_co_mutex_lock(&s->dma_map_lock);
    ret = nvme_cmd_unmap_qiov(bs, &local_qiov);
    qemu_co_mutex_unlock(&s->dma_map_lock);

    if (ret) {
        goto out;
    }

    ret = data.ret;
    trace_nvme_dsm_done(s, offset, bytes, ret);
out:
    qemu_iovec_destroy(&local_qiov);
    qemu_vfree(buf);
    return ret;

}


static int nvme_reopen_prepare(BDRVReopenState *reopen_state,
                               BlockReopenQueue *queue, Error **errp)
{
    return 0;
}

static void nvme_refresh_filename(BlockDriverState *bs)
{
    BDRVNVMeState *s = bs->opaque;

    snprintf(bs->exact_filename, sizeof(bs->exact_filename), "nvme://%s/%i",
             s->device, s->nsid);
}

static void nvme_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVNVMeState *s = bs->opaque;

    bs->bl.opt_mem_alignment = s->page_size;
    bs->bl.request_alignment = s->page_size;
    bs->bl.max_transfer = s->max_transfer;
}

static void nvme_detach_aio_context(BlockDriverState *bs)
{
    BDRVNVMeState *s = bs->opaque;

    aio_set_event_notifier(bdrv_get_aio_context(bs), &s->irq_notifier,
                           false, NULL, NULL);
}

static void nvme_attach_aio_context(BlockDriverState *bs,
                                    AioContext *new_context)
{
    BDRVNVMeState *s = bs->opaque;

    s->aio_context = new_context;
    aio_set_event_notifier(new_context, &s->irq_notifier,
                           false, nvme_handle_event, nvme_poll_cb);
}

static void nvme_aio_plug(BlockDriverState *bs)
{
    BDRVNVMeState *s = bs->opaque;
    assert(!s->plugged);
    s->plugged = true;
}

static void nvme_aio_unplug(BlockDriverState *bs)
{
    int i;
    BDRVNVMeState *s = bs->opaque;
    assert(s->plugged);
    s->plugged = false;
    for (i = 1; i < s->nr_queues; i++) {
        NVMeQueuePair *q = s->queues[i];
        qemu_mutex_lock(&q->lock);
        nvme_kick(s, q);
        nvme_process_completion(s, q);
        qemu_mutex_unlock(&q->lock);
    }
}

static void nvme_register_buf(BlockDriverState *bs, void *host, size_t size)
{
    int ret;
    BDRVNVMeState *s = bs->opaque;

    ret = qemu_vfio_dma_map(s->vfio, host, size, false, NULL);
    if (ret) {
        /* FIXME: we may run out of IOVA addresses after repeated
         * bdrv_register_buf/bdrv_unregister_buf, because nvme_vfio_dma_unmap
         * doesn't reclaim addresses for fixed mappings. */
        error_report("nvme_register_buf failed: %s", strerror(-ret));
    }
}

static void nvme_unregister_buf(BlockDriverState *bs, void *host)
{
    BDRVNVMeState *s = bs->opaque;

    qemu_vfio_dma_unmap(s->vfio, host);
}

static const char *const nvme_strong_runtime_opts[] = {
    NVME_BLOCK_OPT_DEVICE,
    NVME_BLOCK_OPT_NAMESPACE,

    NULL
};

static BlockDriver bdrv_nvme = {
    .format_name              = "nvme",
    .protocol_name            = "nvme",
    .instance_size            = sizeof(BDRVNVMeState),

    .bdrv_parse_filename      = nvme_parse_filename,
    .bdrv_file_open           = nvme_file_open,
    .bdrv_close               = nvme_close,
    .bdrv_getlength           = nvme_getlength,
    .bdrv_probe_blocksizes    = nvme_probe_blocksizes,

    .bdrv_co_preadv           = nvme_co_preadv,
    .bdrv_co_pwritev          = nvme_co_pwritev,

    .bdrv_co_pwrite_zeroes    = nvme_co_pwrite_zeroes,
    .bdrv_co_pdiscard         = nvme_co_pdiscard,

    .bdrv_co_flush_to_disk    = nvme_co_flush,
    .bdrv_reopen_prepare      = nvme_reopen_prepare,

    .bdrv_refresh_filename    = nvme_refresh_filename,
    .bdrv_refresh_limits      = nvme_refresh_limits,
    .strong_runtime_opts      = nvme_strong_runtime_opts,

    .bdrv_detach_aio_context  = nvme_detach_aio_context,
    .bdrv_attach_aio_context  = nvme_attach_aio_context,

    .bdrv_io_plug             = nvme_aio_plug,
    .bdrv_io_unplug           = nvme_aio_unplug,

    .bdrv_register_buf        = nvme_register_buf,
    .bdrv_unregister_buf      = nvme_unregister_buf,
};

static void bdrv_nvme_init(void)
{
    bdrv_register(&bdrv_nvme);
}

block_init(bdrv_nvme_init);
