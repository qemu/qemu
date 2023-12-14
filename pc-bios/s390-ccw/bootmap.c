/*
 * QEMU S390 bootmap interpreter
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
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
    do { sclp_print("zipl: " txt); } while (0)
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

static inline void verify_boot_info(BootInfo *bip)
{
    IPL_assert(magic_match(bip->magic, ZIPL_MAGIC), "No zIPL sig in BootInfo");
    IPL_assert(bip->version == BOOT_INFO_VERSION, "Wrong zIPL version");
    IPL_assert(bip->bp_type == BOOT_INFO_BP_TYPE_IPL, "DASD is not for IPL");
    IPL_assert(bip->dev_type == BOOT_INFO_DEV_TYPE_ECKD, "DASD is not ECKD");
    IPL_assert(bip->flags == BOOT_INFO_FLAGS_ARCH, "Not for this arch");
    IPL_assert(block_size_ok(bip->bp.ipl.bm_ptr.eckd.bptr.size),
               "Bad block size in zIPL section of the 1st record.");
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
    read_block(blk, bprs, "BPRS read failed");

    do {
        more_data = false;
        for (j = 0;; j++) {
            block_nr = gen_eckd_block_num(&bprs[j].xeckd, ldipl);
            if (is_null_block_number(block_nr)) { /* end of chunk */
                break;
            }

            /* we need the updated blockno for the next indirect entry
             * in the chain, but don't want to advance address
             */
            if (j == (max_bprs_entries - 1)) {
                break;
            }

            /* List directed pointer does not store block size */
            IPL_assert(ldipl || block_size_ok(bprs[j].xeckd.bptr.size),
                       "bad chunk block size");

            if (!eckd_valid_address(&bprs[j].xeckd, ldipl)) {
                /*
                 * If an invalid address is found during LD-IPL then break and
                 * retry as CCW
                 */
                IPL_assert(ldipl, "bad chunk ECKD addr");
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
                read_block(block_nr, bprs, "BPRS continuation read failed");
                more_data = true;
                break;
            }

            /* Load (count+1) blocks of code at (block_nr)
             * to memory (address).
             */
            rc = virtio_read_many(block_nr, (void *)(*address), count + 1);
            IPL_assert(rc == 0, "code chunk read failed");

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
    read_block(s1b_block_nr, s1b, "Cannot read stage1b boot loader");

    memset(_s2, FREE_SPACE_FILLER, sizeof(_s2));

    /* Get Stage2 data */
    for (i = 0; i < STAGE2_BLK_CNT_MAX; i++) {
        cur_block_nr = eckd_block_num(&s1b->seek[i].chs);

        if (!cur_block_nr || is_null_block_number(cur_block_nr)) {
            break;
        }

        read_block(cur_block_nr, s2_cur_blk, "Cannot read stage2 boot loader");

        if (find_zipl_boot_menu_banner(&banner_offset)) {
            /*
             * Load the adjacent blocks to account for the
             * possibility of menu data spanning multiple blocks.
             */
            if (prev_block_nr) {
                read_block(prev_block_nr, s2_prev_blk,
                           "Cannot read stage2 boot loader");
            }

            if (i + 1 < STAGE2_BLK_CNT_MAX) {
                next_block_nr = eckd_block_num(&s1b->seek[i + 1].chs);
            }

            if (next_block_nr && !is_null_block_number(next_block_nr)) {
                read_block(next_block_nr, s2_next_blk,
                           "Cannot read stage2 boot loader");
            }

            return menu_get_zipl_boot_index(s2_cur_blk + banner_offset);
        }

        prev_block_nr = cur_block_nr;
    }

    sclp_print("No zipl boot menu data found. Booting default entry.");
    return 0;
}

static void run_eckd_boot_script(block_number_t bmt_block_nr,
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
    IPL_assert(loadparm < MAX_BOOT_ENTRIES, "loadparm value greater than"
               " maximum number of boot entries allowed");

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(bmt_block_nr, sec, "Cannot read Boot Map Table");

    block_nr = gen_eckd_block_num(&bmt->entry[loadparm].xeckd, ldipl);
    IPL_assert(block_nr != -1, "Cannot find Boot Map Table Entry");

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(block_nr, sec, "Cannot read Boot Map Script");

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
        } while (block_nr != -1);
    }

    if (ldipl && bms->entry[i].type != BOOT_SCRIPT_EXEC) {
        /* Abort LD-IPL and retry as CCW-IPL */
        return;
    }

    IPL_assert(bms->entry[i].type == BOOT_SCRIPT_EXEC,
               "Unknown script entry type");
    write_reset_psw(bms->entry[i].address.load_address); /* no return */
    jump_to_IPL_code(0); /* no return */
}

