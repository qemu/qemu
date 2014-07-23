/*
 * QEMU Block driver for Archipelago
 *
 * Copyright (C) 2014 Chrysostomos Nanakos <cnanakos@grnet.gr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/*
 * VM Image on Archipelago volume is specified like this:
 *
 * file.driver=archipelago,file.volume=<volumename>
 * [,file.mport=<mapperd_port>[,file.vport=<vlmcd_port>]
 * [,file.segment=<segment_name>]]
 *
 * or
 *
 * file=archipelago:<volumename>[/mport=<mapperd_port>[:vport=<vlmcd_port>][:
 * segment=<segment_name>]]
 *
 * 'archipelago' is the protocol.
 *
 * 'mport' is the port number on which mapperd is listening. This is optional
 * and if not specified, QEMU will make Archipelago to use the default port.
 *
 * 'vport' is the port number on which vlmcd is listening. This is optional
 * and if not specified, QEMU will make Archipelago to use the default port.
 *
 * 'segment' is the name of the shared memory segment Archipelago stack
 * is using. This is optional and if not specified, QEMU will make Archipelago
 * to use the default value, 'archipelago'.
 *
 * Examples:
 *
 * file.driver=archipelago,file.volume=my_vm_volume
 * file.driver=archipelago,file.volume=my_vm_volume,file.mport=123
 * file.driver=archipelago,file.volume=my_vm_volume,file.mport=123,
 *  file.vport=1234
 * file.driver=archipelago,file.volume=my_vm_volume,file.mport=123,
 *  file.vport=1234,file.segment=my_segment
 *
 * or
 *
 * file=archipelago:my_vm_volume
 * file=archipelago:my_vm_volume/mport=123
 * file=archipelago:my_vm_volume/mport=123:vport=1234
 * file=archipelago:my_vm_volume/mport=123:vport=1234:segment=my_segment
 *
 */

#include "qemu-common.h"
#include "block/block_int.h"
#include "qemu/error-report.h"
#include "qemu/thread.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qjson.h"

#include <inttypes.h>
#include <xseg/xseg.h>
#include <xseg/protocol.h>

#define ARCHIP_FD_READ      0
#define ARCHIP_FD_WRITE     1
#define MAX_REQUEST_SIZE    524288

#define ARCHIPELAGO_OPT_VOLUME      "volume"
#define ARCHIPELAGO_OPT_SEGMENT     "segment"
#define ARCHIPELAGO_OPT_MPORT       "mport"
#define ARCHIPELAGO_OPT_VPORT       "vport"
#define ARCHIPELAGO_DFL_MPORT       1001
#define ARCHIPELAGO_DFL_VPORT       501

#define archipelagolog(fmt, ...) \
    do {                         \
        fprintf(stderr, "archipelago\t%-24s: " fmt, __func__, ##__VA_ARGS__); \
    } while (0)

typedef enum {
    ARCHIP_OP_READ,
    ARCHIP_OP_WRITE,
    ARCHIP_OP_FLUSH,
    ARCHIP_OP_VOLINFO,
} ARCHIPCmd;

typedef struct ArchipelagoAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    struct BDRVArchipelagoState *s;
    QEMUIOVector *qiov;
    ARCHIPCmd cmd;
    bool cancelled;
    int status;
    int64_t size;
    int64_t ret;
} ArchipelagoAIOCB;

typedef struct BDRVArchipelagoState {
    ArchipelagoAIOCB *event_acb;
    char *volname;
    char *segment_name;
    uint64_t size;
    /* Archipelago specific */
    struct xseg *xseg;
    struct xseg_port *port;
    xport srcport;
    xport sport;
    xport mportno;
    xport vportno;
    QemuMutex archip_mutex;
    QemuCond archip_cond;
    bool is_signaled;
    /* Request handler specific */
    QemuThread request_th;
    QemuCond request_cond;
    QemuMutex request_mutex;
    bool th_is_signaled;
    bool stopping;
} BDRVArchipelagoState;

typedef struct ArchipelagoSegmentedRequest {
    size_t count;
    size_t total;
    int ref;
    int failed;
} ArchipelagoSegmentedRequest;

typedef struct AIORequestData {
    const char *volname;
    off_t offset;
    size_t size;
    uint64_t bufidx;
    int ret;
    int op;
    ArchipelagoAIOCB *aio_cb;
    ArchipelagoSegmentedRequest *segreq;
} AIORequestData;

