/*
 * Block driver for Hyper-V VHDX Images
 *
 * Copyright (c) 2013 Red Hat, Inc.,
 *
 * Authors:
 *  Jeff Cody <jcody@redhat.com>
 *
 *  This is based on the "VHDX Format Specification v0.95", published 4/12/2012
 *  by Microsoft:
 *      https://www.microsoft.com/en-us/download/details.aspx?id=29681
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef BLOCK_VHDX_H
#define BLOCK_VHDX_H

/* Structures and fields present in the VHDX file */

/* The header section has the following blocks,
 * each block is 64KB:
 *
 * _____________________________________________________________________________
 * | File Id. |   Header 1    | Header 2   | Region Table |  Reserved (768KB)  |
 * |----------|---------------|------------|--------------|--------------------|
 * |          |               |            |              |                    |
 * 0.........64KB...........128KB........192KB..........256KB................1MB
 */

#define VHDX_HEADER_BLOCK_SIZE      (64*1024)

#define VHDX_FILE_ID_OFFSET         0
#define VHDX_HEADER1_OFFSET         (VHDX_HEADER_BLOCK_SIZE*1)
#define VHDX_HEADER2_OFFSET         (VHDX_HEADER_BLOCK_SIZE*2)
#define VHDX_REGION_TABLE_OFFSET    (VHDX_HEADER_BLOCK_SIZE*3)


/*
 * A note on the use of MS-GUID fields.  For more details on the GUID,
 * please see: https://en.wikipedia.org/wiki/Globally_unique_identifier.
 *
 * The VHDX specification only states that these are MS GUIDs, and which
 * bytes are data1-data4. It makes no mention of what algorithm should be used
 * to generate the GUID, nor what standard.  However, looking at the specified
 * known GUID fields, it appears the GUIDs are:
 *  Standard/DCE GUID type  (noted by 10b in the MSB of byte 0 of .data4)
 *  Random algorithm        (noted by 0x4XXX for .data3)
 */

/* ---- HEADER SECTION STRUCTURES ---- */

/* These structures are ones that are defined in the VHDX specification
 * document */

typedef struct VHDXFileIdentifier {
    uint64_t    signature;              /* "vhdxfile" in ASCII */
    uint16_t    creator[256];           /* optional; utf-16 string to identify
                                           the vhdx file creator.  Diagnotistic
                                           only */
} VHDXFileIdentifier;


/* the guid is a 16 byte unique ID - the definition for this used by
 * Microsoft is not just 16 bytes though - it is a structure that is defined,
 * so we need to follow it here so that endianness does not trip us up */

typedef struct MSGUID {
    uint32_t  data1;
    uint16_t  data2;
    uint16_t  data3;
    uint8_t   data4[8];
} MSGUID;

#define guid_eq(a, b) \
    (memcmp(&(a), &(b), sizeof(MSGUID)) == 0)

#define VHDX_HEADER_SIZE (4*1024)   /* although the vhdx_header struct in disk
                                       is only 582 bytes, for purposes of crc
                                       the header is the first 4KB of the 64KB
                                       block */

/* The full header is 4KB, although the actual header data is much smaller.
 * But for the checksum calculation, it is over the entire 4KB structure,
 * not just the defined portion of it */
typedef struct QEMU_PACKED VHDXHeader {
    uint32_t    signature;              /* "head" in ASCII */
    uint32_t    checksum;               /* CRC-32C hash of the whole header */
    uint64_t    sequence_number;        /* Seq number of this header.  Each
                                           VHDX file has 2 of these headers,
                                           and only the header with the highest
                                           sequence number is valid */
    MSGUID      file_write_guid;       /* 128 bit unique identifier. Must be
                                           updated to new, unique value before
                                           the first modification is made to
                                           file */
    MSGUID      data_write_guid;        /* 128 bit unique identifier. Must be
                                           updated to new, unique value before
                                           the first modification is made to
                                           visible data.   Visbile data is
                                           defined as:
                                                    - system & user metadata
                                                    - raw block data
                                                    - disk size
                                                    - any change that will
                                                      cause the virtual disk
                                                      sector read to differ

                                           This does not need to change if
                                           blocks are re-arranged */
    MSGUID      log_guid;               /* 128 bit unique identifier. If zero,
                                           there is no valid log. If non-zero,
                                           log entries with this guid are
                                           valid. */
    uint16_t    log_version;            /* version of the log format. Mustn't be
                                           zero, unless log_guid is also zero */
    uint16_t    version;                /* version of th evhdx file.  Currently,
                                           only supported version is "1" */
    uint32_t    log_length;             /* length of the log.  Must be multiple
                                           of 1MB */
    uint64_t    log_offset;             /* byte offset in the file of the log.
                                           Must also be a multiple of 1MB */
} VHDXHeader;

