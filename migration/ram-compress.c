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
#include "migration.h"
#include "options.h"
#include "io/channel-null.h"
#include "exec/ram_addr.h"

CompressionStats compression_counters;

static CompressParam *comp_param;
static QemuThread *compress_threads;
/* comp_done_cond is used to wake up the migration thread when
 * one of the compression threads has finished the compression.
 * comp_done_lock is used to co-work with comp_done_cond.
 */
static QemuMutex comp_done_lock;
static QemuCond comp_done_cond;

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
        comp_param[i].originbuf = g_try_malloc(TARGET_PAGE_SIZE);
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
    int ret;

    if (buffer_is_zero(p, TARGET_PAGE_SIZE)) {
        return RES_ZEROPAGE;
    }

    /*
     * copy it to a internal buffer to avoid it being modified by VM
     * so that we can catch up the error during compression and
     * decompression
     */
    memcpy(source_buf, p, TARGET_PAGE_SIZE);
    ret = qemu_put_compression_data(f, stream, source_buf, TARGET_PAGE_SIZE);
    if (ret < 0) {
        qemu_file_set_error(migrate_get_current()->to_dst_file, ret);
        error_report("compressed data failed!");
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

void flush_compressed_data(int (send_queued_data(CompressParam *)))
{
    int idx, thread_count;

    thread_count = migrate_compress_threads();

    qemu_mutex_lock(&comp_done_lock);
    for (idx = 0; idx < thread_count; idx++) {
        while (!comp_param[idx].done) {
            qemu_cond_wait(&comp_done_cond, &comp_done_lock);
        }
    }
    qemu_mutex_unlock(&comp_done_lock);

    for (idx = 0; idx < thread_count; idx++) {
        qemu_mutex_lock(&comp_param[idx].mutex);
        if (!comp_param[idx].quit) {
            CompressParam *param = &comp_param[idx];
            send_queued_data(param);
            compress_reset_result(param);
        }
        qemu_mutex_unlock(&comp_param[idx].mutex);
    }
}

static inline void set_compress_params(CompressParam *param, RAMBlock *block,
                                       ram_addr_t offset)
{
    param->block = block;
    param->offset = offset;
    param->trigger = true;
}

int compress_page_with_multi_thread(RAMBlock *block, ram_addr_t offset,
                                int (send_queued_data(CompressParam *)))
{
    int idx, thread_count, pages = -1;
    bool wait = migrate_compress_wait_thread();

    thread_count = migrate_compress_threads();
    qemu_mutex_lock(&comp_done_lock);
retry:
    for (idx = 0; idx < thread_count; idx++) {
        if (comp_param[idx].done) {
            CompressParam *param = &comp_param[idx];
            qemu_mutex_lock(&param->mutex);
            param->done = false;
            send_queued_data(param);
            compress_reset_result(param);
            set_compress_params(param, block, offset);

            qemu_cond_signal(&param->cond);
            qemu_mutex_unlock(&param->mutex);
            pages = 1;
            break;
        }
    }

    /*
     * wait for the free thread if the user specifies 'compress-wait-thread',
     * otherwise we will post the page out in the main thread as normal page.
     */
    if (pages < 0 && wait) {
        qemu_cond_wait(&comp_done_cond, &comp_done_lock);
        goto retry;
    }
    qemu_mutex_unlock(&comp_done_lock);

    return pages;
}
