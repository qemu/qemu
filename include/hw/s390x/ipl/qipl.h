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

/* Boot Menu flags */
#define QIPL_FLAG_BM_OPTS_CMD   0x80
#define QIPL_FLAG_BM_OPTS_ZIPL  0x40

#define QIPL_ADDRESS  0xcc
#define LOADPARM_LEN    8
#define NO_LOADPARM "\0\0\0\0\0\0\0\0"

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
    uint64_t next_iplb;
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

union IplParameterBlock {
    struct {
        uint32_t len;
        uint8_t  reserved0[3];
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

#endif