static void qemu_archipelago_complete_aio(void *opaque);

static void init_local_signal(struct xseg *xseg, xport sport, xport srcport)
{
    if (xseg && (sport != srcport)) {
        xseg_init_local_signal(xseg, srcport);
        sport = srcport;
    }
}

static void archipelago_finish_aiocb(AIORequestData *reqdata)
{
    if (reqdata->aio_cb->ret != reqdata->segreq->total) {
        reqdata->aio_cb->ret = -EIO;
    } else if (reqdata->aio_cb->ret == reqdata->segreq->total) {
        reqdata->aio_cb->ret = 0;
    }
    reqdata->aio_cb->bh = aio_bh_new(
                        bdrv_get_aio_context(reqdata->aio_cb->common.bs),
                        qemu_archipelago_complete_aio, reqdata
                        );
    qemu_bh_schedule(reqdata->aio_cb->bh);
}

static int wait_reply(struct xseg *xseg, xport srcport, struct xseg_port *port,
                      struct xseg_request *expected_req)
{
    struct xseg_request *req;
    xseg_prepare_wait(xseg, srcport);
    void *psd = xseg_get_signal_desc(xseg, port);
    while (1) {
        req = xseg_receive(xseg, srcport, X_NONBLOCK);
        if (req) {
            if (req != expected_req) {
                archipelagolog("Unknown received request\n");
                xseg_put_request(xseg, req, srcport);
            } else if (!(req->state & XS_SERVED)) {
                return -1;
            } else {
                break;
            }
        }
        xseg_wait_signal(xseg, psd, 100000UL);
    }
    xseg_cancel_wait(xseg, srcport);
    return 0;
}

static void xseg_request_handler(void *state)
{
    BDRVArchipelagoState *s = (BDRVArchipelagoState *) state;
    void *psd = xseg_get_signal_desc(s->xseg, s->port);
    qemu_mutex_lock(&s->request_mutex);

    while (!s->stopping) {
        struct xseg_request *req;
        void *data;
        xseg_prepare_wait(s->xseg, s->srcport);
        req = xseg_receive(s->xseg, s->srcport, X_NONBLOCK);
        if (req) {
            AIORequestData *reqdata;
            ArchipelagoSegmentedRequest *segreq;
            xseg_get_req_data(s->xseg, req, (void **)&reqdata);

            switch (reqdata->op) {
            case ARCHIP_OP_READ:
                data = xseg_get_data(s->xseg, req);
                segreq = reqdata->segreq;
                segreq->count += req->serviced;

                qemu_iovec_from_buf(reqdata->aio_cb->qiov, reqdata->bufidx,
                                    data,
                                    req->serviced);

                xseg_put_request(s->xseg, req, s->srcport);

                if ((__sync_add_and_fetch(&segreq->ref, -1)) == 0) {
                    if (!segreq->failed) {
                        reqdata->aio_cb->ret = segreq->count;
                        archipelago_finish_aiocb(reqdata);
                        g_free(segreq);
                    } else {
                        g_free(segreq);
                        g_free(reqdata);
                    }
                } else {
                    g_free(reqdata);
                }
                break;
            case ARCHIP_OP_WRITE:
            case ARCHIP_OP_FLUSH:
                segreq = reqdata->segreq;
                segreq->count += req->serviced;
                xseg_put_request(s->xseg, req, s->srcport);

                if ((__sync_add_and_fetch(&segreq->ref, -1)) == 0) {
                    if (!segreq->failed) {
                        reqdata->aio_cb->ret = segreq->count;
                        archipelago_finish_aiocb(reqdata);
                        g_free(segreq);
                    } else {
                        g_free(segreq);
                        g_free(reqdata);
                    }
                } else {
                    g_free(reqdata);
                }
                break;
            case ARCHIP_OP_VOLINFO:
                s->is_signaled = true;
                qemu_cond_signal(&s->archip_cond);
                break;
            }
        } else {
            xseg_wait_signal(s->xseg, psd, 100000UL);
        }
        xseg_cancel_wait(s->xseg, s->srcport);
    }

    s->th_is_signaled = true;
    qemu_cond_signal(&s->request_cond);
    qemu_mutex_unlock(&s->request_mutex);
    qemu_thread_exit(NULL);
}

