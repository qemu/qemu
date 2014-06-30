/*
 * QEMU Guest Agent
 *
 * Copyright IBM Corp. 2011
 *
 * Authors:
 *  Adam Litke        <aglitke@linux.vnet.ibm.com>
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <glib.h>
#include <getopt.h>
#include <glib/gstdio.h>
#ifndef _WIN32
#include <syslog.h>
#include <sys/wait.h>
#include <sys/stat.h>
#endif
#include "qapi/qmp/json-streamer.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qjson.h"
#include "qga/guest-agent-core.h"
#include "qemu/module.h"
#include "signal.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/dispatch.h"
#include "qga/channel.h"
#include "qemu/bswap.h"
#ifdef _WIN32
#include "qga/service-win32.h"
#include "qga/vss-win32.h"
#endif
#ifdef __linux__
#include <linux/fs.h>
#ifdef FIFREEZE
#define CONFIG_FSFREEZE
#endif
#endif

#ifndef _WIN32
#define QGA_VIRTIO_PATH_DEFAULT "/dev/virtio-ports/org.qemu.guest_agent.0"
#define QGA_STATE_RELATIVE_DIR  "run"
#define QGA_SERIAL_PATH_DEFAULT "/dev/ttyS0"
#else
#define QGA_VIRTIO_PATH_DEFAULT "\\\\.\\Global\\org.qemu.guest_agent.0"
#define QGA_STATE_RELATIVE_DIR  "qemu-ga"
#define QGA_SERIAL_PATH_DEFAULT "COM1"
#endif
#ifdef CONFIG_FSFREEZE
#define QGA_FSFREEZE_HOOK_DEFAULT CONFIG_QEMU_CONFDIR "/fsfreeze-hook"
#endif
#define QGA_SENTINEL_BYTE 0xFF

static struct {
    const char *state_dir;
    const char *pidfile;
} dfl_pathnames;

typedef struct GAPersistentState {
#define QGA_PSTATE_DEFAULT_FD_COUNTER 1000
    int64_t fd_counter;
} GAPersistentState;

struct GAState {
    JSONMessageParser parser;
    GMainLoop *main_loop;
    GAChannel *channel;
    bool virtio; /* fastpath to check for virtio to deal with poll() quirks */
    GACommandState *command_state;
    GLogLevelFlags log_level;
    FILE *log_file;
    bool logging_enabled;
#ifdef _WIN32
    GAService service;
#endif
    bool delimit_response;
    bool frozen;
    GList *blacklist;
    const char *state_filepath_isfrozen;
    struct {
        const char *log_filepath;
        const char *pid_filepath;
    } deferred_options;
#ifdef CONFIG_FSFREEZE
    const char *fsfreeze_hook;
#endif
    const gchar *pstate_filepath;
    GAPersistentState pstate;
};

struct GAState *ga_state;

/* commands that are safe to issue while filesystems are frozen */
static const char *ga_freeze_whitelist[] = {
    "guest-ping",
    "guest-info",
    "guest-sync",
    "guest-sync-delimited",
    "guest-fsfreeze-status",
    "guest-fsfreeze-thaw",
    NULL
};

#ifdef _WIN32
DWORD WINAPI service_ctrl_handler(DWORD ctrl, DWORD type, LPVOID data,
                                  LPVOID ctx);
VOID WINAPI service_main(DWORD argc, TCHAR *argv[]);
#endif

static void
init_dfl_pathnames(void)
{
    g_assert(dfl_pathnames.state_dir == NULL);
    g_assert(dfl_pathnames.pidfile == NULL);
    dfl_pathnames.state_dir = qemu_get_local_state_pathname(
      QGA_STATE_RELATIVE_DIR);
    dfl_pathnames.pidfile   = qemu_get_local_state_pathname(
      QGA_STATE_RELATIVE_DIR G_DIR_SEPARATOR_S "qemu-ga.pid");
}

static void quit_handler(int sig)
{
    /* if we're frozen, don't exit unless we're absolutely forced to,
     * because it's basically impossible for graceful exit to complete
     * unless all log/pid files are on unfreezable filesystems. there's
     * also a very likely chance killing the agent before unfreezing
     * the filesystems is a mistake (or will be viewed as one later).
     */
    if (ga_is_frozen(ga_state)) {
        return;
    }
    g_debug("received signal num %d, quitting", sig);

    if (g_main_loop_is_running(ga_state->main_loop)) {
        g_main_loop_quit(ga_state->main_loop);
    }
}

