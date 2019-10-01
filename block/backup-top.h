/*
 * backup-top filter driver
 *
 * The driver performs Copy-Before-Write (CBW) operation: it is injected above
 * some node, and before each write it copies _old_ data to the target node.
 *
 * Copyright (c) 2018-2019 Virtuozzo International GmbH.
 *
 * Author:
 *  Sementsov-Ogievskiy Vladimir <vsementsov@virtuozzo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BACKUP_TOP_H
#define BACKUP_TOP_H

#include "block/block_int.h"
#include "block/block-copy.h"

BlockDriverState *bdrv_backup_top_append(BlockDriverState *source,
                                         BlockDriverState *target,
                                         const char *filter_node_name,
                                         uint64_t cluster_size,
                                         BdrvRequestFlags write_flags,
                                         BlockCopyState **bcs,
                                         Error **errp);
void bdrv_backup_top_drop(BlockDriverState *bs);

#endif /* BACKUP_TOP_H */
