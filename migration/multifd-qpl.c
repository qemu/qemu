/*
 * Multifd qpl compression accelerator implementation
 *
 * Copyright (c) 2023 Intel Corporation
 *
 * Authors:
 *  Yuan Liu<yuan1.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "multifd.h"
#include "qpl/qpl.h"

typedef struct {
    /* the QPL hardware path job */
    qpl_job *job;
    /* indicates if fallback to software path is required */
    bool fallback_sw_path;
    /* output data from the software path */
    uint8_t *sw_output;
    /* output data length from the software path */
    uint32_t sw_output_len;
} QplHwJob;

typedef struct {
    /* array of hardware jobs, the number of jobs equals the number pages */
    QplHwJob *hw_jobs;
    /* the QPL software job for the slow path and software fallback */
    qpl_job *sw_job;
    /* the number of pages that the QPL needs to process at one time */
    uint32_t page_num;
    /* array of compressed page buffers */
    uint8_t *zbuf;
    /* array of compressed page lengths */
    uint32_t *zlen;
    /* the status of the hardware device */
    bool hw_avail;
} QplData;

/**
 * check_hw_avail: check if IAA hardware is available
 *
 * If the IAA hardware does not exist or is unavailable,
 * the QPL hardware job initialization will fail.
 *
 * Returns true if IAA hardware is available, otherwise false.
 *
 * @job_size: indicates the hardware job size if hardware is available
 */
static bool check_hw_avail(uint32_t *job_size)
{
    qpl_path_t path = qpl_path_hardware;
    uint32_t size = 0;
    qpl_job *job;

    if (qpl_get_job_size(path, &size) != QPL_STS_OK) {
        return false;
    }
    assert(size > 0);
    job = g_malloc0(size);
    if (qpl_init_job(path, job) != QPL_STS_OK) {
        g_free(job);
        return false;
    }
    g_free(job);
    *job_size = size;
    return true;
}

/**
 * multifd_qpl_free_sw_job: clean up software job
 *
 * Free the software job resources.
 *
 * @qpl: pointer to the QplData structure
 */
static void multifd_qpl_free_sw_job(QplData *qpl)
{
    assert(qpl);
    if (qpl->sw_job) {
        qpl_fini_job(qpl->sw_job);
        g_free(qpl->sw_job);
        qpl->sw_job = NULL;
    }
}

/**
 * multifd_qpl_free_jobs: clean up hardware jobs
 *
 * Free all hardware job resources.
 *
 * @qpl: pointer to the QplData structure
 */
static void multifd_qpl_free_hw_job(QplData *qpl)
{
    assert(qpl);
    if (qpl->hw_jobs) {
        for (int i = 0; i < qpl->page_num; i++) {
            qpl_fini_job(qpl->hw_jobs[i].job);
            g_free(qpl->hw_jobs[i].job);
            qpl->hw_jobs[i].job = NULL;
        }
        g_free(qpl->hw_jobs);
        qpl->hw_jobs = NULL;
    }
}

/**
 * multifd_qpl_init_sw_job: initialize a software job
 *
 * Use the QPL software path to initialize a job
 *
 * @qpl: pointer to the QplData structure
 * @errp: pointer to an error
 */
static int multifd_qpl_init_sw_job(QplData *qpl, Error **errp)
{
    qpl_path_t path = qpl_path_software;
    uint32_t size = 0;
    qpl_job *job = NULL;
    qpl_status status;

    status = qpl_get_job_size(path, &size);
    if (status != QPL_STS_OK) {
        error_setg(errp, "qpl_get_job_size failed with error %d", status);
        return -1;
    }
    job = g_malloc0(size);
    status = qpl_init_job(path, job);
    if (status != QPL_STS_OK) {
        error_setg(errp, "qpl_init_job failed with error %d", status);
        g_free(job);
        return -1;
    }
    qpl->sw_job = job;
    return 0;
}

/**
 * multifd_qpl_init_jobs: initialize hardware jobs
 *
 * Use the QPL hardware path to initialize jobs
 *
 * @qpl: pointer to the QplData structure
 * @size: the size of QPL hardware path job
 * @errp: pointer to an error
 */
static void multifd_qpl_init_hw_job(QplData *qpl, uint32_t size, Error **errp)
{
    qpl_path_t path = qpl_path_hardware;
    qpl_job *job = NULL;
    qpl_status status;

    qpl->hw_jobs = g_new0(QplHwJob, qpl->page_num);
    for (int i = 0; i < qpl->page_num; i++) {
        job = g_malloc0(size);
        status = qpl_init_job(path, job);
        /* the job initialization should succeed after check_hw_avail */
        assert(status == QPL_STS_OK);
        qpl->hw_jobs[i].job = job;
    }
}

