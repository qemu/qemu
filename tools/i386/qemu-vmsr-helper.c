/*
 * Privileged RAPL MSR helper commands for QEMU
 *
 * Copyright (C) 2024 Red Hat, Inc. <aharivel@redhat.com>
 *
 * Author: Anthony Harivel <aharivel@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <getopt.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#ifdef CONFIG_LIBCAP_NG
#include <cap-ng.h>
#endif
#include <pwd.h>
#include <grp.h>

#include "qemu/help-texts.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu-version.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/systemd.h"
#include "io/channel.h"
#include "io/channel-socket.h"
#include "trace/control.h"
#include "qemu-version.h"
#include "rapl-msr-index.h"

#define MSR_PATH_TEMPLATE "/dev/cpu/%u/msr"

static char *socket_path;
static char *pidfile;
static enum { RUNNING, TERMINATE, TERMINATING } state;
static QIOChannelSocket *server_ioc;
static int server_watch;
static int num_active_sockets = 1;
static bool verbose;

#ifdef CONFIG_LIBCAP_NG
static int uid = -1;
static int gid = -1;
#endif

static void compute_default_paths(void)
{
    g_autofree char *state = qemu_get_local_state_dir();

    socket_path = g_build_filename(state, "run", "qemu-vmsr-helper.sock", NULL);
    pidfile = g_build_filename(state, "run", "qemu-vmsr-helper.pid", NULL);
}

static int is_intel_processor(void)
{
    int ebx, ecx, edx;

    /* Execute CPUID instruction with eax=0 (basic identification) */
    asm volatile (
        "cpuid"
        : "=b" (ebx), "=c" (ecx), "=d" (edx)
        : "a" (0)
    );

    /*
     *  Check if processor is "GenuineIntel"
     *  0x756e6547 = "Genu"
     *  0x49656e69 = "ineI"
     *  0x6c65746e = "ntel"
     */
    return (ebx == 0x756e6547) && (edx == 0x49656e69) && (ecx == 0x6c65746e);
}

static int is_rapl_enabled(void)
{
    const char *path = "/sys/class/powercap/intel-rapl/enabled";
    FILE *file = fopen(path, "r");
    int value = 0;

    if (file != NULL) {
        if (fscanf(file, "%d", &value) != 1) {
            error_report("INTEL RAPL not enabled");
        }
        fclose(file);
    } else {
        error_report("Error opening %s", path);
    }

    return value;
}

/*
 * Check if the TID that request the MSR read
 * belongs to the peer. It be should a TID of a vCPU.
 */
static bool is_tid_present(pid_t pid, pid_t tid)
{
    g_autofree char *tidPath = g_strdup_printf("/proc/%d/task/%d", pid, tid);

    /* Check if the TID directory exists within the PID directory */
    if (access(tidPath, F_OK) == 0) {
        return true;
    }

    error_report("Failed to open /proc at %s", tidPath);
    return false;
}

/*
 * Only the RAPL MSR in target/i386/cpu.h are allowed
 */
static bool is_msr_allowed(uint32_t reg)
{
    switch (reg) {
    case MSR_RAPL_POWER_UNIT:
    case MSR_PKG_POWER_LIMIT:
    case MSR_PKG_ENERGY_STATUS:
    case MSR_PKG_POWER_INFO:
        return true;
    default:
        return false;
    }
}

static uint64_t vmsr_read_msr(uint32_t msr_register, unsigned int cpu_id)
{
    int fd;
    uint64_t result = 0;

    g_autofree char *path = g_strdup_printf(MSR_PATH_TEMPLATE, cpu_id);

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        error_report("Failed to open MSR file at %s", path);
        return result;
    }

    if (pread(fd, &result, sizeof(result), msr_register) != sizeof(result)) {
        error_report("Failed to read MSR");
        result = 0;
    }

    close(fd);
    return result;
}

static void usage(const char *name)
{
    (printf) (
"Usage: %s [OPTIONS] FILE\n"
"Virtual RAPL MSR helper program for QEMU\n"
"\n"
"  -h, --help                display this help and exit\n"
"  -V, --version             output version information and exit\n"
"\n"
"  -d, --daemon              run in the background\n"
"  -f, --pidfile=PATH        PID file when running as a daemon\n"
"                            (default '%s')\n"
"  -k, --socket=PATH         path to the unix socket\n"
"                            (default '%s')\n"
"  -T, --trace [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
"                            specify tracing options\n"
#ifdef CONFIG_LIBCAP_NG
"  -u, --user=USER           user to drop privileges to\n"
"  -g, --group=GROUP         group to drop privileges to\n"
#endif
"\n"
QEMU_HELP_BOTTOM "\n"
    , name, pidfile, socket_path);
}

