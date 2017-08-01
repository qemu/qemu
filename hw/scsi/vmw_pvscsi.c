/*
 * QEMU VMWARE PVSCSI paravirtual SCSI bus
 *
 * Copyright (c) 2012 Ravello Systems LTD (http://ravellosystems.com)
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Based on implementation by Paolo Bonzini
 * http://lists.gnu.org/archive/html/qemu-devel/2011-08/msg00729.html
 *
 * Authors:
 * Paolo Bonzini <pbonzini@redhat.com>
 * Dmitry Fleytman <dmitry@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 * NOTE about MSI-X:
 * MSI-X support has been removed for the moment because it leads Windows OS
 * to crash on startup. The crash happens because Windows driver requires
 * MSI-X shared memory to be part of the same BAR used for rings state
 * registers, etc. This is not supported by QEMU infrastructure so separate
 * BAR created from MSI-X purposes. Windows driver fails to deal with 2 BARs.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/scsi/scsi.h"
#include "block/scsi.h"
#include "hw/pci/msi.h"
#include "vmw_pvscsi.h"
#include "trace.h"


#define PVSCSI_USE_64BIT         (true)
#define PVSCSI_PER_VECTOR_MASK   (false)

#define PVSCSI_MAX_DEVS                   (64)
#define PVSCSI_MSIX_NUM_VECTORS           (1)

#define PVSCSI_MAX_SG_ELEM                2048

#define PVSCSI_MAX_CMD_DATA_WORDS \
    (sizeof(PVSCSICmdDescSetupRings)/sizeof(uint32_t))

#define RS_GET_FIELD(m, field) \
    (ldl_le_pci_dma(&container_of(m, PVSCSIState, rings)->parent_obj, \
                 (m)->rs_pa + offsetof(struct PVSCSIRingsState, field)))
#define RS_SET_FIELD(m, field, val) \
    (stl_le_pci_dma(&container_of(m, PVSCSIState, rings)->parent_obj, \
                 (m)->rs_pa + offsetof(struct PVSCSIRingsState, field), val))

typedef struct PVSCSIClass {
    PCIDeviceClass parent_class;
    DeviceRealize parent_dc_realize;
} PVSCSIClass;

#define TYPE_PVSCSI "pvscsi"
#define PVSCSI(obj) OBJECT_CHECK(PVSCSIState, (obj), TYPE_PVSCSI)

#define PVSCSI_DEVICE_CLASS(klass) \
    OBJECT_CLASS_CHECK(PVSCSIClass, (klass), TYPE_PVSCSI)
#define PVSCSI_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(PVSCSIClass, (obj), TYPE_PVSCSI)

/* Compatibility flags for migration */
#define PVSCSI_COMPAT_OLD_PCI_CONFIGURATION_BIT 0
#define PVSCSI_COMPAT_OLD_PCI_CONFIGURATION \
    (1 << PVSCSI_COMPAT_OLD_PCI_CONFIGURATION_BIT)
#define PVSCSI_COMPAT_DISABLE_PCIE_BIT 1
#define PVSCSI_COMPAT_DISABLE_PCIE \
    (1 << PVSCSI_COMPAT_DISABLE_PCIE_BIT)

#define PVSCSI_USE_OLD_PCI_CONFIGURATION(s) \
    ((s)->compat_flags & PVSCSI_COMPAT_OLD_PCI_CONFIGURATION)
#define PVSCSI_MSI_OFFSET(s) \
    (PVSCSI_USE_OLD_PCI_CONFIGURATION(s) ? 0x50 : 0x7c)
#define PVSCSI_EXP_EP_OFFSET (0x40)

typedef struct PVSCSIRingInfo {
    uint64_t            rs_pa;
    uint32_t            txr_len_mask;
    uint32_t            rxr_len_mask;
    uint32_t            msg_len_mask;
    uint64_t            req_ring_pages_pa[PVSCSI_SETUP_RINGS_MAX_NUM_PAGES];
    uint64_t            cmp_ring_pages_pa[PVSCSI_SETUP_RINGS_MAX_NUM_PAGES];
    uint64_t            msg_ring_pages_pa[PVSCSI_SETUP_MSG_RING_MAX_NUM_PAGES];
    uint64_t            consumed_ptr;
    uint64_t            filled_cmp_ptr;
    uint64_t            filled_msg_ptr;
} PVSCSIRingInfo;

typedef struct PVSCSISGState {
    hwaddr elemAddr;
    hwaddr dataAddr;
    uint32_t resid;
} PVSCSISGState;

typedef QTAILQ_HEAD(, PVSCSIRequest) PVSCSIRequestList;

typedef struct {
    PCIDevice parent_obj;
    MemoryRegion io_space;
    SCSIBus bus;
    QEMUBH *completion_worker;
    PVSCSIRequestList pending_queue;
    PVSCSIRequestList completion_queue;

    uint64_t reg_interrupt_status;        /* Interrupt status register value */
    uint64_t reg_interrupt_enabled;       /* Interrupt mask register value   */
    uint64_t reg_command_status;          /* Command status register value   */

    /* Command data adoption mechanism */
    uint64_t curr_cmd;                   /* Last command arrived             */
    uint32_t curr_cmd_data_cntr;         /* Amount of data for last command  */

    /* Collector for current command data */
    uint32_t curr_cmd_data[PVSCSI_MAX_CMD_DATA_WORDS];

    uint8_t rings_info_valid;            /* Whether data rings initialized   */
    uint8_t msg_ring_info_valid;         /* Whether message ring initialized */
    uint8_t use_msg;                     /* Whether to use message ring      */

    uint8_t msi_used;                    /* For migration compatibility      */
    PVSCSIRingInfo rings;                /* Data transfer rings manager      */
    uint32_t resetting;                  /* Reset in progress                */

    uint32_t compat_flags;
} PVSCSIState;

typedef struct PVSCSIRequest {
    SCSIRequest *sreq;
    PVSCSIState *dev;
    uint8_t sense_key;
    uint8_t completed;
    int lun;
    QEMUSGList sgl;
    PVSCSISGState sg;
    struct PVSCSIRingReqDesc req;
    struct PVSCSIRingCmpDesc cmp;
    QTAILQ_ENTRY(PVSCSIRequest) next;
} PVSCSIRequest;

