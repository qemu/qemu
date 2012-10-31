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
#include "qemu-common.h"
#include "block_int.h"
#include <curl/curl.h>

// #define DEBUG
// #define DEBUG_VERBOSE

#ifdef DEBUG_CURL
#define DPRINTF(fmt, ...) do { printf(fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

#define CURL_NUM_STATES 8
#define CURL_NUM_ACB    8
#define SECTOR_SIZE     512
#define READ_AHEAD_SIZE (256 * 1024)

#define FIND_RET_NONE   0
#define FIND_RET_OK     1
#define FIND_RET_WAIT   2

struct BDRVCURLState;

typedef struct CURLAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    QEMUIOVector *qiov;

    int64_t sector_num;
    int nb_sectors;

    size_t start;
    size_t end;
} CURLAIOCB;

typedef struct CURLState
{
    struct BDRVCURLState *s;
    CURLAIOCB *acb[CURL_NUM_ACB];
    CURL *curl;
    char *orig_buf;
    size_t buf_start;
    size_t buf_off;
    size_t buf_len;
    char range[128];
    char errmsg[CURL_ERROR_SIZE];
    char in_use;
} CURLState;

typedef struct BDRVCURLState {
    CURLM *multi;
    size_t len;
    CURLState states[CURL_NUM_STATES];
    char *url;
    size_t readahead_size;
} BDRVCURLState;

static void curl_clean_state(CURLState *s);
static void curl_multi_do(void *arg);
static int curl_aio_flush(void *opaque);

static int curl_sock_cb(CURL *curl, curl_socket_t fd, int action,
                        void *s, void *sp)
{
    DPRINTF("CURL (AIO): Sock action %d on fd %d\n", action, fd);
    switch (action) {
        case CURL_POLL_IN:
            qemu_aio_set_fd_handler(fd, curl_multi_do, NULL, curl_aio_flush, s);
            break;
        case CURL_POLL_OUT:
            qemu_aio_set_fd_handler(fd, NULL, curl_multi_do, curl_aio_flush, s);
            break;
        case CURL_POLL_INOUT:
            qemu_aio_set_fd_handler(fd, curl_multi_do, curl_multi_do,
                                    curl_aio_flush, s);
            break;
        case CURL_POLL_REMOVE:
            qemu_aio_set_fd_handler(fd, NULL, NULL, NULL, NULL);
            break;
    }

    return 0;
}

static size_t curl_size_cb(void *ptr, size_t size, size_t nmemb, void *opaque)
{
    CURLState *s = ((CURLState*)opaque);
    size_t realsize = size * nmemb;
    size_t fsize;

    if(sscanf(ptr, "Content-Length: %zd", &fsize) == 1) {
        s->s->len = fsize;
    }

    return realsize;
}

static size_t curl_read_cb(void *ptr, size_t size, size_t nmemb, void *opaque)
{
    CURLState *s = ((CURLState*)opaque);
    size_t realsize = size * nmemb;
    int i;

    DPRINTF("CURL: Just reading %zd bytes\n", realsize);

    if (!s || !s->orig_buf)
        goto read_end;

    memcpy(s->orig_buf + s->buf_off, ptr, realsize);
    s->buf_off += realsize;

    for(i=0; i<CURL_NUM_ACB; i++) {
        CURLAIOCB *acb = s->acb[i];

        if (!acb)
            continue;

        if ((s->buf_off >= acb->end)) {
            qemu_iovec_from_buf(acb->qiov, 0, s->orig_buf + acb->start,
                                acb->end - acb->start);
            acb->common.cb(acb->common.opaque, 0);
            qemu_aio_release(acb);
            s->acb[i] = NULL;
        }
    }

read_end:
    return realsize;
}

