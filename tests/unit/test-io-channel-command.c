/*
 * QEMU I/O channel command test
 *
 * Copyright (c) 2015 Red Hat, Inc.
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
#include <glib/gstdio.h>
#include "io/channel-command.h"
#include "io-channel-helpers.h"
#include "qapi/error.h"
#include "qemu/module.h"

#define TEST_FIFO "test-io-channel-command.fifo"

static char *socat = NULL;

static void test_io_channel_command_fifo(bool async)
{
    g_autofree gchar *tmpdir = g_dir_make_tmp("qemu-test-io-channel.XXXXXX", NULL);
    g_autofree gchar *fifo = g_strdup_printf("%s/%s", tmpdir, TEST_FIFO);
    g_autofree gchar *srcargs = g_strdup_printf("%s - PIPE:%s,wronly", socat, fifo);
    g_autofree gchar *dstargs = g_strdup_printf("%s PIPE:%s,rdonly -", socat, fifo);
    g_auto(GStrv) srcargv = g_strsplit(srcargs, " ", -1);
    g_auto(GStrv) dstargv = g_strsplit(dstargs, " ", -1);
    QIOChannel *src, *dst;
    QIOChannelTest *test;

    src = QIO_CHANNEL(qio_channel_command_new_spawn((const char **) srcargv,
                                                    O_WRONLY,
                                                    &error_abort));
    /* try to avoid a race to create the socket */
    g_usleep(1000);

    dst = QIO_CHANNEL(qio_channel_command_new_spawn((const char **) dstargv,
                                                    O_RDONLY,
                                                    &error_abort));

    test = qio_channel_test_new();
    qio_channel_test_run_threads(test, async, src, dst);
    qio_channel_test_validate(test);

    object_unref(OBJECT(src));
    object_unref(OBJECT(dst));

    g_rmdir(tmpdir);
}


static void test_io_channel_command_fifo_async(void)
{
    if (!socat) {
        g_test_skip("socat is not found in PATH");
        return;
    }

    test_io_channel_command_fifo(true);
}

static void test_io_channel_command_fifo_sync(void)
{
    if (!socat) {
        g_test_skip("socat is not found in PATH");
        return;
    }

    test_io_channel_command_fifo(false);
}


static void test_io_channel_command_echo(bool async)
{
    QIOChannel *ioc;
    QIOChannelTest *test;
    const char *socatargv[] = {
        socat, "-", "-", NULL,
    };

    if (!socat) {
        g_test_skip("socat is not found in PATH");
        return;
    }

    ioc = QIO_CHANNEL(qio_channel_command_new_spawn(socatargv,
                                                    O_RDWR,
                                                    &error_abort));
    test = qio_channel_test_new();
    qio_channel_test_run_threads(test, async, ioc, ioc);
    qio_channel_test_validate(test);

    object_unref(OBJECT(ioc));
}


static void test_io_channel_command_echo_async(void)
{
    test_io_channel_command_echo(true);
}

static void test_io_channel_command_echo_sync(void)
{
    test_io_channel_command_echo(false);
}

int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);

    g_test_init(&argc, &argv, NULL);

    socat = g_find_program_in_path("socat");

    g_test_add_func("/io/channel/command/fifo/sync",
                    test_io_channel_command_fifo_sync);
    g_test_add_func("/io/channel/command/fifo/async",
                    test_io_channel_command_fifo_async);
    g_test_add_func("/io/channel/command/echo/sync",
                    test_io_channel_command_echo_sync);
    g_test_add_func("/io/channel/command/echo/async",
                    test_io_channel_command_echo_async);

    return g_test_run();
}
