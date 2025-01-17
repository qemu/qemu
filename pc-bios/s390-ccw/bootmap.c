/*
 * QEMU S390 bootmap interpreter
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <string.h>
#include <stdio.h>
#include "s390-ccw.h"
#include "s390-arch.h"
#include "bootmap.h"
#include "virtio.h"
#include "bswap.h"

#ifdef DEBUG
/* #define DEBUG_FALLBACK */
#endif

#ifdef DEBUG_FALLBACK
#define dputs(txt) \
    do { printf("zipl: " txt); } while (0)
#else
#define dputs(fmt, ...) \
    do { } while (0)
#endif

/* Scratch space */
static uint8_t sec[MAX_SECTOR_SIZE*4] __attribute__((__aligned__(PAGE_SIZE)));

const uint8_t el_torito_magic[] = "EL TORITO SPECIFICATION"
                                  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

/*
 * Match two CCWs located after PSW and eight filler bytes.
 * From libmagic and arch/s390/kernel/head.S.
 */
const uint8_t linux_s390_magic[] = "\x02\x00\x00\x18\x60\x00\x00\x50\x02\x00"
                                   "\x00\x68\x60\x00\x00\x50\x40\x40\x40\x40"
                                   "\x40\x40\x40\x40";

static inline bool is_iso_vd_valid(IsoVolDesc *vd)
{
    const uint8_t vol_desc_magic[] = "CD001";

    return !memcmp(&vd->ident[0], vol_desc_magic, 5) &&
           vd->version == 0x1 &&
           vd->type <= VOL_DESC_TYPE_PARTITION;
}

/***********************************************************************
 * IPL an ECKD DASD (CDL or LDL/CMS format)
 */

static unsigned char _bprs[8*1024]; /* guessed "max" ECKD sector size */
static const int max_bprs_entries = sizeof(_bprs) / sizeof(ExtEckdBlockPtr);
static uint8_t _s2[MAX_SECTOR_SIZE * 3] __attribute__((__aligned__(PAGE_SIZE)));
static void *s2_prev_blk = _s2;
static void *s2_cur_blk = _s2 + MAX_SECTOR_SIZE;
static void *s2_next_blk = _s2 + MAX_SECTOR_SIZE * 2;

static inline int verify_boot_info(BootInfo *bip)
{
    if (!magic_match(bip->magic, ZIPL_MAGIC)) {
        puts("No zIPL sig in BootInfo");
        return -EINVAL;
    }
    if (bip->version != BOOT_INFO_VERSION) {
        puts("Wrong zIPL version");
        return -EINVAL;
    }
    if (bip->bp_type != BOOT_INFO_BP_TYPE_IPL) {
        puts("DASD is not for IPL");
        return -ENODEV;
    }
    if (bip->dev_type != BOOT_INFO_DEV_TYPE_ECKD) {
        puts("DASD is not ECKD");
        return -ENODEV;
    }
    if (bip->flags != BOOT_INFO_FLAGS_ARCH) {
        puts("Not for this arch");
        return -EINVAL;
    }
    if (!block_size_ok(bip->bp.ipl.bm_ptr.eckd.bptr.size)) {
        puts("Bad block size in zIPL section of 1st record");
        return -EINVAL;
    }

    return 0;
}

static void eckd_format_chs(ExtEckdBlockPtr *ptr,  bool ldipl,
                            uint64_t *c,
                            uint64_t *h,
                            uint64_t *s)
{
    if (ldipl) {
        *c = ptr->ldptr.chs.cylinder;
        *h = ptr->ldptr.chs.head;
        *s = ptr->ldptr.chs.sector;
    } else {
        *c = ptr->bptr.chs.cylinder;
        *h = ptr->bptr.chs.head;
        *s = ptr->bptr.chs.sector;
    }
}

static block_number_t eckd_chs_to_block(uint64_t c, uint64_t h, uint64_t s)
{
    const uint64_t sectors = virtio_get_sectors();
    const uint64_t heads = virtio_get_heads();
    const uint64_t cylinder = c + ((h & 0xfff0) << 12);
    const uint64_t head = h & 0x000f;
    const block_number_t block = sectors * heads * cylinder
                               + sectors * head
                               + s - 1; /* block nr starts with zero */
    return block;
}

static block_number_t eckd_block_num(EckdCHS *chs)
{
    return eckd_chs_to_block(chs->cylinder, chs->head, chs->sector);
}