#ifndef _WIN32
static gboolean register_signal_handlers(void)
{
    struct sigaction sigact;
    int ret;

    memset(&sigact, 0, sizeof(struct sigaction));
    sigact.sa_handler = quit_handler;

    ret = sigaction(SIGINT, &sigact, NULL);
    if (ret == -1) {
        g_error("error configuring signal handler: %s", strerror(errno));
    }
    ret = sigaction(SIGTERM, &sigact, NULL);
    if (ret == -1) {
        g_error("error configuring signal handler: %s", strerror(errno));
    }

    return true;
}

/* TODO: use this in place of all post-fork() fclose(std*) callers */
void reopen_fd_to_null(int fd)
{
    int nullfd;

    nullfd = open("/dev/null", O_RDWR);
    if (nullfd < 0) {
        return;
    }

    dup2(nullfd, fd);

    if (nullfd != fd) {
        close(nullfd);
    }
}
#endif

static void usage(const char *cmd)
{
    printf(
"Usage: %s [-m <method> -p <path>] [<options>]\n"
"QEMU Guest Agent %s\n"
"\n"
"  -m, --method      transport method: one of unix-listen, virtio-serial, or\n"
"                    isa-serial (virtio-serial is the default)\n"
"  -p, --path        device/socket path (the default for virtio-serial is:\n"
"                    %s,\n"
"                    the default for isa-serial is:\n"
"                    %s)\n"
"  -l, --logfile     set logfile path, logs to stderr by default\n"
"  -f, --pidfile     specify pidfile (default is %s)\n"
#ifdef CONFIG_FSFREEZE
"  -F, --fsfreeze-hook\n"
"                    enable fsfreeze hook. Accepts an optional argument that\n"
"                    specifies script to run on freeze/thaw. Script will be\n"
"                    called with 'freeze'/'thaw' arguments accordingly.\n"
"                    (default is %s)\n"
"                    If using -F with an argument, do not follow -F with a\n"
"                    space.\n"
"                    (for example: -F/var/run/fsfreezehook.sh)\n"
#endif
"  -t, --statedir    specify dir to store state information (absolute paths\n"
"                    only, default is %s)\n"
"  -v, --verbose     log extra debugging information\n"
"  -V, --version     print version information and exit\n"
"  -d, --daemonize   become a daemon\n"
#ifdef _WIN32
"  -s, --service     service commands: install, uninstall\n"
#endif
"  -b, --blacklist   comma-separated list of RPCs to disable (no spaces, \"?\"\n"
"                    to list available RPCs)\n"
"  -h, --help        display this help and exit\n"
"\n"
"Report bugs to <mdroth@linux.vnet.ibm.com>\n"
    , cmd, QEMU_VERSION, QGA_VIRTIO_PATH_DEFAULT, QGA_SERIAL_PATH_DEFAULT,
    dfl_pathnames.pidfile,
#ifdef CONFIG_FSFREEZE
    QGA_FSFREEZE_HOOK_DEFAULT,
#endif
    dfl_pathnames.state_dir);
}

static const char *ga_log_level_str(GLogLevelFlags level)
{
    switch (level & G_LOG_LEVEL_MASK) {
        case G_LOG_LEVEL_ERROR:
            return "error";
        case G_LOG_LEVEL_CRITICAL:
            return "critical";
        case G_LOG_LEVEL_WARNING:
            return "warning";
        case G_LOG_LEVEL_MESSAGE:
            return "message";
        case G_LOG_LEVEL_INFO:
            return "info";
        case G_LOG_LEVEL_DEBUG:
            return "debug";
        default:
            return "user";
    }
}

bool ga_logging_enabled(GAState *s)
{
    return s->logging_enabled;
}

void ga_disable_logging(GAState *s)
{
    s->logging_enabled = false;
}

void ga_enable_logging(GAState *s)
{
    s->logging_enabled = true;
}

