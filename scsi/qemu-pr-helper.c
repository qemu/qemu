/*
 * Privileged helper to handle persistent reservation commands for QEMU
 *
 * Copyright (C) 2017 Red Hat, Inc. <pbonzini@redhat.com>
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/dm-ioctl.h>
#include <scsi/sg.h>

#ifdef CONFIG_LIBCAP
#include <cap-ng.h>
#endif
#include <pwd.h>
#include <grp.h>

#ifdef CONFIG_MPATH
#include <libudev.h>
#include <mpath_cmd.h>
#include <mpath_persist.h>
#endif

#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/systemd.h"
#include "qapi/util.h"
#include "qapi/qmp/qstring.h"
#include "io/channel-socket.h"
#include "trace/control.h"
#include "qemu-version.h"

#include "block/aio.h"
#include "block/thread-pool.h"

#include "scsi/constants.h"
#include "scsi/utils.h"
#include "pr-helper.h"

#define PR_OUT_FIXED_PARAM_SIZE 24

static char *socket_path;
static char *pidfile;
static enum { RUNNING, TERMINATE, TERMINATING } state;
static QIOChannelSocket *server_ioc;
static int server_watch;
static int num_active_sockets = 1;
static int noisy;
static int verbose;

#ifdef CONFIG_LIBCAP
static int uid = -1;
static int gid = -1;
#endif

static void compute_default_paths(void)
{
    socket_path = qemu_get_local_state_pathname("run/qemu-pr-helper.sock");
    pidfile = qemu_get_local_state_pathname("run/qemu-pr-helper.pid");
}

static void usage(const char *name)
{
    (printf) (
"Usage: %s [OPTIONS] FILE\n"
"Persistent Reservation helper program for QEMU\n"
"\n"
"  -h, --help                display this help and exit\n"
"  -V, --version             output version information and exit\n"
"\n"
"  -d, --daemon              run in the background\n"
"  -f, --pidfile=PATH        PID file when running as a daemon\n"
"                            (default '%s')\n"
"  -k, --socket=PATH         path to the unix socket\n"
"                            (default '%s')\n"
"  -T, --trace [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
"                            specify tracing options\n"
#ifdef CONFIG_LIBCAP
"  -u, --user=USER           user to drop privileges to\n"
"  -g, --group=GROUP         group to drop privileges to\n"
#endif
"\n"
QEMU_HELP_BOTTOM "\n"
    , name, pidfile, socket_path);
}

static void version(const char *name)
{
    printf(
"%s " QEMU_FULL_VERSION "\n"
"Written by Paolo Bonzini.\n"
"\n"
QEMU_COPYRIGHT "\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
    , name);
}

/* SG_IO support */

typedef struct PRHelperSGIOData {
    int fd;
    const uint8_t *cdb;
    uint8_t *sense;
    uint8_t *buf;
    int sz;              /* input/output */
    int dir;
} PRHelperSGIOData;