static block_number_t gen_eckd_block_num(ExtEckdBlockPtr *ptr, bool ldipl)
{
    uint64_t cyl, head, sec;
    eckd_format_chs(ptr, ldipl, &cyl, &head, &sec);
    return eckd_chs_to_block(cyl, head, sec);
}

static bool eckd_valid_chs(uint64_t cyl, uint64_t head, uint64_t sector)
{
    if (head >= virtio_get_heads()
        || sector > virtio_get_sectors()
        || sector <= 0) {
        return false;
    }

    if (!virtio_guessed_disk_nature() &&
        eckd_chs_to_block(cyl, head, sector) >= virtio_get_blocks()) {
        return false;
    }

    return true;
}

static bool eckd_valid_address(ExtEckdBlockPtr *ptr, bool ldipl)
{
    uint64_t cyl, head, sec;
    eckd_format_chs(ptr, ldipl, &cyl, &head, &sec);
    return eckd_valid_chs(cyl, head, sec);
}

static block_number_t load_eckd_segments(block_number_t blk, bool ldipl,
                                         uint64_t *address)
{
    block_number_t block_nr;
    int j, rc, count;
    BootMapPointer *bprs = (void *)_bprs;
    bool more_data;

    memset(_bprs, FREE_SPACE_FILLER, sizeof(_bprs));
    if (virtio_read(blk, bprs)) {
        puts("BPRS read failed");
        return ERROR_BLOCK_NR;
    }

    do {
        more_data = false;
        for (j = 0;; j++) {
            block_nr = gen_eckd_block_num(&bprs[j].xeckd, ldipl);
            if (is_null_block_number(block_nr)) { /* end of chunk */
                return NULL_BLOCK_NR;
            }

            /* we need the updated blockno for the next indirect entry
             * in the chain, but don't want to advance address
             */
            if (j == (max_bprs_entries - 1)) {
                break;
            }

            /* List directed pointer does not store block size */
            if (!ldipl && !block_size_ok(bprs[j].xeckd.bptr.size)) {
                puts("Bad chunk block size");
                return ERROR_BLOCK_NR;
            }

            if (!eckd_valid_address(&bprs[j].xeckd, ldipl)) {
                /*
                 * If an invalid address is found during LD-IPL then break and
                 * retry as CCW-IPL, otherwise abort on error
                 */
                if (!ldipl) {
                    puts("Bad chunk ECKD address");
                    return ERROR_BLOCK_NR;
                }
                break;
            }

            if (ldipl) {
                count = bprs[j].xeckd.ldptr.count;
            } else {
                count = bprs[j].xeckd.bptr.count;
            }

            if (count == 0 && unused_space(&bprs[j + 1],
                sizeof(EckdBlockPtr))) {
                /* This is a "continue" pointer.
                 * This ptr should be the last one in the current
                 * script section.
                 * I.e. the next ptr must point to the unused memory area
                 */
                memset(_bprs, FREE_SPACE_FILLER, sizeof(_bprs));
                if (virtio_read(block_nr, bprs)) {
                    puts("BPRS continuation read failed");
                    return ERROR_BLOCK_NR;
                }
                more_data = true;
                break;
            }

            /* Load (count+1) blocks of code at (block_nr)
             * to memory (address).
             */
            rc = virtio_read_many(block_nr, (void *)(*address), count + 1);
            if (rc != 0) {
                puts("Code chunk read failed");
                return ERROR_BLOCK_NR;
            }

            *address += (count + 1) * virtio_get_block_size();
        }
    } while (more_data);
    return block_nr;
}

static bool find_zipl_boot_menu_banner(int *offset)
{
    int i;

    /* Menu banner starts with "zIPL" */
    for (i = 0; i <= virtio_get_block_size() - 4; i++) {
        if (magic_match(s2_cur_blk + i, ZIPL_MAGIC_EBCDIC)) {
            *offset = i;
            return true;
        }
    }

    return false;
}

