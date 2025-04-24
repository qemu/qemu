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
#include "system/ramblock.h"
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

static int multifd_zstd_send_setup(MultiFDSendParams *p, Error **errp)
{
    struct zstd_data *z = g_new0(struct zstd_data, 1);
    int res;

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
    p->compress_data = z;

    /* Needs 2 IOVs, one for packet header and one for compressed data */
    p->iov = g_new0(struct iovec, 2);
    return 0;
}

static void multifd_zstd_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    struct zstd_data *z = p->compress_data;

    ZSTD_freeCStream(z->zcs);
    z->zcs = NULL;
    g_free(z->zbuff);
    z->zbuff = NULL;
    g_free(p->compress_data);
    p->compress_data = NULL;

    g_free(p->iov);
    p->iov = NULL;
}

static int multifd_zstd_send_prepare(MultiFDSendParams *p, Error **errp)
{
    MultiFDPages_t *pages = &p->data->u.ram;
    struct zstd_data *z = p->compress_data;
    int ret;
    uint32_t i;

    if (!multifd_send_prepare_common(p)) {
        goto out;
    }

    z->out.dst = z->zbuff;
    z->out.size = z->zbuff_len;
    z->out.pos = 0;

    for (i = 0; i < pages->normal_num; i++) {
        ZSTD_EndDirective flush = ZSTD_e_continue;

        if (i == pages->normal_num - 1) {
            flush = ZSTD_e_flush;
        }
        z->in.src = pages->block->host + pages->offset[i];
        z->in.size = multifd_ram_page_size();
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
        } while (ret > 0 && (z->in.size > z->in.pos)
                         && (z->out.size > z->out.pos));
        if (ret > 0 && (z->in.size > z->in.pos)) {
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

out:
    p->flags |= MULTIFD_FLAG_ZSTD;
    multifd_send_fill_packet(p);
    return 0;
}

static int multifd_zstd_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    struct zstd_data *z = g_new0(struct zstd_data, 1);
    int ret;

    p->compress_data = z;
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

static void multifd_zstd_recv_cleanup(MultiFDRecvParams *p)
{
    struct zstd_data *z = p->compress_data;

    ZSTD_freeDStream(z->zds);
    z->zds = NULL;
    g_free(z->zbuff);
    z->zbuff = NULL;
    g_free(p->compress_data);
    p->compress_data = NULL;
}

static int multifd_zstd_recv(MultiFDRecvParams *p, Error **errp)
{
    uint32_t in_size = p->next_packet_size;
    uint32_t out_size = 0;
    uint32_t page_size = multifd_ram_page_size();
    uint32_t expected_size = p->normal_num * page_size;
    uint32_t flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;
    struct zstd_data *z = p->compress_data;
    int ret;
    int i;

    if (flags != MULTIFD_FLAG_ZSTD) {
        error_setg(errp, "multifd %u: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_ZSTD);
        return -1;
    }

    multifd_recv_zero_page_process(p);

    if (!p->normal_num) {
        assert(in_size == 0);
        return 0;
    }

    ret = qio_channel_read_all(p->c, (void *)z->zbuff, in_size, errp);

    if (ret != 0) {
        return ret;
    }

    z->in.src = z->zbuff;
    z->in.size = in_size;
    z->in.pos = 0;

    for (i = 0; i < p->normal_num; i++) {
        ramblock_recv_bitmap_set_offset(p->block, p->normal[i]);
        z->out.dst = p->host + p->normal[i];
        z->out.size = page_size;
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
        } while (ret > 0 && (z->in.size > z->in.pos)
                         && (z->out.pos < page_size));
        if (ret > 0 && (z->out.pos < page_size)) {
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

static const MultiFDMethods multifd_zstd_ops = {
    .send_setup = multifd_zstd_send_setup,
    .send_cleanup = multifd_zstd_send_cleanup,
    .send_prepare = multifd_zstd_send_prepare,
    .recv_setup = multifd_zstd_recv_setup,
    .recv_cleanup = multifd_zstd_recv_cleanup,
    .recv = multifd_zstd_recv
};

static void multifd_zstd_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_ZSTD, &multifd_zstd_ops);
}

migration_init(multifd_zstd_register);
