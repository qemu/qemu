/*
 * QEMU Block driver for CURL images
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "block/block_int.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qstring.h"
#include "crypto/secret.h"
#include <curl/curl.h>
#include "qemu/cutils.h"

// #define DEBUG_CURL
// #define DEBUG_VERBOSE

#ifdef DEBUG_CURL
#define DEBUG_CURL_PRINT 1
#else
#define DEBUG_CURL_PRINT 0
#endif
#define DPRINTF(fmt, ...)                                            \
    do {                                                             \
        if (DEBUG_CURL_PRINT) {                                      \
            fprintf(stderr, fmt, ## __VA_ARGS__);                    \
        }                                                            \
    } while (0)

#if LIBCURL_VERSION_NUM >= 0x071000
/* The multi interface timer callback was introduced in 7.16.0 */
#define NEED_CURL_TIMER_CALLBACK
#define HAVE_SOCKET_ACTION
#endif

#ifndef HAVE_SOCKET_ACTION
/* If curl_multi_socket_action isn't available, define it statically here in
 * terms of curl_multi_socket. Note that ev_bitmask will be ignored, which is
 * less efficient but still safe. */
static CURLMcode __curl_multi_socket_action(CURLM *multi_handle,
                                            curl_socket_t sockfd,
                                            int ev_bitmask,
                                            int *running_handles)
{
    return curl_multi_socket(multi_handle, sockfd, running_handles);
}
#define curl_multi_socket_action __curl_multi_socket_action
#endif

#define PROTOCOLS (CURLPROTO_HTTP | CURLPROTO_HTTPS | \
                   CURLPROTO_FTP | CURLPROTO_FTPS)

#define CURL_NUM_STATES 8
#define CURL_NUM_ACB    8
#define READ_AHEAD_DEFAULT (256 * 1024)
#define CURL_TIMEOUT_DEFAULT 5
#define CURL_TIMEOUT_MAX 10000

#define CURL_BLOCK_OPT_URL       "url"
#define CURL_BLOCK_OPT_READAHEAD "readahead"
#define CURL_BLOCK_OPT_SSLVERIFY "sslverify"
#define CURL_BLOCK_OPT_TIMEOUT "timeout"
#define CURL_BLOCK_OPT_COOKIE    "cookie"
#define CURL_BLOCK_OPT_COOKIE_SECRET "cookie-secret"
#define CURL_BLOCK_OPT_USERNAME "username"
#define CURL_BLOCK_OPT_PASSWORD_SECRET "password-secret"
#define CURL_BLOCK_OPT_PROXY_USERNAME "proxy-username"
#define CURL_BLOCK_OPT_PROXY_PASSWORD_SECRET "proxy-password-secret"

struct BDRVCURLState;

typedef struct CURLAIOCB {
    Coroutine *co;
    QEMUIOVector *qiov;

    uint64_t offset;
    uint64_t bytes;
    int ret;

    size_t start;
    size_t end;

    QSIMPLEQ_ENTRY(CURLAIOCB) next;
} CURLAIOCB;

typedef struct CURLSocket {
    int fd;
    QLIST_ENTRY(CURLSocket) next;
} CURLSocket;

typedef struct CURLState
{
    struct BDRVCURLState *s;
    CURLAIOCB *acb[CURL_NUM_ACB];
    CURL *curl;
    QLIST_HEAD(, CURLSocket) sockets;
    char *orig_buf;
    uint64_t buf_start;
    size_t buf_off;
    size_t buf_len;
    char range[128];
    char errmsg[CURL_ERROR_SIZE];
    char in_use;
} CURLState;

typedef struct BDRVCURLState {
    CURLM *multi;
    QEMUTimer timer;
    uint64_t len;
    CURLState states[CURL_NUM_STATES];
    char *url;
    size_t readahead_size;
    bool sslverify;
    uint64_t timeout;
    char *cookie;
    bool accept_range;
    AioContext *aio_context;
    QemuMutex mutex;
    QSIMPLEQ_HEAD(, CURLAIOCB) free_state_waitq;
    char *username;
    char *password;
    char *proxyusername;
    char *proxypassword;
} BDRVCURLState;

