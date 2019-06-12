/*
 * QEMU I/O channel command test
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
#include "io/channel-command.h"
#include "io-channel-helpers.h"
#include "qapi/error.h"
#include "qemu/module.h"

#ifndef WIN32
static void test_io_channel_command_fifo(bool async)
{
#define TEST_FIFO "tests/test-io-channel-command.fifo"
    QIOChannel *src, *dst;
    QIOChannelTest *test;
    const char *srcfifo = "PIPE:" TEST_FIFO ",wronly";
    const char *dstfifo = "PIPE:" TEST_FIFO ",rdonly";
    const char *srcargv[] = {
        "/bin/socat", "-", srcfifo, NULL,
    };
    const char *dstargv[] = {
        "/bin/socat", dstfifo, "-", NULL,
    };

    unlink(TEST_FIFO);
    if (access("/bin/socat", X_OK) < 0) {
        return; /* Pretend success if socat is not present */
    }
    if (mkfifo(TEST_FIFO, 0600) < 0) {
        abort();
    }
    src = QIO_CHANNEL(qio_channel_command_new_spawn(srcargv,
                                                    O_WRONLY,
                                                    &error_abort));
    dst = QIO_CHANNEL(qio_channel_command_new_spawn(dstargv,
                                                    O_RDONLY,
                                                    &error_abort));

    test = qio_channel_test_new();
    qio_channel_test_run_threads(test, async, src, dst);
    qio_channel_test_validate(test);

    object_unref(OBJECT(src));
    object_unref(OBJECT(dst));

    unlink(TEST_FIFO);
}


static void test_io_channel_command_fifo_async(void)
{
    test_io_channel_command_fifo(true);
}

static void test_io_channel_command_fifo_sync(void)
{
    test_io_channel_command_fifo(false);
}


static void test_io_channel_command_echo(bool async)
{
    QIOChannel *ioc;
    QIOChannelTest *test;
    const char *socatargv[] = {
        "/bin/socat", "-", "-", NULL,
    };

    if (access("/bin/socat", X_OK) < 0) {
        return; /* Pretend success if socat is not present */
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
#endif

int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);

    g_test_init(&argc, &argv, NULL);

#ifndef WIN32
    g_test_add_func("/io/channel/command/fifo/sync",
                    test_io_channel_command_fifo_sync);
    g_test_add_func("/io/channel/command/fifo/async",
                    test_io_channel_command_fifo_async);
    g_test_add_func("/io/channel/command/echo/sync",
                    test_io_channel_command_echo_sync);
    g_test_add_func("/io/channel/command/echo/async",
                    test_io_channel_command_echo_async);
#endif

    return g_test_run();
}