/* Integer binary logarithm */
static int
pvscsi_log2(uint32_t input)
{
    int log = 0;
    assert(input > 0);
    while (input >> ++log) {
    }
    return log;
}

static void
pvscsi_ring_init_data(PVSCSIRingInfo *m, PVSCSICmdDescSetupRings *ri)
{
    int i;
    uint32_t txr_len_log2, rxr_len_log2;
    uint32_t req_ring_size, cmp_ring_size;
    m->rs_pa = ri->ringsStatePPN << VMW_PAGE_SHIFT;

    req_ring_size = ri->reqRingNumPages * PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE;
    cmp_ring_size = ri->cmpRingNumPages * PVSCSI_MAX_NUM_CMP_ENTRIES_PER_PAGE;
    txr_len_log2 = pvscsi_log2(req_ring_size - 1);
    rxr_len_log2 = pvscsi_log2(cmp_ring_size - 1);

    m->txr_len_mask = MASK(txr_len_log2);
    m->rxr_len_mask = MASK(rxr_len_log2);

    m->consumed_ptr = 0;
    m->filled_cmp_ptr = 0;

    for (i = 0; i < ri->reqRingNumPages; i++) {
        m->req_ring_pages_pa[i] = ri->reqRingPPNs[i] << VMW_PAGE_SHIFT;
    }

    for (i = 0; i < ri->cmpRingNumPages; i++) {
        m->cmp_ring_pages_pa[i] = ri->cmpRingPPNs[i] << VMW_PAGE_SHIFT;
    }

    RS_SET_FIELD(m, reqProdIdx, 0);
    RS_SET_FIELD(m, reqConsIdx, 0);
    RS_SET_FIELD(m, reqNumEntriesLog2, txr_len_log2);

    RS_SET_FIELD(m, cmpProdIdx, 0);
    RS_SET_FIELD(m, cmpConsIdx, 0);
    RS_SET_FIELD(m, cmpNumEntriesLog2, rxr_len_log2);

    trace_pvscsi_ring_init_data(txr_len_log2, rxr_len_log2);

    /* Flush ring state page changes */
    smp_wmb();
}

static int
pvscsi_ring_init_msg(PVSCSIRingInfo *m, PVSCSICmdDescSetupMsgRing *ri)
{
    int i;
    uint32_t len_log2;
    uint32_t ring_size;

    if (!ri->numPages || ri->numPages > PVSCSI_SETUP_MSG_RING_MAX_NUM_PAGES) {
        return -1;
    }
    ring_size = ri->numPages * PVSCSI_MAX_NUM_MSG_ENTRIES_PER_PAGE;
    len_log2 = pvscsi_log2(ring_size - 1);

    m->msg_len_mask = MASK(len_log2);

    m->filled_msg_ptr = 0;

    for (i = 0; i < ri->numPages; i++) {
        m->msg_ring_pages_pa[i] = ri->ringPPNs[i] << VMW_PAGE_SHIFT;
    }

    RS_SET_FIELD(m, msgProdIdx, 0);
    RS_SET_FIELD(m, msgConsIdx, 0);
    RS_SET_FIELD(m, msgNumEntriesLog2, len_log2);

    trace_pvscsi_ring_init_msg(len_log2);

    /* Flush ring state page changes */
    smp_wmb();

    return 0;
}

static void
pvscsi_ring_cleanup(PVSCSIRingInfo *mgr)
{
    mgr->rs_pa = 0;
    mgr->txr_len_mask = 0;
    mgr->rxr_len_mask = 0;
    mgr->msg_len_mask = 0;
    mgr->consumed_ptr = 0;
    mgr->filled_cmp_ptr = 0;
    mgr->filled_msg_ptr = 0;
    memset(mgr->req_ring_pages_pa, 0, sizeof(mgr->req_ring_pages_pa));
    memset(mgr->cmp_ring_pages_pa, 0, sizeof(mgr->cmp_ring_pages_pa));
    memset(mgr->msg_ring_pages_pa, 0, sizeof(mgr->msg_ring_pages_pa));
}

static hwaddr
pvscsi_ring_pop_req_descr(PVSCSIRingInfo *mgr)
{
    uint32_t ready_ptr = RS_GET_FIELD(mgr, reqProdIdx);
    uint32_t ring_size = PVSCSI_MAX_NUM_PAGES_REQ_RING
                            * PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE;

    if (ready_ptr != mgr->consumed_ptr
        && ready_ptr - mgr->consumed_ptr < ring_size) {
        uint32_t next_ready_ptr =
            mgr->consumed_ptr++ & mgr->txr_len_mask;
        uint32_t next_ready_page =
            next_ready_ptr / PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE;
        uint32_t inpage_idx =
            next_ready_ptr % PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE;

        return mgr->req_ring_pages_pa[next_ready_page] +
               inpage_idx * sizeof(PVSCSIRingReqDesc);
    } else {
        return 0;
    }
}

static void
pvscsi_ring_flush_req(PVSCSIRingInfo *mgr)
{
    RS_SET_FIELD(mgr, reqConsIdx, mgr->consumed_ptr);
}

static hwaddr
pvscsi_ring_pop_cmp_descr(PVSCSIRingInfo *mgr)
{
    /*
     * According to Linux driver code it explicitly verifies that number
     * of requests being processed by device is less then the size of
     * completion queue, so device may omit completion queue overflow
     * conditions check. We assume that this is true for other (Windows)
     * drivers as well.
     */

    uint32_t free_cmp_ptr =
        mgr->filled_cmp_ptr++ & mgr->rxr_len_mask;
    uint32_t free_cmp_page =
        free_cmp_ptr / PVSCSI_MAX_NUM_CMP_ENTRIES_PER_PAGE;
    uint32_t inpage_idx =
        free_cmp_ptr % PVSCSI_MAX_NUM_CMP_ENTRIES_PER_PAGE;
    return mgr->cmp_ring_pages_pa[free_cmp_page] +
           inpage_idx * sizeof(PVSCSIRingCmpDesc);
}

