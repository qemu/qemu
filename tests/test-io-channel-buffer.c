/*
 * QEMU I/O channel buffer test
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
#include "io/channel-buffer.h"
#include "qemu/module.h"
#include "io-channel-helpers.h"


static void test_io_channel_buf(void)
{
    QIOChannelBuffer *buf;
    QIOChannelTest *test;

    buf = qio_channel_buffer_new(0);

    test = qio_channel_test_new();
    qio_channel_test_run_writer(test, QIO_CHANNEL(buf));
    buf->offset = 0;
    qio_channel_test_run_reader(test, QIO_CHANNEL(buf));
    qio_channel_test_validate(test);

    object_unref(OBJECT(buf));
}


int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/io/channel/buf", test_io_channel_buf);
    return g_test_run();
}
