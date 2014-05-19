/*
 * QEMU S390 bootmap interpreter
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "s390-ccw.h"
#include "bootmap.h"
#include "virtio.h"

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

typedef struct ResetInfo {
    uint32_t ipl_mask;
    uint32_t ipl_addr;
    uint32_t ipl_continue;
} ResetInfo;

ResetInfo save;

static void jump_to_IPL_2(void)
{
    ResetInfo *current = 0;

    void (*ipl)(void) = (void *) (uint64_t) current->ipl_continue;
    debug_print_addr("set IPL addr to", ipl);

    /* Ensure the guest output starts fresh */
    sclp_print("\n");

    *current = save;
    ipl(); /* should not return */
}

static void jump_to_IPL_code(uint64_t address)
{
    /*
     * The IPL PSW is at address 0. We also must not overwrite the
     * content of non-BIOS memory after we loaded the guest, so we
     * save the original content and restore it in jump_to_IPL_2.
     */
    ResetInfo *current = 0;

    save = *current;
    current->ipl_addr = (uint32_t) (uint64_t) &jump_to_IPL_2;
    current->ipl_continue = address & 0x7fffffff;

    /*
     * HACK ALERT.
     * We use the load normal reset to keep r15 unchanged. jump_to_IPL_2
     * can then use r15 as its stack pointer.
     */
    asm volatile("lghi 1,1\n\t"
                 "diag 1,1,0x308\n\t"
                 : : : "1", "memory");
    virtio_panic("\n! IPL returns !\n");
}

/***********************************************************************
 * IPL an ECKD DASD (CDL or LDL/CMS format)
 */

static unsigned char _bprs[8*1024]; /* guessed "max" ECKD sector size */
const int max_bprs_entries = sizeof(_bprs) / sizeof(ExtEckdBlockPtr);

static bool eckd_valid_address(BootMapPointer *p)
{
    const uint64_t cylinder = p->eckd.cylinder
                            + ((p->eckd.head & 0xfff0) << 12);
    const uint64_t head = p->eckd.head & 0x000f;

    if (head >= virtio_get_heads()
        ||  p->eckd.sector > virtio_get_sectors()
        ||  p->eckd.sector <= 0) {
        return false;
    }

    if (!virtio_guessed_disk_nature() && cylinder >= virtio_get_cylinders()) {
        return false;
    }

    return true;
}

static block_number_t eckd_block_num(BootMapPointer *p)
{
    const uint64_t sectors = virtio_get_sectors();
    const uint64_t heads = virtio_get_heads();
    const uint64_t cylinder = p->eckd.cylinder
                            + ((p->eckd.head & 0xfff0) << 12);
    const uint64_t head = p->eckd.head & 0x000f;
    const block_number_t block = sectors * heads * cylinder
                               + sectors * head
                               + p->eckd.sector
                               - 1; /* block nr starts with zero */
    return block;
}

