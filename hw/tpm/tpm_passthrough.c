/*
 *  passthrough TPM driver
 *
 *  Copyright (c) 2010 - 2013 IBM Corporation
 *  Authors:
 *    Stefan Berger <stefanb@us.ibm.com>
 *
 *  Copyright (C) 2011 IAIK, Graz University of Technology
 *    Author: Andreas Niederl
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "sysemu/tpm_backend.h"
#include "tpm_int.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "sysemu/tpm_backend_int.h"
#include "tpm_tis.h"
#include "tpm_util.h"

#define DEBUG_TPM 0

#define DPRINTF(fmt, ...) do { \
    if (DEBUG_TPM) { \
        fprintf(stderr, fmt, ## __VA_ARGS__); \
    } \
} while (0);

#define TYPE_TPM_PASSTHROUGH "tpm-passthrough"
#define TPM_PASSTHROUGH(obj) \
    OBJECT_CHECK(TPMPassthruState, (obj), TYPE_TPM_PASSTHROUGH)

static const TPMDriverOps tpm_passthrough_driver;

/* data structures */
typedef struct TPMPassthruThreadParams {
    TPMState *tpm_state;

    TPMRecvDataCB *recv_data_callback;
    TPMBackend *tb;
} TPMPassthruThreadParams;

struct TPMPassthruState {
    TPMBackend parent;

    TPMBackendThread tbt;

    TPMPassthruThreadParams tpm_thread_params;

    char *tpm_dev;
    int tpm_fd;
    bool tpm_executing;
    bool tpm_op_canceled;
    int cancel_fd;
    bool had_startup_error;

    TPMVersion tpm_version;
};

typedef struct TPMPassthruState TPMPassthruState;

#define TPM_PASSTHROUGH_DEFAULT_DEVICE "/dev/tpm0"

/* functions */

static void tpm_passthrough_cancel_cmd(TPMBackend *tb);

static int tpm_passthrough_unix_write(int fd, const uint8_t *buf, uint32_t len)
{
    int ret, remain;

    remain = len;
    while (remain > 0) {
        ret = write(fd, buf, remain);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                return -1;
            }
        } else if (ret == 0) {
            break;
        } else {
            buf += ret;
            remain -= ret;
        }
    }
    return len - remain;
}

static int tpm_passthrough_unix_read(int fd, uint8_t *buf, uint32_t len)
{
    int ret;
 reread:
    ret = read(fd, buf, len);
    if (ret < 0) {
        if (errno != EINTR && errno != EAGAIN) {
            return -1;
        }
        goto reread;
    }
    return ret;
}

static uint32_t tpm_passthrough_get_size_from_buffer(const uint8_t *buf)
{
    struct tpm_resp_hdr *resp = (struct tpm_resp_hdr *)buf;

    return be32_to_cpu(resp->len);
}

/*
 * Write an error message in the given output buffer.
 */
static void tpm_write_fatal_error_response(uint8_t *out, uint32_t out_len)
{
    if (out_len >= sizeof(struct tpm_resp_hdr)) {
        struct tpm_resp_hdr *resp = (struct tpm_resp_hdr *)out;

        resp->tag = cpu_to_be16(TPM_TAG_RSP_COMMAND);
        resp->len = cpu_to_be32(sizeof(struct tpm_resp_hdr));
        resp->errcode = cpu_to_be32(TPM_FAIL);
    }
}

static bool tpm_passthrough_is_selftest(const uint8_t *in, uint32_t in_len)
{
    struct tpm_req_hdr *hdr = (struct tpm_req_hdr *)in;

    if (in_len >= sizeof(*hdr)) {
        return (be32_to_cpu(hdr->ordinal) == TPM_ORD_ContinueSelfTest);
    }

    return false;
}

