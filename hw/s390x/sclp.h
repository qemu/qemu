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
#define SCLP_CMD_READ_EVENT_DATA                0x00770005
#define SCLP_CMD_WRITE_EVENT_DATA               0x00760005
#define SCLP_CMD_READ_EVENT_DATA                0x00770005
#define SCLP_CMD_WRITE_EVENT_DATA               0x00760005
#define SCLP_CMD_WRITE_EVENT_MASK               0x00780005

/* SCLP response codes */
#define SCLP_RC_NORMAL_READ_COMPLETION          0x0010
#define SCLP_RC_NORMAL_COMPLETION               0x0020
#define SCLP_RC_INVALID_SCLP_COMMAND            0x01f0
#define SCLP_RC_CONTAINED_EQUIPMENT_CHECK       0x0340
#define SCLP_RC_INSUFFICIENT_SCCB_LENGTH        0x0300
#define SCLP_RC_INVALID_FUNCTION                0x40f0
#define SCLP_RC_NO_EVENT_BUFFERS_STORED         0x60f0
#define SCLP_RC_INVALID_SELECTION_MASK          0x70f0
#define SCLP_RC_INCONSISTENT_LENGTHS            0x72f0
#define SCLP_RC_EVENT_BUFFER_SYNTAX_ERROR       0x73f0
#define SCLP_RC_INVALID_MASK_LENGTH             0x74f0


/* Service Call Control Block (SCCB) and its elements */

#define SCCB_SIZE 4096

#define SCLP_VARIABLE_LENGTH_RESPONSE           0x80
#define SCLP_EVENT_BUFFER_ACCEPTED              0x80

#define SCLP_FC_NORMAL_WRITE                    0

/*
 * Normally packed structures are not the right thing to do, since all code
 * must take care of endianness. We cannot use ldl_phys and friends for two
 * reasons, though:
 * - some of the embedded structures below the SCCB can appear multiple times
 *   at different locations, so there is no fixed offset
 * - we work on a private copy of the SCCB, since there are several length
 *   fields, that would cause a security nightmare if we allow the guest to
 *   alter the structure while we parse it. We cannot use ldl_p and friends
 *   either without doing pointer arithmetics
 * So we have to double check that all users of sclp data structures use the
 * right endianness wrappers.
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

static inline int sccb_data_len(SCCB *sccb)
{
    return be16_to_cpu(sccb->h.length) - sizeof(sccb->h);
}

#define TYPE_DEVICE_S390_SCLP "s390-sclp-device"
#define SCLP_S390_DEVICE(obj) \
     OBJECT_CHECK(S390SCLPDevice, (obj), TYPE_DEVICE_S390_SCLP)
#define SCLP_S390_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(S390SCLPDeviceClass, (klass), \
             TYPE_DEVICE_S390_SCLP)
#define SCLP_S390_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(S390SCLPDeviceClass, (obj), \
             TYPE_DEVICE_S390_SCLP)

typedef struct SCLPEventFacility SCLPEventFacility;

typedef struct S390SCLPDevice {
    SysBusDevice busdev;
    SCLPEventFacility *ef;
    void (*sclp_command_handler)(SCLPEventFacility *ef, SCCB *sccb,
                                 uint64_t code);
    bool (*event_pending)(SCLPEventFacility *ef);
} S390SCLPDevice;

typedef struct S390SCLPDeviceClass {
    DeviceClass qdev;
    int (*init)(S390SCLPDevice *sdev);
} S390SCLPDeviceClass;

void s390_sclp_init(void);
void sclp_service_interrupt(uint32_t sccb);

#endif
