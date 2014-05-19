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

    virtio_panic("\n* invalid MBR magic *\n");
}