static int tpm_passthrough_unix_tx_bufs(TPMPassthruState *tpm_pt,
                                        const uint8_t *in, uint32_t in_len,
                                        uint8_t *out, uint32_t out_len,
                                        bool *selftest_done)
{
    int ret;
    bool is_selftest;
    const struct tpm_resp_hdr *hdr;

    tpm_pt->tpm_op_canceled = false;
    tpm_pt->tpm_executing = true;
    *selftest_done = false;

    is_selftest = tpm_passthrough_is_selftest(in, in_len);

    ret = tpm_passthrough_unix_write(tpm_pt->tpm_fd, in, in_len);
    if (ret != in_len) {
        if (!tpm_pt->tpm_op_canceled || errno != ECANCELED) {
            error_report("tpm_passthrough: error while transmitting data "
                         "to TPM: %s (%i)",
                         strerror(errno), errno);
        }
        goto err_exit;
    }

    tpm_pt->tpm_executing = false;

    ret = tpm_passthrough_unix_read(tpm_pt->tpm_fd, out, out_len);
    if (ret < 0) {
        if (!tpm_pt->tpm_op_canceled || errno != ECANCELED) {
            error_report("tpm_passthrough: error while reading data from "
                         "TPM: %s (%i)",
                         strerror(errno), errno);
        }
    } else if (ret < sizeof(struct tpm_resp_hdr) ||
               tpm_passthrough_get_size_from_buffer(out) != ret) {
        ret = -1;
        error_report("tpm_passthrough: received invalid response "
                     "packet from TPM");
    }

    if (is_selftest && (ret >= sizeof(struct tpm_resp_hdr))) {
        hdr = (struct tpm_resp_hdr *)out;
        *selftest_done = (be32_to_cpu(hdr->errcode) == 0);
    }

err_exit:
    if (ret < 0) {
        tpm_write_fatal_error_response(out, out_len);
    }

    tpm_pt->tpm_executing = false;

    return ret;
}

static int tpm_passthrough_unix_transfer(TPMPassthruState *tpm_pt,
                                         const TPMLocality *locty_data,
                                         bool *selftest_done)
{
    return tpm_passthrough_unix_tx_bufs(tpm_pt,
                                        locty_data->w_buffer.buffer,
                                        locty_data->w_offset,
                                        locty_data->r_buffer.buffer,
                                        locty_data->r_buffer.size,
                                        selftest_done);
}

static void tpm_passthrough_worker_thread(gpointer data,
                                          gpointer user_data)
{
    TPMPassthruThreadParams *thr_parms = user_data;
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(thr_parms->tb);
    TPMBackendCmd cmd = (TPMBackendCmd)data;
    bool selftest_done = false;

    DPRINTF("tpm_passthrough: processing command type %d\n", cmd);

    switch (cmd) {
    case TPM_BACKEND_CMD_PROCESS_CMD:
        tpm_passthrough_unix_transfer(tpm_pt,
                                      thr_parms->tpm_state->locty_data,
                                      &selftest_done);

        thr_parms->recv_data_callback(thr_parms->tpm_state,
                                      thr_parms->tpm_state->locty_number,
                                      selftest_done);
        break;
    case TPM_BACKEND_CMD_INIT:
    case TPM_BACKEND_CMD_END:
    case TPM_BACKEND_CMD_TPM_RESET:
        /* nothing to do */
        break;
    }
}

/*
 * Start the TPM (thread). If it had been started before, then terminate
 * and start it again.
 */
static int tpm_passthrough_startup_tpm(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);

    /* terminate a running TPM */
    tpm_backend_thread_end(&tpm_pt->tbt);

    tpm_backend_thread_create(&tpm_pt->tbt,
                              tpm_passthrough_worker_thread,
                              &tpm_pt->tpm_thread_params);

    return 0;
}

static void tpm_passthrough_reset(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);

    DPRINTF("tpm_passthrough: CALL TO TPM_RESET!\n");

    tpm_passthrough_cancel_cmd(tb);

    tpm_backend_thread_end(&tpm_pt->tbt);

    tpm_pt->had_startup_error = false;
}

static int tpm_passthrough_init(TPMBackend *tb, TPMState *s,
                                TPMRecvDataCB *recv_data_cb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);

    tpm_pt->tpm_thread_params.tpm_state = s;
    tpm_pt->tpm_thread_params.recv_data_callback = recv_data_cb;
    tpm_pt->tpm_thread_params.tb = tb;

    return 0;
}

