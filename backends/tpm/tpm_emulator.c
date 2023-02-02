/*
 *  Emulator TPM driver
 *
 *  Copyright (c) 2017 Intel Corporation
 *  Author: Amarnath Valluri <amarnath.valluri@intel.com>
 *
 *  Copyright (c) 2010 - 2013, 2018 IBM Corporation
 *  Authors:
 *    Stefan Berger <stefanb@us.ibm.com>
 *
 *  Copyright (C) 2011 IAIK, Graz University of Technology
 *    Author: Andreas Niederl
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qemu/lockable.h"
#include "io/channel-socket.h"
#include "sysemu/runstate.h"
#include "sysemu/tpm_backend.h"
#include "sysemu/tpm_util.h"
#include "tpm_int.h"
#include "tpm_ioctl.h"
#include "migration/blocker.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-tpm.h"
#include "chardev/char-fe.h"
#include "trace.h"
#include "qom/object.h"

#define TYPE_TPM_EMULATOR "tpm-emulator"
OBJECT_DECLARE_SIMPLE_TYPE(TPMEmulator, TPM_EMULATOR)

#define TPM_EMULATOR_IMPLEMENTS_ALL_CAPS(S, cap) (((S)->caps & (cap)) == (cap))

/* data structures */

/* blobs from the TPM; part of VM state when migrating */
typedef struct TPMBlobBuffers {
    uint32_t permanent_flags;
    TPMSizedBuffer permanent;

    uint32_t volatil_flags;
    TPMSizedBuffer volatil;

    uint32_t savestate_flags;
    TPMSizedBuffer savestate;
} TPMBlobBuffers;

struct TPMEmulator {
    TPMBackend parent;

    TPMEmulatorOptions *options;
    CharBackend ctrl_chr;
    QIOChannel *data_ioc;
    TPMVersion tpm_version;
    ptm_cap caps; /* capabilities of the TPM */
    uint8_t cur_locty_number; /* last set locality */
    Error *migration_blocker;

    QemuMutex mutex;

    unsigned int established_flag:1;
    unsigned int established_flag_cached:1;

    TPMBlobBuffers state_blobs;

    bool relock_storage;
    VMChangeStateEntry *vmstate;
};

struct tpm_error {
    uint32_t tpm_result;
    const char *string;
};

static const struct tpm_error tpm_errors[] = {
    /* TPM 1.2 error codes */
    { TPM_BAD_PARAMETER   , "a parameter is bad" },
    { TPM_FAIL            , "operation failed" },
    { TPM_KEYNOTFOUND     , "key could not be found" },
    { TPM_BAD_PARAM_SIZE  , "bad parameter size"},
    { TPM_ENCRYPT_ERROR   , "encryption error" },
    { TPM_DECRYPT_ERROR   , "decryption error" },
    { TPM_BAD_KEY_PROPERTY, "bad key property" },
    { TPM_BAD_MODE        , "bad (encryption) mode" },
    { TPM_BAD_VERSION     , "bad version identifier" },
    { TPM_BAD_LOCALITY    , "bad locality" },
    /* TPM 2 error codes */
    { TPM_RC_FAILURE     , "operation failed" },
    { TPM_RC_LOCALITY    , "bad locality"     },
    { TPM_RC_INSUFFICIENT, "insufficient amount of data" },
};

static const char *tpm_emulator_strerror(uint32_t tpm_result)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(tpm_errors); i++) {
        if (tpm_errors[i].tpm_result == tpm_result) {
            return tpm_errors[i].string;
        }
    }
    return "";
}

