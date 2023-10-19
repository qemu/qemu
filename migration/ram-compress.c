/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2011-2015 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
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
#include "qemu/cutils.h"

#include "ram-compress.h"

#include "qemu/error-report.h"
#include "qemu/stats64.h"
#include "migration.h"
#include "options.h"
#include "io/channel-null.h"
#include "exec/target_page.h"
#include "exec/ramblock.h"
#include "ram.h"
#include "migration-stats.h"

static struct {
    int64_t pages;
    int64_t busy;
    double busy_rate;
    int64_t compressed_size;
    double compression_rate;
    /* compression statistics since the beginning of the period */
    /* amount of count that no free thread to compress data */
    uint64_t compress_thread_busy_prev;
    /* amount bytes after compression */
    uint64_t compressed_size_prev;
    /* amount of compressed pages */
    uint64_t compress_pages_prev;
} compression_counters;

static CompressParam *comp_param;
static QemuThread *compress_threads;
/* comp_done_cond is used to wake up the migration thread when
 * one of the compression threads has finished the compression.
 * comp_done_lock is used to co-work with comp_done_cond.
 */
static QemuMutex comp_done_lock;
static QemuCond comp_done_cond;

struct DecompressParam {
    bool done;
    bool quit;
    QemuMutex mutex;
    QemuCond cond;
    void *des;
    uint8_t *compbuf;
    int len;
    z_stream stream;
};
typedef struct DecompressParam DecompressParam;

static QEMUFile *decomp_file;
static DecompressParam *decomp_param;
static QemuThread *decompress_threads;
static QemuMutex decomp_done_lock;
static QemuCond decomp_done_cond;

static CompressResult do_compress_ram_page(QEMUFile *f, z_stream *stream,
                                           RAMBlock *block, ram_addr_t offset,
                                           uint8_t *source_buf);

static void *do_data_compress(void *opaque)
{
    CompressParam *param = opaque;
    RAMBlock *block;
    ram_addr_t offset;
    CompressResult result;

    qemu_mutex_lock(&param->mutex);
    while (!param->quit) {
        if (param->trigger) {
            block = param->block;
            offset = param->offset;
            param->trigger = false;
            qemu_mutex_unlock(&param->mutex);

            result = do_compress_ram_page(param->file, &param->stream,
                                          block, offset, param->originbuf);

            qemu_mutex_lock(&comp_done_lock);
            param->done = true;
            param->result = result;
            qemu_cond_signal(&comp_done_cond);
            qemu_mutex_unlock(&comp_done_lock);

            qemu_mutex_lock(&param->mutex);
        } else {
            qemu_cond_wait(&param->cond, &param->mutex);
        }
    }
    qemu_mutex_unlock(&param->mutex);

    return NULL;
}

void compress_threads_save_cleanup(void)
{
    int i, thread_count;

    if (!migrate_compress() || !comp_param) {
        return;
    }

    thread_count = migrate_compress_threads();
    for (i = 0; i < thread_count; i++) {
        /*
         * we use it as a indicator which shows if the thread is
         * properly init'd or not
         */
        if (!comp_param[i].file) {
            break;
        }

        qemu_mutex_lock(&comp_param[i].mutex);
        comp_param[i].quit = true;
        qemu_cond_signal(&comp_param[i].cond);
        qemu_mutex_unlock(&comp_param[i].mutex);

        qemu_thread_join(compress_threads + i);
        qemu_mutex_destroy(&comp_param[i].mutex);
        qemu_cond_destroy(&comp_param[i].cond);
        deflateEnd(&comp_param[i].stream);
        g_free(comp_param[i].originbuf);
        qemu_fclose(comp_param[i].file);
        comp_param[i].file = NULL;
    }
    qemu_mutex_destroy(&comp_done_lock);
    qemu_cond_destroy(&comp_done_cond);
    g_free(compress_threads);
    g_free(comp_param);
    compress_threads = NULL;
    comp_param = NULL;
}