static void curl_clean_state(CURLState *s);
static void curl_multi_do(void *arg);
static void curl_multi_read(void *arg);

#ifdef NEED_CURL_TIMER_CALLBACK
/* Called from curl_multi_do_locked, with s->mutex held.  */
static int curl_timer_cb(CURLM *multi, long timeout_ms, void *opaque)
{
    BDRVCURLState *s = opaque;

    DPRINTF("CURL: timer callback timeout_ms %ld\n", timeout_ms);
    if (timeout_ms == -1) {
        timer_del(&s->timer);
    } else {
        int64_t timeout_ns = (int64_t)timeout_ms * 1000 * 1000;
        timer_mod(&s->timer,
                  qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + timeout_ns);
    }
    return 0;
}
#endif

/* Called from curl_multi_do_locked, with s->mutex held.  */
static int curl_sock_cb(CURL *curl, curl_socket_t fd, int action,
                        void *userp, void *sp)
{
    BDRVCURLState *s;
    CURLState *state = NULL;
    CURLSocket *socket;

    curl_easy_getinfo(curl, CURLINFO_PRIVATE, (char **)&state);
    s = state->s;

    QLIST_FOREACH(socket, &state->sockets, next) {
        if (socket->fd == fd) {
            if (action == CURL_POLL_REMOVE) {
                QLIST_REMOVE(socket, next);
                g_free(socket);
            }
            break;
        }
    }
    if (!socket) {
        socket = g_new0(CURLSocket, 1);
        socket->fd = fd;
        QLIST_INSERT_HEAD(&state->sockets, socket, next);
    }
    socket = NULL;

    DPRINTF("CURL (AIO): Sock action %d on fd %d\n", action, (int)fd);
    switch (action) {
        case CURL_POLL_IN:
            aio_set_fd_handler(s->aio_context, fd, false,
                               curl_multi_read, NULL, NULL, state);
            break;
        case CURL_POLL_OUT:
            aio_set_fd_handler(s->aio_context, fd, false,
                               NULL, curl_multi_do, NULL, state);
            break;
        case CURL_POLL_INOUT:
            aio_set_fd_handler(s->aio_context, fd, false,
                               curl_multi_read, curl_multi_do, NULL, state);
            break;
        case CURL_POLL_REMOVE:
            aio_set_fd_handler(s->aio_context, fd, false,
                               NULL, NULL, NULL, NULL);
            break;
    }

    return 0;
}

/* Called from curl_multi_do_locked, with s->mutex held.  */
static size_t curl_header_cb(void *ptr, size_t size, size_t nmemb, void *opaque)
{
    BDRVCURLState *s = opaque;
    size_t realsize = size * nmemb;
    const char *accept_line = "Accept-Ranges: bytes";

    if (realsize >= strlen(accept_line)
        && strncmp((char *)ptr, accept_line, strlen(accept_line)) == 0) {
        s->accept_range = true;
    }

    return realsize;
}

/* Called from curl_multi_do_locked, with s->mutex held.  */
static size_t curl_read_cb(void *ptr, size_t size, size_t nmemb, void *opaque)
{
    CURLState *s = ((CURLState*)opaque);
    size_t realsize = size * nmemb;
    int i;

    DPRINTF("CURL: Just reading %zd bytes\n", realsize);

    if (!s || !s->orig_buf) {
        goto read_end;
    }

    if (s->buf_off >= s->buf_len) {
        /* buffer full, read nothing */
        goto read_end;
    }
    realsize = MIN(realsize, s->buf_len - s->buf_off);
    memcpy(s->orig_buf + s->buf_off, ptr, realsize);
    s->buf_off += realsize;

    for(i=0; i<CURL_NUM_ACB; i++) {
        CURLAIOCB *acb = s->acb[i];

        if (!acb)
            continue;

        if ((s->buf_off >= acb->end)) {
            size_t request_length = acb->bytes;

            qemu_iovec_from_buf(acb->qiov, 0, s->orig_buf + acb->start,
                                acb->end - acb->start);

            if (acb->end - acb->start < request_length) {
                size_t offset = acb->end - acb->start;
                qemu_iovec_memset(acb->qiov, offset, 0,
                                  request_length - offset);
            }

            acb->ret = 0;
            s->acb[i] = NULL;
            qemu_mutex_unlock(&s->s->mutex);
            aio_co_wake(acb->co);
            qemu_mutex_lock(&s->s->mutex);
        }
    }

read_end:
    /* curl will error out if we do not return this value */
    return size * nmemb;
}

