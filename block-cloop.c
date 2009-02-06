/*
 * QEMU Block driver for CLOOP images
 *
 * Copyright (c) 2004 Johannes E. Schindelin
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
#include "qemu-common.h"
#include "block_int.h"
#include <zlib.h>

typedef struct BDRVCloopState {
    int fd;
    uint32_t block_size;
    uint32_t n_blocks;
    uint64_t* offsets;
    uint32_t sectors_per_block;
    uint32_t current_block;
    uint8_t *compressed_block;
    uint8_t *uncompressed_block;
    z_stream zstream;
} BDRVCloopState;

static int cloop_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const char* magic_version_2_0="#!/bin/sh\n"
	"#V2.0 Format\n"
	"modprobe cloop file=$0 && mount -r -t iso9660 /dev/cloop $1\n";
    int length=strlen(magic_version_2_0);
    if(length>buf_size)
	length=buf_size;
    if(!memcmp(magic_version_2_0,buf,length))
	return 2;
    return 0;
}

static int cloop_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVCloopState *s = bs->opaque;
    uint32_t offsets_size,max_compressed_block_size=1,i;

    s->fd = open(filename, O_RDONLY | O_BINARY);
    if (s->fd < 0)
        return -errno;
    bs->read_only = 1;

    /* read header */
    if(lseek(s->fd,128,SEEK_SET)<0) {
cloop_close:
	close(s->fd);
	return -1;
    }
    if(read(s->fd,&s->block_size,4)<4)
	goto cloop_close;
    s->block_size=be32_to_cpu(s->block_size);
    if(read(s->fd,&s->n_blocks,4)<4)
	goto cloop_close;
    s->n_blocks=be32_to_cpu(s->n_blocks);

    /* read offsets */
    offsets_size=s->n_blocks*sizeof(uint64_t);
    s->offsets=(uint64_t*)qemu_malloc(offsets_size);
    if(read(s->fd,s->offsets,offsets_size)<offsets_size)
	goto cloop_close;
    for(i=0;i<s->n_blocks;i++) {
	s->offsets[i]=be64_to_cpu(s->offsets[i]);
	if(i>0) {
	    uint32_t size=s->offsets[i]-s->offsets[i-1];
	    if(size>max_compressed_block_size)
		max_compressed_block_size=size;
	}
    }

    /* initialize zlib engine */
    s->compressed_block = qemu_malloc(max_compressed_block_size+1);
    s->uncompressed_block = qemu_malloc(s->block_size);
    if(inflateInit(&s->zstream) != Z_OK)
	goto cloop_close;
    s->current_block=s->n_blocks;

    s->sectors_per_block = s->block_size/512;
    bs->total_sectors = s->n_blocks*s->sectors_per_block;
    return 0;
}

static inline int cloop_read_block(BDRVCloopState *s,int block_num)
{
    if(s->current_block != block_num) {
	int ret;
        uint32_t bytes = s->offsets[block_num+1]-s->offsets[block_num];

	lseek(s->fd, s->offsets[block_num], SEEK_SET);
        ret = read(s->fd, s->compressed_block, bytes);
        if (ret != bytes)
            return -1;

	s->zstream.next_in = s->compressed_block;
	s->zstream.avail_in = bytes;
	s->zstream.next_out = s->uncompressed_block;
	s->zstream.avail_out = s->block_size;
	ret = inflateReset(&s->zstream);
	if(ret != Z_OK)
	    return -1;
	ret = inflate(&s->zstream, Z_FINISH);
	if(ret != Z_STREAM_END || s->zstream.total_out != s->block_size)
	    return -1;

	s->current_block = block_num;
    }
    return 0;
}

static int cloop_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVCloopState *s = bs->opaque;
    int i;

    for(i=0;i<nb_sectors;i++) {
	uint32_t sector_offset_in_block=((sector_num+i)%s->sectors_per_block),
	    block_num=(sector_num+i)/s->sectors_per_block;
	if(cloop_read_block(s, block_num) != 0)
	    return -1;
	memcpy(buf+i*512,s->uncompressed_block+sector_offset_in_block*512,512);
    }
    return 0;
}

static void cloop_close(BlockDriverState *bs)
{
    BDRVCloopState *s = bs->opaque;
    close(s->fd);
    if(s->n_blocks>0)
	free(s->offsets);
    free(s->compressed_block);
    free(s->uncompressed_block);
    inflateEnd(&s->zstream);
}

BlockDriver bdrv_cloop = {
    "cloop",
    sizeof(BDRVCloopState),
    cloop_probe,
    cloop_open,
    cloop_read,
    NULL,
    cloop_close,
};