static int tpm_emulator_ctrlcmd(TPMEmulator *tpm, unsigned long cmd, void *msg,
                                size_t msg_len_in, size_t msg_len_out)
{
    CharBackend *dev = &tpm->ctrl_chr;
    uint32_t cmd_no = cpu_to_be32(cmd);
    ssize_t n = sizeof(uint32_t) + msg_len_in;
    uint8_t *buf = NULL;

    WITH_QEMU_LOCK_GUARD(&tpm->mutex) {
        buf = g_alloca(n);
        memcpy(buf, &cmd_no, sizeof(cmd_no));
        memcpy(buf + sizeof(cmd_no), msg, msg_len_in);

        n = qemu_chr_fe_write_all(dev, buf, n);
        if (n <= 0) {
            return -1;
        }

        if (msg_len_out != 0) {
            n = qemu_chr_fe_read_all(dev, msg, msg_len_out);
            if (n <= 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int tpm_emulator_unix_tx_bufs(TPMEmulator *tpm_emu,
                                     const uint8_t *in, uint32_t in_len,
                                     uint8_t *out, uint32_t out_len,
                                     bool *selftest_done,
                                     Error **errp)
{
    ssize_t ret;
    bool is_selftest = false;

    if (selftest_done) {
        *selftest_done = false;
        is_selftest = tpm_util_is_selftest(in, in_len);
    }

    ret = qio_channel_write_all(tpm_emu->data_ioc, (char *)in, in_len, errp);
    if (ret != 0) {
        return -1;
    }

    ret = qio_channel_read_all(tpm_emu->data_ioc, (char *)out,
              sizeof(struct tpm_resp_hdr), errp);
    if (ret != 0) {
        return -1;
    }

    ret = qio_channel_read_all(tpm_emu->data_ioc,
              (char *)out + sizeof(struct tpm_resp_hdr),
              tpm_cmd_get_size(out) - sizeof(struct tpm_resp_hdr), errp);
    if (ret != 0) {
        return -1;
    }

    if (is_selftest) {
        *selftest_done = tpm_cmd_get_errcode(out) == 0;
    }

    return 0;
}

static int tpm_emulator_set_locality(TPMEmulator *tpm_emu, uint8_t locty_number,
                                     Error **errp)
{
    ptm_loc loc;

    if (tpm_emu->cur_locty_number == locty_number) {
        return 0;
    }

    trace_tpm_emulator_set_locality(locty_number);

    memset(&loc, 0, sizeof(loc));
    loc.u.req.loc = locty_number;
    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_SET_LOCALITY, &loc,
                             sizeof(loc), sizeof(loc)) < 0) {
        error_setg(errp, "tpm-emulator: could not set locality : %s",
                   strerror(errno));
        return -1;
    }

    loc.u.resp.tpm_result = be32_to_cpu(loc.u.resp.tpm_result);
    if (loc.u.resp.tpm_result != 0) {
        error_setg(errp, "tpm-emulator: TPM result for set locality : 0x%x",
                   loc.u.resp.tpm_result);
        return -1;
    }

    tpm_emu->cur_locty_number = locty_number;

    return 0;
}

static void tpm_emulator_handle_request(TPMBackend *tb, TPMBackendCmd *cmd,
                                        Error **errp)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);

    trace_tpm_emulator_handle_request();

    if (tpm_emulator_set_locality(tpm_emu, cmd->locty, errp) < 0 ||
        tpm_emulator_unix_tx_bufs(tpm_emu, cmd->in, cmd->in_len,
                                  cmd->out, cmd->out_len,
                                  &cmd->selftest_done, errp) < 0) {
        tpm_util_write_fatal_error_response(cmd->out, cmd->out_len);
    }
}

static int tpm_emulator_probe_caps(TPMEmulator *tpm_emu)
{
    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_GET_CAPABILITY,
                             &tpm_emu->caps, 0, sizeof(tpm_emu->caps)) < 0) {
        error_report("tpm-emulator: probing failed : %s", strerror(errno));
        return -1;
    }

    tpm_emu->caps = be64_to_cpu(tpm_emu->caps);

    trace_tpm_emulator_probe_caps(tpm_emu->caps);

    return 0;
}

static int tpm_emulator_check_caps(TPMEmulator *tpm_emu)
{
    ptm_cap caps = 0;
    const char *tpm = NULL;

    /* check for min. required capabilities */
    switch (tpm_emu->tpm_version) {
    case TPM_VERSION_1_2:
        caps = PTM_CAP_INIT | PTM_CAP_SHUTDOWN | PTM_CAP_GET_TPMESTABLISHED |
               PTM_CAP_SET_LOCALITY | PTM_CAP_SET_DATAFD | PTM_CAP_STOP |
               PTM_CAP_SET_BUFFERSIZE;
        tpm = "1.2";
        break;
    case TPM_VERSION_2_0:
        caps = PTM_CAP_INIT | PTM_CAP_SHUTDOWN | PTM_CAP_GET_TPMESTABLISHED |
               PTM_CAP_SET_LOCALITY | PTM_CAP_RESET_TPMESTABLISHED |
               PTM_CAP_SET_DATAFD | PTM_CAP_STOP | PTM_CAP_SET_BUFFERSIZE;
        tpm = "2";
        break;
    case TPM_VERSION_UNSPEC:
        error_report("tpm-emulator: TPM version has not been set");
        return -1;
    }

    if (!TPM_EMULATOR_IMPLEMENTS_ALL_CAPS(tpm_emu, caps)) {
        error_report("tpm-emulator: TPM does not implement minimum set of "
                     "required capabilities for TPM %s (0x%x)", tpm, (int)caps);
        return -1;
    }

    return 0;
}