static bool tpm_passthrough_get_tpm_established_flag(TPMBackend *tb)
{
    return false;
}

static int tpm_passthrough_reset_tpm_established_flag(TPMBackend *tb,
                                                      uint8_t locty)
{
    /* only a TPM 2.0 will support this */
    return 0;
}

static bool tpm_passthrough_get_startup_error(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);

    return tpm_pt->had_startup_error;
}

static size_t tpm_passthrough_realloc_buffer(TPMSizedBuffer *sb)
{
    size_t wanted_size = 4096; /* Linux tpm.c buffer size */

    if (sb->size != wanted_size) {
        sb->buffer = g_realloc(sb->buffer, wanted_size);
        sb->size = wanted_size;
    }
    return sb->size;
}

static void tpm_passthrough_deliver_request(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);

    tpm_backend_thread_deliver_request(&tpm_pt->tbt);
}

static void tpm_passthrough_cancel_cmd(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);
    int n;

    /*
     * As of Linux 3.7 the tpm_tis driver does not properly cancel
     * commands on all TPM manufacturers' TPMs.
     * Only cancel if we're busy so we don't cancel someone else's
     * command, e.g., a command executed on the host.
     */
    if (tpm_pt->tpm_executing) {
        if (tpm_pt->cancel_fd >= 0) {
            n = write(tpm_pt->cancel_fd, "-", 1);
            if (n != 1) {
                error_report("Canceling TPM command failed: %s",
                             strerror(errno));
            } else {
                tpm_pt->tpm_op_canceled = true;
            }
        } else {
            error_report("Cannot cancel TPM command due to missing "
                         "TPM sysfs cancel entry");
        }
    }
}

static const char *tpm_passthrough_create_desc(void)
{
    return "Passthrough TPM backend driver";
}

static TPMVersion tpm_passthrough_get_tpm_version(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);

    return tpm_pt->tpm_version;
}

/*
 * Unless path or file descriptor set has been provided by user,
 * determine the sysfs cancel file following kernel documentation
 * in Documentation/ABI/stable/sysfs-class-tpm.
 * From /dev/tpm0 create /sys/class/misc/tpm0/device/cancel
 */
static int tpm_passthrough_open_sysfs_cancel(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);
    int fd = -1;
    char *dev;
    char path[PATH_MAX];

    if (tb->cancel_path) {
        fd = qemu_open(tb->cancel_path, O_WRONLY);
        if (fd < 0) {
            error_report("Could not open TPM cancel path : %s",
                         strerror(errno));
        }
        return fd;
    }

    dev = strrchr(tpm_pt->tpm_dev, '/');
    if (dev) {
        dev++;
        if (snprintf(path, sizeof(path), "/sys/class/misc/%s/device/cancel",
                     dev) < sizeof(path)) {
            fd = qemu_open(path, O_WRONLY);
            if (fd >= 0) {
                tb->cancel_path = g_strdup(path);
            } else {
                error_report("tpm_passthrough: Could not open TPM cancel "
                             "path %s : %s", path, strerror(errno));
            }
        }
    } else {
       error_report("tpm_passthrough: Bad TPM device path %s",
                    tpm_pt->tpm_dev);
    }

    return fd;
}

static int tpm_passthrough_handle_device_opts(QemuOpts *opts, TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);
    const char *value;

    value = qemu_opt_get(opts, "cancel-path");
    tb->cancel_path = g_strdup(value);

    value = qemu_opt_get(opts, "path");
    if (!value) {
        value = TPM_PASSTHROUGH_DEFAULT_DEVICE;
    }

    tpm_pt->tpm_dev = g_strdup(value);

    tb->path = g_strdup(tpm_pt->tpm_dev);

    tpm_pt->tpm_fd = qemu_open(tpm_pt->tpm_dev, O_RDWR);
    if (tpm_pt->tpm_fd < 0) {
        error_report("Cannot access TPM device using '%s': %s",
                     tpm_pt->tpm_dev, strerror(errno));
        goto err_free_parameters;
    }

    if (tpm_util_test_tpmdev(tpm_pt->tpm_fd, &tpm_pt->tpm_version)) {
        error_report("'%s' is not a TPM device.",
                     tpm_pt->tpm_dev);
        goto err_close_tpmdev;
    }

    return 0;

 err_close_tpmdev:
    qemu_close(tpm_pt->tpm_fd);
    tpm_pt->tpm_fd = -1;

 err_free_parameters:
    g_free(tb->path);
    tb->path = NULL;

    g_free(tpm_pt->tpm_dev);
    tpm_pt->tpm_dev = NULL;

    return 1;
}

