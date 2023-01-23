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
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "block/block_int.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "crypto/secret.h"
#include <curl/curl.h>
#include "qemu/cutils.h"
#include "trace.h"

// #define DEBUG_VERBOSE

/* CURL 7.85.0 switches to a string based API for specifying
 * the desired protocols.
 */
#if LIBCURL_VERSION_NUM >= 0x075500
#define PROTOCOLS "HTTP,HTTPS,FTP,FTPS"
#else
#define PROTOCOLS (CURLPROTO_HTTP | CURLPROTO_HTTPS | \
                   CURLPROTO_FTP | CURLPROTO_FTPS)
#endif

#define CURL_NUM_STATES 8
#define CURL_NUM_ACB    8
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

#define CURL_BLOCK_OPT_READAHEAD_DEFAULT (256 * 1024)
#define CURL_BLOCK_OPT_SSLVERIFY_DEFAULT true
#define CURL_BLOCK_OPT_TIMEOUT_DEFAULT 5

struct BDRVCURLState;
struct CURLState;

static bool libcurl_initialized;

typedef struct CURLAIOCB {
    Coroutine *co;
    QEMUIOVector *qiov;

    uint64_t offset;
    uint64_t bytes;
    int ret;

    size_t start;
    size_t end;
} CURLAIOCB;

typedef struct CURLSocket {
    int fd;
    struct BDRVCURLState *s;
} CURLSocket;

typedef struct CURLState
{
    struct BDRVCURLState *s;
    CURLAIOCB *acb[CURL_NUM_ACB];
    CURL *curl;
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
    GHashTable *sockets; /* GINT_TO_POINTER(fd) -> socket */
    char *url;
    size_t readahead_size;
    bool sslverify;
    uint64_t timeout;
    char *cookie;
    bool accept_range;
    AioContext *aio_context;
    QemuMutex mutex;
    CoQueue free_state_waitq;
    char *username;
    char *password;
    char *proxyusername;
    char *proxypassword;
} BDRVCURLState;

static void curl_clean_state(CURLState *s);
static void curl_multi_do(void *arg);

static gboolean curl_drop_socket(void *key, void *value, void *opaque)
{
    CURLSocket *socket = value;
    BDRVCURLState *s = socket->s;

    aio_set_fd_handler(s->aio_context, socket->fd, false,
                       NULL, NULL, NULL, NULL, NULL);
    return true;
}

static void curl_drop_all_sockets(GHashTable *sockets)
{
    g_hash_table_foreach_remove(sockets, curl_drop_socket, NULL);
}

/* Called from curl_multi_do_locked, with s->mutex held.  */
static int curl_timer_cb(CURLM *multi, long timeout_ms, void *opaque)
{
    BDRVCURLState *s = opaque;

    trace_curl_timer_cb(timeout_ms);
    if (timeout_ms == -1) {
        timer_del(&s->timer);
    } else {
        int64_t timeout_ns = (int64_t)timeout_ms * 1000 * 1000;
        timer_mod(&s->timer,
                  qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + timeout_ns);
    }
    return 0;
}

/* Called from curl_multi_do_locked, with s->mutex held.  */
static int curl_sock_cb(CURL *curl, curl_socket_t fd, int action,
                        void *userp, void *sp)
{
    BDRVCURLState *s;
    CURLState *state = NULL;
    CURLSocket *socket;

    curl_easy_getinfo(curl, CURLINFO_PRIVATE, (char **)&state);
    s = state->s;

    socket = g_hash_table_lookup(s->sockets, GINT_TO_POINTER(fd));
    if (!socket) {
        socket = g_new0(CURLSocket, 1);
        socket->fd = fd;
        socket->s = s;
        g_hash_table_insert(s->sockets, GINT_TO_POINTER(fd), socket);
    }

    trace_curl_sock_cb(action, (int)fd);
    switch (action) {
        case CURL_POLL_IN:
            aio_set_fd_handler(s->aio_context, fd, false,
                               curl_multi_do, NULL, NULL, NULL, socket);
            break;
        case CURL_POLL_OUT:
            aio_set_fd_handler(s->aio_context, fd, false,
                               NULL, curl_multi_do, NULL, NULL, socket);
            break;
        case CURL_POLL_INOUT:
            aio_set_fd_handler(s->aio_context, fd, false,
                               curl_multi_do, curl_multi_do,
                               NULL, NULL, socket);
            break;
        case CURL_POLL_REMOVE:
            aio_set_fd_handler(s->aio_context, fd, false,
                               NULL, NULL, NULL, NULL, NULL);
            break;
    }

    if (action == CURL_POLL_REMOVE) {
        g_hash_table_remove(s->sockets, GINT_TO_POINTER(fd));
    }

    return 0;
}

