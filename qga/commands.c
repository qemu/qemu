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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "guest-agent-core.h"
#include "qga-qapi-commands.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/base64.h"
#include "qemu/cutils.h"
#include "commands-common.h"

/* Maximum captured guest-exec out_data/err_data - 16MB */
#define GUEST_EXEC_MAX_OUTPUT (16 * 1024 * 1024)
/* Allocation and I/O buffer for reading guest-exec out_data/err_data - 4KB */
#define GUEST_EXEC_IO_SIZE (4 * 1024)
/*
 * Maximum file size to read - 48MB
 *
 * (48MB + Base64 3:4 overhead = JSON parser 64 MB limit)
 */
#define GUEST_FILE_READ_COUNT_MAX (48 * MiB)

/* Note: in some situations, like with the fsfreeze, logging may be
 * temporarily disabled. if it is necessary that a command be able
 * to log for accounting purposes, check ga_logging_enabled() beforehand.
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

static void qmp_command_info(const QmpCommand *cmd, void *opaque)
{
    GuestAgentInfo *info = opaque;
    GuestAgentCommandInfo *cmd_info;

    cmd_info = g_new0(GuestAgentCommandInfo, 1);
    cmd_info->name = g_strdup(qmp_command_name(cmd));
    cmd_info->enabled = qmp_command_is_enabled(cmd);
    cmd_info->success_response = qmp_has_success_response(cmd);

    QAPI_LIST_PREPEND(info->supported_commands, cmd_info);
}

struct GuestAgentInfo *qmp_guest_info(Error **errp)
{
    GuestAgentInfo *info = g_new0(GuestAgentInfo, 1);

    info->version = g_strdup(QEMU_VERSION);
    qmp_for_each_command(&ga_commands, qmp_command_info, info);
    return info;
}

struct GuestExecIOData {
    guchar *data;
    gsize size;
    gsize length;
    bool closed;
    bool truncated;
    const char *name;
};
typedef struct GuestExecIOData GuestExecIOData;

struct GuestExecInfo {
    GPid pid;
    int64_t pid_numeric;
    gint status;
    bool has_output;
    bool finished;
    GuestExecIOData in;
    GuestExecIOData out;
    GuestExecIOData err;
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

GuestExecStatus *qmp_guest_exec_status(int64_t pid, Error **errp)
{
    GuestExecInfo *gei;
    GuestExecStatus *ges;

    slog("guest-exec-status called, pid: %u", (uint32_t)pid);

    gei = guest_exec_info_find(pid);
    if (gei == NULL) {
        error_setg(errp, QERR_INVALID_PARAMETER, "pid");
        return NULL;
    }

    ges = g_new0(GuestExecStatus, 1);

    bool finished = gei->finished;

    /* need to wait till output channels are closed
     * to be sure we captured all output at this point */
    if (gei->has_output) {
        finished &= gei->out.closed && gei->err.closed;
    }

    ges->exited = finished;
    if (finished) {
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
         * on whether the child exited or terminated via signal.
         * We use this simple range check to distinguish application exit code
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
        if (gei->out.length > 0) {
            ges->out_data = g_base64_encode(gei->out.data, gei->out.length);
            g_free(gei->out.data);
            ges->has_out_truncated = gei->out.truncated;
        }

        if (gei->err.length > 0) {
            ges->err_data = g_base64_encode(gei->err.data, gei->err.length);
            g_free(gei->err.data);
            ges->has_err_truncated = gei->err.truncated;
        }

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
    args = g_new(char *, count);
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

static gboolean guest_exec_input_watch(GIOChannel *ch,
        GIOCondition cond, gpointer p_)
{
    GuestExecIOData *p = (GuestExecIOData *)p_;
    gsize bytes_written = 0;
    GIOStatus status;
    GError *gerr = NULL;

    /* nothing left to write */
    if (p->size == p->length) {
        goto done;
    }

    status = g_io_channel_write_chars(ch, (gchar *)p->data + p->length,
            p->size - p->length, &bytes_written, &gerr);

    /* can be not 0 even if not G_IO_STATUS_NORMAL */
    if (bytes_written != 0) {
        p->length += bytes_written;
    }

    /* continue write, our callback will be called again */
    if (status == G_IO_STATUS_NORMAL || status == G_IO_STATUS_AGAIN) {
        return true;
    }

    if (gerr) {
        g_warning("qga: i/o error writing to input_data channel: %s",
                gerr->message);
        g_error_free(gerr);
    }

done:
    g_io_channel_shutdown(ch, true, NULL);
    g_io_channel_unref(ch);
    p->closed = true;
    g_free(p->data);

    return false;
}

static gboolean guest_exec_output_watch(GIOChannel *ch,
        GIOCondition cond, gpointer p_)
{
    GuestExecIOData *p = (GuestExecIOData *)p_;
    gsize bytes_read;
    GIOStatus gstatus;

    if (cond == G_IO_HUP || cond == G_IO_ERR) {
        goto close;
    }

    if (p->size == p->length) {
        gpointer t = NULL;
        if (!p->truncated && p->size < GUEST_EXEC_MAX_OUTPUT) {
            t = g_try_realloc(p->data, p->size + GUEST_EXEC_IO_SIZE);
        }
        if (t == NULL) {
            /* ignore truncated output */
            gchar buf[GUEST_EXEC_IO_SIZE];

            p->truncated = true;
            gstatus = g_io_channel_read_chars(ch, buf, sizeof(buf),
                                              &bytes_read, NULL);
            if (gstatus == G_IO_STATUS_EOF || gstatus == G_IO_STATUS_ERROR) {
                goto close;
            }

            return true;
        }
        p->size += GUEST_EXEC_IO_SIZE;
        p->data = t;
    }

    /* Calling read API once.
     * On next available data our callback will be called again */
    gstatus = g_io_channel_read_chars(ch, (gchar *)p->data + p->length,
            p->size - p->length, &bytes_read, NULL);
    if (gstatus == G_IO_STATUS_EOF || gstatus == G_IO_STATUS_ERROR) {
        goto close;
    }

    p->length += bytes_read;

    return true;

close:
    g_io_channel_shutdown(ch, true, NULL);
    g_io_channel_unref(ch);
    p->closed = true;
    return false;
}

GuestExec *qmp_guest_exec(const char *path,
                       bool has_arg, strList *arg,
                       bool has_env, strList *env,
                       const char *input_data,
                       bool has_capture_output, bool capture_output,
                       Error **errp)
{
    GPid pid;
    GuestExec *ge = NULL;
    GuestExecInfo *gei;
    char **argv, **envp;
    strList arglist;
    gboolean ret;
    GError *gerr = NULL;
    gint in_fd, out_fd, err_fd;
    GIOChannel *in_ch, *out_ch, *err_ch;
    GSpawnFlags flags;
    bool has_output = (has_capture_output && capture_output);
    g_autofree uint8_t *input = NULL;
    size_t ninput = 0;

    arglist.value = (char *)path;
    arglist.next = has_arg ? arg : NULL;

    if (input_data) {
        input = qbase64_decode(input_data, -1, &ninput, errp);
        if (!input) {
            return NULL;
        }
    }

    argv = guest_exec_get_args(&arglist, true);
    envp = has_env ? guest_exec_get_args(env, false) : NULL;

    flags = G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
        G_SPAWN_SEARCH_PATH_FROM_ENVP;
    if (!has_output) {
        flags |= G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL;
    }

    ret = g_spawn_async_with_pipes(NULL, argv, envp, flags,
            guest_exec_task_setup, NULL, &pid, input_data ? &in_fd : NULL,
            has_output ? &out_fd : NULL, has_output ? &err_fd : NULL, &gerr);
    if (!ret) {
        error_setg(errp, QERR_QGA_COMMAND_FAILED, gerr->message);
        g_error_free(gerr);
        goto done;
    }

    ge = g_new0(GuestExec, 1);
    ge->pid = gpid_to_int64(pid);

    gei = guest_exec_info_add(pid);
    gei->has_output = has_output;
    g_child_watch_add(pid, guest_exec_child_watch, gei);

    if (input_data) {
        gei->in.data = g_steal_pointer(&input);
        gei->in.size = ninput;
#ifdef G_OS_WIN32
        in_ch = g_io_channel_win32_new_fd(in_fd);
#else
        in_ch = g_io_channel_unix_new(in_fd);
#endif
        g_io_channel_set_encoding(in_ch, NULL, NULL);
        g_io_channel_set_buffered(in_ch, false);
        g_io_channel_set_flags(in_ch, G_IO_FLAG_NONBLOCK, NULL);
        g_io_channel_set_close_on_unref(in_ch, true);
        g_io_add_watch(in_ch, G_IO_OUT, guest_exec_input_watch, &gei->in);
    }

    if (has_output) {
#ifdef G_OS_WIN32
        out_ch = g_io_channel_win32_new_fd(out_fd);
        err_ch = g_io_channel_win32_new_fd(err_fd);
#else
        out_ch = g_io_channel_unix_new(out_fd);
        err_ch = g_io_channel_unix_new(err_fd);
#endif
        g_io_channel_set_encoding(out_ch, NULL, NULL);
        g_io_channel_set_encoding(err_ch, NULL, NULL);
        g_io_channel_set_buffered(out_ch, false);
        g_io_channel_set_buffered(err_ch, false);
        g_io_channel_set_close_on_unref(out_ch, true);
        g_io_channel_set_close_on_unref(err_ch, true);
        g_io_add_watch(out_ch, G_IO_IN | G_IO_HUP,
                guest_exec_output_watch, &gei->out);
        g_io_add_watch(err_ch, G_IO_IN | G_IO_HUP,
                guest_exec_output_watch, &gei->err);
    }

done:
    g_free(argv);
    g_free(envp);

    return ge;
}

/* Convert GuestFileWhence (either a raw integer or an enum value) into
 * the guest's SEEK_ constants.  */
int ga_parse_whence(GuestFileWhence *whence, Error **errp)
{
    /*
     * Exploit the fact that we picked values to match QGA_SEEK_*;
     * however, we have to use a temporary variable since the union
     * members may have different size.
     */
    if (whence->type == QTYPE_QSTRING) {
        int value = whence->u.name;
        whence->type = QTYPE_QNUM;
        whence->u.value = value;
    }
    switch (whence->u.value) {
    case QGA_SEEK_SET:
        return SEEK_SET;
    case QGA_SEEK_CUR:
        return SEEK_CUR;
    case QGA_SEEK_END:
        return SEEK_END;
    }
    error_setg(errp, "invalid whence code %"PRId64, whence->u.value);
    return -1;
}

GuestHostName *qmp_guest_get_host_name(Error **errp)
{
    GuestHostName *result = NULL;
    g_autofree char *hostname = qga_get_host_name(errp);

    /*
     * We want to avoid using g_get_host_name() because that
     * caches the result and we wouldn't reflect changes in the
     * host name.
     */

    if (!hostname) {
        hostname = g_strdup("localhost");
    }

    result = g_new0(GuestHostName, 1);
    result->host_name = g_steal_pointer(&hostname);
    return result;
}

GuestTimezone *qmp_guest_get_timezone(Error **errp)
{
    GuestTimezone *info = NULL;
    GTimeZone *tz = NULL;
    gint64 now = 0;
    gint32 intv = 0;
    gchar const *name = NULL;

    info = g_new0(GuestTimezone, 1);
    tz = g_time_zone_new_local();
    if (tz == NULL) {
        error_setg(errp, QERR_QGA_COMMAND_FAILED,
                   "Couldn't retrieve local timezone");
        goto error;
    }

    now = g_get_real_time() / G_USEC_PER_SEC;
    intv = g_time_zone_find_interval(tz, G_TIME_TYPE_UNIVERSAL, now);
    info->offset = g_time_zone_get_offset(tz, intv);
    name = g_time_zone_get_abbreviation(tz, intv);
    if (name != NULL) {
        info->zone = g_strdup(name);
    }
    g_time_zone_unref(tz);

    return info;

error:
    g_free(info);
    return NULL;
}

GuestFileRead *qmp_guest_file_read(int64_t handle, bool has_count,
                                   int64_t count, Error **errp)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);
    GuestFileRead *read_data;

    if (!gfh) {
        return NULL;
    }
    if (!has_count) {
        count = QGA_READ_COUNT_DEFAULT;
    } else if (count < 0 || count > GUEST_FILE_READ_COUNT_MAX) {
        error_setg(errp, "value '%" PRId64 "' is invalid for argument count",
                   count);
        return NULL;
    }

    read_data = guest_file_read_unsafe(gfh, count, errp);
    if (!read_data) {
        slog("guest-file-write failed, handle: %" PRId64, handle);
    }

    return read_data;
}

int64_t qmp_guest_get_time(Error **errp)
{
    return g_get_real_time() * 1000;
}
