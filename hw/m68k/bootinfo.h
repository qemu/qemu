/*
 * SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * Bootinfo tags from linux bootinfo.h and bootinfo-mac.h:
 * This is an easily parsable and extendable structure containing all
 * information to be passed from the bootstrap to the kernel
 *
 * This structure is copied right after the kernel by the bootstrap
 * routine.
 */

#ifndef HW_M68K_BOOTINFO_H
#define HW_M68K_BOOTINFO_H
struct bi_record {
    uint16_t tag;        /* tag ID */
    uint16_t size;       /* size of record */
    uint32_t data[0];    /* data */
};

/* machine independent tags */

#define BI_LAST         0x0000 /* last record */
#define BI_MACHTYPE     0x0001 /* machine type (u_long) */
#define BI_CPUTYPE      0x0002 /* cpu type (u_long) */
#define BI_FPUTYPE      0x0003 /* fpu type (u_long) */
#define BI_MMUTYPE      0x0004 /* mmu type (u_long) */
#define BI_MEMCHUNK     0x0005 /* memory chunk address and size */
                               /* (struct mem_info) */
#define BI_RAMDISK      0x0006 /* ramdisk address and size */
                               /* (struct mem_info) */
#define BI_COMMAND_LINE 0x0007 /* kernel command line parameters */
                               /* (string) */

/*  Macintosh-specific tags (all u_long) */

#define BI_MAC_MODEL    0x8000  /* Mac Gestalt ID (model type) */
#define BI_MAC_VADDR    0x8001  /* Mac video base address */
#define BI_MAC_VDEPTH   0x8002  /* Mac video depth */
#define BI_MAC_VROW     0x8003  /* Mac video rowbytes */
#define BI_MAC_VDIM     0x8004  /* Mac video dimensions */
#define BI_MAC_VLOGICAL 0x8005  /* Mac video logical base */
#define BI_MAC_SCCBASE  0x8006  /* Mac SCC base address */
#define BI_MAC_BTIME    0x8007  /* Mac boot time */
#define BI_MAC_GMTBIAS  0x8008  /* Mac GMT timezone offset */
#define BI_MAC_MEMSIZE  0x8009  /* Mac RAM size (sanity check) */
#define BI_MAC_CPUID    0x800a  /* Mac CPU type (sanity check) */
#define BI_MAC_ROMBASE  0x800b  /* Mac system ROM base address */

/*  Macintosh hardware profile data */

#define BI_MAC_VIA1BASE 0x8010  /* Mac VIA1 base address (always present) */
#define BI_MAC_VIA2BASE 0x8011  /* Mac VIA2 base address (type varies) */
#define BI_MAC_VIA2TYPE 0x8012  /* Mac VIA2 type (VIA, RBV, OSS) */
#define BI_MAC_ADBTYPE  0x8013  /* Mac ADB interface type */
#define BI_MAC_ASCBASE  0x8014  /* Mac Apple Sound Chip base address */
#define BI_MAC_SCSI5380 0x8015  /* Mac NCR 5380 SCSI (base address, multi) */
#define BI_MAC_SCSIDMA  0x8016  /* Mac SCSI DMA (base address) */
#define BI_MAC_SCSI5396 0x8017  /* Mac NCR 53C96 SCSI (base address, multi) */
#define BI_MAC_IDETYPE  0x8018  /* Mac IDE interface type */
#define BI_MAC_IDEBASE  0x8019  /* Mac IDE interface base address */
#define BI_MAC_NUBUS    0x801a  /* Mac Nubus type (none, regular, pseudo) */
#define BI_MAC_SLOTMASK 0x801b  /* Mac Nubus slots present */
#define BI_MAC_SCCTYPE  0x801c  /* Mac SCC serial type (normal, IOP) */
#define BI_MAC_ETHTYPE  0x801d  /* Mac builtin ethernet type (Sonic, MACE */
#define BI_MAC_ETHBASE  0x801e  /* Mac builtin ethernet base address */
#define BI_MAC_PMU      0x801f  /* Mac power management / poweroff hardware */
#define BI_MAC_IOP_SWIM 0x8020  /* Mac SWIM floppy IOP */
#define BI_MAC_IOP_ADB  0x8021  /* Mac ADB IOP */

#define BOOTINFO0(as, base, id) \
    do { \
        stw_phys(as, base, id); \
        base += 2; \
        stw_phys(as, base, sizeof(struct bi_record)); \
        base += 2; \
    } while (0)

#define BOOTINFO1(as, base, id, value) \
    do { \
        stw_phys(as, base, id); \
        base += 2; \
        stw_phys(as, base, sizeof(struct bi_record) + 4); \
        base += 2; \
        stl_phys(as, base, value); \
        base += 4; \
    } while (0)

#define BOOTINFO2(as, base, id, value1, value2) \
    do { \
        stw_phys(as, base, id); \
        base += 2; \
        stw_phys(as, base, sizeof(struct bi_record) + 8); \
        base += 2; \
        stl_phys(as, base, value1); \
        base += 4; \
        stl_phys(as, base, value2); \
        base += 4; \
    } while (0)

#define BOOTINFOSTR(as, base, id, string) \
    do { \
        int i; \
        stw_phys(as, base, id); \
        base += 2; \
        stw_phys(as, base, \
                 (sizeof(struct bi_record) + strlen(string) + 2) & ~1); \
        base += 2; \
        for (i = 0; string[i]; i++) { \
            stb_phys(as, base++, string[i]); \
        } \
        stb_phys(as, base++, 0); \
        base = (parameters_base + 1) & ~1; \
    } while (0)
#endif