static void ga_log(const gchar *domain, GLogLevelFlags level,
                   const gchar *msg, gpointer opaque)
{
    GAState *s = opaque;
    GTimeVal time;
    const char *level_str = ga_log_level_str(level);

    if (!ga_logging_enabled(s)) {
        return;
    }

    level &= G_LOG_LEVEL_MASK;
#ifndef _WIN32
    if (domain && strcmp(domain, "syslog") == 0) {
        syslog(LOG_INFO, "%s: %s", level_str, msg);
    } else if (level & s->log_level) {
#else
    if (level & s->log_level) {
#endif
        g_get_current_time(&time);
        fprintf(s->log_file,
                "%lu.%lu: %s: %s\n", time.tv_sec, time.tv_usec, level_str, msg);
        fflush(s->log_file);
    }
}

void ga_set_response_delimited(GAState *s)
{
    s->delimit_response = true;
}

static FILE *ga_open_logfile(const char *logfile)
{
    FILE *f;

    f = fopen(logfile, "a");
    if (!f) {
        return NULL;
    }

    qemu_set_cloexec(fileno(f));
    return f;
}

#ifndef _WIN32
static bool ga_open_pidfile(const char *pidfile)
{
    int pidfd;
    char pidstr[32];

    pidfd = qemu_open(pidfile, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR);
    if (pidfd == -1 || lockf(pidfd, F_TLOCK, 0)) {
        g_critical("Cannot lock pid file, %s", strerror(errno));
        if (pidfd != -1) {
            close(pidfd);
        }
        return false;
    }

    if (ftruncate(pidfd, 0)) {
        g_critical("Failed to truncate pid file");
        goto fail;
    }
    snprintf(pidstr, sizeof(pidstr), "%d\n", getpid());
    if (write(pidfd, pidstr, strlen(pidstr)) != strlen(pidstr)) {
        g_critical("Failed to write pid file");
        goto fail;
    }

    /* keep pidfile open & locked forever */
    return true;

fail:
    unlink(pidfile);
    close(pidfd);
    return false;
}
#else /* _WIN32 */
static bool ga_open_pidfile(const char *pidfile)
{
    return true;
}
#endif

static gint ga_strcmp(gconstpointer str1, gconstpointer str2)
{
    return strcmp(str1, str2);
}

/* disable commands that aren't safe for fsfreeze */
static void ga_disable_non_whitelisted(QmpCommand *cmd, void *opaque)
{
    bool whitelisted = false;
    int i = 0;
    const char *name = qmp_command_name(cmd);

    while (ga_freeze_whitelist[i] != NULL) {
        if (strcmp(name, ga_freeze_whitelist[i]) == 0) {
            whitelisted = true;
        }
        i++;
    }
    if (!whitelisted) {
        g_debug("disabling command: %s", name);
        qmp_disable_command(name);
    }
}

/* [re-]enable all commands, except those explicitly blacklisted by user */
static void ga_enable_non_blacklisted(QmpCommand *cmd, void *opaque)
{
    GList *blacklist = opaque;
    const char *name = qmp_command_name(cmd);

    if (g_list_find_custom(blacklist, name, ga_strcmp) == NULL &&
        !qmp_command_is_enabled(cmd)) {
        g_debug("enabling command: %s", name);
        qmp_enable_command(name);
    }
}

static bool ga_create_file(const char *path)
{
    int fd = open(path, O_CREAT | O_WRONLY, S_IWUSR | S_IRUSR);
    if (fd == -1) {
        g_warning("unable to open/create file %s: %s", path, strerror(errno));
        return false;
    }
    close(fd);
    return true;
}

static bool ga_delete_file(const char *path)
{
    int ret = unlink(path);
    if (ret == -1) {
        g_warning("unable to delete file: %s: %s", path, strerror(errno));
        return false;
    }

    return true;
}

bool ga_is_frozen(GAState *s)
{
    return s->frozen;
}

void ga_set_frozen(GAState *s)
{
    if (ga_is_frozen(s)) {
        return;
    }
    /* disable all non-whitelisted (for frozen state) commands */
    qmp_for_each_command(ga_disable_non_whitelisted, NULL);
    g_warning("disabling logging due to filesystem freeze");
    ga_disable_logging(s);
    s->frozen = true;
    if (!ga_create_file(s->state_filepath_isfrozen)) {
        g_warning("unable to create %s, fsfreeze may not function properly",
                  s->state_filepath_isfrozen);
    }
}

void ga_unset_frozen(GAState *s)
{
    if (!ga_is_frozen(s)) {
        return;
    }

    /* if we delayed creation/opening of pid/log files due to being
     * in a frozen state at start up, do it now
     */
    if (s->deferred_options.log_filepath) {
        s->log_file = ga_open_logfile(s->deferred_options.log_filepath);
        if (!s->log_file) {
            s->log_file = stderr;
        }
        s->deferred_options.log_filepath = NULL;
    }
    ga_enable_logging(s);
    g_warning("logging re-enabled due to filesystem unfreeze");
    if (s->deferred_options.pid_filepath) {
        if (!ga_open_pidfile(s->deferred_options.pid_filepath)) {
            g_warning("failed to create/open pid file");
        }
        s->deferred_options.pid_filepath = NULL;
    }

    /* enable all disabled, non-blacklisted commands */
    qmp_for_each_command(ga_enable_non_blacklisted, s->blacklist);
    s->frozen = false;
    if (!ga_delete_file(s->state_filepath_isfrozen)) {
        g_warning("unable to delete %s, fsfreeze may not function properly",
                  s->state_filepath_isfrozen);
    }
}

#ifdef CONFIG_FSFREEZE
const char *ga_fsfreeze_hook(GAState *s)
{
    return s->fsfreeze_hook;
}
#endif

static void become_daemon(const char *pidfile)
{
#ifndef _WIN32
    pid_t pid, sid;

    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (pidfile) {
        if (!ga_open_pidfile(pidfile)) {
            g_critical("failed to create pidfile");
            exit(EXIT_FAILURE);
        }
    }

    umask(S_IRWXG | S_IRWXO);
    sid = setsid();
    if (sid < 0) {
        goto fail;
    }
    if ((chdir("/")) < 0) {
        goto fail;
    }

    reopen_fd_to_null(STDIN_FILENO);
    reopen_fd_to_null(STDOUT_FILENO);
    reopen_fd_to_null(STDERR_FILENO);
    return;

fail:
    if (pidfile) {
        unlink(pidfile);
    }
    g_critical("failed to daemonize");
    exit(EXIT_FAILURE);
#endif
}

static int send_response(GAState *s, QObject *payload)
{
    const char *buf;
    QString *payload_qstr, *response_qstr;
    GIOStatus status;

    g_assert(payload && s->channel);

    payload_qstr = qobject_to_json(payload);
    if (!payload_qstr) {
        return -EINVAL;
    }

    if (s->delimit_response) {
        s->delimit_response = false;
        response_qstr = qstring_new();
        qstring_append_chr(response_qstr, QGA_SENTINEL_BYTE);
        qstring_append(response_qstr, qstring_get_str(payload_qstr));
        QDECREF(payload_qstr);
    } else {
        response_qstr = payload_qstr;
    }

    qstring_append_chr(response_qstr, '\n');
    buf = qstring_get_str(response_qstr);
    status = ga_channel_write_all(s->channel, buf, strlen(buf));
    QDECREF(response_qstr);
    if (status != G_IO_STATUS_NORMAL) {
        return -EIO;
    }

    return 0;
}

static void process_command(GAState *s, QDict *req)
{
    QObject *rsp = NULL;
    int ret;

    g_assert(req);
    g_debug("processing command");
    rsp = qmp_dispatch(QOBJECT(req));
    if (rsp) {
        ret = send_response(s, rsp);
        if (ret) {
            g_warning("error sending response: %s", strerror(ret));
        }
        qobject_decref(rsp);
    }
}

/* handle requests/control events coming in over the channel */
static void process_event(JSONMessageParser *parser, QList *tokens)
{
    GAState *s = container_of(parser, GAState, parser);
    QObject *obj;
    QDict *qdict;
    Error *err = NULL;
    int ret;

    g_assert(s && parser);

    g_debug("process_event: called");
    obj = json_parser_parse_err(tokens, NULL, &err);
    if (err || !obj || qobject_type(obj) != QTYPE_QDICT) {
        qobject_decref(obj);
        qdict = qdict_new();
        if (!err) {
            g_warning("failed to parse event: unknown error");
            error_set(&err, QERR_JSON_PARSING);
        } else {
            g_warning("failed to parse event: %s", error_get_pretty(err));
        }
        qdict_put_obj(qdict, "error", qmp_build_error_object(err));
        error_free(err);
    } else {
        qdict = qobject_to_qdict(obj);
    }

    g_assert(qdict);

    /* handle host->guest commands */
    if (qdict_haskey(qdict, "execute")) {
        process_command(s, qdict);
    } else {
        if (!qdict_haskey(qdict, "error")) {
            QDECREF(qdict);
            qdict = qdict_new();
            g_warning("unrecognized payload format");
            error_set(&err, QERR_UNSUPPORTED);
            qdict_put_obj(qdict, "error", qmp_build_error_object(err));
            error_free(err);
        }
        ret = send_response(s, QOBJECT(qdict));
        if (ret) {
            g_warning("error sending error response: %s", strerror(ret));
        }
    }

    QDECREF(qdict);
}

/* false return signals GAChannel to close the current client connection */
static gboolean channel_event_cb(GIOCondition condition, gpointer data)
{
    GAState *s = data;
    gchar buf[QGA_READ_COUNT_DEFAULT+1];
    gsize count;
    GError *err = NULL;
    GIOStatus status = ga_channel_read(s->channel, buf, QGA_READ_COUNT_DEFAULT, &count);
    if (err != NULL) {
        g_warning("error reading channel: %s", err->message);
        g_error_free(err);
        return false;
    }
    switch (status) {
    case G_IO_STATUS_ERROR:
        g_warning("error reading channel");
        return false;
    case G_IO_STATUS_NORMAL:
        buf[count] = 0;
        g_debug("read data, count: %d, data: %s", (int)count, buf);
        json_message_parser_feed(&s->parser, (char *)buf, (int)count);
        break;
    case G_IO_STATUS_EOF:
        g_debug("received EOF");
        if (!s->virtio) {
            return false;
        }
        /* fall through */
    case G_IO_STATUS_AGAIN:
        /* virtio causes us to spin here when no process is attached to
         * host-side chardev. sleep a bit to mitigate this
         */
        if (s->virtio) {
            usleep(100*1000);
        }
        return true;
    default:
        g_warning("unknown channel read status, closing");
        return false;
    }
    return true;
}

static gboolean channel_init(GAState *s, const gchar *method, const gchar *path)
{
    GAChannelMethod channel_method;

    if (method == NULL) {
        method = "virtio-serial";
    }

    if (path == NULL) {
        if (strcmp(method, "virtio-serial") == 0 ) {
            /* try the default path for the virtio-serial port */
            path = QGA_VIRTIO_PATH_DEFAULT;
        } else if (strcmp(method, "isa-serial") == 0){
            /* try the default path for the serial port - COM1 */
            path = QGA_SERIAL_PATH_DEFAULT;
        } else {
            g_critical("must specify a path for this channel");
            return false;
        }
    }

    if (strcmp(method, "virtio-serial") == 0) {
        s->virtio = true; /* virtio requires special handling in some cases */
        channel_method = GA_CHANNEL_VIRTIO_SERIAL;
    } else if (strcmp(method, "isa-serial") == 0) {
        channel_method = GA_CHANNEL_ISA_SERIAL;
    } else if (strcmp(method, "unix-listen") == 0) {
        channel_method = GA_CHANNEL_UNIX_LISTEN;
    } else {
        g_critical("unsupported channel method/type: %s", method);
        return false;
    }

    s->channel = ga_channel_new(channel_method, path, channel_event_cb, s);
    if (!s->channel) {
        g_critical("failed to create guest agent channel");
        return false;
    }

    return true;
}

#ifdef _WIN32
DWORD WINAPI service_ctrl_handler(DWORD ctrl, DWORD type, LPVOID data,
                                  LPVOID ctx)
{
    DWORD ret = NO_ERROR;
    GAService *service = &ga_state->service;

    switch (ctrl)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            quit_handler(SIGTERM);
            service->status.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(service->status_handle, &service->status);
            break;

        default:
            ret = ERROR_CALL_NOT_IMPLEMENTED;
    }
    return ret;
}

