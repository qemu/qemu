/*
   Copyright (C) Matthew Chapman 2003

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#define SECTOR_BITS	9
#define SECTOR_SIZE	(1 << SECTOR_BITS)
#define SECTOR_MASK	(SECTOR_SIZE - 1)

#define L1_BITS		(SECTOR_BITS - 3)
#define L1_SIZE		(1 << L1_BITS)
#define L1_MASK		(L1_SIZE - 1)

#define L2_BITS		SECTOR_BITS
#define L2_SIZE		(1 << L2_BITS)
#define L2_MASK		(L2_SIZE - 1)

#define MIN(x,y)	(((x) < (y)) ? (x) : (y))

struct cowdisk_header
{
    uint32_t version;
    uint32_t flags;
    uint32_t disk_sectors;
    uint32_t granularity;
    uint32_t l1dir_offset;
    uint32_t l1dir_size;
    uint32_t file_sectors;
    uint32_t cylinders;
    uint32_t heads;
    uint32_t sectors_per_track;
};

struct cowdisk_header2
{
   uint32_t parent_ts;
   uint32_t timestamp;
};

/* based on vdk 3.1 10-11-2003 by Ken Kato */

struct vmdisk_header
{
    uint32_t version;
    uint32_t flags;
    
    int64_t capacity;
    int64_t granularity;
    int64_t desc_offset;
    int64_t desc_size;
    int32_t num_gtes_per_gte;
    int64_t rgd_offset;
    int64_t gd_offset;
    int64_t grain_offset;

    char filler[1];

    char check_bytes[4];
};