/* Called with s->mutex held.  */
static bool curl_find_buf(BDRVCURLState *s, uint64_t start, uint64_t len,
                          CURLAIOCB *acb)
{
    int i;
    uint64_t end = start + len;
    uint64_t clamped_end = MIN(end, s->len);
    uint64_t clamped_len = clamped_end - start;

    for (i=0; i<CURL_NUM_STATES; i++) {
        CURLState *state = &s->states[i];
        uint64_t buf_end = (state->buf_start + state->buf_off);
        uint64_t buf_fend = (state->buf_start + state->buf_len);

        if (!state->orig_buf)
            continue;
        if (!state->buf_off)
            continue;

        // Does the existing buffer cover our section?
        if ((start >= state->buf_start) &&
            (start <= buf_end) &&
            (clamped_end >= state->buf_start) &&
            (clamped_end <= buf_end))
        {
            char *buf = state->orig_buf + (start - state->buf_start);

            qemu_iovec_from_buf(acb->qiov, 0, buf, clamped_len);
            if (clamped_len < len) {
                qemu_iovec_memset(acb->qiov, clamped_len, 0, len - clamped_len);
            }
            acb->ret = 0;
            return true;
        }

        // Wait for unfinished chunks
        if (state->in_use &&
            (start >= state->buf_start) &&
            (start <= buf_fend) &&
            (clamped_end >= state->buf_start) &&
            (clamped_end <= buf_fend))
        {
            int j;

            acb->start = start - state->buf_start;
            acb->end = acb->start + clamped_len;

            for (j=0; j<CURL_NUM_ACB; j++) {
                if (!state->acb[j]) {
                    state->acb[j] = acb;
                    return true;
                }
            }
        }
    }

    return false;
}

/* Called with s->mutex held.  */
static void curl_multi_check_completion(BDRVCURLState *s)
{
    int msgs_in_queue;

    /* Try to find done transfers, so we can free the easy
     * handle again. */
    for (;;) {
        CURLMsg *msg;
        msg = curl_multi_info_read(s->multi, &msgs_in_queue);

        /* Quit when there are no more completions */
        if (!msg)
            break;

        if (msg->msg == CURLMSG_DONE) {
            CURLState *state = NULL;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE,
                              (char **)&state);

            /* ACBs for successful messages get completed in curl_read_cb */
            if (msg->data.result != CURLE_OK) {
                int i;
                static int errcount = 100;

                /* Don't lose the original error message from curl, since
                 * it contains extra data.
                 */
                if (errcount > 0) {
                    error_report("curl: %s", state->errmsg);
                    if (--errcount == 0) {
                        error_report("curl: further errors suppressed");
                    }
                }

                for (i = 0; i < CURL_NUM_ACB; i++) {
                    CURLAIOCB *acb = state->acb[i];

                    if (acb == NULL) {
                        continue;
                    }

                    acb->ret = -EIO;
                    state->acb[i] = NULL;
                    qemu_mutex_unlock(&s->mutex);
                    aio_co_wake(acb->co);
                    qemu_mutex_lock(&s->mutex);
                }
            }

            curl_clean_state(state);
            break;
        }
    }
}

/* Called with s->mutex held.  */
static void curl_multi_do_locked(CURLState *s)
{
    CURLSocket *socket, *next_socket;
    int running;
    int r;

    if (!s->s->multi) {
        return;
    }

    /* Need to use _SAFE because curl_multi_socket_action() may trigger
     * curl_sock_cb() which might modify this list */
    QLIST_FOREACH_SAFE(socket, &s->sockets, next, next_socket) {
        do {
            r = curl_multi_socket_action(s->s->multi, socket->fd, 0, &running);
        } while (r == CURLM_CALL_MULTI_PERFORM);
    }
}

