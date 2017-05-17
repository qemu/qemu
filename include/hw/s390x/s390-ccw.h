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

#define TYPE_S390_CCW "s390-ccw"
#define S390_CCW_DEVICE(obj) \
    OBJECT_CHECK(S390CCWDevice, (obj), TYPE_S390_CCW)
#define S390_CCW_DEVICE_CLASS(klass) \
    OBJECT_CLASS_CHECK(S390CCWDeviceClass, (klass), TYPE_S390_CCW)
#define S390_CCW_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(S390CCWDeviceClass, (obj), TYPE_S390_CCW)

typedef struct S390CCWDevice {
    CcwDevice parent_obj;
    CssDevId hostid;
    char *mdevid;
} S390CCWDevice;

typedef struct S390CCWDeviceClass {
    CCWDeviceClass parent_class;
    void (*realize)(S390CCWDevice *dev, char *sysfsdev, Error **errp);
    void (*unrealize)(S390CCWDevice *dev, Error **errp);
    int (*handle_request) (ORB *, SCSW *, void *);
} S390CCWDeviceClass;

#endif