/* Called from curl_multi_do_locked, with s->mutex held.  */
static size_t curl_header_cb(void *ptr, size_t size, size_t nmemb, void *opaque)
{
    BDRVCURLState *s = opaque;
    size_t realsize = size * nmemb;
    const char *header = (char *)ptr;
    const char *end = header + realsize;
    const char *accept_ranges = "accept-ranges:";
    const char *bytes = "bytes";

    if (realsize >= strlen(accept_ranges)
        && g_ascii_strncasecmp(header, accept_ranges,
                               strlen(accept_ranges)) == 0) {

        char *p = strchr(header, ':') + 1;

        /* Skip whitespace between the header name and value. */
        while (p < end && *p && g_ascii_isspace(*p)) {
            p++;
        }

        if (end - p >= strlen(bytes)
            && strncmp(p, bytes, strlen(bytes)) == 0) {

            /* Check that there is nothing but whitespace after the value. */
            p += strlen(bytes);
            while (p < end && *p && g_ascii_isspace(*p)) {
                p++;
            }

            if (p == end || !*p) {
                s->accept_range = true;
            }
        }
    }

    return realsize;
}

/* Called from curl_multi_do_locked, with s->mutex held.  */
static size_t curl_read_cb(void *ptr, size_t size, size_t nmemb, void *opaque)
{
    CURLState *s = ((CURLState*)opaque);
    size_t realsize = size * nmemb;

    trace_curl_read_cb(realsize);

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
            int i;
            CURLState *state = NULL;
            bool error = msg->data.result != CURLE_OK;

            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE,
                              (char **)&state);

            if (error) {
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
            }

            for (i = 0; i < CURL_NUM_ACB; i++) {
                CURLAIOCB *acb = state->acb[i];

                if (acb == NULL) {
                    continue;
                }

                if (!error) {
                    /* Assert that we have read all data */
                    assert(state->buf_off >= acb->end);

                    qemu_iovec_from_buf(acb->qiov, 0,
                                        state->orig_buf + acb->start,
                                        acb->end - acb->start);

                    if (acb->end - acb->start < acb->bytes) {
                        size_t offset = acb->end - acb->start;
                        qemu_iovec_memset(acb->qiov, offset, 0,
                                          acb->bytes - offset);
                    }
                }

                acb->ret = error ? -EIO : 0;
                state->acb[i] = NULL;
                qemu_mutex_unlock(&s->mutex);
                aio_co_wake(acb->co);
                qemu_mutex_lock(&s->mutex);
            }

            curl_clean_state(state);
            break;
        }
    }
}

/* Called with s->mutex held.  */
static void curl_multi_do_locked(CURLSocket *socket)
{
    BDRVCURLState *s = socket->s;
    int running;
    int r;

    if (!s->multi) {
        return;
    }

    do {
        r = curl_multi_socket_action(s->multi, socket->fd, 0, &running);
    } while (r == CURLM_CALL_MULTI_PERFORM);
}

static void curl_multi_do(void *arg)
{
    CURLSocket *socket = arg;
    BDRVCURLState *s = socket->s;

    qemu_mutex_lock(&s->mutex);
    curl_multi_do_locked(socket);
    curl_multi_check_completion(s);
    qemu_mutex_unlock(&s->mutex);
}