static int eckd_get_boot_menu_index(block_number_t s1b_block_nr)
{
    block_number_t cur_block_nr;
    block_number_t prev_block_nr = 0;
    block_number_t next_block_nr = 0;
    EckdStage1b *s1b = (void *)sec;
    int banner_offset;
    int i;

    /* Get Stage1b data */
    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    if (virtio_read(s1b_block_nr, s1b)) {
        puts("Cannot read stage1b boot loader");
        return -EIO;
    }

    memset(_s2, FREE_SPACE_FILLER, sizeof(_s2));

    /* Get Stage2 data */
    for (i = 0; i < STAGE2_BLK_CNT_MAX; i++) {
        cur_block_nr = eckd_block_num(&s1b->seek[i].chs);

        if (!cur_block_nr || is_null_block_number(cur_block_nr)) {
            break;
        }

        if (virtio_read(cur_block_nr, s2_cur_blk)) {
            puts("Cannot read stage2 boot loader");
            return -EIO;
        }

        if (find_zipl_boot_menu_banner(&banner_offset)) {
            /*
             * Load the adjacent blocks to account for the
             * possibility of menu data spanning multiple blocks.
             */
            if (prev_block_nr) {
                if (virtio_read(prev_block_nr, s2_prev_blk)) {
                    puts("Cannot read stage2 boot loader");
                    return -EIO;
                }
            }

            if (i + 1 < STAGE2_BLK_CNT_MAX) {
                next_block_nr = eckd_block_num(&s1b->seek[i + 1].chs);
            }

            if (next_block_nr && !is_null_block_number(next_block_nr)) {
                if (virtio_read(next_block_nr, s2_next_blk)) {
                    puts("Cannot read stage2 boot loader");
                    return -EIO;
                }
            }

            return menu_get_zipl_boot_index(s2_cur_blk + banner_offset);
        }

        prev_block_nr = cur_block_nr;
    }

    printf("No zipl boot menu data found. Booting default entry.");
    return 0;
}

static int run_eckd_boot_script(block_number_t bmt_block_nr,
                                 block_number_t s1b_block_nr)
{
    int i;
    unsigned int loadparm = get_loadparm_index();
    block_number_t block_nr;
    uint64_t address;
    BootMapTable *bmt = (void *)sec;
    BootMapScript *bms = (void *)sec;
    /* The S1B block number is NULL_BLOCK_NR if and only if it's an LD-IPL */
    bool ldipl = (s1b_block_nr == NULL_BLOCK_NR);

    if (menu_is_enabled_zipl() && !ldipl) {
        loadparm = eckd_get_boot_menu_index(s1b_block_nr);
    }

    debug_print_int("loadparm", loadparm);
    if (loadparm >= MAX_BOOT_ENTRIES) {
        panic("loadparm value greater than max number of boot entries allowed");
    }

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    if (virtio_read(bmt_block_nr, sec)) {
        puts("Cannot read Boot Map Table");
        return -EIO;
    }

    block_nr = gen_eckd_block_num(&bmt->entry[loadparm].xeckd, ldipl);
    if (block_nr == NULL_BLOCK_NR) {
        printf("The requested boot entry (%d) is invalid\n", loadparm);
        panic("Invalid loadparm");
    }

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    if (virtio_read(block_nr, sec)) {
        puts("Cannot read Boot Map Script");
        return -EIO;
    }

    for (i = 0; bms->entry[i].type == BOOT_SCRIPT_LOAD ||
                bms->entry[i].type == BOOT_SCRIPT_SIGNATURE; i++) {

        /* We don't support secure boot yet, so we skip signature entries */
        if (bms->entry[i].type == BOOT_SCRIPT_SIGNATURE) {
            continue;
        }

        address = bms->entry[i].address.load_address;
        block_nr = gen_eckd_block_num(&bms->entry[i].blkptr.xeckd, ldipl);

        do {
            block_nr = load_eckd_segments(block_nr, ldipl, &address);
            if (block_nr == ERROR_BLOCK_NR) {
                return ldipl ? 0 : -EIO;
            }
        } while (block_nr != NULL_BLOCK_NR);
    }

    if (ldipl && bms->entry[i].type != BOOT_SCRIPT_EXEC) {
        /* Abort LD-IPL and retry as CCW-IPL */
        return 0;
    }

    if (bms->entry[i].type != BOOT_SCRIPT_EXEC) {
        puts("Unknown script entry type");
        return -EINVAL;
    }
    write_reset_psw(bms->entry[i].address.load_address);
    jump_to_IPL_code(0);
    return -1;
}

