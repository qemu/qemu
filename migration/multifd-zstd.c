/*
 * Multifd zlib compression implementation
 *
 * Copyright (c) 2020 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <zstd.h>
#include "qemu/rcu.h"
#include "exec/ramblock.h"
#include "exec/target_page.h"
#include "qapi/error.h"
#include "migration.h"
#include "trace.h"
#include "options.h"
#include "multifd.h"

struct zstd_data {
    /* stream for compression */
    ZSTD_CStream *zcs;
    /* stream for decompression */
    ZSTD_DStream *zds;
    /* buffers */
    ZSTD_inBuffer in;
    ZSTD_outBuffer out;
    /* compressed buffer */
    uint8_t *zbuff;
    /* size of compressed buffer */
    uint32_t zbuff_len;
};

/* Multifd zstd compression */

/**
 * zstd_send_setup: setup send side
 *
 * Setup each channel with zstd compression.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int zstd_send_setup(MultiFDSendParams *p, Error **errp)
{
    struct zstd_data *z = g_new0(struct zstd_data, 1);
    int res;

    p->data = z;
    z->zcs = ZSTD_createCStream();
    if (!z->zcs) {
        g_free(z);
        error_setg(errp, "multifd %u: zstd createCStream failed", p->id);
        return -1;
    }

    res = ZSTD_initCStream(z->zcs, migrate_multifd_zstd_level());
    if (ZSTD_isError(res)) {
        ZSTD_freeCStream(z->zcs);
        g_free(z);
        error_setg(errp, "multifd %u: initCStream failed with error %s",
                   p->id, ZSTD_getErrorName(res));
        return -1;
    }
    /* This is the maximum size of the compressed buffer */
    z->zbuff_len = ZSTD_compressBound(MULTIFD_PACKET_SIZE);
    z->zbuff = g_try_malloc(z->zbuff_len);
    if (!z->zbuff) {
        ZSTD_freeCStream(z->zcs);
        g_free(z);
        error_setg(errp, "multifd %u: out of memory for zbuff", p->id);
        return -1;
    }
    return 0;
}

/**
 * zstd_send_cleanup: cleanup send side
 *
 * Close the channel and return memory.
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static void zstd_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    struct zstd_data *z = p->data;

    ZSTD_freeCStream(z->zcs);
    z->zcs = NULL;
    g_free(z->zbuff);
    z->zbuff = NULL;
    g_free(p->data);
    p->data = NULL;
}

/**
 * zstd_send_prepare: prepare date to be able to send
 *
 * Create a compressed buffer with all the pages that we are going to
 * send.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int zstd_send_prepare(MultiFDSendParams *p, Error **errp)
{
    struct zstd_data *z = p->data;
    int ret;
    uint32_t i;

    z->out.dst = z->zbuff;
    z->out.size = z->zbuff_len;
    z->out.pos = 0;

    for (i = 0; i < p->normal_num; i++) {
        ZSTD_EndDirective flush = ZSTD_e_continue;

        if (i == p->normal_num - 1) {
            flush = ZSTD_e_flush;
        }
        z->in.src = p->pages->block->host + p->normal[i];
        z->in.size = p->page_size;
        z->in.pos = 0;

        /*
         * Welcome to compressStream2 semantics
         *
         * We need to loop while:
         * - return is > 0
         * - there is input available
         * - there is output space free
         */
        do {
            ret = ZSTD_compressStream2(z->zcs, &z->out, &z->in, flush);
        } while (ret > 0 && (z->in.size - z->in.pos > 0)
                         && (z->out.size - z->out.pos > 0));
        if (ret > 0 && (z->in.size - z->in.pos > 0)) {
            error_setg(errp, "multifd %u: compressStream buffer too small",
                       p->id);
            return -1;
        }
        if (ZSTD_isError(ret)) {
            error_setg(errp, "multifd %u: compressStream error %s",
                       p->id, ZSTD_getErrorName(ret));
            return -1;
        }
    }
    p->iov[p->iovs_num].iov_base = z->zbuff;
    p->iov[p->iovs_num].iov_len = z->out.pos;
    p->iovs_num++;
    p->next_packet_size = z->out.pos;
    p->flags |= MULTIFD_FLAG_ZSTD;

    return 0;
}

