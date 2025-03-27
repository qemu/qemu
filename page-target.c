/*
 * QEMU page values getters (target independent)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "exec/target_page.h"

/* Convert target pages to MiB (2**20). */
size_t qemu_target_pages_to_MiB(size_t pages)
{
    int page_bits = TARGET_PAGE_BITS;

    /* So far, the largest (non-huge) page size is 64k, i.e. 16 bits. */
    g_assert(page_bits < 20);

    return pages >> (20 - page_bits);
}