static int ipl_eckd_cdl(void)
{
    XEckdMbr *mbr;
    EckdCdlIpl2 *ipl2 = (void *)sec;
    IplVolumeLabel *vlbl = (void *)sec;
    block_number_t bmt_block_nr, s1b_block_nr;

    /* we have just read the block #0 and recognized it as "IPL1" */
    puts("CDL");

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    if (virtio_read(1, ipl2)) {
        puts("Cannot read IPL2 record at block 1");
        return -EIO;
    }

    mbr = &ipl2->mbr;
    if (!magic_match(mbr, ZIPL_MAGIC)) {
        puts("No zIPL section in IPL2 record.");
        return 0;
    }
    if (!block_size_ok(mbr->blockptr.xeckd.bptr.size)) {
        puts("Bad block size in zIPL section of IPL2 record.");
        return 0;
    }
    if (mbr->dev_type != DEV_TYPE_ECKD) {
        puts("Non-ECKD device type in zIPL section of IPL2 record.");
        return 0;
    }

    /* save pointer to Boot Map Table */
    bmt_block_nr = eckd_block_num(&mbr->blockptr.xeckd.bptr.chs);

    /* save pointer to Stage1b Data */
    s1b_block_nr = eckd_block_num(&ipl2->stage1.seek[0].chs);

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    if (virtio_read(2, vlbl)) {
        puts("Cannot read Volume Label at block 2");
        return -EIO;
    }
    if (!magic_match(vlbl->key, VOL1_MAGIC)) {
        puts("Invalid magic of volume label block.");
        return 0;
    }
    if (!magic_match(vlbl->f.key, VOL1_MAGIC)) {
        puts("Invalid magic of volser block.");
        return 0;
    }
    print_volser(vlbl->f.volser);

    return run_eckd_boot_script(bmt_block_nr, s1b_block_nr);
}

static void print_eckd_ldl_msg(ECKD_IPL_mode_t mode)
{
    LDL_VTOC *vlbl = (void *)sec; /* already read, 3rd block */
    char msg[4] = { '?', '.', '\n', '\0' };

    printf((mode == ECKD_CMS) ? "CMS" : "LDL");
    printf(" version ");
    switch (vlbl->LDL_version) {
    case LDL1_VERSION:
        msg[0] = '1';
        break;
    case LDL2_VERSION:
        msg[0] = '2';
        break;
    default:
        msg[0] = ebc2asc[vlbl->LDL_version];
        msg[1] = '?';
        break;
    }
    printf("%s", msg);
    print_volser(vlbl->volser);
}

static int ipl_eckd_ldl(ECKD_IPL_mode_t mode)
{
    block_number_t bmt_block_nr, s1b_block_nr;
    EckdLdlIpl1 *ipl1 = (void *)sec;

    if (mode != ECKD_LDL_UNLABELED) {
        print_eckd_ldl_msg(mode);
    }

    /* DO NOT read BootMap pointer (only one, xECKD) at block #2 */

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    if (virtio_read(0, sec)) {
        puts("Cannot read block 0 to grab boot info.");
        return -EIO;
    }
    if (mode == ECKD_LDL_UNLABELED) {
        if (!magic_match(ipl1->bip.magic, ZIPL_MAGIC)) {
            return 0; /* not applicable layout */
        }
        puts("unlabeled LDL.");
    }
    verify_boot_info(&ipl1->bip);

    /* save pointer to Boot Map Table */
    bmt_block_nr = eckd_block_num(&ipl1->bip.bp.ipl.bm_ptr.eckd.bptr.chs);

    /* save pointer to Stage1b Data */
    s1b_block_nr = eckd_block_num(&ipl1->stage1.seek[0].chs);

    return run_eckd_boot_script(bmt_block_nr, s1b_block_nr);
}

static block_number_t eckd_find_bmt(ExtEckdBlockPtr *ptr)
{
    block_number_t blockno;
    uint8_t tmp_sec[MAX_SECTOR_SIZE];
    BootRecord *br;

    blockno = gen_eckd_block_num(ptr, 0);
    if (virtio_read(blockno, tmp_sec)) {
        puts("Cannot read boot record");
        return ERROR_BLOCK_NR;
    }
    br = (BootRecord *)tmp_sec;
    if (!magic_match(br->magic, ZIPL_MAGIC)) {
        /* If the boot record is invalid, return and try CCW-IPL instead */
        return NULL_BLOCK_NR;
    }

    return gen_eckd_block_num(&br->pgt.xeckd, 1);
}

static void print_eckd_msg(void)
{
    char msg[] = "Using ECKD scheme (block size *****), ";
    char *p = &msg[34], *q = &msg[30];
    int n = virtio_get_block_size();

    /* Fill in the block size and show up the message */
    if (n > 0 && n <= 99999) {
        while (n) {
            *p-- = '0' + (n % 10);
            n /= 10;
        }
        while (p >= q) {
            *p-- = ' ';
        }
    }
    printf("%s", msg);
}

