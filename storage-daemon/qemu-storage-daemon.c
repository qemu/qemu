/*
 * QEMU storage daemon
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2019 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include <getopt.h>

#include "block/block.h"
#include "block/nbd.h"
#include "chardev/char.h"
#include "crypto/init.h"
#include "monitor/monitor.h"
#include "monitor/monitor-internal.h"

#include "qapi/error.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qapi-visit-block-export.h"
#include "qapi/qapi-visit-control.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qobject-input-visitor.h"

#include "qemu-common.h"
#include "qemu-version.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/help_option.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qom/object_interfaces.h"

#include "storage-daemon/qapi/qapi-commands.h"
#include "storage-daemon/qapi/qapi-init-commands.h"

#include "sysemu/runstate.h"
#include "trace/control.h"

static const char *pid_file;
static volatile bool exit_requested = false;

void qemu_system_killed(int signal, pid_t pid)
{
    exit_requested = true;
}

void qmp_quit(Error **errp)
{
    exit_requested = true;
}

static void help(void)
{
    printf(
"Usage: %s [options]\n"
"QEMU storage daemon\n"
"\n"
"  -h, --help             display this help and exit\n"
"  -T, --trace [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
"                         specify tracing options\n"
"  -V, --version          output version information and exit\n"
"\n"
"  --blockdev [driver=]<driver>[,node-name=<N>][,discard=ignore|unmap]\n"
"             [,cache.direct=on|off][,cache.no-flush=on|off]\n"
"             [,read-only=on|off][,auto-read-only=on|off]\n"
"             [,force-share=on|off][,detect-zeroes=on|off|unmap]\n"
"             [,driver specific parameters...]\n"
"                         configure a block backend\n"
"\n"
"  --chardev <options>    configure a character device backend\n"
"                         (see the qemu(1) man page for possible options)\n"
"\n"
"  --export [type=]nbd,id=<id>,node-name=<node-name>[,name=<export-name>]\n"
"           [,writable=on|off][,bitmap=<name>]\n"
"                         export the specified block node over NBD\n"
"                         (requires --nbd-server)\n"
"\n"
#ifdef CONFIG_FUSE
"  --export [type=]fuse,id=<id>,node-name=<node-name>,mountpoint=<file>\n"
"           [,growable=on|off][,writable=on|off]\n"
"                         export the specified block node over FUSE\n"
"\n"
#endif /* CONFIG_FUSE */
"  --monitor [chardev=]name[,mode=control][,pretty[=on|off]]\n"
"                         configure a QMP monitor\n"
"\n"
"  --nbd-server addr.type=inet,addr.host=<host>,addr.port=<port>\n"
"               [,tls-creds=<id>][,tls-authz=<id>][,max-connections=<n>]\n"
"  --nbd-server addr.type=unix,addr.path=<path>\n"
"               [,tls-creds=<id>][,tls-authz=<id>][,max-connections=<n>]\n"
"                         start an NBD server for exporting block nodes\n"
"\n"
"  --object help          list object types that can be added\n"
"  --object <type>,help   list properties for the given object type\n"
"  --object <type>[,<property>=<value>...]\n"
"                         create a new object of type <type>, setting\n"
"                         properties in the order they are specified. Note\n"
"                         that the 'id' property must be set.\n"
"                         See the qemu(1) man page for documentation of the\n"
"                         objects that can be added.\n"
"\n"
"  --pidfile <path>       write process ID to a file after startup\n"
"\n"
QEMU_HELP_BOTTOM "\n",
    error_get_progname());
}

enum {
    OPTION_BLOCKDEV = 256,
    OPTION_CHARDEV,
    OPTION_EXPORT,
    OPTION_MONITOR,
    OPTION_NBD_SERVER,
    OPTION_OBJECT,
    OPTION_PIDFILE,
};

extern QemuOptsList qemu_chardev_opts;

static void init_qmp_commands(void)
{
    qmp_init_marshal(&qmp_commands);

    QTAILQ_INIT(&qmp_cap_negotiation_commands);
    qmp_register_command(&qmp_cap_negotiation_commands, "qmp_capabilities",
                         qmp_marshal_qmp_capabilities,
                         QCO_ALLOW_PRECONFIG, 0);
}

static int getopt_set_loc(int argc, char **argv, const char *optstring,
                          const struct option *longopts)
{
    int c, save_index;

    optarg = NULL;
    save_index = optind;
    c = getopt_long(argc, argv, optstring, longopts, NULL);
    if (optarg) {
        loc_set_cmdline(argv, save_index, MAX(1, optind - save_index));
    }
    return c;
}

