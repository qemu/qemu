/*
 * CXL CDAT Structure
 *
 * Copyright (C) 2021 Avery Design Systems, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_CDAT_H
#define CXL_CDAT_H

#include "hw/cxl/cxl_pci.h"

/*
 * Reference:
 *   Coherent Device Attribute Table (CDAT) Specification, Rev. 1.03, July. 2022
 *   Compute Express Link (CXL) Specification, Rev. 3.0, Aug. 2022
 */
/* Table Access DOE - CXL r3.0 8.1.11 */
#define CXL_DOE_TABLE_ACCESS      2
#define CXL_DOE_PROTOCOL_CDAT     ((CXL_DOE_TABLE_ACCESS << 16) | CXL_VENDOR_ID)

/* Read Entry - CXL r3.0 8.1.11.1 */
#define CXL_DOE_TAB_TYPE_CDAT 0
#define CXL_DOE_TAB_ENT_MAX 0xFFFF

/* Read Entry Request - CXL r3.0 8.1.11.1 Table 8-13 */
#define CXL_DOE_TAB_REQ 0
typedef struct CDATReq {
    DOEHeader header;
    uint8_t req_code;
    uint8_t table_type;
    uint16_t entry_handle;
} QEMU_PACKED CDATReq;

/* Read Entry Response - CXL r3.0 8.1.11.1 Table 8-14 */
#define CXL_DOE_TAB_RSP 0
typedef struct CDATRsp {
    DOEHeader header;
    uint8_t rsp_code;
    uint8_t table_type;
    uint16_t entry_handle;
} QEMU_PACKED CDATRsp;

/* CDAT Table Format - CDAT Table 1 */
#define CXL_CDAT_REV 2
typedef struct CDATTableHeader {
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    uint8_t reserved[6];
    uint32_t sequence;
} QEMU_PACKED CDATTableHeader;

/* CDAT Structure Types - CDAT Table 2 */
typedef enum {
    CDAT_TYPE_DSMAS = 0,
    CDAT_TYPE_DSLBIS = 1,
    CDAT_TYPE_DSMSCIS = 2,
    CDAT_TYPE_DSIS = 3,
    CDAT_TYPE_DSEMTS = 4,
    CDAT_TYPE_SSLBIS = 5,
} CDATType;

typedef struct CDATSubHeader {
    uint8_t type;
    uint8_t reserved;
    uint16_t length;
} CDATSubHeader;

/* Device Scoped Memory Affinity Structure - CDAT Table 3 */
typedef struct CDATDsmas {
    CDATSubHeader header;
    uint8_t DSMADhandle;
    uint8_t flags;
#define CDAT_DSMAS_FLAG_NV              (1 << 2)
#define CDAT_DSMAS_FLAG_SHAREABLE       (1 << 3)
#define CDAT_DSMAS_FLAG_HW_COHERENT     (1 << 4)
#define CDAT_DSMAS_FLAG_DYNAMIC_CAP     (1 << 5)
    uint16_t reserved;
    uint64_t DPA_base;
    uint64_t DPA_length;
} QEMU_PACKED CDATDsmas;

/* Device Scoped Latency and Bandwidth Information Structure - CDAT Table 5 */
typedef struct CDATDslbis {
    CDATSubHeader header;
    uint8_t handle;
    /* Definitions of these fields refer directly to HMAT fields */
    uint8_t flags;
    uint8_t data_type;
    uint8_t reserved;
    uint64_t entry_base_unit;
    uint16_t entry[3];
    uint16_t reserved2;
} QEMU_PACKED CDATDslbis;

/* Device Scoped Memory Side Cache Information Structure - CDAT Table 6 */
typedef struct CDATDsmscis {
    CDATSubHeader header;
    uint8_t DSMAS_handle;
    uint8_t reserved[3];
    uint64_t memory_side_cache_size;
    uint32_t cache_attributes;
} QEMU_PACKED CDATDsmscis;

/* Device Scoped Initiator Structure - CDAT Table 7 */
typedef struct CDATDsis {
    CDATSubHeader header;
    uint8_t flags;
    uint8_t handle;
    uint16_t reserved;
} QEMU_PACKED CDATDsis;

/* Device Scoped EFI Memory Type Structure - CDAT Table 8 */
typedef struct CDATDsemts {
    CDATSubHeader header;
    uint8_t DSMAS_handle;
    uint8_t EFI_memory_type_attr;
    uint16_t reserved;
    uint64_t DPA_offset;
    uint64_t DPA_length;
} QEMU_PACKED CDATDsemts;

/* Switch Scoped Latency and Bandwidth Information Structure - CDAT Table 9 */
typedef struct CDATSslbisHeader {
    CDATSubHeader header;
    uint8_t data_type;
    uint8_t reserved[3];
    uint64_t entry_base_unit;
} QEMU_PACKED CDATSslbisHeader;

/* Switch Scoped Latency and Bandwidth Entry - CDAT Table 10 */
typedef struct CDATSslbe {
    uint16_t port_x_id;
    uint16_t port_y_id;
    uint16_t latency_bandwidth;
    uint16_t reserved;
} QEMU_PACKED CDATSslbe;

typedef struct CDATSslbis {
    CDATSslbisHeader sslbis_header;
    CDATSslbe sslbe[];
} QEMU_PACKED CDATSslbis;

typedef struct CDATEntry {
    void *base;
    uint32_t length;
} CDATEntry;

typedef struct CDATObject {
    CDATEntry *entry;
    int entry_len;

    int (*build_cdat_table)(CDATSubHeader ***cdat_table, void *priv);
    void (*free_cdat_table)(CDATSubHeader **cdat_table, int num, void *priv);
    bool to_update;
    void *private;
    char *filename;
    uint8_t *buf;
    struct CDATSubHeader **built_buf;
    int built_buf_len;
} CDATObject;
#endif /* CXL_CDAT_H */