static int qemu_archipelago_xseg_init(BDRVArchipelagoState *s)
{
    if (xseg_initialize()) {
        archipelagolog("Cannot initialize XSEG\n");
        goto err_exit;
    }

    s->xseg = xseg_join("posix", s->segment_name,
                        "posixfd", NULL);
    if (!s->xseg) {
        archipelagolog("Cannot join XSEG shared memory segment\n");
        goto err_exit;
    }
    s->port = xseg_bind_dynport(s->xseg);
    s->srcport = s->port->portno;
    init_local_signal(s->xseg, s->sport, s->srcport);
    return 0;

err_exit:
    return -1;
}

static int qemu_archipelago_init(BDRVArchipelagoState *s)
{
    int ret;

    ret = qemu_archipelago_xseg_init(s);
    if (ret < 0) {
        error_report("Cannot initialize XSEG. Aborting...\n");
        goto err_exit;
    }

    qemu_cond_init(&s->archip_cond);
    qemu_mutex_init(&s->archip_mutex);
    qemu_cond_init(&s->request_cond);
    qemu_mutex_init(&s->request_mutex);
    s->th_is_signaled = false;
    qemu_thread_create(&s->request_th, "xseg_io_th",
                       (void *) xseg_request_handler,
                       (void *) s, QEMU_THREAD_JOINABLE);

err_exit:
    return ret;
}

static void qemu_archipelago_complete_aio(void *opaque)
{
    AIORequestData *reqdata = (AIORequestData *) opaque;
    ArchipelagoAIOCB *aio_cb = (ArchipelagoAIOCB *) reqdata->aio_cb;

    qemu_bh_delete(aio_cb->bh);
    aio_cb->common.cb(aio_cb->common.opaque, aio_cb->ret);
    aio_cb->status = 0;

    if (!aio_cb->cancelled) {
        qemu_aio_release(aio_cb);
    }
    g_free(reqdata);
}

static void xseg_find_port(char *pstr, const char *needle, xport *aport)
{
    const char *a;
    char *endptr = NULL;
    unsigned long port;
    if (strstart(pstr, needle, &a)) {
        if (strlen(a) > 0) {
            port = strtoul(a, &endptr, 10);
            if (strlen(endptr)) {
                *aport = -2;
                return;
            }
            *aport = (xport) port;
        }
    }
}

static void xseg_find_segment(char *pstr, const char *needle,
                              char **segment_name)
{
    const char *a;
    if (strstart(pstr, needle, &a)) {
        if (strlen(a) > 0) {
            *segment_name = g_strdup(a);
        }
    }
}

static void parse_filename_opts(const char *filename, Error **errp,
                                char **volume, char **segment_name,
                                xport *mport, xport *vport)
{
    const char *start;
    char *tokens[4], *ds;
    int idx;
    xport lmport = NoPort, lvport = NoPort;

    strstart(filename, "archipelago:", &start);

    ds = g_strdup(start);
    tokens[0] = strtok(ds, "/");
    tokens[1] = strtok(NULL, ":");
    tokens[2] = strtok(NULL, ":");
    tokens[3] = strtok(NULL, "\0");

    if (!strlen(tokens[0])) {
        error_setg(errp, "volume name must be specified first");
        g_free(ds);
        return;
    }

    for (idx = 1; idx < 4; idx++) {
        if (tokens[idx] != NULL) {
            if (strstart(tokens[idx], "mport=", NULL)) {
                xseg_find_port(tokens[idx], "mport=", &lmport);
            }
            if (strstart(tokens[idx], "vport=", NULL)) {
                xseg_find_port(tokens[idx], "vport=", &lvport);
            }
            if (strstart(tokens[idx], "segment=", NULL)) {
                xseg_find_segment(tokens[idx], "segment=", segment_name);
            }
        }
    }

    if ((lmport == -2) || (lvport == -2)) {
        error_setg(errp, "mport and/or vport must be set");
        g_free(ds);
        return;
    }
    *volume = g_strdup(tokens[0]);
    *mport = lmport;
    *vport = lvport;
    g_free(ds);
}

