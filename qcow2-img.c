#include "qemu/osdep.h"
#include "qemu-version.h"
#include "qapi/error.h"
#include "qapi-visit.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qjson.h"
#include "qemu/cutils.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "block/qapi.h"
#include "crypto/init.h"
#include "trace/control.h"
#include <getopt.h>

#define QEMU_IMG_VERSION "qemu-img version " QEMU_VERSION QEMU_PKGVERSION \
                          "\n" QEMU_COPYRIGHT "\n"

typedef struct img_cmd_t {
    const char *name;
    int (*handler)(int argc, char **argv);
} img_cmd_t;

enum {
    OPTION_OUTPUT = 256,
    OPTION_BACKING_CHAIN = 257,
    OPTION_OBJECT = 258,
    OPTION_IMAGE_OPTS = 259,
    OPTION_PATTERN = 260,
    OPTION_FLUSH_INTERVAL = 261,
    OPTION_NO_DRAIN = 262,
};

static QemuOptsList qemu_object_opts = {
    .name = "object",
    .implied_opt_name = "qom-type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_object_opts.head),
    .desc = {
        { }
    },
};

static QemuOptsList qemu_source_opts = {
    .name = "source",
    .implied_opt_name = "file",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_source_opts.head),
    .desc = {
        { }
    },
};

static void QEMU_NORETURN GCC_FMT_ATTR(1, 2) error_exit(const char *fmt, ...)
{
    va_list ap;

    error_printf("qcow2-img: ");

    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);

    error_printf("\nTry 'qcow2-img --help' for more information\n");
    exit(EXIT_FAILURE);
}

static void QEMU_NORETURN help(void)
{
    const char *help_msg =
            "usage: qcow2-img command [command options]\n"
            "create [-o options] {-t <template file> -l <layer UUID> -s <size>} filename\n";
    printf("%s", help_msg);
    exit(EXIT_SUCCESS);
}

static char *generate_encoded_backingfile(const char* template, const char* layer_uuid)
{
    char *backing_string = malloc(PATH_MAX*2);
    snprintf(backing_string, PATH_MAX*2, "qcow2://%s?layer=%s", template, layer_uuid);
    return backing_string;
}

static int img_resize(int argc, char **argv)
{
    Error *err = NULL;
    int c, ret, relative;
    const char *filename, *fmt, *size;
    int64_t n, total_size;
    bool quiet = false;
    BlockBackend *blk = NULL;
    QemuOpts *param;

    static QemuOptsList resize_options = {
        .name = "resize_options",
        .head = QTAILQ_HEAD_INITIALIZER(resize_options.head),
        .desc = {
            {
                .name = BLOCK_OPT_SIZE,
                .type = QEMU_OPT_SIZE,
                .help = "Virtual disk size"
            }, {
                /* end of list */
            }
        },
    };
    bool image_opts = false;

    /* Remove size from argv manually so that negative numbers are not treated
     * as options by getopt. */
    if (argc < 3) {
        error_exit("Not enough arguments");
        return 1;
    }

    size = argv[--argc];

    /* Parse getopt arguments */
    fmt = NULL;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "f:hq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                return 1;
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        }
    }
    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    filename = argv[optind++];

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        return 1;
    }

    /* Choose grow, shrink, or absolute resize mode */
    switch (size[0]) {
    case '+':
        relative = 1;
        size++;
        break;
    case '-':
        relative = -1;
        size++;
        break;
    default:
        relative = 0;
        break;
    }

    /* Parse size */
    param = qemu_opts_create(&resize_options, NULL, 0, &error_abort);
    qemu_opt_set(param, BLOCK_OPT_SIZE, size, &err);
    if (err) {
        error_report_err(err);
        ret = -1;
        qemu_opts_del(param);
        goto out;
    }
    n = qemu_opt_get_size(param, BLOCK_OPT_SIZE, 0);
    qemu_opts_del(param);

    blk = img_open(image_opts, filename, fmt,
                   BDRV_O_RDWR, false, quiet);
    if (!blk) {
        ret = -1;
        goto out;
    }

    if (relative) {
        total_size = blk_getlength(blk) + n * relative;
    } else {
        total_size = n;
    }
    if (total_size <= 0) {
        error_report("New image size must be positive");
        ret = -1;
        goto out;
    }

    ret = blk_truncate(blk, total_size);
    switch (ret) {
    case 0:
        qprintf(quiet, "Image resized.\n");
        break;
    case -ENOTSUP:
        error_report("This image does not support resize");
        break;
    case -EACCES:
        error_report("Image is read-only");
        break;
    default:
        error_report("Error resizing image: %s", strerror(-ret));
        break;
    }
out:
    blk_unref(blk);
    if (ret) {
        return 1;
    }
    return 0;
}

