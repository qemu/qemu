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

struct cowdisk_header header;
struct cowdisk_header2 header2;
off_t disk_base, disk_limit;
unsigned int granule_size;
uint32_t l1dir[L1_SIZE];

unsigned int cached_l2dir;
uint32_t l2dir[L2_SIZE];

size_t read_physical(int fd, off64_t offset, size_t length, void *buffer)
{
    size_t n;

    if (lseek64(fd, offset, SEEK_SET) == -1)
    {
        perror("lseek");
        return -1;
    }

    n = read(fd, buffer, length);
    if (n == -1)
    {
        perror("read from disk");
        return -1;
    }

    return n;
}

size_t copy_virtual(int in_fd, int out_fd, off64_t offset, void *buffer, size_t length)
{
    unsigned int granule_index, granule_offset;
    unsigned int l1index, l2index;

    granule_index = offset / granule_size;
    granule_offset = offset % granule_size;
    length = MIN(length, granule_size - granule_offset);
    length = MIN(length, disk_limit - offset);

    l1index = (granule_index >> L2_BITS) & L1_MASK;
    l2index = granule_index & L2_MASK;

    if (l1dir[l1index] == 0)
        goto zero_fill;

    if (l1index != cached_l2dir)
    {
        if (read_physical(in_fd, (l1dir[l1index] << SECTOR_BITS), sizeof(l2dir), (char *)l2dir) != sizeof(l2dir))
            return 0;

        cached_l2dir = l1index;
    }

    if (l2dir[l2index] == 0)
        goto zero_fill;

    if (read_physical(in_fd, (l2dir[l2index] << SECTOR_BITS) + granule_offset, length, buffer) != length)
        return 0;

    write(out_fd, buffer, length);
    return length;

zero_fill:
    /* the last chunk of the file can not be sparse
     * or the file will be truncated */
    if (offset + length < disk_limit) {
        memset(buffer, 0, length);
        write(out_fd, buffer, length);
    } else {
        if (lseek(out_fd, length, SEEK_CUR) == (off_t)-1)
            perror("lseek");
    }
    return length;
}


int open_vmdk(const char *filename)
{
    int fd = open(filename, O_RDONLY | O_LARGEFILE);
    if (fd == -1)
    {
        perror(filename);
        return -1;
    }

    if (read(fd, &header, sizeof(header)) != sizeof(header))
    {
        perror("read from disk");
        return -1;
    }

    if (memcmp(header.magic, "COWD", 4) != 0)
    {
        fprintf(stderr, "%s is not a VMware virtual disk image\n", filename);
        return -1;
    }

    granule_size = header.granularity << SECTOR_BITS;
    if (read_physical(fd, header.l1dir_sector << SECTOR_BITS, sizeof(l1dir), (char *)l1dir) != sizeof(l1dir))
        return -1;

    disk_limit = header.disk_sectors << SECTOR_BITS;

    cached_l2dir = -1;
    return fd;
}

void help(void)
{
    printf("vmdk2raw\n"
           "usage: vmdk2raw vmware_image output_image\n"
           "\n"
           "vmware_image   a vmware 2.x/3.x cow image\n"
           "output_image   the created disk image\n" 
          );
    exit(1);
}

#define BUF_SIZE granule_size
void copy_disk(in_fd, out_fd)
{
    char buf[BUF_SIZE];
    off64_t i = 0;
    while (i < disk_limit) {
        i += copy_virtual(in_fd, out_fd, i, buf, sizeof(buf));
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

    out_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (out_fd < 0) {
        perror(argv[2]);
        return -1;
    }

    copy_disk(in_fd, out_fd);
    close(out_fd);
    return 0;
}