VOID WINAPI service_main(DWORD argc, TCHAR *argv[])
{
    GAService *service = &ga_state->service;

    service->status_handle = RegisterServiceCtrlHandlerEx(QGA_SERVICE_NAME,
        service_ctrl_handler, NULL);

    if (service->status_handle == 0) {
        g_critical("Failed to register extended requests function!\n");
        return;
    }

    service->status.dwServiceType = SERVICE_WIN32;
    service->status.dwCurrentState = SERVICE_RUNNING;
    service->status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    service->status.dwWin32ExitCode = NO_ERROR;
    service->status.dwServiceSpecificExitCode = NO_ERROR;
    service->status.dwCheckPoint = 0;
    service->status.dwWaitHint = 0;
    SetServiceStatus(service->status_handle, &service->status);

    g_main_loop_run(ga_state->main_loop);

    service->status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(service->status_handle, &service->status);
}
#endif

static void set_persistent_state_defaults(GAPersistentState *pstate)
{
    g_assert(pstate);
    pstate->fd_counter = QGA_PSTATE_DEFAULT_FD_COUNTER;
}

static void persistent_state_from_keyfile(GAPersistentState *pstate,
                                          GKeyFile *keyfile)
{
    g_assert(pstate);
    g_assert(keyfile);
    /* if any fields are missing, either because the file was tampered with
     * by agents of chaos, or because the field wasn't present at the time the
     * file was created, the best we can ever do is start over with the default
     * values. so load them now, and ignore any errors in accessing key-value
     * pairs
     */
    set_persistent_state_defaults(pstate);

    if (g_key_file_has_key(keyfile, "global", "fd_counter", NULL)) {
        pstate->fd_counter =
            g_key_file_get_integer(keyfile, "global", "fd_counter", NULL);
    }
}

