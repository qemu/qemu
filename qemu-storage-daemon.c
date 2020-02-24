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
#include "crypto/init.h"

#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu-version.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"

#include "trace/control.h"

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
QEMU_HELP_BOTTOM "\n",
    error_get_progname());
}

static void process_options(int argc, char *argv[])
{
    int c;

    static const struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"trace", required_argument, NULL, 'T'},
        {"version", no_argument, NULL, 'V'},
        {0, 0, 0, 0}
    };

    /*
     * In contrast to the system emulator, options are processed in the order
     * they are given on the command lines. This means that things must be
     * defined first before they can be referenced in another option.
     */
    while ((c = getopt_long(argc, argv, "hT:V", long_options, NULL)) != -1) {
        switch (c) {
        case '?':
            exit(EXIT_FAILURE);
        case 'h':
            help();
            exit(EXIT_SUCCESS);
        case 'T':
            {
                char *trace_file = trace_opt_parse(optarg);
                trace_init_file(trace_file);
                g_free(trace_file);
                break;
            }
        case 'V':
            printf("qemu-storage-daemon version "
                   QEMU_FULL_VERSION "\n" QEMU_COPYRIGHT "\n");
            exit(EXIT_SUCCESS);
        default:
            g_assert_not_reached();
        }
    }
    if (optind != argc) {
        error_report("Unexpected argument: %s", argv[optind]);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
#ifdef CONFIG_POSIX
    signal(SIGPIPE, SIG_IGN);
#endif

    error_init(argv[0]);
    qemu_init_exec_dir(argv[0]);

    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_TRACE);
    qemu_add_opts(&qemu_trace_opts);
    qcrypto_init(&error_fatal);
    bdrv_init();

    if (!trace_init_backends()) {
        return EXIT_FAILURE;
    }
    qemu_set_log(LOG_TRACE);

    qemu_init_main_loop(&error_fatal);
    process_options(argc, argv);

    return EXIT_SUCCESS;
}