static hwaddr
pvscsi_ring_pop_msg_descr(PVSCSIRingInfo *mgr)
{
    uint32_t free_msg_ptr =
        mgr->filled_msg_ptr++ & mgr->msg_len_mask;
    uint32_t free_msg_page =
        free_msg_ptr / PVSCSI_MAX_NUM_MSG_ENTRIES_PER_PAGE;
    uint32_t inpage_idx =
        free_msg_ptr % PVSCSI_MAX_NUM_MSG_ENTRIES_PER_PAGE;
    return mgr->msg_ring_pages_pa[free_msg_page] +
           inpage_idx * sizeof(PVSCSIRingMsgDesc);
}

static void
pvscsi_ring_flush_cmp(PVSCSIRingInfo *mgr)
{
    /* Flush descriptor changes */
    smp_wmb();

    trace_pvscsi_ring_flush_cmp(mgr->filled_cmp_ptr);

    RS_SET_FIELD(mgr, cmpProdIdx, mgr->filled_cmp_ptr);
}

static bool
pvscsi_ring_msg_has_room(PVSCSIRingInfo *mgr)
{
    uint32_t prodIdx = RS_GET_FIELD(mgr, msgProdIdx);
    uint32_t consIdx = RS_GET_FIELD(mgr, msgConsIdx);

    return (prodIdx - consIdx) < (mgr->msg_len_mask + 1);
}

static void
pvscsi_ring_flush_msg(PVSCSIRingInfo *mgr)
{
    /* Flush descriptor changes */
    smp_wmb();

    trace_pvscsi_ring_flush_msg(mgr->filled_msg_ptr);

    RS_SET_FIELD(mgr, msgProdIdx, mgr->filled_msg_ptr);
}

static void
pvscsi_reset_state(PVSCSIState *s)
{
    s->curr_cmd = PVSCSI_CMD_FIRST;
    s->curr_cmd_data_cntr = 0;
    s->reg_command_status = PVSCSI_COMMAND_PROCESSING_SUCCEEDED;
    s->reg_interrupt_status = 0;
    pvscsi_ring_cleanup(&s->rings);
    s->rings_info_valid = FALSE;
    s->msg_ring_info_valid = FALSE;
    QTAILQ_INIT(&s->pending_queue);
    QTAILQ_INIT(&s->completion_queue);
}

static void
pvscsi_update_irq_status(PVSCSIState *s)
{
    PCIDevice *d = PCI_DEVICE(s);
    bool should_raise = s->reg_interrupt_enabled & s->reg_interrupt_status;

    trace_pvscsi_update_irq_level(should_raise, s->reg_interrupt_enabled,
                                  s->reg_interrupt_status);

    if (msi_enabled(d)) {
        if (should_raise) {
            trace_pvscsi_update_irq_msi();
            msi_notify(d, PVSCSI_VECTOR_COMPLETION);
        }
        return;
    }

    pci_set_irq(d, !!should_raise);
}

static void
pvscsi_raise_completion_interrupt(PVSCSIState *s)
{
    s->reg_interrupt_status |= PVSCSI_INTR_CMPL_0;

    /* Memory barrier to flush interrupt status register changes*/
    smp_wmb();

    pvscsi_update_irq_status(s);
}

static void
pvscsi_raise_message_interrupt(PVSCSIState *s)
{
    s->reg_interrupt_status |= PVSCSI_INTR_MSG_0;

    /* Memory barrier to flush interrupt status register changes*/
    smp_wmb();

    pvscsi_update_irq_status(s);
}

static void
pvscsi_cmp_ring_put(PVSCSIState *s, struct PVSCSIRingCmpDesc *cmp_desc)
{
    hwaddr cmp_descr_pa;

    cmp_descr_pa = pvscsi_ring_pop_cmp_descr(&s->rings);
    trace_pvscsi_cmp_ring_put(cmp_descr_pa);
    cpu_physical_memory_write(cmp_descr_pa, (void *)cmp_desc,
                              sizeof(*cmp_desc));
}

static void
pvscsi_msg_ring_put(PVSCSIState *s, struct PVSCSIRingMsgDesc *msg_desc)
{
    hwaddr msg_descr_pa;

    msg_descr_pa = pvscsi_ring_pop_msg_descr(&s->rings);
    trace_pvscsi_msg_ring_put(msg_descr_pa);
    cpu_physical_memory_write(msg_descr_pa, (void *)msg_desc,
                              sizeof(*msg_desc));
}

static void
pvscsi_process_completion_queue(void *opaque)
{
    PVSCSIState *s = opaque;
    PVSCSIRequest *pvscsi_req;
    bool has_completed = false;

    while (!QTAILQ_EMPTY(&s->completion_queue)) {
        pvscsi_req = QTAILQ_FIRST(&s->completion_queue);
        QTAILQ_REMOVE(&s->completion_queue, pvscsi_req, next);
        pvscsi_cmp_ring_put(s, &pvscsi_req->cmp);
        g_free(pvscsi_req);
        has_completed = true;
    }

    if (has_completed) {
        pvscsi_ring_flush_cmp(&s->rings);
        pvscsi_raise_completion_interrupt(s);
    }
}

static void
pvscsi_reset_adapter(PVSCSIState *s)
{
    s->resetting++;
    qbus_reset_all_fn(&s->bus);
    s->resetting--;
    pvscsi_process_completion_queue(s);
    assert(QTAILQ_EMPTY(&s->pending_queue));
    pvscsi_reset_state(s);
}

static void
pvscsi_schedule_completion_processing(PVSCSIState *s)
{
    /* Try putting more complete requests on the ring. */
    if (!QTAILQ_EMPTY(&s->completion_queue)) {
        qemu_bh_schedule(s->completion_worker);
    }
}

static void
pvscsi_complete_request(PVSCSIState *s, PVSCSIRequest *r)
{
    assert(!r->completed);

    trace_pvscsi_complete_request(r->cmp.context, r->cmp.dataLen,
                                  r->sense_key);
    if (r->sreq != NULL) {
        scsi_req_unref(r->sreq);
        r->sreq = NULL;
    }
    r->completed = 1;
    QTAILQ_REMOVE(&s->pending_queue, r, next);
    QTAILQ_INSERT_TAIL(&s->completion_queue, r, next);
    pvscsi_schedule_completion_processing(s);
}

