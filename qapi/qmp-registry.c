/*
 * Core Definitions for QAPI/QMP Dispatch
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Michael Roth      <mdroth@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qapi/qmp-core.h"

static QTAILQ_HEAD(, QmpCommand) qmp_commands =
    QTAILQ_HEAD_INITIALIZER(qmp_commands);

void qmp_register_command(const char *name, QmpCommandFunc *fn)
{
    QmpCommand *cmd = g_malloc0(sizeof(*cmd));

    cmd->name = name;
    cmd->type = QCT_NORMAL;
    cmd->fn = fn;
    QTAILQ_INSERT_TAIL(&qmp_commands, cmd, node);
}

QmpCommand *qmp_find_command(const char *name)
{
    QmpCommand *i;

    QTAILQ_FOREACH(i, &qmp_commands, node) {
        if (strcmp(i->name, name) == 0) {
            return i;
        }
    }
    return NULL;
}
