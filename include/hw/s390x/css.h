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

#include "hw/s390x/adapter.h"
#include "hw/s390x/s390_flic.h"
#include "hw/s390x/ioinst.h"

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

typedef struct SubchDev SubchDev;
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
    bool ccw_fmt_1;
    bool thinint_active;
    uint8_t ccw_no_data_cnt;
    /* transport-provided data: */
    int (*ccw_cb) (SubchDev *, CCW1);
    void (*disable_cb)(SubchDev *);
    SenseId id;
    void *driver_data;
};

typedef struct IndAddr {
    hwaddr addr;
    uint64_t map;
    unsigned long refcnt;
    int len;
    QTAILQ_ENTRY(IndAddr) sibling;
} IndAddr;

IndAddr *get_indicator(hwaddr ind_addr, int len);
void release_indicator(AdapterInfo *adapter, IndAddr *indicator);
int map_indicator(AdapterInfo *adapter, IndAddr *indicator);

typedef SubchDev *(*css_subch_cb_func)(uint8_t m, uint8_t cssid, uint8_t ssid,
                                       uint16_t schid);
void subch_device_save(SubchDev *s, QEMUFile *f);
int subch_device_load(SubchDev *s, QEMUFile *f);
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
void css_generate_css_crws(uint8_t cssid);
void css_clear_sei_pending(void);
void css_adapter_interrupt(uint8_t isc);

#define CSS_IO_ADAPTER_VIRTIO 1
int css_register_io_adapter(uint8_t type, uint8_t isc, bool swap,
                            bool maskable, uint32_t *id);

#ifndef CONFIG_USER_ONLY
SubchDev *css_find_subch(uint8_t m, uint8_t cssid, uint8_t ssid,
                         uint16_t schid);
bool css_subch_visible(SubchDev *sch);
void css_conditional_io_interrupt(SubchDev *sch);
int css_do_stsch(SubchDev *sch, SCHIB *schib);
bool css_schid_final(int m, uint8_t cssid, uint8_t ssid, uint16_t schid);
int css_do_msch(SubchDev *sch, const SCHIB *schib);
int css_do_xsch(SubchDev *sch);
int css_do_csch(SubchDev *sch);
int css_do_hsch(SubchDev *sch);
int css_do_ssch(SubchDev *sch, ORB *orb);
int css_do_tsch_get_irb(SubchDev *sch, IRB *irb, int *irb_len);
void css_do_tsch_update_subch(SubchDev *sch);
int css_do_stcrw(CRW *crw);
void css_undo_stcrw(CRW *crw);
int css_do_tpi(IOIntCode *int_code, int lowcore);
int css_collect_chp_desc(int m, uint8_t cssid, uint8_t f_chpid, uint8_t l_chpid,
                         int rfmt, void *buf);
void css_do_schm(uint8_t mbk, int update, int dct, uint64_t mbo);
int css_enable_mcsse(void);
int css_enable_mss(void);
int css_do_rsch(SubchDev *sch);
int css_do_rchp(uint8_t cssid, uint8_t chpid);
bool css_present(uint8_t cssid);
#endif

#endif