static void curl_multi_do(void *arg)
{
    CURLState *s = (CURLState *)arg;

    qemu_mutex_lock(&s->s->mutex);
    curl_multi_do_locked(s);
    qemu_mutex_unlock(&s->s->mutex);
}

static void curl_multi_read(void *arg)
{
    CURLState *s = (CURLState *)arg;

    qemu_mutex_lock(&s->s->mutex);
    curl_multi_do_locked(s);
    curl_multi_check_completion(s->s);
    qemu_mutex_unlock(&s->s->mutex);
}

static void curl_multi_timeout_do(void *arg)
{
#ifdef NEED_CURL_TIMER_CALLBACK
    BDRVCURLState *s = (BDRVCURLState *)arg;
    int running;

    if (!s->multi) {
        return;
    }

    qemu_mutex_lock(&s->mutex);
    curl_multi_socket_action(s->multi, CURL_SOCKET_TIMEOUT, 0, &running);

    curl_multi_check_completion(s);
    qemu_mutex_unlock(&s->mutex);
#else
    abort();
#endif
}

/* Called with s->mutex held.  */
static CURLState *curl_find_state(BDRVCURLState *s)
{
    CURLState *state = NULL;
    int i;

    for (i = 0; i < CURL_NUM_STATES; i++) {
        if (!s->states[i].in_use) {
            state = &s->states[i];
            state->in_use = 1;
            break;
        }
    }
    return state;
}

