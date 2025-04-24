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
#include "qapi/qapi-types-migration.h"
#include "system/ramblock.h"
#include "multifd.h"
#include "qpl/qpl.h"

/* Maximum number of retries to resubmit a job if IAA work queues are full */
#define MAX_SUBMIT_RETRY_NUM (3)

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

static int multifd_qpl_send_setup(MultiFDSendParams *p, Error **errp)
{
    QplData *qpl;
    uint32_t page_size = multifd_ram_page_size();
    uint32_t page_count = multifd_ram_page_count();

    qpl = multifd_qpl_init(page_count, page_size, errp);
    if (!qpl) {
        return -1;
    }
    p->compress_data = qpl;

    /*
     * the page will be compressed independently and sent using an IOV. The
     * additional two IOVs are used to store packet header and compressed data
     * length
     */
    p->iov = g_new0(struct iovec, page_count + 2);
    return 0;
}

static void multifd_qpl_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    multifd_qpl_deinit(p->compress_data);
    p->compress_data = NULL;
    g_free(p->iov);
    p->iov = NULL;
}

/**
 * multifd_qpl_prepare_job: prepare the job
 *
 * Set the QPL job parameters and properties.
 *
 * @job: pointer to the qpl_job structure
 * @is_compression: indicates compression and decompression
 * @input: pointer to the input data buffer
 * @input_len: the length of the input data
 * @output: pointer to the output data buffer
 * @output_len: the length of the output data
 */
static void multifd_qpl_prepare_job(qpl_job *job, bool is_compression,
                                    uint8_t *input, uint32_t input_len,
                                    uint8_t *output, uint32_t output_len)
{
    job->op = is_compression ? qpl_op_compress : qpl_op_decompress;
    job->next_in_ptr = input;
    job->next_out_ptr = output;
    job->available_in = input_len;
    job->available_out = output_len;
    job->flags = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_OMIT_VERIFY;
    /* only supports compression level 1 */
    job->level = 1;
}

/**
 * multifd_qpl_prepare_comp_job: prepare the compression job
 *
 * Set the compression job parameters and properties.
 *
 * @job: pointer to the qpl_job structure
 * @input: pointer to the input data buffer
 * @output: pointer to the output data buffer
 * @size: the page size
 */
static void multifd_qpl_prepare_comp_job(qpl_job *job, uint8_t *input,
                                         uint8_t *output, uint32_t size)
{
    /*
     * Set output length to less than the page size to force the job to
     * fail in case it compresses to a larger size. We'll send that page
     * without compression and skip the decompression operation on the
     * destination.
     */
    multifd_qpl_prepare_job(job, true, input, size, output, size - 1);
}

/**
 * multifd_qpl_prepare_decomp_job: prepare the decompression job
 *
 * Set the decompression job parameters and properties.
 *
 * @job: pointer to the qpl_job structure
 * @input: pointer to the input data buffer
 * @len: the length of the input data
 * @output: pointer to the output data buffer
 * @size: the page size
 */
static void multifd_qpl_prepare_decomp_job(qpl_job *job, uint8_t *input,
                                           uint32_t len, uint8_t *output,
                                           uint32_t size)
{
    multifd_qpl_prepare_job(job, false, input, len, output, size);
}

/**
 * multifd_qpl_fill_iov: fill in the IOV
 *
 * Fill in the QPL packet IOV
 *
 * @p: Params for the channel being used
 * @data: pointer to the IOV data
 * @len: The length of the IOV data
 */
static void multifd_qpl_fill_iov(MultiFDSendParams *p, uint8_t *data,
                                 uint32_t len)
{
    p->iov[p->iovs_num].iov_base = data;
    p->iov[p->iovs_num].iov_len = len;
    p->iovs_num++;
    p->next_packet_size += len;
}

/**
 * multifd_qpl_fill_packet: fill the compressed page into the QPL packet
 *
 * Fill the compressed page length and IOV into the QPL packet
 *
 * @idx: The index of the compressed length array
 * @p: Params for the channel being used
 * @data: pointer to the compressed page buffer
 * @len: The length of the compressed page
 */