static void ipl_eckd_cdl(void)
{
    XEckdMbr *mbr;
    EckdCdlIpl2 *ipl2 = (void *)sec;
    IplVolumeLabel *vlbl = (void *)sec;
    block_number_t bmt_block_nr, s1b_block_nr;

    /* we have just read the block #0 and recognized it as "IPL1" */
    sclp_print("CDL\n");

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(1, ipl2, "Cannot read IPL2 record at block 1");

    mbr = &ipl2->mbr;
    if (!magic_match(mbr, ZIPL_MAGIC)) {
        sclp_print("No zIPL section in IPL2 record.\n");
        return;
    }
    if (!block_size_ok(mbr->blockptr.xeckd.bptr.size)) {
        sclp_print("Bad block size in zIPL section of IPL2 record.\n");
        return;
    }
    if (mbr->dev_type != DEV_TYPE_ECKD) {
        sclp_print("Non-ECKD device type in zIPL section of IPL2 record.\n");
        return;
    }

    /* save pointer to Boot Map Table */
    bmt_block_nr = eckd_block_num(&mbr->blockptr.xeckd.bptr.chs);

    /* save pointer to Stage1b Data */
    s1b_block_nr = eckd_block_num(&ipl2->stage1.seek[0].chs);

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(2, vlbl, "Cannot read Volume Label at block 2");
    if (!magic_match(vlbl->key, VOL1_MAGIC)) {
        sclp_print("Invalid magic of volume label block.\n");
        return;
    }
    if (!magic_match(vlbl->f.key, VOL1_MAGIC)) {
        sclp_print("Invalid magic of volser block.\n");
        return;
    }
    print_volser(vlbl->f.volser);

    run_eckd_boot_script(bmt_block_nr, s1b_block_nr);
    /* no return */
}

static void print_eckd_ldl_msg(ECKD_IPL_mode_t mode)
{
    LDL_VTOC *vlbl = (void *)sec; /* already read, 3rd block */
    char msg[4] = { '?', '.', '\n', '\0' };

    sclp_print((mode == ECKD_CMS) ? "CMS" : "LDL");
    sclp_print(" version ");
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
    sclp_print(msg);
    print_volser(vlbl->volser);
}

static void ipl_eckd_ldl(ECKD_IPL_mode_t mode)
{
    block_number_t bmt_block_nr, s1b_block_nr;
    EckdLdlIpl1 *ipl1 = (void *)sec;

    if (mode != ECKD_LDL_UNLABELED) {
        print_eckd_ldl_msg(mode);
    }

    /* DO NOT read BootMap pointer (only one, xECKD) at block #2 */

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(0, sec, "Cannot read block 0 to grab boot info.");
    if (mode == ECKD_LDL_UNLABELED) {
        if (!magic_match(ipl1->bip.magic, ZIPL_MAGIC)) {
            return; /* not applicable layout */
        }
        sclp_print("unlabeled LDL.\n");
    }
    verify_boot_info(&ipl1->bip);

    /* save pointer to Boot Map Table */
    bmt_block_nr = eckd_block_num(&ipl1->bip.bp.ipl.bm_ptr.eckd.bptr.chs);

    /* save pointer to Stage1b Data */
    s1b_block_nr = eckd_block_num(&ipl1->stage1.seek[0].chs);

    run_eckd_boot_script(bmt_block_nr, s1b_block_nr);
    /* no return */
}

static block_number_t eckd_find_bmt(ExtEckdBlockPtr *ptr)
{
    block_number_t blockno;
    uint8_t tmp_sec[MAX_SECTOR_SIZE];
    BootRecord *br;

    blockno = gen_eckd_block_num(ptr, 0);
    read_block(blockno, tmp_sec, "Cannot read boot record");
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
    sclp_print(msg);
}