static int curl_init_state(BDRVCURLState *s, CURLState *state)
{
    if (!state->curl) {
        state->curl = curl_easy_init();
        if (!state->curl) {
            return -EIO;
        }
        curl_easy_setopt(state->curl, CURLOPT_URL, s->url);
        curl_easy_setopt(state->curl, CURLOPT_SSL_VERIFYPEER,
                         (long) s->sslverify);
        if (s->cookie) {
            curl_easy_setopt(state->curl, CURLOPT_COOKIE, s->cookie);
        }
        curl_easy_setopt(state->curl, CURLOPT_TIMEOUT, (long)s->timeout);
        curl_easy_setopt(state->curl, CURLOPT_WRITEFUNCTION,
                         (void *)curl_read_cb);
        curl_easy_setopt(state->curl, CURLOPT_WRITEDATA, (void *)state);
        curl_easy_setopt(state->curl, CURLOPT_PRIVATE, (void *)state);
        curl_easy_setopt(state->curl, CURLOPT_AUTOREFERER, 1);
        curl_easy_setopt(state->curl, CURLOPT_FOLLOWLOCATION, 1);
        curl_easy_setopt(state->curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(state->curl, CURLOPT_ERRORBUFFER, state->errmsg);
        curl_easy_setopt(state->curl, CURLOPT_FAILONERROR, 1);

        if (s->username) {
            curl_easy_setopt(state->curl, CURLOPT_USERNAME, s->username);
        }
        if (s->password) {
            curl_easy_setopt(state->curl, CURLOPT_PASSWORD, s->password);
        }
        if (s->proxyusername) {
            curl_easy_setopt(state->curl,
                             CURLOPT_PROXYUSERNAME, s->proxyusername);
        }
        if (s->proxypassword) {
            curl_easy_setopt(state->curl,
                             CURLOPT_PROXYPASSWORD, s->proxypassword);
        }

        /* Restrict supported protocols to avoid security issues in the more
         * obscure protocols.  For example, do not allow POP3/SMTP/IMAP see
         * CVE-2013-0249.
         *
         * Restricting protocols is only supported from 7.19.4 upwards.
         */
#if LIBCURL_VERSION_NUM >= 0x071304
        curl_easy_setopt(state->curl, CURLOPT_PROTOCOLS, PROTOCOLS);
        curl_easy_setopt(state->curl, CURLOPT_REDIR_PROTOCOLS, PROTOCOLS);
#endif

#ifdef DEBUG_VERBOSE
        curl_easy_setopt(state->curl, CURLOPT_VERBOSE, 1);
#endif
    }

    QLIST_INIT(&state->sockets);
    state->s = s;

    return 0;
}

/* Called with s->mutex held.  */
static void curl_clean_state(CURLState *s)
{
    CURLAIOCB *next;
    int j;
    for (j = 0; j < CURL_NUM_ACB; j++) {
        assert(!s->acb[j]);
    }

    if (s->s->multi)
        curl_multi_remove_handle(s->s->multi, s->curl);

    while (!QLIST_EMPTY(&s->sockets)) {
        CURLSocket *socket = QLIST_FIRST(&s->sockets);

        QLIST_REMOVE(socket, next);
        g_free(socket);
    }

    s->in_use = 0;

    next = QSIMPLEQ_FIRST(&s->s->free_state_waitq);
    if (next) {
        QSIMPLEQ_REMOVE_HEAD(&s->s->free_state_waitq, next);
        qemu_mutex_unlock(&s->s->mutex);
        aio_co_wake(next->co);
        qemu_mutex_lock(&s->s->mutex);
    }
}

static void curl_parse_filename(const char *filename, QDict *options,
                                Error **errp)
{
    qdict_put_str(options, CURL_BLOCK_OPT_URL, filename);
}

static void curl_detach_aio_context(BlockDriverState *bs)
{
    BDRVCURLState *s = bs->opaque;
    int i;

    qemu_mutex_lock(&s->mutex);
    for (i = 0; i < CURL_NUM_STATES; i++) {
        if (s->states[i].in_use) {
            curl_clean_state(&s->states[i]);
        }
        if (s->states[i].curl) {
            curl_easy_cleanup(s->states[i].curl);
            s->states[i].curl = NULL;
        }
        g_free(s->states[i].orig_buf);
        s->states[i].orig_buf = NULL;
    }
    if (s->multi) {
        curl_multi_cleanup(s->multi);
        s->multi = NULL;
    }
    qemu_mutex_unlock(&s->mutex);

    timer_del(&s->timer);
}

static void curl_attach_aio_context(BlockDriverState *bs,
                                    AioContext *new_context)
{
    BDRVCURLState *s = bs->opaque;

    aio_timer_init(new_context, &s->timer,
                   QEMU_CLOCK_REALTIME, SCALE_NS,
                   curl_multi_timeout_do, s);

    assert(!s->multi);
    s->multi = curl_multi_init();
    s->aio_context = new_context;
    curl_multi_setopt(s->multi, CURLMOPT_SOCKETFUNCTION, curl_sock_cb);
#ifdef NEED_CURL_TIMER_CALLBACK
    curl_multi_setopt(s->multi, CURLMOPT_TIMERDATA, s);
    curl_multi_setopt(s->multi, CURLMOPT_TIMERFUNCTION, curl_timer_cb);
#endif
}

static QemuOptsList runtime_opts = {
    .name = "curl",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = CURL_BLOCK_OPT_URL,
            .type = QEMU_OPT_STRING,
            .help = "URL to open",
        },
        {
            .name = CURL_BLOCK_OPT_READAHEAD,
            .type = QEMU_OPT_SIZE,
            .help = "Readahead size",
        },
        {
            .name = CURL_BLOCK_OPT_SSLVERIFY,
            .type = QEMU_OPT_BOOL,
            .help = "Verify SSL certificate"
        },
        {
            .name = CURL_BLOCK_OPT_TIMEOUT,
            .type = QEMU_OPT_NUMBER,
            .help = "Curl timeout"
        },
        {
            .name = CURL_BLOCK_OPT_COOKIE,
            .type = QEMU_OPT_STRING,
            .help = "Pass the cookie or list of cookies with each request"
        },
        {
            .name = CURL_BLOCK_OPT_COOKIE_SECRET,
            .type = QEMU_OPT_STRING,
            .help = "ID of secret used as cookie passed with each request"
        },
        {
            .name = CURL_BLOCK_OPT_USERNAME,
            .type = QEMU_OPT_STRING,
            .help = "Username for HTTP auth"
        },
        {
            .name = CURL_BLOCK_OPT_PASSWORD_SECRET,
            .type = QEMU_OPT_STRING,
            .help = "ID of secret used as password for HTTP auth",
        },
        {
            .name = CURL_BLOCK_OPT_PROXY_USERNAME,
            .type = QEMU_OPT_STRING,
            .help = "Username for HTTP proxy auth"
        },
        {
            .name = CURL_BLOCK_OPT_PROXY_PASSWORD_SECRET,
            .type = QEMU_OPT_STRING,
            .help = "ID of secret used as password for HTTP proxy auth",
        },
        { /* end of list */ }
    },
};


