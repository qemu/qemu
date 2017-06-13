/*
 * S/390 channel I/O instructions
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
*/

#ifndef S390X_IOINST_H
#define S390X_IOINST_H

/*
 * Channel I/O related definitions, as defined in the Principles
 * Of Operation (and taken from the Linux implementation).
 */

/* subchannel status word (command mode only) */
typedef struct SCSW {
    uint16_t flags;
    uint16_t ctrl;
    uint32_t cpa;
    uint8_t dstat;
    uint8_t cstat;
    uint16_t count;
} QEMU_PACKED SCSW;

#define SCSW_FLAGS_MASK_KEY 0xf000
#define SCSW_FLAGS_MASK_SCTL 0x0800
#define SCSW_FLAGS_MASK_ESWF 0x0400
#define SCSW_FLAGS_MASK_CC 0x0300
#define SCSW_FLAGS_MASK_FMT 0x0080
#define SCSW_FLAGS_MASK_PFCH 0x0040
#define SCSW_FLAGS_MASK_ISIC 0x0020
#define SCSW_FLAGS_MASK_ALCC 0x0010
#define SCSW_FLAGS_MASK_SSI 0x0008
#define SCSW_FLAGS_MASK_ZCC 0x0004
#define SCSW_FLAGS_MASK_ECTL 0x0002
#define SCSW_FLAGS_MASK_PNO 0x0001

#define SCSW_CTRL_MASK_FCTL 0x7000
#define SCSW_CTRL_MASK_ACTL 0x0fe0
#define SCSW_CTRL_MASK_STCTL 0x001f

#define SCSW_FCTL_CLEAR_FUNC 0x1000
#define SCSW_FCTL_HALT_FUNC 0x2000
#define SCSW_FCTL_START_FUNC 0x4000

#define SCSW_ACTL_SUSP 0x0020
#define SCSW_ACTL_DEVICE_ACTIVE 0x0040
#define SCSW_ACTL_SUBCH_ACTIVE 0x0080
#define SCSW_ACTL_CLEAR_PEND 0x0100
#define SCSW_ACTL_HALT_PEND  0x0200
#define SCSW_ACTL_START_PEND 0x0400
#define SCSW_ACTL_RESUME_PEND 0x0800

#define SCSW_STCTL_STATUS_PEND 0x0001
#define SCSW_STCTL_SECONDARY 0x0002
#define SCSW_STCTL_PRIMARY 0x0004
#define SCSW_STCTL_INTERMEDIATE 0x0008
#define SCSW_STCTL_ALERT 0x0010

#define SCSW_DSTAT_ATTENTION     0x80
#define SCSW_DSTAT_STAT_MOD      0x40
#define SCSW_DSTAT_CU_END        0x20
#define SCSW_DSTAT_BUSY          0x10
#define SCSW_DSTAT_CHANNEL_END   0x08
#define SCSW_DSTAT_DEVICE_END    0x04
#define SCSW_DSTAT_UNIT_CHECK    0x02
#define SCSW_DSTAT_UNIT_EXCEP    0x01

#define SCSW_CSTAT_PCI           0x80
#define SCSW_CSTAT_INCORR_LEN    0x40
#define SCSW_CSTAT_PROG_CHECK    0x20
#define SCSW_CSTAT_PROT_CHECK    0x10
#define SCSW_CSTAT_DATA_CHECK    0x08
#define SCSW_CSTAT_CHN_CTRL_CHK  0x04
#define SCSW_CSTAT_INTF_CTRL_CHK 0x02
#define SCSW_CSTAT_CHAIN_CHECK   0x01

/* path management control word */
typedef struct PMCW {
    uint32_t intparm;
    uint16_t flags;
    uint16_t devno;
    uint8_t  lpm;
    uint8_t  pnom;
    uint8_t  lpum;
    uint8_t  pim;
    uint16_t mbi;
    uint8_t  pom;
    uint8_t  pam;
    uint8_t  chpid[8];
    uint32_t chars;
} QEMU_PACKED PMCW;

#define PMCW_FLAGS_MASK_QF 0x8000
#define PMCW_FLAGS_MASK_W 0x4000
#define PMCW_FLAGS_MASK_ISC 0x3800
#define PMCW_FLAGS_MASK_ENA 0x0080
#define PMCW_FLAGS_MASK_LM 0x0060
#define PMCW_FLAGS_MASK_MME 0x0018
#define PMCW_FLAGS_MASK_MP 0x0004
#define PMCW_FLAGS_MASK_TF 0x0002
#define PMCW_FLAGS_MASK_DNV 0x0001
#define PMCW_FLAGS_MASK_INVALID 0x0700