static int curl_find_buf(BDRVCURLState *s, size_t start, size_t len,
                         CURLAIOCB *acb)
{
    int i;
    size_t end = start + len;

    for (i=0; i<CURL_NUM_STATES; i++) {
        CURLState *state = &s->states[i];
        size_t buf_end = (state->buf_start + state->buf_off);
        size_t buf_fend = (state->buf_start + state->buf_len);

        if (!state->orig_buf)
            continue;
        if (!state->buf_off)
            continue;

        // Does the existing buffer cover our section?
        if ((start >= state->buf_start) &&
            (start <= buf_end) &&
            (end >= state->buf_start) &&
            (end <= buf_end))
        {
            char *buf = state->orig_buf + (start - state->buf_start);

            qemu_iovec_from_buf(acb->qiov, 0, buf, len);
            acb->common.cb(acb->common.opaque, 0);

            return FIND_RET_OK;
        }

        // Wait for unfinished chunks
        if ((start >= state->buf_start) &&
            (start <= buf_fend) &&
            (end >= state->buf_start) &&
            (end <= buf_fend))
        {
            int j;

            acb->start = start - state->buf_start;
            acb->end = acb->start + len;

            for (j=0; j<CURL_NUM_ACB; j++) {
                if (!state->acb[j]) {
                    state->acb[j] = acb;
                    return FIND_RET_WAIT;
                }
            }
        }
    }

    return FIND_RET_NONE;
}

static void curl_multi_do(void *arg)
{
    BDRVCURLState *s = (BDRVCURLState *)arg;
    int running;
    int r;
    int msgs_in_queue;

    if (!s->multi)
        return;

    do {
        r = curl_multi_socket_all(s->multi, &running);
    } while(r == CURLM_CALL_MULTI_PERFORM);

    /* Try to find done transfers, so we can free the easy
     * handle again. */
    do {
        CURLMsg *msg;
        msg = curl_multi_info_read(s->multi, &msgs_in_queue);

        if (!msg)
            break;
        if (msg->msg == CURLMSG_NONE)
            break;

        switch (msg->msg) {
            case CURLMSG_DONE:
            {
                CURLState *state = NULL;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char**)&state);

                /* ACBs for successful messages get completed in curl_read_cb */
                if (msg->data.result != CURLE_OK) {
                    int i;
                    for (i = 0; i < CURL_NUM_ACB; i++) {
                        CURLAIOCB *acb = state->acb[i];

                        if (acb == NULL) {
                            continue;
                        }

                        acb->common.cb(acb->common.opaque, -EIO);
                        qemu_aio_release(acb);
                        state->acb[i] = NULL;
                    }
                }

                curl_clean_state(state);
                break;
            }
            default:
                msgs_in_queue = 0;
                break;
        }
    } while(msgs_in_queue);
}