static QEMUSGList *pvscsi_get_sg_list(SCSIRequest *r)
{
    PVSCSIRequest *req = r->hba_private;

    trace_pvscsi_get_sg_list(req->sgl.nsg, req->sgl.size);

    return &req->sgl;
}

static void
pvscsi_get_next_sg_elem(PVSCSISGState *sg)
{
    struct PVSCSISGElement elem;

    cpu_physical_memory_read(sg->elemAddr, (void *)&elem, sizeof(elem));
    if ((elem.flags & ~PVSCSI_KNOWN_FLAGS) != 0) {
        /*
            * There is PVSCSI_SGE_FLAG_CHAIN_ELEMENT flag described in
            * header file but its value is unknown. This flag requires
            * additional processing, so we put warning here to catch it
            * some day and make proper implementation
            */
        trace_pvscsi_get_next_sg_elem(elem.flags);
    }

    sg->elemAddr += sizeof(elem);
    sg->dataAddr = elem.addr;
    sg->resid = elem.length;
}

static void
pvscsi_write_sense(PVSCSIRequest *r, uint8_t *sense, int len)
{
    r->cmp.senseLen = MIN(r->req.senseLen, len);
    r->sense_key = sense[(sense[0] & 2) ? 1 : 2];
    cpu_physical_memory_write(r->req.senseAddr, sense, r->cmp.senseLen);
}

static void
pvscsi_command_complete(SCSIRequest *req, uint32_t status, size_t resid)
{
    PVSCSIRequest *pvscsi_req = req->hba_private;
    PVSCSIState *s;

    if (!pvscsi_req) {
        trace_pvscsi_command_complete_not_found(req->tag);
        return;
    }
    s = pvscsi_req->dev;

    if (resid) {
        /* Short transfer.  */
        trace_pvscsi_command_complete_data_run();
        pvscsi_req->cmp.hostStatus = BTSTAT_DATARUN;
    }

    pvscsi_req->cmp.scsiStatus = status;
    if (pvscsi_req->cmp.scsiStatus == CHECK_CONDITION) {
        uint8_t sense[SCSI_SENSE_BUF_SIZE];
        int sense_len =
            scsi_req_get_sense(pvscsi_req->sreq, sense, sizeof(sense));

        trace_pvscsi_command_complete_sense_len(sense_len);
        pvscsi_write_sense(pvscsi_req, sense, sense_len);
    }
    qemu_sglist_destroy(&pvscsi_req->sgl);
    pvscsi_complete_request(s, pvscsi_req);
}

static void
pvscsi_send_msg(PVSCSIState *s, SCSIDevice *dev, uint32_t msg_type)
{
    if (s->msg_ring_info_valid && pvscsi_ring_msg_has_room(&s->rings)) {
        PVSCSIMsgDescDevStatusChanged msg = {0};

        msg.type = msg_type;
        msg.bus = dev->channel;
        msg.target = dev->id;
        msg.lun[1] = dev->lun;

        pvscsi_msg_ring_put(s, (PVSCSIRingMsgDesc *)&msg);
        pvscsi_ring_flush_msg(&s->rings);
        pvscsi_raise_message_interrupt(s);
    }
}

static void
pvscsi_hotplug(HotplugHandler *hotplug_dev, DeviceState *dev, Error **errp)
{
    PVSCSIState *s = PVSCSI(hotplug_dev);

    pvscsi_send_msg(s, SCSI_DEVICE(dev), PVSCSI_MSG_DEV_ADDED);
}

static void
pvscsi_hot_unplug(HotplugHandler *hotplug_dev, DeviceState *dev, Error **errp)
{
    PVSCSIState *s = PVSCSI(hotplug_dev);

    pvscsi_send_msg(s, SCSI_DEVICE(dev), PVSCSI_MSG_DEV_REMOVED);
    qdev_simple_device_unplug_cb(hotplug_dev, dev, errp);
}

static void
pvscsi_request_cancelled(SCSIRequest *req)
{
    PVSCSIRequest *pvscsi_req = req->hba_private;
    PVSCSIState *s = pvscsi_req->dev;

    if (pvscsi_req->completed) {
        return;
    }

   if (pvscsi_req->dev->resetting) {
       pvscsi_req->cmp.hostStatus = BTSTAT_BUSRESET;
    } else {
       pvscsi_req->cmp.hostStatus = BTSTAT_ABORTQUEUE;
    }

    pvscsi_complete_request(s, pvscsi_req);
}

static SCSIDevice*
pvscsi_device_find(PVSCSIState *s, int channel, int target,
                   uint8_t *requested_lun, uint8_t *target_lun)
{
    if (requested_lun[0] || requested_lun[2] || requested_lun[3] ||
        requested_lun[4] || requested_lun[5] || requested_lun[6] ||
        requested_lun[7] || (target > PVSCSI_MAX_DEVS)) {
        return NULL;
    } else {
        *target_lun = requested_lun[1];
        return scsi_device_find(&s->bus, channel, target, *target_lun);
    }
}

static PVSCSIRequest *
pvscsi_queue_pending_descriptor(PVSCSIState *s, SCSIDevice **d,
                                struct PVSCSIRingReqDesc *descr)
{
    PVSCSIRequest *pvscsi_req;
    uint8_t lun;

    pvscsi_req = g_malloc0(sizeof(*pvscsi_req));
    pvscsi_req->dev = s;
    pvscsi_req->req = *descr;
    pvscsi_req->cmp.context = pvscsi_req->req.context;
    QTAILQ_INSERT_TAIL(&s->pending_queue, pvscsi_req, next);

    *d = pvscsi_device_find(s, descr->bus, descr->target, descr->lun, &lun);
    if (*d) {
        pvscsi_req->lun = lun;
    }

    return pvscsi_req;
}