#define PMCW_CHARS_MASK_ST 0x00e00000
#define PMCW_CHARS_MASK_MBFC 0x00000004
#define PMCW_CHARS_MASK_XMWME 0x00000002
#define PMCW_CHARS_MASK_CSENSE 0x00000001
#define PMCW_CHARS_MASK_INVALID 0xff1ffff8

/* subchannel information block */
typedef struct SCHIB {
    PMCW pmcw;
    SCSW scsw;
    uint64_t mba;
    uint8_t mda[4];
} QEMU_PACKED SCHIB;

/* interruption response block */
typedef struct IRB {
    SCSW scsw;
    uint32_t esw[5];
    uint32_t ecw[8];
    uint32_t emw[8];
} QEMU_PACKED IRB;

/* operation request block */
typedef struct ORB {
    uint32_t intparm;
    uint16_t ctrl0;
    uint8_t lpm;
    uint8_t ctrl1;
    uint32_t cpa;
} QEMU_PACKED ORB;

#define ORB_CTRL0_MASK_KEY 0xf000
#define ORB_CTRL0_MASK_SPND 0x0800
#define ORB_CTRL0_MASK_STR 0x0400
#define ORB_CTRL0_MASK_MOD 0x0200
#define ORB_CTRL0_MASK_SYNC 0x0100
#define ORB_CTRL0_MASK_FMT 0x0080
#define ORB_CTRL0_MASK_PFCH 0x0040
#define ORB_CTRL0_MASK_ISIC 0x0020
#define ORB_CTRL0_MASK_ALCC 0x0010
#define ORB_CTRL0_MASK_SSIC 0x0008
#define ORB_CTRL0_MASK_C64 0x0002
#define ORB_CTRL0_MASK_I2K 0x0001
#define ORB_CTRL0_MASK_INVALID 0x0004

#define ORB_CTRL1_MASK_ILS 0x80
#define ORB_CTRL1_MASK_MIDAW 0x40
#define ORB_CTRL1_MASK_ORBX 0x01
#define ORB_CTRL1_MASK_INVALID 0x3e

/* channel command word (type 0) */
typedef struct CCW0 {
        uint8_t cmd_code;
        uint8_t cda0;
        uint16_t cda1;
        uint8_t flags;
        uint8_t reserved;
        uint16_t count;
} QEMU_PACKED CCW0;

/* channel command word (type 1) */
typedef struct CCW1 {
    uint8_t cmd_code;
    uint8_t flags;
    uint16_t count;
    uint32_t cda;
} QEMU_PACKED CCW1;

#define CCW_FLAG_DC              0x80
#define CCW_FLAG_CC              0x40
#define CCW_FLAG_SLI             0x20
#define CCW_FLAG_SKIP            0x10
#define CCW_FLAG_PCI             0x08
#define CCW_FLAG_IDA             0x04
#define CCW_FLAG_SUSPEND         0x02
#define CCW_FLAG_MIDA            0x01

#define CCW_CMD_NOOP             0x03
#define CCW_CMD_BASIC_SENSE      0x04
#define CCW_CMD_TIC              0x08
#define CCW_CMD_SENSE_ID         0xe4

typedef struct CRW {
    uint16_t flags;
    uint16_t rsid;
} QEMU_PACKED CRW;

#define CRW_FLAGS_MASK_S 0x4000
#define CRW_FLAGS_MASK_R 0x2000
#define CRW_FLAGS_MASK_C 0x1000
#define CRW_FLAGS_MASK_RSC 0x0f00
#define CRW_FLAGS_MASK_A 0x0080
#define CRW_FLAGS_MASK_ERC 0x003f

#define CRW_ERC_INIT 0x02
#define CRW_ERC_IPI  0x04

#define CRW_RSC_SUBCH 0x3
#define CRW_RSC_CHP   0x4
#define CRW_RSC_CSS   0xb

/* I/O interruption code */
typedef struct IOIntCode {
    uint32_t subsys_id;
    uint32_t intparm;
    uint32_t interrupt_id;
} QEMU_PACKED IOIntCode;

/* schid disintegration */
#define IOINST_SCHID_ONE(_schid)   ((_schid & 0x00010000) >> 16)
#define IOINST_SCHID_M(_schid)     ((_schid & 0x00080000) >> 19)
#define IOINST_SCHID_CSSID(_schid) ((_schid & 0xff000000) >> 24)
#define IOINST_SCHID_SSID(_schid)  ((_schid & 0x00060000) >> 17)
#define IOINST_SCHID_NR(_schid)    (_schid & 0x0000ffff)

#define IO_INT_WORD_ISC(_int_word) ((_int_word & 0x38000000) >> 27)
#define ISC_TO_ISC_BITS(_isc)      ((0x80 >> _isc) << 24)

#define IO_INT_WORD_AI 0x80000000

int ioinst_disassemble_sch_ident(uint32_t value, int *m, int *cssid, int *ssid,
                                 int *schid);

#endif