static void ipl_eckd(void)
{
    IplVolumeLabel *vlbl = (void *)sec;
    LDL_VTOC *vtoc = (void *)sec;
    block_number_t ldipl_bmt; /* Boot Map Table for List-Directed IPL */

    print_eckd_msg();

    /* Block 2 can contain either the CDL VOL1 label or the LDL VTOC */
    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(2, vlbl, "Cannot read block 2");

    /*
     * First check for a list-directed-format pointer which would
     * supersede the CCW pointer.
     */
    if (eckd_valid_address((ExtEckdBlockPtr *)&vlbl->f.br, 0)) {
        ldipl_bmt = eckd_find_bmt((ExtEckdBlockPtr *)&vlbl->f.br);
        if (ldipl_bmt) {
            sclp_print("List-Directed\n");
            /* LD-IPL does not use the S1B bock, just make it NULL */
            run_eckd_boot_script(ldipl_bmt, NULL_BLOCK_NR);
            /* Only return in error, retry as CCW-IPL */
            sclp_print("Retrying IPL ");
            print_eckd_msg();
        }
        memset(sec, FREE_SPACE_FILLER, sizeof(sec));
        read_block(2, vtoc, "Cannot read block 2");
    }

    /* Not list-directed */
    if (magic_match(vtoc->magic, VOL1_MAGIC)) {
        ipl_eckd_cdl(); /* may return in error */
    }

    if (magic_match(vtoc->magic, CMS1_MAGIC)) {
        ipl_eckd_ldl(ECKD_CMS); /* no return */
    }
    if (magic_match(vtoc->magic, LNX1_MAGIC)) {
        ipl_eckd_ldl(ECKD_LDL); /* no return */
    }

    ipl_eckd_ldl(ECKD_LDL_UNLABELED); /* it still may return */
    /*
     * Ok, it is not a LDL by any means.
     * It still might be a CDL with zero record keys for IPL1 and IPL2
     */
    ipl_eckd_cdl();
}

/***********************************************************************
 * IPL a SCSI disk
 */

static void zipl_load_segment(ComponentEntry *entry)
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
        read_block(blockno, bprs, err_msg);

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
            IPL_assert(address != -1, "zIPL load segment failed");
        }
    } while (blockno);
}

/* Run a zipl program */
static void zipl_run(ScsiBlockPtr *pte)
{
    ComponentHeader *header;
    ComponentEntry *entry;
    uint8_t tmp_sec[MAX_SECTOR_SIZE];

    read_block(pte->blockno, tmp_sec, "Cannot read header");
    header = (ComponentHeader *)tmp_sec;

    IPL_assert(magic_match(tmp_sec, ZIPL_MAGIC), "No zIPL magic in header");
    IPL_assert(header->type == ZIPL_COMP_HEADER_IPL, "Bad header type");

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

        zipl_load_segment(entry);

        entry++;

        IPL_assert((uint8_t *)(&entry[1]) <= (tmp_sec + MAX_SECTOR_SIZE),
                   "Wrong entry value");
    }

    IPL_assert(entry->component_type == ZIPL_COMP_ENTRY_EXEC, "No EXEC entry");

    /* should not return */
    write_reset_psw(entry->compdat.load_psw);
    jump_to_IPL_code(0);
}

static void ipl_scsi(void)
{
    ScsiMbr *mbr = (void *)sec;
    int program_table_entries = 0;
    BootMapTable *prog_table = (void *)sec;
    unsigned int loadparm = get_loadparm_index();
    bool valid_entries[MAX_BOOT_ENTRIES] = {false};
    size_t i;

    /* Grab the MBR */
    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(0, mbr, "Cannot read block 0");

    if (!magic_match(mbr->magic, ZIPL_MAGIC)) {
        return;
    }

    sclp_print("Using SCSI scheme.\n");
    debug_print_int("MBR Version", mbr->version_id);
    IPL_check(mbr->version_id == 1,
              "Unknown MBR layout version, assuming version 1");
    debug_print_int("program table", mbr->pt.blockno);
    IPL_assert(mbr->pt.blockno, "No Program Table");

    /* Parse the program table */
    read_block(mbr->pt.blockno, sec, "Error reading Program Table");
    IPL_assert(magic_match(sec, ZIPL_MAGIC), "No zIPL magic in PT");

    for (i = 0; i < MAX_BOOT_ENTRIES; i++) {
        if (prog_table->entry[i].scsi.blockno) {
            valid_entries[i] = true;
            program_table_entries++;
        }
    }

    debug_print_int("program table entries", program_table_entries);
    IPL_assert(program_table_entries != 0, "Empty Program Table");

    if (menu_is_enabled_enum()) {
        loadparm = menu_get_enum_boot_index(valid_entries);
    }

    debug_print_int("loadparm", loadparm);
    IPL_assert(loadparm < MAX_BOOT_ENTRIES, "loadparm value greater than"
               " maximum number of boot entries allowed");

    zipl_run(&prog_table->entry[loadparm].scsi); /* no return */
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
    read_iso_sector(bswap32(s->load_rba), magic_sec,
                    "Failed to read image sector 0");

    /* Checking bytes 8 - 32 for S390 Linux magic */
    return !memcmp(magic_sec + 8, linux_s390_magic, 24);
}