static void
pvscsi_convert_sglist(PVSCSIRequest *r)
{
    uint32_t chunk_size, elmcnt = 0;
    uint64_t data_length = r->req.dataLen;
    PVSCSISGState sg = r->sg;
    while (data_length && elmcnt < PVSCSI_MAX_SG_ELEM) {
        while (!sg.resid && elmcnt++ < PVSCSI_MAX_SG_ELEM) {
            pvscsi_get_next_sg_elem(&sg);
            trace_pvscsi_convert_sglist(r->req.context, r->sg.dataAddr,
                                        r->sg.resid);
        }
        chunk_size = MIN(data_length, sg.resid);
        if (chunk_size) {
            qemu_sglist_add(&r->sgl, sg.dataAddr, chunk_size);
        }

        sg.dataAddr += chunk_size;
        data_length -= chunk_size;
        sg.resid -= chunk_size;
    }
}

static void
pvscsi_build_sglist(PVSCSIState *s, PVSCSIRequest *r)
{
    PCIDevice *d = PCI_DEVICE(s);

    pci_dma_sglist_init(&r->sgl, d, 1);
    if (r->req.flags & PVSCSI_FLAG_CMD_WITH_SG_LIST) {
        pvscsi_convert_sglist(r);
    } else {
        qemu_sglist_add(&r->sgl, r->req.dataAddr, r->req.dataLen);
    }
}

static void
pvscsi_process_request_descriptor(PVSCSIState *s,
                                  struct PVSCSIRingReqDesc *descr)
{
    SCSIDevice *d;
    PVSCSIRequest *r = pvscsi_queue_pending_descriptor(s, &d, descr);
    int64_t n;

    trace_pvscsi_process_req_descr(descr->cdb[0], descr->context);

    if (!d) {
        r->cmp.hostStatus = BTSTAT_SELTIMEO;
        trace_pvscsi_process_req_descr_unknown_device();
        pvscsi_complete_request(s, r);
        return;
    }

    if (descr->flags & PVSCSI_FLAG_CMD_WITH_SG_LIST) {
        r->sg.elemAddr = descr->dataAddr;
    }

    r->sreq = scsi_req_new(d, descr->context, r->lun, descr->cdb, r);
    if (r->sreq->cmd.mode == SCSI_XFER_FROM_DEV &&
        (descr->flags & PVSCSI_FLAG_CMD_DIR_TODEVICE)) {
        r->cmp.hostStatus = BTSTAT_BADMSG;
        trace_pvscsi_process_req_descr_invalid_dir();
        scsi_req_cancel(r->sreq);
        return;
    }
    if (r->sreq->cmd.mode == SCSI_XFER_TO_DEV &&
        (descr->flags & PVSCSI_FLAG_CMD_DIR_TOHOST)) {
        r->cmp.hostStatus = BTSTAT_BADMSG;
        trace_pvscsi_process_req_descr_invalid_dir();
        scsi_req_cancel(r->sreq);
        return;
    }

    pvscsi_build_sglist(s, r);
    n = scsi_req_enqueue(r->sreq);

    if (n) {
        scsi_req_continue(r->sreq);
    }
}

static void
pvscsi_process_io(PVSCSIState *s)
{
    PVSCSIRingReqDesc descr;
    hwaddr next_descr_pa;

    assert(s->rings_info_valid);
    while ((next_descr_pa = pvscsi_ring_pop_req_descr(&s->rings)) != 0) {

        /* Only read after production index verification */
        smp_rmb();

        trace_pvscsi_process_io(next_descr_pa);
        cpu_physical_memory_read(next_descr_pa, &descr, sizeof(descr));
        pvscsi_process_request_descriptor(s, &descr);
    }

    pvscsi_ring_flush_req(&s->rings);
}

static void
pvscsi_dbg_dump_tx_rings_config(PVSCSICmdDescSetupRings *rc)
{
    int i;
    trace_pvscsi_tx_rings_ppn("Rings State", rc->ringsStatePPN);

    trace_pvscsi_tx_rings_num_pages("Request Ring", rc->reqRingNumPages);
    for (i = 0; i < rc->reqRingNumPages; i++) {
        trace_pvscsi_tx_rings_ppn("Request Ring", rc->reqRingPPNs[i]);
    }

    trace_pvscsi_tx_rings_num_pages("Confirm Ring", rc->cmpRingNumPages);
    for (i = 0; i < rc->cmpRingNumPages; i++) {
        trace_pvscsi_tx_rings_ppn("Confirm Ring", rc->cmpRingPPNs[i]);
    }
}

static uint64_t
pvscsi_on_cmd_config(PVSCSIState *s)
{
    trace_pvscsi_on_cmd_noimpl("PVSCSI_CMD_CONFIG");
    return PVSCSI_COMMAND_PROCESSING_FAILED;
}

static uint64_t
pvscsi_on_cmd_unplug(PVSCSIState *s)
{
    trace_pvscsi_on_cmd_noimpl("PVSCSI_CMD_DEVICE_UNPLUG");
    return PVSCSI_COMMAND_PROCESSING_FAILED;
}

static uint64_t
pvscsi_on_issue_scsi(PVSCSIState *s)
{
    trace_pvscsi_on_cmd_noimpl("PVSCSI_CMD_ISSUE_SCSI");
    return PVSCSI_COMMAND_PROCESSING_FAILED;
}

static uint64_t
pvscsi_on_cmd_setup_rings(PVSCSIState *s)
{
    PVSCSICmdDescSetupRings *rc =
        (PVSCSICmdDescSetupRings *) s->curr_cmd_data;

    trace_pvscsi_on_cmd_arrived("PVSCSI_CMD_SETUP_RINGS");

    if (!rc->reqRingNumPages
        || rc->reqRingNumPages > PVSCSI_SETUP_RINGS_MAX_NUM_PAGES
        || !rc->cmpRingNumPages
        || rc->cmpRingNumPages > PVSCSI_SETUP_RINGS_MAX_NUM_PAGES) {
        return PVSCSI_COMMAND_PROCESSING_FAILED;
    }

    pvscsi_dbg_dump_tx_rings_config(rc);
    pvscsi_ring_init_data(&s->rings, rc);

    s->rings_info_valid = TRUE;
    return PVSCSI_COMMAND_PROCESSING_SUCCEEDED;
}

