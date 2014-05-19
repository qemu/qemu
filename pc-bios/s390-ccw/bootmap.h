/*
 * QEMU S390 bootmap interpreter -- declarations
 *
 * Copyright 2014 IBM Corp.
 * Author(s): Eugene (jno) Dvurechenski <jno@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#ifndef _PC_BIOS_S390_CCW_BOOTMAP_H
#define _PC_BIOS_S390_CCW_BOOTMAP_H

#include "s390-ccw.h"

#define FREE_SPACE_FILLER '\xAA'

typedef struct ScsiBlockPtr {
    uint64_t blockno;
    uint16_t size;
    uint16_t blockct;
    uint8_t reserved[4];
} __attribute__ ((packed)) ScsiBlockPtr;

typedef struct FbaBlockPtr {
    uint32_t blockno;
    uint16_t size;
    uint16_t blockct;
} __attribute__ ((packed)) FbaBlockPtr;

typedef struct EckdBlockPtr {
    uint16_t cylinder; /* cylinder/head/sector is an address of the block */
    uint16_t head;
    uint8_t sector;
    uint16_t size;
    uint8_t count; /* (size_in_blocks-1);
                    * it's 0 for TablePtr, ScriptPtr, and SectionPtr */
} __attribute__ ((packed)) EckdBlockPtr;

typedef struct ExtEckdBlockPtr {
    EckdBlockPtr bptr;
    uint8_t reserved[8];
} __attribute__ ((packed)) ExtEckdBlockPtr;

typedef union BootMapPointer {
    ScsiBlockPtr scsi;
    FbaBlockPtr fba;
    EckdBlockPtr eckd;
    ExtEckdBlockPtr xeckd;
} __attribute__ ((packed)) BootMapPointer;

typedef struct ComponentEntry {
    ScsiBlockPtr data;
    uint8_t pad[7];
    uint8_t component_type;
    uint64_t load_address;
} __attribute((packed)) ComponentEntry;

typedef struct ComponentHeader {
    uint8_t magic[4];   /* == "zIPL"                    */
    uint8_t type;       /* == ZIPL_COMP_HEADER_*        */
    uint8_t reserved[27];
} __attribute((packed)) ComponentHeader;

typedef struct ScsiMbr {
    uint8_t magic[4];
    uint32_t version_id;
    uint8_t reserved[8];
    ScsiBlockPtr blockptr;
} __attribute__ ((packed)) ScsiMbr;

#define ZIPL_MAGIC              "zIPL"
#define IPL1_MAGIC "\xc9\xd7\xd3\xf1" /* == "IPL1" in EBCDIC */
#define IPL2_MAGIC "\xc9\xd7\xd3\xf2" /* == "IPL2" in EBCDIC */
#define VOL1_MAGIC "\xe5\xd6\xd3\xf1" /* == "VOL1" in EBCDIC */
#define LNX1_MAGIC "\xd3\xd5\xe7\xf1" /* == "LNX1" in EBCDIC */
#define CMS1_MAGIC "\xc3\xd4\xe2\xf1" /* == "CMS1" in EBCDIC */

#define LDL1_VERSION '\x40' /* == ' ' in EBCDIC */
#define LDL2_VERSION '\xf2' /* == '2' in EBCDIC */

#define ZIPL_COMP_HEADER_IPL    0x00
#define ZIPL_COMP_HEADER_DUMP   0x01

#define ZIPL_COMP_ENTRY_LOAD    0x02
#define ZIPL_COMP_ENTRY_EXEC    0x01

typedef struct XEckdMbr {
    uint8_t magic[4];   /* == "xIPL"        */
    uint8_t version;
    uint8_t bp_type;
    uint8_t dev_type;   /* == DEV_TYPE_*    */
#define DEV_TYPE_ECKD 0x00
#define DEV_TYPE_FBA  0x01
    uint8_t flags;
    BootMapPointer blockptr;
    uint8_t reserved[8];
} __attribute__ ((packed)) XEckdMbr; /* see also BootInfo */

typedef struct BootMapScriptEntry {
    BootMapPointer blkptr;
    uint8_t pad[7];
    uint8_t type;   /* == BOOT_SCRIPT_* */
#define BOOT_SCRIPT_EXEC 0x01
#define BOOT_SCRIPT_LOAD 0x02
    union {
        uint64_t load_address;
        uint64_t load_psw;
    } address;
} __attribute__ ((packed)) BootMapScriptEntry;

typedef struct BootMapScriptHeader {
    uint32_t magic;
    uint8_t type;
#define BOOT_SCRIPT_HDR_IPL 0x00
    uint8_t reserved[27];
} __attribute__ ((packed)) BootMapScriptHeader;

typedef struct BootMapScript {
    BootMapScriptHeader header;
    BootMapScriptEntry  entry[0];
} __attribute__ ((packed)) BootMapScript;

/*
 * These aren't real VTOCs, but referred to this way in some docs.
 * They are "volume labels" actually.
 *
 * Some structures looks similar to described above, but left
 * separate as there is no indication that they are the same.
 * So, the value definitions are left separate too.
 */
