/*
 * Emulated ccw-attached 3270 definitions
 *
 * Copyright 2017 IBM Corp.
 * Author(s): Yang Chen <bjcyang@linux.vnet.ibm.com>
 *            Jing Liu <liujbjl@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390X_3270_CCW_H
#define HW_S390X_3270_CCW_H

#include "hw/sysbus.h"
#include "hw/s390x/css.h"
#include "hw/s390x/ccw-device.h"
#include "qom/object.h"

#define EMULATED_CCW_3270_CU_TYPE 0x3270
#define EMULATED_CCW_3270_CHPID_TYPE 0x1a

#define TYPE_EMULATED_CCW_3270 "emulated-ccw-3270"

/* Local Channel Commands */
#define TC_WRITE   0x01         /* Write */
#define TC_RDBUF   0x02         /* Read buffer */
#define TC_EWRITE  0x05         /* Erase write */
#define TC_READMOD 0x06         /* Read modified */
#define TC_EWRITEA 0x0d         /* Erase write alternate */
#define TC_WRITESF 0x11         /* Write structured field */

OBJECT_DECLARE_TYPE(EmulatedCcw3270Device, EmulatedCcw3270Class, EMULATED_CCW_3270)

struct EmulatedCcw3270Device {
    CcwDevice parent_obj;
};

struct EmulatedCcw3270Class {
    CCWDeviceClass parent_class;

    void (*init)(EmulatedCcw3270Device *, Error **);
    int (*read_payload_3270)(EmulatedCcw3270Device *);
    int (*write_payload_3270)(EmulatedCcw3270Device *, uint8_t);
};

#endif
