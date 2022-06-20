/*
 * QEMU I/O channel null test
 *
 * Copyright (c) 2022 Red Hat, Inc.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "io/channel-null.h"
#include "qapi/error.h"

static gboolean test_io_channel_watch(QIOChannel *ioc,
                                      GIOCondition condition,
                                      gpointer opaque)
{
    GIOCondition *gotcond = opaque;
    *gotcond = condition;
    return G_SOURCE_REMOVE;
}

static void test_io_channel_null_io(void)
{
    g_autoptr(QIOChannelNull) null = qio_channel_null_new();
    char buf[1024];
    GIOCondition gotcond = 0;
    Error *local_err = NULL;

    g_assert(qio_channel_write(QIO_CHANNEL(null),
                               "Hello World", 11,
                               &error_abort) == 11);

    g_assert(qio_channel_read(QIO_CHANNEL(null),
                              buf, sizeof(buf),
                              &error_abort) == 0);

    qio_channel_add_watch(QIO_CHANNEL(null),
                          G_IO_IN,
                          test_io_channel_watch,
                          &gotcond,
                          NULL);

    g_main_context_iteration(NULL, false);

    g_assert(gotcond == G_IO_IN);

    qio_channel_add_watch(QIO_CHANNEL(null),
                          G_IO_IN | G_IO_OUT,
                          test_io_channel_watch,
                          &gotcond,
                          NULL);

    g_main_context_iteration(NULL, false);

    g_assert(gotcond == (G_IO_IN | G_IO_OUT));

    qio_channel_close(QIO_CHANNEL(null), &error_abort);

    g_assert(qio_channel_write(QIO_CHANNEL(null),
                               "Hello World", 11,
                               &local_err) == -1);
    g_assert_nonnull(local_err);

    g_clear_pointer(&local_err, error_free);

    g_assert(qio_channel_read(QIO_CHANNEL(null),
                              buf, sizeof(buf),
                              &local_err) == -1);
    g_assert_nonnull(local_err);

    g_clear_pointer(&local_err, error_free);
}

int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/io/channel/null/io", test_io_channel_null_io);

    return g_test_run();
}
