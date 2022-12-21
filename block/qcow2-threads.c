/*
 * Threaded data processing for Qcow2: compression, encryption
 *
 * Copyright (c) 2004-2006 Fabrice Bellard
 * Copyright (c) 2018 Virtuozzo International GmbH. All rights reserved.
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

#include "qemu/osdep.h"

#define ZLIB_CONST
#include <zlib.h>

#ifdef CONFIG_ZSTD
#include <zstd.h>
#include <zstd_errors.h>
#endif

#include "qcow2.h"
#include "block/block-io.h"
#include "block/thread-pool.h"
#include "crypto.h"

static int coroutine_fn
qcow2_co_process(BlockDriverState *bs, ThreadPoolFunc *func, void *arg)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;
    ThreadPool *pool = aio_get_thread_pool(bdrv_get_aio_context(bs));

    qemu_co_mutex_lock(&s->lock);
    while (s->nb_threads >= QCOW2_MAX_THREADS) {
        qemu_co_queue_wait(&s->thread_task_queue, &s->lock);
    }
    s->nb_threads++;
    qemu_co_mutex_unlock(&s->lock);

    ret = thread_pool_submit_co(pool, func, arg);

    qemu_co_mutex_lock(&s->lock);
    s->nb_threads--;
    qemu_co_queue_next(&s->thread_task_queue);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}


/*
 * Compression
 */

typedef ssize_t (*Qcow2CompressFunc)(void *dest, size_t dest_size,
                                     const void *src, size_t src_size);
typedef struct Qcow2CompressData {
    void *dest;
    size_t dest_size;
    const void *src;
    size_t src_size;
    ssize_t ret;

    Qcow2CompressFunc func;
} Qcow2CompressData;

/*
 * qcow2_zlib_compress()
 *
 * Compress @src_size bytes of data using zlib compression method
 *
 * @dest - destination buffer, @dest_size bytes
 * @src - source buffer, @src_size bytes
 *
 * Returns: compressed size on success
 *          -ENOMEM destination buffer is not enough to store compressed data
 *          -EIO    on any other error
 */
static ssize_t qcow2_zlib_compress(void *dest, size_t dest_size,
                                   const void *src, size_t src_size)
{
    ssize_t ret;
    z_stream strm;

    /* best compression, small window, no zlib header */
    memset(&strm, 0, sizeof(strm));
    ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                       -12, 9, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        return -EIO;
    }

    /*
     * strm.next_in is not const in old zlib versions, such as those used on
     * OpenBSD/NetBSD, so cast the const away
     */
    strm.avail_in = src_size;
    strm.next_in = (void *) src;
    strm.avail_out = dest_size;
    strm.next_out = dest;

    ret = deflate(&strm, Z_FINISH);
    if (ret == Z_STREAM_END) {
        ret = dest_size - strm.avail_out;
    } else {
        ret = (ret == Z_OK ? -ENOMEM : -EIO);
    }

    deflateEnd(&strm);

    return ret;
}

/*
 * qcow2_zlib_decompress()
 *
 * Decompress some data (not more than @src_size bytes) to produce exactly
 * @dest_size bytes using zlib compression method
 *
 * @dest - destination buffer, @dest_size bytes
 * @src - source buffer, @src_size bytes
 *
 * Returns: 0 on success
 *          -EIO on fail
 */
static ssize_t qcow2_zlib_decompress(void *dest, size_t dest_size,
                                     const void *src, size_t src_size)
{
    int ret;
    z_stream strm;

    memset(&strm, 0, sizeof(strm));
    strm.avail_in = src_size;
    strm.next_in = (void *) src;
    strm.avail_out = dest_size;
    strm.next_out = dest;

    ret = inflateInit2(&strm, -12);
    if (ret != Z_OK) {
        return -EIO;
    }

    ret = inflate(&strm, Z_FINISH);
    if ((ret == Z_STREAM_END || ret == Z_BUF_ERROR) && strm.avail_out == 0) {
        /*
         * We approve Z_BUF_ERROR because we need @dest buffer to be filled, but
         * @src buffer may be processed partly (because in qcow2 we know size of
         * compressed data with precision of one sector)
         */
        ret = 0;
    } else {
        ret = -EIO;
    }

    inflateEnd(&strm);

    return ret;
}

#ifdef CONFIG_ZSTD

/*
 * qcow2_zstd_compress()
 *
 * Compress @src_size bytes of data using zstd compression method
 *
 * @dest - destination buffer, @dest_size bytes
 * @src - source buffer, @src_size bytes
 *
 * Returns: compressed size on success
 *          -ENOMEM destination buffer is not enough to store compressed data
 *          -EIO    on any other error
 */
