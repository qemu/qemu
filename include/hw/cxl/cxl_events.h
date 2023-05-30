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

#endif /* CXL_EVENTS_H */