static int curl_open(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp)
{
    BDRVCURLState *s = bs->opaque;
    CURLState *state = NULL;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *file;
    const char *cookie;
    const char *cookie_secret;
    double d;
    const char *secretid;
    const char *protocol_delimiter;

    static int inited = 0;

    if (flags & BDRV_O_RDWR) {
        error_setg(errp, "curl block device does not support writes");
        return -EROFS;
    }

    qemu_mutex_init(&s->mutex);
    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out_noclean;
    }

    s->readahead_size = qemu_opt_get_size(opts, CURL_BLOCK_OPT_READAHEAD,
                                          READ_AHEAD_DEFAULT);
    if ((s->readahead_size & 0x1ff) != 0) {
        error_setg(errp, "HTTP_READAHEAD_SIZE %zd is not a multiple of 512",
                   s->readahead_size);
        goto out_noclean;
    }

    s->timeout = qemu_opt_get_number(opts, CURL_BLOCK_OPT_TIMEOUT,
                                     CURL_TIMEOUT_DEFAULT);
    if (s->timeout > CURL_TIMEOUT_MAX) {
        error_setg(errp, "timeout parameter is too large or negative");
        goto out_noclean;
    }

    s->sslverify = qemu_opt_get_bool(opts, CURL_BLOCK_OPT_SSLVERIFY, true);

    cookie = qemu_opt_get(opts, CURL_BLOCK_OPT_COOKIE);
    cookie_secret = qemu_opt_get(opts, CURL_BLOCK_OPT_COOKIE_SECRET);

    if (cookie && cookie_secret) {
        error_setg(errp,
                   "curl driver cannot handle both cookie and cookie secret");
        goto out_noclean;
    }

    if (cookie_secret) {
        s->cookie = qcrypto_secret_lookup_as_utf8(cookie_secret, errp);
        if (!s->cookie) {
            goto out_noclean;
        }
    } else {
        s->cookie = g_strdup(cookie);
    }

    file = qemu_opt_get(opts, CURL_BLOCK_OPT_URL);
    if (file == NULL) {
        error_setg(errp, "curl block driver requires an 'url' option");
        goto out_noclean;
    }

    if (!strstart(file, bs->drv->protocol_name, &protocol_delimiter) ||
        !strstart(protocol_delimiter, "://", NULL))
    {
        error_setg(errp, "%s curl driver cannot handle the URL '%s' (does not "
                   "start with '%s://')", bs->drv->protocol_name, file,
                   bs->drv->protocol_name);
        goto out_noclean;
    }

    s->username = g_strdup(qemu_opt_get(opts, CURL_BLOCK_OPT_USERNAME));
    secretid = qemu_opt_get(opts, CURL_BLOCK_OPT_PASSWORD_SECRET);

    if (secretid) {
        s->password = qcrypto_secret_lookup_as_utf8(secretid, errp);
        if (!s->password) {
            goto out_noclean;
        }
    }

    s->proxyusername = g_strdup(
        qemu_opt_get(opts, CURL_BLOCK_OPT_PROXY_USERNAME));
    secretid = qemu_opt_get(opts, CURL_BLOCK_OPT_PROXY_PASSWORD_SECRET);
    if (secretid) {
        s->proxypassword = qcrypto_secret_lookup_as_utf8(secretid, errp);
        if (!s->proxypassword) {
            goto out_noclean;
        }
    }

    if (!inited) {
        curl_global_init(CURL_GLOBAL_ALL);
        inited = 1;
    }

    DPRINTF("CURL: Opening %s\n", file);
    QSIMPLEQ_INIT(&s->free_state_waitq);
    s->aio_context = bdrv_get_aio_context(bs);
    s->url = g_strdup(file);
    qemu_mutex_lock(&s->mutex);
    state = curl_find_state(s);
    qemu_mutex_unlock(&s->mutex);
    if (!state) {
        goto out_noclean;
    }

    // Get file size

    if (curl_init_state(s, state) < 0) {
        goto out;
    }

    s->accept_range = false;
    curl_easy_setopt(state->curl, CURLOPT_NOBODY, 1);
    curl_easy_setopt(state->curl, CURLOPT_HEADERFUNCTION,
                     curl_header_cb);
    curl_easy_setopt(state->curl, CURLOPT_HEADERDATA, s);
    if (curl_easy_perform(state->curl))
        goto out;
    if (curl_easy_getinfo(state->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &d)) {
        goto out;
    }
    /* Prior CURL 7.19.4 return value of 0 could mean that the file size is not
     * know or the size is zero. From 7.19.4 CURL returns -1 if size is not
     * known and zero if it is realy zero-length file. */