static ssize_t qcow2_zstd_compress(void *dest, size_t dest_size,
                                   const void *src, size_t src_size)
{
    ssize_t ret;
    size_t zstd_ret;
    ZSTD_outBuffer output = {
        .dst = dest,
        .size = dest_size,
        .pos = 0
    };
    ZSTD_inBuffer input = {
        .src = src,
        .size = src_size,
        .pos = 0
    };
    ZSTD_CCtx *cctx = ZSTD_createCCtx();

    if (!cctx) {
        return -EIO;
    }
    /*
     * Use the zstd streamed interface for symmetry with decompression,
     * where streaming is essential since we don't record the exact
     * compressed size.
     *
     * ZSTD_compressStream2() tries to compress everything it could
     * with a single call. Although, ZSTD docs says that:
     * "You must continue calling ZSTD_compressStream2() with ZSTD_e_end
     * until it returns 0, at which point you are free to start a new frame",
     * in out tests we saw the only case when it returned with >0 -
     * when the output buffer was too small. In that case,
     * ZSTD_compressStream2() expects a bigger buffer on the next call.
     * We can't provide a bigger buffer because we are limited with dest_size
     * which we pass to the ZSTD_compressStream2() at once.
     * So, we don't need any loops and just abort the compression when we
     * don't get 0 result on the first call.
     */
    zstd_ret = ZSTD_compressStream2(cctx, &output, &input, ZSTD_e_end);

    if (zstd_ret) {
        if (zstd_ret > output.size - output.pos) {
            ret = -ENOMEM;
        } else {
            ret = -EIO;
        }
        goto out;
    }

    /* make sure that zstd didn't overflow the dest buffer */
    assert(output.pos <= dest_size);
    ret = output.pos;
out:
    ZSTD_freeCCtx(cctx);
    return ret;
}

/*
 * qcow2_zstd_decompress()
 *
 * Decompress some data (not more than @src_size bytes) to produce exactly
 * @dest_size bytes using zstd compression method
 *
 * @dest - destination buffer, @dest_size bytes
 * @src - source buffer, @src_size bytes
 *
 * Returns: 0 on success
 *          -EIO on any error
 */
static ssize_t qcow2_zstd_decompress(void *dest, size_t dest_size,
                                     const void *src, size_t src_size)
{
    size_t zstd_ret = 0;
    ssize_t ret = 0;
    ZSTD_outBuffer output = {
        .dst = dest,
        .size = dest_size,
        .pos = 0
    };
    ZSTD_inBuffer input = {
        .src = src,
        .size = src_size,
        .pos = 0
    };
    ZSTD_DCtx *dctx = ZSTD_createDCtx();

    if (!dctx) {
        return -EIO;
    }

    /*
     * The compressed stream from the input buffer may consist of more
     * than one zstd frame. So we iterate until we get a fully
     * uncompressed cluster.
     * From zstd docs related to ZSTD_decompressStream:
     * "return : 0 when a frame is completely decoded and fully flushed"
     * We suppose that this means: each time ZSTD_decompressStream reads
     * only ONE full frame and returns 0 if and only if that frame
     * is completely decoded and flushed. Only after returning 0,
     * ZSTD_decompressStream reads another ONE full frame.
     */
    while (output.pos < output.size) {
        size_t last_in_pos = input.pos;
        size_t last_out_pos = output.pos;
        zstd_ret = ZSTD_decompressStream(dctx, &output, &input);

        if (ZSTD_isError(zstd_ret)) {
            ret = -EIO;
            break;
        }

        /*
         * The ZSTD manual is vague about what to do if it reads
         * the buffer partially, and we don't want to get stuck
         * in an infinite loop where ZSTD_decompressStream
         * returns > 0 waiting for another input chunk. So, we add
         * a check which ensures that the loop makes some progress
         * on each step.
         */
        if (last_in_pos >= input.pos &&
            last_out_pos >= output.pos) {
            ret = -EIO;
            break;
        }
    }
    /*
     * Make sure that we have the frame fully flushed here
     * if not, we somehow managed to get uncompressed cluster
     * greater then the cluster size, possibly because of its
     * damage.
     */
    if (zstd_ret > 0) {
        ret = -EIO;
    }

    ZSTD_freeDCtx(dctx);
    assert(ret == 0 || ret == -EIO);
    return ret;
}
#endif

static int qcow2_compress_pool_func(void *opaque)
{
    Qcow2CompressData *data = opaque;

    data->ret = data->func(data->dest, data->dest_size,
                           data->src, data->src_size);

    return 0;
}

static ssize_t coroutine_fn
qcow2_co_do_compress(BlockDriverState *bs, void *dest, size_t dest_size,
                     const void *src, size_t src_size, Qcow2CompressFunc func)
{
    Qcow2CompressData arg = {
        .dest = dest,
        .dest_size = dest_size,
        .src = src,
        .src_size = src_size,
        .func = func,
    };

    qcow2_co_process(bs, qcow2_compress_pool_func, &arg);

    return arg.ret;
}

/*
 * qcow2_co_compress()
 *
 * Compress @src_size bytes of data using the compression
 * method defined by the image compression type
 *
 * @dest - destination buffer, @dest_size bytes
 * @src - source buffer, @src_size bytes
 *
 * Returns: compressed size on success
 *          a negative error code on failure
 */
