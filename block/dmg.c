/*
 * QEMU Block driver for DMG images
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
#include "bswap.h"
#include "module.h"
#include <zlib.h>

typedef struct BDRVDMGState {
    /* each chunk contains a certain number of sectors,
     * offsets[i] is the offset in the .dmg file,
     * lengths[i] is the length of the compressed chunk,
     * sectors[i] is the sector beginning at offsets[i],
     * sectorcounts[i] is the number of sectors in that chunk,
     * the sectors array is ordered
     * 0<=i<n_chunks */

    uint32_t n_chunks;
    uint32_t* types;
    uint64_t* offsets;
    uint64_t* lengths;
    uint64_t* sectors;
    uint64_t* sectorcounts;
    uint32_t current_chunk;
    uint8_t *compressed_chunk;
    uint8_t *uncompressed_chunk;
    z_stream zstream;
} BDRVDMGState;

static int dmg_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    int len=strlen(filename);
    if(len>4 && !strcmp(filename+len-4,".dmg"))
	return 2;
    return 0;
}

static off_t read_off(BlockDriverState *bs, int64_t offset)
{
	uint64_t buffer;
	if (bdrv_pread(bs->file, offset, &buffer, 8) < 8)
		return 0;
	return be64_to_cpu(buffer);
}

static off_t read_uint32(BlockDriverState *bs, int64_t offset)
{
	uint32_t buffer;
	if (bdrv_pread(bs->file, offset, &buffer, 4) < 4)
		return 0;
	return be32_to_cpu(buffer);
}

static int dmg_open(BlockDriverState *bs, int flags)
{
    BDRVDMGState *s = bs->opaque;
    off_t info_begin,info_end,last_in_offset,last_out_offset;
    uint32_t count;
    uint32_t max_compressed_size=1,max_sectors_per_chunk=1,i;
    int64_t offset;

    bs->read_only = 1;
    s->n_chunks = 0;
    s->offsets = s->lengths = s->sectors = s->sectorcounts = NULL;

    /* read offset of info blocks */
    offset = bdrv_getlength(bs->file);
    if (offset < 0) {
        goto fail;
    }
    offset -= 0x1d8;

    info_begin = read_off(bs, offset);
    if (info_begin == 0) {
	goto fail;
    }

    if (read_uint32(bs, info_begin) != 0x100) {
        goto fail;
    }

    count = read_uint32(bs, info_begin + 4);
    if (count == 0) {
        goto fail;
    }
    info_end = info_begin + count;

    offset = info_begin + 0x100;

    /* read offsets */
    last_in_offset = last_out_offset = 0;
    while (offset < info_end) {
        uint32_t type;

	count = read_uint32(bs, offset);
	if(count==0)
	    goto fail;
        offset += 4;

	type = read_uint32(bs, offset);
	if (type == 0x6d697368 && count >= 244) {
	    int new_size, chunk_count;

            offset += 4;
            offset += 200;

	    chunk_count = (count-204)/40;
	    new_size = sizeof(uint64_t) * (s->n_chunks + chunk_count);
	    s->types = g_realloc(s->types, new_size/2);
	    s->offsets = g_realloc(s->offsets, new_size);
	    s->lengths = g_realloc(s->lengths, new_size);
	    s->sectors = g_realloc(s->sectors, new_size);
	    s->sectorcounts = g_realloc(s->sectorcounts, new_size);

	    for(i=s->n_chunks;i<s->n_chunks+chunk_count;i++) {
		s->types[i] = read_uint32(bs, offset);
		offset += 4;
		if(s->types[i]!=0x80000005 && s->types[i]!=1 && s->types[i]!=2) {
		    if(s->types[i]==0xffffffff) {
			last_in_offset = s->offsets[i-1]+s->lengths[i-1];
			last_out_offset = s->sectors[i-1]+s->sectorcounts[i-1];
		    }
		    chunk_count--;
		    i--;
		    offset += 36;
		    continue;
		}
		offset += 4;

		s->sectors[i] = last_out_offset+read_off(bs, offset);
		offset += 8;

		s->sectorcounts[i] = read_off(bs, offset);
		offset += 8;

		s->offsets[i] = last_in_offset+read_off(bs, offset);
		offset += 8;

		s->lengths[i] = read_off(bs, offset);
		offset += 8;

		if(s->lengths[i]>max_compressed_size)
		    max_compressed_size = s->lengths[i];
		if(s->sectorcounts[i]>max_sectors_per_chunk)
		    max_sectors_per_chunk = s->sectorcounts[i];
	    }
	    s->n_chunks+=chunk_count;
	}
    }

    /* initialize zlib engine */
    s->compressed_chunk = g_malloc(max_compressed_size+1);
    s->uncompressed_chunk = g_malloc(512*max_sectors_per_chunk);
    if(inflateInit(&s->zstream) != Z_OK)
	goto fail;

    s->current_chunk = s->n_chunks;

    return 0;
fail:
    return -1;
}