static int tpm_emulator_stop_tpm(TPMBackend *tb)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);
    ptm_res res;

    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_STOP, &res, 0, sizeof(res)) < 0) {
        error_report("tpm-emulator: Could not stop TPM: %s",
                     strerror(errno));
        return -1;
    }

    res = be32_to_cpu(res);
    if (res) {
        error_report("tpm-emulator: TPM result for CMD_STOP: 0x%x %s", res,
                     tpm_emulator_strerror(res));
        return -1;
    }

    return 0;
}

static int tpm_emulator_lock_storage(TPMEmulator *tpm_emu)
{
    ptm_lockstorage pls;

    if (!TPM_EMULATOR_IMPLEMENTS_ALL_CAPS(tpm_emu, PTM_CAP_LOCK_STORAGE)) {
        trace_tpm_emulator_lock_storage_cmd_not_supt();
        return 0;
    }

    /* give failing side 300 * 10ms time to release lock */
    pls.u.req.retries = cpu_to_be32(300);
    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_LOCK_STORAGE, &pls,
                             sizeof(pls.u.req), sizeof(pls.u.resp)) < 0) {
        error_report("tpm-emulator: Could not lock storage within 3 seconds: "
                     "%s", strerror(errno));
        return -1;
    }

    pls.u.resp.tpm_result = be32_to_cpu(pls.u.resp.tpm_result);
    if (pls.u.resp.tpm_result != 0) {
        error_report("tpm-emulator: TPM result for CMD_LOCK_STORAGE: 0x%x %s",
                     pls.u.resp.tpm_result,
                     tpm_emulator_strerror(pls.u.resp.tpm_result));
        return -1;
    }

    return 0;
}

static int tpm_emulator_set_buffer_size(TPMBackend *tb,
                                        size_t wanted_size,
                                        size_t *actual_size)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);
    ptm_setbuffersize psbs;

    if (tpm_emulator_stop_tpm(tb) < 0) {
        return -1;
    }

    psbs.u.req.buffersize = cpu_to_be32(wanted_size);

    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_SET_BUFFERSIZE, &psbs,
                             sizeof(psbs.u.req), sizeof(psbs.u.resp)) < 0) {
        error_report("tpm-emulator: Could not set buffer size: %s",
                     strerror(errno));
        return -1;
    }

    psbs.u.resp.tpm_result = be32_to_cpu(psbs.u.resp.tpm_result);
    if (psbs.u.resp.tpm_result != 0) {
        error_report("tpm-emulator: TPM result for set buffer size : 0x%x %s",
                     psbs.u.resp.tpm_result,
                     tpm_emulator_strerror(psbs.u.resp.tpm_result));
        return -1;
    }

    if (actual_size) {
        *actual_size = be32_to_cpu(psbs.u.resp.buffersize);
    }

    trace_tpm_emulator_set_buffer_size(
            be32_to_cpu(psbs.u.resp.buffersize),
            be32_to_cpu(psbs.u.resp.minsize),
            be32_to_cpu(psbs.u.resp.maxsize));

    return 0;
}

static int tpm_emulator_startup_tpm_resume(TPMBackend *tb, size_t buffersize,
                                     bool is_resume)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);
    ptm_init init = {
        .u.req.init_flags = 0,
    };
    ptm_res res;

    trace_tpm_emulator_startup_tpm_resume(is_resume, buffersize);

    if (buffersize != 0 &&
        tpm_emulator_set_buffer_size(tb, buffersize, NULL) < 0) {
        goto err_exit;
    }

    if (is_resume) {
        init.u.req.init_flags |= cpu_to_be32(PTM_INIT_FLAG_DELETE_VOLATILE);
    }

    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_INIT, &init, sizeof(init),
                             sizeof(init)) < 0) {
        error_report("tpm-emulator: could not send INIT: %s",
                     strerror(errno));
        goto err_exit;
    }

    res = be32_to_cpu(init.u.resp.tpm_result);
    if (res) {
        error_report("tpm-emulator: TPM result for CMD_INIT: 0x%x %s", res,
                     tpm_emulator_strerror(res));
        goto err_exit;
    }
    return 0;

