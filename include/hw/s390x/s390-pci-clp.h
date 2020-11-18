/*
 * s390 CLP instruction definitions
 *
 * Copyright 2019 IBM Corp.
 * Author(s): Pierre Morel <pmorel@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_PCI_CLP
#define HW_S390_PCI_CLP

/* CLP common request & response block size */
#define CLP_BLK_SIZE 4096
#define PCI_BAR_COUNT 6
#define PCI_MAX_FUNCTIONS 4096

typedef struct ClpReqHdr {
    uint16_t len;
    uint16_t cmd;
} QEMU_PACKED ClpReqHdr;

typedef struct ClpRspHdr {
    uint16_t len;
    uint16_t rsp;
} QEMU_PACKED ClpRspHdr;

/* CLP Response Codes */
#define CLP_RC_OK         0x0010  /* Command request successfully */
#define CLP_RC_CMD        0x0020  /* Command code not recognized */
#define CLP_RC_PERM       0x0030  /* Command not authorized */
#define CLP_RC_FMT        0x0040  /* Invalid command request format */
#define CLP_RC_LEN        0x0050  /* Invalid command request length */
#define CLP_RC_8K         0x0060  /* Command requires 8K LPCB */
#define CLP_RC_RESNOT0    0x0070  /* Reserved field not zero */
#define CLP_RC_NODATA     0x0080  /* No data available */
#define CLP_RC_FC_UNKNOWN 0x0100  /* Function code not recognized */

/*
 * Call Logical Processor - Command Codes
 */
#define CLP_LIST_PCI            0x0002
#define CLP_QUERY_PCI_FN        0x0003
#define CLP_QUERY_PCI_FNGRP     0x0004
#define CLP_SET_PCI_FN          0x0005

/* PCI function handle list entry */
typedef struct ClpFhListEntry {
    uint16_t device_id;
    uint16_t vendor_id;
#define CLP_FHLIST_MASK_CONFIG 0x80000000
    uint32_t config;
    uint32_t fid;
    uint32_t fh;
} QEMU_PACKED ClpFhListEntry;

#define CLP_RC_SETPCIFN_FH      0x0101 /* Invalid PCI fn handle */
#define CLP_RC_SETPCIFN_FHOP    0x0102 /* Fn handle not valid for op */
#define CLP_RC_SETPCIFN_DMAAS   0x0103 /* Invalid DMA addr space */
#define CLP_RC_SETPCIFN_RES     0x0104 /* Insufficient resources */
#define CLP_RC_SETPCIFN_ALRDY   0x0105 /* Fn already in requested state */
#define CLP_RC_SETPCIFN_ERR     0x0106 /* Fn in permanent error state */
#define CLP_RC_SETPCIFN_RECPND  0x0107 /* Error recovery pending */
#define CLP_RC_SETPCIFN_BUSY    0x0108 /* Fn busy */
#define CLP_RC_LISTPCI_BADRT    0x010a /* Resume token not recognized */
#define CLP_RC_QUERYPCIFG_PFGID 0x010b /* Unrecognized PFGID */

/* request or response block header length */
#define LIST_PCI_HDR_LEN 32

/* Number of function handles fitting in response block */
#define CLP_FH_LIST_NR_ENTRIES \
    ((CLP_BLK_SIZE - 2 * LIST_PCI_HDR_LEN) \
        / sizeof(ClpFhListEntry))

#define CLP_SET_ENABLE_PCI_FN  0 /* Yes, 0 enables it */
#define CLP_SET_DISABLE_PCI_FN 1 /* Yes, 1 disables it */

#define CLP_UTIL_STR_LEN 64
#define CLP_PFIP_NR_SEGMENTS 4

#define CLP_MASK_FMT 0xf0000000

/* List PCI functions request */
typedef struct ClpReqListPci {
    ClpReqHdr hdr;
    uint32_t fmt;
    uint64_t reserved1;
    uint64_t resume_token;
    uint64_t reserved2;
} QEMU_PACKED ClpReqListPci;

