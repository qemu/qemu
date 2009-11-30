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

void blk_mig_init(void);
int blk_mig_active(void);
uint64_t blk_mig_bytes_transferred(void);
uint64_t blk_mig_bytes_remaining(void);
uint64_t blk_mig_bytes_total(void);

#endif /* BLOCK_MIGRATION_H */
