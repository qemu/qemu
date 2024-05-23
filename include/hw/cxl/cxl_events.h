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
 * CXL r3.1 section 8.2.9.2.2: Get Event Records (Opcode 0100h); Table 8-52
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
 * CXL r3.1 section 8.2.9.2.1: Event Records; Table 8-43
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
 * CXL r3.1 section 8.2.9.2.2; Table 8-53
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
 * CXL r3.1 section 8.2.9.2.3; Table 8-54
 */
typedef struct CXLClearEventPayload {
    uint8_t event_log;      /* CXLEventLogType */
    uint8_t clear_flags;
    uint8_t nr_recs;
    uint8_t reserved[3];
    uint16_t handle[];
} CXLClearEventPayload;

/*
 * Event Interrupt Policy
 *
 * CXL r3.1 section 8.2.9.2.4; Table 8-55
 */
typedef enum CXLEventIntMode {
    CXL_INT_NONE     = 0x00,
    CXL_INT_MSI_MSIX = 0x01,
    CXL_INT_FW       = 0x02,
    CXL_INT_RES      = 0x03,
} CXLEventIntMode;
#define CXL_EVENT_INT_MODE_MASK 0x3
#define CXL_EVENT_INT_SETTING(vector) \
    ((((uint8_t)vector & 0xf) << 4) | CXL_INT_MSI_MSIX)
typedef struct CXLEventInterruptPolicy {
    uint8_t info_settings;
    uint8_t warn_settings;
    uint8_t failure_settings;
    uint8_t fatal_settings;
    uint8_t dyn_cap_settings;
} QEMU_PACKED CXLEventInterruptPolicy;
/* DCD is optional but other fields are not */
#define CXL_EVENT_INT_SETTING_MIN_LEN 4

/*
 * General Media Event Record
 * CXL r3.1 Section 8.2.9.2.1.1; Table 8-45
 */
#define CXL_EVENT_GEN_MED_COMP_ID_SIZE  0x10
#define CXL_EVENT_GEN_MED_RES_SIZE      0x2e
typedef struct CXLEventGenMedia {
    CXLEventRecordHdr hdr;
    uint64_t phys_addr;
    uint8_t descriptor;
    uint8_t type;
    uint8_t transaction_type;
    uint16_t validity_flags;
    uint8_t channel;
    uint8_t rank;
    uint8_t device[3];
    uint8_t component_id[CXL_EVENT_GEN_MED_COMP_ID_SIZE];
    uint8_t reserved[CXL_EVENT_GEN_MED_RES_SIZE];
} QEMU_PACKED CXLEventGenMedia;

/*
 * DRAM Event Record
 * CXL r3.1 Section 8.2.9.2.1.2: Table 8-46
 * All fields little endian.
 */
typedef struct CXLEventDram {
    CXLEventRecordHdr hdr;
    uint64_t phys_addr;
    uint8_t descriptor;
    uint8_t type;
    uint8_t transaction_type;
    uint16_t validity_flags;
    uint8_t channel;
    uint8_t rank;
    uint8_t nibble_mask[3];
    uint8_t bank_group;
    uint8_t bank;
    uint8_t row[3];
    uint16_t column;
    uint64_t correction_mask[4];
    uint8_t reserved[0x17];
} QEMU_PACKED CXLEventDram;

/*
 * Memory Module Event Record
 * CXL r3.1 Section 8.2.9.2.1.3: Table 8-47
 * All fields little endian.
 */
typedef struct CXLEventMemoryModule {
    CXLEventRecordHdr hdr;
    uint8_t type;
    uint8_t health_status;
    uint8_t media_status;
    uint8_t additional_status;
    uint8_t life_used;
    int16_t temperature;
    uint32_t dirty_shutdown_count;
    uint32_t corrected_volatile_error_count;
    uint32_t corrected_persistent_error_count;
    uint8_t reserved[0x3d];
} QEMU_PACKED CXLEventMemoryModule;

/*
 * CXL r3.1 section Table 8-50: Dynamic Capacity Event Record
 * All fields little endian.
 */
typedef struct CXLEventDynamicCapacity {
    CXLEventRecordHdr hdr;
    uint8_t type;
    uint8_t validity_flags;
    uint16_t host_id;
    uint8_t updated_region_id;
    uint8_t flags;
    uint8_t reserved2[2];
    uint8_t dynamic_capacity_extent[0x28]; /* defined in cxl_device.h */
    uint8_t reserved[0x18];
    uint32_t extents_avail;
    uint32_t tags_avail;
} QEMU_PACKED CXLEventDynamicCapacity;

#endif /* CXL_EVENTS_H */
