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
#include "qemu/yank.h"
#include "io/channel-socket.h"
#include "io/channel-tls.h"

void migration_yank_iochannel(void *opaque)
{
    QIOChannel *ioc = QIO_CHANNEL(opaque);

    qio_channel_shutdown(ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
}

/* Return whether yank is supported on this ioc */
static bool migration_ioc_yank_supported(QIOChannel *ioc)
{
    return object_dynamic_cast(OBJECT(ioc), TYPE_QIO_CHANNEL_SOCKET) ||
        object_dynamic_cast(OBJECT(ioc), TYPE_QIO_CHANNEL_TLS);
}

void migration_ioc_register_yank(QIOChannel *ioc)
{
    if (migration_ioc_yank_supported(ioc)) {
        yank_register_function(MIGRATION_YANK_INSTANCE,
                               migration_yank_iochannel,
                               QIO_CHANNEL(ioc));
    }
}

void migration_ioc_unregister_yank(QIOChannel *ioc)
{
    if (migration_ioc_yank_supported(ioc)) {
        yank_unregister_function(MIGRATION_YANK_INSTANCE,
                                 migration_yank_iochannel,
                                 QIO_CHANNEL(ioc));
    }
}
