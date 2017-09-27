/*
 * QEMU live block migration
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Liran Schour   <lirans@il.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef MIGRATION_BLOCK_H
#define MIGRATION_BLOCK_H

#ifdef CONFIG_LIVE_BLOCK_MIGRATION
int blk_mig_active(void);
int blk_mig_bulk_active(void);
uint64_t blk_mig_bytes_transferred(void);
uint64_t blk_mig_bytes_remaining(void);
uint64_t blk_mig_bytes_total(void);

#else
static inline int blk_mig_active(void)
{
    return false;
}

static inline int blk_mig_bulk_active(void)
{
    return false;
}

static inline uint64_t blk_mig_bytes_transferred(void)
{
    return 0;
}

static inline uint64_t blk_mig_bytes_remaining(void)
{
    return 0;
}

static inline uint64_t blk_mig_bytes_total(void)
{
    return 0;
}
#endif /* CONFIG_LIVE_BLOCK_MIGRATION */

void migrate_set_block_enabled(bool value, Error **errp);
#endif /* MIGRATION_BLOCK_H */
