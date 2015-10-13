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

    cmd_info = g_new0(GuestAgentCommandInfo, 1);
    cmd_info->name = g_strdup(qmp_command_name(cmd));
    cmd_info->enabled = qmp_command_is_enabled(cmd);
    cmd_info->success_response = qmp_has_success_response(cmd);

    cmd_info_list = g_new0(GuestAgentCommandInfoList, 1);
    cmd_info_list->value = cmd_info;
    cmd_info_list->next = info->supported_commands;
    info->supported_commands = cmd_info_list;
}

struct GuestAgentInfo *qmp_guest_info(Error **errp)
{
    GuestAgentInfo *info = g_new0(GuestAgentInfo, 1);

    info->version = g_strdup(QEMU_VERSION);
    qmp_for_each_command(qmp_command_info, info);
    return info;
}

struct GuestExecInfo {
    GPid pid;
    int64_t pid_numeric;
    gint status;
    bool finished;
    QTAILQ_ENTRY(GuestExecInfo) next;
};
typedef struct GuestExecInfo GuestExecInfo;

static struct {
    QTAILQ_HEAD(, GuestExecInfo) processes;
} guest_exec_state = {
    .processes = QTAILQ_HEAD_INITIALIZER(guest_exec_state.processes),
};

static int64_t gpid_to_int64(GPid pid)
{
#ifdef G_OS_WIN32
    return GetProcessId(pid);
#else
    return (int64_t)pid;
#endif
}

static GuestExecInfo *guest_exec_info_add(GPid pid)
{
    GuestExecInfo *gei;

    gei = g_new0(GuestExecInfo, 1);
    gei->pid = pid;
    gei->pid_numeric = gpid_to_int64(pid);
    QTAILQ_INSERT_TAIL(&guest_exec_state.processes, gei, next);

    return gei;
}

static GuestExecInfo *guest_exec_info_find(int64_t pid_numeric)
{
    GuestExecInfo *gei;

    QTAILQ_FOREACH(gei, &guest_exec_state.processes, next) {
        if (gei->pid_numeric == pid_numeric) {
            return gei;
        }
    }

    return NULL;
}

GuestExecStatus *qmp_guest_exec_status(int64_t pid, Error **err)
{
    GuestExecInfo *gei;
    GuestExecStatus *ges;

    slog("guest-exec-status called, pid: %u", (uint32_t)pid);

    gei = guest_exec_info_find(pid);
    if (gei == NULL) {
        error_setg(err, QERR_INVALID_PARAMETER, "pid");
        return NULL;
    }

    ges = g_new0(GuestExecStatus, 1);
    ges->exited = gei->finished;

    if (gei->finished) {
        /* Glib has no portable way to parse exit status.
         * On UNIX, we can get either exit code from normal termination
         * or signal number.
         * On Windows, it is either the same exit code or the exception
         * value for an unhandled exception that caused the process
         * to terminate.
         * See MSDN for GetExitCodeProcess() and ntstatus.h for possible
         * well-known codes, e.g. C0000005 ACCESS_DENIED - analog of SIGSEGV
         * References:
         *   https://msdn.microsoft.com/en-us/library/windows/desktop/ms683189(v=vs.85).aspx
         *   https://msdn.microsoft.com/en-us/library/aa260331(v=vs.60).aspx
         */
#ifdef G_OS_WIN32
        /* Additionally WIN32 does not provide any additional information
         * on whetherthe child exited or terminated via signal.
         * We use this simple range check to distingish application exit code
         * (usually value less then 256) and unhandled exception code with
         * ntstatus (always value greater then 0xC0000005). */
        if ((uint32_t)gei->status < 0xC0000000U) {
            ges->has_exitcode = true;
            ges->exitcode = gei->status;
        } else {
            ges->has_signal = true;
            ges->signal = gei->status;
        }
#else
        if (WIFEXITED(gei->status)) {
            ges->has_exitcode = true;
            ges->exitcode = WEXITSTATUS(gei->status);
        } else if (WIFSIGNALED(gei->status)) {
            ges->has_signal = true;
            ges->signal = WTERMSIG(gei->status);
        }
#endif
        QTAILQ_REMOVE(&guest_exec_state.processes, gei, next);
        g_free(gei);
    }

    return ges;
}

/* Get environment variables or arguments array for execve(). */
static char **guest_exec_get_args(const strList *entry, bool log)
{
    const strList *it;
    int count = 1, i = 0;  /* reserve for NULL terminator */
    char **args;
    char *str; /* for logging array of arguments */
    size_t str_size = 1;

    for (it = entry; it != NULL; it = it->next) {
        count++;
        str_size += 1 + strlen(it->value);
    }

    str = g_malloc(str_size);
    *str = 0;
    args = g_malloc(count * sizeof(char *));
    for (it = entry; it != NULL; it = it->next) {
        args[i++] = it->value;
        pstrcat(str, str_size, it->value);
        if (it->next) {
            pstrcat(str, str_size, " ");
        }
    }
    args[i] = NULL;

    if (log) {
        slog("guest-exec called: \"%s\"", str);
    }
    g_free(str);

    return args;
}

static void guest_exec_child_watch(GPid pid, gint status, gpointer data)
{
    GuestExecInfo *gei = (GuestExecInfo *)data;

    g_debug("guest_exec_child_watch called, pid: %d, status: %u",
            (int32_t)gpid_to_int64(pid), (uint32_t)status);

    gei->status = status;
    gei->finished = true;

    g_spawn_close_pid(pid);
}

/** Reset ignored signals back to default. */
static void guest_exec_task_setup(gpointer data)
{
#if !defined(G_OS_WIN32)
    struct sigaction sigact;

    memset(&sigact, 0, sizeof(struct sigaction));
    sigact.sa_handler = SIG_DFL;

    if (sigaction(SIGPIPE, &sigact, NULL) != 0) {
        slog("sigaction() failed to reset child process's SIGPIPE: %s",
             strerror(errno));
    }
#endif
}

GuestExec *qmp_guest_exec(const char *path,
                       bool has_arg, strList *arg,
                       bool has_env, strList *env,
                       bool has_input_data, const char *input_data,
                       bool has_capture_output, bool capture_output,
                       Error **err)
{
    GPid pid;
    GuestExec *ge = NULL;
    GuestExecInfo *gei;
    char **argv, **envp;
    strList arglist;
    gboolean ret;
    GError *gerr = NULL;

    arglist.value = (char *)path;
    arglist.next = has_arg ? arg : NULL;

    argv = guest_exec_get_args(&arglist, true);
    envp = guest_exec_get_args(has_env ? env : NULL, false);

    ret = g_spawn_async_with_pipes(NULL, argv, envp,
            G_SPAWN_SEARCH_PATH |
            G_SPAWN_DO_NOT_REAP_CHILD |
            G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
            guest_exec_task_setup, NULL, &pid, NULL, NULL, NULL, &gerr);
    if (!ret) {
        error_setg(err, QERR_QGA_COMMAND_FAILED, gerr->message);
        g_error_free(gerr);
        goto done;
    }

    ge = g_new0(GuestExec, 1);
    ge->pid = gpid_to_int64(pid);

    gei = guest_exec_info_add(pid);
    g_child_watch_add(pid, guest_exec_child_watch, gei);

done:
    g_free(argv);
    g_free(envp);

    return ge;
}
