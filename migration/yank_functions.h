/*
 * migration yank functions
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/**
 * migration_yank_iochannel: yank function for iochannel
 *
 * This yank function will call qio_channel_shutdown on the provided QIOChannel.
 *
 * @opaque: QIOChannel to shutdown
 */
void migration_yank_iochannel(void *opaque);
void migration_ioc_register_yank(QIOChannel *ioc);
void migration_ioc_unregister_yank(QIOChannel *ioc);
void migration_ioc_unregister_yank_from_file(QEMUFile *file);