static int ipl_eckd(void)
{
    IplVolumeLabel *vlbl = (void *)sec;
    LDL_VTOC *vtoc = (void *)sec;
    block_number_t ldipl_bmt; /* Boot Map Table for List-Directed IPL */

    print_eckd_msg();

    /* Block 2 can contain either the CDL VOL1 label or the LDL VTOC */
    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    if (virtio_read(2, vlbl)) {
        puts("Cannot read block 2");
        return -EIO;
    }

    /*
     * First check for a list-directed-format pointer which would
     * supersede the CCW pointer.
     */
    if (eckd_valid_address((ExtEckdBlockPtr *)&vlbl->f.br, 0)) {
        ldipl_bmt = eckd_find_bmt((ExtEckdBlockPtr *)&vlbl->f.br);
        switch (ldipl_bmt) {
        case ERROR_BLOCK_NR:
            return -EIO;
        case NULL_BLOCK_NR:
            break; /* Invalid BMT but the device may still boot with CCW-IPL */
        default:
            puts("List-Directed");
            /*
             * LD-IPL does not use the S1B bock, just make it NULL_BLOCK_NR.
             * In some failure cases retry IPL before aborting.
             */
            if (run_eckd_boot_script(ldipl_bmt, NULL_BLOCK_NR)) {
                return -EIO;
            }
            /* Non-fatal error, retry as CCW-IPL */
            printf("Retrying IPL ");
            print_eckd_msg();
        }
        memset(sec, FREE_SPACE_FILLER, sizeof(sec));
        if (virtio_read(2, vtoc)) {
            puts("Cannot read block 2");
            return -EIO;
        }
    }

    /* Not list-directed */
    if (magic_match(vtoc->magic, VOL1_MAGIC)) {
        if (ipl_eckd_cdl()) {
            return -1;
        }
    }

    if (magic_match(vtoc->magic, CMS1_MAGIC)) {
        return ipl_eckd_ldl(ECKD_CMS);
    }
    if (magic_match(vtoc->magic, LNX1_MAGIC)) {
        return ipl_eckd_ldl(ECKD_LDL);
    }

    if (ipl_eckd_ldl(ECKD_LDL_UNLABELED)) {
        return -1;
    }
    /*
     * Ok, it is not a LDL by any means.
     * It still might be a CDL with zero record keys for IPL1 and IPL2
     */
    return ipl_eckd_cdl();
}

/***********************************************************************
 * IPL a SCSI disk
 */

static int zipl_load_segment(ComponentEntry *entry)
{
    const int max_entries = (MAX_SECTOR_SIZE / sizeof(ScsiBlockPtr));
    ScsiBlockPtr *bprs = (void *)sec;
    const int bprs_size = sizeof(sec);
    block_number_t blockno;
    uint64_t address;
    int i;
    char err_msg[] = "zIPL failed to read BPRS at 0xZZZZZZZZZZZZZZZZ";
    char *blk_no = &err_msg[30]; /* where to print blockno in (those ZZs) */

    blockno = entry->data.blockno;
    address = entry->compdat.load_addr;

    debug_print_int("loading segment at block", blockno);
    debug_print_int("addr", address);

    do {
        memset(bprs, FREE_SPACE_FILLER, bprs_size);
        fill_hex_val(blk_no, &blockno, sizeof(blockno));
        if (virtio_read(blockno, bprs)) {
            puts(err_msg);
            return -EIO;
        }

        for (i = 0;; i++) {
            uint64_t *cur_desc = (void *)&bprs[i];

            blockno = bprs[i].blockno;
            if (!blockno) {
                break;
            }

            /* we need the updated blockno for the next indirect entry in the
               chain, but don't want to advance address */
            if (i == (max_entries - 1)) {
                break;
            }

            if (bprs[i].blockct == 0 && unused_space(&bprs[i + 1],
                sizeof(ScsiBlockPtr))) {
                /* This is a "continue" pointer.
                 * This ptr is the last one in the current script section.
                 * I.e. the next ptr must point to the unused memory area.
                 * The blockno is not zero, so the upper loop must continue
                 * reading next section of BPRS.
                 */
                break;
            }
            address = virtio_load_direct(cur_desc[0], cur_desc[1], 0,
                                         (void *)address);
            if (!address) {
                puts("zIPL load segment failed");
                return -EIO;
            }
        }
    } while (blockno);

    return 0;
}

