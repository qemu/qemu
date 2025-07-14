/*
 * CXL Event processing
 *
 * Copyright(C) 2023 Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_events.h"

/* Artificial limit on the number of events a log can hold */
#define CXL_TEST_EVENT_OVERFLOW 8

static void reset_overflow(CXLEventLog *log)
{
    log->overflow_err_count = 0;
    log->first_overflow_timestamp = 0;
    log->last_overflow_timestamp = 0;
}

void cxl_event_init(CXLDeviceState *cxlds, int start_msg_num)
{
    CXLEventLog *log;
    int i;

    for (i = 0; i < CXL_EVENT_TYPE_MAX; i++) {
        log = &cxlds->event_logs[i];
        log->next_handle = 1;
        log->overflow_err_count = 0;
        log->first_overflow_timestamp = 0;
        log->last_overflow_timestamp = 0;
        log->irq_enabled = false;
        log->irq_vec = start_msg_num++;
        qemu_mutex_init(&log->lock);
        QSIMPLEQ_INIT(&log->events);
    }

    /* Override -- Dynamic Capacity uses the same vector as info */
    cxlds->event_logs[CXL_EVENT_TYPE_DYNAMIC_CAP].irq_vec =
                      cxlds->event_logs[CXL_EVENT_TYPE_INFO].irq_vec;

}

static CXLEvent *cxl_event_get_head(CXLEventLog *log)
{
    return QSIMPLEQ_FIRST(&log->events);
}

static CXLEvent *cxl_event_get_next(CXLEvent *entry)
{
    return QSIMPLEQ_NEXT(entry, node);
}

static int cxl_event_count(CXLEventLog *log)
{
    CXLEvent *event;
    int rc = 0;

    QSIMPLEQ_FOREACH(event, &log->events, node) {
        rc++;
    }

    return rc;
}

static bool cxl_event_empty(CXLEventLog *log)
{
    return QSIMPLEQ_EMPTY(&log->events);
}

static void cxl_event_delete_head(CXLDeviceState *cxlds,
                                  CXLEventLogType log_type,
                                  CXLEventLog *log)
{
    CXLEvent *entry = cxl_event_get_head(log);

    reset_overflow(log);
    QSIMPLEQ_REMOVE_HEAD(&log->events, node);
    if (cxl_event_empty(log)) {
        cxl_event_set_status(cxlds, log_type, false);
    }
    g_free(entry);
}

/*
 * return true if an interrupt should be generated as a result
 * of inserting this event.
 */
bool cxl_event_insert(CXLDeviceState *cxlds, CXLEventLogType log_type,
                      CXLEventRecordRaw *event)
{
    uint64_t time;
    CXLEventLog *log;
    CXLEvent *entry;

    if (log_type >= CXL_EVENT_TYPE_MAX) {
        return false;
    }

    time = cxl_device_get_timestamp(cxlds);

    log = &cxlds->event_logs[log_type];

    QEMU_LOCK_GUARD(&log->lock);

    if (cxl_event_count(log) >= CXL_TEST_EVENT_OVERFLOW) {
        if (log->overflow_err_count == 0) {
            log->first_overflow_timestamp = time;
        }
        log->overflow_err_count++;
        log->last_overflow_timestamp = time;
        return false;
    }

    entry = g_new0(CXLEvent, 1);

    memcpy(&entry->data, event, sizeof(*event));

    entry->data.hdr.handle = cpu_to_le16(log->next_handle);
    log->next_handle++;
    /* 0 handle is never valid */
    if (log->next_handle == 0) {
        log->next_handle++;
    }
    entry->data.hdr.timestamp = cpu_to_le64(time);

    QSIMPLEQ_INSERT_TAIL(&log->events, entry, node);
    cxl_event_set_status(cxlds, log_type, true);

    /* Count went from 0 to 1 */
    return cxl_event_count(log) == 1;
}

void cxl_discard_all_event_records(CXLDeviceState *cxlds)
{
    CXLEventLogType log_type;
    CXLEventLog *log;

    for (log_type = 0; log_type < CXL_EVENT_TYPE_MAX; log_type++) {
        log = &cxlds->event_logs[log_type];
        while (!cxl_event_empty(log)) {
            cxl_event_delete_head(cxlds, log_type, log);
        }
    }
}

