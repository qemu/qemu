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

#include "hw/qdev.h"
#include "qemu/thread.h"
#include "hw/s390x/sclp.h"

/* SCLP event types */
#define SCLP_EVENT_OPRTNS_COMMAND               0x01
#define SCLP_EVENT_MESSAGE                      0x02
#define SCLP_EVENT_CONFIG_MGT_DATA              0x04
#define SCLP_EVENT_PMSGCMD                      0x09
#define SCLP_EVENT_ASCII_CONSOLE_DATA           0x1a
#define SCLP_EVENT_SIGNAL_QUIESCE               0x1d

/* SCLP event masks */
#define SCLP_EVMASK(T)  (1ULL << (sizeof(sccb_mask_t) * 8 - (T)))

#define SCLP_EVENT_MASK_OP_CMD          SCLP_EVMASK(SCLP_EVENT_OPRTNS_COMMAND)
#define SCLP_EVENT_MASK_MSG             SCLP_EVMASK(SCLP_EVENT_MESSAGE)
#define SCLP_EVENT_MASK_CONFIG_MGT_DATA SCLP_EVMASK(SCLP_EVENT_CONFIG_MGT_DATA)
#define SCLP_EVENT_MASK_PMSGCMD         SCLP_EVMASK(SCLP_EVENT_PMSGCMD)
#define SCLP_EVENT_MASK_MSG_ASCII       SCLP_EVMASK(SCLP_EVENT_ASCII_CONSOLE_DATA)
#define SCLP_EVENT_MASK_SIGNAL_QUIESCE  SCLP_EVMASK(SCLP_EVENT_SIGNAL_QUIESCE)

#define SCLP_UNCONDITIONAL_READ                 0x00
#define SCLP_SELECTIVE_READ                     0x01

#define TYPE_SCLP_EVENT "s390-sclp-event-type"
#define SCLP_EVENT(obj) \
     OBJECT_CHECK(SCLPEvent, (obj), TYPE_SCLP_EVENT)
#define SCLP_EVENT_CLASS(klass) \
     OBJECT_CLASS_CHECK(SCLPEventClass, (klass), TYPE_SCLP_EVENT)
#define SCLP_EVENT_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SCLPEventClass, (obj), TYPE_SCLP_EVENT)

#define TYPE_SCLP_CPU_HOTPLUG "sclp-cpu-hotplug"
#define TYPE_SCLP_QUIESCE "sclpquiesce"

#define SCLP_EVENT_MASK_LEN_MAX 1021

typedef struct WriteEventMask {
    SCCBHeader h;
    uint16_t _reserved;
    uint16_t mask_length;
    uint8_t masks[];
/*
 * Layout of the masks is
 *  uint8_t cp_receive_mask[mask_length];
 *  uint8_t cp_send_mask[mask_length];
 *  uint8_t receive_mask[mask_length];
 *  uint8_t send_mask[mask_length];
 * where 1 <= mask_length <= SCLP_EVENT_MASK_LEN_MAX
 */
} QEMU_PACKED WriteEventMask;

#define WEM_CP_RECEIVE_MASK(wem, mask_len) ((wem)->masks)
#define WEM_CP_SEND_MASK(wem, mask_len) ((wem)->masks + (mask_len))
#define WEM_RECEIVE_MASK(wem, mask_len) ((wem)->masks + 2 * (mask_len))
#define WEM_SEND_MASK(wem, mask_len) ((wem)->masks + 3 * (mask_len))

typedef uint64_t sccb_mask_t;

typedef struct EventBufferHeader {
    uint16_t length;
    uint8_t  type;
    uint8_t  flags;
    uint16_t _reserved;
} QEMU_PACKED EventBufferHeader;

typedef struct MdbHeader {
    uint16_t length;
    uint16_t type;
    uint32_t tag;
    uint32_t revision_code;
} QEMU_PACKED MdbHeader;