int compress_threads_save_setup(void)
{
    int i, thread_count;

    if (!migrate_compress()) {
        return 0;
    }
    thread_count = migrate_compress_threads();
    compress_threads = g_new0(QemuThread, thread_count);
    comp_param = g_new0(CompressParam, thread_count);
    qemu_cond_init(&comp_done_cond);
    qemu_mutex_init(&comp_done_lock);
    for (i = 0; i < thread_count; i++) {
        comp_param[i].originbuf = g_try_malloc(qemu_target_page_size());
        if (!comp_param[i].originbuf) {
            goto exit;
        }

        if (deflateInit(&comp_param[i].stream,
                        migrate_compress_level()) != Z_OK) {
            g_free(comp_param[i].originbuf);
            goto exit;
        }

        /* comp_param[i].file is just used as a dummy buffer to save data,
         * set its ops to empty.
         */
        comp_param[i].file = qemu_file_new_output(
            QIO_CHANNEL(qio_channel_null_new()));
        comp_param[i].done = true;
        comp_param[i].quit = false;
        qemu_mutex_init(&comp_param[i].mutex);
        qemu_cond_init(&comp_param[i].cond);
        qemu_thread_create(compress_threads + i, "compress",
                           do_data_compress, comp_param + i,
                           QEMU_THREAD_JOINABLE);
    }
    return 0;

exit:
    compress_threads_save_cleanup();
    return -1;
}

static CompressResult do_compress_ram_page(QEMUFile *f, z_stream *stream,
                                           RAMBlock *block, ram_addr_t offset,
                                           uint8_t *source_buf)
{
    uint8_t *p = block->host + offset;
    size_t page_size = qemu_target_page_size();
    int ret;

    assert(qemu_file_buffer_empty(f));

    if (buffer_is_zero(p, page_size)) {
        return RES_ZEROPAGE;
    }

    /*
     * copy it to a internal buffer to avoid it being modified by VM
     * so that we can catch up the error during compression and
     * decompression
     */
    memcpy(source_buf, p, page_size);
    ret = qemu_put_compression_data(f, stream, source_buf, page_size);
    if (ret < 0) {
        qemu_file_set_error(migrate_get_current()->to_dst_file, ret);
        error_report("compressed data failed!");
        qemu_fflush(f);
        return RES_NONE;
    }
    return RES_COMPRESS;
}

static inline void compress_reset_result(CompressParam *param)
{
    param->result = RES_NONE;
    param->block = NULL;
    param->offset = 0;
}

void compress_flush_data(void)
{
    int thread_count = migrate_compress_threads();

    if (!migrate_compress()) {
        return;
    }

    qemu_mutex_lock(&comp_done_lock);
    for (int i = 0; i < thread_count; i++) {
        while (!comp_param[i].done) {
            qemu_cond_wait(&comp_done_cond, &comp_done_lock);
        }
    }
    qemu_mutex_unlock(&comp_done_lock);

    for (int i = 0; i < thread_count; i++) {
        qemu_mutex_lock(&comp_param[i].mutex);
        if (!comp_param[i].quit) {
            CompressParam *param = &comp_param[i];
            compress_send_queued_data(param);
            assert(qemu_file_buffer_empty(param->file));
            compress_reset_result(param);
        }
        qemu_mutex_unlock(&comp_param[i].mutex);
    }
}

static inline void set_compress_params(CompressParam *param, RAMBlock *block,
                                       ram_addr_t offset)
{
    param->block = block;
    param->offset = offset;
    param->trigger = true;
}

/*
 * Return true when it compress a page
 */
