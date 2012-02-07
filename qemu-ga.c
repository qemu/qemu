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
#ifndef _WIN32
#include <syslog.h>
#include <sys/wait.h>
#endif
#include "json-streamer.h"
#include "json-parser.h"
#include "qint.h"
#include "qjson.h"
#include "qga/guest-agent-core.h"
#include "module.h"
#include "signal.h"
#include "qerror.h"
#include "error_int.h"
#include "qapi/qmp-core.h"
#include "qga/channel.h"
#ifdef _WIN32
#include "qga/service-win32.h"
#include <windows.h>
#endif

#ifndef _WIN32
#define QGA_VIRTIO_PATH_DEFAULT "/dev/virtio-ports/org.qemu.guest_agent.0"
#else
#define QGA_VIRTIO_PATH_DEFAULT "\\\\.\\Global\\org.qemu.guest_agent.0"
#endif
#define QGA_PIDFILE_DEFAULT "/var/run/qemu-ga.pid"
#define QGA_SENTINEL_BYTE 0xFF

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
};

struct GAState *ga_state;

#ifdef _WIN32
DWORD WINAPI service_ctrl_handler(DWORD ctrl, DWORD type, LPVOID data,
                                  LPVOID ctx);
VOID WINAPI service_main(DWORD argc, TCHAR *argv[]);
#endif

static void quit_handler(int sig)
{
    g_debug("received signal num %d, quitting", sig);

    if (g_main_loop_is_running(ga_state->main_loop)) {
        g_main_loop_quit(ga_state->main_loop);
    }
}

#ifndef _WIN32
/* reap _all_ terminated children */
static void child_handler(int sig)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) /* NOTHING */;
}

static gboolean register_signal_handlers(void)
{
    struct sigaction sigact, sigact_chld;
    int ret;

    memset(&sigact, 0, sizeof(struct sigaction));
    sigact.sa_handler = quit_handler;

    ret = sigaction(SIGINT, &sigact, NULL);
    if (ret == -1) {
        g_error("error configuring signal handler: %s", strerror(errno));
        return false;
    }
    ret = sigaction(SIGTERM, &sigact, NULL);
    if (ret == -1) {
        g_error("error configuring signal handler: %s", strerror(errno));
        return false;
    }

    memset(&sigact_chld, 0, sizeof(struct sigaction));
    sigact_chld.sa_handler = child_handler;
    sigact_chld.sa_flags = SA_NOCLDSTOP;
    ret = sigaction(SIGCHLD, &sigact_chld, NULL);
    if (ret == -1) {
        g_error("error configuring signal handler: %s", strerror(errno));
    }

    return true;
}
#endif

