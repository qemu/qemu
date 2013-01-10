/*
 * SCLP
 *    Event Facility definitions
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Heinz Graalfs <graalfs@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_S390_SCLP_EVENT_FACILITY_H
#define HW_S390_SCLP_EVENT_FACILITY_H

#include <hw/qdev.h>
#include "qemu/thread.h"

/* SCLP event types */
#define SCLP_EVENT_ASCII_CONSOLE_DATA           0x1a
#define SCLP_EVENT_SIGNAL_QUIESCE               0x1d

/* SCLP event masks */
#define SCLP_EVENT_MASK_SIGNAL_QUIESCE          0x00000008
#define SCLP_EVENT_MASK_MSG_ASCII               0x00000040

#define SCLP_UNCONDITIONAL_READ                 0x00
#define SCLP_SELECTIVE_READ                     0x01

#define TYPE_SCLP_EVENT "s390-sclp-event-type"
#define SCLP_EVENT(obj) \
     OBJECT_CHECK(SCLPEvent, (obj), TYPE_SCLP_EVENT)
#define SCLP_EVENT_CLASS(klass) \
     OBJECT_CLASS_CHECK(SCLPEventClass, (klass), TYPE_SCLP_EVENT)
#define SCLP_EVENT_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SCLPEventClass, (obj), TYPE_SCLP_EVENT)

typedef struct WriteEventMask {
    SCCBHeader h;
    uint16_t _reserved;
    uint16_t mask_length;
    uint32_t cp_receive_mask;
    uint32_t cp_send_mask;
    uint32_t send_mask;
    uint32_t receive_mask;
} QEMU_PACKED WriteEventMask;

typedef struct EventBufferHeader {
    uint16_t length;
    uint8_t  type;
    uint8_t  flags;
    uint16_t _reserved;
} QEMU_PACKED EventBufferHeader;

typedef struct WriteEventData {
    SCCBHeader h;
    EventBufferHeader ebh;
} QEMU_PACKED WriteEventData;

typedef struct ReadEventData {
    SCCBHeader h;
    EventBufferHeader ebh;
    uint32_t mask;
} QEMU_PACKED ReadEventData;

typedef struct SCLPEvent {
    DeviceState qdev;
    bool event_pending;
    uint32_t event_type;
    char *name;
} SCLPEvent;

typedef struct SCLPEventClass {
    DeviceClass parent_class;
    int (*init)(SCLPEvent *event);
    int (*exit)(SCLPEvent *event);

    /* get SCLP's send mask */
    unsigned int (*get_send_mask)(void);

    /* get SCLP's receive mask */
    unsigned int (*get_receive_mask)(void);

    int (*read_event_data)(SCLPEvent *event, EventBufferHeader *evt_buf_hdr,
                           int *slen);

    int (*write_event_data)(SCLPEvent *event, EventBufferHeader *evt_buf_hdr);

    /* returns the supported event type */
    int (*event_type)(void);

} SCLPEventClass;

#endif
