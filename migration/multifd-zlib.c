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
#include <zlib.h>
#include "qemu/rcu.h"
#include "exec/target_page.h"
#include "qapi/error.h"
#include "migration.h"
#include "trace.h"
#include "multifd.h"

struct zlib_data {
    /* stream for compression */
    z_stream zs;
    /* compressed buffer */
    uint8_t *zbuff;
    /* size of compressed buffer */
    uint32_t zbuff_len;
};

/* Multifd zlib compression */

/**
 * zlib_send_setup: setup send side
 *
 * Setup each channel with zlib compression.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int zlib_send_setup(MultiFDSendParams *p, Error **errp)
{
    uint32_t page_count = MULTIFD_PACKET_SIZE / qemu_target_page_size();
    struct zlib_data *z = g_malloc0(sizeof(struct zlib_data));
    z_stream *zs = &z->zs;

    zs->zalloc = Z_NULL;
    zs->zfree = Z_NULL;
    zs->opaque = Z_NULL;
    if (deflateInit(zs, migrate_multifd_zlib_level()) != Z_OK) {
        g_free(z);
        error_setg(errp, "multifd %d: deflate init failed", p->id);
        return -1;
    }
    /* We will never have more than page_count pages */
    z->zbuff_len = page_count * qemu_target_page_size();
    z->zbuff_len *= 2;
    z->zbuff = g_try_malloc(z->zbuff_len);
    if (!z->zbuff) {
        deflateEnd(&z->zs);
        g_free(z);
        error_setg(errp, "multifd %d: out of memory for zbuff", p->id);
        return -1;
    }
    p->data = z;
    return 0;
}

/**
 * zlib_send_cleanup: cleanup send side
 *
 * Close the channel and return memory.
 *
 * @p: Params for the channel that we are using
 */
static void zlib_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    struct zlib_data *z = p->data;

    deflateEnd(&z->zs);
    g_free(z->zbuff);
    z->zbuff = NULL;
    g_free(p->data);
    p->data = NULL;
}

/**
 * zlib_send_prepare: prepare date to be able to send
 *
 * Create a compressed buffer with all the pages that we are going to
 * send.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @used: number of pages used
 */
static int zlib_send_prepare(MultiFDSendParams *p, uint32_t used, Error **errp)
{
    struct iovec *iov = p->pages->iov;
    struct zlib_data *z = p->data;
    z_stream *zs = &z->zs;
    uint32_t out_size = 0;
    int ret;
    uint32_t i;

    for (i = 0; i < used; i++) {
        uint32_t available = z->zbuff_len - out_size;
        int flush = Z_NO_FLUSH;

        if (i == used - 1) {
            flush = Z_SYNC_FLUSH;
        }

        zs->avail_in = iov[i].iov_len;
        zs->next_in = iov[i].iov_base;

        zs->avail_out = available;
        zs->next_out = z->zbuff + out_size;

        /*
         * Welcome to deflate semantics
         *
         * We need to loop while:
         * - return is Z_OK
         * - there are stuff to be compressed
         * - there are output space free
         */
        do {
            ret = deflate(zs, flush);
        } while (ret == Z_OK && zs->avail_in && zs->avail_out);
        if (ret == Z_OK && zs->avail_in) {
            error_setg(errp, "multifd %d: deflate failed to compress all input",
                       p->id);
            return -1;
        }
        if (ret != Z_OK) {
            error_setg(errp, "multifd %d: deflate returned %d instead of Z_OK",
                       p->id, ret);
            return -1;
        }
        out_size += available - zs->avail_out;
    }
    p->next_packet_size = out_size;
    p->flags |= MULTIFD_FLAG_ZLIB;

    return 0;
}

/**
 * zlib_send_write: do the actual write of the data
 *
 * Do the actual write of the comprresed buffer.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @used: number of pages used
 * @errp: pointer to an error
 */
static int zlib_send_write(MultiFDSendParams *p, uint32_t used, Error **errp)
{
    struct zlib_data *z = p->data;

    return qio_channel_write_all(p->c, (void *)z->zbuff, p->next_packet_size,
                                 errp);
}