typedef struct LDL_VTOC {       /* @ rec.3 cyl.0 trk.0 for ECKD */
    char magic[4];              /* "LNX1", EBCDIC               */
    char volser[6];             /* volser, EBCDIC               */
    uint8_t reserved[69];       /* reserved, 0x40               */
    uint8_t LDL_version;        /* 0x40 or 0xF2                 */
    uint64_t formatted_blocks;  /* if LDL_version >= 0xF2       */
} __attribute__ ((packed)) LDL_VTOC;

typedef struct format_date {
    uint8_t YY;
    uint8_t MM;
    uint8_t DD;
    uint8_t hh;
    uint8_t mm;
    uint8_t ss;
} __attribute__ ((packed)) format_date_t;

typedef struct CMS_VTOC {       /* @ rec.3 cyl.0 trk.0 for ECKD */
                                /* @ blk.1 (zero based) for FBA */
    char magic[4];              /* 'CMS1', EBCDIC               */
    char volser[6];             /* volser, EBCDIC               */
    uint16_t version;           /* = 0                          */
    uint32_t block_size;        /* = 512, 1024, 2048, or 4096   */
    uint32_t disk_origin;       /* = 4 or 5                     */
    uint32_t blocks;            /* Number of usable cyls/blocks */
    uint32_t formatted;         /* Max number of fmtd cyls/blks */
    uint32_t CMS_blocks;        /* disk size in CMS blocks      */
    uint32_t CMS_used;          /* Number of CMS blocks in use  */
    uint32_t FST_size;          /* = 64, bytes                  */
    uint32_t FST_per_CMS_blk;   /*                              */
    format_date_t format_date;  /* YYMMDDhhmmss as 6 bytes      */
    uint8_t reserved1[2];       /* = 0                          */
    uint32_t offset;            /* disk offset when reserved    */
    uint32_t next_hole;         /* block nr                     */
    uint32_t HBLK_hole_offset;  /* >> HBLK data of next hole    */
    uint32_t alloc_map_usr_off; /* >> user part of Alloc map    */
    uint8_t reserved2[4];       /* = 0                          */
    char shared_seg_name[8];    /*                              */
} __attribute__ ((packed)) CMS_VTOC;

/* from zipl/include/boot.h */
typedef struct BootInfoBpIpl {
    union {
        ExtEckdBlockPtr eckd;
        ScsiBlockPtr linr;
    } bm_ptr;
    uint8_t unused[16];
} __attribute__ ((packed)) BootInfoBpIpl;

typedef struct EckdDumpParam {
    uint32_t        start_blk;
    uint32_t        end_blk;
    uint16_t        blocksize;
    uint8_t         num_heads;
    uint8_t         bpt;
    char            reserved[4];
} __attribute((packed, may_alias)) EckdDumpParam;

typedef struct FbaDumpParam {
    uint64_t        start_blk;
    uint64_t        blockct;
} __attribute((packed)) FbaDumpParam;

typedef struct BootInfoBpDump {
    union {
        EckdDumpParam eckd;
        FbaDumpParam fba;
    } param;
    uint8_t         unused[16];
} __attribute__ ((packed)) BootInfoBpDump;

typedef struct BootInfo {          /* @ 0x70, record #0    */
    unsigned char magic[4]; /* = 'zIPL', ASCII      */
    uint8_t version;        /* = 1                  */
#define BOOT_INFO_VERSION               1
    uint8_t bp_type;        /* = 0                  */
#define BOOT_INFO_BP_TYPE_IPL           0x00
#define BOOT_INFO_BP_TYPE_DUMP          0x01
    uint8_t dev_type;       /* = 0                  */
#define BOOT_INFO_DEV_TYPE_ECKD         0x00
#define BOOT_INFO_DEV_TYPE_FBA          0x01
    uint8_t flags;          /* = 1                  */
#ifdef __s390x__
#define BOOT_INFO_FLAGS_ARCH            0x01
#else
#define BOOT_INFO_FLAGS_ARCH            0x00
#endif
    union {
        BootInfoBpDump dump;
        BootInfoBpIpl ipl;
    } bp;
} __attribute__ ((packed)) BootInfo; /* see also XEckdMbr   */

typedef struct Ipl1 {
    unsigned char key[4]; /* == "IPL1" */
    unsigned char data[24];
} __attribute__((packed)) Ipl1;

typedef struct Ipl2 {
    unsigned char key[4]; /* == "IPL2" */
    union {
        unsigned char data[144];
        struct {
            unsigned char reserved1[92-4];
            XEckdMbr mbr;
            unsigned char reserved2[144-(92-4)-sizeof(XEckdMbr)];
        } x;
    } u;
} __attribute__((packed)) Ipl2;

typedef struct IplVolumeLabel {
    unsigned char key[4]; /* == "VOL1" */
    union {
        unsigned char data[80];
        struct {
            unsigned char key[4]; /* == "VOL1" */
            unsigned char volser[6];
            unsigned char reserved[6];
        } f;
    };
} __attribute__((packed)) IplVolumeLabel;

#endif /* _PC_BIOS_S390_CCW_BOOTMAP_H */
