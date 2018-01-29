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
#include "qapi/clone-visitor.h"
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

/* data structures */
struct TPMPassthruState {
    TPMBackend parent;

    TPMPassthroughOptions *options;
    const char *tpm_dev;
    int tpm_fd;
    bool tpm_executing;
    bool tpm_op_canceled;
    int cancel_fd;

    TPMVersion tpm_version;
};

typedef struct TPMPassthruState TPMPassthruState;

#define TPM_PASSTHROUGH_DEFAULT_DEVICE "/dev/tpm0"

/* functions */

static void tpm_passthrough_cancel_cmd(TPMBackend *tb);

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
static int tpm_passthrough_unix_tx_bufs(TPMPassthruState *tpm_pt,
                                        const uint8_t *in, uint32_t in_len,
                                        uint8_t *out, uint32_t out_len,
                                        bool *selftest_done)
{
    ssize_t ret;
    bool is_selftest;
    const struct tpm_resp_hdr *hdr;

    tpm_pt->tpm_op_canceled = false;
    tpm_pt->tpm_executing = true;
    *selftest_done = false;

    is_selftest = tpm_util_is_selftest(in, in_len);

    ret = qemu_write_full(tpm_pt->tpm_fd, in, in_len);
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
               be32_to_cpu(((struct tpm_resp_hdr *)out)->len) != ret) {
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
        tpm_util_write_fatal_error_response(out, out_len);
    }

    tpm_pt->tpm_executing = false;

    return ret;
}

static void tpm_passthrough_handle_request(TPMBackend *tb, TPMBackendCmd *cmd)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);
    TPMIfClass *tic = TPM_IF_GET_CLASS(tb->tpm_state);

    DPRINTF("tpm_passthrough: processing command %p\n", cmd);

    tpm_passthrough_unix_tx_bufs(tpm_pt, cmd->in, cmd->in_len,
                                 cmd->out, cmd->out_len, &cmd->selftest_done);

    tic->request_completed(TPM_IF(tb->tpm_state));
}

static void tpm_passthrough_reset(TPMBackend *tb)
{
    DPRINTF("tpm_passthrough: CALL TO TPM_RESET!\n");

    tpm_passthrough_cancel_cmd(tb);
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

static TPMVersion tpm_passthrough_get_tpm_version(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);

    return tpm_pt->tpm_version;
}

/*
 * Unless path or file descriptor set has been provided by user,
 * determine the sysfs cancel file following kernel documentation
 * in Documentation/ABI/stable/sysfs-class-tpm.
 * From /dev/tpm0 create /sys/class/tpm/tpm0/device/cancel
 * before 4.0: /sys/class/misc/tpm0/device/cancel
 */
static int tpm_passthrough_open_sysfs_cancel(TPMPassthruState *tpm_pt)
{
    int fd = -1;
    char *dev;
    char path[PATH_MAX];

    if (tpm_pt->options->cancel_path) {
        fd = qemu_open(tpm_pt->options->cancel_path, O_WRONLY);
        if (fd < 0) {
            error_report("tpm_passthrough: Could not open TPM cancel path: %s",
                         strerror(errno));
        }
        return fd;
    }

    dev = strrchr(tpm_pt->tpm_dev, '/');
    if (!dev) {
        error_report("tpm_passthrough: Bad TPM device path %s",
                     tpm_pt->tpm_dev);
        return -1;
    }

    dev++;
    if (snprintf(path, sizeof(path), "/sys/class/tpm/%s/device/cancel",
                 dev) < sizeof(path)) {
        fd = qemu_open(path, O_WRONLY);
        if (fd < 0) {
            if (snprintf(path, sizeof(path), "/sys/class/misc/%s/device/cancel",
                         dev) < sizeof(path)) {
                fd = qemu_open(path, O_WRONLY);
            }
        }
    }

    if (fd < 0) {
        error_report("tpm_passthrough: Could not guess TPM cancel path");
    } else {
        tpm_pt->options->cancel_path = g_strdup(path);
    }

    return fd;
}

static int tpm_passthrough_handle_device_opts(QemuOpts *opts, TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);
    const char *value;

    value = qemu_opt_get(opts, "cancel-path");
    if (value) {
        tpm_pt->options->cancel_path = g_strdup(value);
        tpm_pt->options->has_cancel_path = true;
    }

    value = qemu_opt_get(opts, "path");
    if (value) {
        tpm_pt->options->has_path = true;
        tpm_pt->options->path = g_strdup(value);
    }

    tpm_pt->tpm_dev = value ? value : TPM_PASSTHROUGH_DEFAULT_DEVICE;
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
    qapi_free_TPMPassthroughOptions(tpm_pt->options);
    tpm_pt->options = NULL;
    tpm_pt->tpm_dev = NULL;

    return 1;
}

static TPMBackend *tpm_passthrough_create(QemuOpts *opts, const char *id)
{
    Object *obj = object_new(TYPE_TPM_PASSTHROUGH);
    TPMBackend *tb = TPM_BACKEND(obj);
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(tb);

    tb->id = g_strdup(id);

    if (tpm_passthrough_handle_device_opts(opts, tb)) {
        goto err_exit;
    }

    tpm_pt->cancel_fd = tpm_passthrough_open_sysfs_cancel(tpm_pt);
    if (tpm_pt->cancel_fd < 0) {
        goto err_exit;
    }

    return tb;

err_exit:
    object_unref(obj);

    return NULL;
}

static TpmTypeOptions *tpm_passthrough_get_tpm_options(TPMBackend *tb)
{
    TpmTypeOptions *options = g_new0(TpmTypeOptions, 1);

    options->type = TPM_TYPE_OPTIONS_KIND_PASSTHROUGH;
    options->u.passthrough.data = QAPI_CLONE(TPMPassthroughOptions,
                                             TPM_PASSTHROUGH(tb)->options);

    return options;
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

static void tpm_passthrough_inst_init(Object *obj)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(obj);

    tpm_pt->options = g_new0(TPMPassthroughOptions, 1);
    tpm_pt->tpm_fd = -1;
    tpm_pt->cancel_fd = -1;
}

static void tpm_passthrough_inst_finalize(Object *obj)
{
    TPMPassthruState *tpm_pt = TPM_PASSTHROUGH(obj);

    tpm_passthrough_cancel_cmd(TPM_BACKEND(obj));

    qemu_close(tpm_pt->tpm_fd);
    qemu_close(tpm_pt->cancel_fd);
    qapi_free_TPMPassthroughOptions(tpm_pt->options);
}

static void tpm_passthrough_class_init(ObjectClass *klass, void *data)
{
    TPMBackendClass *tbc = TPM_BACKEND_CLASS(klass);

    tbc->type = TPM_TYPE_PASSTHROUGH;
    tbc->opts = tpm_passthrough_cmdline_opts;
    tbc->desc = "Passthrough TPM backend driver";
    tbc->create = tpm_passthrough_create;
    tbc->reset = tpm_passthrough_reset;
    tbc->cancel_cmd = tpm_passthrough_cancel_cmd;
    tbc->get_tpm_established_flag = tpm_passthrough_get_tpm_established_flag;
    tbc->reset_tpm_established_flag =
        tpm_passthrough_reset_tpm_established_flag;
    tbc->get_tpm_version = tpm_passthrough_get_tpm_version;
    tbc->get_tpm_options = tpm_passthrough_get_tpm_options;
    tbc->handle_request = tpm_passthrough_handle_request;
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
}

type_init(tpm_passthrough_register)