static CURLState *curl_init_state(BDRVCURLState *s)
{
    CURLState *state = NULL;
    int i, j;

    do {
        for (i=0; i<CURL_NUM_STATES; i++) {
            for (j=0; j<CURL_NUM_ACB; j++)
                if (s->states[i].acb[j])
                    continue;
            if (s->states[i].in_use)
                continue;

            state = &s->states[i];
            state->in_use = 1;
            break;
        }
        if (!state) {
            g_usleep(100);
            curl_multi_do(s);
        }
    } while(!state);

    if (state->curl)
        goto has_curl;

    state->curl = curl_easy_init();
    if (!state->curl)
        return NULL;
    curl_easy_setopt(state->curl, CURLOPT_URL, s->url);
    curl_easy_setopt(state->curl, CURLOPT_TIMEOUT, 5);
    curl_easy_setopt(state->curl, CURLOPT_WRITEFUNCTION, (void *)curl_read_cb);
    curl_easy_setopt(state->curl, CURLOPT_WRITEDATA, (void *)state);
    curl_easy_setopt(state->curl, CURLOPT_PRIVATE, (void *)state);
    curl_easy_setopt(state->curl, CURLOPT_AUTOREFERER, 1);
    curl_easy_setopt(state->curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(state->curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(state->curl, CURLOPT_ERRORBUFFER, state->errmsg);
    curl_easy_setopt(state->curl, CURLOPT_FAILONERROR, 1);

#ifdef DEBUG_VERBOSE
    curl_easy_setopt(state->curl, CURLOPT_VERBOSE, 1);
#endif

has_curl:

    state->s = s;

    return state;
}

static void curl_clean_state(CURLState *s)
{
    if (s->s->multi)
        curl_multi_remove_handle(s->s->multi, s->curl);
    s->in_use = 0;
}

static int curl_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVCURLState *s = bs->opaque;
    CURLState *state = NULL;
    double d;

    #define RA_OPTSTR ":readahead="
    char *file;
    char *ra;
    const char *ra_val;
    int parse_state = 0;

    static int inited = 0;

    file = g_strdup(filename);
    s->readahead_size = READ_AHEAD_SIZE;

    /* Parse a trailing ":readahead=#:" param, if present. */
    ra = file + strlen(file) - 1;
    while (ra >= file) {
        if (parse_state == 0) {
            if (*ra == ':')
                parse_state++;
            else
                break;
        } else if (parse_state == 1) {
            if (*ra > '9' || *ra < '0') {
                char *opt_start = ra - strlen(RA_OPTSTR) + 1;
                if (opt_start > file &&
                    strncmp(opt_start, RA_OPTSTR, strlen(RA_OPTSTR)) == 0) {
                    ra_val = ra + 1;
                    ra -= strlen(RA_OPTSTR) - 1;
                    *ra = '\0';
                    s->readahead_size = atoi(ra_val);
                    break;
                } else {
                    break;
                }
            }
        }
        ra--;
    }

    if ((s->readahead_size & 0x1ff) != 0) {
        fprintf(stderr, "HTTP_READAHEAD_SIZE %zd is not a multiple of 512\n",
                s->readahead_size);
        goto out_noclean;
    }

    if (!inited) {
        curl_global_init(CURL_GLOBAL_ALL);
        inited = 1;
    }

    DPRINTF("CURL: Opening %s\n", file);
    s->url = file;
    state = curl_init_state(s);
    if (!state)
        goto out_noclean;

    // Get file size

    curl_easy_setopt(state->curl, CURLOPT_NOBODY, 1);
    curl_easy_setopt(state->curl, CURLOPT_WRITEFUNCTION, (void *)curl_size_cb);
    if (curl_easy_perform(state->curl))
        goto out;
    curl_easy_getinfo(state->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &d);
    curl_easy_setopt(state->curl, CURLOPT_WRITEFUNCTION, (void *)curl_read_cb);
    curl_easy_setopt(state->curl, CURLOPT_NOBODY, 0);
    if (d)
        s->len = (size_t)d;
    else if(!s->len)
        goto out;
    DPRINTF("CURL: Size = %zd\n", s->len);

    curl_clean_state(state);
    curl_easy_cleanup(state->curl);
    state->curl = NULL;

    // Now we know the file exists and its size, so let's
    // initialize the multi interface!

    s->multi = curl_multi_init();
    curl_multi_setopt( s->multi, CURLMOPT_SOCKETDATA, s); 
    curl_multi_setopt( s->multi, CURLMOPT_SOCKETFUNCTION, curl_sock_cb ); 
    curl_multi_do(s);

    return 0;

out:
    fprintf(stderr, "CURL: Error opening file: %s\n", state->errmsg);
    curl_easy_cleanup(state->curl);
    state->curl = NULL;
out_noclean:
    g_free(file);
    return -EINVAL;
}

static int curl_aio_flush(void *opaque)
{
    BDRVCURLState *s = opaque;
    int i, j;

    for (i=0; i < CURL_NUM_STATES; i++) {
        for(j=0; j < CURL_NUM_ACB; j++) {
            if (s->states[i].acb[j]) {
                return 1;
            }
        }
    }
    return 0;
}

static void curl_aio_cancel(BlockDriverAIOCB *blockacb)
{
    // Do we have to implement canceling? Seems to work without...
}

static const AIOCBInfo curl_aiocb_info = {
    .aiocb_size         = sizeof(CURLAIOCB),
    .cancel             = curl_aio_cancel,
};


static void curl_readv_bh_cb(void *p)
{
    CURLState *state;

    CURLAIOCB *acb = p;
    BDRVCURLState *s = acb->common.bs->opaque;

    qemu_bh_delete(acb->bh);
    acb->bh = NULL;

    size_t start = acb->sector_num * SECTOR_SIZE;
    size_t end;

    // In case we have the requested data already (e.g. read-ahead),
    // we can just call the callback and be done.
    switch (curl_find_buf(s, start, acb->nb_sectors * SECTOR_SIZE, acb)) {
        case FIND_RET_OK:
            qemu_aio_release(acb);
            // fall through
        case FIND_RET_WAIT:
            return;
        default:
            break;
    }

    // No cache found, so let's start a new request
    state = curl_init_state(s);
    if (!state) {
        acb->common.cb(acb->common.opaque, -EIO);
        qemu_aio_release(acb);
        return;
    }

    acb->start = 0;
    acb->end = (acb->nb_sectors * SECTOR_SIZE);

    state->buf_off = 0;
    if (state->orig_buf)
        g_free(state->orig_buf);
    state->buf_start = start;
    state->buf_len = acb->end + s->readahead_size;
    end = MIN(start + state->buf_len, s->len) - 1;
    state->orig_buf = g_malloc(state->buf_len);
    state->acb[0] = acb;

    snprintf(state->range, 127, "%zd-%zd", start, end);
    DPRINTF("CURL (AIO): Reading %d at %zd (%s)\n",
            (acb->nb_sectors * SECTOR_SIZE), start, state->range);
    curl_easy_setopt(state->curl, CURLOPT_RANGE, state->range);

    curl_multi_add_handle(s->multi, state->curl);
    curl_multi_do(s);

}

static BlockDriverAIOCB *curl_aio_readv(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    CURLAIOCB *acb;

    acb = qemu_aio_get(&curl_aiocb_info, bs, cb, opaque);

    acb->qiov = qiov;
    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;

    acb->bh = qemu_bh_new(curl_readv_bh_cb, acb);

    if (!acb->bh) {
        DPRINTF("CURL: qemu_bh_new failed\n");
        return NULL;
    }

    qemu_bh_schedule(acb->bh);
    return &acb->common;
}

static void curl_close(BlockDriverState *bs)
{
    BDRVCURLState *s = bs->opaque;
    int i;

    DPRINTF("CURL: Close\n");
    for (i=0; i<CURL_NUM_STATES; i++) {
        if (s->states[i].in_use)
            curl_clean_state(&s->states[i]);
        if (s->states[i].curl) {
            curl_easy_cleanup(s->states[i].curl);
            s->states[i].curl = NULL;
        }
        if (s->states[i].orig_buf) {
            g_free(s->states[i].orig_buf);
            s->states[i].orig_buf = NULL;
        }
    }
    if (s->multi)
        curl_multi_cleanup(s->multi);
    g_free(s->url);
}

static int64_t curl_getlength(BlockDriverState *bs)
{
    BDRVCURLState *s = bs->opaque;
    return s->len;
}

static BlockDriver bdrv_http = {
    .format_name     = "http",
    .protocol_name   = "http",

    .instance_size   = sizeof(BDRVCURLState),
    .bdrv_file_open  = curl_open,
    .bdrv_close      = curl_close,
    .bdrv_getlength  = curl_getlength,

    .bdrv_aio_readv  = curl_aio_readv,
};

static BlockDriver bdrv_https = {
    .format_name     = "https",
    .protocol_name   = "https",

    .instance_size   = sizeof(BDRVCURLState),
    .bdrv_file_open  = curl_open,
    .bdrv_close      = curl_close,
    .bdrv_getlength  = curl_getlength,

    .bdrv_aio_readv  = curl_aio_readv,
};

static BlockDriver bdrv_ftp = {
    .format_name     = "ftp",
    .protocol_name   = "ftp",

    .instance_size   = sizeof(BDRVCURLState),
    .bdrv_file_open  = curl_open,
    .bdrv_close      = curl_close,
    .bdrv_getlength  = curl_getlength,

    .bdrv_aio_readv  = curl_aio_readv,
};

static BlockDriver bdrv_ftps = {
    .format_name     = "ftps",
    .protocol_name   = "ftps",

    .instance_size   = sizeof(BDRVCURLState),
    .bdrv_file_open  = curl_open,
    .bdrv_close      = curl_close,
    .bdrv_getlength  = curl_getlength,

    .bdrv_aio_readv  = curl_aio_readv,
};

static BlockDriver bdrv_tftp = {
    .format_name     = "tftp",
    .protocol_name   = "tftp",

    .instance_size   = sizeof(BDRVCURLState),
    .bdrv_file_open  = curl_open,
    .bdrv_close      = curl_close,
    .bdrv_getlength  = curl_getlength,

    .bdrv_aio_readv  = curl_aio_readv,
};

static void curl_block_init(void)
{
    bdrv_register(&bdrv_http);
    bdrv_register(&bdrv_https);
    bdrv_register(&bdrv_ftp);
    bdrv_register(&bdrv_ftps);
    bdrv_register(&bdrv_tftp);
}

block_init(curl_block_init);