/* Run a zipl program */
static int zipl_run(ScsiBlockPtr *pte)
{
    ComponentHeader *header;
    ComponentEntry *entry;
    uint8_t tmp_sec[MAX_SECTOR_SIZE];

    if (virtio_read(pte->blockno, tmp_sec)) {
        puts("Cannot read header");
        return -EIO;
    }
    header = (ComponentHeader *)tmp_sec;

    if (!magic_match(tmp_sec, ZIPL_MAGIC)) {
        puts("No zIPL magic in header");
        return -EINVAL;
    }
    if (header->type != ZIPL_COMP_HEADER_IPL) {
        puts("Bad header type");
        return -EINVAL;
    }

    dputs("start loading images\n");

    /* Load image(s) into RAM */
    entry = (ComponentEntry *)(&header[1]);
    while (entry->component_type == ZIPL_COMP_ENTRY_LOAD ||
           entry->component_type == ZIPL_COMP_ENTRY_SIGNATURE) {

        /* We don't support secure boot yet, so we skip signature entries */
        if (entry->component_type == ZIPL_COMP_ENTRY_SIGNATURE) {
            entry++;
            continue;
        }

        if (zipl_load_segment(entry)) {
            return -1;
        }

        entry++;

        if ((uint8_t *)(&entry[1]) > (tmp_sec + MAX_SECTOR_SIZE)) {
            puts("Wrong entry value");
            return -EINVAL;
        }
    }

    if (entry->component_type != ZIPL_COMP_ENTRY_EXEC) {
        puts("No EXEC entry");
        return -EINVAL;
    }

    /* should not return */
    write_reset_psw(entry->compdat.load_psw);
    jump_to_IPL_code(0);
    return -1;
}

static int ipl_scsi(void)
{
    ScsiMbr *mbr = (void *)sec;
    int program_table_entries = 0;
    BootMapTable *prog_table = (void *)sec;
    unsigned int loadparm = get_loadparm_index();
    bool valid_entries[MAX_BOOT_ENTRIES] = {false};
    size_t i;

    /* Grab the MBR */
    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    if (virtio_read(0, mbr)) {
        puts("Cannot read block 0");
        return -EIO;
    }

    if (!magic_match(mbr->magic, ZIPL_MAGIC)) {
        return 0;
    }

    puts("Using SCSI scheme.");
    debug_print_int("MBR Version", mbr->version_id);
    IPL_check(mbr->version_id == 1,
              "Unknown MBR layout version, assuming version 1");
    debug_print_int("program table", mbr->pt.blockno);
    if (!mbr->pt.blockno) {
        puts("No Program Table");
        return -EINVAL;
    }

    /* Parse the program table */
    if (virtio_read(mbr->pt.blockno, sec)) {
        puts("Error reading Program Table");
        return -EIO;
    }
    if (!magic_match(sec, ZIPL_MAGIC)) {
        puts("No zIPL magic in Program Table");
        return -EINVAL;
    }

    for (i = 0; i < MAX_BOOT_ENTRIES; i++) {
        if (prog_table->entry[i].scsi.blockno) {
            valid_entries[i] = true;
            program_table_entries++;
        }
    }

    debug_print_int("program table entries", program_table_entries);
    if (program_table_entries == 0) {
        puts("Empty Program Table");
        return -EINVAL;
    }

    if (menu_is_enabled_enum()) {
        loadparm = menu_get_enum_boot_index(valid_entries);
    }

    debug_print_int("loadparm", loadparm);
    if (loadparm >= MAX_BOOT_ENTRIES) {
        panic("loadparm value greater than max number of boot entries allowed");
    }

    if (!valid_entries[loadparm]) {
        printf("The requested boot entry (%d) is invalid\n", loadparm);
        panic("Invalid loadparm");
    }

    return zipl_run(&prog_table->entry[loadparm].scsi);
}

/***********************************************************************
 * IPL El Torito ISO9660 image or DVD
 */

static bool is_iso_bc_entry_compatible(IsoBcSection *s)
{
    uint8_t *magic_sec = (uint8_t *)(sec + ISO_SECTOR_SIZE);

    if (s->unused || !s->sector_count) {
        return false;
    }
    if (virtio_read(bswap32(s->load_rba), magic_sec)) {
        puts("Failed to read image sector 0");
        return false;
    }

    /* Checking bytes 8 - 32 for S390 Linux magic */
    return !memcmp(magic_sec + 8, linux_s390_magic, 24);
}