/* Header for the region table block */
typedef struct QEMU_PACKED VHDXRegionTableHeader {
    uint32_t    signature;              /* "regi" in ASCII */
    uint32_t    checksum;               /* CRC-32C hash of the 64KB table */
    uint32_t    entry_count;            /* number of valid entries */
    uint32_t    reserved;
} VHDXRegionTableHeader;

/* Individual region table entry.  There may be a maximum of 2047 of these
 *
 *  There are two known region table properties.  Both are required.
 *  BAT (block allocation table):  2DC27766F62342009D64115E9BFD4A08
 *  Metadata:                      8B7CA20647904B9AB8FE575F050F886E
 */
#define VHDX_REGION_ENTRY_REQUIRED  0x01    /* if set, parser must understand
                                               this entry in order to open
                                               file */
typedef struct QEMU_PACKED VHDXRegionTableEntry {
    MSGUID      guid;                   /* 128-bit unique identifier */
    uint64_t    file_offset;            /* offset of the object in the file.
                                           Must be multiple of 1MB */
    uint32_t    length;                 /* length, in bytes, of the object */
    uint32_t    data_bits;
} VHDXRegionTableEntry;


/* ---- LOG ENTRY STRUCTURES ---- */
#define VHDX_LOG_HDR_SIZE 64
typedef struct QEMU_PACKED VHDXLogEntryHeader {
    uint32_t    signature;              /* "loge" in ASCII */
    uint32_t    checksum;               /* CRC-32C hash of the 64KB table */
    uint32_t    entry_length;           /* length in bytes, multiple of 1MB */
    uint32_t    tail;                   /* byte offset of first log entry of a
                                           seq, where this entry is the last
                                           entry */
    uint64_t    sequence_number;        /* incremented with each log entry.
                                           May not be zero. */
    uint32_t    descriptor_count;       /* number of descriptors in this log
                                           entry, must be >= 0 */
    uint32_t    reserved;
    MSGUID      log_guid;               /* value of the log_guid from
                                           vhdx_header.  If not found in
                                           vhdx_header, it is invalid */
    uint64_t    flushed_file_offset;    /* see spec for full details - this
                                           sould be vhdx file size in bytes */
    uint64_t    last_file_offset;       /* size in bytes that all allocated
                                           file structures fit into */
} VHDXLogEntryHeader;

#define VHDX_LOG_DESC_SIZE 32

typedef struct QEMU_PACKED VHDXLogDescriptor {
    uint32_t    signature;              /* "zero" or "desc" in ASCII */
    union  {
        uint32_t    reserved;           /* zero desc */
        uint32_t    trailing_bytes;     /* data desc: bytes 4092-4096 of the
                                           data sector */
    };
    union {
        uint64_t    zero_length;        /* zero desc: length of the section to
                                           zero */
        uint64_t    leading_bytes;      /* data desc: bytes 0-7 of the data
                                           sector */
    };
    uint64_t    file_offset;            /* file offset to write zeros - multiple
                                           of 4kB */
    uint64_t    sequence_number;        /* must match same field in
                                           vhdx_log_entry_header */
} VHDXLogDescriptor;

typedef struct QEMU_PACKED VHDXLogDataSector {
    uint32_t    data_signature;         /* "data" in ASCII */
    uint32_t    sequence_high;          /* 4 MSB of 8 byte sequence_number */
    uint8_t     data[4084];             /* raw data, bytes 8-4091 (inclusive).
                                           see the data descriptor field for the
                                           other mising bytes */
    uint32_t    sequence_low;           /* 4 LSB of 8 byte sequence_number */
} VHDXLogDataSector;



/* block states - different state values depending on whether it is a
 * payload block, or a sector block. */

#define PAYLOAD_BLOCK_NOT_PRESENT       0
#define PAYLOAD_BLOCK_UNDEFINED         1
#define PAYLOAD_BLOCK_ZERO              2
#define PAYLOAD_BLOCK_UNMAPPED          5
#define PAYLOAD_BLOCK_FULL_PRESENT      6
#define PAYLOAD_BLOCK_PARTIALLY_PRESENT 7

#define SB_BLOCK_NOT_PRESENT    0
#define SB_BLOCK_PRESENT        6

/* per the spec */
#define VHDX_MAX_SECTORS_PER_BLOCK  (1<<23)

/* upper 44 bits are the file offset in 1MB units lower 3 bits are the state
   other bits are reserved */
#define VHDX_BAT_STATE_BIT_MASK 0x07
#define VHDX_BAT_FILE_OFF_BITS (64-44)
typedef uint64_t VHDXBatEntry;

