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

#include "cpu.h"
#include "hw/s390x/adapter.h"
#include "hw/s390x/s390_flic.h"
#include "hw/s390x/ioinst.h"

/* Channel subsystem constants. */
#define MAX_DEVNO 65535
#define MAX_SCHID 65535
#define MAX_SSID 3
#define MAX_CSSID 255
#define MAX_CHPID 255

#define MAX_ISC 7

#define MAX_CIWS 62

#define VIRTUAL_CSSID 0xfe
#define VIRTIO_CCW_CHPID 0   /* used by convention */

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
    uint16_t migrated_schid; /* used for missmatch detection */
    ORB orb;
    /* transport-provided data: */
    int (*ccw_cb) (SubchDev *, CCW1);
    void (*disable_cb)(SubchDev *);
    int (*do_subchannel_work) (SubchDev *);
    SenseId id;
    void *driver_data;
};

extern const VMStateDescription vmstate_subch_dev;

/*
 * Identify a device within the channel subsystem.
 * Note that this can be used to identify either the subchannel or
 * the attached I/O device, as there's always one I/O device per
 * subchannel.
 */
typedef struct CssDevId {
    uint8_t cssid;
    uint8_t ssid;
    uint16_t devid;
    bool valid;
} CssDevId;

extern const PropertyInfo css_devid_propinfo;

#define DEFINE_PROP_CSS_DEV_ID(_n, _s, _f) \
    DEFINE_PROP(_n, _s, _f, css_devid_propinfo, CssDevId)

typedef struct IndAddr {
    hwaddr addr;
    uint64_t map;
    unsigned long refcnt;
    int32_t len;
    QTAILQ_ENTRY(IndAddr) sibling;
} IndAddr;

extern const VMStateDescription vmstate_ind_addr;

#define VMSTATE_PTR_TO_IND_ADDR(_f, _s)                                   \
    VMSTATE_STRUCT(_f, _s, 1, vmstate_ind_addr, IndAddr*)

IndAddr *get_indicator(hwaddr ind_addr, int len);
void release_indicator(AdapterInfo *adapter, IndAddr *indicator);
int map_indicator(AdapterInfo *adapter, IndAddr *indicator);

typedef SubchDev *(*css_subch_cb_func)(uint8_t m, uint8_t cssid, uint8_t ssid,
                                       uint16_t schid);
int css_create_css_image(uint8_t cssid, bool default_image);
bool css_devno_used(uint8_t cssid, uint8_t ssid, uint16_t devno);
void css_subch_assign(uint8_t cssid, uint8_t ssid, uint16_t schid,
                      uint16_t devno, SubchDev *sch);
void css_sch_build_virtual_schib(SubchDev *sch, uint8_t chpid, uint8_t type);
int css_sch_build_schib(SubchDev *sch, CssDevId *dev_id);
unsigned int css_find_free_chpid(uint8_t cssid);
uint16_t css_build_subchannel_id(SubchDev *sch);
void copy_scsw_to_guest(SCSW *dest, const SCSW *src);
void css_inject_io_interrupt(SubchDev *sch);
void css_reset(void);
void css_reset_sch(SubchDev *sch);
void css_queue_crw(uint8_t rsc, uint8_t erc, int chain, uint16_t rsid);
void css_generate_sch_crws(uint8_t cssid, uint8_t ssid, uint16_t schid,
                           int hotplugged, int add);
void css_generate_chp_crws(uint8_t cssid, uint8_t chpid);
void css_generate_css_crws(uint8_t cssid);
void css_clear_sei_pending(void);
int s390_ccw_cmd_request(ORB *orb, SCSW *scsw, void *data);
int do_subchannel_work_virtual(SubchDev *sub);
int do_subchannel_work_passthrough(SubchDev *sub);

typedef enum {
    CSS_IO_ADAPTER_VIRTIO = 0,
    CSS_IO_ADAPTER_PCI = 1,
    CSS_IO_ADAPTER_TYPE_NUMS,
} CssIoAdapterType;

void css_adapter_interrupt(CssIoAdapterType type, uint8_t isc);
int css_do_sic(CPUS390XState *env, uint8_t isc, uint16_t mode);
uint32_t css_get_adapter_id(CssIoAdapterType type, uint8_t isc);
void css_register_io_adapters(CssIoAdapterType type, bool swap, bool maskable,
                              uint8_t flags, Error **errp);

#ifndef CONFIG_KVM
#define S390_ADAPTER_SUPPRESSIBLE 0x01
#else
#define S390_ADAPTER_SUPPRESSIBLE KVM_S390_ADAPTER_SUPPRESSIBLE
#endif

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

extern const PropertyInfo css_devid_ro_propinfo;

#define DEFINE_PROP_CSS_DEV_ID_RO(_n, _s, _f) \
    DEFINE_PROP(_n, _s, _f, css_devid_ro_propinfo, CssDevId)

/**
 * Create a subchannel for the given bus id.
 *
 * If @p bus_id is valid, and @p squash_mcss is true, verify that it is
 * not already in use in the default css, and find a free devno from the
 * default css image for it.
 * If @p bus_id is valid, and @p squash_mcss is false, verify that it is
 * not already in use, and find a free devno for it.
 * If @p bus_id is not valid, and if either @p squash_mcss or @p is_virtual
 * is true, find a free subchannel id and device number across all
 * subchannel sets from the default css image.
 * If @p bus_id is not valid, and if both @p squash_mcss and @p is_virtual
 * are false, find a non-full css image and find a free subchannel id and
 * device number across all subchannel sets from it.
 *
 * If either of the former actions succeed, allocate a subchannel structure,
 * initialise it with the bus id, subchannel id and device number, register
 * it with the CSS and return it. Otherwise return NULL.
 *
 * The caller becomes owner of the returned subchannel structure and
 * is responsible for unregistering and freeing it.
 */
SubchDev *css_create_sch(CssDevId bus_id, bool is_virtual, bool squash_mcss,
                         Error **errp);

/** Turn on css migration */
void css_register_vmstate(void);

#endif