err_exit:
    return -1;
}

static int tpm_emulator_startup_tpm(TPMBackend *tb, size_t buffersize)
{
    /* TPM startup will be done from post_load hook */
    if (runstate_check(RUN_STATE_INMIGRATE)) {
        if (buffersize != 0) {
            return tpm_emulator_set_buffer_size(tb, buffersize, NULL);
        }

        return 0;
    }

    return tpm_emulator_startup_tpm_resume(tb, buffersize, false);
}

static bool tpm_emulator_get_tpm_established_flag(TPMBackend *tb)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);
    ptm_est est;

    if (tpm_emu->established_flag_cached) {
        return tpm_emu->established_flag;
    }

    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_GET_TPMESTABLISHED, &est,
                             0, sizeof(est)) < 0) {
        error_report("tpm-emulator: Could not get the TPM established flag: %s",
                     strerror(errno));
        return false;
    }
    trace_tpm_emulator_get_tpm_established_flag(est.u.resp.bit);

    tpm_emu->established_flag_cached = 1;
    tpm_emu->established_flag = (est.u.resp.bit != 0);

    return tpm_emu->established_flag;
}

static int tpm_emulator_reset_tpm_established_flag(TPMBackend *tb,
                                                   uint8_t locty)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);
    ptm_reset_est reset_est;
    ptm_res res;

    /* only a TPM 2.0 will support this */
    if (tpm_emu->tpm_version != TPM_VERSION_2_0) {
        return 0;
    }

    reset_est.u.req.loc = tpm_emu->cur_locty_number;
    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_RESET_TPMESTABLISHED,
                             &reset_est, sizeof(reset_est),
                             sizeof(reset_est)) < 0) {
        error_report("tpm-emulator: Could not reset the establishment bit: %s",
                     strerror(errno));
        return -1;
    }

    res = be32_to_cpu(reset_est.u.resp.tpm_result);
    if (res) {
        error_report(
            "tpm-emulator: TPM result for rest established flag: 0x%x %s",
            res, tpm_emulator_strerror(res));
        return -1;
    }

    tpm_emu->established_flag_cached = 0;

    return 0;
}

static void tpm_emulator_cancel_cmd(TPMBackend *tb)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);
    ptm_res res;

    if (!TPM_EMULATOR_IMPLEMENTS_ALL_CAPS(tpm_emu, PTM_CAP_CANCEL_TPM_CMD)) {
        trace_tpm_emulator_cancel_cmd_not_supt();
        return;
    }

    /* FIXME: make the function non-blocking, or it may block a VCPU */
    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_CANCEL_TPM_CMD, &res, 0,
                             sizeof(res)) < 0) {
        error_report("tpm-emulator: Could not cancel command: %s",
                     strerror(errno));
    } else if (res != 0) {
        error_report("tpm-emulator: Failed to cancel TPM: 0x%x",
                     be32_to_cpu(res));
    }
}

static TPMVersion tpm_emulator_get_tpm_version(TPMBackend *tb)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);

    return tpm_emu->tpm_version;
}

static size_t tpm_emulator_get_buffer_size(TPMBackend *tb)
{
    size_t actual_size;

    if (tpm_emulator_set_buffer_size(tb, 0, &actual_size) < 0) {
        return 4096;
    }

    return actual_size;
}

static int tpm_emulator_block_migration(TPMEmulator *tpm_emu)
{
    Error *err = NULL;
    ptm_cap caps = PTM_CAP_GET_STATEBLOB | PTM_CAP_SET_STATEBLOB |
                   PTM_CAP_STOP;

    if (!TPM_EMULATOR_IMPLEMENTS_ALL_CAPS(tpm_emu, caps)) {
        error_setg(&tpm_emu->migration_blocker,
                   "Migration disabled: TPM emulator does not support "
                   "migration");
        if (migrate_add_blocker(tpm_emu->migration_blocker, &err) < 0) {
            error_report_err(err);
            error_free(tpm_emu->migration_blocker);
            tpm_emu->migration_blocker = NULL;

            return -1;
        }
    }

    return 0;
}

