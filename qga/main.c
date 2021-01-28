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

#include "qemu/osdep.h"
#include <getopt.h>
#include <glib/gstdio.h>
#ifndef _WIN32
#include <syslog.h>
#include <sys/wait.h>
#endif
#include "qemu-common.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "guest-agent-core.h"
#include "qga-qapi-init-commands.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "channel.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"
#include "qemu/sockets.h"
#include "qemu/systemd.h"
#include "qemu-version.h"
#ifdef _WIN32
#include <dbt.h>
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
#define QGA_CONF_DEFAULT CONFIG_QEMU_CONFDIR G_DIR_SEPARATOR_S "qemu-ga.conf"
#define QGA_RETRY_INTERVAL 5

static struct {
    const char *state_dir;
    const char *pidfile;
} dfl_pathnames;

typedef struct GAPersistentState {
#define QGA_PSTATE_DEFAULT_FD_COUNTER 1000
    int64_t fd_counter;
} GAPersistentState;

typedef struct GAConfig GAConfig;

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
    HANDLE wakeup_event;
#endif
    bool delimit_response;
    bool frozen;
    GList *blacklist;
    char *state_filepath_isfrozen;
    struct {
        const char *log_filepath;
        const char *pid_filepath;
    } deferred_options;
#ifdef CONFIG_FSFREEZE
    const char *fsfreeze_hook;
#endif
    gchar *pstate_filepath;
    GAPersistentState pstate;
    GAConfig *config;
    int socket_activation;
    bool force_exit;
};

struct GAState *ga_state;
QmpCommandList ga_commands;

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
DWORD WINAPI handle_serial_device_events(DWORD type, LPVOID data);
VOID WINAPI service_main(DWORD argc, TCHAR *argv[]);
#endif
static int run_agent(GAState *s);
static void stop_agent(GAState *s, bool requested);

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
     * On Windows the freeze interval is limited to 10 seconds, so
     * we should quit, but first we should wait for the timeout, thaw
     * the filesystem and quit.
     */
    if (ga_is_frozen(ga_state)) {
#ifdef _WIN32
        int i = 0;
        Error *err = NULL;
        HANDLE hEventTimeout;

        g_debug("Thawing filesystems before exiting");

        hEventTimeout = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVENT_NAME_TIMEOUT);
        if (hEventTimeout) {
            WaitForSingleObject(hEventTimeout, 0);
            CloseHandle(hEventTimeout);
        }
        qga_vss_fsfreeze(&i, false, NULL, &err);
        if (err) {
            g_debug("Error unfreezing filesystems prior to exiting: %s",
                error_get_pretty(err));
            error_free(err);
        }
#else
        return;
#endif
    }
    g_debug("received signal num %d, quitting", sig);

    stop_agent(ga_state, true);
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

    sigact.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sigact, NULL) != 0) {
        g_error("error configuring SIGPIPE signal handler: %s",
                strerror(errno));
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
"QEMU Guest Agent " QEMU_FULL_VERSION "\n"
QEMU_COPYRIGHT "\n"
"\n"
"  -m, --method      transport method: one of unix-listen, virtio-serial,\n"
"                    isa-serial, or vsock-listen (virtio-serial is the default)\n"
"  -p, --path        device/socket path (the default for virtio-serial is:\n"
"                    %s,\n"
"                    the default for isa-serial is:\n"
"                    %s).\n"
"                    Socket addresses for vsock-listen are written as\n"
"                    <cid>:<port>.\n"
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
"  -s, --service     service commands: install, uninstall, vss-install, vss-uninstall\n"
#endif
"  -b, --blacklist   comma-separated list of RPCs to disable (no spaces, \"?\"\n"
"                    to list available RPCs)\n"
"  -D, --dump-conf   dump a qemu-ga config file based on current config\n"
"                    options / command-line parameters to stdout\n"
"  -r, --retry-path  attempt re-opening path if it's unavailable or closed\n"
"                    due to an error which may be recoverable in the future\n"
"                    (virtio-serial driver re-install, serial device hot\n"
"                    plug/unplug, etc.)\n"
"  -h, --help        display this help and exit\n"
"\n"
QEMU_HELP_BOTTOM "\n"
    , cmd, QGA_VIRTIO_PATH_DEFAULT, QGA_SERIAL_PATH_DEFAULT,
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
    if (g_strcmp0(domain, "syslog") == 0) {
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

static gint ga_strcmp(gconstpointer str1, gconstpointer str2)
{
    return strcmp(str1, str2);
}

/* disable commands that aren't safe for fsfreeze */
static void ga_disable_non_whitelisted(const QmpCommand *cmd, void *opaque)
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
        qmp_disable_command(&ga_commands, name);
    }
}