/**
 * zlib_recv_setup: setup receive side
 *
 * Create the compressed channel and buffer.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int zlib_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    uint32_t page_count = MULTIFD_PACKET_SIZE / qemu_target_page_size();
    struct zlib_data *z = g_malloc0(sizeof(struct zlib_data));
    z_stream *zs = &z->zs;

    p->data = z;
    zs->zalloc = Z_NULL;
    zs->zfree = Z_NULL;
    zs->opaque = Z_NULL;
    zs->avail_in = 0;
    zs->next_in = Z_NULL;
    if (inflateInit(zs) != Z_OK) {
        error_setg(errp, "multifd %d: inflate init failed", p->id);
        return -1;
    }
    /* We will never have more than page_count pages */
    z->zbuff_len = page_count * qemu_target_page_size();
    /* We know compression "could" use more space */
    z->zbuff_len *= 2;
    z->zbuff = g_try_malloc(z->zbuff_len);
    if (!z->zbuff) {
        inflateEnd(zs);
        error_setg(errp, "multifd %d: out of memory for zbuff", p->id);
        return -1;
    }
    return 0;
}

/**
 * zlib_recv_cleanup: setup receive side
 *
 * For no compression this function does nothing.
 *
 * @p: Params for the channel that we are using
 */
static void zlib_recv_cleanup(MultiFDRecvParams *p)
{
    struct zlib_data *z = p->data;

    inflateEnd(&z->zs);
    g_free(z->zbuff);
    z->zbuff = NULL;
    g_free(p->data);
    p->data = NULL;
}

/**
 * zlib_recv_pages: read the data from the channel into actual pages
 *
 * Read the compressed buffer, and uncompress it into the actual
 * pages.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @used: number of pages used
 * @errp: pointer to an error
 */
static int zlib_recv_pages(MultiFDRecvParams *p, uint32_t used, Error **errp)
{
    struct zlib_data *z = p->data;
    z_stream *zs = &z->zs;
    uint32_t in_size = p->next_packet_size;
    /* we measure the change of total_out */
    uint32_t out_size = zs->total_out;
    uint32_t expected_size = used * qemu_target_page_size();
    uint32_t flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;
    int ret;
    int i;

    if (flags != MULTIFD_FLAG_ZLIB) {
        error_setg(errp, "multifd %d: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_ZLIB);
        return -1;
    }
    ret = qio_channel_read_all(p->c, (void *)z->zbuff, in_size, errp);

    if (ret != 0) {
        return ret;
    }

    zs->avail_in = in_size;
    zs->next_in = z->zbuff;

    for (i = 0; i < used; i++) {
        struct iovec *iov = &p->pages->iov[i];
        int flush = Z_NO_FLUSH;
        unsigned long start = zs->total_out;

        if (i == used - 1) {
            flush = Z_SYNC_FLUSH;
        }

        zs->avail_out = iov->iov_len;
        zs->next_out = iov->iov_base;

        /*
         * Welcome to inflate semantics
         *
         * We need to loop while:
         * - return is Z_OK
         * - there are input available
         * - we haven't completed a full page
         */
        do {
            ret = inflate(zs, flush);
        } while (ret == Z_OK && zs->avail_in
                             && (zs->total_out - start) < iov->iov_len);
        if (ret == Z_OK && (zs->total_out - start) < iov->iov_len) {
            error_setg(errp, "multifd %d: inflate generated too few output",
                       p->id);
            return -1;
        }
        if (ret != Z_OK) {
            error_setg(errp, "multifd %d: inflate returned %d instead of Z_OK",
                       p->id, ret);
            return -1;
        }
    }
    out_size = zs->total_out - out_size;
    if (out_size != expected_size) {
        error_setg(errp, "multifd %d: packet size received %d size expected %d",
                   p->id, out_size, expected_size);
        return -1;
    }
    return 0;
}

static MultiFDMethods multifd_zlib_ops = {
    .send_setup = zlib_send_setup,
    .send_cleanup = zlib_send_cleanup,
    .send_prepare = zlib_send_prepare,
    .send_write = zlib_send_write,
    .recv_setup = zlib_recv_setup,
    .recv_cleanup = zlib_recv_cleanup,
    .recv_pages = zlib_recv_pages
};

static void multifd_zlib_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_ZLIB, &multifd_zlib_ops);
}

migration_init(multifd_zlib_register);
