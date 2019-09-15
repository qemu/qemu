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

#include "qcow2.h"
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
 * qcow2_compress()
 *
 * @dest - destination buffer, @dest_size bytes
 * @src - source buffer, @src_size bytes
 *
 * Returns: compressed size on success
 *          -ENOMEM destination buffer is not enough to store compressed data
 *          -EIO    on any other error
 */
static ssize_t qcow2_compress(void *dest, size_t dest_size,
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
 * qcow2_decompress()
 *
 * Decompress some data (not more than @src_size bytes) to produce exactly
 * @dest_size bytes.
 *
 * @dest - destination buffer, @dest_size bytes
 * @src - source buffer, @src_size bytes
 *
 * Returns: 0 on success
 *          -1 on fail
 */
static ssize_t qcow2_decompress(void *dest, size_t dest_size,
                                const void *src, size_t src_size)
{
    int ret = 0;
    z_stream strm;

    memset(&strm, 0, sizeof(strm));
    strm.avail_in = src_size;
    strm.next_in = (void *) src;
    strm.avail_out = dest_size;
    strm.next_out = dest;

    ret = inflateInit2(&strm, -12);
    if (ret != Z_OK) {
        return -1;
    }

    ret = inflate(&strm, Z_FINISH);
    if ((ret != Z_STREAM_END && ret != Z_BUF_ERROR) || strm.avail_out != 0) {
        /*
         * We approve Z_BUF_ERROR because we need @dest buffer to be filled, but
         * @src buffer may be processed partly (because in qcow2 we know size of
         * compressed data with precision of one sector)
         */
        ret = -1;
    }

    inflateEnd(&strm);

    return ret;
}

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

ssize_t coroutine_fn
qcow2_co_compress(BlockDriverState *bs, void *dest, size_t dest_size,
                  const void *src, size_t src_size)
{
    return qcow2_co_do_compress(bs, dest, dest_size, src, src_size,
                                qcow2_compress);
}

ssize_t coroutine_fn
qcow2_co_decompress(BlockDriverState *bs, void *dest, size_t dest_size,
                    const void *src, size_t src_size)
{
    return qcow2_co_do_compress(bs, dest, dest_size, src, src_size,
                                qcow2_decompress);
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

    assert(QEMU_IS_ALIGNED(guest_offset, BDRV_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(host_offset, BDRV_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(len, BDRV_SECTOR_SIZE));
    assert(s->crypto);

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
 * @len - length of the buffer (must be a BDRV_SECTOR_SIZE multiple)
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
