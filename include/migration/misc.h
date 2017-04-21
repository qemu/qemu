/*
 * QEMU migration miscellaneus exported functions
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef MIGRATION_MISC_H
#define MIGRATION_MISC_H

/* migration/ram.c */

void ram_mig_init(void);

/* migration/block.c */

#ifdef CONFIG_LIVE_BLOCK_MIGRATION
void blk_mig_init(void);
#else
static inline void blk_mig_init(void) {}
#endif

#define SELF_ANNOUNCE_ROUNDS 5

static inline
int64_t self_announce_delay(int round)
{
    assert(round < SELF_ANNOUNCE_ROUNDS && round > 0);
    /* delay 50ms, 150ms, 250ms, ... */
    return 50 + (SELF_ANNOUNCE_ROUNDS - round - 1) * 100;
}

#endif