/* ---- METADATA REGION STRUCTURES ---- */

#define VHDX_METADATA_ENTRY_SIZE 32
#define VHDX_METADATA_MAX_ENTRIES 2047  /* not including the header */
#define VHDX_METADATA_TABLE_MAX_SIZE \
    (VHDX_METADATA_ENTRY_SIZE * (VHDX_METADATA_MAX_ENTRIES+1))
typedef struct QEMU_PACKED VHDXMetadataTableHeader {
    uint64_t    signature;              /* "metadata" in ASCII */
    uint16_t    reserved;
    uint16_t    entry_count;            /* number table entries. <= 2047 */
    uint32_t    reserved2[5];
} VHDXMetadataTableHeader;

#define VHDX_META_FLAGS_IS_USER         0x01    /* max 1024 entries */
#define VHDX_META_FLAGS_IS_VIRTUAL_DISK 0x02    /* virtual disk metadata if set,
                                                   otherwise file metdata */
#define VHDX_META_FLAGS_IS_REQUIRED     0x04    /* parse must understand this
                                                   entry to open the file */
typedef struct QEMU_PACKED VHDXMetadataTableEntry {
    MSGUID      item_id;                /* 128-bit identifier for metadata */
    uint32_t    offset;                 /* byte offset of the metadata.  At
                                           least 64kB.  Relative to start of
                                           metadata region */
                                        /* note: if length = 0, so is offset */
    uint32_t    length;                 /* length of metadata. <= 1MB. */
    uint32_t    data_bits;      /* least-significant 3 bits are flags, the
                                   rest are reserved (see above) */
    uint32_t    reserved2;
} VHDXMetadataTableEntry;

#define VHDX_PARAMS_LEAVE_BLOCKS_ALLOCED 0x01   /* Do not change any blocks to
                                                   be BLOCK_NOT_PRESENT.
                                                   If set indicates a fixed
                                                   size VHDX file */
#define VHDX_PARAMS_HAS_PARENT           0x02    /* has parent / backing file */
typedef struct QEMU_PACKED VHDXFileParameters {
    uint32_t    block_size;             /* size of each payload block, always
                                           power of 2, <= 256MB and >= 1MB. */
    uint32_t data_bits;     /* least-significant 2 bits are flags, the rest
                               are reserved (see above) */
} VHDXFileParameters;

typedef struct QEMU_PACKED VHDXVirtualDiskSize {
    uint64_t    virtual_disk_size;      /* Size of the virtual disk, in bytes.
                                           Must be multiple of the sector size,
                                           max of 64TB */
} VHDXVirtualDiskSize;

typedef struct QEMU_PACKED VHDXPage83Data {
    MSGUID      page_83_data[16];       /* unique id for scsi devices that
                                           support page 0x83 */
} VHDXPage83Data;

typedef struct QEMU_PACKED VHDXVirtualDiskLogicalSectorSize {
    uint32_t    logical_sector_size;    /* virtual disk sector size (in bytes).
                                           Can only be 512 or 4096 bytes */
} VHDXVirtualDiskLogicalSectorSize;

typedef struct QEMU_PACKED VHDXVirtualDiskPhysicalSectorSize {
    uint32_t    physical_sector_size;   /* physical sector size (in bytes).
                                           Can only be 512 or 4096 bytes */
} VHDXVirtualDiskPhysicalSectorSize;

typedef struct QEMU_PACKED VHDXParentLocatorHeader {
    MSGUID      locator_type[16];       /* type of the parent virtual disk. */
    uint16_t    reserved;
    uint16_t    key_value_count;        /* number of key/value pairs for this
                                           locator */
} VHDXParentLocatorHeader;

/* key and value strings are UNICODE strings, UTF-16 LE encoding, no NULs */
typedef struct QEMU_PACKED VHDXParentLocatorEntry {
    uint32_t    key_offset;             /* offset in metadata for key, > 0 */
    uint32_t    value_offset;           /* offset in metadata for value, >0 */
    uint16_t    key_length;             /* length of entry key, > 0 */
    uint16_t    value_length;           /* length of entry value, > 0 */
} VHDXParentLocatorEntry;


/* ----- END VHDX SPECIFICATION STRUCTURES ---- */


uint32_t vhdx_checksum_calc(uint32_t crc, uint8_t *buf, size_t size,
                            int crc_offset);

bool vhdx_checksum_is_valid(uint8_t *buf, size_t size, int crc_offset);


static void leguid_to_cpus(MSGUID *guid)
{
    le32_to_cpus(&guid->data1);
    le16_to_cpus(&guid->data2);
    le16_to_cpus(&guid->data3);
}

#endif
