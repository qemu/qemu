/*
 * Channel subsystem structures and definitions.
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef CSS_H
#define CSS_H

#include "ioinst.h"

/* Channel subsystem constants. */
#define MAX_SCHID 65535
#define MAX_SSID 3
#define MAX_CSSID 254 /* 255 is reserved */
#define MAX_CHPID 255

#define MAX_CIWS 62

typedef struct CIW {
    uint8_t type;
    uint8_t command;
    uint16_t count;
} QEMU_PACKED CIW;

typedef struct SenseId {
    /* common part */
    uint8_t reserved;        /* always 0x'FF' */
    uint16_t cu_type;        /* control unit type */
    uint8_t cu_model;        /* control unit model */
    uint16_t dev_type;       /* device type */
    uint8_t dev_model;       /* device model */
    uint8_t unused;          /* padding byte */
    /* extended part */
    CIW ciw[MAX_CIWS];       /* variable # of CIWs */
} QEMU_PACKED SenseId;

/* Channel measurements, from linux/drivers/s390/cio/cmf.c. */
typedef struct CMB {
    uint16_t ssch_rsch_count;
    uint16_t sample_count;
    uint32_t device_connect_time;
    uint32_t function_pending_time;
    uint32_t device_disconnect_time;
    uint32_t control_unit_queuing_time;
    uint32_t device_active_only_time;
    uint32_t reserved[2];
} QEMU_PACKED CMB;

typedef struct CMBE {
    uint32_t ssch_rsch_count;
    uint32_t sample_count;
    uint32_t device_connect_time;
    uint32_t function_pending_time;
    uint32_t device_disconnect_time;
    uint32_t control_unit_queuing_time;
    uint32_t device_active_only_time;
    uint32_t device_busy_time;
    uint32_t initial_command_response_time;
    uint32_t reserved[7];
} QEMU_PACKED CMBE;

struct SubchDev {
    /* channel-subsystem related things: */
    uint8_t cssid;
    uint8_t ssid;
    uint16_t schid;
    uint16_t devno;
    SCHIB curr_status;
    uint8_t sense_data[32];
    hwaddr channel_prog;
    CCW1 last_cmd;
    bool last_cmd_valid;
    ORB *orb;
    bool thinint_active;
    /* transport-provided data: */
    int (*ccw_cb) (SubchDev *, CCW1);
    SenseId id;
    void *driver_data;
};

typedef SubchDev *(*css_subch_cb_func)(uint8_t m, uint8_t cssid, uint8_t ssid,
                                       uint16_t schid);
int css_create_css_image(uint8_t cssid, bool default_image);
bool css_devno_used(uint8_t cssid, uint8_t ssid, uint16_t devno);
void css_subch_assign(uint8_t cssid, uint8_t ssid, uint16_t schid,
                      uint16_t devno, SubchDev *sch);
void css_sch_build_virtual_schib(SubchDev *sch, uint8_t chpid, uint8_t type);
uint16_t css_build_subchannel_id(SubchDev *sch);
void css_reset(void);
void css_reset_sch(SubchDev *sch);
void css_queue_crw(uint8_t rsc, uint8_t erc, int chain, uint16_t rsid);
void css_generate_sch_crws(uint8_t cssid, uint8_t ssid, uint16_t schid,
                           int hotplugged, int add);
void css_generate_chp_crws(uint8_t cssid, uint8_t chpid);
void css_adapter_interrupt(uint8_t isc);
#endif
