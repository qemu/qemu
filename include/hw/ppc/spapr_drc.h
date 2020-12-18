/*
 * QEMU SPAPR Dynamic Reconfiguration Connector Implementation
 *
 * Copyright IBM Corp. 2014
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_SPAPR_DRC_H
#define HW_SPAPR_DRC_H

#include <libfdt.h>
#include "qom/object.h"
#include "sysemu/runstate.h"
#include "hw/qdev-core.h"
#include "qapi/error.h"

#define TYPE_SPAPR_DR_CONNECTOR "spapr-dr-connector"
#define SPAPR_DR_CONNECTOR_GET_CLASS(obj) \
        OBJECT_GET_CLASS(SpaprDrcClass, obj, TYPE_SPAPR_DR_CONNECTOR)
#define SPAPR_DR_CONNECTOR_CLASS(klass) \
        OBJECT_CLASS_CHECK(SpaprDrcClass, klass, \
                           TYPE_SPAPR_DR_CONNECTOR)
#define SPAPR_DR_CONNECTOR(obj) OBJECT_CHECK(SpaprDrc, (obj), \
                                             TYPE_SPAPR_DR_CONNECTOR)

#define TYPE_SPAPR_DRC_PHYSICAL "spapr-drc-physical"
#define SPAPR_DRC_PHYSICAL(obj) OBJECT_CHECK(SpaprDrcPhysical, (obj), \
                                             TYPE_SPAPR_DRC_PHYSICAL)

#define TYPE_SPAPR_DRC_LOGICAL "spapr-drc-logical"

#define TYPE_SPAPR_DRC_CPU "spapr-drc-cpu"

#define TYPE_SPAPR_DRC_PCI "spapr-drc-pci"

#define TYPE_SPAPR_DRC_LMB "spapr-drc-lmb"

#define TYPE_SPAPR_DRC_PHB "spapr-drc-phb"

#define TYPE_SPAPR_DRC_PMEM "spapr-drc-pmem"

/*
 * Various hotplug types managed by SpaprDrc
 *
 * these are somewhat arbitrary, but to make things easier
 * when generating DRC indexes later we've aligned the bit
 * positions with the values used to assign DRC indexes on
 * pSeries. we use those values as bit shifts to allow for
 * the OR'ing of these values in various QEMU routines, but
 * for values exposed to the guest (via DRC indexes for
 * instance) we will use the shift amounts.
 */
typedef enum {
    SPAPR_DR_CONNECTOR_TYPE_SHIFT_CPU = 1,
    SPAPR_DR_CONNECTOR_TYPE_SHIFT_PHB = 2,
    SPAPR_DR_CONNECTOR_TYPE_SHIFT_VIO = 3,
    SPAPR_DR_CONNECTOR_TYPE_SHIFT_PCI = 4,
    SPAPR_DR_CONNECTOR_TYPE_SHIFT_LMB = 8,
    SPAPR_DR_CONNECTOR_TYPE_SHIFT_PMEM = 9,
} SpaprDrcTypeShift;

typedef enum {
    SPAPR_DR_CONNECTOR_TYPE_ANY = ~0,
    SPAPR_DR_CONNECTOR_TYPE_CPU = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_CPU,
    SPAPR_DR_CONNECTOR_TYPE_PHB = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_PHB,
    SPAPR_DR_CONNECTOR_TYPE_VIO = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_VIO,
    SPAPR_DR_CONNECTOR_TYPE_PCI = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_PCI,
    SPAPR_DR_CONNECTOR_TYPE_LMB = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_LMB,
    SPAPR_DR_CONNECTOR_TYPE_PMEM = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_PMEM,
} SpaprDrcType;

/*
 * set via set-indicator RTAS calls
 * as documented by PAPR+ 2.7 13.5.3.4, Table 177
 *
 * isolated: put device under firmware control
 * unisolated: claim OS control of device (may or may not be in use)
 */
typedef enum {
    SPAPR_DR_ISOLATION_STATE_ISOLATED   = 0,
    SPAPR_DR_ISOLATION_STATE_UNISOLATED = 1
} SpaprDRIsolationState;

/*
 * set via set-indicator RTAS calls
 * as documented by PAPR+ 2.7 13.5.3.4, Table 177
 *
 * unusable: mark device as unavailable to OS
 * usable: mark device as available to OS
 * exchange: (currently unused)
 * recover: (currently unused)
 */
typedef enum {
    SPAPR_DR_ALLOCATION_STATE_UNUSABLE  = 0,
    SPAPR_DR_ALLOCATION_STATE_USABLE    = 1,
    SPAPR_DR_ALLOCATION_STATE_EXCHANGE  = 2,
    SPAPR_DR_ALLOCATION_STATE_RECOVER   = 3
} SpaprDRAllocationState;

/*
 * DR-indicator (LED/visual indicator)
 *
 * set via set-indicator RTAS calls
 * as documented by PAPR+ 2.7 13.5.3.4, Table 177,
 * and PAPR+ 2.7 13.5.4.1, Table 180
 *
 * inactive: hotpluggable entity inactive and safely removable
 * active: hotpluggable entity in use and not safely removable
 * identify: (currently unused)
 * action: (currently unused)
 */
typedef enum {
    SPAPR_DR_INDICATOR_INACTIVE   = 0,
    SPAPR_DR_INDICATOR_ACTIVE     = 1,
    SPAPR_DR_INDICATOR_IDENTIFY   = 2,
    SPAPR_DR_INDICATOR_ACTION     = 3,
} SpaprDRIndicatorState;