static void persistent_state_to_keyfile(const GAPersistentState *pstate,
                                        GKeyFile *keyfile)
{
    g_assert(pstate);
    g_assert(keyfile);

    g_key_file_set_integer(keyfile, "global", "fd_counter", pstate->fd_counter);
}

static gboolean write_persistent_state(const GAPersistentState *pstate,
                                       const gchar *path)
{
    GKeyFile *keyfile = g_key_file_new();
    GError *gerr = NULL;
    gboolean ret = true;
    gchar *data = NULL;
    gsize data_len;

    g_assert(pstate);

    persistent_state_to_keyfile(pstate, keyfile);
    data = g_key_file_to_data(keyfile, &data_len, &gerr);
    if (gerr) {
        g_critical("failed to convert persistent state to string: %s",
                   gerr->message);
        ret = false;
        goto out;
    }

    g_file_set_contents(path, data, data_len, &gerr);
    if (gerr) {
        g_critical("failed to write persistent state to %s: %s",
                    path, gerr->message);
        ret = false;
        goto out;
    }

out:
    if (gerr) {
        g_error_free(gerr);
    }
    if (keyfile) {
        g_key_file_free(keyfile);
    }
    g_free(data);
    return ret;
}

static gboolean read_persistent_state(GAPersistentState *pstate,
                                      const gchar *path, gboolean frozen)
{
    GKeyFile *keyfile = NULL;
    GError *gerr = NULL;
    struct stat st;
    gboolean ret = true;

    g_assert(pstate);

    if (stat(path, &st) == -1) {
        /* it's okay if state file doesn't exist, but any other error
         * indicates a permissions issue or some other misconfiguration
         * that we likely won't be able to recover from.
         */
        if (errno != ENOENT) {
            g_critical("unable to access state file at path %s: %s",
                       path, strerror(errno));
            ret = false;
            goto out;
        }

        /* file doesn't exist. initialize state to default values and
         * attempt to save now. (we could wait till later when we have
         * modified state we need to commit, but if there's a problem,
         * such as a missing parent directory, we want to catch it now)
         *
         * there is a potential scenario where someone either managed to
         * update the agent from a version that didn't use a key store
         * while qemu-ga thought the filesystem was frozen, or
         * deleted the key store prior to issuing a fsfreeze, prior
         * to restarting the agent. in this case we go ahead and defer
         * initial creation till we actually have modified state to
         * write, otherwise fail to recover from freeze.
         */
        set_persistent_state_defaults(pstate);
        if (!frozen) {
            ret = write_persistent_state(pstate, path);
            if (!ret) {
                g_critical("unable to create state file at path %s", path);
                ret = false;
                goto out;
            }
        }
        ret = true;
        goto out;
    }

    keyfile = g_key_file_new();
    g_key_file_load_from_file(keyfile, path, 0, &gerr);
    if (gerr) {
        g_critical("error loading persistent state from path: %s, %s",
                   path, gerr->message);
        ret = false;
        goto out;
    }

    persistent_state_from_keyfile(pstate, keyfile);

out:
    if (keyfile) {
        g_key_file_free(keyfile);
    }
    if (gerr) {
        g_error_free(gerr);
    }

    return ret;
}

