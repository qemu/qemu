/*
 * QEMU System Emulator block driver
 * 
 * Copyright (c) 2003 Fabrice Bellard
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <malloc.h>
#include <termios.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/wait.h>

#include "vl.h"

struct BlockDriverState {
    int fd;
    int64_t total_sectors;
};

BlockDriverState *bdrv_open(const char *filename)
{
    BlockDriverState *bs;
    int fd;
    int64_t size;

    bs = malloc(sizeof(BlockDriverState));
    if(!bs)
        return NULL;
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        close(fd);
        free(bs);
        return NULL;
    }
    size = lseek64(fd, 0, SEEK_END);
    bs->total_sectors = size / 512;
    bs->fd = fd;
    return bs;
}

void bdrv_close(BlockDriverState *bs)
{
    close(bs->fd);
    free(bs);
}

/* return -1 if error */
int bdrv_read(BlockDriverState *bs, int64_t sector_num, 
              uint8_t *buf, int nb_sectors)
{
    int ret;

    lseek64(bs->fd, sector_num * 512, SEEK_SET);
    ret = read(bs->fd, buf, nb_sectors * 512);
    if (ret != nb_sectors * 512)
        return -1;
    else
        return 0;
}

/* return -1 if error */
int bdrv_write(BlockDriverState *bs, int64_t sector_num, 
               const uint8_t *buf, int nb_sectors)
{
    int ret;

    lseek64(bs->fd, sector_num * 512, SEEK_SET);
    ret = write(bs->fd, buf, nb_sectors * 512);
    if (ret != nb_sectors * 512)
        return -1;
    else
        return 0;
}

void bdrv_get_geometry(BlockDriverState *bs, int64_t *nb_sectors_ptr)
{
    *nb_sectors_ptr = bs->total_sectors;
}
