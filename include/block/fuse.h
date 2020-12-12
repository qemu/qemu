/*
 * Present a block device as a raw image through FUSE
 *
 * Copyright (c) 2020 Max Reitz <mreitz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 or later of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BLOCK_FUSE_H
#define BLOCK_FUSE_H

#ifdef CONFIG_FUSE

#include "block/export.h"

extern const BlockExportDriver blk_exp_fuse;

#endif /* CONFIG_FUSE */

#endif