/*
 * returned via get-sensor-state RTAS calls
 * as documented by PAPR+ 2.7 13.5.3.3, Table 175:
 *
 * empty: connector slot empty (e.g. empty hotpluggable PCI slot)
 * present: connector slot populated and device available to OS
 * unusable: device not currently available to OS
 * exchange: (currently unused)
 * recover: (currently unused)
 */
typedef enum {
    SPAPR_DR_ENTITY_SENSE_EMPTY     = 0,
    SPAPR_DR_ENTITY_SENSE_PRESENT   = 1,
    SPAPR_DR_ENTITY_SENSE_UNUSABLE  = 2,
    SPAPR_DR_ENTITY_SENSE_EXCHANGE  = 3,
    SPAPR_DR_ENTITY_SENSE_RECOVER   = 4,
} SpaprDREntitySense;

typedef enum {
    SPAPR_DR_CC_RESPONSE_NEXT_SIB         = 1, /* currently unused */
    SPAPR_DR_CC_RESPONSE_NEXT_CHILD       = 2,
    SPAPR_DR_CC_RESPONSE_NEXT_PROPERTY    = 3,
    SPAPR_DR_CC_RESPONSE_PREV_PARENT      = 4,
    SPAPR_DR_CC_RESPONSE_SUCCESS          = 0,
    SPAPR_DR_CC_RESPONSE_ERROR            = -1,
    SPAPR_DR_CC_RESPONSE_CONTINUE         = -2,
    SPAPR_DR_CC_RESPONSE_NOT_CONFIGURABLE = -9003,
} SpaprDRCCResponse;

typedef enum {
    /*
     * Values come from Fig. 12 in LoPAPR section 13.4
     *
     * These are exposed in the migration stream, so don't change
     * them.
     */
    SPAPR_DRC_STATE_INVALID             = 0,
    SPAPR_DRC_STATE_LOGICAL_UNUSABLE    = 1,
    SPAPR_DRC_STATE_LOGICAL_AVAILABLE   = 2,
    SPAPR_DRC_STATE_LOGICAL_UNISOLATE   = 3,
    SPAPR_DRC_STATE_LOGICAL_CONFIGURED  = 4,
    SPAPR_DRC_STATE_PHYSICAL_AVAILABLE  = 5,
    SPAPR_DRC_STATE_PHYSICAL_POWERON    = 6,
    SPAPR_DRC_STATE_PHYSICAL_UNISOLATE  = 7,
    SPAPR_DRC_STATE_PHYSICAL_CONFIGURED = 8,
} SpaprDrcState;

typedef struct SpaprDrc {
    /*< private >*/
    DeviceState parent;

    uint32_t id;
    Object *owner;

    uint32_t state;

    /* RTAS ibm,configure-connector state */
    /* (only valid in UNISOLATE state) */
    int ccs_offset;
    int ccs_depth;

    /* device pointer, via link property */
    DeviceState *dev;
    bool unplug_requested;
    void *fdt;
    int fdt_start_offset;
} SpaprDrc;

struct SpaprMachineState;

typedef struct SpaprDrcClass {
    /*< private >*/
    DeviceClass parent;
    SpaprDrcState empty_state;
    SpaprDrcState ready_state;

    /*< public >*/
    SpaprDrcTypeShift typeshift;
    const char *typename; /* used in device tree, PAPR 13.5.2.6 & C.6.1 */
    const char *drc_name_prefix; /* used other places in device tree */

    SpaprDREntitySense (*dr_entity_sense)(SpaprDrc *drc);
    uint32_t (*isolate)(SpaprDrc *drc);
    uint32_t (*unisolate)(SpaprDrc *drc);
    void (*release)(DeviceState *dev);

    int (*dt_populate)(SpaprDrc *drc, struct SpaprMachineState *spapr,
                       void *fdt, int *fdt_start_offset, Error **errp);
} SpaprDrcClass;

typedef struct SpaprDrcPhysical {
    /*< private >*/
    SpaprDrc parent;

    /* DR-indicator */
    uint32_t dr_indicator;
} SpaprDrcPhysical;

static inline bool spapr_drc_hotplugged(DeviceState *dev)
{
    return dev->hotplugged && !runstate_check(RUN_STATE_INMIGRATE);
}

/* Returns true if an unplug request completed */
bool spapr_drc_reset(SpaprDrc *drc);

uint32_t spapr_drc_index(SpaprDrc *drc);
SpaprDrcType spapr_drc_type(SpaprDrc *drc);

SpaprDrc *spapr_dr_connector_new(Object *owner, const char *type,
                                         uint32_t id);
SpaprDrc *spapr_drc_by_index(uint32_t index);
SpaprDrc *spapr_drc_by_id(const char *type, uint32_t id);
int spapr_dt_drc(void *fdt, int offset, Object *owner, uint32_t drc_type_mask);

/*
 * These functions respectively abort if called with a device already
 * attached or no device attached. In the case of spapr_drc_attach(),
 * this means that the attachability of the DRC *must* be checked
 * beforehand (eg. check drc->dev at pre-plug).
 */
void spapr_drc_attach(SpaprDrc *drc, DeviceState *d);
void spapr_drc_detach(SpaprDrc *drc);

/*
 * Reset all DRCs, causing pending hot-plug/unplug requests to complete.
 * Safely handles potential DRC removal (eg. PHBs or PCI bridges).
 */
void spapr_drc_reset_all(struct SpaprMachineState *spapr);

static inline bool spapr_drc_unplug_requested(SpaprDrc *drc)
{
    return drc->unplug_requested;
}

#endif /* HW_SPAPR_DRC_H */
