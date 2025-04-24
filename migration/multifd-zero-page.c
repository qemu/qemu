/*
 * Multifd zero page detection implementation.
 *
 * Copyright (c) 2024 Bytedance Inc
 *
 * Authors:
 *  Hao Xiang <hao.xiang@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "system/ramblock.h"
#include "migration.h"
#include "migration-stats.h"
#include "multifd.h"
#include "options.h"
#include "ram.h"

static bool multifd_zero_page_enabled(void)
{
    return migrate_zero_page_detection() == ZERO_PAGE_DETECTION_MULTIFD;
}

static void swap_page_offset(ram_addr_t *pages_offset, int a, int b)
{
    ram_addr_t temp;

    if (a == b) {
        return;
    }

    temp = pages_offset[a];
    pages_offset[a] = pages_offset[b];
    pages_offset[b] = temp;
}

/**
 * multifd_send_zero_page_detect: Perform zero page detection on all pages.
 *
 * Sorts normal pages before zero pages in p->pages->offset and updates
 * p->pages->normal_num.
 *
 * @param p A pointer to the send params.
 */
void multifd_send_zero_page_detect(MultiFDSendParams *p)
{
    MultiFDPages_t *pages = &p->data->u.ram;
    RAMBlock *rb = pages->block;
    int i = 0;
    int j = pages->num - 1;

    if (!multifd_zero_page_enabled()) {
        pages->normal_num = pages->num;
        goto out;
    }

    /*
     * Sort the page offset array by moving all normal pages to
     * the left and all zero pages to the right of the array.
     */
    while (i <= j) {
        uint64_t offset = pages->offset[i];

        if (!buffer_is_zero(rb->host + offset, multifd_ram_page_size())) {
            i++;
            continue;
        }

        swap_page_offset(pages->offset, i, j);
        ram_release_page(rb->idstr, offset);
        j--;
    }

    pages->normal_num = i;

out:
    stat64_add(&mig_stats.normal_pages, pages->normal_num);
    stat64_add(&mig_stats.zero_pages, pages->num - pages->normal_num);
}

void multifd_recv_zero_page_process(MultiFDRecvParams *p)
{
    for (int i = 0; i < p->zero_num; i++) {
        void *page = p->host + p->zero[i];
        if (ramblock_recv_bitmap_test_byte_offset(p->block, p->zero[i])) {
            memset(page, 0, multifd_ram_page_size());
        } else {
            ramblock_recv_bitmap_set_offset(p->block, p->zero[i]);
        }
    }
}
