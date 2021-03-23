/*
 * migration yank functions
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "io/channel.h"
#include "yank_functions.h"

void migration_yank_iochannel(void *opaque)
{
    QIOChannel *ioc = QIO_CHANNEL(opaque);

    qio_channel_shutdown(ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
}
