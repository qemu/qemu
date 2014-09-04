/*
 * QEMU Guest Agent common/cross-platform command implementations
 *
 * Copyright IBM Corp. 2012
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include "qga/guest-agent-core.h"
#include "qga-qmp-commands.h"
#include "qapi/qmp/qerror.h"

/* Note: in some situations, like with the fsfreeze, logging may be
 * temporarilly disabled. if it is necessary that a command be able
 * to log for accounting purposes, check ga_logging_enabled() beforehand,
 * and use the QERR_QGA_LOGGING_DISABLED to generate an error
 */
void slog(const gchar *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    g_logv("syslog", G_LOG_LEVEL_INFO, fmt, ap);
    va_end(ap);
}

int64_t qmp_guest_sync_delimited(int64_t id, Error **errp)
{
    ga_set_response_delimited(ga_state);
    return id;
}

int64_t qmp_guest_sync(int64_t id, Error **errp)
{
    return id;
}

void qmp_guest_ping(Error **errp)
{
    slog("guest-ping called");
}

static void qmp_command_info(QmpCommand *cmd, void *opaque)
{
    GuestAgentInfo *info = opaque;
    GuestAgentCommandInfo *cmd_info;
    GuestAgentCommandInfoList *cmd_info_list;

    cmd_info = g_malloc0(sizeof(GuestAgentCommandInfo));
    cmd_info->name = g_strdup(qmp_command_name(cmd));
    cmd_info->enabled = qmp_command_is_enabled(cmd);
    cmd_info->success_response = qmp_has_success_response(cmd);

    cmd_info_list = g_malloc0(sizeof(GuestAgentCommandInfoList));
    cmd_info_list->value = cmd_info;
    cmd_info_list->next = info->supported_commands;
    info->supported_commands = cmd_info_list;
}

struct GuestAgentInfo *qmp_guest_info(Error **errp)
{
    GuestAgentInfo *info = g_malloc0(sizeof(GuestAgentInfo));

    info->version = g_strdup(QEMU_VERSION);
    qmp_for_each_command(qmp_command_info, info);
    return info;
}
