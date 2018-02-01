/*
 * QEMU I/O channel test helpers
 *
 * Copyright (c) 2015 Red Hat, Inc.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "io-channel-helpers.h"
#include "qemu/iov.h"

struct QIOChannelTest {
    QIOChannel *src;
    QIOChannel *dst;
    bool blocking;
    size_t len;
    size_t niov;
    char *input;
    struct iovec *inputv;
    char *output;
    struct iovec *outputv;
    Error *writeerr;
    Error *readerr;
};


/* This thread sends all data using iovecs */
static gpointer test_io_thread_writer(gpointer opaque)
{
    QIOChannelTest *data = opaque;

    qio_channel_set_blocking(data->src, data->blocking, NULL);

    qio_channel_writev_all(data->src,
                           data->inputv,
                           data->niov,
                           &data->writeerr);

    return NULL;
}


/* This thread receives all data using iovecs */
static gpointer test_io_thread_reader(gpointer opaque)
{
    QIOChannelTest *data = opaque;

    qio_channel_set_blocking(data->dst, data->blocking, NULL);

    qio_channel_readv_all(data->dst,
                          data->outputv,
                          data->niov,
                          &data->readerr);

    return NULL;
}


QIOChannelTest *qio_channel_test_new(void)
{
    QIOChannelTest *data = g_new0(QIOChannelTest, 1);
    size_t i;
    size_t offset;


    /* We'll send 1 MB of data */
#define CHUNK_COUNT 250
#define CHUNK_LEN 4194

    data->len = CHUNK_COUNT * CHUNK_LEN;
    data->input = g_new0(char, data->len);
    data->output = g_new0(gchar, data->len);

    /* Fill input with a pattern */
    for (i = 0; i < data->len; i += CHUNK_LEN) {
        memset(data->input + i, (i / CHUNK_LEN), CHUNK_LEN);
    }

    /* We'll split the data across a bunch of IO vecs */
    data->niov = CHUNK_COUNT;
    data->inputv = g_new0(struct iovec, data->niov);
    data->outputv = g_new0(struct iovec, data->niov);

    for (i = 0, offset = 0; i < data->niov; i++, offset += CHUNK_LEN) {
        data->inputv[i].iov_base = data->input + offset;
        data->outputv[i].iov_base = data->output + offset;
        data->inputv[i].iov_len = CHUNK_LEN;
        data->outputv[i].iov_len = CHUNK_LEN;
    }

    return data;
}

void qio_channel_test_run_threads(QIOChannelTest *test,
                                  bool blocking,
                                  QIOChannel *src,
                                  QIOChannel *dst)
{
    GThread *reader, *writer;

    test->src = src;
    test->dst = dst;
    test->blocking = blocking;

    reader = g_thread_new("reader",
                          test_io_thread_reader,
                          test);
    writer = g_thread_new("writer",
                          test_io_thread_writer,
                          test);

    g_thread_join(reader);
    g_thread_join(writer);

    test->dst = test->src = NULL;
}


void qio_channel_test_run_writer(QIOChannelTest *test,
                                 QIOChannel *src)
{
    test->src = src;
    test_io_thread_writer(test);
    test->src = NULL;
}


void qio_channel_test_run_reader(QIOChannelTest *test,
                                 QIOChannel *dst)
{
    test->dst = dst;
    test_io_thread_reader(test);
    test->dst = NULL;
}


void qio_channel_test_validate(QIOChannelTest *test)
{
    g_assert(test->readerr == NULL);
    g_assert(test->writeerr == NULL);
    g_assert_cmpint(memcmp(test->input,
                           test->output,
                           test->len), ==, 0);

    g_free(test->inputv);
    g_free(test->outputv);
    g_free(test->input);
    g_free(test->output);
    g_free(test);
}