typedef struct MTO {
    uint16_t line_type_flags;
    uint8_t  alarm_control;
    uint8_t  _reserved[3];
    char     message[];
} QEMU_PACKED MTO;

typedef struct GO {
    uint32_t domid;
    uint8_t  hhmmss_time[8];
    uint8_t  th_time[3];
    uint8_t  _reserved_0;
    uint8_t  dddyyyy_date[7];
    uint8_t  _reserved_1;
    uint16_t general_msg_flags;
    uint8_t  _reserved_2[10];
    uint8_t  originating_system_name[8];
    uint8_t  job_guest_name[8];
} QEMU_PACKED GO;

#define MESSAGE_TEXT 0x0004

typedef struct MDBO {
    uint16_t length;
    uint16_t type;
    union {
        GO go;
        MTO mto;
    };
} QEMU_PACKED MDBO;

typedef struct MDB {
    MdbHeader header;
    MDBO mdbo[0];
} QEMU_PACKED MDB;

typedef struct SclpMsg {
    EventBufferHeader header;
    MDB mdb;
} QEMU_PACKED SclpMsg;

#define GDS_ID_MDSMU                            0x1310
#define GDS_ID_CPMSU                            0x1212
#define GDS_ID_TEXTCMD                          0x1320

typedef struct GdsVector {
    uint16_t length;
    uint16_t gds_id;
} QEMU_PACKED GdsVector;

#define GDS_KEY_SELFDEFTEXTMSG                  0x31
#define GDS_KEY_TEXTMSG                         0x30

typedef struct GdsSubvector {
    uint8_t length;
    uint8_t key;
} QEMU_PACKED GdsSubvector;

/* MDS Message Unit */
typedef struct MDMSU {
    GdsVector mdmsu;
    GdsVector cpmsu;
    GdsVector text_command;
    GdsSubvector self_def_text_message;
    GdsSubvector text_message;
} QEMU_PACKED MDMSU;

typedef struct WriteEventData {
    SCCBHeader h;
    EventBufferHeader ebh;
} QEMU_PACKED WriteEventData;

typedef struct ReadEventData {
    SCCBHeader h;
    union {
        sccb_mask_t mask;
        EventBufferHeader ebh;
    };
} QEMU_PACKED ReadEventData;

typedef struct SCLPEvent {
    DeviceState qdev;
    bool event_pending;
    char *name;
} SCLPEvent;

typedef struct SCLPEventClass {
    DeviceClass parent_class;
    int (*init)(SCLPEvent *event);

    /* get SCLP's send mask */
    sccb_mask_t (*get_send_mask)(void);

    /* get SCLP's receive mask */
    sccb_mask_t (*get_receive_mask)(void);

    int (*read_event_data)(SCLPEvent *event, EventBufferHeader *evt_buf_hdr,
                           int *slen);

    int (*write_event_data)(SCLPEvent *event, EventBufferHeader *evt_buf_hdr);

    /* can we handle this event type? */
    bool (*can_handle_event)(uint8_t type);
} SCLPEventClass;

#define TYPE_SCLP_EVENT_FACILITY "s390-sclp-event-facility"
#define EVENT_FACILITY(obj) \
     OBJECT_CHECK(SCLPEventFacility, (obj), TYPE_SCLP_EVENT_FACILITY)
#define EVENT_FACILITY_CLASS(klass) \
     OBJECT_CLASS_CHECK(SCLPEventFacilityClass, (klass), \
                        TYPE_SCLP_EVENT_FACILITY)
#define EVENT_FACILITY_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SCLPEventFacilityClass, (obj), \
                      TYPE_SCLP_EVENT_FACILITY)

typedef struct SCLPEventFacilityClass {
    SysBusDeviceClass parent_class;
    void (*command_handler)(SCLPEventFacility *ef, SCCB *sccb, uint64_t code);
    bool (*event_pending)(SCLPEventFacility *ef);
} SCLPEventFacilityClass;

BusState *sclp_get_event_facility_bus(void);

#endif