bool compress_page_with_multi_thread(RAMBlock *block, ram_addr_t offset,
                                     int (send_queued_data(CompressParam *)))
{
    int thread_count;
    bool wait = migrate_compress_wait_thread();

    thread_count = migrate_compress_threads();
    qemu_mutex_lock(&comp_done_lock);

    while (true) {
        for (int i = 0; i < thread_count; i++) {
            if (comp_param[i].done) {
                CompressParam *param = &comp_param[i];
                qemu_mutex_lock(&param->mutex);
                param->done = false;
                send_queued_data(param);
                assert(qemu_file_buffer_empty(param->file));
                compress_reset_result(param);
                set_compress_params(param, block, offset);

                qemu_cond_signal(&param->cond);
                qemu_mutex_unlock(&param->mutex);
                qemu_mutex_unlock(&comp_done_lock);
                return true;
            }
        }
        if (!wait) {
            qemu_mutex_unlock(&comp_done_lock);
            compression_counters.busy++;
            return false;
        }
        /*
         * wait for a free thread if the user specifies
         * 'compress-wait-thread', otherwise we will post the page out
         * in the main thread as normal page.
         */
        qemu_cond_wait(&comp_done_cond, &comp_done_lock);
    }
}

/* return the size after decompression, or negative value on error */
static int
qemu_uncompress_data(z_stream *stream, uint8_t *dest, size_t dest_len,
                     const uint8_t *source, size_t source_len)
{
    int err;

    err = inflateReset(stream);
    if (err != Z_OK) {
        return -1;
    }

    stream->avail_in = source_len;
    stream->next_in = (uint8_t *)source;
    stream->avail_out = dest_len;
    stream->next_out = dest;

    err = inflate(stream, Z_NO_FLUSH);
    if (err != Z_STREAM_END) {
        return -1;
    }

    return stream->total_out;
}

static void *do_data_decompress(void *opaque)
{
    DecompressParam *param = opaque;
    unsigned long pagesize;
    uint8_t *des;
    int len, ret;

    qemu_mutex_lock(&param->mutex);
    while (!param->quit) {
        if (param->des) {
            des = param->des;
            len = param->len;
            param->des = 0;
            qemu_mutex_unlock(&param->mutex);

            pagesize = qemu_target_page_size();

            ret = qemu_uncompress_data(&param->stream, des, pagesize,
                                       param->compbuf, len);
            if (ret < 0 && migrate_get_current()->decompress_error_check) {
                error_report("decompress data failed");
                qemu_file_set_error(decomp_file, ret);
            }

            qemu_mutex_lock(&decomp_done_lock);
            param->done = true;
            qemu_cond_signal(&decomp_done_cond);
            qemu_mutex_unlock(&decomp_done_lock);

            qemu_mutex_lock(&param->mutex);
        } else {
            qemu_cond_wait(&param->cond, &param->mutex);
        }
    }
    qemu_mutex_unlock(&param->mutex);

    return NULL;
}

int wait_for_decompress_done(void)
{
    if (!migrate_compress()) {
        return 0;
    }

    int thread_count = migrate_decompress_threads();
    qemu_mutex_lock(&decomp_done_lock);
    for (int i = 0; i < thread_count; i++) {
        while (!decomp_param[i].done) {
            qemu_cond_wait(&decomp_done_cond, &decomp_done_lock);
        }
    }
    qemu_mutex_unlock(&decomp_done_lock);
    return qemu_file_get_error(decomp_file);
}

void compress_threads_load_cleanup(void)
{
    int i, thread_count;

    if (!migrate_compress()) {
        return;
    }
    thread_count = migrate_decompress_threads();
    for (i = 0; i < thread_count; i++) {
        /*
         * we use it as a indicator which shows if the thread is
         * properly init'd or not
         */
        if (!decomp_param[i].compbuf) {
            break;
        }

        qemu_mutex_lock(&decomp_param[i].mutex);
        decomp_param[i].quit = true;
        qemu_cond_signal(&decomp_param[i].cond);
        qemu_mutex_unlock(&decomp_param[i].mutex);
    }
    for (i = 0; i < thread_count; i++) {
        if (!decomp_param[i].compbuf) {
            break;
        }

        qemu_thread_join(decompress_threads + i);
        qemu_mutex_destroy(&decomp_param[i].mutex);
        qemu_cond_destroy(&decomp_param[i].cond);
        inflateEnd(&decomp_param[i].stream);
        g_free(decomp_param[i].compbuf);
        decomp_param[i].compbuf = NULL;
    }
    g_free(decompress_threads);
    g_free(decomp_param);
    decompress_threads = NULL;
    decomp_param = NULL;
    decomp_file = NULL;
}