static void version(const char *name)
{
    printf(
"%s " QEMU_FULL_VERSION "\n"
"Written by Anthony Harivel.\n"
"\n"
QEMU_COPYRIGHT "\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
    , name);
}

typedef struct VMSRHelperClient {
    QIOChannelSocket *ioc;
    Coroutine *co;
} VMSRHelperClient;

static void coroutine_fn vh_co_entry(void *opaque)
{
    VMSRHelperClient *client = opaque;
    Error *local_err = NULL;
    unsigned int peer_pid;
    uint32_t request[3];
    uint64_t vmsr;
    int r;

    if (!qio_channel_set_blocking(QIO_CHANNEL(client->ioc),
                                  false, &local_err)) {
        goto out;
    }

    qio_channel_set_follow_coroutine_ctx(QIO_CHANNEL(client->ioc), true);

    /*
     * Check peer credentials
     */
    r = qio_channel_get_peerpid(QIO_CHANNEL(client->ioc),
                                &peer_pid,
                                &local_err);
    if (r < 0) {
        goto out;
    }

    for (;;) {
        /*
         * Read the requested MSR
         * Only RAPL MSR in rapl-msr-index.h is allowed
         */
        r = qio_channel_read_all_eof(QIO_CHANNEL(client->ioc),
                                     (char *) &request, sizeof(request), &local_err);
        if (r <= 0) {
            break;
        }

        if (!is_msr_allowed(request[0])) {
            error_report("Requested unallowed msr: %d", request[0]);
            break;
        }

        vmsr = vmsr_read_msr(request[0], request[1]);

        if (!is_tid_present(peer_pid, request[2])) {
            error_report("Requested TID not in peer PID: %d %d",
                peer_pid, request[2]);
            vmsr = 0;
        }

        r = qio_channel_write_all(QIO_CHANNEL(client->ioc),
                                  (char *) &vmsr,
                                  sizeof(vmsr),
                                  &local_err);
        if (r < 0) {
            break;
        }
    }

out:
    if (local_err) {
        if (!verbose) {
            error_free(local_err);
        } else {
            error_report_err(local_err);
        }
    }

    object_unref(OBJECT(client->ioc));
    g_free(client);
}

static gboolean accept_client(QIOChannel *ioc,
                              GIOCondition cond,
                              gpointer opaque)
{
    QIOChannelSocket *cioc;
    VMSRHelperClient *vmsrh;

    cioc = qio_channel_socket_accept(QIO_CHANNEL_SOCKET(ioc),
                                     NULL);
    if (!cioc) {
        return TRUE;
    }

    vmsrh = g_new(VMSRHelperClient, 1);
    vmsrh->ioc = cioc;
    vmsrh->co = qemu_coroutine_create(vh_co_entry, vmsrh);
    qemu_coroutine_enter(vmsrh->co);

    return TRUE;
}

static void termsig_handler(int signum)
{
    qatomic_cmpxchg(&state, RUNNING, TERMINATE);
    qemu_notify_event();
}

static void close_server_socket(void)
{
    assert(server_ioc);

    g_source_remove(server_watch);
    server_watch = -1;
    object_unref(OBJECT(server_ioc));
    num_active_sockets--;
}

#ifdef CONFIG_LIBCAP_NG
static int drop_privileges(void)
{
    /* clear all capabilities */
    capng_clear(CAPNG_SELECT_BOTH);

    if (capng_update(CAPNG_ADD, CAPNG_EFFECTIVE | CAPNG_PERMITTED,
                     CAP_SYS_RAWIO) < 0) {
        return -1;
    }

    return 0;
}
#endif