static int tpm_emulator_prepare_data_fd(TPMEmulator *tpm_emu)
{
    ptm_res res;
    Error *err = NULL;
    int fds[2] = { -1, -1 };

    if (qemu_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        error_report("tpm-emulator: Failed to create socketpair");
        return -1;
    }

    qemu_chr_fe_set_msgfds(&tpm_emu->ctrl_chr, fds + 1, 1);

    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_SET_DATAFD, &res, 0,
                             sizeof(res)) < 0 || res != 0) {
        error_report("tpm-emulator: Failed to send CMD_SET_DATAFD: %s",
                     strerror(errno));
        goto err_exit;
    }

    tpm_emu->data_ioc = QIO_CHANNEL(qio_channel_socket_new_fd(fds[0], &err));
    if (err) {
        error_prepend(&err, "tpm-emulator: Failed to create io channel: ");
        error_report_err(err);
        goto err_exit;
    }

    closesocket(fds[1]);

    return 0;

err_exit:
    closesocket(fds[0]);
    closesocket(fds[1]);
    return -1;
}

static int tpm_emulator_handle_device_opts(TPMEmulator *tpm_emu, QemuOpts *opts)
{
    const char *value;
    Error *err = NULL;
    Chardev *dev;

    value = qemu_opt_get(opts, "chardev");
    if (!value) {
        error_report("tpm-emulator: parameter 'chardev' is missing");
        goto err;
    }

    dev = qemu_chr_find(value);
    if (!dev) {
        error_report("tpm-emulator: tpm chardev '%s' not found", value);
        goto err;
    }

    if (!qemu_chr_fe_init(&tpm_emu->ctrl_chr, dev, &err)) {
        error_prepend(&err, "tpm-emulator: No valid chardev found at '%s':",
                      value);
        error_report_err(err);
        goto err;
    }

    tpm_emu->options->chardev = g_strdup(value);

    if (tpm_emulator_prepare_data_fd(tpm_emu) < 0) {
        goto err;
    }

    /* FIXME: tpm_util_test_tpmdev() accepts only on socket fd, as it also used
     * by passthrough driver, which not yet using GIOChannel.
     */
    if (tpm_util_test_tpmdev(QIO_CHANNEL_SOCKET(tpm_emu->data_ioc)->fd,
                             &tpm_emu->tpm_version)) {
        error_report("'%s' is not emulating TPM device. Error: %s",
                      tpm_emu->options->chardev, strerror(errno));
        goto err;
    }

    switch (tpm_emu->tpm_version) {
    case TPM_VERSION_1_2:
        trace_tpm_emulator_handle_device_opts_tpm12();
        break;
    case TPM_VERSION_2_0:
        trace_tpm_emulator_handle_device_opts_tpm2();
        break;
    default:
        trace_tpm_emulator_handle_device_opts_unspec();
    }

    if (tpm_emulator_probe_caps(tpm_emu) ||
        tpm_emulator_check_caps(tpm_emu)) {
        goto err;
    }

    return tpm_emulator_block_migration(tpm_emu);

err:
    trace_tpm_emulator_handle_device_opts_startup_error();

    return -1;
}

static TPMBackend *tpm_emulator_create(QemuOpts *opts)
{
    TPMBackend *tb = TPM_BACKEND(object_new(TYPE_TPM_EMULATOR));

    if (tpm_emulator_handle_device_opts(TPM_EMULATOR(tb), opts)) {
        object_unref(OBJECT(tb));
        return NULL;
    }

    return tb;
}

static TpmTypeOptions *tpm_emulator_get_tpm_options(TPMBackend *tb)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);
    TpmTypeOptions *options = g_new0(TpmTypeOptions, 1);

    options->type = TPM_TYPE_EMULATOR;
    options->u.emulator.data = QAPI_CLONE(TPMEmulatorOptions, tpm_emu->options);

    return options;
}

static const QemuOptDesc tpm_emulator_cmdline_opts[] = {
    TPM_STANDARD_CMDLINE_OPTS,
    {
        .name = "chardev",
        .type = QEMU_OPT_STRING,
        .help = "Character device to use for out-of-band control messages",
    },
    { /* end of list */ },
};

/*
 * Transfer a TPM state blob from the TPM into a provided buffer.
 *
 * @tpm_emu: TPMEmulator
 * @type: the type of blob to transfer
 * @tsb: the TPMSizeBuffer to fill with the blob
 * @flags: the flags to return to the caller
 */
