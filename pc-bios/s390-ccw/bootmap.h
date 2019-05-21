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
#include "virtio.h"

typedef uint64_t block_number_t;
#define NULL_BLOCK_NR 0xffffffffffffffffULL

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

typedef struct EckdCHS {
    uint16_t cylinder;
    uint16_t head;
    uint8_t sector;
} __attribute__ ((packed)) EckdCHS;

typedef struct EckdBlockPtr {
    EckdCHS chs; /* cylinder/head/sector is an address of the block */
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

/* aka Program Table */
typedef struct BootMapTable {
    uint8_t magic[4];
    uint8_t reserved[12];
    BootMapPointer entry[];
} __attribute__ ((packed)) BootMapTable;

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
    ScsiBlockPtr pt;   /* block pointer to program table */
} __attribute__ ((packed)) ScsiMbr;

#define ZIPL_MAGIC              "zIPL"
#define ZIPL_MAGIC_EBCDIC       "\xa9\xc9\xd7\xd3"
#define IPL1_MAGIC "\xc9\xd7\xd3\xf1" /* == "IPL1" in EBCDIC */
#define IPL2_MAGIC "\xc9\xd7\xd3\xf2" /* == "IPL2" in EBCDIC */
#define VOL1_MAGIC "\xe5\xd6\xd3\xf1" /* == "VOL1" in EBCDIC */
#define LNX1_MAGIC "\xd3\xd5\xe7\xf1" /* == "LNX1" in EBCDIC */
#define CMS1_MAGIC "\xc3\xd4\xe2\xf1" /* == "CMS1" in EBCDIC */

#define LDL1_VERSION '\x40' /* == ' ' in EBCDIC */
#define LDL2_VERSION '\xf2' /* == '2' in EBCDIC */

#define ZIPL_COMP_HEADER_IPL    0x00
#define ZIPL_COMP_HEADER_DUMP   0x01

#define ZIPL_COMP_ENTRY_EXEC      0x01
#define ZIPL_COMP_ENTRY_LOAD      0x02
#define ZIPL_COMP_ENTRY_SIGNATURE 0x03

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
#define BOOT_SCRIPT_EXEC      0x01
#define BOOT_SCRIPT_LOAD      0x02
#define BOOT_SCRIPT_SIGNATURE 0x03
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

/*
 * Structs for IPL
 */
#define STAGE2_BLK_CNT_MAX  24 /* Stage 1b can load up to 24 blocks */

typedef struct EckdCdlIpl1 {
    uint8_t key[4]; /* == "IPL1" */
    uint8_t data[24];
} __attribute__((packed)) EckdCdlIpl1;

typedef struct EckdSeekArg {
    uint16_t pad;
    EckdCHS chs;
    uint8_t pad2;
} __attribute__ ((packed)) EckdSeekArg;

typedef struct EckdStage1b {
    uint8_t reserved[32 * STAGE2_BLK_CNT_MAX];
    struct EckdSeekArg seek[STAGE2_BLK_CNT_MAX];
    uint8_t unused[64];
} __attribute__ ((packed)) EckdStage1b;

typedef struct EckdStage1 {
    uint8_t reserved[72];
    struct EckdSeekArg seek[2];
} __attribute__ ((packed)) EckdStage1;

typedef struct EckdCdlIpl2 {
    uint8_t key[4]; /* == "IPL2" */
    struct EckdStage1 stage1;
    XEckdMbr mbr;
    uint8_t reserved[24];
} __attribute__((packed)) EckdCdlIpl2;

typedef struct EckdLdlIpl1 {
    uint8_t reserved[24];
    struct EckdStage1 stage1;
    BootInfo bip; /* BootInfo is MBR for LDL */
} __attribute__((packed)) EckdLdlIpl1;

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

typedef enum {
    ECKD_NO_IPL,
    ECKD_CMS,
    ECKD_LDL,
    ECKD_LDL_UNLABELED,
} ECKD_IPL_mode_t;

/* utility code below */

static inline void print_volser(const void *volser)
{
    char ascii[8];

    ebcdic_to_ascii((char *)volser, ascii, 6);
    ascii[6] = '\0';
    sclp_print("VOLSER=[");
    sclp_print(ascii);
    sclp_print("]\n");
}

static inline bool unused_space(const void *p, size_t size)
{
    size_t i;
    const unsigned char *m = p;

    for (i = 0; i < size; i++) {
        if (m[i] != FREE_SPACE_FILLER) {
            return false;
        }
    }
    return true;
}

static inline bool is_null_block_number(block_number_t x)
{
    return x == NULL_BLOCK_NR;
}

static inline void read_block(block_number_t blockno,
                              void *buffer,
                              const char *errmsg)
{
    IPL_assert(virtio_read(blockno, buffer) == 0, errmsg);
}

static inline bool block_size_ok(uint32_t block_size)
{
    return block_size == virtio_get_block_size();
}

static inline bool magic_match(const void *data, const void *magic)
{
    return *((uint32_t *)data) == *((uint32_t *)magic);
}