/* [re-]enable all commands, except those explicitly blacklisted by user */
static void ga_enable_non_blacklisted(const QmpCommand *cmd, void *opaque)
{
    GList *blacklist = opaque;
    const char *name = qmp_command_name(cmd);

    if (g_list_find_custom(blacklist, name, ga_strcmp) == NULL &&
        !qmp_command_is_enabled(cmd)) {
        g_debug("enabling command: %s", name);
        qmp_enable_command(&ga_commands, name);
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
    qmp_for_each_command(&ga_commands, ga_disable_non_whitelisted, NULL);
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
        Error *err = NULL;

        if (!qemu_write_pidfile(s->deferred_options.pid_filepath, &err)) {
            g_warning("%s", error_get_pretty(err));
            error_free(err);
        }
        s->deferred_options.pid_filepath = NULL;
    }

    /* enable all disabled, non-blacklisted commands */
    qmp_for_each_command(&ga_commands, ga_enable_non_blacklisted, s->blacklist);
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
        Error *err = NULL;

        if (!qemu_write_pidfile(pidfile, &err)) {
            g_critical("%s", error_get_pretty(err));
            error_free(err);
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

static int send_response(GAState *s, const QDict *rsp)
{
    GString *response;
    GIOStatus status;

    g_assert(s->channel);

    if (!rsp) {
        return 0;
    }

    response = qobject_to_json(QOBJECT(rsp));
    if (!response) {
        return -EINVAL;
    }

    if (s->delimit_response) {
        s->delimit_response = false;
        g_string_prepend_c(response, QGA_SENTINEL_BYTE);
    }

    g_string_append_c(response, '\n');
    status = ga_channel_write_all(s->channel, response->str, response->len);
    g_string_free(response, true);
    if (status != G_IO_STATUS_NORMAL) {
        return -EIO;
    }

    return 0;
}

/* handle requests/control events coming in over the channel */
static void process_event(void *opaque, QObject *obj, Error *err)
{
    GAState *s = opaque;
    QDict *rsp;
    int ret;

    g_debug("process_event: called");
    assert(!obj != !err);
    if (err) {
        rsp = qmp_error_response(err);
        goto end;
    }

    g_debug("processing command");
    rsp = qmp_dispatch(&ga_commands, obj, false, NULL);

end:
    ret = send_response(s, rsp);
    if (ret < 0) {
        g_warning("error sending error response: %s", strerror(-ret));
    }
    qobject_unref(rsp);
    qobject_unref(obj);
}

/* false return signals GAChannel to close the current client connection */
static gboolean channel_event_cb(GIOCondition condition, gpointer data)
{
    GAState *s = data;
    gchar buf[QGA_READ_COUNT_DEFAULT+1];
    gsize count;
    GIOStatus status = ga_channel_read(s->channel, buf, QGA_READ_COUNT_DEFAULT, &count);
    switch (status) {
    case G_IO_STATUS_ERROR:
        g_warning("error reading channel");
        stop_agent(s, false);
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

static gboolean channel_init(GAState *s, const gchar *method, const gchar *path,
                             int listen_fd)
{
    GAChannelMethod channel_method;

    if (strcmp(method, "virtio-serial") == 0) {
        s->virtio = true; /* virtio requires special handling in some cases */
        channel_method = GA_CHANNEL_VIRTIO_SERIAL;
    } else if (strcmp(method, "isa-serial") == 0) {
        channel_method = GA_CHANNEL_ISA_SERIAL;
    } else if (strcmp(method, "unix-listen") == 0) {
        channel_method = GA_CHANNEL_UNIX_LISTEN;
    } else if (strcmp(method, "vsock-listen") == 0) {
        channel_method = GA_CHANNEL_VSOCK_LISTEN;
    } else {
        g_critical("unsupported channel method/type: %s", method);
        return false;
    }

    s->channel = ga_channel_new(channel_method, path, listen_fd,
                                channel_event_cb, s);
    if (!s->channel) {
        g_critical("failed to create guest agent channel");
        return false;
    }

    return true;
}

#ifdef _WIN32
DWORD WINAPI handle_serial_device_events(DWORD type, LPVOID data)
{
    DWORD ret = NO_ERROR;
    PDEV_BROADCAST_HDR broadcast_header = (PDEV_BROADCAST_HDR)data;

    if (broadcast_header->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
        switch (type) {
            /* Device inserted */
        case DBT_DEVICEARRIVAL:
            /* Start QEMU-ga's service */
            if (!SetEvent(ga_state->wakeup_event)) {
                ret = GetLastError();
            }
            break;
            /* Device removed */
        case DBT_DEVICEQUERYREMOVE:
        case DBT_DEVICEREMOVEPENDING:
        case DBT_DEVICEREMOVECOMPLETE:
            /* Stop QEMU-ga's service */
            if (!ResetEvent(ga_state->wakeup_event)) {
                ret = GetLastError();
            }
            break;
        default:
            ret = ERROR_CALL_NOT_IMPLEMENTED;
        }
    }
    return ret;
}

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
            SetEvent(ga_state->wakeup_event);
            service->status.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(service->status_handle, &service->status);
            break;
        case SERVICE_CONTROL_DEVICEEVENT:
            handle_serial_device_events(type, data);
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
    DEV_BROADCAST_DEVICEINTERFACE notification_filter;
    ZeroMemory(&notification_filter, sizeof(notification_filter));
    notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    notification_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    notification_filter.dbcc_classguid = GUID_VIOSERIAL_PORT;

    service->device_notification_handle =
        RegisterDeviceNotification(service->status_handle,
            &notification_filter, DEVICE_NOTIFY_SERVICE_HANDLE);
    if (!service->device_notification_handle) {
        g_critical("Failed to register device notification handle!\n");
        return;
    }
    SetServiceStatus(service->status_handle, &service->status);

    run_agent(ga_state);

    UnregisterDeviceNotification(service->device_notification_handle);
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

static void ga_print_cmd(const QmpCommand *cmd, void *opaque)
{
    printf("%s\n", qmp_command_name(cmd));
}

static GList *split_list(const gchar *str, const gchar *delim)
{
    GList *list = NULL;
    int i;
    gchar **strv;

    strv = g_strsplit(str, delim, -1);
    for (i = 0; strv[i]; i++) {
        list = g_list_prepend(list, strv[i]);
    }
    g_free(strv);

    return list;
}

struct GAConfig {
    char *channel_path;
    char *method;
    char *log_filepath;
    char *pid_filepath;
#ifdef CONFIG_FSFREEZE
    char *fsfreeze_hook;
#endif
    char *state_dir;
#ifdef _WIN32
    const char *service;
#endif
    gchar *bliststr; /* blacklist may point to this string */
    GList *blacklist;
    int daemonize;
    GLogLevelFlags log_level;
    int dumpconf;
    bool retry_path;
};

static void config_load(GAConfig *config)
{
    GError *gerr = NULL;
    GKeyFile *keyfile;
    g_autofree char *conf = g_strdup(g_getenv("QGA_CONF")) ?: get_relocated_path(QGA_CONF_DEFAULT);

    /* read system config */
    keyfile = g_key_file_new();
    if (!g_key_file_load_from_file(keyfile, conf, 0, &gerr)) {
        goto end;
    }
    if (g_key_file_has_key(keyfile, "general", "daemon", NULL)) {
        config->daemonize =
            g_key_file_get_boolean(keyfile, "general", "daemon", &gerr);
    }
    if (g_key_file_has_key(keyfile, "general", "method", NULL)) {
        config->method =
            g_key_file_get_string(keyfile, "general", "method", &gerr);
    }
    if (g_key_file_has_key(keyfile, "general", "path", NULL)) {
        config->channel_path =
            g_key_file_get_string(keyfile, "general", "path", &gerr);
    }
    if (g_key_file_has_key(keyfile, "general", "logfile", NULL)) {
        config->log_filepath =
            g_key_file_get_string(keyfile, "general", "logfile", &gerr);
    }
    if (g_key_file_has_key(keyfile, "general", "pidfile", NULL)) {
        config->pid_filepath =
            g_key_file_get_string(keyfile, "general", "pidfile", &gerr);
    }
#ifdef CONFIG_FSFREEZE
    if (g_key_file_has_key(keyfile, "general", "fsfreeze-hook", NULL)) {
        config->fsfreeze_hook =
            g_key_file_get_string(keyfile,
                                  "general", "fsfreeze-hook", &gerr);
    }
#endif
    if (g_key_file_has_key(keyfile, "general", "statedir", NULL)) {
        config->state_dir =
            g_key_file_get_string(keyfile, "general", "statedir", &gerr);
    }
    if (g_key_file_has_key(keyfile, "general", "verbose", NULL) &&
        g_key_file_get_boolean(keyfile, "general", "verbose", &gerr)) {
        /* enable all log levels */
        config->log_level = G_LOG_LEVEL_MASK;
    }
    if (g_key_file_has_key(keyfile, "general", "retry-path", NULL)) {
        config->retry_path =
            g_key_file_get_boolean(keyfile, "general", "retry-path", &gerr);
    }
    if (g_key_file_has_key(keyfile, "general", "blacklist", NULL)) {
        config->bliststr =
            g_key_file_get_string(keyfile, "general", "blacklist", &gerr);
        config->blacklist = g_list_concat(config->blacklist,
                                          split_list(config->bliststr, ","));
    }

end:
    g_key_file_free(keyfile);
    if (gerr &&
        !(gerr->domain == G_FILE_ERROR && gerr->code == G_FILE_ERROR_NOENT)) {
        g_critical("error loading configuration from path: %s, %s",
                   conf, gerr->message);
        exit(EXIT_FAILURE);
    }
    g_clear_error(&gerr);
}

static gchar *list_join(GList *list, const gchar separator)
{
    GString *str = g_string_new("");

    while (list) {
        str = g_string_append(str, (gchar *)list->data);
        list = g_list_next(list);
        if (list) {
            str = g_string_append_c(str, separator);
        }
    }

    return g_string_free(str, FALSE);
}

static void config_dump(GAConfig *config)
{
    GError *error = NULL;
    GKeyFile *keyfile;
    gchar *tmp;

    keyfile = g_key_file_new();
    g_assert(keyfile);

    g_key_file_set_boolean(keyfile, "general", "daemon", config->daemonize);
    g_key_file_set_string(keyfile, "general", "method", config->method);
    if (config->channel_path) {
        g_key_file_set_string(keyfile, "general", "path", config->channel_path);
    }
    if (config->log_filepath) {
        g_key_file_set_string(keyfile, "general", "logfile",
                              config->log_filepath);
    }
    g_key_file_set_string(keyfile, "general", "pidfile", config->pid_filepath);
#ifdef CONFIG_FSFREEZE
    if (config->fsfreeze_hook) {
        g_key_file_set_string(keyfile, "general", "fsfreeze-hook",
                              config->fsfreeze_hook);
    }
#endif
    g_key_file_set_string(keyfile, "general", "statedir", config->state_dir);
    g_key_file_set_boolean(keyfile, "general", "verbose",
                           config->log_level == G_LOG_LEVEL_MASK);
    g_key_file_set_boolean(keyfile, "general", "retry-path",
                           config->retry_path);
    tmp = list_join(config->blacklist, ',');
    g_key_file_set_string(keyfile, "general", "blacklist", tmp);
    g_free(tmp);

    tmp = g_key_file_to_data(keyfile, NULL, &error);
    if (error) {
        g_critical("Failed to dump keyfile: %s", error->message);
        g_clear_error(&error);
    } else {
        printf("%s", tmp);
    }

    g_free(tmp);
    g_key_file_free(keyfile);
}

static void config_parse(GAConfig *config, int argc, char **argv)
{
    const char *sopt = "hVvdm:p:l:f:F::b:s:t:Dr";
    int opt_ind = 0, ch;
    const struct option lopt[] = {
        { "help", 0, NULL, 'h' },
        { "version", 0, NULL, 'V' },
        { "dump-conf", 0, NULL, 'D' },
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
        { "retry-path", 0, NULL, 'r' },
        { NULL, 0, NULL, 0 }
    };

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 'm':
            g_free(config->method);
            config->method = g_strdup(optarg);
            break;
        case 'p':
            g_free(config->channel_path);
            config->channel_path = g_strdup(optarg);
            break;
        case 'l':
            g_free(config->log_filepath);
            config->log_filepath = g_strdup(optarg);
            break;
        case 'f':
            g_free(config->pid_filepath);
            config->pid_filepath = g_strdup(optarg);
            break;
#ifdef CONFIG_FSFREEZE
        case 'F':
            g_free(config->fsfreeze_hook);
            config->fsfreeze_hook = optarg ? g_strdup(optarg) : get_relocated_path(QGA_FSFREEZE_HOOK_DEFAULT);
            break;
#endif
        case 't':
            g_free(config->state_dir);
            config->state_dir = g_strdup(optarg);
            break;
        case 'v':
            /* enable all log levels */
            config->log_level = G_LOG_LEVEL_MASK;
            break;
        case 'V':
            printf("QEMU Guest Agent %s\n", QEMU_VERSION);
            exit(EXIT_SUCCESS);
        case 'd':
            config->daemonize = 1;
            break;
        case 'D':
            config->dumpconf = 1;
            break;
        case 'r':
            config->retry_path = true;
            break;
        case 'b': {
            if (is_help_option(optarg)) {
                qmp_for_each_command(&ga_commands, ga_print_cmd, NULL);
                exit(EXIT_SUCCESS);
            }
            config->blacklist = g_list_concat(config->blacklist,
                                             split_list(optarg, ","));
            break;
        }
#ifdef _WIN32
        case 's':
            config->service = optarg;
            if (strcmp(config->service, "install") == 0) {
                if (ga_install_vss_provider()) {
                    exit(EXIT_FAILURE);
                }
                if (ga_install_service(config->channel_path,
                                       config->log_filepath, config->state_dir)) {
                    exit(EXIT_FAILURE);
                }
                exit(EXIT_SUCCESS);
            } else if (strcmp(config->service, "uninstall") == 0) {
                ga_uninstall_vss_provider();
                exit(ga_uninstall_service());
            } else if (strcmp(config->service, "vss-install") == 0) {
                if (ga_install_vss_provider()) {
                    exit(EXIT_FAILURE);
                }
                exit(EXIT_SUCCESS);
            } else if (strcmp(config->service, "vss-uninstall") == 0) {
                ga_uninstall_vss_provider();
                exit(EXIT_SUCCESS);
            } else {
                printf("Unknown service command.\n");
                exit(EXIT_FAILURE);
            }
            break;
#endif
        case 'h':
            usage(argv[0]);
            exit(EXIT_SUCCESS);
        case '?':
            g_print("Unknown option, try '%s --help' for more information.\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }
}

static void config_free(GAConfig *config)
{
    g_free(config->method);
    g_free(config->log_filepath);
    g_free(config->pid_filepath);
    g_free(config->state_dir);
    g_free(config->channel_path);
    g_free(config->bliststr);
#ifdef CONFIG_FSFREEZE
    g_free(config->fsfreeze_hook);
#endif
    g_list_free_full(config->blacklist, g_free);
    g_free(config);
}

static bool check_is_frozen(GAState *s)
{
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
        return true;
    }
#endif
    return false;
}

static GAState *initialize_agent(GAConfig *config, int socket_activation)
{
    GAState *s = g_new0(GAState, 1);

    g_assert(ga_state == NULL);

    s->log_level = config->log_level;
    s->log_file = stderr;
#ifdef CONFIG_FSFREEZE
    s->fsfreeze_hook = config->fsfreeze_hook;
#endif
    s->pstate_filepath = g_strdup_printf("%s/qga.state", config->state_dir);
    s->state_filepath_isfrozen = g_strdup_printf("%s/qga.state.isfrozen",
                                                 config->state_dir);
    s->frozen = check_is_frozen(s);

    g_log_set_default_handler(ga_log, s);
    g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR);
    ga_enable_logging(s);

#ifdef _WIN32
    /* On win32 the state directory is application specific (be it the default
     * or a user override). We got past the command line parsing; let's create
     * the directory (with any intermediate directories). If we run into an
     * error later on, we won't try to clean up the directory, it is considered
     * persistent.
     */
    if (g_mkdir_with_parents(config->state_dir, S_IRWXU) == -1) {
        g_critical("unable to create (an ancestor of) the state directory"
                   " '%s': %s", config->state_dir, strerror(errno));
        return NULL;
    }
#endif

    if (ga_is_frozen(s)) {
        if (config->daemonize) {
            /* delay opening/locking of pidfile till filesystems are unfrozen */
            s->deferred_options.pid_filepath = config->pid_filepath;
            become_daemon(NULL);
        }
        if (config->log_filepath) {
            /* delay opening the log file till filesystems are unfrozen */
            s->deferred_options.log_filepath = config->log_filepath;
        }
        ga_disable_logging(s);
        qmp_for_each_command(&ga_commands, ga_disable_non_whitelisted, NULL);
    } else {
        if (config->daemonize) {
            become_daemon(config->pid_filepath);
        }
        if (config->log_filepath) {
            FILE *log_file = ga_open_logfile(config->log_filepath);
            if (!log_file) {
                g_critical("unable to open specified log file: %s",
                           strerror(errno));
                return NULL;
            }
            s->log_file = log_file;
        }
    }

    /* load persistent state from disk */
    if (!read_persistent_state(&s->pstate,
                               s->pstate_filepath,
                               ga_is_frozen(s))) {
        g_critical("failed to load persistent state");
        return NULL;
    }

    config->blacklist = ga_command_blacklist_init(config->blacklist);
    if (config->blacklist) {
        GList *l = config->blacklist;
        s->blacklist = config->blacklist;
        do {
            g_debug("disabling command: %s", (char *)l->data);
            qmp_disable_command(&ga_commands, l->data);
            l = g_list_next(l);
        } while (l);
    }
    s->command_state = ga_command_state_new();
    ga_command_state_init(s, s->command_state);
    ga_command_state_init_all(s->command_state);
    json_message_parser_init(&s->parser, process_event, s, NULL);

#ifndef _WIN32
    if (!register_signal_handlers()) {
        g_critical("failed to register signal handlers");
        return NULL;
    }
#endif

    s->main_loop = g_main_loop_new(NULL, false);

    s->config = config;
    s->socket_activation = socket_activation;

#ifdef _WIN32
    s->wakeup_event = CreateEvent(NULL, TRUE, FALSE, TEXT("WakeUp"));
    if (s->wakeup_event == NULL) {
        g_critical("CreateEvent failed");
        return NULL;
    }
#endif

    ga_state = s;
    return s;
}

static void cleanup_agent(GAState *s)
{
#ifdef _WIN32
    CloseHandle(s->wakeup_event);
#endif
    if (s->command_state) {
        ga_command_state_cleanup_all(s->command_state);
        ga_command_state_free(s->command_state);
        json_message_parser_destroy(&s->parser);
    }
    g_free(s->pstate_filepath);
    g_free(s->state_filepath_isfrozen);
    if (s->main_loop) {
        g_main_loop_unref(s->main_loop);
    }
    g_free(s);
    ga_state = NULL;
}

static int run_agent_once(GAState *s)
{
    if (!channel_init(s, s->config->method, s->config->channel_path,
                      s->socket_activation ? FIRST_SOCKET_ACTIVATION_FD : -1)) {
        g_critical("failed to initialize guest agent channel");
        return EXIT_FAILURE;
    }

    g_main_loop_run(ga_state->main_loop);

    if (s->channel) {
        ga_channel_free(s->channel);
    }

    return EXIT_SUCCESS;
}

static void wait_for_channel_availability(GAState *s)
{
    g_warning("waiting for channel path...");
#ifndef _WIN32
    sleep(QGA_RETRY_INTERVAL);
#else
    DWORD dwWaitResult;

    dwWaitResult = WaitForSingleObject(s->wakeup_event, INFINITE);

    switch (dwWaitResult) {
    case WAIT_OBJECT_0:
        break;
    case WAIT_TIMEOUT:
        break;
    default:
        g_critical("WaitForSingleObject failed");
    }
#endif
}

static int run_agent(GAState *s)
{
    int ret = EXIT_SUCCESS;

    s->force_exit = false;

    do {
        ret = run_agent_once(s);
        if (s->config->retry_path && !s->force_exit) {
            g_warning("agent stopped unexpectedly, restarting...");
            wait_for_channel_availability(s);
        }
    } while (s->config->retry_path && !s->force_exit);

    return ret;
}

static void stop_agent(GAState *s, bool requested)
{
    if (!s->force_exit) {
        s->force_exit = requested;
    }

    if (g_main_loop_is_running(s->main_loop)) {
        g_main_loop_quit(s->main_loop);
    }
}

int main(int argc, char **argv)
{
    int ret = EXIT_SUCCESS;
    GAState *s;
    GAConfig *config = g_new0(GAConfig, 1);
    int socket_activation;

    config->log_level = G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL;

    qemu_init_exec_dir(argv[0]);
    qga_qmp_init_marshal(&ga_commands);

    init_dfl_pathnames();
    config_load(config);
    config_parse(config, argc, argv);

    if (config->pid_filepath == NULL) {
        config->pid_filepath = g_strdup(dfl_pathnames.pidfile);
    }

    if (config->state_dir == NULL) {
        config->state_dir = g_strdup(dfl_pathnames.state_dir);
    }

    if (config->method == NULL) {
        config->method = g_strdup("virtio-serial");
    }

    socket_activation = check_socket_activation();
    if (socket_activation > 1) {
        g_critical("qemu-ga only supports listening on one socket");
        ret = EXIT_FAILURE;
        goto end;
    }
    if (socket_activation) {
        SocketAddress *addr;

        g_free(config->method);
        g_free(config->channel_path);
        config->method = NULL;
        config->channel_path = NULL;

        addr = socket_local_address(FIRST_SOCKET_ACTIVATION_FD, NULL);
        if (addr) {
            if (addr->type == SOCKET_ADDRESS_TYPE_UNIX) {
                config->method = g_strdup("unix-listen");
            } else if (addr->type == SOCKET_ADDRESS_TYPE_VSOCK) {
                config->method = g_strdup("vsock-listen");
            }

            qapi_free_SocketAddress(addr);
        }

        if (!config->method) {
            g_critical("unsupported listen fd type");
            ret = EXIT_FAILURE;
            goto end;
        }
    } else if (config->channel_path == NULL) {
        if (strcmp(config->method, "virtio-serial") == 0) {
            /* try the default path for the virtio-serial port */
            config->channel_path = g_strdup(QGA_VIRTIO_PATH_DEFAULT);
        } else if (strcmp(config->method, "isa-serial") == 0) {
            /* try the default path for the serial port - COM1 */
            config->channel_path = g_strdup(QGA_SERIAL_PATH_DEFAULT);
        } else {
            g_critical("must specify a path for this channel");
            ret = EXIT_FAILURE;
            goto end;
        }
    }

    if (config->dumpconf) {
        config_dump(config);
        goto end;
    }

    s = initialize_agent(config, socket_activation);
    if (!s) {
        g_critical("error initializing guest agent");
        goto end;
    }

#ifdef _WIN32
    if (config->daemonize) {
        SERVICE_TABLE_ENTRY service_table[] = {
            { (char *)QGA_SERVICE_NAME, service_main }, { NULL, NULL } };
        StartServiceCtrlDispatcher(service_table);
    } else {
        ret = run_agent(s);
    }
#else
    ret = run_agent(s);
#endif

    cleanup_agent(s);

end:
    if (config->daemonize) {
        unlink(config->pid_filepath);
    }

    config_free(config);

    return ret;
}