int compress_threads_load_setup(QEMUFile *f)
{
    int i, thread_count;

    if (!migrate_compress()) {
        return 0;
    }

    /*
     * set compression_counters memory to zero for a new migration
     */
    memset(&compression_counters, 0, sizeof(compression_counters));

    thread_count = migrate_decompress_threads();
    decompress_threads = g_new0(QemuThread, thread_count);
    decomp_param = g_new0(DecompressParam, thread_count);
    qemu_mutex_init(&decomp_done_lock);
    qemu_cond_init(&decomp_done_cond);
    decomp_file = f;
    for (i = 0; i < thread_count; i++) {
        if (inflateInit(&decomp_param[i].stream) != Z_OK) {
            goto exit;
        }

        size_t compbuf_size = compressBound(qemu_target_page_size());
        decomp_param[i].compbuf = g_malloc0(compbuf_size);
        qemu_mutex_init(&decomp_param[i].mutex);
        qemu_cond_init(&decomp_param[i].cond);
        decomp_param[i].done = true;
        decomp_param[i].quit = false;
        qemu_thread_create(decompress_threads + i, "decompress",
                           do_data_decompress, decomp_param + i,
                           QEMU_THREAD_JOINABLE);
    }
    return 0;
exit:
    compress_threads_load_cleanup();
    return -1;
}

void decompress_data_with_multi_threads(QEMUFile *f, void *host, int len)
{
    int thread_count = migrate_decompress_threads();
    QEMU_LOCK_GUARD(&decomp_done_lock);
    while (true) {
        for (int i = 0; i < thread_count; i++) {
            if (decomp_param[i].done) {
                decomp_param[i].done = false;
                qemu_mutex_lock(&decomp_param[i].mutex);
                qemu_get_buffer(f, decomp_param[i].compbuf, len);
                decomp_param[i].des = host;
                decomp_param[i].len = len;
                qemu_cond_signal(&decomp_param[i].cond);
                qemu_mutex_unlock(&decomp_param[i].mutex);
                return;
            }
        }
        qemu_cond_wait(&decomp_done_cond, &decomp_done_lock);
    }
}

void populate_compress(MigrationInfo *info)
{
    if (!migrate_compress()) {
        return;
    }
    info->compression = g_malloc0(sizeof(*info->compression));
    info->compression->pages = compression_counters.pages;
    info->compression->busy = compression_counters.busy;
    info->compression->busy_rate = compression_counters.busy_rate;
    info->compression->compressed_size = compression_counters.compressed_size;
    info->compression->compression_rate = compression_counters.compression_rate;
}

uint64_t compress_ram_pages(void)
{
    return compression_counters.pages;
}

void update_compress_thread_counts(const CompressParam *param, int bytes_xmit)
{
    ram_transferred_add(bytes_xmit);

    if (param->result == RES_ZEROPAGE) {
        stat64_add(&mig_stats.zero_pages, 1);
        return;
    }

    /* 8 means a header with RAM_SAVE_FLAG_CONTINUE. */
    compression_counters.compressed_size += bytes_xmit - 8;
    compression_counters.pages++;
}

void compress_update_rates(uint64_t page_count)
{
    if (!migrate_compress()) {
        return;
    }
    compression_counters.busy_rate = (double)(compression_counters.busy -
            compression_counters.compress_thread_busy_prev) / page_count;
    compression_counters.compress_thread_busy_prev =
            compression_counters.busy;

    double compressed_size = compression_counters.compressed_size -
        compression_counters.compressed_size_prev;
    if (compressed_size) {
        double uncompressed_size = (compression_counters.pages -
                                    compression_counters.compress_pages_prev) *
            qemu_target_page_size();

        /* Compression-Ratio = Uncompressed-size / Compressed-size */
        compression_counters.compression_rate =
            uncompressed_size / compressed_size;

        compression_counters.compress_pages_prev =
            compression_counters.pages;
        compression_counters.compressed_size_prev =
            compression_counters.compressed_size;
    }
}