#if LIBCURL_VERSION_NUM >= 0x071304
    if (d < 0) {
        pstrcpy(state->errmsg, CURL_ERROR_SIZE,
                "Server didn't report file size.");
        goto out;
    }
#else
    if (d <= 0) {
        pstrcpy(state->errmsg, CURL_ERROR_SIZE,
                "Unknown file size or zero-length file.");
        goto out;
    }
#endif

    s->len = d;

    if ((!strncasecmp(s->url, "http://", strlen("http://"))
        || !strncasecmp(s->url, "https://", strlen("https://")))
        && !s->accept_range) {
        pstrcpy(state->errmsg, CURL_ERROR_SIZE,
                "Server does not support 'range' (byte ranges).");
        goto out;
    }
    DPRINTF("CURL: Size = %" PRIu64 "\n", s->len);

    qemu_mutex_lock(&s->mutex);
    curl_clean_state(state);
    qemu_mutex_unlock(&s->mutex);
    curl_easy_cleanup(state->curl);
    state->curl = NULL;

    curl_attach_aio_context(bs, bdrv_get_aio_context(bs));

    qemu_opts_del(opts);
    return 0;

out:
    error_setg(errp, "CURL: Error opening file: %s", state->errmsg);
    curl_easy_cleanup(state->curl);
    state->curl = NULL;
out_noclean:
    qemu_mutex_destroy(&s->mutex);
    g_free(s->cookie);
    g_free(s->url);
    qemu_opts_del(opts);
    return -EINVAL;
}

static void curl_setup_preadv(BlockDriverState *bs, CURLAIOCB *acb)
{
    CURLState *state;
    int running;

    BDRVCURLState *s = bs->opaque;

    uint64_t start = acb->offset;
    uint64_t end;

    qemu_mutex_lock(&s->mutex);

    // In case we have the requested data already (e.g. read-ahead),
    // we can just call the callback and be done.
    if (curl_find_buf(s, start, acb->bytes, acb)) {
        goto out;
    }

    // No cache found, so let's start a new request
    for (;;) {
        state = curl_find_state(s);
        if (state) {
            break;
        }
        QSIMPLEQ_INSERT_TAIL(&s->free_state_waitq, acb, next);
        qemu_mutex_unlock(&s->mutex);
        qemu_coroutine_yield();
        qemu_mutex_lock(&s->mutex);
    }

    if (curl_init_state(s, state) < 0) {
        curl_clean_state(state);
        acb->ret = -EIO;
        goto out;
    }

    acb->start = 0;
    acb->end = MIN(acb->bytes, s->len - start);

    state->buf_off = 0;
    g_free(state->orig_buf);
    state->buf_start = start;
    state->buf_len = MIN(acb->end + s->readahead_size, s->len - start);
    end = start + state->buf_len - 1;
    state->orig_buf = g_try_malloc(state->buf_len);
    if (state->buf_len && state->orig_buf == NULL) {
        curl_clean_state(state);
        acb->ret = -ENOMEM;
        goto out;
    }
    state->acb[0] = acb;

    snprintf(state->range, 127, "%" PRIu64 "-%" PRIu64, start, end);
    DPRINTF("CURL (AIO): Reading %" PRIu64 " at %" PRIu64 " (%s)\n",
            acb->bytes, start, state->range);
    curl_easy_setopt(state->curl, CURLOPT_RANGE, state->range);

    curl_multi_add_handle(s->multi, state->curl);

    /* Tell curl it needs to kick things off */
    curl_multi_socket_action(s->multi, CURL_SOCKET_TIMEOUT, 0, &running);

out:
    qemu_mutex_unlock(&s->mutex);
}