int64_t ga_get_fd_handle(GAState *s, Error **errp)
{
    int64_t handle;

    g_assert(s->pstate_filepath);
    /* we blacklist commands and avoid operations that potentially require
     * writing to disk when we're in a frozen state. this includes opening
     * new files, so we should never get here in that situation
     */
    g_assert(!ga_is_frozen(s));

    handle = s->pstate.fd_counter++;

    /* This should never happen on a reasonable timeframe, as guest-file-open
     * would have to be issued 2^63 times */
    if (s->pstate.fd_counter == INT64_MAX) {
        abort();
    }

    if (!write_persistent_state(&s->pstate, s->pstate_filepath)) {
        error_setg(errp, "failed to commit persistent state to disk");
        return -1;
    }

    return handle;
}

static void ga_print_cmd(QmpCommand *cmd, void *opaque)
{
    printf("%s\n", qmp_command_name(cmd));
}

int main(int argc, char **argv)
{
    const char *sopt = "hVvdm:p:l:f:F::b:s:t:";
    const char *method = NULL, *path = NULL;
    const char *log_filepath = NULL;
    const char *pid_filepath;
#ifdef CONFIG_FSFREEZE
    const char *fsfreeze_hook = NULL;
#endif
    const char *state_dir;
#ifdef _WIN32
    const char *service = NULL;
#endif
    const struct option lopt[] = {
        { "help", 0, NULL, 'h' },
        { "version", 0, NULL, 'V' },
        { "logfile", 1, NULL, 'l' },
        { "pidfile", 1, NULL, 'f' },
#ifdef CONFIG_FSFREEZE
        { "fsfreeze-hook", 2, NULL, 'F' },
#endif
        { "verbose", 0, NULL, 'v' },
        { "method", 1, NULL, 'm' },
        { "path", 1, NULL, 'p' },
        { "daemonize", 0, NULL, 'd' },
        { "blacklist", 1, NULL, 'b' },
#ifdef _WIN32
        { "service", 1, NULL, 's' },
#endif
        { "statedir", 1, NULL, 't' },
        { NULL, 0, NULL, 0 }
    };
    int opt_ind = 0, ch, daemonize = 0, i, j, len;
    GLogLevelFlags log_level = G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL;
    GList *blacklist = NULL;
    GAState *s;

    module_call_init(MODULE_INIT_QAPI);

    init_dfl_pathnames();
    pid_filepath = dfl_pathnames.pidfile;
    state_dir = dfl_pathnames.state_dir;

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 'm':
            method = optarg;
            break;
        case 'p':
            path = optarg;
            break;
        case 'l':
            log_filepath = optarg;
            break;
        case 'f':
            pid_filepath = optarg;
            break;
#ifdef CONFIG_FSFREEZE
        case 'F':
            fsfreeze_hook = optarg ? optarg : QGA_FSFREEZE_HOOK_DEFAULT;
            break;
#endif
        case 't':
             state_dir = optarg;
             break;
        case 'v':
            /* enable all log levels */
            log_level = G_LOG_LEVEL_MASK;
            break;
        case 'V':
            printf("QEMU Guest Agent %s\n", QEMU_VERSION);
            return 0;
        case 'd':
            daemonize = 1;
            break;
        case 'b': {
            if (is_help_option(optarg)) {
                qmp_for_each_command(ga_print_cmd, NULL);
                return 0;
            }
            for (j = 0, i = 0, len = strlen(optarg); i < len; i++) {
                if (optarg[i] == ',') {
                    optarg[i] = 0;
                    blacklist = g_list_append(blacklist, &optarg[j]);
                    j = i + 1;
                }
            }
            if (j < i) {
                blacklist = g_list_append(blacklist, &optarg[j]);
            }
            break;
        }
#ifdef _WIN32
        case 's':
            service = optarg;
            if (strcmp(service, "install") == 0) {
                const char *fixed_state_dir;

                /* If the user passed the "-t" option, we save that state dir
                 * in the service. Otherwise we let the service fetch the state
                 * dir from the environment when it starts.
                 */
                fixed_state_dir = (state_dir == dfl_pathnames.state_dir) ?
                                  NULL :
                                  state_dir;
                if (ga_install_vss_provider()) {
                    return EXIT_FAILURE;
                }
                if (ga_install_service(path, log_filepath, fixed_state_dir)) {
                    return EXIT_FAILURE;
                }
                return 0;
            } else if (strcmp(service, "uninstall") == 0) {
                ga_uninstall_vss_provider();
                return ga_uninstall_service();
            } else {
                printf("Unknown service command.\n");
                return EXIT_FAILURE;
            }
            break;
#endif
        case 'h':
            usage(argv[0]);
            return 0;
        case '?':
            g_print("Unknown option, try '%s --help' for more information.\n",
                    argv[0]);
            return EXIT_FAILURE;
        }
    }