/**
 * zstd_recv_setup: setup receive side
 *
 * Create the compressed channel and buffer.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int zstd_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    struct zstd_data *z = g_new0(struct zstd_data, 1);
    int ret;

    p->data = z;
    z->zds = ZSTD_createDStream();
    if (!z->zds) {
        g_free(z);
        error_setg(errp, "multifd %u: zstd createDStream failed", p->id);
        return -1;
    }

    ret = ZSTD_initDStream(z->zds);
    if (ZSTD_isError(ret)) {
        ZSTD_freeDStream(z->zds);
        g_free(z);
        error_setg(errp, "multifd %u: initDStream failed with error %s",
                   p->id, ZSTD_getErrorName(ret));
        return -1;
    }

    /* To be safe, we reserve twice the size of the packet */
    z->zbuff_len = MULTIFD_PACKET_SIZE * 2;
    z->zbuff = g_try_malloc(z->zbuff_len);
    if (!z->zbuff) {
        ZSTD_freeDStream(z->zds);
        g_free(z);
        error_setg(errp, "multifd %u: out of memory for zbuff", p->id);
        return -1;
    }
    return 0;
}

/**
 * zstd_recv_cleanup: setup receive side
 *
 * For no compression this function does nothing.
 *
 * @p: Params for the channel that we are using
 */
static void zstd_recv_cleanup(MultiFDRecvParams *p)
{
    struct zstd_data *z = p->data;

    ZSTD_freeDStream(z->zds);
    z->zds = NULL;
    g_free(z->zbuff);
    z->zbuff = NULL;
    g_free(p->data);
    p->data = NULL;
}

/**
 * zstd_recv_pages: read the data from the channel into actual pages
 *
 * Read the compressed buffer, and uncompress it into the actual
 * pages.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int zstd_recv_pages(MultiFDRecvParams *p, Error **errp)
{
    uint32_t in_size = p->next_packet_size;
    uint32_t out_size = 0;
    uint32_t expected_size = p->normal_num * p->page_size;
    uint32_t flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;
    struct zstd_data *z = p->data;
    int ret;
    int i;

    if (flags != MULTIFD_FLAG_ZSTD) {
        error_setg(errp, "multifd %u: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_ZSTD);
        return -1;
    }
    ret = qio_channel_read_all(p->c, (void *)z->zbuff, in_size, errp);

    if (ret != 0) {
        return ret;
    }

    z->in.src = z->zbuff;
    z->in.size = in_size;
    z->in.pos = 0;

    for (i = 0; i < p->normal_num; i++) {
        z->out.dst = p->host + p->normal[i];
        z->out.size = p->page_size;
        z->out.pos = 0;

        /*
         * Welcome to decompressStream semantics
         *
         * We need to loop while:
         * - return is > 0
         * - there is input available
         * - we haven't put out a full page
         */
        do {
            ret = ZSTD_decompressStream(z->zds, &z->out, &z->in);
        } while (ret > 0 && (z->in.size - z->in.pos > 0)
                         && (z->out.pos < p->page_size));
        if (ret > 0 && (z->out.pos < p->page_size)) {
            error_setg(errp, "multifd %u: decompressStream buffer too small",
                       p->id);
            return -1;
        }
        if (ZSTD_isError(ret)) {
            error_setg(errp, "multifd %u: decompressStream returned %s",
                       p->id, ZSTD_getErrorName(ret));
            return ret;
        }
        out_size += z->out.pos;
    }
    if (out_size != expected_size) {
        error_setg(errp, "multifd %u: packet size received %u size expected %u",
                   p->id, out_size, expected_size);
        return -1;
    }
    return 0;
}

static MultiFDMethods multifd_zstd_ops = {
    .send_setup = zstd_send_setup,
    .send_cleanup = zstd_send_cleanup,
    .send_prepare = zstd_send_prepare,
    .recv_setup = zstd_recv_setup,
    .recv_cleanup = zstd_recv_cleanup,
    .recv_pages = zstd_recv_pages
};

static void multifd_zstd_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_ZSTD, &multifd_zstd_ops);
}

migration_init(multifd_zstd_register);