static void process_options(int argc, char *argv[])
{
    int c;

    static const struct option long_options[] = {
        {"blockdev", required_argument, NULL, OPTION_BLOCKDEV},
        {"chardev", required_argument, NULL, OPTION_CHARDEV},
        {"export", required_argument, NULL, OPTION_EXPORT},
        {"help", no_argument, NULL, 'h'},
        {"monitor", required_argument, NULL, OPTION_MONITOR},
        {"nbd-server", required_argument, NULL, OPTION_NBD_SERVER},
        {"object", required_argument, NULL, OPTION_OBJECT},
        {"pidfile", required_argument, NULL, OPTION_PIDFILE},
        {"trace", required_argument, NULL, 'T'},
        {"version", no_argument, NULL, 'V'},
        {0, 0, 0, 0}
    };

    /*
     * In contrast to the system emulator, options are processed in the order
     * they are given on the command lines. This means that things must be
     * defined first before they can be referenced in another option.
     */
    while ((c = getopt_set_loc(argc, argv, "-hT:V", long_options)) != -1) {
        switch (c) {
        case '?':
            exit(EXIT_FAILURE);
        case 'h':
            help();
            exit(EXIT_SUCCESS);
        case 'T':
            trace_opt_parse(optarg);
            trace_init_file();
            break;
        case 'V':
            printf("qemu-storage-daemon version "
                   QEMU_FULL_VERSION "\n" QEMU_COPYRIGHT "\n");
            exit(EXIT_SUCCESS);
        case OPTION_BLOCKDEV:
            {
                Visitor *v;
                BlockdevOptions *options;

                v = qobject_input_visitor_new_str(optarg, "driver",
                                                  &error_fatal);

                visit_type_BlockdevOptions(v, NULL, &options, &error_fatal);
                visit_free(v);

                qmp_blockdev_add(options, &error_fatal);
                qapi_free_BlockdevOptions(options);
                break;
            }
        case OPTION_CHARDEV:
            {
                /* TODO This interface is not stable until we QAPIfy it */
                QemuOpts *opts = qemu_opts_parse_noisily(&qemu_chardev_opts,
                                                         optarg, true);
                if (opts == NULL) {
                    exit(EXIT_FAILURE);
                }

                if (!qemu_chr_new_from_opts(opts, NULL, &error_fatal)) {
                    /* No error, but NULL returned means help was printed */
                    exit(EXIT_SUCCESS);
                }
                qemu_opts_del(opts);
                break;
            }
        case OPTION_EXPORT:
            {
                Visitor *v;
                BlockExportOptions *export;

                v = qobject_input_visitor_new_str(optarg, "type", &error_fatal);
                visit_type_BlockExportOptions(v, NULL, &export, &error_fatal);
                visit_free(v);

                qmp_block_export_add(export, &error_fatal);
                qapi_free_BlockExportOptions(export);
                break;
            }
        case OPTION_MONITOR:
            {
                Visitor *v;
                MonitorOptions *monitor;

                v = qobject_input_visitor_new_str(optarg, "chardev",
                                                  &error_fatal);
                visit_type_MonitorOptions(v, NULL, &monitor, &error_fatal);
                visit_free(v);

                /* TODO Catch duplicate monitor IDs */
                monitor_init(monitor, false, &error_fatal);
                qapi_free_MonitorOptions(monitor);
                break;
            }
        case OPTION_NBD_SERVER:
            {
                Visitor *v;
                NbdServerOptions *options;

                v = qobject_input_visitor_new_str(optarg, NULL, &error_fatal);
                visit_type_NbdServerOptions(v, NULL, &options, &error_fatal);
                visit_free(v);

                nbd_server_start_options(options, &error_fatal);
                qapi_free_NbdServerOptions(options);
                break;
            }
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        case OPTION_PIDFILE:
            pid_file = optarg;
            break;
        case 1:
            error_report("Unexpected argument");
            exit(EXIT_FAILURE);
        default:
            g_assert_not_reached();
        }
    }
    loc_set_none();
}

static void pid_file_cleanup(void)
{
    unlink(pid_file);
}

static void pid_file_init(void)
{
    Error *err = NULL;

    if (!pid_file) {
        return;
    }

    if (!qemu_write_pidfile(pid_file, &err)) {
        error_reportf_err(err, "cannot create PID file: ");
        exit(EXIT_FAILURE);
    }

    atexit(pid_file_cleanup);
}

int main(int argc, char *argv[])
{
#ifdef CONFIG_POSIX
    signal(SIGPIPE, SIG_IGN);
#endif

    error_init(argv[0]);
    qemu_init_exec_dir(argv[0]);
    os_setup_signal_handling();

    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_TRACE);
    qemu_add_opts(&qemu_trace_opts);
    qcrypto_init(&error_fatal);
    bdrv_init();
    monitor_init_globals_core();
    init_qmp_commands();

    if (!trace_init_backends()) {
        return EXIT_FAILURE;
    }
    qemu_set_log(LOG_TRACE);

    qemu_init_main_loop(&error_fatal);
    process_options(argc, argv);

    /*
     * Write the pid file after creating chardevs, exports, and NBD servers but
     * before accepting connections. This ordering is documented. Do not change
     * it.
     */
    pid_file_init();

    while (!exit_requested) {
        main_loop_wait(false);
    }

    blk_exp_close_all();
    bdrv_drain_all_begin();
    job_cancel_sync_all();
    bdrv_close_all();

    monitor_cleanup();
    qemu_chr_cleanup();
    user_creatable_cleanup();

    return EXIT_SUCCESS;
}