static inline int is_sector_in_chunk(BDRVDMGState* s,
		uint32_t chunk_num,int sector_num)
{
    if(chunk_num>=s->n_chunks || s->sectors[chunk_num]>sector_num ||
	    s->sectors[chunk_num]+s->sectorcounts[chunk_num]<=sector_num)
	return 0;
    else
	return -1;
}

static inline uint32_t search_chunk(BDRVDMGState* s,int sector_num)
{
    /* binary search */
    uint32_t chunk1=0,chunk2=s->n_chunks,chunk3;
    while(chunk1!=chunk2) {
	chunk3 = (chunk1+chunk2)/2;
	if(s->sectors[chunk3]>sector_num)
	    chunk2 = chunk3;
	else if(s->sectors[chunk3]+s->sectorcounts[chunk3]>sector_num)
	    return chunk3;
	else
	    chunk1 = chunk3;
    }
    return s->n_chunks; /* error */
}

static inline int dmg_read_chunk(BlockDriverState *bs, int sector_num)
{
    BDRVDMGState *s = bs->opaque;

    if(!is_sector_in_chunk(s,s->current_chunk,sector_num)) {
	int ret;
	uint32_t chunk = search_chunk(s,sector_num);

	if(chunk>=s->n_chunks)
	    return -1;

	s->current_chunk = s->n_chunks;
	switch(s->types[chunk]) {
	case 0x80000005: { /* zlib compressed */
	    int i;

	    /* we need to buffer, because only the chunk as whole can be
	     * inflated. */
	    i=0;
	    do {
                ret = bdrv_pread(bs->file, s->offsets[chunk] + i,
                                 s->compressed_chunk+i, s->lengths[chunk]-i);
		if(ret<0 && errno==EINTR)
		    ret=0;
		i+=ret;
	    } while(ret>=0 && ret+i<s->lengths[chunk]);

	    if (ret != s->lengths[chunk])
		return -1;

	    s->zstream.next_in = s->compressed_chunk;
	    s->zstream.avail_in = s->lengths[chunk];
	    s->zstream.next_out = s->uncompressed_chunk;
	    s->zstream.avail_out = 512*s->sectorcounts[chunk];
	    ret = inflateReset(&s->zstream);
	    if(ret != Z_OK)
		return -1;
	    ret = inflate(&s->zstream, Z_FINISH);
	    if(ret != Z_STREAM_END || s->zstream.total_out != 512*s->sectorcounts[chunk])
		return -1;
	    break; }
	case 1: /* copy */
	    ret = bdrv_pread(bs->file, s->offsets[chunk],
                             s->uncompressed_chunk, s->lengths[chunk]);
	    if (ret != s->lengths[chunk])
		return -1;
	    break;
	case 2: /* zero */
	    memset(s->uncompressed_chunk, 0, 512*s->sectorcounts[chunk]);
	    break;
	}
	s->current_chunk = chunk;
    }
    return 0;
}

static int dmg_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVDMGState *s = bs->opaque;
    int i;

    for(i=0;i<nb_sectors;i++) {
	uint32_t sector_offset_in_chunk;
	if(dmg_read_chunk(bs, sector_num+i) != 0)
	    return -1;
	sector_offset_in_chunk = sector_num+i-s->sectors[s->current_chunk];
	memcpy(buf+i*512,s->uncompressed_chunk+sector_offset_in_chunk*512,512);
    }
    return 0;
}

static void dmg_close(BlockDriverState *bs)
{
    BDRVDMGState *s = bs->opaque;
    if(s->n_chunks>0) {
	free(s->types);
	free(s->offsets);
	free(s->lengths);
	free(s->sectors);
	free(s->sectorcounts);
    }
    free(s->compressed_chunk);
    free(s->uncompressed_chunk);
    inflateEnd(&s->zstream);
}

static BlockDriver bdrv_dmg = {
    .format_name	= "dmg",
    .instance_size	= sizeof(BDRVDMGState),
    .bdrv_probe		= dmg_probe,
    .bdrv_open		= dmg_open,
    .bdrv_read		= dmg_read,
    .bdrv_close		= dmg_close,
};

static void bdrv_dmg_init(void)
{
    bdrv_register(&bdrv_dmg);
}

block_init(bdrv_dmg_init);