#ifdef _WIN32
    /* On win32 the state directory is application specific (be it the default
     * or a user override). We got past the command line parsing; let's create
     * the directory (with any intermediate directories). If we run into an
     * error later on, we won't try to clean up the directory, it is considered
     * persistent.
     */
    if (g_mkdir_with_parents(state_dir, S_IRWXU) == -1) {
        g_critical("unable to create (an ancestor of) the state directory"
                   " '%s': %s", state_dir, strerror(errno));
        return EXIT_FAILURE;
    }
#endif

    s = g_malloc0(sizeof(GAState));
    s->log_level = log_level;
    s->log_file = stderr;
#ifdef CONFIG_FSFREEZE
    s->fsfreeze_hook = fsfreeze_hook;
#endif
    g_log_set_default_handler(ga_log, s);
    g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR);
    ga_enable_logging(s);
    s->state_filepath_isfrozen = g_strdup_printf("%s/qga.state.isfrozen",
                                                 state_dir);
    s->pstate_filepath = g_strdup_printf("%s/qga.state", state_dir);
    s->frozen = false;

#ifndef _WIN32
    /* check if a previous instance of qemu-ga exited with filesystems' state
     * marked as frozen. this could be a stale value (a non-qemu-ga process
     * or reboot may have since unfrozen them), but better to require an
     * uneeded unfreeze than to risk hanging on start-up
     */
    struct stat st;
    if (stat(s->state_filepath_isfrozen, &st) == -1) {
        /* it's okay if the file doesn't exist, but if we can't access for
         * some other reason, such as permissions, there's a configuration
         * that needs to be addressed. so just bail now before we get into
         * more trouble later
         */
        if (errno != ENOENT) {
            g_critical("unable to access state file at path %s: %s",
                       s->state_filepath_isfrozen, strerror(errno));
            return EXIT_FAILURE;
        }
    } else {
        g_warning("previous instance appears to have exited with frozen"
                  " filesystems. deferring logging/pidfile creation and"
                  " disabling non-fsfreeze-safe commands until"
                  " guest-fsfreeze-thaw is issued, or filesystems are"
                  " manually unfrozen and the file %s is removed",
                  s->state_filepath_isfrozen);
        s->frozen = true;
    }