static void archipelago_parse_filename(const char *filename, QDict *options,
                                       Error **errp)
{
    const char *start;
    char *volume = NULL, *segment_name = NULL;
    xport mport = NoPort, vport = NoPort;

    if (qdict_haskey(options, ARCHIPELAGO_OPT_VOLUME)
            || qdict_haskey(options, ARCHIPELAGO_OPT_SEGMENT)
            || qdict_haskey(options, ARCHIPELAGO_OPT_MPORT)
            || qdict_haskey(options, ARCHIPELAGO_OPT_VPORT)) {
        error_setg(errp, "volume/mport/vport/segment and a file name may not"
                         " be specified at the same time");
        return;
    }

    if (!strstart(filename, "archipelago:", &start)) {
        error_setg(errp, "File name must start with 'archipelago:'");
        return;
    }

    if (!strlen(start) || strstart(start, "/", NULL)) {
        error_setg(errp, "volume name must be specified");
        return;
    }

    parse_filename_opts(filename, errp, &volume, &segment_name, &mport, &vport);

    if (volume) {
        qdict_put(options, ARCHIPELAGO_OPT_VOLUME, qstring_from_str(volume));
        g_free(volume);
    }
    if (segment_name) {
        qdict_put(options, ARCHIPELAGO_OPT_SEGMENT,
                  qstring_from_str(segment_name));
        g_free(segment_name);
    }
    if (mport != NoPort) {
        qdict_put(options, ARCHIPELAGO_OPT_MPORT, qint_from_int(mport));
    }
    if (vport != NoPort) {
        qdict_put(options, ARCHIPELAGO_OPT_VPORT, qint_from_int(vport));
    }
}

static QemuOptsList archipelago_runtime_opts = {
    .name = "archipelago",
    .head = QTAILQ_HEAD_INITIALIZER(archipelago_runtime_opts.head),
    .desc = {
        {
            .name = ARCHIPELAGO_OPT_VOLUME,
            .type = QEMU_OPT_STRING,
            .help = "Name of the volume image",
        },
        {
            .name = ARCHIPELAGO_OPT_SEGMENT,
            .type = QEMU_OPT_STRING,
            .help = "Name of the Archipelago shared memory segment",
        },
        {
            .name = ARCHIPELAGO_OPT_MPORT,
            .type = QEMU_OPT_NUMBER,
            .help = "Archipelago mapperd port number"
        },
        {
            .name = ARCHIPELAGO_OPT_VPORT,
            .type = QEMU_OPT_NUMBER,
            .help = "Archipelago vlmcd port number"

        },
        { /* end of list */ }
    },
};

static int qemu_archipelago_open(BlockDriverState *bs,
                                 QDict *options,
                                 int bdrv_flags,
                                 Error **errp)
{
    int ret = 0;
    const char *volume, *segment_name;
    QemuOpts *opts;
    Error *local_err = NULL;
    BDRVArchipelagoState *s = bs->opaque;

    opts = qemu_opts_create(&archipelago_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto err_exit;
    }

    s->mportno = qemu_opt_get_number(opts, ARCHIPELAGO_OPT_MPORT,
                                     ARCHIPELAGO_DFL_MPORT);
    s->vportno = qemu_opt_get_number(opts, ARCHIPELAGO_OPT_VPORT,
                                     ARCHIPELAGO_DFL_VPORT);

    segment_name = qemu_opt_get(opts, ARCHIPELAGO_OPT_SEGMENT);
    if (segment_name == NULL) {
        s->segment_name = g_strdup("archipelago");
    } else {
        s->segment_name = g_strdup(segment_name);
    }

    volume = qemu_opt_get(opts, ARCHIPELAGO_OPT_VOLUME);
    if (volume == NULL) {
        error_setg(errp, "archipelago block driver requires the 'volume'"
                   " option");
        ret = -EINVAL;
        goto err_exit;
    }
    s->volname = g_strdup(volume);

    /* Initialize XSEG, join shared memory segment */
    ret = qemu_archipelago_init(s);
    if (ret < 0) {
        error_setg(errp, "cannot initialize XSEG and join shared "
                   "memory segment");
        goto err_exit;
    }

    qemu_opts_del(opts);
    return 0;

err_exit:
    g_free(s->volname);
    g_free(s->segment_name);
    qemu_opts_del(opts);
    return ret;
}

