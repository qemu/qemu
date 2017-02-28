/*
 * s390 IPL device
 *
 * Copyright 2015 IBM Corp.
 * Author(s): Zhang Fan <bjfanzh@cn.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_IPL_H
#define HW_S390_IPL_H

#include "hw/qdev.h"
#include "cpu.h"

struct IplBlockCcw {
    uint64_t netboot_start_addr;
    uint8_t  reserved0[77];
    uint8_t  ssid;
    uint16_t devno;
    uint8_t  vm_flags;
    uint8_t  reserved3[3];
    uint32_t vm_parm_len;
    uint8_t  nss_name[8];
    uint8_t  vm_parm[64];
    uint8_t  reserved4[8];
} QEMU_PACKED;
typedef struct IplBlockCcw IplBlockCcw;

struct IplBlockFcp {
    uint8_t  reserved1[305 - 1];
    uint8_t  opt;
    uint8_t  reserved2[3];
    uint16_t reserved3;
    uint16_t devno;
    uint8_t  reserved4[4];
    uint64_t wwpn;
    uint64_t lun;
    uint32_t bootprog;
    uint8_t  reserved5[12];
    uint64_t br_lba;
    uint32_t scp_data_len;
    uint8_t  reserved6[260];
    uint8_t  scp_data[];
} QEMU_PACKED;
typedef struct IplBlockFcp IplBlockFcp;

struct IplBlockQemuScsi {
    uint32_t lun;
    uint16_t target;
    uint16_t channel;
    uint8_t  reserved0[77];
    uint8_t  ssid;
    uint16_t devno;
} QEMU_PACKED;
typedef struct IplBlockQemuScsi IplBlockQemuScsi;

union IplParameterBlock {
    struct {
        uint32_t len;
        uint8_t  reserved0[3];
        uint8_t  version;
        uint32_t blk0_len;
        uint8_t  pbt;
        uint8_t  flags;
        uint16_t reserved01;
        uint8_t  loadparm[8];
        union {
            IplBlockCcw ccw;
            IplBlockFcp fcp;
            IplBlockQemuScsi scsi;
        };
    } QEMU_PACKED;
    struct {
        uint8_t  reserved1[110];
        uint16_t devno;
        uint8_t  reserved2[88];
        uint8_t  reserved_ext[4096 - 200];
    } QEMU_PACKED;
} QEMU_PACKED;
typedef union IplParameterBlock IplParameterBlock;

void s390_ipl_update_diag308(IplParameterBlock *iplb);
void s390_ipl_prepare_cpu(S390CPU *cpu);
IplParameterBlock *s390_ipl_get_iplb(void);
void s390_reipl_request(void);

#define TYPE_S390_IPL "s390-ipl"
#define S390_IPL(obj) OBJECT_CHECK(S390IPLState, (obj), TYPE_S390_IPL)

struct S390IPLState {
    /*< private >*/
    DeviceState parent_obj;
    uint64_t start_addr;
    uint64_t compat_start_addr;
    uint64_t bios_start_addr;
    uint64_t compat_bios_start_addr;
    bool enforce_bios;
    IplParameterBlock iplb;
    bool iplb_valid;
    bool reipl_requested;
    bool netboot;

    /*< public >*/
    char *kernel;
    char *initrd;
    char *cmdline;
    char *firmware;
    char *netboot_fw;
    uint8_t cssid;
    uint8_t ssid;
    uint16_t devno;
    bool iplbext_migration;
};
typedef struct S390IPLState S390IPLState;

#define S390_IPL_TYPE_FCP 0x00
#define S390_IPL_TYPE_CCW 0x02
#define S390_IPL_TYPE_QEMU_SCSI 0xff

#define S390_IPLB_HEADER_LEN 8
#define S390_IPLB_MIN_CCW_LEN 200
#define S390_IPLB_MIN_FCP_LEN 384
#define S390_IPLB_MIN_QEMU_SCSI_LEN 200

static inline bool iplb_valid_len(IplParameterBlock *iplb)
{
    return be32_to_cpu(iplb->len) <= sizeof(IplParameterBlock);
}

static inline bool iplb_valid_ccw(IplParameterBlock *iplb)
{
    return be32_to_cpu(iplb->len) >= S390_IPLB_MIN_CCW_LEN &&
           iplb->pbt == S390_IPL_TYPE_CCW;
}

static inline bool iplb_valid_fcp(IplParameterBlock *iplb)
{
    return be32_to_cpu(iplb->len) >= S390_IPLB_MIN_FCP_LEN &&
           iplb->pbt == S390_IPL_TYPE_FCP;
}

#endif