static inline uint32_t iso_733_to_u32(uint64_t x)
{
    return (uint32_t)x;
}

#define ISO_SECTOR_SIZE 2048
/* El Torito specifies boot image size in 512 byte blocks */
#define ET_SECTOR_SHIFT 2

#define ISO_PRIMARY_VD_SECTOR 16

static inline void read_iso_sector(uint32_t block_offset, void *buf,
                                   const char *errmsg)
{
    IPL_assert(virtio_read_many(block_offset, buf, 1) == 0, errmsg);
}

static inline void read_iso_boot_image(uint32_t block_offset, void *load_addr,
                                       uint32_t blks_to_load)
{
    IPL_assert(virtio_read_many(block_offset, load_addr, blks_to_load) == 0,
               "Failed to read boot image!");
}

#define ISO9660_MAX_DIR_DEPTH 8

typedef struct IsoDirHdr {
    uint8_t dr_len;
    uint8_t ear_len;
    uint64_t ext_loc;
    uint64_t data_len;
    uint8_t recording_datetime[7];
    uint8_t file_flags;
    uint8_t file_unit_size;
    uint8_t gap_size;
    uint32_t vol_seqnum;
    uint8_t fileid_len;
} __attribute__((packed)) IsoDirHdr;

typedef struct IsoVdElTorito {
    uint8_t el_torito[32]; /* must contain el_torito_magic value */
    uint8_t unused0[32];
    uint32_t bc_offset;
    uint8_t unused1[1974];
} __attribute__((packed)) IsoVdElTorito;

typedef struct IsoVdPrimary {
    uint8_t unused1;
    uint8_t sys_id[32];
    uint8_t vol_id[32];
    uint8_t unused2[8];
    uint64_t vol_space_size;
    uint8_t unused3[32];
    uint32_t vol_set_size;
    uint32_t vol_seqnum;
    uint32_t log_block_size;
    uint64_t path_table_size;
    uint32_t l_path_table;
    uint32_t opt_l_path_table;
    uint32_t m_path_table;
    uint32_t opt_m_path_table;
    IsoDirHdr rootdir;
    uint8_t root_null;
    uint8_t reserved2[1858];
} __attribute__((packed)) IsoVdPrimary;

typedef struct IsoVolDesc {
    uint8_t type;
    uint8_t ident[5];
    uint8_t version;
    union {
        IsoVdElTorito boot;
        IsoVdPrimary primary;
    } vd;
} __attribute__((packed)) IsoVolDesc;

#define VOL_DESC_TYPE_BOOT 0
#define VOL_DESC_TYPE_PRIMARY 1
#define VOL_DESC_TYPE_SUPPLEMENT 2
#define VOL_DESC_TYPE_PARTITION 3
#define VOL_DESC_TERMINATOR 255

typedef struct IsoBcValid {
    uint8_t platform_id;
    uint16_t reserved;
    uint8_t id[24];
    uint16_t checksum;
    uint8_t key[2];
} __attribute__((packed)) IsoBcValid;

typedef struct IsoBcSection {
    uint8_t boot_type;
    uint16_t load_segment;
    uint8_t sys_type;
    uint8_t unused;
    uint16_t sector_count;
    uint32_t load_rba;
    uint8_t selection[20];
} __attribute__((packed)) IsoBcSection;

typedef struct IsoBcHdr {
    uint8_t platform_id;
    uint16_t sect_num;
    uint8_t id[28];
} __attribute__((packed)) IsoBcHdr;

typedef struct IsoBcEntry {
    uint8_t id;
    union {
        IsoBcValid valid; /* id == 0x01 */
        IsoBcSection sect; /* id == 0x88 || id == 0x0 */
        IsoBcHdr hdr; /* id == 0x90 || id == 0x91 */
    } body;
} __attribute__((packed)) IsoBcEntry;

#define ISO_BC_ENTRY_PER_SECTOR (ISO_SECTOR_SIZE / sizeof(IsoBcEntry))
#define ISO_BC_HDR_VALIDATION 0x01
#define ISO_BC_BOOTABLE_SECTION 0x88
#define ISO_BC_MAGIC_55 0x55
#define ISO_BC_MAGIC_AA 0xaa
#define ISO_BC_PLATFORM_X86 0x0
#define ISO_BC_PLATFORM_PPC 0x1
#define ISO_BC_PLATFORM_MAC 0x2

static inline bool is_iso_bc_valid(IsoBcEntry *e)
{
    IsoBcValid *v = &e->body.valid;

    if (e->id != ISO_BC_HDR_VALIDATION) {
        return false;
    }

    if (v->platform_id != ISO_BC_PLATFORM_X86 &&
        v->platform_id != ISO_BC_PLATFORM_PPC &&
        v->platform_id != ISO_BC_PLATFORM_MAC) {
        return false;
    }

    return v->key[0] == ISO_BC_MAGIC_55 &&
           v->key[1] == ISO_BC_MAGIC_AA &&
           v->reserved == 0x0;
}

#endif /* _PC_BIOS_S390_CCW_BOOTMAP_H */