static void qemu_archipelago_close(BlockDriverState *bs)
{
    int r, targetlen;
    char *target;
    struct xseg_request *req;
    BDRVArchipelagoState *s = bs->opaque;

    s->stopping = true;

    qemu_mutex_lock(&s->request_mutex);
    while (!s->th_is_signaled) {
        qemu_cond_wait(&s->request_cond,
                       &s->request_mutex);
    }
    qemu_mutex_unlock(&s->request_mutex);
    qemu_thread_join(&s->request_th);
    qemu_cond_destroy(&s->request_cond);
    qemu_mutex_destroy(&s->request_mutex);

    qemu_cond_destroy(&s->archip_cond);
    qemu_mutex_destroy(&s->archip_mutex);

    targetlen = strlen(s->volname);
    req = xseg_get_request(s->xseg, s->srcport, s->vportno, X_ALLOC);
    if (!req) {
        archipelagolog("Cannot get XSEG request\n");
        goto err_exit;
    }
    r = xseg_prep_request(s->xseg, req, targetlen, 0);
    if (r < 0) {
        xseg_put_request(s->xseg, req, s->srcport);
        archipelagolog("Cannot prepare XSEG close request\n");
        goto err_exit;
    }

    target = xseg_get_target(s->xseg, req);
    memcpy(target, s->volname, targetlen);
    req->size = req->datalen;
    req->offset = 0;
    req->op = X_CLOSE;

    xport p = xseg_submit(s->xseg, req, s->srcport, X_ALLOC);
    if (p == NoPort) {
        xseg_put_request(s->xseg, req, s->srcport);
        archipelagolog("Cannot submit XSEG close request\n");
        goto err_exit;
    }

    xseg_signal(s->xseg, p);
    wait_reply(s->xseg, s->srcport, s->port, req);

    xseg_put_request(s->xseg, req, s->srcport);

err_exit:
    g_free(s->volname);
    g_free(s->segment_name);
    xseg_quit_local_signal(s->xseg, s->srcport);
    xseg_leave_dynport(s->xseg, s->port);
    xseg_leave(s->xseg);
}

static int qemu_archipelago_create_volume(Error **errp, const char *volname,
                                          char *segment_name,
                                          uint64_t size, xport mportno,
                                          xport vportno)
{
    int ret, targetlen;
    struct xseg *xseg = NULL;
    struct xseg_request *req;
    struct xseg_request_clone *xclone;
    struct xseg_port *port;
    xport srcport = NoPort, sport = NoPort;
    char *target;

    /* Try default values if none has been set */
    if (mportno == (xport) -1) {
        mportno = ARCHIPELAGO_DFL_MPORT;
    }

    if (vportno == (xport) -1) {
        vportno = ARCHIPELAGO_DFL_VPORT;
    }

    if (xseg_initialize()) {
        error_setg(errp, "Cannot initialize XSEG");
        return -1;
    }

    xseg = xseg_join("posix", segment_name,
                     "posixfd", NULL);

    if (!xseg) {
        error_setg(errp, "Cannot join XSEG shared memory segment");
        return -1;
    }

    port = xseg_bind_dynport(xseg);
    srcport = port->portno;
    init_local_signal(xseg, sport, srcport);

    req = xseg_get_request(xseg, srcport, mportno, X_ALLOC);
    if (!req) {
        error_setg(errp, "Cannot get XSEG request");
        return -1;
    }

    targetlen = strlen(volname);
    ret = xseg_prep_request(xseg, req, targetlen,
                            sizeof(struct xseg_request_clone));
    if (ret < 0) {
        error_setg(errp, "Cannot prepare XSEG request");
        goto err_exit;
    }

    target = xseg_get_target(xseg, req);
    if (!target) {
        error_setg(errp, "Cannot get XSEG target.\n");
        goto err_exit;
    }
    memcpy(target, volname, targetlen);
    xclone = (struct xseg_request_clone *) xseg_get_data(xseg, req);
    memset(xclone->target, 0 , XSEG_MAX_TARGETLEN);
    xclone->targetlen = 0;
    xclone->size = size;
    req->offset = 0;
    req->size = req->datalen;
    req->op = X_CLONE;

    xport p = xseg_submit(xseg, req, srcport, X_ALLOC);
    if (p == NoPort) {
        error_setg(errp, "Could not submit XSEG request");
        goto err_exit;
    }
    xseg_signal(xseg, p);

    ret = wait_reply(xseg, srcport, port, req);
    if (ret < 0) {
        error_setg(errp, "wait_reply() error.");
    }

    xseg_put_request(xseg, req, srcport);
    xseg_quit_local_signal(xseg, srcport);
    xseg_leave_dynport(xseg, port);
    xseg_leave(xseg);
    return ret;

err_exit:
    xseg_put_request(xseg, req, srcport);
    xseg_quit_local_signal(xseg, srcport);
    xseg_leave_dynport(xseg, port);
    xseg_leave(xseg);
    return -1;
}