static void usage(const char *cmd)
{
    printf(
"Usage: %s -c <channel_opts>\n"
"QEMU Guest Agent %s\n"
"\n"
"  -m, --method      transport method: one of unix-listen, virtio-serial, or\n"
"                    isa-serial (virtio-serial is the default)\n"
"  -p, --path        device/socket path (%s is the default for virtio-serial)\n"
"  -l, --logfile     set logfile path, logs to stderr by default\n"
"  -f, --pidfile     specify pidfile (default is %s)\n"
"  -v, --verbose     log extra debugging information\n"
"  -V, --version     print version information and exit\n"
"  -d, --daemonize   become a daemon\n"
#ifdef _WIN32
"  -s, --service     service commands: install, uninstall\n"
#endif
"  -b, --blacklist   comma-separated list of RPCs to disable (no spaces, \"?\""
"                    to list available RPCs)\n"
"  -h, --help        display this help and exit\n"
"\n"
"Report bugs to <mdroth@linux.vnet.ibm.com>\n"
    , cmd, QGA_VERSION, QGA_VIRTIO_PATH_DEFAULT, QGA_PIDFILE_DEFAULT);
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

#ifndef _WIN32
static void become_daemon(const char *pidfile)
{
    pid_t pid, sid;
    int pidfd;
    char *pidstr = NULL;

    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    pidfd = open(pidfile, O_CREAT|O_WRONLY|O_EXCL, S_IRUSR|S_IWUSR);
    if (pidfd == -1) {
        g_critical("Cannot create pid file, %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (asprintf(&pidstr, "%d", getpid()) == -1) {
        g_critical("Cannot allocate memory");
        goto fail;
    }
    if (write(pidfd, pidstr, strlen(pidstr)) != strlen(pidstr)) {
        free(pidstr);
        g_critical("Failed to write pid file");
        goto fail;
    }

    umask(0);
    sid = setsid();
    if (sid < 0) {
        goto fail;
    }
    if ((chdir("/")) < 0) {
        goto fail;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    free(pidstr);
    return;

fail:
    unlink(pidfile);
    g_critical("failed to daemonize");
    exit(EXIT_FAILURE);
}
#endif

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
    } else {
        g_warning("error getting response");
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
        qdict_put_obj(qdict, "error", error_get_qobject(err));
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
            qdict_put_obj(qdict, "error", error_get_qobject(err));
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
        if (strcmp(method, "virtio-serial") != 0) {
            g_critical("must specify a path for this channel");
            return false;
        }
        /* try the default path for the virtio-serial port */
        path = QGA_VIRTIO_PATH_DEFAULT;
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

int main(int argc, char **argv)
{
    const char *sopt = "hVvdm:p:l:f:b:s:";
    const char *method = NULL, *path = NULL, *pidfile = QGA_PIDFILE_DEFAULT;
    const char *log_file_name = NULL;
#ifdef _WIN32
    const char *service = NULL;
#endif
    const struct option lopt[] = {
        { "help", 0, NULL, 'h' },
        { "version", 0, NULL, 'V' },
        { "logfile", 1, NULL, 'l' },
        { "pidfile", 1, NULL, 'f' },
        { "verbose", 0, NULL, 'v' },
        { "method", 1, NULL, 'm' },
        { "path", 1, NULL, 'p' },
        { "daemonize", 0, NULL, 'd' },
        { "blacklist", 1, NULL, 'b' },
#ifdef _WIN32
        { "service", 1, NULL, 's' },
#endif        
        { NULL, 0, NULL, 0 }
    };
    int opt_ind = 0, ch, daemonize = 0, i, j, len;
    GLogLevelFlags log_level = G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL;
    FILE *log_file = stderr;
    GAState *s;

    module_call_init(MODULE_INIT_QAPI);

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 'm':
            method = optarg;
            break;
        case 'p':
            path = optarg;
            break;
        case 'l':
            log_file_name = optarg;
            log_file = fopen(log_file_name, "a");
            if (!log_file) {
                g_critical("unable to open specified log file: %s",
                           strerror(errno));
                return EXIT_FAILURE;
            }
            break;
        case 'f':
            pidfile = optarg;
            break;
        case 'v':
            /* enable all log levels */
            log_level = G_LOG_LEVEL_MASK;
            break;
        case 'V':
            printf("QEMU Guest Agent %s\n", QGA_VERSION);
            return 0;
        case 'd':
            daemonize = 1;
            break;
        case 'b': {
            char **list_head, **list;
            if (*optarg == '?') {
                list_head = list = qmp_get_command_list();
                while (*list != NULL) {
                    printf("%s\n", *list);
                    g_free(*list);
                    list++;
                }
                g_free(list_head);
                return 0;
            }
            for (j = 0, i = 0, len = strlen(optarg); i < len; i++) {
                if (optarg[i] == ',') {
                    optarg[i] = 0;
                    qmp_disable_command(&optarg[j]);
                    g_debug("disabling command: %s", &optarg[j]);
                    j = i + 1;
                }
            }
            if (j < i) {
                qmp_disable_command(&optarg[j]);
                g_debug("disabling command: %s", &optarg[j]);
            }
            break;
        }
#ifdef _WIN32
        case 's':
            service = optarg;
            if (strcmp(service, "install") == 0) {
                return ga_install_service(path, log_file_name);
            } else if (strcmp(service, "uninstall") == 0) {
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

#ifndef _WIN32
    if (daemonize) {
        g_debug("starting daemon");
        become_daemon(pidfile);
    }
#endif

    s = g_malloc0(sizeof(GAState));
    s->log_file = log_file;
    s->log_level = log_level;
    g_log_set_default_handler(ga_log, s);
    g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR);
    s->logging_enabled = true;
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
        unlink(pidfile);
    }
    return 0;

out_bad:
    if (daemonize) {
        unlink(pidfile);
    }
    return EXIT_FAILURE;
}