/* Location of the current sector of the directory */
static uint32_t sec_loc[ISO9660_MAX_DIR_DEPTH];
/* Offset in the current sector of the directory */
static uint32_t sec_offset[ISO9660_MAX_DIR_DEPTH];
/* Remained directory space in bytes */
static uint32_t dir_rem[ISO9660_MAX_DIR_DEPTH];

static inline long iso_get_file_size(uint32_t load_rba)
{
    IsoVolDesc *vd = (IsoVolDesc *)sec;
    IsoDirHdr *cur_record = &vd->vd.primary.rootdir;
    uint8_t *temp = sec + ISO_SECTOR_SIZE;
    int level = 0;

    if (virtio_read(ISO_PRIMARY_VD_SECTOR, sec)) {
        puts("Failed to read ISO primary descriptor");
        return -EIO;
    }

    sec_loc[0] = iso_733_to_u32(cur_record->ext_loc);
    dir_rem[0] = 0;
    sec_offset[0] = 0;

    while (level >= 0) {
        if (sec_offset[level] > ISO_SECTOR_SIZE) {
            puts("Directory tree structure violation");
            return -EIO;
        }

        cur_record = (IsoDirHdr *)(temp + sec_offset[level]);

        if (sec_offset[level] == 0) {
            if (virtio_read(sec_loc[level], temp)) {
                puts("Failed to read ISO directory");
                return -EIO;
            }
            if (dir_rem[level] == 0) {
                /* Skip self and parent records */
                dir_rem[level] = iso_733_to_u32(cur_record->data_len) -
                                 cur_record->dr_len;
                sec_offset[level] += cur_record->dr_len;

                cur_record = (IsoDirHdr *)(temp + sec_offset[level]);
                dir_rem[level] -= cur_record->dr_len;
                sec_offset[level] += cur_record->dr_len;
                continue;
            }
        }

        if (!cur_record->dr_len || sec_offset[level] == ISO_SECTOR_SIZE) {
            /* Zero-padding and/or the end of current sector */
            dir_rem[level] -= ISO_SECTOR_SIZE - sec_offset[level];
            sec_offset[level] = 0;
            sec_loc[level]++;
        } else {
            /* The directory record is valid */
            if (load_rba == iso_733_to_u32(cur_record->ext_loc)) {
                return iso_733_to_u32(cur_record->data_len);
            }

            dir_rem[level] -= cur_record->dr_len;
            sec_offset[level] += cur_record->dr_len;

            if (cur_record->file_flags & 0x2) {
                /* Subdirectory */
                if (level == ISO9660_MAX_DIR_DEPTH - 1) {
                    puts("ISO-9660 directory depth limit exceeded");
                } else {
                    level++;
                    sec_loc[level] = iso_733_to_u32(cur_record->ext_loc);
                    sec_offset[level] = 0;
                    dir_rem[level] = 0;
                    continue;
                }
            }
        }

        if (dir_rem[level] == 0) {
            /* Nothing remaining */
            level--;
            if (virtio_read(sec_loc[level], temp)) {
                puts("Failed to read ISO directory");
                return -EIO;
            }
        }
    }

    return 0;
}