static int qemu_archipelago_create(const char *filename,
                                   QemuOpts *options,
                                   Error **errp)
{
    int ret = 0;
    uint64_t total_size = 0;
    char *volname = NULL, *segment_name = NULL;
    const char *start;
    xport mport = NoPort, vport = NoPort;

    if (!strstart(filename, "archipelago:", &start)) {
        error_setg(errp, "File name must start with 'archipelago:'");
        return -1;
    }

    if (!strlen(start) || strstart(start, "/", NULL)) {
        error_setg(errp, "volume name must be specified");
        return -1;
    }

    parse_filename_opts(filename, errp, &volname, &segment_name, &mport,
                        &vport);
    total_size = qemu_opt_get_size_del(options, BLOCK_OPT_SIZE, 0);

    if (segment_name == NULL) {
        segment_name = g_strdup("archipelago");
    }

    /* Create an Archipelago volume */
    ret = qemu_archipelago_create_volume(errp, volname, segment_name,
                                         total_size, mport,
                                         vport);

    g_free(volname);
    g_free(segment_name);
    return ret;
}

static void qemu_archipelago_aio_cancel(BlockDriverAIOCB *blockacb)
{
    ArchipelagoAIOCB *aio_cb = (ArchipelagoAIOCB *) blockacb;
    aio_cb->cancelled = true;
    while (aio_cb->status == -EINPROGRESS) {
        aio_poll(bdrv_get_aio_context(aio_cb->common.bs), true);
    }
    qemu_aio_release(aio_cb);
}

static const AIOCBInfo archipelago_aiocb_info = {
    .aiocb_size = sizeof(ArchipelagoAIOCB),
    .cancel = qemu_archipelago_aio_cancel,
};

static int archipelago_submit_request(BDRVArchipelagoState *s,
                                        uint64_t bufidx,
                                        size_t count,
                                        off_t offset,
                                        ArchipelagoAIOCB *aio_cb,
                                        ArchipelagoSegmentedRequest *segreq,
                                        int op)
{
    int ret, targetlen;
    char *target;
    void *data = NULL;
    struct xseg_request *req;
    AIORequestData *reqdata = g_malloc(sizeof(AIORequestData));

    targetlen = strlen(s->volname);
    req = xseg_get_request(s->xseg, s->srcport, s->vportno, X_ALLOC);
    if (!req) {
        archipelagolog("Cannot get XSEG request\n");
        goto err_exit2;
    }
    ret = xseg_prep_request(s->xseg, req, targetlen, count);
    if (ret < 0) {
        archipelagolog("Cannot prepare XSEG request\n");
        goto err_exit;
    }
    target = xseg_get_target(s->xseg, req);
    if (!target) {
        archipelagolog("Cannot get XSEG target\n");
        goto err_exit;
    }
    memcpy(target, s->volname, targetlen);
    req->size = count;
    req->offset = offset;

    switch (op) {
    case ARCHIP_OP_READ:
        req->op = X_READ;
        break;
    case ARCHIP_OP_WRITE:
        req->op = X_WRITE;
        break;
    case ARCHIP_OP_FLUSH:
        req->op = X_FLUSH;
        break;
    }
    reqdata->volname = s->volname;
    reqdata->offset = offset;
    reqdata->size = count;
    reqdata->bufidx = bufidx;
    reqdata->aio_cb = aio_cb;
    reqdata->segreq = segreq;
    reqdata->op = op;

    xseg_set_req_data(s->xseg, req, reqdata);
    if (op == ARCHIP_OP_WRITE) {
        data = xseg_get_data(s->xseg, req);
        if (!data) {
            archipelagolog("Cannot get XSEG data\n");
            goto err_exit;
        }
        qemu_iovec_to_buf(aio_cb->qiov, bufidx, data, count);
    }

    xport p = xseg_submit(s->xseg, req, s->srcport, X_ALLOC);
    if (p == NoPort) {
        archipelagolog("Could not submit XSEG request\n");
        goto err_exit;
    }
    xseg_signal(s->xseg, p);
    return 0;

err_exit:
    g_free(reqdata);
    xseg_put_request(s->xseg, req, s->srcport);
    return -EIO;
err_exit2:
    g_free(reqdata);
    return -EIO;
}

