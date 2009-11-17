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

#ifndef BLOCK_MIGRATION_H
#define BLOCK_MIGRATION_H

typedef struct BlkMigDevState {
    BlockDriverState *bs;
    int bulk_completed;
    int shared_base;
    struct BlkMigDevState *next;
    int64_t cur_sector;
    int64_t total_sectors;
    int64_t dirty;
} BlkMigDevState;
 
void blk_mig_init(void);
void blk_mig_info(void);
#endif /* BLOCK_MIGRATION_H */