static int tpm_emulator_get_state_blob(TPMEmulator *tpm_emu,
                                       uint8_t type,
                                       TPMSizedBuffer *tsb,
                                       uint32_t *flags)
{
    ptm_getstate pgs;
    ptm_res res;
    ssize_t n;
    uint32_t totlength, length;

    tpm_sized_buffer_reset(tsb);

    pgs.u.req.state_flags = cpu_to_be32(PTM_STATE_FLAG_DECRYPTED);
    pgs.u.req.type = cpu_to_be32(type);
    pgs.u.req.offset = 0;

    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_GET_STATEBLOB,
                             &pgs, sizeof(pgs.u.req),
                             offsetof(ptm_getstate, u.resp.data)) < 0) {
        error_report("tpm-emulator: could not get state blob type %d : %s",
                     type, strerror(errno));
        return -1;
    }

    res = be32_to_cpu(pgs.u.resp.tpm_result);
    if (res != 0 && (res & 0x800) == 0) {
        error_report("tpm-emulator: Getting the stateblob (type %d) failed "
                     "with a TPM error 0x%x %s", type, res,
                     tpm_emulator_strerror(res));
        return -1;
    }

    totlength = be32_to_cpu(pgs.u.resp.totlength);
    length = be32_to_cpu(pgs.u.resp.length);
    if (totlength != length) {
        error_report("tpm-emulator: Expecting to read %u bytes "
                     "but would get %u", totlength, length);
        return -1;
    }

    *flags = be32_to_cpu(pgs.u.resp.state_flags);

    if (totlength > 0) {
        tsb->buffer = g_try_malloc(totlength);
        if (!tsb->buffer) {
            error_report("tpm-emulator: Out of memory allocating %u bytes",
                         totlength);
            return -1;
        }

        n = qemu_chr_fe_read_all(&tpm_emu->ctrl_chr, tsb->buffer, totlength);
        if (n != totlength) {
            error_report("tpm-emulator: Could not read stateblob (type %d); "
                         "expected %u bytes, got %zd",
                         type, totlength, n);
            return -1;
        }
    }
    tsb->size = totlength;

    trace_tpm_emulator_get_state_blob(type, tsb->size, *flags);

    return 0;
}

static int tpm_emulator_get_state_blobs(TPMEmulator *tpm_emu)
{
    TPMBlobBuffers *state_blobs = &tpm_emu->state_blobs;

    if (tpm_emulator_get_state_blob(tpm_emu, PTM_BLOB_TYPE_PERMANENT,
                                    &state_blobs->permanent,
                                    &state_blobs->permanent_flags) < 0 ||
        tpm_emulator_get_state_blob(tpm_emu, PTM_BLOB_TYPE_VOLATILE,
                                    &state_blobs->volatil,
                                    &state_blobs->volatil_flags) < 0 ||
        tpm_emulator_get_state_blob(tpm_emu, PTM_BLOB_TYPE_SAVESTATE,
                                    &state_blobs->savestate,
                                    &state_blobs->savestate_flags) < 0) {
        goto err_exit;
    }

    return 0;

 err_exit:
    tpm_sized_buffer_reset(&state_blobs->volatil);
    tpm_sized_buffer_reset(&state_blobs->permanent);
    tpm_sized_buffer_reset(&state_blobs->savestate);

    return -1;
}

/*
 * Transfer a TPM state blob to the TPM emulator.
 *
 * @tpm_emu: TPMEmulator
 * @type: the type of TPM state blob to transfer
 * @tsb: TPMSizedBuffer containing the TPM state blob
 * @flags: Flags describing the (encryption) state of the TPM state blob
 */
static int tpm_emulator_set_state_blob(TPMEmulator *tpm_emu,
                                       uint32_t type,
                                       TPMSizedBuffer *tsb,
                                       uint32_t flags)
{
    ssize_t n;
    ptm_setstate pss;
    ptm_res tpm_result;

    if (tsb->size == 0) {
        return 0;
    }

    pss = (ptm_setstate) {
        .u.req.state_flags = cpu_to_be32(flags),
        .u.req.type = cpu_to_be32(type),
        .u.req.length = cpu_to_be32(tsb->size),
    };

    /* write the header only */
    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_SET_STATEBLOB, &pss,
                             offsetof(ptm_setstate, u.req.data), 0) < 0) {
        error_report("tpm-emulator: could not set state blob type %d : %s",
                     type, strerror(errno));
        return -1;
    }

    /* now the body */
    n = qemu_chr_fe_write_all(&tpm_emu->ctrl_chr, tsb->buffer, tsb->size);
    if (n != tsb->size) {
        error_report("tpm-emulator: Writing the stateblob (type %d) "
                     "failed; could not write %u bytes, but only %zd",
                     type, tsb->size, n);
        return -1;
    }

    /* now get the result */
    n = qemu_chr_fe_read_all(&tpm_emu->ctrl_chr,
                             (uint8_t *)&pss, sizeof(pss.u.resp));
    if (n != sizeof(pss.u.resp)) {
        error_report("tpm-emulator: Reading response from writing stateblob "
                     "(type %d) failed; expected %zu bytes, got %zd", type,
                     sizeof(pss.u.resp), n);
        return -1;
    }

    tpm_result = be32_to_cpu(pss.u.resp.tpm_result);
    if (tpm_result != 0) {
        error_report("tpm-emulator: Setting the stateblob (type %d) failed "
                     "with a TPM error 0x%x %s", type, tpm_result,
                     tpm_emulator_strerror(tpm_result));
        return -1;
    }

    trace_tpm_emulator_set_state_blob(type, tsb->size, flags);

    return 0;
}