/* List PCI functions response */
typedef struct ClpRspListPci {
    ClpRspHdr hdr;
    uint32_t fmt;
    uint64_t reserved1;
    uint64_t resume_token;
    uint32_t mdd;
    uint16_t max_fn;
    uint8_t flags;
    uint8_t entry_size;
    ClpFhListEntry fh_list[CLP_FH_LIST_NR_ENTRIES];
} QEMU_PACKED ClpRspListPci;

/* Query PCI function request */
typedef struct ClpReqQueryPci {
    ClpReqHdr hdr;
    uint32_t fmt;
    uint64_t reserved1;
    uint32_t fh; /* function handle */
    uint32_t reserved2;
    uint64_t reserved3;
} QEMU_PACKED ClpReqQueryPci;

/* Query PCI function response */
typedef struct ClpRspQueryPci {
    ClpRspHdr hdr;
    uint32_t fmt;
    uint64_t reserved1;
    uint16_t vfn; /* virtual fn number */
#define CLP_RSP_QPCI_MASK_UTIL  0x01
    uint8_t flags;
    uint8_t pfgid;
    uint32_t fid; /* pci function id */
    uint8_t bar_size[PCI_BAR_COUNT];
    uint16_t pchid;
    uint32_t bar[PCI_BAR_COUNT];
    uint8_t pfip[CLP_PFIP_NR_SEGMENTS];
    uint16_t reserved2;
    uint8_t fmbl;
    uint8_t pft;
    uint64_t sdma; /* start dma as */
    uint64_t edma; /* end dma as */
    uint32_t reserved3[11];
    uint32_t uid;
    uint8_t util_str[CLP_UTIL_STR_LEN]; /* utility string */
} QEMU_PACKED ClpRspQueryPci;

/* Query PCI function group request */
typedef struct ClpReqQueryPciGrp {
    ClpReqHdr hdr;
    uint32_t fmt;
    uint64_t reserved1;
    uint8_t reserved2[3];
    uint8_t g;
    uint32_t reserved3;
    uint64_t reserved4;
} QEMU_PACKED ClpReqQueryPciGrp;

/* Query PCI function group response */
typedef struct ClpRspQueryPciGrp {
    ClpRspHdr hdr;
    uint32_t fmt;
    uint64_t reserved1;
#define CLP_RSP_QPCIG_MASK_NOI 0xfff
    uint16_t i;
    uint8_t version;
#define CLP_RSP_QPCIG_MASK_FRAME   0x2
#define CLP_RSP_QPCIG_MASK_REFRESH 0x1
    uint8_t fr;
    uint16_t maxstbl;
    uint16_t mui;
    uint64_t reserved3;
    uint64_t dasm; /* dma address space mask */
    uint64_t msia; /* MSI address */
    uint64_t reserved4;
    uint64_t reserved5;
} QEMU_PACKED ClpRspQueryPciGrp;

/* Set PCI function request */
typedef struct ClpReqSetPci {
    ClpReqHdr hdr;
    uint32_t fmt;
    uint64_t reserved1;
    uint32_t fh; /* function handle */
    uint16_t reserved2;
    uint8_t oc; /* operation controls */
    uint8_t ndas; /* number of dma spaces */
    uint64_t reserved3;
} QEMU_PACKED ClpReqSetPci;

/* Set PCI function response */
typedef struct ClpRspSetPci {
    ClpRspHdr hdr;
    uint32_t fmt;
    uint64_t reserved1;
    uint32_t fh; /* function handle */
    uint32_t reserved3;
    uint64_t reserved4;
} QEMU_PACKED ClpRspSetPci;

typedef struct ClpReqRspListPci {
    ClpReqListPci request;
    ClpRspListPci response;
} QEMU_PACKED ClpReqRspListPci;

typedef struct ClpReqRspSetPci {
    ClpReqSetPci request;
    ClpRspSetPci response;
} QEMU_PACKED ClpReqRspSetPci;

typedef struct ClpReqRspQueryPci {
    ClpReqQueryPci request;
    ClpRspQueryPci response;
} QEMU_PACKED ClpReqRspQueryPci;

typedef struct ClpReqRspQueryPciGrp {
    ClpReqQueryPciGrp request;
    ClpRspQueryPciGrp response;
} QEMU_PACKED ClpReqRspQueryPciGrp;

#endif
