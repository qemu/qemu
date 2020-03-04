/*
 * SCLP ASCII access driver
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef SCLP_H
#define SCLP_H

/* SCLP command codes */
#define SCLP_CMDW_READ_SCP_INFO                 0x00020001
#define SCLP_CMDW_READ_SCP_INFO_FORCED          0x00120001
#define SCLP_CMD_READ_EVENT_DATA                0x00770005
#define SCLP_CMD_WRITE_EVENT_DATA               0x00760005
#define SCLP_CMD_READ_EVENT_DATA                0x00770005
#define SCLP_CMD_WRITE_EVENT_DATA               0x00760005
#define SCLP_CMD_WRITE_EVENT_MASK               0x00780005

/* SCLP response codes */
#define SCLP_RC_NORMAL_READ_COMPLETION          0x0010
#define SCLP_RC_NORMAL_COMPLETION               0x0020
#define SCLP_RC_INVALID_SCLP_COMMAND            0x01f0
#define SCLP_RC_CONTAINED_EQUIPMENT_CHECK       0x0340
#define SCLP_RC_INSUFFICIENT_SCCB_LENGTH        0x0300
#define SCLP_RC_INVALID_FUNCTION                0x40f0
#define SCLP_RC_NO_EVENT_BUFFERS_STORED         0x60f0
#define SCLP_RC_INVALID_SELECTION_MASK          0x70f0
#define SCLP_RC_INCONSISTENT_LENGTHS            0x72f0
#define SCLP_RC_EVENT_BUFFER_SYNTAX_ERROR       0x73f0
#define SCLP_RC_INVALID_MASK_LENGTH             0x74f0

/* Service Call Control Block (SCCB) and its elements */

#define SCCB_SIZE 4096

#define SCLP_VARIABLE_LENGTH_RESPONSE           0x80
#define SCLP_EVENT_BUFFER_ACCEPTED              0x80

#define SCLP_FC_NORMAL_WRITE                    0

typedef struct SCCBHeader {
    uint16_t length;
    uint8_t function_code;
    uint8_t control_mask[3];
    uint16_t response_code;
} __attribute__((packed)) SCCBHeader;

#define SCCB_DATA_LEN (SCCB_SIZE - sizeof(SCCBHeader))

typedef struct ReadInfo {
    SCCBHeader h;
    uint16_t rnmax;
    uint8_t rnsize;
    uint8_t reserved[13];
    uint8_t loadparm[LOADPARM_LEN];
} __attribute__((packed)) ReadInfo;

typedef struct SCCB {
    SCCBHeader h;
    char data[SCCB_DATA_LEN];
 } __attribute__((packed)) SCCB;

/* SCLP event types */
#define SCLP_EVENT_ASCII_CONSOLE_DATA           0x1a
#define SCLP_EVENT_SIGNAL_QUIESCE               0x1d

/* SCLP event masks */
#define SCLP_EVENT_MASK_SIGNAL_QUIESCE          0x00000008
#define SCLP_EVENT_MASK_MSG_ASCII               0x00000040

#define SCLP_UNCONDITIONAL_READ                 0x00
#define SCLP_SELECTIVE_READ                     0x01

typedef struct WriteEventMask {
    SCCBHeader h;
    uint16_t _reserved;
    uint16_t mask_length;
    uint32_t cp_receive_mask;
    uint32_t cp_send_mask;
    uint32_t send_mask;
    uint32_t receive_mask;
} __attribute__((packed)) WriteEventMask;

typedef struct EventBufferHeader {
    uint16_t length;
    uint8_t  type;
    uint8_t  flags;
    uint16_t _reserved;
} __attribute__((packed)) EventBufferHeader;

typedef struct WriteEventData {
    SCCBHeader h;
    EventBufferHeader ebh;
    char data[];
} __attribute__((packed)) WriteEventData;

typedef struct ReadEventData {
    SCCBHeader h;
    EventBufferHeader ebh;
    uint32_t mask;
} __attribute__((packed)) ReadEventData;

#define __pa(x) (x)

#endif /* SCLP_H */