int main(int argc, char **argv)
{
    const char *sopt = "hVk:f:dT:u:g:vq";
    struct option lopt[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { "socket", required_argument, NULL, 'k' },
        { "pidfile", required_argument, NULL, 'f' },
        { "daemon", no_argument, NULL, 'd' },
        { "trace", required_argument, NULL, 'T' },
        { "verbose", no_argument, NULL, 'v' },
        { NULL, 0, NULL, 0 }
    };
    int opt_ind = 0;
    int ch;
    Error *local_err = NULL;
    bool daemonize = false;
    bool pidfile_specified = false;
    bool socket_path_specified = false;
    unsigned socket_activation;

    struct sigaction sa_sigterm;
    memset(&sa_sigterm, 0, sizeof(sa_sigterm));
    sa_sigterm.sa_handler = termsig_handler;
    sigaction(SIGTERM, &sa_sigterm, NULL);
    sigaction(SIGINT, &sa_sigterm, NULL);
    sigaction(SIGHUP, &sa_sigterm, NULL);

    signal(SIGPIPE, SIG_IGN);

    error_init(argv[0]);
    module_call_init(MODULE_INIT_TRACE);
    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_trace_opts);
    qemu_init_exec_dir(argv[0]);

    compute_default_paths();

    /*
     * Sanity check
     * 1. cpu must be Intel cpu
     * 2. RAPL must be enabled
     */
    if (!is_intel_processor()) {
        error_report("error: CPU is not INTEL cpu");
        exit(EXIT_FAILURE);
    }

    if (!is_rapl_enabled()) {
        error_report("error: RAPL driver not enable");
        exit(EXIT_FAILURE);
    }

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 'k':
            g_free(socket_path);
            socket_path = g_strdup(optarg);
            socket_path_specified = true;
            if (socket_path[0] != '/') {
                error_report("socket path must be absolute");
                exit(EXIT_FAILURE);
            }
            break;
        case 'f':
            g_free(pidfile);
            pidfile = g_strdup(optarg);
            pidfile_specified = true;
            break;
#ifdef CONFIG_LIBCAP_NG
        case 'u': {
            unsigned long res;
            struct passwd *userinfo = getpwnam(optarg);
            if (userinfo) {
                uid = userinfo->pw_uid;
            } else if (qemu_strtoul(optarg, NULL, 10, &res) == 0 &&
                       (uid_t)res == res) {
                uid = res;
            } else {
                error_report("invalid user '%s'", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        }
        case 'g': {
            unsigned long res;
            struct group *groupinfo = getgrnam(optarg);
            if (groupinfo) {
                gid = groupinfo->gr_gid;
            } else if (qemu_strtoul(optarg, NULL, 10, &res) == 0 &&
                       (gid_t)res == res) {
                gid = res;
            } else {
                error_report("invalid group '%s'", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        }
#else
        case 'u':
        case 'g':
            error_report("-%c not supported by this %s", ch, argv[0]);
            exit(1);
#endif
        case 'd':
            daemonize = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 'T':
            trace_opt_parse(optarg);
            break;
        case 'V':
            version(argv[0]);
            exit(EXIT_SUCCESS);
            break;
        case 'h':
            usage(argv[0]);
            exit(EXIT_SUCCESS);
            break;
        case '?':
            error_report("Try `%s --help' for more information.", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (!trace_init_backends()) {
        exit(EXIT_FAILURE);
    }
    trace_init_file();
    qemu_set_log(LOG_TRACE, &error_fatal);

    socket_activation = check_socket_activation();
    if (socket_activation == 0) {
        SocketAddress saddr;
        saddr = (SocketAddress){
            .type = SOCKET_ADDRESS_TYPE_UNIX,
            .u.q_unix.path = socket_path,
        };
        server_ioc = qio_channel_socket_new();
        if (qio_channel_socket_listen_sync(server_ioc, &saddr,
                                           1, &local_err) < 0) {
            object_unref(OBJECT(server_ioc));
            error_report_err(local_err);
            return 1;
        }
    } else {
        /* Using socket activation - check user didn't use -p etc. */
        if (socket_path_specified) {
            error_report("Unix socket can't be set when"
                         "using socket activation");
            exit(EXIT_FAILURE);
        }

        /* Can only listen on a single socket.  */
        if (socket_activation > 1) {
            error_report("%s does not support socket activation"
                         "with LISTEN_FDS > 1",
                        argv[0]);
            exit(EXIT_FAILURE);
        }
        server_ioc = qio_channel_socket_new_fd(FIRST_SOCKET_ACTIVATION_FD,
                                               &local_err);
        if (server_ioc == NULL) {
            error_reportf_err(local_err,
                              "Failed to use socket activation: ");
            exit(EXIT_FAILURE);
        }
    }

    qemu_init_main_loop(&error_fatal);

    server_watch = qio_channel_add_watch(QIO_CHANNEL(server_ioc),
                                         G_IO_IN,
                                         accept_client,
                                         NULL, NULL);

    if (daemonize) {
        if (daemon(0, 0) < 0) {
            error_report("Failed to daemonize: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (daemonize || pidfile_specified) {
        qemu_write_pidfile(pidfile, &error_fatal);
    }

#ifdef CONFIG_LIBCAP_NG
    if (drop_privileges() < 0) {
        error_report("Failed to drop privileges: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
#endif

    info_report("Listening on %s", socket_path);

    state = RUNNING;
    do {
        main_loop_wait(false);
        if (state == TERMINATE) {
            state = TERMINATING;
            close_server_socket();
        }
    } while (num_active_sockets > 0);

    exit(EXIT_SUCCESS);
}
