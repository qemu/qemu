/*
 * HMP commands related to the block layer
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * or (at your option) any later version.
 * See the COPYING file in the top-level directory.
 */

#ifndef BLOCK_HMP_COMMANDS_H
#define BLOCK_HMP_COMMANDS_H

void hmp_drive_add(Monitor *mon, const QDict *qdict);

void hmp_commit(Monitor *mon, const QDict *qdict);
void hmp_drive_del(Monitor *mon, const QDict *qdict);

#endif