ssize_t coroutine_fn
qcow2_co_compress(BlockDriverState *bs, void *dest, size_t dest_size,
                  const void *src, size_t src_size)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2CompressFunc fn;

    switch (s->compression_type) {
    case QCOW2_COMPRESSION_TYPE_ZLIB:
        fn = qcow2_zlib_compress;
        break;

#ifdef CONFIG_ZSTD
    case QCOW2_COMPRESSION_TYPE_ZSTD:
        fn = qcow2_zstd_compress;
        break;
#endif
    default:
        abort();
    }

    return qcow2_co_do_compress(bs, dest, dest_size, src, src_size, fn);
}

/*
 * qcow2_co_decompress()
 *
 * Decompress some data (not more than @src_size bytes) to produce exactly
 * @dest_size bytes using the compression method defined by the image
 * compression type
 *
 * @dest - destination buffer, @dest_size bytes
 * @src - source buffer, @src_size bytes
 *
 * Returns: 0 on success
 *          a negative error code on failure
 */
ssize_t coroutine_fn
qcow2_co_decompress(BlockDriverState *bs, void *dest, size_t dest_size,
                    const void *src, size_t src_size)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2CompressFunc fn;

    switch (s->compression_type) {
    case QCOW2_COMPRESSION_TYPE_ZLIB:
        fn = qcow2_zlib_decompress;
        break;

#ifdef CONFIG_ZSTD
    case QCOW2_COMPRESSION_TYPE_ZSTD:
        fn = qcow2_zstd_decompress;
        break;
#endif
    default:
        abort();
    }

    return qcow2_co_do_compress(bs, dest, dest_size, src, src_size, fn);
}


/*
 * Cryptography
 */

/*
 * Qcow2EncDecFunc: common prototype of qcrypto_block_encrypt() and
 * qcrypto_block_decrypt() functions.
 */
typedef int (*Qcow2EncDecFunc)(QCryptoBlock *block, uint64_t offset,
                               uint8_t *buf, size_t len, Error **errp);

typedef struct Qcow2EncDecData {
    QCryptoBlock *block;
    uint64_t offset;
    uint8_t *buf;
    size_t len;

    Qcow2EncDecFunc func;
} Qcow2EncDecData;

static int qcow2_encdec_pool_func(void *opaque)
{
    Qcow2EncDecData *data = opaque;

    return data->func(data->block, data->offset, data->buf, data->len, NULL);
}

static int coroutine_fn
qcow2_co_encdec(BlockDriverState *bs, uint64_t host_offset,
                uint64_t guest_offset, void *buf, size_t len,
                Qcow2EncDecFunc func)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2EncDecData arg = {
        .block = s->crypto,
        .offset = s->crypt_physical_offset ? host_offset : guest_offset,
        .buf = buf,
        .len = len,
        .func = func,
    };
    uint64_t sector_size;

    assert(s->crypto);

    sector_size = qcrypto_block_get_sector_size(s->crypto);
    assert(QEMU_IS_ALIGNED(guest_offset, sector_size));
    assert(QEMU_IS_ALIGNED(host_offset, sector_size));
    assert(QEMU_IS_ALIGNED(len, sector_size));

    return len == 0 ? 0 : qcow2_co_process(bs, qcow2_encdec_pool_func, &arg);
}

/*
 * qcow2_co_encrypt()
 *
 * Encrypts one or more contiguous aligned sectors
 *
 * @host_offset - underlying storage offset of the first sector of the
 * data to be encrypted
 *
 * @guest_offset - guest (virtual) offset of the first sector of the
 * data to be encrypted
 *
 * @buf - buffer with the data to encrypt, that after encryption
 *        will be written to the underlying storage device at
 *        @host_offset
 *
 * @len - length of the buffer (must be a multiple of the encryption
 *        sector size)
 *
 * Depending on the encryption method, @host_offset and/or @guest_offset
 * may be used for generating the initialization vector for
 * encryption.
 *
 * Note that while the whole range must be aligned on sectors, it
 * does not have to be aligned on clusters and can also cross cluster
 * boundaries
 */
int coroutine_fn
qcow2_co_encrypt(BlockDriverState *bs, uint64_t host_offset,
                 uint64_t guest_offset, void *buf, size_t len)
{
    return qcow2_co_encdec(bs, host_offset, guest_offset, buf, len,
                           qcrypto_block_encrypt);
}

/*
 * qcow2_co_decrypt()
 *
 * Decrypts one or more contiguous aligned sectors
 * Similar to qcow2_co_encrypt
 */
int coroutine_fn
qcow2_co_decrypt(BlockDriverState *bs, uint64_t host_offset,
                 uint64_t guest_offset, void *buf, size_t len)
{
    return qcow2_co_encdec(bs, host_offset, guest_offset, buf, len,
                           qcrypto_block_decrypt);
}
