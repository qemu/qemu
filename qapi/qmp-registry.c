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

static QTAILQ_HEAD(QmpCommandList, QmpCommand) qmp_commands =
    QTAILQ_HEAD_INITIALIZER(qmp_commands);

void qmp_register_command(const char *name, QmpCommandFunc *fn)
{
    QmpCommand *cmd = g_malloc0(sizeof(*cmd));

    cmd->name = name;
    cmd->type = QCT_NORMAL;
    cmd->fn = fn;
    cmd->enabled = true;
    QTAILQ_INSERT_TAIL(&qmp_commands, cmd, node);
}

QmpCommand *qmp_find_command(const char *name)
{
    QmpCommand *cmd;

    QTAILQ_FOREACH(cmd, &qmp_commands, node) {
        if (strcmp(cmd->name, name) == 0) {
            return cmd;
        }
    }
    return NULL;
}

void qmp_disable_command(const char *name)
{
    QmpCommand *cmd;

    QTAILQ_FOREACH(cmd, &qmp_commands, node) {
        if (strcmp(cmd->name, name) == 0) {
            cmd->enabled = false;
            return;
        }
    }
}

bool qmp_command_is_enabled(const char *name)
{
    QmpCommand *cmd;

    QTAILQ_FOREACH(cmd, &qmp_commands, node) {
        if (strcmp(cmd->name, name) == 0) {
            return cmd->enabled;
        }
    }

    return false;
}

char **qmp_get_command_list(void)
{
    QmpCommand *cmd;
    int count = 1;
    char **list_head, **list;

    QTAILQ_FOREACH(cmd, &qmp_commands, node) {
        count++;
    }

    list_head = list = g_malloc0(count * sizeof(char *));

    QTAILQ_FOREACH(cmd, &qmp_commands, node) {
        *list = strdup(cmd->name);
        list++;
    }

    return list_head;
}
