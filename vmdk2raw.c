/*
   vmdk2raw: convert vmware images to raw disk images
   Copyright (C) Net Integration Technologies 2004
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "vmdk.h"
#include "config-host.h"

static struct cowdisk_header header;
static struct vmdisk_header header4;
static off64_t disk_limit;
static unsigned int granule_size;
static uint32_t *l1dir;

static unsigned int cached_l2dir;
static uint32_t l2dir[L2_SIZE];

static struct vmdk_prm {
    uint32_t grain_table_size;
    uint32_t sectors_per_grain;
    uint32_t sectors_per_table;
    uint32_t directory_size;
} vdsk;

static size_t read_physical(int fd, off64_t offset, size_t length, void *buffer)
{
    size_t n;

    if (lseek64(fd, offset, SEEK_SET) == -1) {
        printf(" error trying to seek lseek to %lld", offset);
        return -1;
    }

    n = read(fd, buffer, length);
    
    if (n == -1) {
        printf("read from disk %lld", offset);
        return -1;
    }

    return n;
}

static int read_l1dir(int fd, size_t offset, int num)
{
    l1dir = malloc(sizeof(*l1dir) * num);
    if (!l1dir)
        return -1;
    return read_physical(fd, offset << SECTOR_BITS, sizeof(*l1dir) * num, (char *)l1dir) != (sizeof(*l1dir) * num);
}

static int read_l2dir(int fd, size_t offset, int num)
{
    return read_physical(fd, offset << SECTOR_BITS, sizeof(l2dir[0]) * num, (char *)l2dir) != sizeof(l2dir);
}

static size_t copy_virtual(struct vmdk_prm *dsk, int in_fd, int out_fd, off64_t offset, void *buffer, size_t length)
{
    
    unsigned int granule_offset;
    unsigned int grain_index;
    unsigned int sector_map_idx;
    
    granule_offset = offset % granule_size;
    length = MIN(length, granule_size - granule_offset);
    length = MIN(length, disk_limit - offset);

    sector_map_idx = (offset >> SECTOR_BITS) / dsk->sectors_per_table;
    
    if (sector_map_idx >= dsk->directory_size) {
        fprintf(stderr, "cannot locate grain table for %d in %d\n", sector_map_idx, dsk->directory_size);
        return -1;
    }

    if (l1dir[sector_map_idx] == 0)
        goto zero_fill;
   
    if (sector_map_idx != cached_l2dir) {
        if (read_l2dir(in_fd, l1dir[sector_map_idx], dsk->grain_table_size)) {
            fprintf(stderr, "read failed\n");
            return -1;
        }
        cached_l2dir = sector_map_idx;
    }

    grain_index = ((offset >> SECTOR_BITS) % dsk->sectors_per_table) / dsk->sectors_per_grain;
    
    if (grain_index >= dsk->grain_table_size) {
        fprintf(stderr, "grain to large");
        return -1;
    }

    if (l2dir[grain_index] == 0)
        goto zero_fill;

    if (read_physical(in_fd, (l2dir[grain_index] << SECTOR_BITS) + granule_offset, length, buffer) != length) {
        fprintf(stderr, "read error 2\n");
        return -1;
    }
    
    write(out_fd, buffer, length);
    return length;

zero_fill:
    /* the last chunk of the file can not be sparse
     * or the file will be truncated */
    if (offset + length >= disk_limit) {
        if (lseek64(out_fd, length-1, SEEK_CUR) == (off_t)-1)
            perror("lseek");
        /* write the last NULL byte instead of seeking */
        const char nil = 0;
        write(out_fd, &nil, 1);
    } else {
        if (lseek64(out_fd, length, SEEK_CUR) == (off_t)-1)
            perror("lseek");
    }
    return length;
}

static int open_vmdk4(int fd)
{
    if (read(fd, &header4, sizeof(header4)) != sizeof(header4)) {
        perror("read from disk");
        return -1;
    }
    
    granule_size = header4.granularity << SECTOR_BITS;
    disk_limit = header4.capacity << SECTOR_BITS; 
    
    cached_l2dir = -1;
    vdsk.grain_table_size = header4.num_gtes_per_gte;
    vdsk.sectors_per_grain = header4.granularity;
    vdsk.sectors_per_table = vdsk.grain_table_size * vdsk.sectors_per_grain;
    vdsk.directory_size = (header4.capacity + vdsk.sectors_per_table - 1) / vdsk.sectors_per_table + 1;

    if (read_l1dir(fd, header4.rgd_offset, vdsk.directory_size))
        return -1;
    
    return 0;
    
}

static int open_vmdk3(int fd)
{
    if (read(fd, &header, sizeof(header)) != sizeof(header)) {
        perror("read from disk\n");
        return -1;
    }
    granule_size = header.granularity << SECTOR_BITS;
    vdsk.sectors_per_grain = header.granularity;
    vdsk.grain_table_size = L2_SIZE;
    vdsk.sectors_per_table = vdsk.grain_table_size * vdsk.sectors_per_grain;
    vdsk.directory_size = L1_SIZE;
    if (read_l1dir(fd, header.l1dir_offset, L1_SIZE))
        return -1;

    disk_limit = header.disk_sectors << SECTOR_BITS;

    return fd;
}

static int open_vmdk(const char *filename)
{
    int fd = open(filename, O_RDONLY | O_LARGEFILE);
    if (fd == -1) {
        perror(filename);
        return -1;
    }

    char magic[4];
    if (read(fd, &magic, sizeof(magic)) != sizeof(magic)) {
        perror("read from disk");
        return -1;
    }
    
    if (!memcmp(magic, "KDMV", sizeof(magic))) {
        open_vmdk4(fd);
    } else if (!memcmp(magic, "COWD", sizeof(magic))) {
        open_vmdk3(fd);
    } else {
        fprintf(stderr, "%s is not a VMware virtual disk image\n", filename);
        return -1;
    }
    
    cached_l2dir = -1;
    return fd;
}

static void help(void)
{
    printf("vmdk2raw\n"
           "usage: vmdk2raw vmware_image output_image\n"
           "\n"
           "vmware_image   a vmware cow image\n"
           "output_image   the created disk image\n" 
          );
    exit(1);
}

#define BUF_SIZE 0x10000
static void copy_disk(in_fd, out_fd)
{
    char buf[BUF_SIZE];
    off64_t i = 0;
    int ret;
    while (i < disk_limit) {
        ret = copy_virtual(&vdsk, in_fd, out_fd, i, buf, sizeof(buf));
        if (ret < 0) {
            fprintf(stderr, "copying failed\n");
            exit(-1);
        }
        i += ret;
    }
}

int main(int argc, char **argv)
{
    int out_fd, in_fd;

    if (argc < 3)
        help();

    in_fd = open_vmdk(argv[1]);
    if (in_fd < 0) {
        return -1;
    }

    out_fd = open(argv[2], O_WRONLY | O_LARGEFILE | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (out_fd < 0) {
        perror(argv[2]);
        return -1;
    }

    copy_disk(in_fd, out_fd);
    close(out_fd);
    return 0;
}