static void multifd_qpl_fill_packet(uint32_t idx, MultiFDSendParams *p,
                                    uint8_t *data, uint32_t len)
{
    QplData *qpl = p->compress_data;

    qpl->zlen[idx] = cpu_to_be32(len);
    multifd_qpl_fill_iov(p, data, len);
}

/**
 * multifd_qpl_submit_job: submit a job to the hardware
 *
 * Submit a QPL hardware job to the IAA device
 *
 * Returns true if the job is submitted successfully, otherwise false.
 *
 * @job: pointer to the qpl_job structure
 */
static bool multifd_qpl_submit_job(qpl_job *job)
{
    qpl_status status;
    uint32_t num = 0;

retry:
    status = qpl_submit_job(job);
    if (status == QPL_STS_QUEUES_ARE_BUSY_ERR) {
        if (num < MAX_SUBMIT_RETRY_NUM) {
            num++;
            goto retry;
        }
    }
    return (status == QPL_STS_OK);
}

/**
 * multifd_qpl_compress_pages_slow_path: compress pages using slow path
 *
 * Compress the pages using software. If compression fails, the uncompressed
 * page will be sent.
 *
 * @p: Params for the channel being used
 */
static void multifd_qpl_compress_pages_slow_path(MultiFDSendParams *p)
{
    QplData *qpl = p->compress_data;
    MultiFDPages_t *pages = &p->data->u.ram;
    uint32_t size = multifd_ram_page_size();
    qpl_job *job = qpl->sw_job;
    uint8_t *zbuf = qpl->zbuf;
    uint8_t *buf;

    for (int i = 0; i < pages->normal_num; i++) {
        buf = pages->block->host + pages->offset[i];
        multifd_qpl_prepare_comp_job(job, buf, zbuf, size);
        if (qpl_execute_job(job) == QPL_STS_OK) {
            multifd_qpl_fill_packet(i, p, zbuf, job->total_out);
        } else {
            /* send the uncompressed page */
            multifd_qpl_fill_packet(i, p, buf, size);
        }
        zbuf += size;
    }
}

/**
 * multifd_qpl_compress_pages: compress pages
 *
 * Submit the pages to the IAA hardware for compression. If hardware
 * compression fails, it falls back to software compression. If software
 * compression also fails, the uncompressed page is sent.
 *
 * @p: Params for the channel being used
 */
static void multifd_qpl_compress_pages(MultiFDSendParams *p)
{
    QplData *qpl = p->compress_data;
    MultiFDPages_t *pages = &p->data->u.ram;
    uint32_t size = multifd_ram_page_size();
    QplHwJob *hw_job;
    uint8_t *buf;
    uint8_t *zbuf;

    for (int i = 0; i < pages->normal_num; i++) {
        buf = pages->block->host + pages->offset[i];
        zbuf = qpl->zbuf + (size * i);
        hw_job = &qpl->hw_jobs[i];
        multifd_qpl_prepare_comp_job(hw_job->job, buf, zbuf, size);
        if (multifd_qpl_submit_job(hw_job->job)) {
            hw_job->fallback_sw_path = false;
        } else {
            /*
             * The IAA work queue is full, any immediate subsequent job
             * submission is likely to fail, sending the page via the QPL
             * software path at this point gives us a better chance of
             * finding the queue open for the next pages.
             */
            hw_job->fallback_sw_path = true;
            multifd_qpl_prepare_comp_job(qpl->sw_job, buf, zbuf, size);
            if (qpl_execute_job(qpl->sw_job) == QPL_STS_OK) {
                hw_job->sw_output = zbuf;
                hw_job->sw_output_len = qpl->sw_job->total_out;
            } else {
                hw_job->sw_output = buf;
                hw_job->sw_output_len = size;
            }
        }
    }

    for (int i = 0; i < pages->normal_num; i++) {
        buf = pages->block->host + pages->offset[i];
        zbuf = qpl->zbuf + (size * i);
        hw_job = &qpl->hw_jobs[i];
        if (hw_job->fallback_sw_path) {
            multifd_qpl_fill_packet(i, p, hw_job->sw_output,
                                    hw_job->sw_output_len);
            continue;
        }
        if (qpl_wait_job(hw_job->job) == QPL_STS_OK) {
            multifd_qpl_fill_packet(i, p, zbuf, hw_job->job->total_out);
        } else {
            /* send the uncompressed page */
            multifd_qpl_fill_packet(i, p, buf, size);
        }
    }
}