/*
 * Set all the TPM state blobs.
 *
 * Returns a negative errno code in case of error.
 */
static int tpm_emulator_set_state_blobs(TPMBackend *tb)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);
    TPMBlobBuffers *state_blobs = &tpm_emu->state_blobs;

    trace_tpm_emulator_set_state_blobs();

    if (tpm_emulator_stop_tpm(tb) < 0) {
        trace_tpm_emulator_set_state_blobs_error("Could not stop TPM");
        return -EIO;
    }

    if (tpm_emulator_set_state_blob(tpm_emu, PTM_BLOB_TYPE_PERMANENT,
                                    &state_blobs->permanent,
                                    state_blobs->permanent_flags) < 0 ||
        tpm_emulator_set_state_blob(tpm_emu, PTM_BLOB_TYPE_VOLATILE,
                                    &state_blobs->volatil,
                                    state_blobs->volatil_flags) < 0 ||
        tpm_emulator_set_state_blob(tpm_emu, PTM_BLOB_TYPE_SAVESTATE,
                                    &state_blobs->savestate,
                                    state_blobs->savestate_flags) < 0) {
        return -EIO;
    }

    trace_tpm_emulator_set_state_blobs_done();

    return 0;
}

static int tpm_emulator_pre_save(void *opaque)
{
    TPMBackend *tb = opaque;
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);
    int ret;

    trace_tpm_emulator_pre_save();

    tpm_backend_finish_sync(tb);

    /* get the state blobs from the TPM */
    ret = tpm_emulator_get_state_blobs(tpm_emu);

    tpm_emu->relock_storage = ret == 0;

    return ret;
}

static void tpm_emulator_vm_state_change(void *opaque, bool running,
                                         RunState state)
{
    TPMBackend *tb = opaque;
    TPMEmulator *tpm_emu = TPM_EMULATOR(tb);

    trace_tpm_emulator_vm_state_change(running, state);

    if (!running || state != RUN_STATE_RUNNING || !tpm_emu->relock_storage) {
        return;
    }

    /* lock storage after migration fall-back */
    tpm_emulator_lock_storage(tpm_emu);
}

/*
 * Load the TPM state blobs into the TPM.
 *
 * Returns negative errno codes in case of error.
 */
static int tpm_emulator_post_load(void *opaque, int version_id)
{
    TPMBackend *tb = opaque;
    int ret;

    ret = tpm_emulator_set_state_blobs(tb);
    if (ret < 0) {
        return ret;
    }

    if (tpm_emulator_startup_tpm_resume(tb, 0, true) < 0) {
        return -EIO;
    }

    return 0;
}

static const VMStateDescription vmstate_tpm_emulator = {
    .name = "tpm-emulator",
    .version_id = 0,
    .pre_save = tpm_emulator_pre_save,
    .post_load = tpm_emulator_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(state_blobs.permanent_flags, TPMEmulator),
        VMSTATE_UINT32(state_blobs.permanent.size, TPMEmulator),
        VMSTATE_VBUFFER_ALLOC_UINT32(state_blobs.permanent.buffer,
                                     TPMEmulator, 0, 0,
                                     state_blobs.permanent.size),

        VMSTATE_UINT32(state_blobs.volatil_flags, TPMEmulator),
        VMSTATE_UINT32(state_blobs.volatil.size, TPMEmulator),
        VMSTATE_VBUFFER_ALLOC_UINT32(state_blobs.volatil.buffer,
                                     TPMEmulator, 0, 0,
                                     state_blobs.volatil.size),

        VMSTATE_UINT32(state_blobs.savestate_flags, TPMEmulator),
        VMSTATE_UINT32(state_blobs.savestate.size, TPMEmulator),
        VMSTATE_VBUFFER_ALLOC_UINT32(state_blobs.savestate.buffer,
                                     TPMEmulator, 0, 0,
                                     state_blobs.savestate.size),

        VMSTATE_END_OF_LIST()
    }
};