/**
 * multifd_qpl_init: initialize QplData structure
 *
 * Allocate and initialize a QplData structure
 *
 * Returns a QplData pointer on success or NULL on error
 *
 * @num: the number of pages
 * @size: the page size
 * @errp: pointer to an error
 */
static QplData *multifd_qpl_init(uint32_t num, uint32_t size, Error **errp)
{
    uint32_t job_size = 0;
    QplData *qpl;

    qpl = g_new0(QplData, 1);
    qpl->page_num = num;
    if (multifd_qpl_init_sw_job(qpl, errp) != 0) {
        g_free(qpl);
        return NULL;
    }
    qpl->hw_avail = check_hw_avail(&job_size);
    if (qpl->hw_avail) {
        multifd_qpl_init_hw_job(qpl, job_size, errp);
    }
    qpl->zbuf = g_malloc0(size * num);
    qpl->zlen = g_new0(uint32_t, num);
    return qpl;
}

/**
 * multifd_qpl_deinit: clean up QplData structure
 *
 * Free jobs, buffers and the QplData structure
 *
 * @qpl: pointer to the QplData structure
 */
static void multifd_qpl_deinit(QplData *qpl)
{
    if (qpl) {
        multifd_qpl_free_sw_job(qpl);
        multifd_qpl_free_hw_job(qpl);
        g_free(qpl->zbuf);
        g_free(qpl->zlen);
        g_free(qpl);
    }
}

/**
 * multifd_qpl_send_setup: set up send side
 *
 * Set up the channel with QPL compression.
 *
 * Returns 0 on success or -1 on error
 *
 * @p: Params for the channel being used
 * @errp: pointer to an error
 */
static int multifd_qpl_send_setup(MultiFDSendParams *p, Error **errp)
{
    QplData *qpl;

    qpl = multifd_qpl_init(p->page_count, p->page_size, errp);
    if (!qpl) {
        return -1;
    }
    p->compress_data = qpl;

    /*
     * the page will be compressed independently and sent using an IOV. The
     * additional two IOVs are used to store packet header and compressed data
     * length
     */
    p->iov = g_new0(struct iovec, p->page_count + 2);
    return 0;
}

/**
 * multifd_qpl_send_cleanup: clean up send side
 *
 * Close the channel and free memory.
 *
 * @p: Params for the channel being used
 * @errp: pointer to an error
 */
static void multifd_qpl_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    multifd_qpl_deinit(p->compress_data);
    p->compress_data = NULL;
    g_free(p->iov);
    p->iov = NULL;
}

/**
 * multifd_qpl_send_prepare: prepare data to be able to send
 *
 * Create a compressed buffer with all the pages that we are going to
 * send.
 *
 * Returns 0 on success or -1 on error
 *
 * @p: Params for the channel being used
 * @errp: pointer to an error
 */
static int multifd_qpl_send_prepare(MultiFDSendParams *p, Error **errp)
{
    /* Implement in next patch */
    return -1;
}

/**
 * multifd_qpl_recv_setup: set up receive side
 *
 * Create the compressed channel and buffer.
 *
 * Returns 0 on success or -1 on error
 *
 * @p: Params for the channel being used
 * @errp: pointer to an error
 */
static int multifd_qpl_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    QplData *qpl;

    qpl = multifd_qpl_init(p->page_count, p->page_size, errp);
    if (!qpl) {
        return -1;
    }
    p->compress_data = qpl;
    return 0;
}

/**
 * multifd_qpl_recv_cleanup: set up receive side
 *
 * Close the channel and free memory.
 *
 * @p: Params for the channel being used
 */
static void multifd_qpl_recv_cleanup(MultiFDRecvParams *p)
{
    multifd_qpl_deinit(p->compress_data);
    p->compress_data = NULL;
}

/**
 * multifd_qpl_recv: read the data from the channel into actual pages
 *
 * Read the compressed buffer, and uncompress it into the actual
 * pages.
 *
 * Returns 0 on success or -1 on error
 *
 * @p: Params for the channel being used
 * @errp: pointer to an error
 */
static int multifd_qpl_recv(MultiFDRecvParams *p, Error **errp)
{
    /* Implement in next patch */
    return -1;
}

static MultiFDMethods multifd_qpl_ops = {
    .send_setup = multifd_qpl_send_setup,
    .send_cleanup = multifd_qpl_send_cleanup,
    .send_prepare = multifd_qpl_send_prepare,
    .recv_setup = multifd_qpl_recv_setup,
    .recv_cleanup = multifd_qpl_recv_cleanup,
    .recv = multifd_qpl_recv,
};

static void multifd_qpl_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_QPL, &multifd_qpl_ops);
}

migration_init(multifd_qpl_register);
