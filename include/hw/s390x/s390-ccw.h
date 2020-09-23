/*
 * s390 CCW Assignment Support
 *
 * Copyright 2017 IBM Corp.
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_CCW_H
#define HW_S390_CCW_H

#include "hw/s390x/ccw-device.h"
#include "qom/object.h"

#define TYPE_S390_CCW "s390-ccw"
typedef struct S390CCWDevice S390CCWDevice;
typedef struct S390CCWDeviceClass S390CCWDeviceClass;
DECLARE_OBJ_CHECKERS(S390CCWDevice, S390CCWDeviceClass,
                     S390_CCW_DEVICE, TYPE_S390_CCW)

struct S390CCWDevice {
    CcwDevice parent_obj;
    CssDevId hostid;
    char *mdevid;
    int32_t bootindex;
};

struct S390CCWDeviceClass {
    CCWDeviceClass parent_class;
    void (*realize)(S390CCWDevice *dev, char *sysfsdev, Error **errp);
    void (*unrealize)(S390CCWDevice *dev);
    IOInstEnding (*handle_request) (SubchDev *sch);
    int (*handle_halt) (SubchDev *sch);
    int (*handle_clear) (SubchDev *sch);
    IOInstEnding (*handle_store) (SubchDev *sch);
};

#endif