static int do_sgio_worker(void *opaque)
{
    PRHelperSGIOData *data = opaque;
    struct sg_io_hdr io_hdr;
    int ret;
    int status;
    SCSISense sense_code;

    memset(data->sense, 0, PR_HELPER_SENSE_SIZE);
    memset(&io_hdr, 0, sizeof(io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = PR_HELPER_CDB_SIZE;
    io_hdr.cmdp = (uint8_t *)data->cdb;
    io_hdr.sbp = data->sense;
    io_hdr.mx_sb_len = PR_HELPER_SENSE_SIZE;
    io_hdr.timeout = 1;
    io_hdr.dxfer_direction = data->dir;
    io_hdr.dxferp = (char *)data->buf;
    io_hdr.dxfer_len = data->sz;
    ret = ioctl(data->fd, SG_IO, &io_hdr);
    status = sg_io_sense_from_errno(ret < 0 ? errno : 0, &io_hdr,
                                    &sense_code);
    if (status == GOOD) {
        data->sz -= io_hdr.resid;
    } else {
        data->sz = 0;
    }

    if (status == CHECK_CONDITION &&
        !(io_hdr.driver_status & SG_ERR_DRIVER_SENSE)) {
        scsi_build_sense(data->sense, sense_code);
    }

    return status;
}

static int do_sgio(int fd, const uint8_t *cdb, uint8_t *sense,
                    uint8_t *buf, int *sz, int dir)
{
    ThreadPool *pool = aio_get_thread_pool(qemu_get_aio_context());
    int r;

    PRHelperSGIOData data = {
        .fd = fd,
        .cdb = cdb,
        .sense = sense,
        .buf = buf,
        .sz = *sz,
        .dir = dir,
    };

    r = thread_pool_submit_co(pool, do_sgio_worker, &data);
    *sz = data.sz;
    return r;
}

/* Device mapper interface */

#ifdef CONFIG_MPATH
#define CONTROL_PATH "/dev/mapper/control"

typedef struct DMData {
    struct dm_ioctl dm;
    uint8_t data[1024];
} DMData;

static int control_fd;

static void *dm_ioctl(int ioc, struct dm_ioctl *dm)
{
    static DMData d;
    memcpy(&d.dm, dm, sizeof(d.dm));
    QEMU_BUILD_BUG_ON(sizeof(d.data) < sizeof(struct dm_target_spec));

    d.dm.version[0] = DM_VERSION_MAJOR;
    d.dm.version[1] = 0;
    d.dm.version[2] = 0;
    d.dm.data_size = 1024;
    d.dm.data_start = offsetof(DMData, data);
    if (ioctl(control_fd, ioc, &d) < 0) {
        return NULL;
    }
    memcpy(dm, &d.dm, sizeof(d.dm));
    return &d.data;
}

static void *dm_dev_ioctl(int fd, int ioc, struct dm_ioctl *dm)
{
    struct stat st;
    int r;

    r = fstat(fd, &st);
    if (r < 0) {
        perror("fstat");
        exit(1);
    }

    dm->dev = st.st_rdev;
    return dm_ioctl(ioc, dm);
}

static void dm_init(void)
{
    control_fd = open(CONTROL_PATH, O_RDWR);
    if (control_fd < 0) {
        perror("Cannot open " CONTROL_PATH);
        exit(1);
    }
    struct dm_ioctl dm = { };
    if (!dm_ioctl(DM_VERSION, &dm)) {
        perror("ioctl");
        exit(1);
    }
    if (dm.version[0] != DM_VERSION_MAJOR) {
        fprintf(stderr, "Unsupported device mapper interface");
        exit(1);
    }
}

/* Variables required by libmultipath and libmpathpersist.  */
QEMU_BUILD_BUG_ON(PR_HELPER_DATA_SIZE > MPATH_MAX_PARAM_LEN);
static struct config *multipath_conf;
unsigned mpath_mx_alloc_len = PR_HELPER_DATA_SIZE;
int logsink;
struct udev *udev;

extern struct config *get_multipath_config(void);
struct config *get_multipath_config(void)
{
    return multipath_conf;
}

extern void put_multipath_config(struct config *conf);
void put_multipath_config(struct config *conf)
{
}

static void multipath_pr_init(void)
{
    udev = udev_new();
#ifdef CONFIG_MPATH_NEW_API
    multipath_conf = mpath_lib_init();
#else
    mpath_lib_init(udev);
#endif
}

static int is_mpath(int fd)
{
    struct dm_ioctl dm = { .flags = DM_NOFLUSH_FLAG };
    struct dm_target_spec *tgt;

    tgt = dm_dev_ioctl(fd, DM_TABLE_STATUS, &dm);
    if (!tgt) {
        if (errno == ENXIO) {
            return 0;
        }
        perror("ioctl");
        exit(EXIT_FAILURE);
    }
    return !strncmp(tgt->target_type, "multipath", DM_MAX_TYPE_NAME);
}

static SCSISense mpath_generic_sense(int r)
{
    switch (r) {
    case MPATH_PR_SENSE_NOT_READY:
         return SENSE_CODE(NOT_READY);
    case MPATH_PR_SENSE_MEDIUM_ERROR:
         return SENSE_CODE(READ_ERROR);
    case MPATH_PR_SENSE_HARDWARE_ERROR:
         return SENSE_CODE(TARGET_FAILURE);
    case MPATH_PR_SENSE_ABORTED_COMMAND:
         return SENSE_CODE(IO_ERROR);
    default:
         abort();
    }
}

static int mpath_reconstruct_sense(int fd, int r, uint8_t *sense)
{
    switch (r) {
    case MPATH_PR_SUCCESS:
        return GOOD;
    case MPATH_PR_SENSE_NOT_READY:
    case MPATH_PR_SENSE_MEDIUM_ERROR:
    case MPATH_PR_SENSE_HARDWARE_ERROR:
    case MPATH_PR_SENSE_ABORTED_COMMAND:
        {
            /* libmpathpersist ate the exact sense.  Try to find it by
             * issuing TEST UNIT READY.
             */
            uint8_t cdb[6] = { TEST_UNIT_READY };
            int sz = 0;
            int r = do_sgio(fd, cdb, sense, NULL, &sz, SG_DXFER_NONE);

            if (r != GOOD) {
                return r;
            }
            scsi_build_sense(sense, mpath_generic_sense(r));
            return CHECK_CONDITION;
        }

    case MPATH_PR_SENSE_UNIT_ATTENTION:
        /* Congratulations libmpathpersist, you ruined the Unit Attention...
         * Return a heavyweight one.
         */
        scsi_build_sense(sense, SENSE_CODE(SCSI_BUS_RESET));
        return CHECK_CONDITION;
    case MPATH_PR_SENSE_INVALID_OP:
        /* Only one valid sense.  */
        scsi_build_sense(sense, SENSE_CODE(INVALID_OPCODE));
        return CHECK_CONDITION;
    case MPATH_PR_ILLEGAL_REQ:
        /* Guess.  */
        scsi_build_sense(sense, SENSE_CODE(INVALID_PARAM));
        return CHECK_CONDITION;
    case MPATH_PR_NO_SENSE:
        scsi_build_sense(sense, SENSE_CODE(NO_SENSE));
        return CHECK_CONDITION;

    case MPATH_PR_RESERV_CONFLICT:
        return RESERVATION_CONFLICT;

    case MPATH_PR_OTHER:
    default:
        scsi_build_sense(sense, SENSE_CODE(LUN_COMM_FAILURE));
        return CHECK_CONDITION;
    }
}

static int multipath_pr_in(int fd, const uint8_t *cdb, uint8_t *sense,
                           uint8_t *data, int sz)
{
    int rq_servact = cdb[1];
    struct prin_resp resp;
    size_t written;
    int r;

    switch (rq_servact) {
    case MPATH_PRIN_RKEY_SA:
    case MPATH_PRIN_RRES_SA:
    case MPATH_PRIN_RCAP_SA:
        break;
    case MPATH_PRIN_RFSTAT_SA:
        /* Nobody implements it anyway, so bail out. */
    default:
        /* Cannot parse any other output.  */
        scsi_build_sense(sense, SENSE_CODE(INVALID_FIELD));
        return CHECK_CONDITION;
    }

    r = mpath_persistent_reserve_in(fd, rq_servact, &resp, noisy, verbose);
    if (r == MPATH_PR_SUCCESS) {
        switch (rq_servact) {
        case MPATH_PRIN_RKEY_SA:
        case MPATH_PRIN_RRES_SA: {
            struct prin_readdescr *out = &resp.prin_descriptor.prin_readkeys;
            assert(sz >= 8);
            written = MIN(out->additional_length + 8, sz);
            stl_be_p(&data[0], out->prgeneration);
            stl_be_p(&data[4], out->additional_length);
            memcpy(&data[8], out->key_list, written - 8);
            break;
        }
        case MPATH_PRIN_RCAP_SA: {
            struct prin_capdescr *out = &resp.prin_descriptor.prin_readcap;
            assert(sz >= 6);
            written = 6;
            stw_be_p(&data[0], out->length);
            data[2] = out->flags[0];
            data[3] = out->flags[1];
            stw_be_p(&data[4], out->pr_type_mask);
            break;
        }
        default:
            scsi_build_sense(sense, SENSE_CODE(INVALID_OPCODE));
            return CHECK_CONDITION;
        }
        assert(written <= sz);
        memset(data + written, 0, sz - written);
    }

    return mpath_reconstruct_sense(fd, r, sense);
}

static int multipath_pr_out(int fd, const uint8_t *cdb, uint8_t *sense,
                            const uint8_t *param, int sz)
{
    int rq_servact = cdb[1];
    int rq_scope = cdb[2] >> 4;
    int rq_type = cdb[2] & 0xf;
    struct prout_param_descriptor paramp;
    char transportids[PR_HELPER_DATA_SIZE];
    int r;

    if (sz < PR_OUT_FIXED_PARAM_SIZE) {
        /* Illegal request, Parameter list length error.  This isn't fatal;
         * we have read the data, send an error without closing the socket.
         */
        scsi_build_sense(sense, SENSE_CODE(INVALID_PARAM_LEN));
        return CHECK_CONDITION;
    }

    switch (rq_servact) {
    case MPATH_PROUT_REG_SA:
    case MPATH_PROUT_RES_SA:
    case MPATH_PROUT_REL_SA:
    case MPATH_PROUT_CLEAR_SA:
    case MPATH_PROUT_PREE_SA:
    case MPATH_PROUT_PREE_AB_SA:
    case MPATH_PROUT_REG_IGN_SA:
        break;
    case MPATH_PROUT_REG_MOV_SA:
        /* Not supported by struct prout_param_descriptor.  */
    default:
        /* Cannot parse any other input.  */
        scsi_build_sense(sense, SENSE_CODE(INVALID_FIELD));
        return CHECK_CONDITION;
    }

    /* Convert input data, especially transport IDs, to the structs
     * used by libmpathpersist (which, of course, will immediately
     * do the opposite).
     */
    memset(&paramp, 0, sizeof(paramp));
    memcpy(&paramp.key, &param[0], 8);
    memcpy(&paramp.sa_key, &param[8], 8);
    paramp.sa_flags = param[20];
    if (sz > PR_OUT_FIXED_PARAM_SIZE) {
        size_t transportid_len;
        int i, j;
        if (sz < PR_OUT_FIXED_PARAM_SIZE + 4) {
            scsi_build_sense(sense, SENSE_CODE(INVALID_PARAM_LEN));
            return CHECK_CONDITION;
        }
        transportid_len = ldl_be_p(&param[24]) + PR_OUT_FIXED_PARAM_SIZE + 4;
        if (transportid_len > sz) {
            scsi_build_sense(sense, SENSE_CODE(INVALID_PARAM));
            return CHECK_CONDITION;
        }
        for (i = PR_OUT_FIXED_PARAM_SIZE + 4, j = 0; i < transportid_len; ) {
            struct transportid *id = (struct transportid *) &transportids[j];
            int len;

            id->format_code = param[i] & 0xc0;
            id->protocol_id = param[i] & 0x0f;
            switch (param[i] & 0xcf) {
            case 0:
                /* FC transport.  */
                if (i + 24 > transportid_len) {
                    goto illegal_req;
                }
                memcpy(id->n_port_name, &param[i + 8], 8);
                j += offsetof(struct transportid, n_port_name[8]);
                i += 24;
                break;
            case 5:
            case 0x45:
                /* iSCSI transport.  */
                len = lduw_be_p(&param[i + 2]);
                if (len > 252 || (len & 3) || i + len + 4 > transportid_len) {
                    /* For format code 00, the standard says the maximum is 223
                     * plus the NUL terminator.  For format code 01 there is no
                     * maximum length, but libmpathpersist ignores the first
                     * byte of id->iscsi_name so our maximum is 252.
                     */
                    goto illegal_req;
                }
                if (memchr(&param[i + 4], 0, len) == NULL) {
                    goto illegal_req;
                }
                memcpy(id->iscsi_name, &param[i + 2], len + 2);
                j += offsetof(struct transportid, iscsi_name[len + 2]);
                i += len + 4;
                break;
            case 6:
                /* SAS transport.  */
                if (i + 24 > transportid_len) {
                    goto illegal_req;
                }
                memcpy(id->sas_address, &param[i + 4], 8);
                j += offsetof(struct transportid, sas_address[8]);
                i += 24;
                break;
            default:
            illegal_req:
                scsi_build_sense(sense, SENSE_CODE(INVALID_PARAM));
                return CHECK_CONDITION;
            }

            paramp.trnptid_list[paramp.num_transportid++] = id;
        }
    }

    r = mpath_persistent_reserve_out(fd, rq_servact, rq_scope, rq_type,
                                     &paramp, noisy, verbose);
    return mpath_reconstruct_sense(fd, r, sense);
}
#endif

static int do_pr_in(int fd, const uint8_t *cdb, uint8_t *sense,
                    uint8_t *data, int *resp_sz)
{
#ifdef CONFIG_MPATH
    if (is_mpath(fd)) {
        /* multipath_pr_in fills the whole input buffer.  */
        int r = multipath_pr_in(fd, cdb, sense, data, *resp_sz);
        if (r != GOOD) {
            *resp_sz = 0;
        }
        return r;
    }
#endif

    return do_sgio(fd, cdb, sense, data, resp_sz,
                   SG_DXFER_FROM_DEV);
}

static int do_pr_out(int fd, const uint8_t *cdb, uint8_t *sense,
                     const uint8_t *param, int sz)
{
    int resp_sz;

    if ((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY) {
        scsi_build_sense(sense, SENSE_CODE(INVALID_OPCODE));
        return CHECK_CONDITION;
    }

#ifdef CONFIG_MPATH
    if (is_mpath(fd)) {
        return multipath_pr_out(fd, cdb, sense, param, sz);
    }
#endif

    resp_sz = sz;
    return do_sgio(fd, cdb, sense, (uint8_t *)param, &resp_sz,
                   SG_DXFER_TO_DEV);
}

/* Client */

typedef struct PRHelperClient {
    QIOChannelSocket *ioc;
    Coroutine *co;
    int fd;
    uint8_t data[PR_HELPER_DATA_SIZE];
} PRHelperClient;

typedef struct PRHelperRequest {
    int fd;
    size_t sz;
    uint8_t cdb[PR_HELPER_CDB_SIZE];
} PRHelperRequest;

static int coroutine_fn prh_read(PRHelperClient *client, void *buf, int sz,
                                 Error **errp)
{
    int ret = 0;

    while (sz > 0) {
        int *fds = NULL;
        size_t nfds = 0;
        int i;
        struct iovec iov;
        ssize_t n_read;

        iov.iov_base = buf;
        iov.iov_len = sz;
        n_read = qio_channel_readv_full(QIO_CHANNEL(client->ioc), &iov, 1,
                                        &fds, &nfds, errp);

        if (n_read == QIO_CHANNEL_ERR_BLOCK) {
            qio_channel_yield(QIO_CHANNEL(client->ioc), G_IO_IN);
            continue;
        }
        if (n_read <= 0) {
            ret = n_read ? n_read : -1;
            goto err;
        }

        /* Stash one file descriptor per request.  */
        if (nfds) {
            bool too_many = false;
            for (i = 0; i < nfds; i++) {
                if (client->fd == -1) {
                    client->fd = fds[i];
                } else {
                    close(fds[i]);
                    too_many = true;
                }
            }
            g_free(fds);
            if (too_many) {
                ret = -1;
                goto err;
            }
        }

        buf += n_read;
        sz -= n_read;
    }

    return 0;

err:
    if (client->fd != -1) {
        close(client->fd);
        client->fd = -1;
    }
    return ret;
}

static int coroutine_fn prh_read_request(PRHelperClient *client,
                                         PRHelperRequest *req,
                                         PRHelperResponse *resp, Error **errp)
{
    uint32_t sz;

    if (prh_read(client, req->cdb, sizeof(req->cdb), NULL) < 0) {
        return -1;
    }

    if (client->fd == -1) {
        error_setg(errp, "No file descriptor in request.");
        return -1;
    }

    if (req->cdb[0] != PERSISTENT_RESERVE_OUT &&
        req->cdb[0] != PERSISTENT_RESERVE_IN) {
        error_setg(errp, "Invalid CDB, closing socket.");
        goto out_close;
    }

    sz = scsi_cdb_xfer(req->cdb);
    if (sz > sizeof(client->data)) {
        goto out_close;
    }

    if (req->cdb[0] == PERSISTENT_RESERVE_OUT) {
        if (qio_channel_read_all(QIO_CHANNEL(client->ioc),
                                 (char *)client->data, sz,
                                 errp) < 0) {
            goto out_close;
        }
    }

    req->fd = client->fd;
    req->sz = sz;
    client->fd = -1;
    return sz;

out_close:
    close(client->fd);
    client->fd = -1;
    return -1;
}

static int coroutine_fn prh_write_response(PRHelperClient *client,
                                           PRHelperRequest *req,
                                           PRHelperResponse *resp, Error **errp)
{
    ssize_t r;
    size_t sz;

    if (req->cdb[0] == PERSISTENT_RESERVE_IN && resp->result == GOOD) {
        assert(resp->sz <= req->sz && resp->sz <= sizeof(client->data));
    } else {
        assert(resp->sz == 0);
    }

    sz = resp->sz;

    resp->result = cpu_to_be32(resp->result);
    resp->sz = cpu_to_be32(resp->sz);
    r = qio_channel_write_all(QIO_CHANNEL(client->ioc),
                              (char *) resp, sizeof(*resp), errp);
    if (r < 0) {
        return r;
    }

    r = qio_channel_write_all(QIO_CHANNEL(client->ioc),
                              (char *) client->data,
                              sz, errp);
    return r < 0 ? r : 0;
}

static void coroutine_fn prh_co_entry(void *opaque)
{
    PRHelperClient *client = opaque;
    Error *local_err = NULL;
    uint32_t flags;
    int r;

    qio_channel_set_blocking(QIO_CHANNEL(client->ioc),
                             false, NULL);
    qio_channel_attach_aio_context(QIO_CHANNEL(client->ioc),
                                   qemu_get_aio_context());

    /* A very simple negotiation for future extensibility.  No features
     * are defined so write 0.
     */
    flags = cpu_to_be32(0);
    r = qio_channel_write_all(QIO_CHANNEL(client->ioc),
                             (char *) &flags, sizeof(flags), NULL);
    if (r < 0) {
        goto out;
    }

    r = qio_channel_read_all(QIO_CHANNEL(client->ioc),
                             (char *) &flags, sizeof(flags), NULL);
    if (be32_to_cpu(flags) != 0 || r < 0) {
        goto out;
    }

    while (atomic_read(&state) == RUNNING) {
        PRHelperRequest req;
        PRHelperResponse resp;
        int sz;

        sz = prh_read_request(client, &req, &resp, &local_err);
        if (sz < 0) {
            break;
        }

        num_active_sockets++;
        if (req.cdb[0] == PERSISTENT_RESERVE_OUT) {
            r = do_pr_out(req.fd, req.cdb, resp.sense,
                          client->data, sz);
            resp.sz = 0;
        } else {
            resp.sz = sizeof(client->data);
            r = do_pr_in(req.fd, req.cdb, resp.sense,
                         client->data, &resp.sz);
            resp.sz = MIN(resp.sz, sz);
        }
        num_active_sockets--;
        close(req.fd);
        if (r == -1) {
            break;
        }
        resp.result = r;

        if (prh_write_response(client, &req, &resp, &local_err) < 0) {
            break;
        }
    }

    if (local_err) {
        if (verbose == 0) {
            error_free(local_err);
        } else {
            error_report_err(local_err);
        }
    }

out:
    qio_channel_detach_aio_context(QIO_CHANNEL(client->ioc));
    object_unref(OBJECT(client->ioc));
    g_free(client);
}

static gboolean accept_client(QIOChannel *ioc, GIOCondition cond, gpointer opaque)
{
    QIOChannelSocket *cioc;
    PRHelperClient *prh;

    cioc = qio_channel_socket_accept(QIO_CHANNEL_SOCKET(ioc),
                                     NULL);
    if (!cioc) {
        return TRUE;
    }

    prh = g_new(PRHelperClient, 1);
    prh->ioc = cioc;
    prh->fd = -1;
    prh->co = qemu_coroutine_create(prh_co_entry, prh);
    qemu_coroutine_enter(prh->co);

    return TRUE;
}

static void termsig_handler(int signum)
{
    atomic_cmpxchg(&state, RUNNING, TERMINATE);
    qemu_notify_event();
}

static void close_server_socket(void)
{
    assert(server_ioc);

    g_source_remove(server_watch);
    server_watch = -1;
    object_unref(OBJECT(server_ioc));
    num_active_sockets--;
}

#ifdef CONFIG_LIBCAP
static int drop_privileges(void)
{
    /* clear all capabilities */
    capng_clear(CAPNG_SELECT_BOTH);

    if (capng_update(CAPNG_ADD, CAPNG_EFFECTIVE | CAPNG_PERMITTED,
                     CAP_SYS_RAWIO) < 0) {
        return -1;
    }

#ifdef CONFIG_MPATH
    /* For /dev/mapper/control ioctls */
    if (capng_update(CAPNG_ADD, CAPNG_EFFECTIVE | CAPNG_PERMITTED,
                     CAP_SYS_ADMIN) < 0) {
        return -1;
    }
#endif

    /* Change user/group id, retaining the capabilities.  Because file descriptors
     * are passed via SCM_RIGHTS, we don't need supplementary groups (and in
     * fact the helper can run as "nobody").
     */
    if (capng_change_id(uid != -1 ? uid : getuid(),
                        gid != -1 ? gid : getgid(),
                        CAPNG_DROP_SUPP_GRP | CAPNG_CLEAR_BOUNDING)) {
        return -1;
    }

    return 0;
}
#endif

int main(int argc, char **argv)
{
    const char *sopt = "hVk:f:dT:u:g:vq";
    struct option lopt[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { "socket", required_argument, NULL, 'k' },
        { "pidfile", required_argument, NULL, 'f' },
        { "daemon", no_argument, NULL, 'd' },
        { "trace", required_argument, NULL, 'T' },
        { "user", required_argument, NULL, 'u' },
        { "group", required_argument, NULL, 'g' },
        { "verbose", no_argument, NULL, 'v' },
        { "quiet", no_argument, NULL, 'q' },
        { NULL, 0, NULL, 0 }
    };
    int opt_ind = 0;
    int loglevel = 1;
    int quiet = 0;
    int ch;
    Error *local_err = NULL;
    char *trace_file = NULL;
    bool daemonize = false;
    bool pidfile_specified = false;
    bool socket_path_specified = false;
    unsigned socket_activation;

    struct sigaction sa_sigterm;
    memset(&sa_sigterm, 0, sizeof(sa_sigterm));
    sa_sigterm.sa_handler = termsig_handler;
    sigaction(SIGTERM, &sa_sigterm, NULL);
    sigaction(SIGINT, &sa_sigterm, NULL);
    sigaction(SIGHUP, &sa_sigterm, NULL);

    signal(SIGPIPE, SIG_IGN);

    module_call_init(MODULE_INIT_TRACE);
    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_trace_opts);
    qemu_init_exec_dir(argv[0]);

    compute_default_paths();

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 'k':
            g_free(socket_path);
            socket_path = g_strdup(optarg);
            socket_path_specified = true;
            if (socket_path[0] != '/') {
                error_report("socket path must be absolute");
                exit(EXIT_FAILURE);
            }
            break;
        case 'f':
            g_free(pidfile);
            pidfile = g_strdup(optarg);
            pidfile_specified = true;
            break;
#ifdef CONFIG_LIBCAP
        case 'u': {
            unsigned long res;
            struct passwd *userinfo = getpwnam(optarg);
            if (userinfo) {
                uid = userinfo->pw_uid;
            } else if (qemu_strtoul(optarg, NULL, 10, &res) == 0 &&
                       (uid_t)res == res) {
                uid = res;
            } else {
                error_report("invalid user '%s'", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        }
        case 'g': {
            unsigned long res;
            struct group *groupinfo = getgrnam(optarg);
            if (groupinfo) {
                gid = groupinfo->gr_gid;
            } else if (qemu_strtoul(optarg, NULL, 10, &res) == 0 &&
                       (gid_t)res == res) {
                gid = res;
            } else {
                error_report("invalid group '%s'", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        }
#else
        case 'u':
        case 'g':
            error_report("-%c not supported by this %s", ch, argv[0]);
            exit(1);
#endif
        case 'd':
            daemonize = true;
            break;
        case 'q':
            quiet = 1;
            break;
        case 'v':
            ++loglevel;
            break;
        case 'T':
            g_free(trace_file);
            trace_file = trace_opt_parse(optarg);
            break;
        case 'V':
            version(argv[0]);
            exit(EXIT_SUCCESS);
            break;
        case 'h':
            usage(argv[0]);
            exit(EXIT_SUCCESS);
            break;
        case '?':
            error_report("Try `%s --help' for more information.", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* set verbosity */
    noisy = !quiet && (loglevel >= 3);
    verbose = quiet ? 0 : MIN(loglevel, 3);

    if (!trace_init_backends()) {
        exit(EXIT_FAILURE);
    }
    trace_init_file(trace_file);
    qemu_set_log(LOG_TRACE);

#ifdef CONFIG_MPATH
    dm_init();
    multipath_pr_init();
#endif

    socket_activation = check_socket_activation();
    if (socket_activation == 0) {
        SocketAddress saddr;
        saddr = (SocketAddress){
            .type = SOCKET_ADDRESS_TYPE_UNIX,
            .u.q_unix.path = socket_path,
        };
        server_ioc = qio_channel_socket_new();
        if (qio_channel_socket_listen_sync(server_ioc, &saddr, &local_err) < 0) {
            object_unref(OBJECT(server_ioc));
            error_report_err(local_err);
            return 1;
        }
    } else {
        /* Using socket activation - check user didn't use -p etc. */
        if (socket_path_specified) {
            error_report("Unix socket can't be set when using socket activation");
            exit(EXIT_FAILURE);
        }

        /* Can only listen on a single socket.  */
        if (socket_activation > 1) {
            error_report("%s does not support socket activation with LISTEN_FDS > 1",
                         argv[0]);
            exit(EXIT_FAILURE);
        }
        server_ioc = qio_channel_socket_new_fd(FIRST_SOCKET_ACTIVATION_FD,
                                               &local_err);
        if (server_ioc == NULL) {
            error_report("Failed to use socket activation: %s",
                         error_get_pretty(local_err));
            exit(EXIT_FAILURE);
        }
    }

    if (qemu_init_main_loop(&local_err)) {
        error_report_err(local_err);
        exit(EXIT_FAILURE);
    }

    server_watch = qio_channel_add_watch(QIO_CHANNEL(server_ioc),
                                         G_IO_IN,
                                         accept_client,
                                         NULL, NULL);

    if (daemonize) {
        if (daemon(0, 0) < 0) {
            error_report("Failed to daemonize: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if ((daemonize || pidfile_specified) &&
        !qemu_write_pidfile(pidfile, &local_err)) {
        error_report_err(local_err);
        exit(EXIT_FAILURE);
    }

#ifdef CONFIG_LIBCAP
    if (drop_privileges() < 0) {
        error_report("Failed to drop privileges: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
#endif

    state = RUNNING;
    do {
        main_loop_wait(false);
        if (state == TERMINATE) {
            state = TERMINATING;
            close_server_socket();
        }
    } while (num_active_sockets > 0);

    exit(EXIT_SUCCESS);
}