static int coroutine_fn curl_co_preadv(BlockDriverState *bs,
        uint64_t offset, uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    CURLAIOCB acb = {
        .co = qemu_coroutine_self(),
        .ret = -EINPROGRESS,
        .qiov = qiov,
        .offset = offset,
        .bytes = bytes
    };

    curl_setup_preadv(bs, &acb);
    while (acb.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }
    return acb.ret;
}

static void curl_close(BlockDriverState *bs)
{
    BDRVCURLState *s = bs->opaque;

    DPRINTF("CURL: Close\n");
    curl_detach_aio_context(bs);
    qemu_mutex_destroy(&s->mutex);

    g_free(s->cookie);
    g_free(s->url);
}

static int64_t curl_getlength(BlockDriverState *bs)
{
    BDRVCURLState *s = bs->opaque;
    return s->len;
}

static BlockDriver bdrv_http = {
    .format_name                = "http",
    .protocol_name              = "http",

    .instance_size              = sizeof(BDRVCURLState),
    .bdrv_parse_filename        = curl_parse_filename,
    .bdrv_file_open             = curl_open,
    .bdrv_close                 = curl_close,
    .bdrv_getlength             = curl_getlength,

    .bdrv_co_preadv             = curl_co_preadv,

    .bdrv_detach_aio_context    = curl_detach_aio_context,
    .bdrv_attach_aio_context    = curl_attach_aio_context,
};

static BlockDriver bdrv_https = {
    .format_name                = "https",
    .protocol_name              = "https",

    .instance_size              = sizeof(BDRVCURLState),
    .bdrv_parse_filename        = curl_parse_filename,
    .bdrv_file_open             = curl_open,
    .bdrv_close                 = curl_close,
    .bdrv_getlength             = curl_getlength,

    .bdrv_co_preadv             = curl_co_preadv,

    .bdrv_detach_aio_context    = curl_detach_aio_context,
    .bdrv_attach_aio_context    = curl_attach_aio_context,
};

static BlockDriver bdrv_ftp = {
    .format_name                = "ftp",
    .protocol_name              = "ftp",

    .instance_size              = sizeof(BDRVCURLState),
    .bdrv_parse_filename        = curl_parse_filename,
    .bdrv_file_open             = curl_open,
    .bdrv_close                 = curl_close,
    .bdrv_getlength             = curl_getlength,

    .bdrv_co_preadv             = curl_co_preadv,

    .bdrv_detach_aio_context    = curl_detach_aio_context,
    .bdrv_attach_aio_context    = curl_attach_aio_context,
};

static BlockDriver bdrv_ftps = {
    .format_name                = "ftps",
    .protocol_name              = "ftps",

    .instance_size              = sizeof(BDRVCURLState),
    .bdrv_parse_filename        = curl_parse_filename,
    .bdrv_file_open             = curl_open,
    .bdrv_close                 = curl_close,
    .bdrv_getlength             = curl_getlength,

    .bdrv_co_preadv             = curl_co_preadv,

    .bdrv_detach_aio_context    = curl_detach_aio_context,
    .bdrv_attach_aio_context    = curl_attach_aio_context,
};

static void curl_block_init(void)
{
    bdrv_register(&bdrv_http);
    bdrv_register(&bdrv_https);
    bdrv_register(&bdrv_ftp);
    bdrv_register(&bdrv_ftps);
}

block_init(curl_block_init);
