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

#include "io/channel.h"

#ifndef TEST_IO_CHANNEL_HELPERS
#define TEST_IO_CHANNEL_HELPERS

typedef struct QIOChannelTest QIOChannelTest;

QIOChannelTest *qio_channel_test_new(void);

void qio_channel_test_run_threads(QIOChannelTest *test,
                                  bool blocking,
                                  QIOChannel *src,
                                  QIOChannel *dst);

void qio_channel_test_run_writer(QIOChannelTest *test,
                                 QIOChannel *src);
void qio_channel_test_run_reader(QIOChannelTest *test,
                                 QIOChannel *dst);

void qio_channel_test_validate(QIOChannelTest *test);

#endif /* TEST_IO_CHANNEL_HELPERS */
