/*
 * vfio based subchannel assignment support
 *
 * Copyright 2017, 2019 IBM Corp.
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_VFIO_CCW_H
#define HW_VFIO_CCW_H

#include "hw/vfio/vfio-common.h"
#include "hw/s390x/s390-ccw.h"
#include "hw/s390x/ccw-device.h"
#include "qom/object.h"

#define TYPE_VFIO_CCW "vfio-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VFIOCCWDevice, VFIO_CCW)

#endif