static void curl_multi_timeout_do(void *arg)
{
    BDRVCURLState *s = (BDRVCURLState *)arg;
    int running;

    if (!s->multi) {
        return;
    }

    qemu_mutex_lock(&s->mutex);
    curl_multi_socket_action(s->multi, CURL_SOCKET_TIMEOUT, 0, &running);

    curl_multi_check_completion(s);
    qemu_mutex_unlock(&s->mutex);
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
        if (curl_easy_setopt(state->curl, CURLOPT_URL, s->url) ||
            curl_easy_setopt(state->curl, CURLOPT_SSL_VERIFYPEER,
                             (long) s->sslverify) ||
            curl_easy_setopt(state->curl, CURLOPT_SSL_VERIFYHOST,
                             s->sslverify ? 2L : 0L)) {
            goto err;
        }
        if (s->cookie) {
            if (curl_easy_setopt(state->curl, CURLOPT_COOKIE, s->cookie)) {
                goto err;
            }
        }
        if (curl_easy_setopt(state->curl, CURLOPT_TIMEOUT, (long)s->timeout) ||
            curl_easy_setopt(state->curl, CURLOPT_WRITEFUNCTION,
                             (void *)curl_read_cb) ||
            curl_easy_setopt(state->curl, CURLOPT_WRITEDATA, (void *)state) ||
            curl_easy_setopt(state->curl, CURLOPT_PRIVATE, (void *)state) ||
            curl_easy_setopt(state->curl, CURLOPT_AUTOREFERER, 1) ||
            curl_easy_setopt(state->curl, CURLOPT_FOLLOWLOCATION, 1) ||
            curl_easy_setopt(state->curl, CURLOPT_NOSIGNAL, 1) ||
            curl_easy_setopt(state->curl, CURLOPT_ERRORBUFFER, state->errmsg) ||
            curl_easy_setopt(state->curl, CURLOPT_FAILONERROR, 1)) {
            goto err;
        }
        if (s->username) {
            if (curl_easy_setopt(state->curl, CURLOPT_USERNAME, s->username)) {
                goto err;
            }
        }
        if (s->password) {
            if (curl_easy_setopt(state->curl, CURLOPT_PASSWORD, s->password)) {
                goto err;
            }
        }
        if (s->proxyusername) {
            if (curl_easy_setopt(state->curl,
                                 CURLOPT_PROXYUSERNAME, s->proxyusername)) {
                goto err;
            }
        }
        if (s->proxypassword) {
            if (curl_easy_setopt(state->curl,
                                 CURLOPT_PROXYPASSWORD, s->proxypassword)) {
                goto err;
            }
        }

        /* Restrict supported protocols to avoid security issues in the more
         * obscure protocols.  For example, do not allow POP3/SMTP/IMAP see
         * CVE-2013-0249.
         *
         * Restricting protocols is only supported from 7.19.4 upwards. Note:
         * version 7.85.0 deprecates CURLOPT_*PROTOCOLS in favour of a string
         * based CURLOPT_*PROTOCOLS_STR API.
         */
#if LIBCURL_VERSION_NUM >= 0x075500
        if (curl_easy_setopt(state->curl,
                             CURLOPT_PROTOCOLS_STR, PROTOCOLS) ||
            curl_easy_setopt(state->curl,
                             CURLOPT_REDIR_PROTOCOLS_STR, PROTOCOLS)) {
            goto err;
        }
#elif LIBCURL_VERSION_NUM >= 0x071304
        if (curl_easy_setopt(state->curl, CURLOPT_PROTOCOLS, PROTOCOLS) ||
            curl_easy_setopt(state->curl, CURLOPT_REDIR_PROTOCOLS, PROTOCOLS)) {
            goto err;
        }
#endif

#ifdef DEBUG_VERBOSE
        if (curl_easy_setopt(state->curl, CURLOPT_VERBOSE, 1)) {
            goto err;
        }
#endif
    }

    state->s = s;

    return 0;

err:
    curl_easy_cleanup(state->curl);
    state->curl = NULL;
    return -EIO;
}

