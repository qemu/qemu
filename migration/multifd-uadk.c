/*
 * Multifd UADK compression accelerator implementation
 *
 * Copyright (c) 2024 Huawei Technologies R & D (UK) Ltd
 *
 * Authors:
 *  Shameer Kolothum <shameerali.kolothum.thodi@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "system/ramblock.h"
#include "migration.h"
#include "multifd.h"
#include "options.h"
#include "qemu/error-report.h"
#include "uadk/wd_comp.h"
#include "uadk/wd_sched.h"

struct wd_data {
    handle_t handle;
    uint8_t *buf;
    uint32_t *buf_hdr;
};

static bool uadk_hw_init(void)
{
    char alg[] = "zlib";
    int ret;

    ret = wd_comp_init2(alg, SCHED_POLICY_RR, TASK_HW);
    if (ret && ret != -WD_EEXIST) {
        return false;
    } else {
        return true;
    }
}

static struct wd_data *multifd_uadk_init_sess(uint32_t count,
                                              uint32_t page_size,
                                              bool compress, Error **errp)
{
    struct wd_comp_sess_setup ss = {0};
    struct sched_params param = {0};
    uint32_t size = count * page_size;
    struct wd_data *wd;

    wd = g_new0(struct wd_data, 1);

    if (uadk_hw_init()) {
        ss.alg_type = WD_ZLIB;
        if (compress) {
            ss.op_type = WD_DIR_COMPRESS;
            /* Add an additional page for handling output > input */
            size += page_size;
        } else {
            ss.op_type = WD_DIR_DECOMPRESS;
        }
        /* We use default level 1 compression and 4K window size */
        param.type = ss.op_type;
        ss.sched_param = &param;

        wd->handle = wd_comp_alloc_sess(&ss);
        if (!wd->handle) {
            error_setg(errp, "multifd: failed wd_comp_alloc_sess");
            goto out;
        }
    } else {
        /* For CI test use */
        warn_report_once("UADK hardware not available. Switch to no compression mode");
    }

    wd->buf = g_try_malloc(size);
    if (!wd->buf) {
        error_setg(errp, "multifd: out of mem for uadk buf");
        goto out_free_sess;
    }
    wd->buf_hdr = g_new0(uint32_t, count);
    return wd;

out_free_sess:
    if (wd->handle) {
        wd_comp_free_sess(wd->handle);
    }
out:
    wd_comp_uninit2();
    g_free(wd);
    return NULL;
}

static void multifd_uadk_uninit_sess(struct wd_data *wd)
{
    if (wd->handle) {
        wd_comp_free_sess(wd->handle);
    }
    wd_comp_uninit2();
    g_free(wd->buf);
    g_free(wd->buf_hdr);
    g_free(wd);
}

static int multifd_uadk_send_setup(MultiFDSendParams *p, Error **errp)
{
    struct wd_data *wd;
    uint32_t page_size = multifd_ram_page_size();
    uint32_t page_count = multifd_ram_page_count();

    wd = multifd_uadk_init_sess(page_count, page_size, true, errp);
    if (!wd) {
        return -1;
    }

    p->compress_data = wd;
    assert(p->iov == NULL);
    /*
     * Each page will be compressed independently and sent using an IOV. The
     * additional two IOVs are used to store packet header and compressed data
     * length
     */

    p->iov = g_new0(struct iovec, page_count + 2);
    return 0;
}

static void multifd_uadk_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    struct wd_data *wd = p->compress_data;

    multifd_uadk_uninit_sess(wd);
    p->compress_data = NULL;
    g_free(p->iov);
    p->iov = NULL;
}

static inline void prepare_next_iov(MultiFDSendParams *p, void *base,
                                    uint32_t len)
{
    p->iov[p->iovs_num].iov_base = (uint8_t *)base;
    p->iov[p->iovs_num].iov_len = len;
    p->next_packet_size += len;
    p->iovs_num++;
}