static block_number_t load_eckd_segments(block_number_t blk, uint64_t *address)
{
    block_number_t block_nr;
    int j, rc;
    BootMapPointer *bprs = (void *)_bprs;
    bool more_data;

    memset(_bprs, FREE_SPACE_FILLER, sizeof(_bprs));
    read_block(blk, bprs, "BPRS read failed");

    do {
        more_data = false;
        for (j = 0;; j++) {
            block_nr = eckd_block_num((void *)&(bprs[j].xeckd));
            if (is_null_block_number(block_nr)) { /* end of chunk */
                break;
            }

            /* we need the updated blockno for the next indirect entry
             * in the chain, but don't want to advance address
             */
            if (j == (max_bprs_entries - 1)) {
                break;
            }

            IPL_assert(block_size_ok(bprs[j].xeckd.bptr.size),
                       "bad chunk block size");
            IPL_assert(eckd_valid_address(&bprs[j]), "bad chunk ECKD addr");

            if ((bprs[j].xeckd.bptr.count == 0) && unused_space(&(bprs[j+1]),
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
            rc = virtio_read_many(block_nr, (void *)(*address),
                                  bprs[j].xeckd.bptr.count+1);
            IPL_assert(rc == 0, "code chunk read failed");

            *address += (bprs[j].xeckd.bptr.count+1) * virtio_get_block_size();
        }
    } while (more_data);
    return block_nr;
}

static void run_eckd_boot_script(block_number_t mbr_block_nr)
{
    int i;
    block_number_t block_nr;
    uint64_t address;
    ScsiMbr *scsi_mbr = (void *)sec;
    BootMapScript *bms = (void *)sec;

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(mbr_block_nr, sec, "Cannot read MBR");

    block_nr = eckd_block_num((void *)&(scsi_mbr->blockptr));

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(block_nr, sec, "Cannot read Boot Map Script");

    for (i = 0; bms->entry[i].type == BOOT_SCRIPT_LOAD; i++) {
        address = bms->entry[i].address.load_address;
        block_nr = eckd_block_num(&(bms->entry[i].blkptr));

        do {
            block_nr = load_eckd_segments(block_nr, &address);
        } while (block_nr != -1);
    }

    IPL_assert(bms->entry[i].type == BOOT_SCRIPT_EXEC,
               "Unknown script entry type");
    jump_to_IPL_code(bms->entry[i].address.load_address); /* no return */
}

static void ipl_eckd(void)
{
    XEckdMbr *mbr;
    Ipl2 *ipl2 = (void *)sec;
    IplVolumeLabel *vlbl = (void *)sec;
    block_number_t block_nr;

    sclp_print("Using ECKD scheme.\n");
    if (virtio_guessed_disk_nature()) {
        sclp_print("Using guessed DASD geometry.\n");
        virtio_assume_eckd();
    }
    /* we have just read the block #0 and recognized it as "IPL1" */

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(1, ipl2, "Cannot read IPL2 record at block 1");
    IPL_assert(magic_match(ipl2, IPL2_MAGIC), "No IPL2 record");

    mbr = &ipl2->u.x.mbr;
    IPL_assert(magic_match(mbr, ZIPL_MAGIC), "No zIPL section in IPL2 record.");
    IPL_assert(block_size_ok(mbr->blockptr.xeckd.bptr.size),
               "Bad block size in zIPL section of IPL2 record.");
    IPL_assert(mbr->dev_type == DEV_TYPE_ECKD,
               "Non-ECKD device type in zIPL section of IPL2 record.");

    /* save pointer to Boot Script */
    block_nr = eckd_block_num((void *)&(mbr->blockptr));

    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(2, vlbl, "Cannot read Volume Label at block 2");
    IPL_assert(magic_match(vlbl->key, VOL1_MAGIC),
               "Invalid magic of volume label block");
    IPL_assert(magic_match(vlbl->f.key, VOL1_MAGIC),
               "Invalid magic of volser block");
    print_volser(vlbl->f.volser);

    run_eckd_boot_script(block_nr);
    /* no return */
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
    address = entry->load_address;

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

    IPL_assert(magic_match(tmp_sec, ZIPL_MAGIC), "No zIPL magic");
    IPL_assert(header->type == ZIPL_COMP_HEADER_IPL, "Bad header type");

    dputs("start loading images\n");

    /* Load image(s) into RAM */
    entry = (ComponentEntry *)(&header[1]);
    while (entry->component_type == ZIPL_COMP_ENTRY_LOAD) {
        zipl_load_segment(entry);

        entry++;

        IPL_assert((uint8_t *)(&entry[1]) <= (tmp_sec + MAX_SECTOR_SIZE),
                   "Wrong entry value");
    }

    IPL_assert(entry->component_type == ZIPL_COMP_ENTRY_EXEC, "No EXEC entry");

    /* should not return */
    jump_to_IPL_code(entry->load_address);
}

static void ipl_scsi(void)
{
    ScsiMbr *mbr = (void *)sec;
    uint8_t *ns, *ns_end;
    int program_table_entries = 0;
    const int pte_len = sizeof(ScsiBlockPtr);
    ScsiBlockPtr *prog_table_entry;

    /* The 0-th block (MBR) was already read into sec[] */

    sclp_print("Using SCSI scheme.\n");
    debug_print_int("program table", mbr->blockptr.blockno);

    /* Parse the program table */
    read_block(mbr->blockptr.blockno, sec,
               "Error reading Program Table");

    IPL_assert(magic_match(sec, ZIPL_MAGIC), "No zIPL magic");

    ns_end = sec + virtio_get_block_size();
    for (ns = (sec + pte_len); (ns + pte_len) < ns_end; ns++) {
        prog_table_entry = (ScsiBlockPtr *)ns;
        if (!prog_table_entry->blockno) {
            break;
        }

        program_table_entries++;
    }

    debug_print_int("program table entries", program_table_entries);

    IPL_assert(program_table_entries != 0, "Empty Program Table");

    /* Run the default entry */

    prog_table_entry = (ScsiBlockPtr *)(sec + pte_len);

    zipl_run(prog_table_entry); /* no return */
}

/***********************************************************************
 * IPL starts here
 */

void zipl_load(void)
{
    ScsiMbr *mbr = (void *)sec;

    /* Grab the MBR */
    memset(sec, FREE_SPACE_FILLER, sizeof(sec));
    read_block(0, mbr, "Cannot read block 0");

    dputs("checking magic\n");

    if (magic_match(mbr->magic, ZIPL_MAGIC)) {
        ipl_scsi(); /* no return */
    }
    if (magic_match(mbr->magic, IPL1_MAGIC)) {
        ipl_eckd(); /* CDL ECKD; no return */
    }

    virtio_panic("\n* invalid MBR magic *\n");
}