/* Called with s->mutex held.  */
static void curl_clean_state(CURLState *s)
{
    int j;
    for (j = 0; j < CURL_NUM_ACB; j++) {
        assert(!s->acb[j]);
    }

    if (s->s->multi)
        curl_multi_remove_handle(s->s->multi, s->curl);

    s->in_use = 0;

    qemu_co_enter_next(&s->s->free_state_waitq, &s->s->mutex);
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

    WITH_QEMU_LOCK_GUARD(&s->mutex) {
        curl_drop_all_sockets(s->sockets);
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
    }

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
    curl_multi_setopt(s->multi, CURLMOPT_TIMERDATA, s);
    curl_multi_setopt(s->multi, CURLMOPT_TIMERFUNCTION, curl_timer_cb);
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
    const char *file;
    const char *cookie;
    const char *cookie_secret;
    /* CURL >= 7.55.0 uses curl_off_t for content length instead of a double */
#if LIBCURL_VERSION_NUM >= 0x073700
    curl_off_t cl;
#else
    double cl;
#endif
    const char *secretid;
    const char *protocol_delimiter;
    int ret;

    ret = bdrv_apply_auto_read_only(bs, "curl driver does not support writes",
                                    errp);
    if (ret < 0) {
        return ret;
    }

    if (!libcurl_initialized) {
        ret = curl_global_init(CURL_GLOBAL_ALL);
        if (ret) {
            error_setg(errp, "libcurl initialization failed with %d", ret);
            return -EIO;
        }
        libcurl_initialized = true;
    }

    qemu_mutex_init(&s->mutex);
    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        goto out_noclean;
    }

    s->readahead_size = qemu_opt_get_size(opts, CURL_BLOCK_OPT_READAHEAD,
                                          CURL_BLOCK_OPT_READAHEAD_DEFAULT);
    if ((s->readahead_size & 0x1ff) != 0) {
        error_setg(errp, "HTTP_READAHEAD_SIZE %zd is not a multiple of 512",
                   s->readahead_size);
        goto out_noclean;
    }

    s->timeout = qemu_opt_get_number(opts, CURL_BLOCK_OPT_TIMEOUT,
                                     CURL_BLOCK_OPT_TIMEOUT_DEFAULT);
    if (s->timeout > CURL_TIMEOUT_MAX) {
        error_setg(errp, "timeout parameter is too large or negative");
        goto out_noclean;
    }

    s->sslverify = qemu_opt_get_bool(opts, CURL_BLOCK_OPT_SSLVERIFY,
                                     CURL_BLOCK_OPT_SSLVERIFY_DEFAULT);

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

    trace_curl_open(file);
    qemu_co_queue_init(&s->free_state_waitq);
    s->aio_context = bdrv_get_aio_context(bs);
    s->url = g_strdup(file);
    s->sockets = g_hash_table_new_full(NULL, NULL, NULL, g_free);
    qemu_mutex_lock(&s->mutex);
    state = curl_find_state(s);
    qemu_mutex_unlock(&s->mutex);
    if (!state) {
        goto out_noclean;
    }

    // Get file size

    if (curl_init_state(s, state) < 0) {
        pstrcpy(state->errmsg, CURL_ERROR_SIZE,
                "curl library initialization failed.");
        goto out;
    }

    s->accept_range = false;
    if (curl_easy_setopt(state->curl, CURLOPT_NOBODY, 1) ||
        curl_easy_setopt(state->curl, CURLOPT_HEADERFUNCTION, curl_header_cb) ||
        curl_easy_setopt(state->curl, CURLOPT_HEADERDATA, s)) {
        pstrcpy(state->errmsg, CURL_ERROR_SIZE,
                "curl library initialization failed.");
        goto out;
    }
    if (curl_easy_perform(state->curl))
        goto out;
    /* CURL 7.55.0 deprecates CURLINFO_CONTENT_LENGTH_DOWNLOAD in favour of
     * the *_T version which returns a more sensible type for content length.
     */
#if LIBCURL_VERSION_NUM >= 0x073700
    if (curl_easy_getinfo(state->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl)) {
        goto out;
    }
#else
    if (curl_easy_getinfo(state->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl)) {
        goto out;
    }
#endif
    /* Prior CURL 7.19.4 return value of 0 could mean that the file size is not
     * know or the size is zero. From 7.19.4 CURL returns -1 if size is not
     * known and zero if it is really zero-length file. */
#if LIBCURL_VERSION_NUM >= 0x071304
    if (cl < 0) {
        pstrcpy(state->errmsg, CURL_ERROR_SIZE,
                "Server didn't report file size.");
        goto out;
    }