static int archipelago_aio_segmented_rw(BDRVArchipelagoState *s,
                                        size_t count,
                                        off_t offset,
                                        ArchipelagoAIOCB *aio_cb,
                                        int op)
{
    int i, ret, segments_nr, last_segment_size;
    ArchipelagoSegmentedRequest *segreq;

    segreq = g_malloc(sizeof(ArchipelagoSegmentedRequest));

    if (op == ARCHIP_OP_FLUSH) {
        segments_nr = 1;
        segreq->ref = segments_nr;
        segreq->total = count;
        segreq->count = 0;
        segreq->failed = 0;
        ret = archipelago_submit_request(s, 0, count, offset, aio_cb,
                                           segreq, ARCHIP_OP_FLUSH);
        if (ret < 0) {
            goto err_exit;
        }
        return 0;
    }

    segments_nr = (int)(count / MAX_REQUEST_SIZE) + \
                  ((count % MAX_REQUEST_SIZE) ? 1 : 0);
    last_segment_size = (int)(count % MAX_REQUEST_SIZE);

    segreq->ref = segments_nr;
    segreq->total = count;
    segreq->count = 0;
    segreq->failed = 0;

    for (i = 0; i < segments_nr - 1; i++) {
        ret = archipelago_submit_request(s, i * MAX_REQUEST_SIZE,
                                           MAX_REQUEST_SIZE,
                                           offset + i * MAX_REQUEST_SIZE,
                                           aio_cb, segreq, op);

        if (ret < 0) {
            goto err_exit;
        }
    }

    if ((segments_nr > 1) && last_segment_size) {
        ret = archipelago_submit_request(s, i * MAX_REQUEST_SIZE,
                                           last_segment_size,
                                           offset + i * MAX_REQUEST_SIZE,
                                           aio_cb, segreq, op);
    } else if ((segments_nr > 1) && !last_segment_size) {
        ret = archipelago_submit_request(s, i * MAX_REQUEST_SIZE,
                                           MAX_REQUEST_SIZE,
                                           offset + i * MAX_REQUEST_SIZE,
                                           aio_cb, segreq, op);
    } else if (segments_nr == 1) {
            ret = archipelago_submit_request(s, 0, count, offset, aio_cb,
                                               segreq, op);
    }

    if (ret < 0) {
        goto err_exit;
    }

    return 0;

err_exit:
    __sync_add_and_fetch(&segreq->failed, 1);
    if (segments_nr == 1) {
        if (__sync_add_and_fetch(&segreq->ref, -1) == 0) {
            g_free(segreq);
        }
    } else {
        if ((__sync_add_and_fetch(&segreq->ref, -segments_nr + i)) == 0) {
            g_free(segreq);
        }
    }

    return ret;
}

static BlockDriverAIOCB *qemu_archipelago_aio_rw(BlockDriverState *bs,
                                                 int64_t sector_num,
                                                 QEMUIOVector *qiov,
                                                 int nb_sectors,
                                                 BlockDriverCompletionFunc *cb,
                                                 void *opaque,
                                                 int op)
{
    ArchipelagoAIOCB *aio_cb;
    BDRVArchipelagoState *s = bs->opaque;
    int64_t size, off;
    int ret;

    aio_cb = qemu_aio_get(&archipelago_aiocb_info, bs, cb, opaque);
    aio_cb->cmd = op;
    aio_cb->qiov = qiov;

    aio_cb->ret = 0;
    aio_cb->s = s;
    aio_cb->cancelled = false;
    aio_cb->status = -EINPROGRESS;

    off = sector_num * BDRV_SECTOR_SIZE;
    size = nb_sectors * BDRV_SECTOR_SIZE;
    aio_cb->size = size;

    ret = archipelago_aio_segmented_rw(s, size, off,
                                       aio_cb, op);
    if (ret < 0) {
        goto err_exit;
    }
    return &aio_cb->common;

err_exit:
    error_report("qemu_archipelago_aio_rw(): I/O Error\n");
    qemu_aio_release(aio_cb);
    return NULL;
}

