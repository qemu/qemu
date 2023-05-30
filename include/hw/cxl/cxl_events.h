/*
 * QEMU CXL Events
 *
 * Copyright (c) 2022 Intel
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#ifndef CXL_EVENTS_H
#define CXL_EVENTS_H

#include "qemu/uuid.h"

/*
 * CXL rev 3.0 section 8.2.9.2.2; Table 8-49
 *
 * Define these as the bit position for the event status register for ease of
 * setting the status.
 */
typedef enum CXLEventLogType {
    CXL_EVENT_TYPE_INFO          = 0,
    CXL_EVENT_TYPE_WARN          = 1,
    CXL_EVENT_TYPE_FAIL          = 2,
    CXL_EVENT_TYPE_FATAL         = 3,
    CXL_EVENT_TYPE_DYNAMIC_CAP   = 4,
    CXL_EVENT_TYPE_MAX
} CXLEventLogType;

/*
 * Common Event Record Format
 * CXL rev 3.0 section 8.2.9.2.1; Table 8-42
 */
#define CXL_EVENT_REC_HDR_RES_LEN 0xf
typedef struct CXLEventRecordHdr {
    QemuUUID id;
    uint8_t length;
    uint8_t flags[3];
    uint16_t handle;
    uint16_t related_handle;
    uint64_t timestamp;
    uint8_t maint_op_class;
    uint8_t reserved[CXL_EVENT_REC_HDR_RES_LEN];
} QEMU_PACKED CXLEventRecordHdr;

#define CXL_EVENT_RECORD_DATA_LENGTH 0x50
typedef struct CXLEventRecordRaw {
    CXLEventRecordHdr hdr;
    uint8_t data[CXL_EVENT_RECORD_DATA_LENGTH];
} QEMU_PACKED CXLEventRecordRaw;
#define CXL_EVENT_RECORD_SIZE (sizeof(CXLEventRecordRaw))

/*
 * Get Event Records output payload
 * CXL rev 3.0 section 8.2.9.2.2; Table 8-50
 */
#define CXL_GET_EVENT_FLAG_OVERFLOW     BIT(0)
#define CXL_GET_EVENT_FLAG_MORE_RECORDS BIT(1)
typedef struct CXLGetEventPayload {
    uint8_t flags;
    uint8_t reserved1;
    uint16_t overflow_err_count;
    uint64_t first_overflow_timestamp;
    uint64_t last_overflow_timestamp;
    uint16_t record_count;
    uint8_t reserved2[0xa];
    CXLEventRecordRaw records[];
} QEMU_PACKED CXLGetEventPayload;
#define CXL_EVENT_PAYLOAD_HDR_SIZE (sizeof(CXLGetEventPayload))

/*
 * Clear Event Records input payload
 * CXL rev 3.0 section 8.2.9.2.3; Table 8-51
 */
typedef struct CXLClearEventPayload {
    uint8_t event_log;      /* CXLEventLogType */
    uint8_t clear_flags;
    uint8_t nr_recs;
    uint8_t reserved[3];
    uint16_t handle[];
} CXLClearEventPayload;

#endif /* CXL_EVENTS_H */