/* Location of the current sector of the directory */
static uint32_t sec_loc[ISO9660_MAX_DIR_DEPTH];
/* Offset in the current sector of the directory */
static uint32_t sec_offset[ISO9660_MAX_DIR_DEPTH];
/* Remained directory space in bytes */
static uint32_t dir_rem[ISO9660_MAX_DIR_DEPTH];

static inline uint32_t iso_get_file_size(uint32_t load_rba)
{
    IsoVolDesc *vd = (IsoVolDesc *)sec;
    IsoDirHdr *cur_record = &vd->vd.primary.rootdir;
    uint8_t *temp = sec + ISO_SECTOR_SIZE;
    int level = 0;

    read_iso_sector(ISO_PRIMARY_VD_SECTOR, sec,
                    "Failed to read ISO primary descriptor");
    sec_loc[0] = iso_733_to_u32(cur_record->ext_loc);
    dir_rem[0] = 0;
    sec_offset[0] = 0;

    while (level >= 0) {
        IPL_assert(sec_offset[level] <= ISO_SECTOR_SIZE,
                   "Directory tree structure violation");

        cur_record = (IsoDirHdr *)(temp + sec_offset[level]);

        if (sec_offset[level] == 0) {
            read_iso_sector(sec_loc[level], temp,
                            "Failed to read ISO directory");
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
                    sclp_print("ISO-9660 directory depth limit exceeded\n");
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
            read_iso_sector(sec_loc[level], temp,
                            "Failed to read ISO directory");
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
    uint32_t real_size = iso_get_file_size(bswap32(s.load_rba));

    if (real_size) {
        /* Round up blocks to load */
        blks_to_load = (real_size + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE;
        sclp_print("ISO boot image size verified\n");
    } else {
        sclp_print("ISO boot image size could not be verified\n");
    }

    read_iso_boot_image(bswap32(s.load_rba),
                        (void *)((uint64_t)bswap16(s.load_segment)),
                        blks_to_load);

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
        read_iso_sector(block_num++, sec,
                        "Failed to read ISO volume descriptor");
    }

    return 0;
}

static IsoBcSection *find_iso_bc_entry(void)
{
    IsoBcEntry *e = (IsoBcEntry *)sec;
    uint32_t offset = find_iso_bc();
    int i;
    unsigned int loadparm = get_loadparm_index();

    if (!offset) {
        return NULL;
    }

    read_iso_sector(offset, sec, "Failed to read El Torito boot catalog");

    if (!is_iso_bc_valid(e)) {
        /* The validation entry is mandatory */
        panic("No valid boot catalog found!\n");
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

    panic("No suitable boot entry found on ISO-9660 media!\n");

    return NULL;
}

static void ipl_iso_el_torito(void)
{
    IsoBcSection *s = find_iso_bc_entry();

    if (s) {
        load_iso_bc_entry(s);
        /* no return */
    }
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

static void zipl_load_vblk(void)
{
    int blksize = virtio_get_block_size();

    if (blksize == VIRTIO_ISO_BLOCK_SIZE || has_iso_signature()) {
        if (blksize != VIRTIO_ISO_BLOCK_SIZE) {
            virtio_assume_iso9660();
        }
        ipl_iso_el_torito();
    }

    if (blksize != VIRTIO_DASD_DEFAULT_BLOCK_SIZE) {
        sclp_print("Using guessed DASD geometry.\n");
        virtio_assume_eckd();
    }
    ipl_eckd();
}

static void zipl_load_vscsi(void)
{
    if (virtio_get_block_size() == VIRTIO_ISO_BLOCK_SIZE) {
        /* Is it an ISO image in non-CD drive? */
        ipl_iso_el_torito();
    }

    sclp_print("Using guessed DASD geometry.\n");
    virtio_assume_eckd();
    ipl_eckd();
}

/***********************************************************************
 * IPL starts here
 */

void zipl_load(void)
{
    VDev *vdev = virtio_get_device();

    if (vdev->is_cdrom) {
        ipl_iso_el_torito();
        panic("\n! Cannot IPL this ISO image !\n");
    }

    if (virtio_get_device_type() == VIRTIO_ID_NET) {
        jump_to_IPL_code(vdev->netboot_start_addr);
    }

    ipl_scsi();

    switch (virtio_get_device_type()) {
    case VIRTIO_ID_BLOCK:
        zipl_load_vblk();
        break;
    case VIRTIO_ID_SCSI:
        zipl_load_vscsi();
        break;
    default:
        panic("\n! Unknown IPL device type !\n");
    }

    sclp_print("zIPL load failed.\n");
}