static BlockDriverAIOCB *qemu_archipelago_aio_readv(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return qemu_archipelago_aio_rw(bs, sector_num, qiov, nb_sectors, cb,
                                   opaque, ARCHIP_OP_READ);
}

static BlockDriverAIOCB *qemu_archipelago_aio_writev(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return qemu_archipelago_aio_rw(bs, sector_num, qiov, nb_sectors, cb,
                                   opaque, ARCHIP_OP_WRITE);
}

static int64_t archipelago_volume_info(BDRVArchipelagoState *s)
{
    uint64_t size;
    int ret, targetlen;
    struct xseg_request *req;
    struct xseg_reply_info *xinfo;
    AIORequestData *reqdata = g_malloc(sizeof(AIORequestData));

    const char *volname = s->volname;
    targetlen = strlen(volname);
    req = xseg_get_request(s->xseg, s->srcport, s->mportno, X_ALLOC);
    if (!req) {
        archipelagolog("Cannot get XSEG request\n");
        goto err_exit2;
    }
    ret = xseg_prep_request(s->xseg, req, targetlen,
                            sizeof(struct xseg_reply_info));
    if (ret < 0) {
        archipelagolog("Cannot prepare XSEG request\n");
        goto err_exit;
    }
    char *target = xseg_get_target(s->xseg, req);
    if (!target) {
        archipelagolog("Cannot get XSEG target\n");
        goto err_exit;
    }
    memcpy(target, volname, targetlen);
    req->size = req->datalen;
    req->offset = 0;
    req->op = X_INFO;

    reqdata->op = ARCHIP_OP_VOLINFO;
    reqdata->volname = volname;
    xseg_set_req_data(s->xseg, req, reqdata);

    xport p = xseg_submit(s->xseg, req, s->srcport, X_ALLOC);
    if (p == NoPort) {
        archipelagolog("Cannot submit XSEG request\n");
        goto err_exit;
    }
    xseg_signal(s->xseg, p);
    qemu_mutex_lock(&s->archip_mutex);
    while (!s->is_signaled) {
        qemu_cond_wait(&s->archip_cond, &s->archip_mutex);
    }
    s->is_signaled = false;
    qemu_mutex_unlock(&s->archip_mutex);

    xinfo = (struct xseg_reply_info *) xseg_get_data(s->xseg, req);
    size = xinfo->size;
    xseg_put_request(s->xseg, req, s->srcport);
    g_free(reqdata);
    s->size = size;
    return size;

err_exit:
    xseg_put_request(s->xseg, req, s->srcport);
err_exit2:
    g_free(reqdata);
    return -EIO;
}

static int64_t qemu_archipelago_getlength(BlockDriverState *bs)
{
    int64_t ret;
    BDRVArchipelagoState *s = bs->opaque;

    ret = archipelago_volume_info(s);
    return ret;
}

static QemuOptsList qemu_archipelago_create_opts = {
    .name = "archipelago-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_archipelago_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        { /* end of list */ }
    }
};

static BlockDriverAIOCB *qemu_archipelago_aio_flush(BlockDriverState *bs,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return qemu_archipelago_aio_rw(bs, 0, NULL, 0, cb, opaque,
                                   ARCHIP_OP_FLUSH);
}

static BlockDriver bdrv_archipelago = {
    .format_name         = "archipelago",
    .protocol_name       = "archipelago",
    .instance_size       = sizeof(BDRVArchipelagoState),
    .bdrv_parse_filename = archipelago_parse_filename,
    .bdrv_file_open      = qemu_archipelago_open,
    .bdrv_close          = qemu_archipelago_close,
    .bdrv_create         = qemu_archipelago_create,
    .bdrv_getlength      = qemu_archipelago_getlength,
    .bdrv_aio_readv      = qemu_archipelago_aio_readv,
    .bdrv_aio_writev     = qemu_archipelago_aio_writev,
    .bdrv_aio_flush      = qemu_archipelago_aio_flush,
    .bdrv_has_zero_init  = bdrv_has_zero_init_1,
    .create_opts         = &qemu_archipelago_create_opts,
};

static void bdrv_archipelago_init(void)
{
    bdrv_register(&bdrv_archipelago);
}

block_init(bdrv_archipelago_init);
