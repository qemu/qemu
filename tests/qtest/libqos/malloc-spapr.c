/*
 * libqos malloc support for SPAPR
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/malloc-spapr.h"

#include "qemu-common.h"

#define PAGE_SIZE 4096

/* Memory must be a multiple of 256 MB,
 * so we have at least 256MB
 */
#define SPAPR_MIN_SIZE 0x10000000

void spapr_alloc_init(QGuestAllocator *s, QTestState *qts, QAllocOpts flags)
{
    alloc_init(s, flags, 1 << 20, SPAPR_MIN_SIZE, PAGE_SIZE);
}