static int multifd_qpl_send_prepare(MultiFDSendParams *p, Error **errp)
{
    QplData *qpl = p->compress_data;
    MultiFDPages_t *pages = &p->data->u.ram;
    uint32_t len = 0;

    if (!multifd_send_prepare_common(p)) {
        goto out;
    }

    /* The first IOV is used to store the compressed page lengths */
    len = pages->normal_num * sizeof(uint32_t);
    multifd_qpl_fill_iov(p, (uint8_t *) qpl->zlen, len);
    if (qpl->hw_avail) {
        multifd_qpl_compress_pages(p);
    } else {
        multifd_qpl_compress_pages_slow_path(p);
    }

out:
    p->flags |= MULTIFD_FLAG_QPL;
    multifd_send_fill_packet(p);
    return 0;
}

static int multifd_qpl_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    QplData *qpl;
    uint32_t page_size = multifd_ram_page_size();
    uint32_t page_count = multifd_ram_page_count();

    qpl = multifd_qpl_init(page_count, page_size, errp);
    if (!qpl) {
        return -1;
    }
    p->compress_data = qpl;
    return 0;
}

static void multifd_qpl_recv_cleanup(MultiFDRecvParams *p)
{
    multifd_qpl_deinit(p->compress_data);
    p->compress_data = NULL;
}

/**
 * multifd_qpl_process_and_check_job: process and check a QPL job
 *
 * Process the job and check whether the job output length is the
 * same as the specified length
 *
 * Returns true if the job execution succeeded and the output length
 * is equal to the specified length, otherwise false.
 *
 * @job: pointer to the qpl_job structure
 * @is_hardware: indicates whether the job is a hardware job
 * @len: Specified output length
 * @errp: pointer to an error
 */
static bool multifd_qpl_process_and_check_job(qpl_job *job, bool is_hardware,
                                              uint32_t len, Error **errp)
{
    qpl_status status;

    status = (is_hardware ? qpl_wait_job(job) : qpl_execute_job(job));
    if (status != QPL_STS_OK) {
        error_setg(errp, "qpl job failed with error %d", status);
        return false;
    }
    if (job->total_out != len) {
        error_setg(errp, "qpl decompressed len %u, expected len %u",
                   job->total_out, len);
        return false;
    }
    return true;
}

/**
 * multifd_qpl_decompress_pages_slow_path: decompress pages using slow path
 *
 * Decompress the pages using software
 *
 * Returns 0 on success or -1 on error
 *
 * @p: Params for the channel being used
 * @errp: pointer to an error
 */
static int multifd_qpl_decompress_pages_slow_path(MultiFDRecvParams *p,
                                                  Error **errp)
{
    QplData *qpl = p->compress_data;
    uint32_t size = multifd_ram_page_size();
    qpl_job *job = qpl->sw_job;
    uint8_t *zbuf = qpl->zbuf;
    uint8_t *addr;
    uint32_t len;

    for (int i = 0; i < p->normal_num; i++) {
        len = qpl->zlen[i];
        addr = p->host + p->normal[i];
        /* the page is uncompressed, load it */
        if (len == size) {
            memcpy(addr, zbuf, size);
            zbuf += size;
            continue;
        }
        multifd_qpl_prepare_decomp_job(job, zbuf, len, addr, size);
        if (!multifd_qpl_process_and_check_job(job, false, size, errp)) {
            return -1;
        }
        zbuf += len;
    }
    return 0;
}