static uint64_t
pvscsi_on_cmd_abort(PVSCSIState *s)
{
    PVSCSICmdDescAbortCmd *cmd = (PVSCSICmdDescAbortCmd *) s->curr_cmd_data;
    PVSCSIRequest *r, *next;

    trace_pvscsi_on_cmd_abort(cmd->context, cmd->target);

    QTAILQ_FOREACH_SAFE(r, &s->pending_queue, next, next) {
        if (r->req.context == cmd->context) {
            break;
        }
    }
    if (r) {
        assert(!r->completed);
        r->cmp.hostStatus = BTSTAT_ABORTQUEUE;
        scsi_req_cancel(r->sreq);
    }

    return PVSCSI_COMMAND_PROCESSING_SUCCEEDED;
}

static uint64_t
pvscsi_on_cmd_unknown(PVSCSIState *s)
{
    trace_pvscsi_on_cmd_unknown_data(s->curr_cmd_data[0]);
    return PVSCSI_COMMAND_PROCESSING_FAILED;
}

static uint64_t
pvscsi_on_cmd_reset_device(PVSCSIState *s)
{
    uint8_t target_lun = 0;
    struct PVSCSICmdDescResetDevice *cmd =
        (struct PVSCSICmdDescResetDevice *) s->curr_cmd_data;
    SCSIDevice *sdev;

    sdev = pvscsi_device_find(s, 0, cmd->target, cmd->lun, &target_lun);

    trace_pvscsi_on_cmd_reset_dev(cmd->target, (int) target_lun, sdev);

    if (sdev != NULL) {
        s->resetting++;
        device_reset(&sdev->qdev);
        s->resetting--;
        return PVSCSI_COMMAND_PROCESSING_SUCCEEDED;
    }

    return PVSCSI_COMMAND_PROCESSING_FAILED;
}

static uint64_t
pvscsi_on_cmd_reset_bus(PVSCSIState *s)
{
    trace_pvscsi_on_cmd_arrived("PVSCSI_CMD_RESET_BUS");

    s->resetting++;
    qbus_reset_all_fn(&s->bus);
    s->resetting--;
    return PVSCSI_COMMAND_PROCESSING_SUCCEEDED;
}

static uint64_t
pvscsi_on_cmd_setup_msg_ring(PVSCSIState *s)
{
    PVSCSICmdDescSetupMsgRing *rc =
        (PVSCSICmdDescSetupMsgRing *) s->curr_cmd_data;

    trace_pvscsi_on_cmd_arrived("PVSCSI_CMD_SETUP_MSG_RING");

    if (!s->use_msg) {
        return PVSCSI_COMMAND_PROCESSING_FAILED;
    }

    if (s->rings_info_valid) {
        if (pvscsi_ring_init_msg(&s->rings, rc) < 0) {
            return PVSCSI_COMMAND_PROCESSING_FAILED;
        }
        s->msg_ring_info_valid = TRUE;
    }
    return sizeof(PVSCSICmdDescSetupMsgRing) / sizeof(uint32_t);
}

static uint64_t
pvscsi_on_cmd_adapter_reset(PVSCSIState *s)
{
    trace_pvscsi_on_cmd_arrived("PVSCSI_CMD_ADAPTER_RESET");

    pvscsi_reset_adapter(s);
    return PVSCSI_COMMAND_PROCESSING_SUCCEEDED;
}

static const struct {
    int       data_size;
    uint64_t  (*handler_fn)(PVSCSIState *s);
} pvscsi_commands[] = {
    [PVSCSI_CMD_FIRST] = {
        .data_size = 0,
        .handler_fn = pvscsi_on_cmd_unknown,
    },

    /* Not implemented, data size defined based on what arrives on windows */
    [PVSCSI_CMD_CONFIG] = {
        .data_size = 6 * sizeof(uint32_t),
        .handler_fn = pvscsi_on_cmd_config,
    },

    /* Command not implemented, data size is unknown */
    [PVSCSI_CMD_ISSUE_SCSI] = {
        .data_size = 0,
        .handler_fn = pvscsi_on_issue_scsi,
    },

    /* Command not implemented, data size is unknown */
    [PVSCSI_CMD_DEVICE_UNPLUG] = {
        .data_size = 0,
        .handler_fn = pvscsi_on_cmd_unplug,
    },

    [PVSCSI_CMD_SETUP_RINGS] = {
        .data_size = sizeof(PVSCSICmdDescSetupRings),
        .handler_fn = pvscsi_on_cmd_setup_rings,
    },

    [PVSCSI_CMD_RESET_DEVICE] = {
        .data_size = sizeof(struct PVSCSICmdDescResetDevice),
        .handler_fn = pvscsi_on_cmd_reset_device,
    },

    [PVSCSI_CMD_RESET_BUS] = {
        .data_size = 0,
        .handler_fn = pvscsi_on_cmd_reset_bus,
    },

    [PVSCSI_CMD_SETUP_MSG_RING] = {
        .data_size = sizeof(PVSCSICmdDescSetupMsgRing),
        .handler_fn = pvscsi_on_cmd_setup_msg_ring,
    },

    [PVSCSI_CMD_ADAPTER_RESET] = {
        .data_size = 0,
        .handler_fn = pvscsi_on_cmd_adapter_reset,
    },

    [PVSCSI_CMD_ABORT_CMD] = {
        .data_size = sizeof(struct PVSCSICmdDescAbortCmd),
        .handler_fn = pvscsi_on_cmd_abort,
    },
};

static void
pvscsi_do_command_processing(PVSCSIState *s)
{
    size_t bytes_arrived = s->curr_cmd_data_cntr * sizeof(uint32_t);

    assert(s->curr_cmd < PVSCSI_CMD_LAST);
    if (bytes_arrived >= pvscsi_commands[s->curr_cmd].data_size) {
        s->reg_command_status = pvscsi_commands[s->curr_cmd].handler_fn(s);
        s->curr_cmd = PVSCSI_CMD_FIRST;
        s->curr_cmd_data_cntr   = 0;
    }
}

static void
pvscsi_on_command_data(PVSCSIState *s, uint32_t value)
{
    size_t bytes_arrived = s->curr_cmd_data_cntr * sizeof(uint32_t);

    assert(bytes_arrived < sizeof(s->curr_cmd_data));
    s->curr_cmd_data[s->curr_cmd_data_cntr++] = value;

    pvscsi_do_command_processing(s);
}

