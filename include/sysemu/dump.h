/*
 * QEMU dump
 *
 * Copyright Fujitsu, Corp. 2011, 2012
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef DUMP_H
#define DUMP_H

#define MAKEDUMPFILE_SIGNATURE      "makedumpfile"
#define MAX_SIZE_MDF_HEADER         (4096) /* max size of makedumpfile_header */
#define TYPE_FLAT_HEADER            (1)    /* type of flattened format */
#define VERSION_FLAT_HEADER         (1)    /* version of flattened format */
#define END_FLAG_FLAT_HEADER        (-1)

#define ARCH_PFN_OFFSET             (0)

#define paddr_to_pfn(X, page_shift) \
    (((unsigned long long)(X) >> (page_shift)) - ARCH_PFN_OFFSET)
#define pfn_to_paddr(X, page_shift) \
    (((unsigned long long)(X) + ARCH_PFN_OFFSET) << (page_shift))

/*
 * flag for compressed format
 */
#define DUMP_DH_COMPRESSED_ZLIB     (0x1)
#define DUMP_DH_COMPRESSED_LZO      (0x2)
#define DUMP_DH_COMPRESSED_SNAPPY   (0x4)

#define KDUMP_SIGNATURE             "KDUMP   "
#define SIG_LEN                     (sizeof(KDUMP_SIGNATURE) - 1)
#define PHYS_BASE                   (0)
#define DUMP_LEVEL                  (1)
#define DISKDUMP_HEADER_BLOCKS      (1)
#define BUFSIZE_BITMAP              (TARGET_PAGE_SIZE)
#define PFN_BUFBITMAP               (CHAR_BIT * BUFSIZE_BITMAP)
#define BUFSIZE_DATA_CACHE          (TARGET_PAGE_SIZE * 4)

typedef struct ArchDumpInfo {
    int d_machine;  /* Architecture */
    int d_endian;   /* ELFDATA2LSB or ELFDATA2MSB */
    int d_class;    /* ELFCLASS32 or ELFCLASS64 */
} ArchDumpInfo;

typedef struct QEMU_PACKED MakedumpfileHeader {
    char signature[16];     /* = "makedumpfile" */
    int64_t type;
    int64_t version;
} MakedumpfileHeader;

typedef struct QEMU_PACKED MakedumpfileDataHeader {
    int64_t offset;
    int64_t buf_size;
} MakedumpfileDataHeader;

typedef struct QEMU_PACKED NewUtsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} NewUtsname;

typedef struct QEMU_PACKED DiskDumpHeader32 {
    char signature[SIG_LEN];        /* = "KDUMP   " */
    uint32_t header_version;        /* Dump header version */
    NewUtsname utsname;             /* copy of system_utsname */
    char timestamp[10];             /* Time stamp */
    uint32_t status;                /* Above flags */
    uint32_t block_size;            /* Size of a block in byte */
    uint32_t sub_hdr_size;          /* Size of arch dependent header in block */
    uint32_t bitmap_blocks;         /* Size of Memory bitmap in block */
    uint32_t max_mapnr;             /* = max_mapnr ,
                                       obsoleted in header_version 6 */
    uint32_t total_ram_blocks;      /* Number of blocks should be written */
    uint32_t device_blocks;         /* Number of total blocks in dump device */
    uint32_t written_blocks;        /* Number of written blocks */
    uint32_t current_cpu;           /* CPU# which handles dump */
    uint32_t nr_cpus;               /* Number of CPUs */
} DiskDumpHeader32;

typedef struct QEMU_PACKED DiskDumpHeader64 {
    char signature[SIG_LEN];        /* = "KDUMP   " */
    uint32_t header_version;        /* Dump header version */
    NewUtsname utsname;             /* copy of system_utsname */
    char timestamp[22];             /* Time stamp */
    uint32_t status;                /* Above flags */
    uint32_t block_size;            /* Size of a block in byte */
    uint32_t sub_hdr_size;          /* Size of arch dependent header in block */
    uint32_t bitmap_blocks;         /* Size of Memory bitmap in block */
    uint32_t max_mapnr;             /* = max_mapnr,
                                       obsoleted in header_version 6 */
    uint32_t total_ram_blocks;      /* Number of blocks should be written */
    uint32_t device_blocks;         /* Number of total blocks in dump device */
    uint32_t written_blocks;        /* Number of written blocks */
    uint32_t current_cpu;           /* CPU# which handles dump */
    uint32_t nr_cpus;               /* Number of CPUs */
} DiskDumpHeader64;

typedef struct QEMU_PACKED KdumpSubHeader32 {
    uint32_t phys_base;
    uint32_t dump_level;            /* header_version 1 and later */
    uint32_t split;                 /* header_version 2 and later */
    uint32_t start_pfn;             /* header_version 2 and later,
                                       obsoleted in header_version 6 */
    uint32_t end_pfn;               /* header_version 2 and later,
                                       obsoleted in header_version 6 */
    uint64_t offset_vmcoreinfo;     /* header_version 3 and later */
    uint32_t size_vmcoreinfo;       /* header_version 3 and later */
    uint64_t offset_note;           /* header_version 4 and later */
    uint32_t note_size;             /* header_version 4 and later */
    uint64_t offset_eraseinfo;      /* header_version 5 and later */
    uint32_t size_eraseinfo;        /* header_version 5 and later */
    uint64_t start_pfn_64;          /* header_version 6 and later */
    uint64_t end_pfn_64;            /* header_version 6 and later */
    uint64_t max_mapnr_64;          /* header_version 6 and later */
} KdumpSubHeader32;

typedef struct QEMU_PACKED KdumpSubHeader64 {
    uint64_t phys_base;
    uint32_t dump_level;            /* header_version 1 and later */
    uint32_t split;                 /* header_version 2 and later */
    uint64_t start_pfn;             /* header_version 2 and later,
                                       obsoleted in header_version 6 */
    uint64_t end_pfn;               /* header_version 2 and later,
                                       obsoleted in header_version 6 */
    uint64_t offset_vmcoreinfo;     /* header_version 3 and later */
    uint64_t size_vmcoreinfo;       /* header_version 3 and later */
    uint64_t offset_note;           /* header_version 4 and later */
    uint64_t note_size;             /* header_version 4 and later */
    uint64_t offset_eraseinfo;      /* header_version 5 and later */
    uint64_t size_eraseinfo;        /* header_version 5 and later */
    uint64_t start_pfn_64;          /* header_version 6 and later */
    uint64_t end_pfn_64;            /* header_version 6 and later */
    uint64_t max_mapnr_64;          /* header_version 6 and later */
} KdumpSubHeader64;

typedef struct DataCache {
    int fd;             /* fd of the file where to write the cached data */
    uint8_t *buf;       /* buffer for cached data */
    size_t buf_size;    /* size of the buf */
    size_t data_size;   /* size of cached data in buf */
    off_t offset;       /* offset of the file */
} DataCache;

typedef struct QEMU_PACKED PageDescriptor {
    uint64_t offset;                /* the offset of the page data*/
    uint32_t size;                  /* the size of this dump page */
    uint32_t flags;                 /* flags */
    uint64_t page_flags;            /* page flags */
} PageDescriptor;

struct GuestPhysBlockList; /* memory_mapping.h */
int cpu_get_dump_info(ArchDumpInfo *info,
                      const struct GuestPhysBlockList *guest_phys_blocks);
ssize_t cpu_get_note_size(int class, int machine, int nr_cpus);

#endif
