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

/* #define DEBUG_FALLBACK */

#ifdef DEBUG_FALLBACK
#define dputs(txt) \
    do { sclp_print("zipl: " txt); } while (0)
#else
#define dputs(fmt, ...) \
    do { } while (0)
#endif

/* Scratch space */
static uint8_t sec[MAX_SECTOR_SIZE]
__attribute__((__aligned__(MAX_SECTOR_SIZE)));

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

/* Check for ZIPL magic. Returns 0 if not matched. */
static int zipl_magic(uint8_t *ptr)
{
    uint32_t *p = (void *)ptr;
    uint32_t *z = (void *)ZIPL_MAGIC;

    if (*p != *z) {
        debug_print_int("invalid magic", *p);
        virtio_panic("invalid magic");
    }

    return 1;
}

static void zipl_load_segment(ComponentEntry *entry)
{
    const int max_entries = (MAX_SECTOR_SIZE / sizeof(ScsiBlockPtr));
    ScsiBlockPtr *bprs = (void *)sec;
    const int bprs_size = sizeof(sec);
    block_number_t blockno;
    long address;
    int i;

    blockno = entry->data.blockno;
    address = entry->load_address;

    debug_print_int("loading segment at block", blockno);
    debug_print_int("addr", address);

    do {
        memset(bprs, FREE_SPACE_FILLER, bprs_size);
        debug_print_int("reading bprs at", blockno);
        read_block(blockno, bprs, "zipl_load_segment: cannot read block");

        for (i = 0;; i++) {
            u64 *cur_desc = (void *)&bprs[i];

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
            IPL_assert(address != -1, "zipl_load_segment: wrong IPL address");
        }
    } while (blockno);
}

/* Run a zipl program */
static void zipl_run(ScsiBlockPtr *pte)
{
    ComponentHeader *header;
    ComponentEntry *entry;
    uint8_t tmp_sec[MAX_SECTOR_SIZE];

    virtio_read(pte->blockno, tmp_sec);
    header = (ComponentHeader *)tmp_sec;

    IPL_assert(zipl_magic(tmp_sec), "zipl_run: zipl_magic");

    IPL_assert(header->type == ZIPL_COMP_HEADER_IPL,
               "zipl_run: wrong header type");

    dputs("start loading images\n");

    /* Load image(s) into RAM */
    entry = (ComponentEntry *)(&header[1]);
    while (entry->component_type == ZIPL_COMP_ENTRY_LOAD) {
        zipl_load_segment(entry);

        entry++;

        IPL_assert((uint8_t *)(&entry[1]) <= (tmp_sec + MAX_SECTOR_SIZE),
                   "zipl_run: wrong entry size");
    }

    IPL_assert(entry->component_type == ZIPL_COMP_ENTRY_EXEC,
               "zipl_run: no EXEC entry");

    /* should not return */
    jump_to_IPL_code(entry->load_address);
}

void zipl_load(void)
{
    ScsiMbr *mbr = (void *)sec;
    uint8_t *ns, *ns_end;
    int program_table_entries = 0;
    const int pte_len = sizeof(ScsiBlockPtr);
    ScsiBlockPtr *prog_table_entry;

    /* Grab the MBR */
    read_block(0, mbr, "zipl_load: cannot read block 0");

    dputs("checking magic\n");

    IPL_assert(zipl_magic(mbr->magic), "zipl_load: zipl_magic 1");

    debug_print_int("program table", mbr->blockptr.blockno);

    /* Parse the program table */
    read_block(mbr->blockptr.blockno, sec,
               "zipl_load: cannot read program table");

    IPL_assert(zipl_magic(sec), "zipl_load: zipl_magic 2");

    ns_end = sec + virtio_get_block_size();
    for (ns = (sec + pte_len); (ns + pte_len) < ns_end; ns++) {
        prog_table_entry = (ScsiBlockPtr *)ns;
        if (!prog_table_entry->blockno) {
            break;
        }

        program_table_entries++;
    }

    debug_print_int("program table entries", program_table_entries);

    IPL_assert(program_table_entries, "zipl_load: no program table");

    /* Run the default entry */

    prog_table_entry = (ScsiBlockPtr *)(sec + pte_len);

    zipl_run(prog_table_entry); /* no return */
}