static TPMBackend *tpm_passthrough_create(QemuOpts *opts, const char *id)
{
    Object *obj = object_new(TYPE_TPM_PASSTHROUGH);
    TPMBackend *tb = TPM_BACKEND(obj);
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);

    tb->id = g_strdup(id);
    /* let frontend set the fe_model to proper value */
    tb->fe_model = -1;

    tb->ops = &tpm_passthrough_driver;

    if (tpm_passthrough_handle_device_opts(opts, tb)) {
        goto err_exit;
    }

    tpm_pt->cancel_fd = tpm_passthrough_open_sysfs_cancel(tb);
    if (tpm_pt->cancel_fd < 0) {
        goto err_exit;
    }

    return tb;

err_exit:
    g_free(tb->id);

    return NULL;
}

static void tpm_passthrough_destroy(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);

    tpm_passthrough_cancel_cmd(tb);

    tpm_backend_thread_end(&tpm_pt->tbt);

    qemu_close(tpm_pt->tpm_fd);
    qemu_close(tpm_pt->cancel_fd);

    g_free(tb->id);
    g_free(tb->path);
    g_free(tb->cancel_path);
    g_free(tpm_pt->tpm_dev);
}

static const QemuOptDesc tpm_passthrough_cmdline_opts[] = {
    TPM_STANDARD_CMDLINE_OPTS,
    {
        .name = "cancel-path",
        .type = QEMU_OPT_STRING,
        .help = "Sysfs file entry for canceling TPM commands",
    },
    {
        .name = "path",
        .type = QEMU_OPT_STRING,
        .help = "Path to TPM device on the host",
    },
    { /* end of list */ },
};

static const TPMDriverOps tpm_passthrough_driver = {
    .type                     = TPM_TYPE_PASSTHROUGH,
    .opts                     = tpm_passthrough_cmdline_opts,
    .desc                     = tpm_passthrough_create_desc,
    .create                   = tpm_passthrough_create,
    .destroy                  = tpm_passthrough_destroy,
    .init                     = tpm_passthrough_init,
    .startup_tpm              = tpm_passthrough_startup_tpm,
    .realloc_buffer           = tpm_passthrough_realloc_buffer,
    .reset                    = tpm_passthrough_reset,
    .had_startup_error        = tpm_passthrough_get_startup_error,
    .deliver_request          = tpm_passthrough_deliver_request,
    .cancel_cmd               = tpm_passthrough_cancel_cmd,
    .get_tpm_established_flag = tpm_passthrough_get_tpm_established_flag,
    .reset_tpm_established_flag = tpm_passthrough_reset_tpm_established_flag,
    .get_tpm_version          = tpm_passthrough_get_tpm_version,
};

static void tpm_passthrough_inst_init(Object *obj)
{
}

static void tpm_passthrough_inst_finalize(Object *obj)
{
}

static void tpm_passthrough_class_init(ObjectClass *klass, void *data)
{
    TPMBackendClass *tbc = TPM_BACKEND_CLASS(klass);

    tbc->ops = &tpm_passthrough_driver;
}

static const TypeInfo tpm_passthrough_info = {
    .name = TYPE_TPM_PASSTHROUGH,
    .parent = TYPE_TPM_BACKEND,
    .instance_size = sizeof(TPMPassthruState),
    .class_init = tpm_passthrough_class_init,
    .instance_init = tpm_passthrough_inst_init,
    .instance_finalize = tpm_passthrough_inst_finalize,
};

static void tpm_passthrough_register(void)
{
    type_register_static(&tpm_passthrough_info);
    tpm_register_driver(&tpm_passthrough_driver);
}

type_init(tpm_passthrough_register)