#else
    if (cl <= 0) {
        pstrcpy(state->errmsg, CURL_ERROR_SIZE,
                "Unknown file size or zero-length file.");
        goto out;
    }
#endif

    s->len = cl;

    if ((!strncasecmp(s->url, "http://", strlen("http://"))
        || !strncasecmp(s->url, "https://", strlen("https://")))
        && !s->accept_range) {
        pstrcpy(state->errmsg, CURL_ERROR_SIZE,
                "Server does not support 'range' (byte ranges).");
        goto out;
    }
    trace_curl_open_size(s->len);

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
    g_free(s->username);
    g_free(s->proxyusername);
    g_free(s->proxypassword);
    curl_drop_all_sockets(s->sockets);
    g_hash_table_destroy(s->sockets);
    qemu_opts_del(opts);
    return -EINVAL;
}

static void coroutine_fn curl_setup_preadv(BlockDriverState *bs, CURLAIOCB *acb)
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
        qemu_co_queue_wait(&s->free_state_waitq, &s->mutex);
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
    trace_curl_setup_preadv(acb->bytes, start, state->range);
    if (curl_easy_setopt(state->curl, CURLOPT_RANGE, state->range) ||
        curl_multi_add_handle(s->multi, state->curl) != CURLM_OK) {
        state->acb[0] = NULL;
        acb->ret = -EIO;

        curl_clean_state(state);
        goto out;
    }

    /* Tell curl it needs to kick things off */
    curl_multi_socket_action(s->multi, CURL_SOCKET_TIMEOUT, 0, &running);

out:
    qemu_mutex_unlock(&s->mutex);
}

static int coroutine_fn curl_co_preadv(BlockDriverState *bs,
        int64_t offset, int64_t bytes, QEMUIOVector *qiov,
        BdrvRequestFlags flags)
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

    trace_curl_close();
    curl_detach_aio_context(bs);
    qemu_mutex_destroy(&s->mutex);

    g_hash_table_destroy(s->sockets);
    g_free(s->cookie);
    g_free(s->url);
    g_free(s->username);
    g_free(s->proxyusername);
    g_free(s->proxypassword);
}

static int64_t curl_getlength(BlockDriverState *bs)
{
    BDRVCURLState *s = bs->opaque;
    return s->len;
}

static void curl_refresh_filename(BlockDriverState *bs)
{
    BDRVCURLState *s = bs->opaque;

    /* "readahead" and "timeout" do not change the guest-visible data,
     * so ignore them */
    if (s->sslverify != CURL_BLOCK_OPT_SSLVERIFY_DEFAULT ||
        s->cookie || s->username || s->password || s->proxyusername ||
        s->proxypassword)
    {
        return;
    }

    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename), s->url);
}


static const char *const curl_strong_runtime_opts[] = {
    CURL_BLOCK_OPT_URL,
    CURL_BLOCK_OPT_SSLVERIFY,
    CURL_BLOCK_OPT_COOKIE,
    CURL_BLOCK_OPT_COOKIE_SECRET,
    CURL_BLOCK_OPT_USERNAME,
    CURL_BLOCK_OPT_PASSWORD_SECRET,
    CURL_BLOCK_OPT_PROXY_USERNAME,
    CURL_BLOCK_OPT_PROXY_PASSWORD_SECRET,

    NULL
};

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

    .bdrv_refresh_filename      = curl_refresh_filename,
    .strong_runtime_opts        = curl_strong_runtime_opts,
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

    .bdrv_refresh_filename      = curl_refresh_filename,
    .strong_runtime_opts        = curl_strong_runtime_opts,
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

    .bdrv_refresh_filename      = curl_refresh_filename,
    .strong_runtime_opts        = curl_strong_runtime_opts,
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

    .bdrv_refresh_filename      = curl_refresh_filename,
    .strong_runtime_opts        = curl_strong_runtime_opts,
};

static void curl_block_init(void)
{
    bdrv_register(&bdrv_http);
    bdrv_register(&bdrv_https);
    bdrv_register(&bdrv_ftp);
    bdrv_register(&bdrv_ftps);
}

block_init(curl_block_init);
