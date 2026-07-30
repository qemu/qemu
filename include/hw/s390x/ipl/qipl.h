/*
 * S/390 boot structures
 *
 * Copyright 2024 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef S390X_QIPL_H
#define S390X_QIPL_H

#include "diag308.h"

/* Boot Menu flags */
#define QIPL_FLAG_BM_OPTS_CMD   0x80
#define QIPL_FLAG_BM_OPTS_ZIPL  0x40

#define QIPL_ADDRESS  0xcc
#define LOADPARM_LEN    8
#define NO_LOADPARM "\0\0\0\0\0\0\0\0"

#define MAX_BOOT_ENTRIES  32

enum S390IplType {
    S390_IPL_TYPE_FCP = 0x00,
    S390_IPL_TYPE_CCW = 0x02,
    S390_IPL_TYPE_PCI = 0x04,
    S390_IPL_TYPE_PV = 0x05,
    S390_IPL_TYPE_QEMU_SCSI = 0xff
};
typedef enum S390IplType S390IplType;

#define QEMU_DEFAULT_IPL S390_IPL_TYPE_CCW

#define S390_IPLB_HEADER_LEN 8
#define S390_IPLB_MIN_PV_LEN 148
#define S390_IPLB_MIN_CCW_LEN 200
#define S390_IPLB_MIN_FCP_LEN 384
#define S390_IPLB_MIN_PCI_LEN 376
#define S390_IPLB_MIN_QEMU_SCSI_LEN 200
#define S390_IPLB_MAX_LEN 4096

#define MAX_BOOT_DEVS 8 /* Max number of devices that may have a bootindex */

#define MAX_CERTIFICATES  64
#define CERT_BUF_SIZE     ((MAX_BOOT_DEVS - 1) * 4096)
/* largest supported block size - same as VIRTIO_DASD_DEFAULT_BLOCK_SIZE */
#define VIRTIO_MAX_BLOCK_SIZE   4096
#define MAX_COMP_ENTRIES        ((VIRTIO_MAX_BLOCK_SIZE - 32) / 32)

/*
 * The QEMU IPL Parameters will be stored at absolute address
 * 204 (0xcc) which means it is 32-bit word aligned but not
 * double-word aligned. Placement of 64-bit data fields in this
 * area must account for their alignment needs.
 * The total size of the struct must never exceed 28 bytes.
 */
struct QemuIplParameters {
    uint8_t  qipl_flags;
    uint8_t  index;
    uint8_t  reserved1[2];
    uint64_t reserved2;
    uint32_t boot_menu_timeout;
    uint8_t  reserved3[2];
    uint16_t chain_len;
    uint64_t ipl_data;
} QEMU_PACKED;
typedef struct QemuIplParameters QemuIplParameters;

struct IPLBlockPVComp {
    uint64_t tweak_pref;
    uint64_t addr;
    uint64_t size;
} QEMU_PACKED;
typedef struct IPLBlockPVComp IPLBlockPVComp;

struct IPLBlockPV {
    uint8_t  reserved18[87];    /* 0x18 */
    uint8_t  version;           /* 0x6f */
    uint32_t reserved70;        /* 0x70 */
    uint32_t num_comp;          /* 0x74 */
    uint64_t pv_header_addr;    /* 0x78 */
    uint64_t pv_header_len;     /* 0x80 */
    struct IPLBlockPVComp components[0];
} QEMU_PACKED;
typedef struct IPLBlockPV IPLBlockPV;

struct IplBlockCcw {
    uint8_t  reserved0[85];
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
    uint8_t  scp_data[0];
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

struct IplBlockPci {
    uint32_t reserved0[76];
    uint8_t  opt;
    uint8_t  reserved1[3];
    uint32_t fid;
} QEMU_PACKED;
typedef struct IplBlockPci IplBlockPci;

union IplParameterBlock {
    struct {
        uint32_t len;
        uint8_t  hdr_flags;
        uint8_t  reserved0[2];
        uint8_t  version;
        uint32_t blk0_len;
        uint8_t  pbt;
        uint8_t  flags;
        uint16_t reserved01;
        uint8_t  loadparm[LOADPARM_LEN];
        union {
            IplBlockCcw ccw;
            IplBlockFcp fcp;
            IPLBlockPV pv;
            IplBlockQemuScsi scsi;
            IplBlockPci pci;
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

struct IplInfoReportBlockHeader {
    uint32_t len;
    uint8_t  flags;
    uint8_t  reserved1[11];
};
typedef struct IplInfoReportBlockHeader IplInfoReportBlockHeader;

struct IplInfoBlockHeader {
    uint32_t len;
    uint8_t  type;
    uint8_t  reserved1[11];
};
typedef struct IplInfoBlockHeader IplInfoBlockHeader;

enum IplInfoBlockType {
    IPL_INFO_BLOCK_TYPE_CERTIFICATES = 1,
    IPL_INFO_BLOCK_TYPE_COMPONENTS = 2,
};

struct IplSignatureCertificateEntry {
    uint64_t addr;
    uint64_t len;
};
typedef struct IplSignatureCertificateEntry IplSignatureCertificateEntry;

struct IplSignatureCertificateList {
    IplInfoBlockHeader            ipl_info_header;
    IplSignatureCertificateEntry  cert_entries[MAX_CERTIFICATES];
};
typedef struct IplSignatureCertificateList IplSignatureCertificateList;

#define S390_IPL_DEV_COMP_FLAG_SC  0x80
#define S390_IPL_DEV_COMP_FLAG_CSV 0x40

struct IplDeviceComponentEntry {
    uint64_t addr;
    uint64_t len;
    uint8_t  flags;
    uint8_t  reserved1[5];
    uint16_t cert_index;
    uint8_t  reserved2[8];
};
typedef struct IplDeviceComponentEntry IplDeviceComponentEntry;

struct IplDeviceComponentList {
    IplInfoBlockHeader       ipl_info_header;
    IplDeviceComponentEntry  device_entries[MAX_COMP_ENTRIES];
};
typedef struct IplDeviceComponentList IplDeviceComponentList;

#define COMP_LIST_MAX   sizeof(IplDeviceComponentList)
#define CERT_LIST_MAX   sizeof(IplSignatureCertificateList)

struct IplInfoReportBlock {
    IplInfoReportBlockHeader     hdr;
    uint8_t                      info_blks[COMP_LIST_MAX + CERT_LIST_MAX];
};
typedef struct IplInfoReportBlock IplInfoReportBlock;

struct IplBlocks {
    IplParameterBlock   iplb;
    IplInfoReportBlock  iirb;
};
typedef struct IplBlocks IplBlocks;

#endif