/**
 * multifd_qpl_decompress_pages: decompress pages
 *
 * Decompress the pages using the IAA hardware. If hardware
 * decompression fails, it falls back to software decompression.
 *
 * Returns 0 on success or -1 on error
 *
 * @p: Params for the channel being used
 * @errp: pointer to an error
 */
static int multifd_qpl_decompress_pages(MultiFDRecvParams *p, Error **errp)
{
    QplData *qpl = p->compress_data;
    uint32_t size = multifd_ram_page_size();
    uint8_t *zbuf = qpl->zbuf;
    uint8_t *addr;
    uint32_t len;
    qpl_job *job;

    for (int i = 0; i < p->normal_num; i++) {
        addr = p->host + p->normal[i];
        len = qpl->zlen[i];
        /* the page is uncompressed if received length equals the page size */
        if (len == size) {
            memcpy(addr, zbuf, size);
            zbuf += size;
            continue;
        }

        job = qpl->hw_jobs[i].job;
        multifd_qpl_prepare_decomp_job(job, zbuf, len, addr, size);
        if (multifd_qpl_submit_job(job)) {
            qpl->hw_jobs[i].fallback_sw_path = false;
        } else {
            /*
             * The IAA work queue is full, any immediate subsequent job
             * submission is likely to fail, sending the page via the QPL
             * software path at this point gives us a better chance of
             * finding the queue open for the next pages.
             */
            qpl->hw_jobs[i].fallback_sw_path = true;
            job = qpl->sw_job;
            multifd_qpl_prepare_decomp_job(job, zbuf, len, addr, size);
            if (!multifd_qpl_process_and_check_job(job, false, size, errp)) {
                return -1;
            }
        }
        zbuf += len;
    }

    for (int i = 0; i < p->normal_num; i++) {
        /* ignore pages that have already been processed */
        if (qpl->zlen[i] == size || qpl->hw_jobs[i].fallback_sw_path) {
            continue;
        }

        job = qpl->hw_jobs[i].job;
        if (!multifd_qpl_process_and_check_job(job, true, size, errp)) {
            return -1;
        }
    }
    return 0;
}
static int multifd_qpl_recv(MultiFDRecvParams *p, Error **errp)
{
    QplData *qpl = p->compress_data;
    uint32_t in_size = p->next_packet_size;
    uint32_t flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;
    uint32_t len = 0;
    uint32_t zbuf_len = 0;
    int ret;

    if (flags != MULTIFD_FLAG_QPL) {
        error_setg(errp, "multifd %u: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_QPL);
        return -1;
    }
    multifd_recv_zero_page_process(p);
    if (!p->normal_num) {
        assert(in_size == 0);
        return 0;
    }

    /* read compressed page lengths */
    len = p->normal_num * sizeof(uint32_t);
    assert(len < in_size);
    ret = qio_channel_read_all(p->c, (void *) qpl->zlen, len, errp);
    if (ret != 0) {
        return ret;
    }
    for (int i = 0; i < p->normal_num; i++) {
        qpl->zlen[i] = be32_to_cpu(qpl->zlen[i]);
        assert(qpl->zlen[i] <= multifd_ram_page_size());
        zbuf_len += qpl->zlen[i];
        ramblock_recv_bitmap_set_offset(p->block, p->normal[i]);
    }

    /* read compressed pages */
    assert(in_size == len + zbuf_len);
    ret = qio_channel_read_all(p->c, (void *) qpl->zbuf, zbuf_len, errp);
    if (ret != 0) {
        return ret;
    }

    if (qpl->hw_avail) {
        return multifd_qpl_decompress_pages(p, errp);
    }
    return multifd_qpl_decompress_pages_slow_path(p, errp);
}

static const MultiFDMethods multifd_qpl_ops = {
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