static void
pvscsi_on_command(PVSCSIState *s, uint64_t cmd_id)
{
    if ((cmd_id > PVSCSI_CMD_FIRST) && (cmd_id < PVSCSI_CMD_LAST)) {
        s->curr_cmd = cmd_id;
    } else {
        s->curr_cmd = PVSCSI_CMD_FIRST;
        trace_pvscsi_on_cmd_unknown(cmd_id);
    }

    s->curr_cmd_data_cntr = 0;
    s->reg_command_status = PVSCSI_COMMAND_NOT_ENOUGH_DATA;

    pvscsi_do_command_processing(s);
}

static void
pvscsi_io_write(void *opaque, hwaddr addr,
                uint64_t val, unsigned size)
{
    PVSCSIState *s = opaque;

    switch (addr) {
    case PVSCSI_REG_OFFSET_COMMAND:
        pvscsi_on_command(s, val);
        break;

    case PVSCSI_REG_OFFSET_COMMAND_DATA:
        pvscsi_on_command_data(s, (uint32_t) val);
        break;

    case PVSCSI_REG_OFFSET_INTR_STATUS:
        trace_pvscsi_io_write("PVSCSI_REG_OFFSET_INTR_STATUS", val);
        s->reg_interrupt_status &= ~val;
        pvscsi_update_irq_status(s);
        pvscsi_schedule_completion_processing(s);
        break;

    case PVSCSI_REG_OFFSET_INTR_MASK:
        trace_pvscsi_io_write("PVSCSI_REG_OFFSET_INTR_MASK", val);
        s->reg_interrupt_enabled = val;
        pvscsi_update_irq_status(s);
        break;

    case PVSCSI_REG_OFFSET_KICK_NON_RW_IO:
        trace_pvscsi_io_write("PVSCSI_REG_OFFSET_KICK_NON_RW_IO", val);
        pvscsi_process_io(s);
        break;

    case PVSCSI_REG_OFFSET_KICK_RW_IO:
        trace_pvscsi_io_write("PVSCSI_REG_OFFSET_KICK_RW_IO", val);
        pvscsi_process_io(s);
        break;

    case PVSCSI_REG_OFFSET_DEBUG:
        trace_pvscsi_io_write("PVSCSI_REG_OFFSET_DEBUG", val);
        break;

    default:
        trace_pvscsi_io_write_unknown(addr, size, val);
        break;
    }

}

static uint64_t
pvscsi_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PVSCSIState *s = opaque;

    switch (addr) {
    case PVSCSI_REG_OFFSET_INTR_STATUS:
        trace_pvscsi_io_read("PVSCSI_REG_OFFSET_INTR_STATUS",
                             s->reg_interrupt_status);
        return s->reg_interrupt_status;

    case PVSCSI_REG_OFFSET_INTR_MASK:
        trace_pvscsi_io_read("PVSCSI_REG_OFFSET_INTR_MASK",
                             s->reg_interrupt_status);
        return s->reg_interrupt_enabled;

    case PVSCSI_REG_OFFSET_COMMAND_STATUS:
        trace_pvscsi_io_read("PVSCSI_REG_OFFSET_COMMAND_STATUS",
                             s->reg_interrupt_status);
        return s->reg_command_status;

    default:
        trace_pvscsi_io_read_unknown(addr, size);
        return 0;
    }
}


static void
pvscsi_init_msi(PVSCSIState *s)
{
    int res;
    PCIDevice *d = PCI_DEVICE(s);

    res = msi_init(d, PVSCSI_MSI_OFFSET(s), PVSCSI_MSIX_NUM_VECTORS,
                   PVSCSI_USE_64BIT, PVSCSI_PER_VECTOR_MASK, NULL);
    if (res < 0) {
        trace_pvscsi_init_msi_fail(res);
        s->msi_used = false;
    } else {
        s->msi_used = true;
    }
}

static void
pvscsi_cleanup_msi(PVSCSIState *s)
{
    PCIDevice *d = PCI_DEVICE(s);

    msi_uninit(d);
}

static const MemoryRegionOps pvscsi_ops = {
        .read = pvscsi_io_read,
        .write = pvscsi_io_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .impl = {
                .min_access_size = 4,
                .max_access_size = 4,
        },
};

static const struct SCSIBusInfo pvscsi_scsi_info = {
        .tcq = true,
        .max_target = PVSCSI_MAX_DEVS,
        .max_channel = 0,
        .max_lun = 0,

        .get_sg_list = pvscsi_get_sg_list,
        .complete = pvscsi_command_complete,
        .cancel = pvscsi_request_cancelled,
};

static void
pvscsi_realizefn(PCIDevice *pci_dev, Error **errp)
{
    PVSCSIState *s = PVSCSI(pci_dev);

    trace_pvscsi_state("init");

    /* PCI subsystem ID, subsystem vendor ID, revision */
    if (PVSCSI_USE_OLD_PCI_CONFIGURATION(s)) {
        pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID, 0x1000);
    } else {
        pci_set_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID,
                     PCI_VENDOR_ID_VMWARE);
        pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID,
                     PCI_DEVICE_ID_VMWARE_PVSCSI);
        pci_config_set_revision(pci_dev->config, 0x2);
    }

    /* PCI latency timer = 255 */
    pci_dev->config[PCI_LATENCY_TIMER] = 0xff;

    /* Interrupt pin A */
    pci_config_set_interrupt_pin(pci_dev->config, 1);

    memory_region_init_io(&s->io_space, OBJECT(s), &pvscsi_ops, s,
                          "pvscsi-io", PVSCSI_MEM_SPACE_SIZE);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->io_space);

    pvscsi_init_msi(s);

    if (pci_is_express(pci_dev) && pci_bus_is_express(pci_dev->bus)) {
        pcie_endpoint_cap_init(pci_dev, PVSCSI_EXP_EP_OFFSET);
    }

    s->completion_worker = qemu_bh_new(pvscsi_process_completion_queue, s);

    scsi_bus_new(&s->bus, sizeof(s->bus), DEVICE(pci_dev),
                 &pvscsi_scsi_info, NULL);
    /* override default SCSI bus hotplug-handler, with pvscsi's one */
    qbus_set_hotplug_handler(BUS(&s->bus), DEVICE(s), &error_abort);
    pvscsi_reset_state(s);
}