static void tpm_emulator_inst_init(Object *obj)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(obj);

    trace_tpm_emulator_inst_init();

    tpm_emu->options = g_new0(TPMEmulatorOptions, 1);
    tpm_emu->cur_locty_number = ~0;
    qemu_mutex_init(&tpm_emu->mutex);
    tpm_emu->vmstate =
        qemu_add_vm_change_state_handler(tpm_emulator_vm_state_change,
                                         tpm_emu);

    vmstate_register(NULL, VMSTATE_INSTANCE_ID_ANY,
                     &vmstate_tpm_emulator, obj);
}

/*
 * Gracefully shut down the external TPM
 */
static void tpm_emulator_shutdown(TPMEmulator *tpm_emu)
{
    ptm_res res;

    if (!tpm_emu->options->chardev) {
        /* was never properly initialized */
        return;
    }

    if (tpm_emulator_ctrlcmd(tpm_emu, CMD_SHUTDOWN, &res, 0, sizeof(res)) < 0) {
        error_report("tpm-emulator: Could not cleanly shutdown the TPM: %s",
                     strerror(errno));
    } else if (res != 0) {
        error_report("tpm-emulator: TPM result for shutdown: 0x%x %s",
                     be32_to_cpu(res), tpm_emulator_strerror(be32_to_cpu(res)));
    }
}

static void tpm_emulator_inst_finalize(Object *obj)
{
    TPMEmulator *tpm_emu = TPM_EMULATOR(obj);
    TPMBlobBuffers *state_blobs = &tpm_emu->state_blobs;

    tpm_emulator_shutdown(tpm_emu);

    object_unref(OBJECT(tpm_emu->data_ioc));

    qemu_chr_fe_deinit(&tpm_emu->ctrl_chr, false);

    qapi_free_TPMEmulatorOptions(tpm_emu->options);

    if (tpm_emu->migration_blocker) {
        migrate_del_blocker(tpm_emu->migration_blocker);
        error_free(tpm_emu->migration_blocker);
    }

    tpm_sized_buffer_reset(&state_blobs->volatil);
    tpm_sized_buffer_reset(&state_blobs->permanent);
    tpm_sized_buffer_reset(&state_blobs->savestate);

    qemu_mutex_destroy(&tpm_emu->mutex);
    qemu_del_vm_change_state_handler(tpm_emu->vmstate);

    vmstate_unregister(NULL, &vmstate_tpm_emulator, obj);
}

static void tpm_emulator_class_init(ObjectClass *klass, void *data)
{
    TPMBackendClass *tbc = TPM_BACKEND_CLASS(klass);

    tbc->type = TPM_TYPE_EMULATOR;
    tbc->opts = tpm_emulator_cmdline_opts;
    tbc->desc = "TPM emulator backend driver";
    tbc->create = tpm_emulator_create;
    tbc->startup_tpm = tpm_emulator_startup_tpm;
    tbc->cancel_cmd = tpm_emulator_cancel_cmd;
    tbc->get_tpm_established_flag = tpm_emulator_get_tpm_established_flag;
    tbc->reset_tpm_established_flag = tpm_emulator_reset_tpm_established_flag;
    tbc->get_tpm_version = tpm_emulator_get_tpm_version;
    tbc->get_buffer_size = tpm_emulator_get_buffer_size;
    tbc->get_tpm_options = tpm_emulator_get_tpm_options;

    tbc->handle_request = tpm_emulator_handle_request;
}

static const TypeInfo tpm_emulator_info = {
    .name = TYPE_TPM_EMULATOR,
    .parent = TYPE_TPM_BACKEND,
    .instance_size = sizeof(TPMEmulator),
    .class_init = tpm_emulator_class_init,
    .instance_init = tpm_emulator_inst_init,
    .instance_finalize = tpm_emulator_inst_finalize,
};

static void tpm_emulator_register(void)
{
    type_register_static(&tpm_emulator_info);
}

type_init(tpm_emulator_register)
