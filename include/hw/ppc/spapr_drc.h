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
#if !defined(__HW_SPAPR_DRC_H__)
#define __HW_SPAPR_DRC_H__

#include "qom/object.h"
#include "hw/qdev.h"
#include "libfdt.h"

#define TYPE_SPAPR_DR_CONNECTOR "spapr-dr-connector"
#define SPAPR_DR_CONNECTOR_GET_CLASS(obj) \
        OBJECT_GET_CLASS(sPAPRDRConnectorClass, obj, TYPE_SPAPR_DR_CONNECTOR)
#define SPAPR_DR_CONNECTOR_CLASS(klass) \
        OBJECT_CLASS_CHECK(sPAPRDRConnectorClass, klass, \
                           TYPE_SPAPR_DR_CONNECTOR)
#define SPAPR_DR_CONNECTOR(obj) OBJECT_CHECK(sPAPRDRConnector, (obj), \
                                             TYPE_SPAPR_DR_CONNECTOR)

/*
 * Various hotplug types managed by sPAPRDRConnector
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
} sPAPRDRConnectorTypeShift;

typedef enum {
    SPAPR_DR_CONNECTOR_TYPE_ANY = ~0,
    SPAPR_DR_CONNECTOR_TYPE_CPU = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_CPU,
    SPAPR_DR_CONNECTOR_TYPE_PHB = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_PHB,
    SPAPR_DR_CONNECTOR_TYPE_VIO = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_VIO,
    SPAPR_DR_CONNECTOR_TYPE_PCI = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_PCI,
    SPAPR_DR_CONNECTOR_TYPE_LMB = 1 << SPAPR_DR_CONNECTOR_TYPE_SHIFT_LMB,
} sPAPRDRConnectorType;

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
} sPAPRDRIsolationState;

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
} sPAPRDRAllocationState;

/*
 * LED/visual indicator state
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
    SPAPR_DR_INDICATOR_STATE_INACTIVE   = 0,
    SPAPR_DR_INDICATOR_STATE_ACTIVE     = 1,
    SPAPR_DR_INDICATOR_STATE_IDENTIFY   = 2,
    SPAPR_DR_INDICATOR_STATE_ACTION     = 3,
} sPAPRDRIndicatorState;

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
} sPAPRDREntitySense;

typedef enum {
    SPAPR_DR_CC_RESPONSE_NEXT_SIB         = 1, /* currently unused */
    SPAPR_DR_CC_RESPONSE_NEXT_CHILD       = 2,
    SPAPR_DR_CC_RESPONSE_NEXT_PROPERTY    = 3,
    SPAPR_DR_CC_RESPONSE_PREV_PARENT      = 4,
    SPAPR_DR_CC_RESPONSE_SUCCESS          = 0,
    SPAPR_DR_CC_RESPONSE_ERROR            = -1,
    SPAPR_DR_CC_RESPONSE_CONTINUE         = -2,
    SPAPR_DR_CC_RESPONSE_NOT_CONFIGURABLE = -9003,
} sPAPRDRCCResponse;

typedef void (spapr_drc_detach_cb)(DeviceState *d, void *opaque);

typedef struct sPAPRDRConnector {
    /*< private >*/
    DeviceState parent;

    sPAPRDRConnectorType type;
    uint32_t id;
    Object *owner;
    const char *name;

    /* sensor/indicator states */
    uint32_t isolation_state;
    uint32_t allocation_state;
    uint32_t indicator_state;

    /* configure-connector state */
    void *fdt;
    int fdt_start_offset;
    bool configured;

    bool awaiting_release;
    bool signalled;
    bool awaiting_allocation;

    /* device pointer, via link property */
    DeviceState *dev;
    spapr_drc_detach_cb *detach_cb;
    void *detach_cb_opaque;
} sPAPRDRConnector;

typedef struct sPAPRDRConnectorClass {
    /*< private >*/
    DeviceClass parent;

    /*< public >*/

    /* accessors for guest-visible (generally via RTAS) DR state */
    uint32_t (*set_isolation_state)(sPAPRDRConnector *drc,
                                    sPAPRDRIsolationState state);
    uint32_t (*set_indicator_state)(sPAPRDRConnector *drc,
                                    sPAPRDRIndicatorState state);
    uint32_t (*set_allocation_state)(sPAPRDRConnector *drc,
                                     sPAPRDRAllocationState state);
    uint32_t (*get_index)(sPAPRDRConnector *drc);
    uint32_t (*get_type)(sPAPRDRConnector *drc);
    const char *(*get_name)(sPAPRDRConnector *drc);

    uint32_t (*entity_sense)(sPAPRDRConnector *drc, sPAPRDREntitySense *state);

    /* QEMU interfaces for managing FDT/configure-connector */
    const void *(*get_fdt)(sPAPRDRConnector *drc, int *fdt_start_offset);
    void (*set_configured)(sPAPRDRConnector *drc);

    /* QEMU interfaces for managing hotplug operations */
    void (*attach)(sPAPRDRConnector *drc, DeviceState *d, void *fdt,
                   int fdt_start_offset, bool coldplug, Error **errp);
    void (*detach)(sPAPRDRConnector *drc, DeviceState *d,
                   spapr_drc_detach_cb *detach_cb,
                   void *detach_cb_opaque, Error **errp);
    bool (*release_pending)(sPAPRDRConnector *drc);
    void (*set_signalled)(sPAPRDRConnector *drc);
} sPAPRDRConnectorClass;

sPAPRDRConnector *spapr_dr_connector_new(Object *owner,
                                         sPAPRDRConnectorType type,
                                         uint32_t id);
sPAPRDRConnector *spapr_dr_connector_by_index(uint32_t index);
sPAPRDRConnector *spapr_dr_connector_by_id(sPAPRDRConnectorType type,
                                           uint32_t id);
int spapr_drc_populate_dt(void *fdt, int fdt_offset, Object *owner,
                          uint32_t drc_type_mask);

#endif /* __HW_SPAPR_DRC_H__ */
