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
#include "qemu-file.h"

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
                               ioc);
    }
}

void migration_ioc_unregister_yank(QIOChannel *ioc)
{
    if (migration_ioc_yank_supported(ioc)) {
        yank_unregister_function(MIGRATION_YANK_INSTANCE,
                                 migration_yank_iochannel,
                                 ioc);
    }
}

void migration_ioc_unregister_yank_from_file(QEMUFile *file)
{
    QIOChannel *ioc = qemu_file_get_ioc(file);

    if (ioc) {
        /*
         * For migration qemufiles, we'll always reach here.  Though we'll skip
         * calls from e.g. savevm/loadvm as they don't use yank.
         */
        migration_ioc_unregister_yank(ioc);
    }
}