static int img_create(int argc, char **argv)
{
    int c;
    uint64_t img_size = -1;
    const char *filename = NULL;
    const char *layer_uuid = NULL;
    const char *template_filename = NULL;
    char *options = NULL;
    Error *local_err = NULL;
    bool quiet = false;
    int64_t sval;
    char *end;

    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "t:l:s:he6o:q",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            break;
        case 't':
            template_filename = optarg;
            break;
        case 'l':
            layer_uuid = optarg;
            break;
        case 's':
            sval = qemu_strtosz_suffix(optarg, &end,
                                       QEMU_STRTOSZ_DEFSUFFIX_B);
            if (sval < 0 || *end) {
                if (sval == -ERANGE) {
                    error_report("Image size must be less than 8 EiB!");
                } else {
                    error_report("Invalid image size specified! You may use k, M, "
                          "G, T, P or E suffixes for ()");
                    error_report("kilobytes, megabytes, gigabytes, terabytes, "
                                 "petabytes and exabytes.");
                }
                goto fail;
            }
            img_size = (uint64_t)sval;
            break;
        case 'e':
            error_report("option -e is deprecated, please use \'-o "
                  "encryption\' instead!");
            goto fail;
        case '6':
            error_report("option -6 is deprecated, please use \'-o "
                  "compat6\' instead!");
            goto fail;
        case 'o':
            if (!is_valid_option_list(optarg)) {
                error_report("Invalid option list: %s", optarg);
                goto fail;
            }
            if (!options) {
                options = g_strdup(optarg);
            } else {
                char *old_options = options;
                options = g_strdup_printf("%s,%s", options, optarg);
                g_free(old_options);
            }
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                goto fail;
            }
        }   break;
        }
    }

    /* Get the filename */
    filename = (optind < argc) ? argv[optind] : NULL;
    if (options && has_help_option(options)) {
        g_free(options);
        return -1;
    }

    if (optind >= argc) {
        error_exit("Expecting image file name");
    }
    optind++;

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        goto fail;
    }

    if (optind != argc) {
        error_exit("Unexpected argument: %s", argv[optind]);
    }

    const char *backing_string = NULL;
    if (template_filename && layer_uuid) {
        backing_string = generate_encoded_backingfile(template_filename, layer_uuid);
    }
    bdrv_img_create(filename, "qcow2", backing_string, NULL,
                    options, img_size, 0, &local_err, quiet);
    if (local_err) {
        error_reportf_err(local_err, "%s: ", filename);
        goto fail;
    }

    g_free(options);
    return 0;

fail:
    g_free(options);
    return 1;
}

static const img_cmd_t img_cmds[] = {
#define DEF(option, callback, arg_string)        \
    { option, callback },
// #include "qemu-img-cmds.h"
    DEF("create", img_create, "create [-o options] {-t <template file> -l <layer UUID> -s <size>} filename")
#undef DEF
#undef GEN_DOCS
    { NULL, NULL, },
};


int main(int argc, char **argv)
{
    const img_cmd_t *cmd;
    const char *cmdname;
    Error *local_error = NULL;
    char *trace_file = NULL;
    int c;
    static const struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {"trace", required_argument, NULL, 'T'},
        {0, 0, 0, 0}
    };

#ifdef CONFIG_POSIX
    signal(SIGPIPE, SIG_IGN);
#endif

    module_call_init(MODULE_INIT_TRACE);
    error_set_progname(argv[0]);
    qemu_init_exec_dir(argv[0]);

    if (qemu_init_main_loop(&local_error)) {
        error_report_err(local_error);
        exit(EXIT_FAILURE);
    }

    qcrypto_init(&error_fatal);

    module_call_init(MODULE_INIT_QOM);
    bdrv_init();
    if (argc < 2) {
        error_exit("Not enough arguments");
    }

    qemu_add_opts(&qemu_object_opts);
    qemu_add_opts(&qemu_source_opts);
    qemu_add_opts(&qemu_trace_opts);

    while ((c = getopt_long(argc, argv, "+hVT:", long_options, NULL)) != -1) {
        switch (c) {
        case 'h':
            help();
            return 0;
        case 'V':
            printf(QEMU_IMG_VERSION);
            return 0;
        case 'T':
            g_free(trace_file);
            trace_file = trace_opt_parse(optarg);
            break;
        }
    }

    cmdname = argv[optind];

    /* reset getopt_long scanning */
    argc -= optind;
    if (argc < 1) {
        return 0;
    }
    argv += optind;
    optind = 0;

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file(trace_file);
    qemu_set_log(LOG_TRACE);

    /* find the command */
    for (cmd = img_cmds; cmd->name != NULL; cmd++) {
        if (!strcmp(cmdname, cmd->name)) {
            return cmd->handler(argc, argv);
        }
    }

    /* not found */
    error_exit("Command not found: %s", cmdname);
}