#endif

    if (ga_is_frozen(s)) {
        if (daemonize) {
            /* delay opening/locking of pidfile till filesystems are unfrozen */
            s->deferred_options.pid_filepath = pid_filepath;
            become_daemon(NULL);
        }
        if (log_filepath) {
            /* delay opening the log file till filesystems are unfrozen */
            s->deferred_options.log_filepath = log_filepath;
        }
        ga_disable_logging(s);
        qmp_for_each_command(ga_disable_non_whitelisted, NULL);
    } else {
        if (daemonize) {
            become_daemon(pid_filepath);
        }
        if (log_filepath) {
            FILE *log_file = ga_open_logfile(log_filepath);
            if (!log_file) {
                g_critical("unable to open specified log file: %s",
                           strerror(errno));
                goto out_bad;
            }
            s->log_file = log_file;
        }
    }

    /* load persistent state from disk */
    if (!read_persistent_state(&s->pstate,
                               s->pstate_filepath,
                               ga_is_frozen(s))) {
        g_critical("failed to load persistent state");
        goto out_bad;
    }

    blacklist = ga_command_blacklist_init(blacklist);
    if (blacklist) {
        s->blacklist = blacklist;
        do {
            g_debug("disabling command: %s", (char *)blacklist->data);
            qmp_disable_command(blacklist->data);
            blacklist = g_list_next(blacklist);
        } while (blacklist);
    }
    s->command_state = ga_command_state_new();
    ga_command_state_init(s, s->command_state);
    ga_command_state_init_all(s->command_state);
    json_message_parser_init(&s->parser, process_event);
    ga_state = s;
#ifndef _WIN32
    if (!register_signal_handlers()) {
        g_critical("failed to register signal handlers");
        goto out_bad;
    }
#endif

    s->main_loop = g_main_loop_new(NULL, false);
    if (!channel_init(ga_state, method, path)) {
        g_critical("failed to initialize guest agent channel");
        goto out_bad;
    }
#ifndef _WIN32
    g_main_loop_run(ga_state->main_loop);
#else
    if (daemonize) {
        SERVICE_TABLE_ENTRY service_table[] = {
            { (char *)QGA_SERVICE_NAME, service_main }, { NULL, NULL } };
        StartServiceCtrlDispatcher(service_table);
    } else {
        g_main_loop_run(ga_state->main_loop);
    }
#endif

    ga_command_state_cleanup_all(ga_state->command_state);
    ga_channel_free(ga_state->channel);

    if (daemonize) {
        unlink(pid_filepath);
    }
    return 0;

out_bad:
    if (daemonize) {
        unlink(pid_filepath);
    }
    return EXIT_FAILURE;
}