static void load_iso_bc_entry(IsoBcSection *load)
{
    IsoBcSection s = *load;
    /*
     * According to spec, extent for each file
     * is padded and ISO_SECTOR_SIZE bytes aligned
     */
    uint32_t blks_to_load = bswap16(s.sector_count) >> ET_SECTOR_SHIFT;
    long real_size = iso_get_file_size(bswap32(s.load_rba));

    if (real_size > 0) {
        /* Round up blocks to load */
        blks_to_load = (real_size + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE;
        puts("ISO boot image size verified");
    } else {
        puts("ISO boot image size could not be verified");
        if (real_size < 0) {
            return;
        }
    }

    if (read_iso_boot_image(bswap32(s.load_rba),
                        (void *)((uint64_t)bswap16(s.load_segment)),
                        blks_to_load)) {
        return;
    }

    jump_to_low_kernel();
}

static uint32_t find_iso_bc(void)
{
    IsoVolDesc *vd = (IsoVolDesc *)sec;
    uint32_t block_num = ISO_PRIMARY_VD_SECTOR;

    if (virtio_read_many(block_num++, sec, 1)) {
        /* If primary vd cannot be read, there is no boot catalog */
        return 0;
    }

    while (is_iso_vd_valid(vd) && vd->type != VOL_DESC_TERMINATOR) {
        if (vd->type == VOL_DESC_TYPE_BOOT) {
            IsoVdElTorito *et = &vd->vd.boot;

            if (!memcmp(&et->el_torito[0], el_torito_magic, 32)) {
                return bswap32(et->bc_offset);
            }
        }
        if (virtio_read(block_num++, sec)) {
            puts("Failed to read ISO volume descriptor");
            return 0;
        }
    }

    return 0;
}

static IsoBcSection *find_iso_bc_entry(uint32_t offset)
{
    IsoBcEntry *e = (IsoBcEntry *)sec;
    int i;
    unsigned int loadparm = get_loadparm_index();

    if (!offset) {
        return NULL;
    }

    if (virtio_read(offset, sec)) {
        puts("Failed to read El Torito boot catalog");
        return NULL;
    }

    if (!is_iso_bc_valid(e)) {
        /* The validation entry is mandatory */
        return NULL;
    }

    /*
     * Each entry has 32 bytes size, so one sector cannot contain > 64 entries.
     * We consider only boot catalogs with no more than 64 entries.
     */
    for (i = 1; i < ISO_BC_ENTRY_PER_SECTOR; i++) {
        if (e[i].id == ISO_BC_BOOTABLE_SECTION) {
            if (is_iso_bc_entry_compatible(&e[i].body.sect)) {
                if (loadparm <= 1) {
                    /* found, default, or unspecified */
                    return &e[i].body.sect;
                }
                loadparm--;
            }
        }
    }

    return NULL;
}

static int ipl_iso_el_torito(void)
{
    uint32_t offset = find_iso_bc();
    if (!offset) {
        return 0;
    }

    IsoBcSection *s = find_iso_bc_entry(offset);

    if (s) {
        load_iso_bc_entry(s); /* only return in error */
        return -1;
    }

    puts("No suitable boot entry found on ISO-9660 media!");
    return -EIO;
}

/**
 * Detect whether we're trying to boot from an .ISO image.
 * These always have a signature string "CD001" at offset 0x8001.
 */
static bool has_iso_signature(void)
{
    int blksize = virtio_get_block_size();

    if (!blksize || virtio_read(0x8000 / blksize, sec)) {
        return false;
    }

    return !memcmp("CD001", &sec[1], 5);
}

/***********************************************************************
 * Bus specific IPL sequences
 */

static int zipl_load_vblk(void)
{
    int blksize = virtio_get_block_size();

    if (blksize == VIRTIO_ISO_BLOCK_SIZE || has_iso_signature()) {
        if (blksize != VIRTIO_ISO_BLOCK_SIZE) {
            virtio_assume_iso9660();
        }
        if (ipl_iso_el_torito()) {
            return 0;
        }
    }

    if (blksize != VIRTIO_DASD_DEFAULT_BLOCK_SIZE) {
        puts("Using guessed DASD geometry.");
        virtio_assume_eckd();
    }
    return ipl_eckd();
}

static int zipl_load_vscsi(void)
{
    if (virtio_get_block_size() == VIRTIO_ISO_BLOCK_SIZE) {
        /* Is it an ISO image in non-CD drive? */
        if (ipl_iso_el_torito()) {
            return 0;
        }
    }

    puts("Using guessed DASD geometry.");
    virtio_assume_eckd();
    return ipl_eckd();
}

/***********************************************************************
 * IPL starts here
 */

void zipl_load(void)
{
    VDev *vdev = virtio_get_device();

    if (vdev->is_cdrom) {
        ipl_iso_el_torito();
        puts("Failed to IPL this ISO image!");
        return;
    }

    if (virtio_get_device_type() == VIRTIO_ID_NET) {
        netmain();
        puts("Failed to IPL from this network!");
        return;
    }

    if (ipl_scsi()) {
        puts("Failed to IPL from this SCSI device!");
        return;
    }

    switch (virtio_get_device_type()) {
    case VIRTIO_ID_BLOCK:
        zipl_load_vblk();
        break;
    case VIRTIO_ID_SCSI:
        zipl_load_vscsi();
        break;
    default:
        puts("Unknown IPL device type!");
        return;
    }

    puts("zIPL load failed!");
}