static int multifd_uadk_send_prepare(MultiFDSendParams *p, Error **errp)
{
    struct wd_data *uadk_data = p->compress_data;
    uint32_t hdr_size;
    uint32_t page_size = multifd_ram_page_size();
    uint8_t *buf = uadk_data->buf;
    int ret = 0;
    MultiFDPages_t *pages = &p->data->u.ram;

    if (!multifd_send_prepare_common(p)) {
        goto out;
    }

    hdr_size = pages->normal_num * sizeof(uint32_t);
    /* prepare the header that stores the lengths of all compressed data */
    prepare_next_iov(p, uadk_data->buf_hdr, hdr_size);

    for (int i = 0; i < pages->normal_num; i++) {
        struct wd_comp_req creq = {
            .op_type = WD_DIR_COMPRESS,
            .src     = pages->block->host + pages->offset[i],
            .src_len = page_size,
            .dst     = buf,
            /* Set dst_len to double the src in case compressed out >= page_size */
            .dst_len = page_size * 2,
        };

        if (uadk_data->handle) {
            ret = wd_do_comp_sync(uadk_data->handle, &creq);
            if (ret || creq.status) {
                error_setg(errp, "multifd %u: failed compression, ret %d status %d",
                           p->id, ret, creq.status);
                return -1;
            }
            if (creq.dst_len < page_size) {
                uadk_data->buf_hdr[i] = cpu_to_be32(creq.dst_len);
                prepare_next_iov(p, buf, creq.dst_len);
                buf += creq.dst_len;
            }
        }
        /*
         * Send raw data if no UADK hardware or if compressed out >= page_size.
         * We might be better off sending raw data if output is slightly less
         * than page_size as well because at the receive end we can skip the
         * decompression. But it is tricky to find the right number here.
         */
        if (!uadk_data->handle || creq.dst_len >= page_size) {
            uadk_data->buf_hdr[i] = cpu_to_be32(page_size);
            prepare_next_iov(p, pages->block->host + pages->offset[i],
                             page_size);
            buf += page_size;
        }
    }
out:
    p->flags |= MULTIFD_FLAG_UADK;
    multifd_send_fill_packet(p);
    return 0;
}

static int multifd_uadk_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    struct wd_data *wd;
    uint32_t page_size = multifd_ram_page_size();
    uint32_t page_count = multifd_ram_page_count();

    wd = multifd_uadk_init_sess(page_count, page_size, false, errp);
    if (!wd) {
        return -1;
    }
    p->compress_data = wd;
    return 0;
}

static void multifd_uadk_recv_cleanup(MultiFDRecvParams *p)
{
    struct wd_data *wd = p->compress_data;

    multifd_uadk_uninit_sess(wd);
    p->compress_data = NULL;
}

static int multifd_uadk_recv(MultiFDRecvParams *p, Error **errp)
{
    struct wd_data *uadk_data = p->compress_data;
    uint32_t in_size = p->next_packet_size;
    uint32_t flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;
    uint32_t hdr_len = p->normal_num * sizeof(uint32_t);
    uint32_t data_len = 0;
    uint32_t page_size = multifd_ram_page_size();
    uint8_t *buf = uadk_data->buf;
    int ret = 0;

    if (flags != MULTIFD_FLAG_UADK) {
        error_setg(errp, "multifd %u: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_ZLIB);
        return -1;
    }

    multifd_recv_zero_page_process(p);
    if (!p->normal_num) {
        assert(in_size == 0);
        return 0;
    }

    /* read compressed data lengths */
    assert(hdr_len < in_size);
    ret = qio_channel_read_all(p->c, (void *) uadk_data->buf_hdr,
                               hdr_len, errp);
    if (ret != 0) {
        return ret;
    }

    for (int i = 0; i < p->normal_num; i++) {
        uadk_data->buf_hdr[i] = be32_to_cpu(uadk_data->buf_hdr[i]);
        data_len += uadk_data->buf_hdr[i];
        assert(uadk_data->buf_hdr[i] <= page_size);
    }

    /* read compressed data */
    assert(in_size == hdr_len + data_len);
    ret = qio_channel_read_all(p->c, (void *)buf, data_len, errp);
    if (ret != 0) {
        return ret;
    }

    for (int i = 0; i < p->normal_num; i++) {
        struct wd_comp_req creq = {
            .op_type = WD_DIR_DECOMPRESS,
            .src     = buf,
            .src_len = uadk_data->buf_hdr[i],
            .dst     = p->host + p->normal[i],
            .dst_len = page_size,
        };

        if (uadk_data->buf_hdr[i] == page_size) {
            memcpy(p->host + p->normal[i], buf, page_size);
            buf += page_size;
            continue;
        }

        if (unlikely(!uadk_data->handle)) {
            error_setg(errp, "multifd %u: UADK HW not available for decompression",
                       p->id);
            return -1;
        }

        ret = wd_do_comp_sync(uadk_data->handle, &creq);
        if (ret || creq.status) {
            error_setg(errp, "multifd %u: failed decompression, ret %d status %d",
                       p->id, ret, creq.status);
            return -1;
        }
        if (creq.dst_len != page_size) {
            error_setg(errp, "multifd %u: decompressed length error", p->id);
            return -1;
        }
        buf += uadk_data->buf_hdr[i];
     }

    return 0;
}

static const MultiFDMethods multifd_uadk_ops = {
    .send_setup = multifd_uadk_send_setup,
    .send_cleanup = multifd_uadk_send_cleanup,
    .send_prepare = multifd_uadk_send_prepare,
    .recv_setup = multifd_uadk_recv_setup,
    .recv_cleanup = multifd_uadk_recv_cleanup,
    .recv = multifd_uadk_recv,
};

static void multifd_uadk_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_UADK, &multifd_uadk_ops);
}
migration_init(multifd_uadk_register);
