/*
 * Common device infrastructure for devices in the virtual css
 *
 * Copyright 2016 IBM Corp.
 * Author(s): Jing Liu <liujbjl@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390X_CCW_DEVICE_H
#define HW_S390X_CCW_DEVICE_H
#include "qom/object.h"
#include "hw/qdev-core.h"
#include "hw/s390x/css.h"
#include "hw/s390x/css-bridge.h"

struct CcwDevice {
    DeviceState parent_obj;
    SubchDev *sch;
    /* <cssid>.<ssid>.<device number> */
    /* The user-set busid of the virtual ccw device. */
    CssDevId devno;
    /* The actual busid of the virtual ccw device. */
    CssDevId dev_id;
    /* The actual busid of the virtual subchannel. */
    CssDevId subch_id;
    /* If set, use this loadparm value when device is boot target */
    uint8_t loadparm[8];
};
typedef struct CcwDevice CcwDevice;

extern const VMStateDescription vmstate_ccw_dev;
#define VMSTATE_CCW_DEVICE(_field, _state)                     \
    VMSTATE_STRUCT(_field, _state, 1, vmstate_ccw_dev, CcwDevice)

struct CCWDeviceClass {
    DeviceClass parent_class;
    void (*unplug)(HotplugHandler *, DeviceState *, Error **);
    bool (*realize)(CcwDevice *, Error **);
    void (*refill_ids)(CcwDevice *);
};

static inline CcwDevice *to_ccw_dev_fast(DeviceState *d)
{
    return container_of(d, CcwDevice, parent_obj);
}

#define TYPE_CCW_DEVICE "ccw-device"

OBJECT_DECLARE_TYPE(CcwDevice, CCWDeviceClass, CCW_DEVICE)

extern const PropertyInfo ccw_loadparm;

#define DEFINE_PROP_CCW_LOADPARM(_n, _s, _f) \
    DEFINE_PROP(_n, _s, _f, ccw_loadparm, typeof(uint8_t[8]))

#endif
