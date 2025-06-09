/*
 * ap bridge
 *
 * Copyright 2018 IBM Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390X_AP_BRIDGE_H
#define HW_S390X_AP_BRIDGE_H

#define TYPE_AP_BRIDGE "ap-bridge"
#define TYPE_AP_BUS "ap-bus"

void s390_init_ap(void);

typedef struct ChscSeiNt0Res {
    uint16_t length;
    uint16_t code;
    uint8_t reserved1;
    uint16_t reserved2;
    uint8_t nt;
#define PENDING_EVENT_INFO_BITMASK 0x80;
    uint8_t flags;
    uint8_t reserved3;
    uint8_t rs;
    uint8_t cc;
} QEMU_PACKED ChscSeiNt0Res;

#define NT0_RES_RESPONSE_CODE 1
#define NT0_RES_NT_DEFAULT    0
#define NT0_RES_RS_AP_CHANGE  5
#define NT0_RES_CC_AP_CHANGE  3

#define EVENT_INFORMATION_NOT_STORED 1
#define EVENT_INFORMATION_STORED     0

/**
 * ap_chsc_sei_nt0_get_event - Retrieve the next pending AP config
 * change event
 * @res: Pointer to a ChscSeiNt0Res struct to be filled with event
 * data
 *
 * This function checks for any pending AP config change events and,
 * if present, populates the provided response structure with the
 * appropriate SEI NT0 fields.
 *
 * Return:
 *   EVENT_INFORMATION_STORED - An event was available and written to @res
 *   EVENT_INFORMATION_NOT_STORED - No event was available
 */
int ap_chsc_sei_nt0_get_event(void *res);

bool ap_chsc_sei_nt0_have_event(void);

#endif
