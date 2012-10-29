/*
 * SCLP Support
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Christian Borntraeger <borntraeger@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_S390_SCLP_H
#define HW_S390_SCLP_H

#include <hw/sysbus.h>
#include <hw/qdev.h>

/* SCLP command codes */
#define SCLP_CMDW_READ_SCP_INFO                 0x00020001
#define SCLP_CMDW_READ_SCP_INFO_FORCED          0x00120001

/* SCLP response codes */
#define SCLP_RC_NORMAL_READ_COMPLETION          0x0010
#define SCLP_RC_INVALID_SCLP_COMMAND            0x01f0

/* Service Call Control Block (SCCB) and its elements */

#define SCCB_SIZE 4096

/*
 * Normally packed structures are not the right thing to do, since all code
 * must take care of endianess. We cant use ldl_phys and friends for two
 * reasons, though:
 * - some of the embedded structures below the SCCB can appear multiple times
 *   at different locations, so there is no fixed offset
 * - we work on a private copy of the SCCB, since there are several length
 *   fields, that would cause a security nightmare if we allow the guest to
 *   alter the structure while we parse it. We cannot use ldl_p and friends
 *   either without doing pointer arithmetics
 * So we have to double check that all users of sclp data structures use the
 * right endianess wrappers.
 */
typedef struct SCCBHeader {
    uint16_t length;
    uint8_t function_code;
    uint8_t control_mask[3];
    uint16_t response_code;
} QEMU_PACKED SCCBHeader;

#define SCCB_DATA_LEN (SCCB_SIZE - sizeof(SCCBHeader))

typedef struct ReadInfo {
    SCCBHeader h;
    uint16_t rnmax;
    uint8_t rnsize;
} QEMU_PACKED ReadInfo;

typedef struct SCCB {
    SCCBHeader h;
    char data[SCCB_DATA_LEN];
 } QEMU_PACKED SCCB;

typedef struct S390SCLPDevice {
    SysBusDevice busdev;
} S390SCLPDevice;

typedef struct S390SCLPDeviceClass {
    DeviceClass qdev;
    int (*init)(S390SCLPDevice *sdev);
} S390SCLPDeviceClass;

void sclp_service_interrupt(uint32_t sccb);

#endif