CXLRetCode cxl_event_get_records(CXLDeviceState *cxlds, CXLGetEventPayload *pl,
                                 uint8_t log_type, int max_recs,
                                 size_t *len)
{
    CXLEventLog *log;
    CXLEvent *entry;
    uint16_t nr;

    if (log_type >= CXL_EVENT_TYPE_MAX) {
        return CXL_MBOX_INVALID_INPUT;
    }

    log = &cxlds->event_logs[log_type];

    QEMU_LOCK_GUARD(&log->lock);

    entry = cxl_event_get_head(log);
    for (nr = 0; entry && nr < max_recs; nr++) {
        memcpy(&pl->records[nr], &entry->data, CXL_EVENT_RECORD_SIZE);
        entry = cxl_event_get_next(entry);
    }

    if (!cxl_event_empty(log)) {
        pl->flags |= CXL_GET_EVENT_FLAG_MORE_RECORDS;
    }

    if (log->overflow_err_count) {
        pl->flags |= CXL_GET_EVENT_FLAG_OVERFLOW;
        pl->overflow_err_count = cpu_to_le16(log->overflow_err_count);
        pl->first_overflow_timestamp =
            cpu_to_le64(log->first_overflow_timestamp);
        pl->last_overflow_timestamp =
            cpu_to_le64(log->last_overflow_timestamp);
    }

    pl->record_count = cpu_to_le16(nr);
    *len = CXL_EVENT_PAYLOAD_HDR_SIZE + (CXL_EVENT_RECORD_SIZE * nr);

    return CXL_MBOX_SUCCESS;
}

CXLRetCode cxl_event_clear_records(CXLDeviceState *cxlds,
                                   CXLClearEventPayload *pl)
{
    CXLEventLog *log;
    uint8_t log_type;
    CXLEvent *entry;
    int nr;

    log_type = pl->event_log;

    if (log_type >= CXL_EVENT_TYPE_MAX) {
        return CXL_MBOX_INVALID_INPUT;
    }

    log = &cxlds->event_logs[log_type];

    QEMU_LOCK_GUARD(&log->lock);
    /*
     * Must iterate the queue twice.
     * "The device shall verify the event record handles specified in the input
     * payload are in temporal order. If the device detects an older event
     * record that will not be cleared when Clear Event Records is executed,
     * the device shall return the Invalid Handle return code and shall not
     * clear any of the specified event records."
     *   -- CXL r3.1 Section 8.2.9.2.3: Clear Event Records (0101h)
     */
    entry = cxl_event_get_head(log);
    for (nr = 0; entry && nr < pl->nr_recs; nr++) {
        uint16_t handle = pl->handle[nr];

        /* NOTE: Both handles are little endian. */
        if (handle == 0 || entry->data.hdr.handle != handle) {
            return CXL_MBOX_INVALID_INPUT;
        }
        entry = cxl_event_get_next(entry);
    }

    entry = cxl_event_get_head(log);
    for (nr = 0; entry && nr < pl->nr_recs; nr++) {
        cxl_event_delete_head(cxlds, log_type, log);
        entry = cxl_event_get_head(log);
    }

    return CXL_MBOX_SUCCESS;
}

void cxl_event_irq_assert(CXLType3Dev *ct3d)
{
    CXLDeviceState *cxlds = &ct3d->cxl_dstate;
    PCIDevice *pdev = &ct3d->parent_obj;
    int i;

    for (i = 0; i < CXL_EVENT_TYPE_MAX; i++) {
        CXLEventLog *log = &cxlds->event_logs[i];

        if (!log->irq_enabled || cxl_event_empty(log)) {
            continue;
        }

        /*  Notifies interrupt, legacy IRQ is not supported */
        if (msix_enabled(pdev)) {
            msix_notify(pdev, log->irq_vec);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, log->irq_vec);
        }
    }
}

void cxl_create_dc_event_records_for_extents(CXLType3Dev *ct3d,
                                             CXLDCEventType type,
                                             CXLDCExtentRaw extents[],
                                             uint32_t ext_count)
{
    CXLEventDynamicCapacity event_rec = {};
    int i;

    cxl_assign_event_header(&event_rec.hdr,
                            &dynamic_capacity_uuid,
                            (1 << CXL_EVENT_TYPE_INFO),
                            sizeof(event_rec),
                            cxl_device_get_timestamp(&ct3d->cxl_dstate));
    event_rec.type = type;
    event_rec.validity_flags = 1;
    event_rec.host_id = 0;
    event_rec.updated_region_id = 0;
    event_rec.extents_avail = CXL_NUM_EXTENTS_SUPPORTED -
                              ct3d->dc.total_extent_count;

    for (i = 0; i < ext_count; i++) {
        memcpy(&event_rec.dynamic_capacity_extent,
               &extents[i],
               sizeof(CXLDCExtentRaw));
        event_rec.flags = 0;
        if (i < ext_count - 1) {
            /* Set "More" flag */
            event_rec.flags |= BIT(0);
        }

        if (cxl_event_insert(&ct3d->cxl_dstate,
                             CXL_EVENT_TYPE_DYNAMIC_CAP,
                             (CXLEventRecordRaw *)&event_rec)) {
            cxl_event_irq_assert(ct3d);
        }
    }
}