static void
pvscsi_uninit(PCIDevice *pci_dev)
{
    PVSCSIState *s = PVSCSI(pci_dev);

    trace_pvscsi_state("uninit");
    qemu_bh_delete(s->completion_worker);

    pvscsi_cleanup_msi(s);
}

static void
pvscsi_reset(DeviceState *dev)
{
    PCIDevice *d = PCI_DEVICE(dev);
    PVSCSIState *s = PVSCSI(d);

    trace_pvscsi_state("reset");
    pvscsi_reset_adapter(s);
}

static void
pvscsi_pre_save(void *opaque)
{
    PVSCSIState *s = (PVSCSIState *) opaque;

    trace_pvscsi_state("presave");

    assert(QTAILQ_EMPTY(&s->pending_queue));
    assert(QTAILQ_EMPTY(&s->completion_queue));
}

static int
pvscsi_post_load(void *opaque, int version_id)
{
    trace_pvscsi_state("postload");
    return 0;
}

static bool pvscsi_vmstate_need_pcie_device(void *opaque)
{
    PVSCSIState *s = PVSCSI(opaque);

    return !(s->compat_flags & PVSCSI_COMPAT_DISABLE_PCIE);
}

static bool pvscsi_vmstate_test_pci_device(void *opaque, int version_id)
{
    return !pvscsi_vmstate_need_pcie_device(opaque);
}

static const VMStateDescription vmstate_pvscsi_pcie_device = {
    .name = "pvscsi/pcie",
    .needed = pvscsi_vmstate_need_pcie_device,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PVSCSIState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pvscsi = {
    .name = "pvscsi",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_save = pvscsi_pre_save,
    .post_load = pvscsi_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_TEST(parent_obj, PVSCSIState,
                            pvscsi_vmstate_test_pci_device, 0,
                            vmstate_pci_device, PCIDevice),
        VMSTATE_UINT8(msi_used, PVSCSIState),
        VMSTATE_UINT32(resetting, PVSCSIState),
        VMSTATE_UINT64(reg_interrupt_status, PVSCSIState),
        VMSTATE_UINT64(reg_interrupt_enabled, PVSCSIState),
        VMSTATE_UINT64(reg_command_status, PVSCSIState),
        VMSTATE_UINT64(curr_cmd, PVSCSIState),
        VMSTATE_UINT32(curr_cmd_data_cntr, PVSCSIState),
        VMSTATE_UINT32_ARRAY(curr_cmd_data, PVSCSIState,
                             ARRAY_SIZE(((PVSCSIState *)NULL)->curr_cmd_data)),
        VMSTATE_UINT8(rings_info_valid, PVSCSIState),
        VMSTATE_UINT8(msg_ring_info_valid, PVSCSIState),
        VMSTATE_UINT8(use_msg, PVSCSIState),

        VMSTATE_UINT64(rings.rs_pa, PVSCSIState),
        VMSTATE_UINT32(rings.txr_len_mask, PVSCSIState),
        VMSTATE_UINT32(rings.rxr_len_mask, PVSCSIState),
        VMSTATE_UINT64_ARRAY(rings.req_ring_pages_pa, PVSCSIState,
                             PVSCSI_SETUP_RINGS_MAX_NUM_PAGES),
        VMSTATE_UINT64_ARRAY(rings.cmp_ring_pages_pa, PVSCSIState,
                             PVSCSI_SETUP_RINGS_MAX_NUM_PAGES),
        VMSTATE_UINT64(rings.consumed_ptr, PVSCSIState),
        VMSTATE_UINT64(rings.filled_cmp_ptr, PVSCSIState),

        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_pvscsi_pcie_device,
        NULL
    }
};

static Property pvscsi_properties[] = {
    DEFINE_PROP_UINT8("use_msg", PVSCSIState, use_msg, 1),
    DEFINE_PROP_BIT("x-old-pci-configuration", PVSCSIState, compat_flags,
                    PVSCSI_COMPAT_OLD_PCI_CONFIGURATION_BIT, false),
    DEFINE_PROP_BIT("x-disable-pcie", PVSCSIState, compat_flags,
                    PVSCSI_COMPAT_DISABLE_PCIE_BIT, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void pvscsi_realize(DeviceState *qdev, Error **errp)
{
    PVSCSIClass *pvs_c = PVSCSI_DEVICE_GET_CLASS(qdev);
    PCIDevice *pci_dev = PCI_DEVICE(qdev);
    PVSCSIState *s = PVSCSI(qdev);

    if (!(s->compat_flags & PVSCSI_COMPAT_DISABLE_PCIE)) {
        pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    }

    pvs_c->parent_dc_realize(qdev, errp);
}

static void pvscsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    PVSCSIClass *pvs_k = PVSCSI_DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    k->realize = pvscsi_realizefn;
    k->exit = pvscsi_uninit;
    k->vendor_id = PCI_VENDOR_ID_VMWARE;
    k->device_id = PCI_DEVICE_ID_VMWARE_PVSCSI;
    k->class_id = PCI_CLASS_STORAGE_SCSI;
    k->subsystem_id = 0x1000;
    pvs_k->parent_dc_realize = dc->realize;
    dc->realize = pvscsi_realize;
    dc->reset = pvscsi_reset;
    dc->vmsd = &vmstate_pvscsi;
    dc->props = pvscsi_properties;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    hc->unplug = pvscsi_hot_unplug;
    hc->plug = pvscsi_hotplug;
}

static const TypeInfo pvscsi_info = {
    .name          = TYPE_PVSCSI,
    .parent        = TYPE_PCI_DEVICE,
    .class_size    = sizeof(PVSCSIClass),
    .instance_size = sizeof(PVSCSIState),
    .class_init    = pvscsi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void
pvscsi_register_types(void)
{
    type_register_static(&pvscsi_info);
}

type_init(pvscsi_register_types);
