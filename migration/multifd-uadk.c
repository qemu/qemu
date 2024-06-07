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
#include "migration.h"
#include "multifd.h"
#include "options.h"
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

    if (!uadk_hw_init()) {
        error_setg(errp, "multifd: UADK hardware not available");
        return NULL;
    }

    wd = g_new0(struct wd_data, 1);
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

    wd->buf = g_try_malloc(size);
    if (!wd->buf) {
        error_setg(errp, "multifd: out of mem for uadk buf");
        goto out_free_sess;
    }
    wd->buf_hdr = g_new0(uint32_t, count);
    return wd;

out_free_sess:
    wd_comp_free_sess(wd->handle);
out:
    wd_comp_uninit2();
    g_free(wd);
    return NULL;
}

static void multifd_uadk_uninit_sess(struct wd_data *wd)
{
    wd_comp_free_sess(wd->handle);
    wd_comp_uninit2();
    g_free(wd->buf);
    g_free(wd->buf_hdr);
    g_free(wd);
}

/**
 * multifd_uadk_send_setup: setup send side
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int multifd_uadk_send_setup(MultiFDSendParams *p, Error **errp)
{
    struct wd_data *wd;

    wd = multifd_uadk_init_sess(p->page_count, p->page_size, true, errp);
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

    p->iov = g_new0(struct iovec, p->page_count + 2);
    return 0;
}

/**
 * multifd_uadk_send_cleanup: cleanup send side
 *
 * Close the channel and return memory.
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static void multifd_uadk_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    struct wd_data *wd = p->compress_data;

    multifd_uadk_uninit_sess(wd);
    p->compress_data = NULL;
}

/**
 * multifd_uadk_send_prepare: prepare data to be able to send
 *
 * Create a compressed buffer with all the pages that we are going to
 * send.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int multifd_uadk_send_prepare(MultiFDSendParams *p, Error **errp)
{
    return -1;
}

/**
 * multifd_uadk_recv_setup: setup receive side
 *
 * Create the compressed channel and buffer.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int multifd_uadk_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    struct wd_data *wd;

    wd = multifd_uadk_init_sess(p->page_count, p->page_size, false, errp);
    if (!wd) {
        return -1;
    }
    p->compress_data = wd;
    return 0;
}

/**
 * multifd_uadk_recv_cleanup: cleanup receive side
 *
 * Close the channel and return memory.
 *
 * @p: Params for the channel that we are using
 */
static void multifd_uadk_recv_cleanup(MultiFDRecvParams *p)
{
    struct wd_data *wd = p->compress_data;

    multifd_uadk_uninit_sess(wd);
    p->compress_data = NULL;
}

/**
 * multifd_uadk_recv: read the data from the channel into actual pages
 *
 * Read the compressed buffer, and uncompress it into the actual
 * pages.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int multifd_uadk_recv(MultiFDRecvParams *p, Error **errp)
{
    return -1;
}

static MultiFDMethods multifd_uadk_ops = {
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
