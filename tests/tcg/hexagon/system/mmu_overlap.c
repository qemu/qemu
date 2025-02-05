/*
 *  Copyright(c) 2019-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define DEBUG        0

#include "mmu.h"

DEFAULT_EVENT_HANDLES

void test_overlap(void)
{
    uint32_t addr = (uint32_t)&data;
    uint32_t page = page_start(addr, 20);
    uint32_t offset = FIVE_MB;
    uint32_t new_page = page + offset;
    uint32_t new_addr = addr + offset;
    uint8_t data_perm = TLB_X | TLB_W | TLB_R | TLB_U;
    uint64_t entry;

    add_trans(1, new_page, page, PAGE_1M, data_perm, 0, 1, 1);
    check32(tlbp(0, new_addr), 1);

    /* Check an entry that overlaps with the one we just created */
    entry =
        create_mmu_entry(1, 0, 0, 0, new_page, 1, 1, 1, 0, 7, page, PAGE_4K);
    check32(tlboc(entry), 1);
    /* Check that conditional TLB write (ctlbw) does NOT write the new entry */
    check32(ctlbw(entry, 2), 0x1);

    /* Create an entry that does not overlap with the one we just created */
    entry = create_mmu_entry(1, 0, 0, 0, new_page + ONE_MB, 1, 1, 1, 0, 7, page,
                             PAGE_4K);
    check32(tlboc(entry), TLB_NOT_FOUND);
    /* Check that conditional TLB write (ctlbw) does write the new entry */
    check32(ctlbw(entry, 2), TLB_NOT_FOUND);

    /* Create an entry that overalps both of these entries */
    entry =
        create_mmu_entry(1, 0, 0, 0, new_page, 1, 1, 1, 0, 7, page, PAGE_4M);
    check32(tlboc(entry), 0xffffffff);

    /* Clear the TLB entries */
    remove_trans(1);
    check32(tlbp(0, new_addr), TLB_NOT_FOUND);
    remove_trans(2);
    check32(tlbp(0, (new_addr + ONE_MB)), TLB_NOT_FOUND);
}

int main()
{
    puts("Hexagon MMU overlap test");

    test_overlap();

    printf("%s\n", ((err) ? "FAIL" : "PASS"));
    return err;
}
