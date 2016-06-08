/*
 * QEMU disk image utility
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu-version.h"
#include "qapi/error.h"
#include "qapi-visit.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qjson.h"
#include "qemu/cutils.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "block/qapi.h"
#include "crypto/init.h"
#include <getopt.h>

#define QEMU_IMG_VERSION "qemu-img version " QEMU_VERSION QEMU_PKGVERSION \
                          ", Copyright (c) 2004-2008 Fabrice Bellard\n"

typedef struct img_cmd_t {
    const char *name;
    int (*handler)(int argc, char **argv);
} img_cmd_t;

enum {
    OPTION_OUTPUT = 256,
    OPTION_BACKING_CHAIN = 257,
    OPTION_OBJECT = 258,
    OPTION_IMAGE_OPTS = 259,
};

typedef enum OutputFormat {
    OFORMAT_JSON,
    OFORMAT_HUMAN,
} OutputFormat;

/* Default to cache=writeback as data integrity is not important for qemu-img */
#define BDRV_DEFAULT_CACHE "writeback"

static void format_print(void *opaque, const char *name)
{
    printf(" %s", name);
}

static void QEMU_NORETURN GCC_FMT_ATTR(1, 2) error_exit(const char *fmt, ...)
{
    va_list ap;

    error_printf("qemu-img: ");

    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);

    error_printf("\nTry 'qemu-img --help' for more information\n");
    exit(EXIT_FAILURE);
}

/* Please keep in synch with qemu-img.texi */
static void QEMU_NORETURN help(void)
{
    const char *help_msg =
           QEMU_IMG_VERSION
           "usage: qemu-img command [command options]\n"
           "QEMU disk image utility\n"
           "\n"
           "Command syntax:\n"
#define DEF(option, callback, arg_string)        \
           "  " arg_string "\n"
#include "qemu-img-cmds.h"
#undef DEF
#undef GEN_DOCS
           "\n"
           "Command parameters:\n"
           "  'filename' is a disk image filename\n"
           "  'objectdef' is a QEMU user creatable object definition. See the qemu(1)\n"
           "    manual page for a description of the object properties. The most common\n"
           "    object type is a 'secret', which is used to supply passwords and/or\n"
           "    encryption keys.\n"
           "  'fmt' is the disk image format. It is guessed automatically in most cases\n"
           "  'cache' is the cache mode used to write the output disk image, the valid\n"
           "    options are: 'none', 'writeback' (default, except for convert), 'writethrough',\n"
           "    'directsync' and 'unsafe' (default for convert)\n"
           "  'src_cache' is the cache mode used to read input disk images, the valid\n"
           "    options are the same as for the 'cache' option\n"
           "  'size' is the disk image size in bytes. Optional suffixes\n"
           "    'k' or 'K' (kilobyte, 1024), 'M' (megabyte, 1024k), 'G' (gigabyte, 1024M),\n"
           "    'T' (terabyte, 1024G), 'P' (petabyte, 1024T) and 'E' (exabyte, 1024P)  are\n"
           "    supported. 'b' is ignored.\n"
           "  'output_filename' is the destination disk image filename\n"
           "  'output_fmt' is the destination format\n"
           "  'options' is a comma separated list of format specific options in a\n"
           "    name=value format. Use -o ? for an overview of the options supported by the\n"
           "    used format\n"
           "  'snapshot_param' is param used for internal snapshot, format\n"
           "    is 'snapshot.id=[ID],snapshot.name=[NAME]', or\n"
           "    '[ID_OR_NAME]'\n"
           "  'snapshot_id_or_name' is deprecated, use 'snapshot_param'\n"
           "    instead\n"
           "  '-c' indicates that target image must be compressed (qcow format only)\n"
           "  '-u' enables unsafe rebasing. It is assumed that old and new backing file\n"
           "       match exactly. The image doesn't need a working backing file before\n"
           "       rebasing in this case (useful for renaming the backing file)\n"
           "  '-h' with or without a command shows this help and lists the supported formats\n"
           "  '-p' show progress of command (only certain commands)\n"
           "  '-q' use Quiet mode - do not print any output (except errors)\n"
           "  '-S' indicates the consecutive number of bytes (defaults to 4k) that must\n"
           "       contain only zeros for qemu-img to create a sparse image during\n"
           "       conversion. If the number of bytes is 0, the source will not be scanned for\n"
           "       unallocated or zero sectors, and the destination image will always be\n"
           "       fully allocated\n"
           "  '--output' takes the format in which the output must be done (human or json)\n"
           "  '-n' skips the target volume creation (useful if the volume is created\n"
           "       prior to running qemu-img)\n"
           "\n"
           "Parameters to check subcommand:\n"
           "  '-r' tries to repair any inconsistencies that are found during the check.\n"
           "       '-r leaks' repairs only cluster leaks, whereas '-r all' fixes all\n"
           "       kinds of errors, with a higher risk of choosing the wrong fix or\n"
           "       hiding corruption that has already occurred.\n"
           "\n"
           "Parameters to snapshot subcommand:\n"
           "  'snapshot' is the name of the snapshot to create, apply or delete\n"
           "  '-a' applies a snapshot (revert disk to saved state)\n"
           "  '-c' creates a snapshot\n"
           "  '-d' deletes a snapshot\n"
           "  '-l' lists all snapshots in the given image\n"
           "\n"
           "Parameters to compare subcommand:\n"
           "  '-f' first image format\n"
           "  '-F' second image format\n"
           "  '-s' run in Strict mode - fail on different image size or sector allocation\n";

    printf("%s\nSupported formats:", help_msg);
    bdrv_iterate_format(format_print, NULL);
    printf("\n");
    exit(EXIT_SUCCESS);
}

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

static int GCC_FMT_ATTR(2, 3) qprintf(bool quiet, const char *fmt, ...)
{
    int ret = 0;
    if (!quiet) {
        va_list args;
        va_start(args, fmt);
        ret = vprintf(fmt, args);
        va_end(args);
    }
    return ret;
}


static int print_block_option_help(const char *filename, const char *fmt)
{
    BlockDriver *drv, *proto_drv;
    QemuOptsList *create_opts = NULL;
    Error *local_err = NULL;

    /* Find driver and parse its options */
    drv = bdrv_find_format(fmt);
    if (!drv) {
        error_report("Unknown file format '%s'", fmt);
        return 1;
    }

    create_opts = qemu_opts_append(create_opts, drv->create_opts);
    if (filename) {
        proto_drv = bdrv_find_protocol(filename, true, &local_err);
        if (!proto_drv) {
            error_report_err(local_err);
            qemu_opts_free(create_opts);
            return 1;
        }
        create_opts = qemu_opts_append(create_opts, proto_drv->create_opts);
    }

    qemu_opts_print_help(create_opts);
    qemu_opts_free(create_opts);
    return 0;
}


static int img_open_password(BlockBackend *blk, const char *filename,
                             int flags, bool quiet)
{
    BlockDriverState *bs;
    char password[256];

    bs = blk_bs(blk);
    if (bdrv_is_encrypted(bs) && bdrv_key_required(bs) &&
        !(flags & BDRV_O_NO_IO)) {
        qprintf(quiet, "Disk image '%s' is encrypted.\n", filename);
        if (qemu_read_password(password, sizeof(password)) < 0) {
            error_report("No password given");
            return -1;
        }
        if (bdrv_set_key(bs, password) < 0) {
            error_report("invalid password");
            return -1;
        }
    }
    return 0;
}


static BlockBackend *img_open_opts(const char *optstr,
                                   QemuOpts *opts, int flags, bool writethrough,
                                   bool quiet)
{
    QDict *options;
    Error *local_err = NULL;
    BlockBackend *blk;
    options = qemu_opts_to_qdict(opts, NULL);
    blk = blk_new_open(NULL, NULL, options, flags, &local_err);
    if (!blk) {
        error_reportf_err(local_err, "Could not open '%s': ", optstr);
        return NULL;
    }
    blk_set_enable_write_cache(blk, !writethrough);

    if (img_open_password(blk, optstr, flags, quiet) < 0) {
        blk_unref(blk);
        return NULL;
    }
    return blk;
}

static BlockBackend *img_open_file(const char *filename,
                                   const char *fmt, int flags,
                                   bool writethrough, bool quiet)
{
    BlockBackend *blk;
    Error *local_err = NULL;
    QDict *options = NULL;

    if (fmt) {
        options = qdict_new();
        qdict_put(options, "driver", qstring_from_str(fmt));
    }

    blk = blk_new_open(filename, NULL, options, flags, &local_err);
    if (!blk) {
        error_reportf_err(local_err, "Could not open '%s': ", filename);
        return NULL;
    }
    blk_set_enable_write_cache(blk, !writethrough);

    if (img_open_password(blk, filename, flags, quiet) < 0) {
        blk_unref(blk);
        return NULL;
    }
    return blk;
}


static BlockBackend *img_open(bool image_opts,
                              const char *filename,
                              const char *fmt, int flags, bool writethrough,
                              bool quiet)
{
    BlockBackend *blk;
    if (image_opts) {
        QemuOpts *opts;
        if (fmt) {
            error_report("--image-opts and --format are mutually exclusive");
            return NULL;
        }
        opts = qemu_opts_parse_noisily(qemu_find_opts("source"),
                                       filename, true);
        if (!opts) {
            return NULL;
        }
        blk = img_open_opts(filename, opts, flags, writethrough, quiet);
    } else {
        blk = img_open_file(filename, fmt, flags, writethrough, quiet);
    }
    return blk;
}


static int add_old_style_options(const char *fmt, QemuOpts *opts,
                                 const char *base_filename,
                                 const char *base_fmt)
{
    Error *err = NULL;

    if (base_filename) {
        qemu_opt_set(opts, BLOCK_OPT_BACKING_FILE, base_filename, &err);
        if (err) {
            error_report("Backing file not supported for file format '%s'",
                         fmt);
            error_free(err);
            return -1;
        }
    }
    if (base_fmt) {
        qemu_opt_set(opts, BLOCK_OPT_BACKING_FMT, base_fmt, &err);
        if (err) {
            error_report("Backing file format not supported for file "
                         "format '%s'", fmt);
            error_free(err);
            return -1;
        }
    }
    return 0;
}

static int img_create(int argc, char **argv)
{
    int c;
    uint64_t img_size = -1;
    const char *fmt = "raw";
    const char *base_fmt = NULL;
    const char *filename;
    const char *base_filename = NULL;
    char *options = NULL;
    Error *local_err = NULL;
    bool quiet = false;

    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "F:b:f:he6o:q",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            break;
        case 'F':
            base_fmt = optarg;
            break;
        case 'b':
            base_filename = optarg;
            break;
        case 'f':
            fmt = optarg;
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
        return print_block_option_help(filename, fmt);
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

    /* Get image size, if specified */
    if (optind < argc) {
        int64_t sval;
        char *end;
        sval = qemu_strtosz_suffix(argv[optind++], &end,
                                   QEMU_STRTOSZ_DEFSUFFIX_B);
        if (sval < 0 || *end) {
            if (sval == -ERANGE) {
                error_report("Image size must be less than 8 EiB!");
            } else {
                error_report("Invalid image size specified! You may use k, M, "
                      "G, T, P or E suffixes for ");
                error_report("kilobytes, megabytes, gigabytes, terabytes, "
                             "petabytes and exabytes.");
            }
            goto fail;
        }
        img_size = (uint64_t)sval;
    }
    if (optind != argc) {
        error_exit("Unexpected argument: %s", argv[optind]);
    }

    bdrv_img_create(filename, fmt, base_filename, base_fmt,
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

static void dump_json_image_check(ImageCheck *check, bool quiet)
{
    Error *local_err = NULL;
    QString *str;
    QmpOutputVisitor *ov = qmp_output_visitor_new();
    QObject *obj;
    visit_type_ImageCheck(qmp_output_get_visitor(ov), NULL, &check,
                          &local_err);
    obj = qmp_output_get_qobject(ov);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    qprintf(quiet, "%s\n", qstring_get_str(str));
    qobject_decref(obj);
    qmp_output_visitor_cleanup(ov);
    QDECREF(str);
}

static void dump_human_image_check(ImageCheck *check, bool quiet)
{
    if (!(check->corruptions || check->leaks || check->check_errors)) {
        qprintf(quiet, "No errors were found on the image.\n");
    } else {
        if (check->corruptions) {
            qprintf(quiet, "\n%" PRId64 " errors were found on the image.\n"
                    "Data may be corrupted, or further writes to the image "
                    "may corrupt it.\n",
                    check->corruptions);
        }

        if (check->leaks) {
            qprintf(quiet,
                    "\n%" PRId64 " leaked clusters were found on the image.\n"
                    "This means waste of disk space, but no harm to data.\n",
                    check->leaks);
        }

        if (check->check_errors) {
            qprintf(quiet,
                    "\n%" PRId64
                    " internal errors have occurred during the check.\n",
                    check->check_errors);
        }
    }

    if (check->total_clusters != 0 && check->allocated_clusters != 0) {
        qprintf(quiet, "%" PRId64 "/%" PRId64 " = %0.2f%% allocated, "
                "%0.2f%% fragmented, %0.2f%% compressed clusters\n",
                check->allocated_clusters, check->total_clusters,
                check->allocated_clusters * 100.0 / check->total_clusters,
                check->fragmented_clusters * 100.0 / check->allocated_clusters,
                check->compressed_clusters * 100.0 /
                check->allocated_clusters);
    }

    if (check->image_end_offset) {
        qprintf(quiet,
                "Image end offset: %" PRId64 "\n", check->image_end_offset);
    }
}

static int collect_image_check(BlockDriverState *bs,
                   ImageCheck *check,
                   const char *filename,
                   const char *fmt,
                   int fix)
{
    int ret;
    BdrvCheckResult result;

    ret = bdrv_check(bs, &result, fix);
    if (ret < 0) {
        return ret;
    }

    check->filename                 = g_strdup(filename);
    check->format                   = g_strdup(bdrv_get_format_name(bs));
    check->check_errors             = result.check_errors;
    check->corruptions              = result.corruptions;
    check->has_corruptions          = result.corruptions != 0;
    check->leaks                    = result.leaks;
    check->has_leaks                = result.leaks != 0;
    check->corruptions_fixed        = result.corruptions_fixed;
    check->has_corruptions_fixed    = result.corruptions != 0;
    check->leaks_fixed              = result.leaks_fixed;
    check->has_leaks_fixed          = result.leaks != 0;
    check->image_end_offset         = result.image_end_offset;
    check->has_image_end_offset     = result.image_end_offset != 0;
    check->total_clusters           = result.bfi.total_clusters;
    check->has_total_clusters       = result.bfi.total_clusters != 0;
    check->allocated_clusters       = result.bfi.allocated_clusters;
    check->has_allocated_clusters   = result.bfi.allocated_clusters != 0;
    check->fragmented_clusters      = result.bfi.fragmented_clusters;
    check->has_fragmented_clusters  = result.bfi.fragmented_clusters != 0;
    check->compressed_clusters      = result.bfi.compressed_clusters;
    check->has_compressed_clusters  = result.bfi.compressed_clusters != 0;

    return 0;
}

/*
 * Checks an image for consistency. Exit codes:
 *
 *  0 - Check completed, image is good
 *  1 - Check not completed because of internal errors
 *  2 - Check completed, image is corrupted
 *  3 - Check completed, image has leaked clusters, but is good otherwise
 * 63 - Checks are not supported by the image format
 */
static int img_check(int argc, char **argv)
{
    int c, ret;
    OutputFormat output_format = OFORMAT_HUMAN;
    const char *filename, *fmt, *output, *cache;
    BlockBackend *blk;
    BlockDriverState *bs;
    int fix = 0;
    int flags = BDRV_O_CHECK;
    bool writethrough;
    ImageCheck *check;
    bool quiet = false;
    bool image_opts = false;

    fmt = NULL;
    output = NULL;
    cache = BDRV_DEFAULT_CACHE;

    for(;;) {
        int option_index = 0;
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"repair", required_argument, 0, 'r'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:r:T:q",
                        long_options, &option_index);
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
        case 'r':
            flags |= BDRV_O_RDWR;

            if (!strcmp(optarg, "leaks")) {
                fix = BDRV_FIX_LEAKS;
            } else if (!strcmp(optarg, "all")) {
                fix = BDRV_FIX_LEAKS | BDRV_FIX_ERRORS;
            } else {
                error_exit("Unknown option value for -r "
                           "(expecting 'leaks' or 'all'): %s", optarg);
            }
            break;
        case OPTION_OUTPUT:
            output = optarg;
            break;
        case 'T':
            cache = optarg;
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

    if (output && !strcmp(output, "json")) {
        output_format = OFORMAT_JSON;
    } else if (output && !strcmp(output, "human")) {
        output_format = OFORMAT_HUMAN;
    } else if (output) {
        error_report("--output must be used with human or json as argument.");
        return 1;
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        return 1;
    }

    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid source cache option: %s", cache);
        return 1;
    }

    blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet);
    if (!blk) {
        return 1;
    }
    bs = blk_bs(blk);

    check = g_new0(ImageCheck, 1);
    ret = collect_image_check(bs, check, filename, fmt, fix);

    if (ret == -ENOTSUP) {
        error_report("This image format does not support checks");
        ret = 63;
        goto fail;
    }

    if (check->corruptions_fixed || check->leaks_fixed) {
        int corruptions_fixed, leaks_fixed;

        leaks_fixed         = check->leaks_fixed;
        corruptions_fixed   = check->corruptions_fixed;

        if (output_format == OFORMAT_HUMAN) {
            qprintf(quiet,
                    "The following inconsistencies were found and repaired:\n\n"
                    "    %" PRId64 " leaked clusters\n"
                    "    %" PRId64 " corruptions\n\n"
                    "Double checking the fixed image now...\n",
                    check->leaks_fixed,
                    check->corruptions_fixed);
        }

        ret = collect_image_check(bs, check, filename, fmt, 0);

        check->leaks_fixed          = leaks_fixed;
        check->corruptions_fixed    = corruptions_fixed;
    }

    if (!ret) {
        switch (output_format) {
        case OFORMAT_HUMAN:
            dump_human_image_check(check, quiet);
            break;
        case OFORMAT_JSON:
            dump_json_image_check(check, quiet);
            break;
        }
    }

    if (ret || check->check_errors) {
        if (ret) {
            error_report("Check failed: %s", strerror(-ret));
        } else {
            error_report("Check failed");
        }
        ret = 1;
        goto fail;
    }

    if (check->corruptions) {
        ret = 2;
    } else if (check->leaks) {
        ret = 3;
    } else {
        ret = 0;
    }

fail:
    qapi_free_ImageCheck(check);
    blk_unref(blk);
    return ret;
}

typedef struct CommonBlockJobCBInfo {
    BlockDriverState *bs;
    Error **errp;
} CommonBlockJobCBInfo;

static void common_block_job_cb(void *opaque, int ret)
{
    CommonBlockJobCBInfo *cbi = opaque;

    if (ret < 0) {
        error_setg_errno(cbi->errp, -ret, "Block job failed");
    }
}

static void run_block_job(BlockJob *job, Error **errp)
{
    AioContext *aio_context = blk_get_aio_context(job->blk);

    do {
        aio_poll(aio_context, true);
        qemu_progress_print(job->len ?
                            ((float)job->offset / job->len * 100.f) : 0.0f, 0);
    } while (!job->ready);

    block_job_complete_sync(job, errp);

    /* A block job may finish instantaneously without publishing any progress,
     * so just signal completion here */
    qemu_progress_print(100.f, 0);
}

static int img_commit(int argc, char **argv)
{
    int c, ret, flags;
    const char *filename, *fmt, *cache, *base;
    BlockBackend *blk;
    BlockDriverState *bs, *base_bs;
    bool progress = false, quiet = false, drop = false;
    bool writethrough;
    Error *local_err = NULL;
    CommonBlockJobCBInfo cbi;
    bool image_opts = false;

    fmt = NULL;
    cache = BDRV_DEFAULT_CACHE;
    base = NULL;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "f:ht:b:dpq",
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
        case 't':
            cache = optarg;
            break;
        case 'b':
            base = optarg;
            /* -b implies -d */
            drop = true;
            break;
        case 'd':
            drop = true;
            break;
        case 'p':
            progress = true;
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

    /* Progress is not shown in Quiet mode */
    if (quiet) {
        progress = false;
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

    flags = BDRV_O_RDWR | BDRV_O_UNMAP;
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        return 1;
    }

    blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet);
    if (!blk) {
        return 1;
    }
    bs = blk_bs(blk);

    qemu_progress_init(progress, 1.f);
    qemu_progress_print(0.f, 100);

    if (base) {
        base_bs = bdrv_find_backing_image(bs, base);
        if (!base_bs) {
            error_setg(&local_err, QERR_BASE_NOT_FOUND, base);
            goto done;
        }
    } else {
        /* This is different from QMP, which by default uses the deepest file in
         * the backing chain (i.e., the very base); however, the traditional
         * behavior of qemu-img commit is using the immediate backing file. */
        base_bs = backing_bs(bs);
        if (!base_bs) {
            error_setg(&local_err, "Image does not have a backing file");
            goto done;
        }
    }

    cbi = (CommonBlockJobCBInfo){
        .errp = &local_err,
        .bs   = bs,
    };

    commit_active_start(bs, base_bs, 0, BLOCKDEV_ON_ERROR_REPORT,
                        common_block_job_cb, &cbi, &local_err);
    if (local_err) {
        goto done;
    }

    /* When the block job completes, the BlockBackend reference will point to
     * the old backing file. In order to avoid that the top image is already
     * deleted, so we can still empty it afterwards, increment the reference
     * counter here preemptively. */
    if (!drop) {
        bdrv_ref(bs);
    }

    run_block_job(bs->job, &local_err);
    if (local_err) {
        goto unref_backing;
    }

    if (!drop && bs->drv->bdrv_make_empty) {
        ret = bs->drv->bdrv_make_empty(bs);
        if (ret) {
            error_setg_errno(&local_err, -ret, "Could not empty %s",
                             filename);
            goto unref_backing;
        }
    }

unref_backing:
    if (!drop) {
        bdrv_unref(bs);
    }

done:
    qemu_progress_end();

    blk_unref(blk);

    if (local_err) {
        error_report_err(local_err);
        return 1;
    }

    qprintf(quiet, "Image committed.\n");
    return 0;
}

/*
 * Returns true iff the first sector pointed to by 'buf' contains at least
 * a non-NUL byte.
 *
 * 'pnum' is set to the number of sectors (including and immediately following
 * the first one) that are known to be in the same allocated/unallocated state.
 */
static int is_allocated_sectors(const uint8_t *buf, int n, int *pnum)
{
    bool is_zero;
    int i;

    if (n <= 0) {
        *pnum = 0;
        return 0;
    }
    is_zero = buffer_is_zero(buf, 512);
    for(i = 1; i < n; i++) {
        buf += 512;
        if (is_zero != buffer_is_zero(buf, 512)) {
            break;
        }
    }
    *pnum = i;
    return !is_zero;
}

/*
 * Like is_allocated_sectors, but if the buffer starts with a used sector,
 * up to 'min' consecutive sectors containing zeros are ignored. This avoids
 * breaking up write requests for only small sparse areas.
 */
static int is_allocated_sectors_min(const uint8_t *buf, int n, int *pnum,
    int min)
{
    int ret;
    int num_checked, num_used;

    if (n < min) {
        min = n;
    }

    ret = is_allocated_sectors(buf, n, pnum);
    if (!ret) {
        return ret;
    }

    num_used = *pnum;
    buf += BDRV_SECTOR_SIZE * *pnum;
    n -= *pnum;
    num_checked = num_used;

    while (n > 0) {
        ret = is_allocated_sectors(buf, n, pnum);

        buf += BDRV_SECTOR_SIZE * *pnum;
        n -= *pnum;
        num_checked += *pnum;
        if (ret) {
            num_used = num_checked;
        } else if (*pnum >= min) {
            break;
        }
    }

    *pnum = num_used;
    return 1;
}

/*
 * Compares two buffers sector by sector. Returns 0 if the first sector of both
 * buffers matches, non-zero otherwise.
 *
 * pnum is set to the number of sectors (including and immediately following
 * the first one) that are known to have the same comparison result
 */
static int compare_sectors(const uint8_t *buf1, const uint8_t *buf2, int n,
    int *pnum)
{
    bool res;
    int i;

    if (n <= 0) {
        *pnum = 0;
        return 0;
    }

    res = !!memcmp(buf1, buf2, 512);
    for(i = 1; i < n; i++) {
        buf1 += 512;
        buf2 += 512;

        if (!!memcmp(buf1, buf2, 512) != res) {
            break;
        }
    }

    *pnum = i;
    return res;
}

#define IO_BUF_SIZE (2 * 1024 * 1024)

static int64_t sectors_to_bytes(int64_t sectors)
{
    return sectors << BDRV_SECTOR_BITS;
}

static int64_t sectors_to_process(int64_t total, int64_t from)
{
    return MIN(total - from, IO_BUF_SIZE >> BDRV_SECTOR_BITS);
}

/*
 * Check if passed sectors are empty (not allocated or contain only 0 bytes)
 *
 * Returns 0 in case sectors are filled with 0, 1 if sectors contain non-zero
 * data and negative value on error.
 *
 * @param blk:  BlockBackend for the image
 * @param sect_num: Number of first sector to check
 * @param sect_count: Number of sectors to check
 * @param filename: Name of disk file we are checking (logging purpose)
 * @param buffer: Allocated buffer for storing read data
 * @param quiet: Flag for quiet mode
 */
static int check_empty_sectors(BlockBackend *blk, int64_t sect_num,
                               int sect_count, const char *filename,
                               uint8_t *buffer, bool quiet)
{
    int pnum, ret = 0;
    ret = blk_pread(blk, sect_num << BDRV_SECTOR_BITS, buffer,
                    sect_count << BDRV_SECTOR_BITS);
    if (ret < 0) {
        error_report("Error while reading offset %" PRId64 " of %s: %s",
                     sectors_to_bytes(sect_num), filename, strerror(-ret));
        return ret;
    }
    ret = is_allocated_sectors(buffer, sect_count, &pnum);
    if (ret || pnum != sect_count) {
        qprintf(quiet, "Content mismatch at offset %" PRId64 "!\n",
                sectors_to_bytes(ret ? sect_num : sect_num + pnum));
        return 1;
    }

    return 0;
}

/*
 * Compares two images. Exit codes:
 *
 * 0 - Images are identical
 * 1 - Images differ
 * >1 - Error occurred
 */
static int img_compare(int argc, char **argv)
{
    const char *fmt1 = NULL, *fmt2 = NULL, *cache, *filename1, *filename2;
    BlockBackend *blk1, *blk2;
    BlockDriverState *bs1, *bs2;
    int64_t total_sectors1, total_sectors2;
    uint8_t *buf1 = NULL, *buf2 = NULL;
    int pnum1, pnum2;
    int allocated1, allocated2;
    int ret = 0; /* return value - 0 Ident, 1 Different, >1 Error */
    bool progress = false, quiet = false, strict = false;
    int flags;
    bool writethrough;
    int64_t total_sectors;
    int64_t sector_num = 0;
    int64_t nb_sectors;
    int c, pnum;
    uint64_t progress_base;
    bool image_opts = false;

    cache = BDRV_DEFAULT_CACHE;
    for (;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:F:T:pqs",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case '?':
        case 'h':
            help();
            break;
        case 'f':
            fmt1 = optarg;
            break;
        case 'F':
            fmt2 = optarg;
            break;
        case 'T':
            cache = optarg;
            break;
        case 'p':
            progress = true;
            break;
        case 'q':
            quiet = true;
            break;
        case 's':
            strict = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                ret = 2;
                goto out4;
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        }
    }

    /* Progress is not shown in Quiet mode */
    if (quiet) {
        progress = false;
    }


    if (optind != argc - 2) {
        error_exit("Expecting two image file names");
    }
    filename1 = argv[optind++];
    filename2 = argv[optind++];

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        ret = 2;
        goto out4;
    }

    /* Initialize before goto out */
    qemu_progress_init(progress, 2.0);

    flags = 0;
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid source cache option: %s", cache);
        ret = 2;
        goto out3;
    }

    blk1 = img_open(image_opts, filename1, fmt1, flags, writethrough, quiet);
    if (!blk1) {
        ret = 2;
        goto out3;
    }

    blk2 = img_open(image_opts, filename2, fmt2, flags, writethrough, quiet);
    if (!blk2) {
        ret = 2;
        goto out2;
    }
    bs1 = blk_bs(blk1);
    bs2 = blk_bs(blk2);

    buf1 = blk_blockalign(blk1, IO_BUF_SIZE);
    buf2 = blk_blockalign(blk2, IO_BUF_SIZE);
    total_sectors1 = blk_nb_sectors(blk1);
    if (total_sectors1 < 0) {
        error_report("Can't get size of %s: %s",
                     filename1, strerror(-total_sectors1));
        ret = 4;
        goto out;
    }
    total_sectors2 = blk_nb_sectors(blk2);
    if (total_sectors2 < 0) {
        error_report("Can't get size of %s: %s",
                     filename2, strerror(-total_sectors2));
        ret = 4;
        goto out;
    }
    total_sectors = MIN(total_sectors1, total_sectors2);
    progress_base = MAX(total_sectors1, total_sectors2);

    qemu_progress_print(0, 100);

    if (strict && total_sectors1 != total_sectors2) {
        ret = 1;
        qprintf(quiet, "Strict mode: Image size mismatch!\n");
        goto out;
    }

    for (;;) {
        int64_t status1, status2;
        BlockDriverState *file;

        nb_sectors = sectors_to_process(total_sectors, sector_num);
        if (nb_sectors <= 0) {
            break;
        }
        status1 = bdrv_get_block_status_above(bs1, NULL, sector_num,
                                              total_sectors1 - sector_num,
                                              &pnum1, &file);
        if (status1 < 0) {
            ret = 3;
            error_report("Sector allocation test failed for %s", filename1);
            goto out;
        }
        allocated1 = status1 & BDRV_BLOCK_ALLOCATED;

        status2 = bdrv_get_block_status_above(bs2, NULL, sector_num,
                                              total_sectors2 - sector_num,
                                              &pnum2, &file);
        if (status2 < 0) {
            ret = 3;
            error_report("Sector allocation test failed for %s", filename2);
            goto out;
        }
        allocated2 = status2 & BDRV_BLOCK_ALLOCATED;
        if (pnum1) {
            nb_sectors = MIN(nb_sectors, pnum1);
        }
        if (pnum2) {
            nb_sectors = MIN(nb_sectors, pnum2);
        }

        if (strict) {
            if ((status1 & ~BDRV_BLOCK_OFFSET_MASK) !=
                (status2 & ~BDRV_BLOCK_OFFSET_MASK)) {
                ret = 1;
                qprintf(quiet, "Strict mode: Offset %" PRId64
                        " block status mismatch!\n",
                        sectors_to_bytes(sector_num));
                goto out;
            }
        }
        if ((status1 & BDRV_BLOCK_ZERO) && (status2 & BDRV_BLOCK_ZERO)) {
            nb_sectors = MIN(pnum1, pnum2);
        } else if (allocated1 == allocated2) {
            if (allocated1) {
                ret = blk_pread(blk1, sector_num << BDRV_SECTOR_BITS, buf1,
                                nb_sectors << BDRV_SECTOR_BITS);
                if (ret < 0) {
                    error_report("Error while reading offset %" PRId64 " of %s:"
                                 " %s", sectors_to_bytes(sector_num), filename1,
                                 strerror(-ret));
                    ret = 4;
                    goto out;
                }
                ret = blk_pread(blk2, sector_num << BDRV_SECTOR_BITS, buf2,
                                nb_sectors << BDRV_SECTOR_BITS);
                if (ret < 0) {
                    error_report("Error while reading offset %" PRId64
                                 " of %s: %s", sectors_to_bytes(sector_num),
                                 filename2, strerror(-ret));
                    ret = 4;
                    goto out;
                }
                ret = compare_sectors(buf1, buf2, nb_sectors, &pnum);
                if (ret || pnum != nb_sectors) {
                    qprintf(quiet, "Content mismatch at offset %" PRId64 "!\n",
                            sectors_to_bytes(
                                ret ? sector_num : sector_num + pnum));
                    ret = 1;
                    goto out;
                }
            }
        } else {

            if (allocated1) {
                ret = check_empty_sectors(blk1, sector_num, nb_sectors,
                                          filename1, buf1, quiet);
            } else {
                ret = check_empty_sectors(blk2, sector_num, nb_sectors,
                                          filename2, buf1, quiet);
            }
            if (ret) {
                if (ret < 0) {
                    error_report("Error while reading offset %" PRId64 ": %s",
                                 sectors_to_bytes(sector_num), strerror(-ret));
                    ret = 4;
                }
                goto out;
            }
        }
        sector_num += nb_sectors;
        qemu_progress_print(((float) nb_sectors / progress_base)*100, 100);
    }

    if (total_sectors1 != total_sectors2) {
        BlockBackend *blk_over;
        int64_t total_sectors_over;
        const char *filename_over;

        qprintf(quiet, "Warning: Image size mismatch!\n");
        if (total_sectors1 > total_sectors2) {
            total_sectors_over = total_sectors1;
            blk_over = blk1;
            filename_over = filename1;
        } else {
            total_sectors_over = total_sectors2;
            blk_over = blk2;
            filename_over = filename2;
        }

        for (;;) {
            nb_sectors = sectors_to_process(total_sectors_over, sector_num);
            if (nb_sectors <= 0) {
                break;
            }
            ret = bdrv_is_allocated_above(blk_bs(blk_over), NULL, sector_num,
                                          nb_sectors, &pnum);
            if (ret < 0) {
                ret = 3;
                error_report("Sector allocation test failed for %s",
                             filename_over);
                goto out;

            }
            nb_sectors = pnum;
            if (ret) {
                ret = check_empty_sectors(blk_over, sector_num, nb_sectors,
                                          filename_over, buf1, quiet);
                if (ret) {
                    if (ret < 0) {
                        error_report("Error while reading offset %" PRId64
                                     " of %s: %s", sectors_to_bytes(sector_num),
                                     filename_over, strerror(-ret));
                        ret = 4;
                    }
                    goto out;
                }
            }
            sector_num += nb_sectors;
            qemu_progress_print(((float) nb_sectors / progress_base)*100, 100);
        }
    }

    qprintf(quiet, "Images are identical.\n");
    ret = 0;

out:
    qemu_vfree(buf1);
    qemu_vfree(buf2);
    blk_unref(blk2);
out2:
    blk_unref(blk1);
out3:
    qemu_progress_end();
out4:
    return ret;
}

enum ImgConvertBlockStatus {
    BLK_DATA,
    BLK_ZERO,
    BLK_BACKING_FILE,
};

typedef struct ImgConvertState {
    BlockBackend **src;
    int64_t *src_sectors;
    int src_cur, src_num;
    int64_t src_cur_offset;
    int64_t total_sectors;
    int64_t allocated_sectors;
    enum ImgConvertBlockStatus status;
    int64_t sector_next_status;
    BlockBackend *target;
    bool has_zero_init;
    bool compressed;
    bool target_has_backing;
    int min_sparse;
    size_t cluster_sectors;
    size_t buf_sectors;
} ImgConvertState;

static void convert_select_part(ImgConvertState *s, int64_t sector_num)
{
    assert(sector_num >= s->src_cur_offset);
    while (sector_num - s->src_cur_offset >= s->src_sectors[s->src_cur]) {
        s->src_cur_offset += s->src_sectors[s->src_cur];
        s->src_cur++;
        assert(s->src_cur < s->src_num);
    }
}

static int convert_iteration_sectors(ImgConvertState *s, int64_t sector_num)
{
    int64_t ret;
    int n;

    convert_select_part(s, sector_num);

    assert(s->total_sectors > sector_num);
    n = MIN(s->total_sectors - sector_num, BDRV_REQUEST_MAX_SECTORS);

    if (s->sector_next_status <= sector_num) {
        BlockDriverState *file;
        ret = bdrv_get_block_status(blk_bs(s->src[s->src_cur]),
                                    sector_num - s->src_cur_offset,
                                    n, &n, &file);
        if (ret < 0) {
            return ret;
        }

        if (ret & BDRV_BLOCK_ZERO) {
            s->status = BLK_ZERO;
        } else if (ret & BDRV_BLOCK_DATA) {
            s->status = BLK_DATA;
        } else if (!s->target_has_backing) {
            /* Without a target backing file we must copy over the contents of
             * the backing file as well. */
            /* Check block status of the backing file chain to avoid
             * needlessly reading zeroes and limiting the iteration to the
             * buffer size */
            ret = bdrv_get_block_status_above(blk_bs(s->src[s->src_cur]), NULL,
                                              sector_num - s->src_cur_offset,
                                              n, &n, &file);
            if (ret < 0) {
                return ret;
            }

            if (ret & BDRV_BLOCK_ZERO) {
                s->status = BLK_ZERO;
            } else {
                s->status = BLK_DATA;
            }
        } else {
            s->status = BLK_BACKING_FILE;
        }

        s->sector_next_status = sector_num + n;
    }

    n = MIN(n, s->sector_next_status - sector_num);
    if (s->status == BLK_DATA) {
        n = MIN(n, s->buf_sectors);
    }

    /* We need to write complete clusters for compressed images, so if an
     * unallocated area is shorter than that, we must consider the whole
     * cluster allocated. */
    if (s->compressed) {
        if (n < s->cluster_sectors) {
            n = MIN(s->cluster_sectors, s->total_sectors - sector_num);
            s->status = BLK_DATA;
        } else {
            n = QEMU_ALIGN_DOWN(n, s->cluster_sectors);
        }
    }

    return n;
}

static int convert_read(ImgConvertState *s, int64_t sector_num, int nb_sectors,
                        uint8_t *buf)
{
    int n;
    int ret;

    assert(nb_sectors <= s->buf_sectors);
    while (nb_sectors > 0) {
        BlockBackend *blk;
        int64_t bs_sectors;

        /* In the case of compression with multiple source files, we can get a
         * nb_sectors that spreads into the next part. So we must be able to
         * read across multiple BDSes for one convert_read() call. */
        convert_select_part(s, sector_num);
        blk = s->src[s->src_cur];
        bs_sectors = s->src_sectors[s->src_cur];

        n = MIN(nb_sectors, bs_sectors - (sector_num - s->src_cur_offset));
        ret = blk_pread(blk,
                        (sector_num - s->src_cur_offset) << BDRV_SECTOR_BITS,
                        buf, n << BDRV_SECTOR_BITS);
        if (ret < 0) {
            return ret;
        }

        sector_num += n;
        nb_sectors -= n;
        buf += n * BDRV_SECTOR_SIZE;
    }

    return 0;
}

static int convert_write(ImgConvertState *s, int64_t sector_num, int nb_sectors,
                         const uint8_t *buf)
{
    int ret;

    while (nb_sectors > 0) {
        int n = nb_sectors;

        switch (s->status) {
        case BLK_BACKING_FILE:
            /* If we have a backing file, leave clusters unallocated that are
             * unallocated in the source image, so that the backing file is
             * visible at the respective offset. */
            assert(s->target_has_backing);
            break;

        case BLK_DATA:
            /* We must always write compressed clusters as a whole, so don't
             * try to find zeroed parts in the buffer. We can only save the
             * write if the buffer is completely zeroed and we're allowed to
             * keep the target sparse. */
            if (s->compressed) {
                if (s->has_zero_init && s->min_sparse &&
                    buffer_is_zero(buf, n * BDRV_SECTOR_SIZE))
                {
                    assert(!s->target_has_backing);
                    break;
                }

                ret = blk_write_compressed(s->target, sector_num, buf, n);
                if (ret < 0) {
                    return ret;
                }
                break;
            }

            /* If there is real non-zero data or we're told to keep the target
             * fully allocated (-S 0), we must write it. Otherwise we can treat
             * it as zero sectors. */
            if (!s->min_sparse ||
                is_allocated_sectors_min(buf, n, &n, s->min_sparse))
            {
                ret = blk_pwrite(s->target, sector_num << BDRV_SECTOR_BITS,
                                 buf, n << BDRV_SECTOR_BITS, 0);
                if (ret < 0) {
                    return ret;
                }
                break;
            }
            /* fall-through */

        case BLK_ZERO:
            if (s->has_zero_init) {
                break;
            }
            ret = blk_pwrite_zeroes(s->target, sector_num << BDRV_SECTOR_BITS,
                                    n << BDRV_SECTOR_BITS, 0);
            if (ret < 0) {
                return ret;
            }
            break;
        }

        sector_num += n;
        nb_sectors -= n;
        buf += n * BDRV_SECTOR_SIZE;
    }

    return 0;
}

static int convert_do_copy(ImgConvertState *s)
{
    uint8_t *buf = NULL;
    int64_t sector_num, allocated_done;
    int ret;
    int n;

    /* Check whether we have zero initialisation or can get it efficiently */
    s->has_zero_init = s->min_sparse && !s->target_has_backing
                     ? bdrv_has_zero_init(blk_bs(s->target))
                     : false;

    if (!s->has_zero_init && !s->target_has_backing &&
        bdrv_can_write_zeroes_with_unmap(blk_bs(s->target)))
    {
        ret = bdrv_make_zero(blk_bs(s->target), BDRV_REQ_MAY_UNMAP);
        if (ret == 0) {
            s->has_zero_init = true;
        }
    }

    /* Allocate buffer for copied data. For compressed images, only one cluster
     * can be copied at a time. */
    if (s->compressed) {
        if (s->cluster_sectors <= 0 || s->cluster_sectors > s->buf_sectors) {
            error_report("invalid cluster size");
            ret = -EINVAL;
            goto fail;
        }
        s->buf_sectors = s->cluster_sectors;
    }
    buf = blk_blockalign(s->target, s->buf_sectors * BDRV_SECTOR_SIZE);

    /* Calculate allocated sectors for progress */
    s->allocated_sectors = 0;
    sector_num = 0;
    while (sector_num < s->total_sectors) {
        n = convert_iteration_sectors(s, sector_num);
        if (n < 0) {
            ret = n;
            goto fail;
        }
        if (s->status == BLK_DATA || (!s->min_sparse && s->status == BLK_ZERO))
        {
            s->allocated_sectors += n;
        }
        sector_num += n;
    }

    /* Do the copy */
    s->src_cur = 0;
    s->src_cur_offset = 0;
    s->sector_next_status = 0;

    sector_num = 0;
    allocated_done = 0;

    while (sector_num < s->total_sectors) {
        n = convert_iteration_sectors(s, sector_num);
        if (n < 0) {
            ret = n;
            goto fail;
        }
        if (s->status == BLK_DATA || (!s->min_sparse && s->status == BLK_ZERO))
        {
            allocated_done += n;
            qemu_progress_print(100.0 * allocated_done / s->allocated_sectors,
                                0);
        }

        if (s->status == BLK_DATA) {
            ret = convert_read(s, sector_num, n, buf);
            if (ret < 0) {
                error_report("error while reading sector %" PRId64
                             ": %s", sector_num, strerror(-ret));
                goto fail;
            }
        } else if (!s->min_sparse && s->status == BLK_ZERO) {
            n = MIN(n, s->buf_sectors);
            memset(buf, 0, n * BDRV_SECTOR_SIZE);
            s->status = BLK_DATA;
        }

        ret = convert_write(s, sector_num, n, buf);
        if (ret < 0) {
            error_report("error while writing sector %" PRId64
                         ": %s", sector_num, strerror(-ret));
            goto fail;
        }

        sector_num += n;
    }

    if (s->compressed) {
        /* signal EOF to align */
        ret = blk_write_compressed(s->target, 0, NULL, 0);
        if (ret < 0) {
            goto fail;
        }
    }

    ret = 0;
fail:
    qemu_vfree(buf);
    return ret;
}

static int img_convert(int argc, char **argv)
{
    int c, bs_n, bs_i, compress, cluster_sectors, skip_create;
    int64_t ret = 0;
    int progress = 0, flags, src_flags;
    bool writethrough, src_writethrough;
    const char *fmt, *out_fmt, *cache, *src_cache, *out_baseimg, *out_filename;
    BlockDriver *drv, *proto_drv;
    BlockBackend **blk = NULL, *out_blk = NULL;
    BlockDriverState **bs = NULL, *out_bs = NULL;
    int64_t total_sectors;
    int64_t *bs_sectors = NULL;
    size_t bufsectors = IO_BUF_SIZE / BDRV_SECTOR_SIZE;
    BlockDriverInfo bdi;
    QemuOpts *opts = NULL;
    QemuOptsList *create_opts = NULL;
    const char *out_baseimg_param;
    char *options = NULL;
    const char *snapshot_name = NULL;
    int min_sparse = 8; /* Need at least 4k of zeros for sparse detection */
    bool quiet = false;
    Error *local_err = NULL;
    QemuOpts *sn_opts = NULL;
    ImgConvertState state;
    bool image_opts = false;

    fmt = NULL;
    out_fmt = "raw";
    cache = "unsafe";
    src_cache = BDRV_DEFAULT_CACHE;
    out_baseimg = NULL;
    compress = 0;
    skip_create = 0;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:O:B:ce6o:s:l:S:pt:T:qn",
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
        case 'O':
            out_fmt = optarg;
            break;
        case 'B':
            out_baseimg = optarg;
            break;
        case 'c':
            compress = 1;
            break;
        case 'e':
            error_report("option -e is deprecated, please use \'-o "
                  "encryption\' instead!");
            ret = -1;
            goto fail_getopt;
        case '6':
            error_report("option -6 is deprecated, please use \'-o "
                  "compat6\' instead!");
            ret = -1;
            goto fail_getopt;
        case 'o':
            if (!is_valid_option_list(optarg)) {
                error_report("Invalid option list: %s", optarg);
                ret = -1;
                goto fail_getopt;
            }
            if (!options) {
                options = g_strdup(optarg);
            } else {
                char *old_options = options;
                options = g_strdup_printf("%s,%s", options, optarg);
                g_free(old_options);
            }
            break;
        case 's':
            snapshot_name = optarg;
            break;
        case 'l':
            if (strstart(optarg, SNAPSHOT_OPT_BASE, NULL)) {
                sn_opts = qemu_opts_parse_noisily(&internal_snapshot_opts,
                                                  optarg, false);
                if (!sn_opts) {
                    error_report("Failed in parsing snapshot param '%s'",
                                 optarg);
                    ret = -1;
                    goto fail_getopt;
                }
            } else {
                snapshot_name = optarg;
            }
            break;
        case 'S':
        {
            int64_t sval;
            char *end;
            sval = qemu_strtosz_suffix(optarg, &end, QEMU_STRTOSZ_DEFSUFFIX_B);
            if (sval < 0 || *end) {
                error_report("Invalid minimum zero buffer size for sparse output specified");
                ret = -1;
                goto fail_getopt;
            }

            min_sparse = sval / BDRV_SECTOR_SIZE;
            break;
        }
        case 'p':
            progress = 1;
            break;
        case 't':
            cache = optarg;
            break;
        case 'T':
            src_cache = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        case 'n':
            skip_create = 1;
            break;
        case OPTION_OBJECT:
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                goto fail_getopt;
            }
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        }
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        goto fail_getopt;
    }

    /* Initialize before goto out */
    if (quiet) {
        progress = 0;
    }
    qemu_progress_init(progress, 1.0);

    bs_n = argc - optind - 1;
    out_filename = bs_n >= 1 ? argv[argc - 1] : NULL;

    if (options && has_help_option(options)) {
        ret = print_block_option_help(out_filename, out_fmt);
        goto out;
    }

    if (bs_n < 1) {
        error_exit("Must specify image file name");
    }


    if (bs_n > 1 && out_baseimg) {
        error_report("-B makes no sense when concatenating multiple input "
                     "images");
        ret = -1;
        goto out;
    }

    src_flags = 0;
    ret = bdrv_parse_cache_mode(src_cache, &src_flags, &src_writethrough);
    if (ret < 0) {
        error_report("Invalid source cache option: %s", src_cache);
        goto out;
    }

    qemu_progress_print(0, 100);

    blk = g_new0(BlockBackend *, bs_n);
    bs = g_new0(BlockDriverState *, bs_n);
    bs_sectors = g_new(int64_t, bs_n);

    total_sectors = 0;
    for (bs_i = 0; bs_i < bs_n; bs_i++) {
        blk[bs_i] = img_open(image_opts, argv[optind + bs_i],
                             fmt, src_flags, src_writethrough, quiet);
        if (!blk[bs_i]) {
            ret = -1;
            goto out;
        }
        bs[bs_i] = blk_bs(blk[bs_i]);
        bs_sectors[bs_i] = blk_nb_sectors(blk[bs_i]);
        if (bs_sectors[bs_i] < 0) {
            error_report("Could not get size of %s: %s",
                         argv[optind + bs_i], strerror(-bs_sectors[bs_i]));
            ret = -1;
            goto out;
        }
        total_sectors += bs_sectors[bs_i];
    }

    if (sn_opts) {
        ret = bdrv_snapshot_load_tmp(bs[0],
                                     qemu_opt_get(sn_opts, SNAPSHOT_OPT_ID),
                                     qemu_opt_get(sn_opts, SNAPSHOT_OPT_NAME),
                                     &local_err);
    } else if (snapshot_name != NULL) {
        if (bs_n > 1) {
            error_report("No support for concatenating multiple snapshot");
            ret = -1;
            goto out;
        }

        bdrv_snapshot_load_tmp_by_id_or_name(bs[0], snapshot_name, &local_err);
    }
    if (local_err) {
        error_reportf_err(local_err, "Failed to load snapshot: ");
        ret = -1;
        goto out;
    }

    /* Find driver and parse its options */
    drv = bdrv_find_format(out_fmt);
    if (!drv) {
        error_report("Unknown file format '%s'", out_fmt);
        ret = -1;
        goto out;
    }

    proto_drv = bdrv_find_protocol(out_filename, true, &local_err);
    if (!proto_drv) {
        error_report_err(local_err);
        ret = -1;
        goto out;
    }

    if (!skip_create) {
        if (!drv->create_opts) {
            error_report("Format driver '%s' does not support image creation",
                         drv->format_name);
            ret = -1;
            goto out;
        }

        if (!proto_drv->create_opts) {
            error_report("Protocol driver '%s' does not support image creation",
                         proto_drv->format_name);
            ret = -1;
            goto out;
        }

        create_opts = qemu_opts_append(create_opts, drv->create_opts);
        create_opts = qemu_opts_append(create_opts, proto_drv->create_opts);

        opts = qemu_opts_create(create_opts, NULL, 0, &error_abort);
        if (options) {
            qemu_opts_do_parse(opts, options, NULL, &local_err);
            if (local_err) {
                error_report_err(local_err);
                ret = -1;
                goto out;
            }
        }

        qemu_opt_set_number(opts, BLOCK_OPT_SIZE, total_sectors * 512,
                            &error_abort);
        ret = add_old_style_options(out_fmt, opts, out_baseimg, NULL);
        if (ret < 0) {
            goto out;
        }
    }

    /* Get backing file name if -o backing_file was used */
    out_baseimg_param = qemu_opt_get(opts, BLOCK_OPT_BACKING_FILE);
    if (out_baseimg_param) {
        out_baseimg = out_baseimg_param;
    }

    /* Check if compression is supported */
    if (compress) {
        bool encryption =
            qemu_opt_get_bool(opts, BLOCK_OPT_ENCRYPT, false);
        const char *preallocation =
            qemu_opt_get(opts, BLOCK_OPT_PREALLOC);

        if (!drv->bdrv_write_compressed) {
            error_report("Compression not supported for this file format");
            ret = -1;
            goto out;
        }

        if (encryption) {
            error_report("Compression and encryption not supported at "
                         "the same time");
            ret = -1;
            goto out;
        }

        if (preallocation
            && strcmp(preallocation, "off"))
        {
            error_report("Compression and preallocation not supported at "
                         "the same time");
            ret = -1;
            goto out;
        }
    }

    if (!skip_create) {
        /* Create the new image */
        ret = bdrv_create(drv, out_filename, opts, &local_err);
        if (ret < 0) {
            error_reportf_err(local_err, "%s: error while converting %s: ",
                              out_filename, out_fmt);
            goto out;
        }
    }

    flags = min_sparse ? (BDRV_O_RDWR | BDRV_O_UNMAP) : BDRV_O_RDWR;
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        goto out;
    }

    /* XXX we should allow --image-opts to trigger use of
     * img_open() here, but then we have trouble with
     * the bdrv_create() call which takes different params.
     * Not critical right now, so fix can wait...
     */
    out_blk = img_open_file(out_filename, out_fmt, flags, writethrough, quiet);
    if (!out_blk) {
        ret = -1;
        goto out;
    }
    out_bs = blk_bs(out_blk);

    /* increase bufsectors from the default 4096 (2M) if opt_transfer_length
     * or discard_alignment of the out_bs is greater. Limit to 32768 (16MB)
     * as maximum. */
    bufsectors = MIN(32768,
                     MAX(bufsectors, MAX(out_bs->bl.opt_transfer_length,
                                         out_bs->bl.discard_alignment))
                    );

    if (skip_create) {
        int64_t output_sectors = blk_nb_sectors(out_blk);
        if (output_sectors < 0) {
            error_report("unable to get output image length: %s",
                         strerror(-output_sectors));
            ret = -1;
            goto out;
        } else if (output_sectors < total_sectors) {
            error_report("output file is smaller than input file");
            ret = -1;
            goto out;
        }
    }

    cluster_sectors = 0;
    ret = bdrv_get_info(out_bs, &bdi);
    if (ret < 0) {
        if (compress) {
            error_report("could not get block driver info");
            goto out;
        }
    } else {
        compress = compress || bdi.needs_compressed_writes;
        cluster_sectors = bdi.cluster_size / BDRV_SECTOR_SIZE;
    }

    state = (ImgConvertState) {
        .src                = blk,
        .src_sectors        = bs_sectors,
        .src_num            = bs_n,
        .total_sectors      = total_sectors,
        .target             = out_blk,
        .compressed         = compress,
        .target_has_backing = (bool) out_baseimg,
        .min_sparse         = min_sparse,
        .cluster_sectors    = cluster_sectors,
        .buf_sectors        = bufsectors,
    };
    ret = convert_do_copy(&state);

out:
    if (!ret) {
        qemu_progress_print(100, 0);
    }
    qemu_progress_end();
    qemu_opts_del(opts);
    qemu_opts_free(create_opts);
    qemu_opts_del(sn_opts);
    blk_unref(out_blk);
    g_free(bs);
    if (blk) {
        for (bs_i = 0; bs_i < bs_n; bs_i++) {
            blk_unref(blk[bs_i]);
        }
        g_free(blk);
    }
    g_free(bs_sectors);
fail_getopt:
    g_free(options);

    if (ret) {
        return 1;
    }
    return 0;
}


static void dump_snapshots(BlockDriverState *bs)
{
    QEMUSnapshotInfo *sn_tab, *sn;
    int nb_sns, i;

    nb_sns = bdrv_snapshot_list(bs, &sn_tab);
    if (nb_sns <= 0)
        return;
    printf("Snapshot list:\n");
    bdrv_snapshot_dump(fprintf, stdout, NULL);
    printf("\n");
    for(i = 0; i < nb_sns; i++) {
        sn = &sn_tab[i];
        bdrv_snapshot_dump(fprintf, stdout, sn);
        printf("\n");
    }
    g_free(sn_tab);
}

static void dump_json_image_info_list(ImageInfoList *list)
{
    Error *local_err = NULL;
    QString *str;
    QmpOutputVisitor *ov = qmp_output_visitor_new();
    QObject *obj;
    visit_type_ImageInfoList(qmp_output_get_visitor(ov), NULL, &list,
                             &local_err);
    obj = qmp_output_get_qobject(ov);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    printf("%s\n", qstring_get_str(str));
    qobject_decref(obj);
    qmp_output_visitor_cleanup(ov);
    QDECREF(str);
}

static void dump_json_image_info(ImageInfo *info)
{
    Error *local_err = NULL;
    QString *str;
    QmpOutputVisitor *ov = qmp_output_visitor_new();
    QObject *obj;
    visit_type_ImageInfo(qmp_output_get_visitor(ov), NULL, &info, &local_err);
    obj = qmp_output_get_qobject(ov);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    printf("%s\n", qstring_get_str(str));
    qobject_decref(obj);
    qmp_output_visitor_cleanup(ov);
    QDECREF(str);
}

static void dump_human_image_info_list(ImageInfoList *list)
{
    ImageInfoList *elem;
    bool delim = false;

    for (elem = list; elem; elem = elem->next) {
        if (delim) {
            printf("\n");
        }
        delim = true;

        bdrv_image_info_dump(fprintf, stdout, elem->value);
    }
}

static gboolean str_equal_func(gconstpointer a, gconstpointer b)
{
    return strcmp(a, b) == 0;
}

/**
 * Open an image file chain and return an ImageInfoList
 *
 * @filename: topmost image filename
 * @fmt: topmost image format (may be NULL to autodetect)
 * @chain: true  - enumerate entire backing file chain
 *         false - only topmost image file
 *
 * Returns a list of ImageInfo objects or NULL if there was an error opening an
 * image file.  If there was an error a message will have been printed to
 * stderr.
 */
static ImageInfoList *collect_image_info_list(bool image_opts,
                                              const char *filename,
                                              const char *fmt,
                                              bool chain)
{
    ImageInfoList *head = NULL;
    ImageInfoList **last = &head;
    GHashTable *filenames;
    Error *err = NULL;

    filenames = g_hash_table_new_full(g_str_hash, str_equal_func, NULL, NULL);

    while (filename) {
        BlockBackend *blk;
        BlockDriverState *bs;
        ImageInfo *info;
        ImageInfoList *elem;

        if (g_hash_table_lookup_extended(filenames, filename, NULL, NULL)) {
            error_report("Backing file '%s' creates an infinite loop.",
                         filename);
            goto err;
        }
        g_hash_table_insert(filenames, (gpointer)filename, NULL);

        blk = img_open(image_opts, filename, fmt,
                       BDRV_O_NO_BACKING | BDRV_O_NO_IO, false, false);
        if (!blk) {
            goto err;
        }
        bs = blk_bs(blk);

        bdrv_query_image_info(bs, &info, &err);
        if (err) {
            error_report_err(err);
            blk_unref(blk);
            goto err;
        }

        elem = g_new0(ImageInfoList, 1);
        elem->value = info;
        *last = elem;
        last = &elem->next;

        blk_unref(blk);

        filename = fmt = NULL;
        if (chain) {
            if (info->has_full_backing_filename) {
                filename = info->full_backing_filename;
            } else if (info->has_backing_filename) {
                error_report("Could not determine absolute backing filename,"
                             " but backing filename '%s' present",
                             info->backing_filename);
                goto err;
            }
            if (info->has_backing_filename_format) {
                fmt = info->backing_filename_format;
            }
        }
    }
    g_hash_table_destroy(filenames);
    return head;

err:
    qapi_free_ImageInfoList(head);
    g_hash_table_destroy(filenames);
    return NULL;
}

static int img_info(int argc, char **argv)
{
    int c;
    OutputFormat output_format = OFORMAT_HUMAN;
    bool chain = false;
    const char *filename, *fmt, *output;
    ImageInfoList *list;
    bool image_opts = false;

    fmt = NULL;
    output = NULL;
    for(;;) {
        int option_index = 0;
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {"backing-chain", no_argument, 0, OPTION_BACKING_CHAIN},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "f:h",
                        long_options, &option_index);
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
        case OPTION_OUTPUT:
            output = optarg;
            break;
        case OPTION_BACKING_CHAIN:
            chain = true;
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

    if (output && !strcmp(output, "json")) {
        output_format = OFORMAT_JSON;
    } else if (output && !strcmp(output, "human")) {
        output_format = OFORMAT_HUMAN;
    } else if (output) {
        error_report("--output must be used with human or json as argument.");
        return 1;
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        return 1;
    }

    list = collect_image_info_list(image_opts, filename, fmt, chain);
    if (!list) {
        return 1;
    }

    switch (output_format) {
    case OFORMAT_HUMAN:
        dump_human_image_info_list(list);
        break;
    case OFORMAT_JSON:
        if (chain) {
            dump_json_image_info_list(list);
        } else {
            dump_json_image_info(list->value);
        }
        break;
    }

    qapi_free_ImageInfoList(list);
    return 0;
}

static void dump_map_entry(OutputFormat output_format, MapEntry *e,
                           MapEntry *next)
{
    switch (output_format) {
    case OFORMAT_HUMAN:
        if (e->data && !e->has_offset) {
            error_report("File contains external, encrypted or compressed clusters.");
            exit(1);
        }
        if (e->data && !e->zero) {
            printf("%#-16"PRIx64"%#-16"PRIx64"%#-16"PRIx64"%s\n",
                   e->start, e->length,
                   e->has_offset ? e->offset : 0,
                   e->has_filename ? e->filename : "");
        }
        /* This format ignores the distinction between 0, ZERO and ZERO|DATA.
         * Modify the flags here to allow more coalescing.
         */
        if (next && (!next->data || next->zero)) {
            next->data = false;
            next->zero = true;
        }
        break;
    case OFORMAT_JSON:
        printf("%s{ \"start\": %"PRId64", \"length\": %"PRId64","
               " \"depth\": %"PRId64", \"zero\": %s, \"data\": %s",
               (e->start == 0 ? "[" : ",\n"),
               e->start, e->length, e->depth,
               e->zero ? "true" : "false",
               e->data ? "true" : "false");
        if (e->has_offset) {
            printf(", \"offset\": %"PRId64"", e->offset);
        }
        putchar('}');

        if (!next) {
            printf("]\n");
        }
        break;
    }
}

static int get_block_status(BlockDriverState *bs, int64_t sector_num,
                            int nb_sectors, MapEntry *e)
{
    int64_t ret;
    int depth;
    BlockDriverState *file;
    bool has_offset;

    /* As an optimization, we could cache the current range of unallocated
     * clusters in each file of the chain, and avoid querying the same
     * range repeatedly.
     */

    depth = 0;
    for (;;) {
        ret = bdrv_get_block_status(bs, sector_num, nb_sectors, &nb_sectors,
                                    &file);
        if (ret < 0) {
            return ret;
        }
        assert(nb_sectors);
        if (ret & (BDRV_BLOCK_ZERO|BDRV_BLOCK_DATA)) {
            break;
        }
        bs = backing_bs(bs);
        if (bs == NULL) {
            ret = 0;
            break;
        }

        depth++;
    }

    has_offset = !!(ret & BDRV_BLOCK_OFFSET_VALID);

    *e = (MapEntry) {
        .start = sector_num * BDRV_SECTOR_SIZE,
        .length = nb_sectors * BDRV_SECTOR_SIZE,
        .data = !!(ret & BDRV_BLOCK_DATA),
        .zero = !!(ret & BDRV_BLOCK_ZERO),
        .offset = ret & BDRV_BLOCK_OFFSET_MASK,
        .has_offset = has_offset,
        .depth = depth,
        .has_filename = file && has_offset,
        .filename = file && has_offset ? file->filename : NULL,
    };

    return 0;
}

static inline bool entry_mergeable(const MapEntry *curr, const MapEntry *next)
{
    if (curr->length == 0) {
        return false;
    }
    if (curr->zero != next->zero ||
        curr->data != next->data ||
        curr->depth != next->depth ||
        curr->has_filename != next->has_filename ||
        curr->has_offset != next->has_offset) {
        return false;
    }
    if (curr->has_filename && strcmp(curr->filename, next->filename)) {
        return false;
    }
    if (curr->has_offset && curr->offset + curr->length != next->offset) {
        return false;
    }
    return true;
}

static int img_map(int argc, char **argv)
{
    int c;
    OutputFormat output_format = OFORMAT_HUMAN;
    BlockBackend *blk;
    BlockDriverState *bs;
    const char *filename, *fmt, *output;
    int64_t length;
    MapEntry curr = { .length = 0 }, next;
    int ret = 0;
    bool image_opts = false;

    fmt = NULL;
    output = NULL;
    for (;;) {
        int option_index = 0;
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "f:h",
                        long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
        case '?':
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_OUTPUT:
            output = optarg;
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
    filename = argv[optind];

    if (output && !strcmp(output, "json")) {
        output_format = OFORMAT_JSON;
    } else if (output && !strcmp(output, "human")) {
        output_format = OFORMAT_HUMAN;
    } else if (output) {
        error_report("--output must be used with human or json as argument.");
        return 1;
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        return 1;
    }

    blk = img_open(image_opts, filename, fmt, 0, false, false);
    if (!blk) {
        return 1;
    }
    bs = blk_bs(blk);

    if (output_format == OFORMAT_HUMAN) {
        printf("%-16s%-16s%-16s%s\n", "Offset", "Length", "Mapped to", "File");
    }

    length = blk_getlength(blk);
    while (curr.start + curr.length < length) {
        int64_t nsectors_left;
        int64_t sector_num;
        int n;

        sector_num = (curr.start + curr.length) >> BDRV_SECTOR_BITS;

        /* Probe up to 1 GiB at a time.  */
        nsectors_left = DIV_ROUND_UP(length, BDRV_SECTOR_SIZE) - sector_num;
        n = MIN(1 << (30 - BDRV_SECTOR_BITS), nsectors_left);
        ret = get_block_status(bs, sector_num, n, &next);

        if (ret < 0) {
            error_report("Could not read file metadata: %s", strerror(-ret));
            goto out;
        }

        if (entry_mergeable(&curr, &next)) {
            curr.length += next.length;
            continue;
        }

        if (curr.length > 0) {
            dump_map_entry(output_format, &curr, &next);
        }
        curr = next;
    }

    dump_map_entry(output_format, &curr, NULL);

out:
    blk_unref(blk);
    return ret < 0;
}

#define SNAPSHOT_LIST   1
#define SNAPSHOT_CREATE 2
#define SNAPSHOT_APPLY  3
#define SNAPSHOT_DELETE 4

static int img_snapshot(int argc, char **argv)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    QEMUSnapshotInfo sn;
    char *filename, *snapshot_name = NULL;
    int c, ret = 0, bdrv_oflags;
    int action = 0;
    qemu_timeval tv;
    bool quiet = false;
    Error *err = NULL;
    bool image_opts = false;

    bdrv_oflags = BDRV_O_RDWR;
    /* Parse commandline parameters */
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "la:c:d:hq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            return 0;
        case 'l':
            if (action) {
                error_exit("Cannot mix '-l', '-a', '-c', '-d'");
                return 0;
            }
            action = SNAPSHOT_LIST;
            bdrv_oflags &= ~BDRV_O_RDWR; /* no need for RW */
            break;
        case 'a':
            if (action) {
                error_exit("Cannot mix '-l', '-a', '-c', '-d'");
                return 0;
            }
            action = SNAPSHOT_APPLY;
            snapshot_name = optarg;
            break;
        case 'c':
            if (action) {
                error_exit("Cannot mix '-l', '-a', '-c', '-d'");
                return 0;
            }
            action = SNAPSHOT_CREATE;
            snapshot_name = optarg;
            break;
        case 'd':
            if (action) {
                error_exit("Cannot mix '-l', '-a', '-c', '-d'");
                return 0;
            }
            action = SNAPSHOT_DELETE;
            snapshot_name = optarg;
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

    /* Open the image */
    blk = img_open(image_opts, filename, NULL, bdrv_oflags, false, quiet);
    if (!blk) {
        return 1;
    }
    bs = blk_bs(blk);

    /* Perform the requested action */
    switch(action) {
    case SNAPSHOT_LIST:
        dump_snapshots(bs);
        break;

    case SNAPSHOT_CREATE:
        memset(&sn, 0, sizeof(sn));
        pstrcpy(sn.name, sizeof(sn.name), snapshot_name);

        qemu_gettimeofday(&tv);
        sn.date_sec = tv.tv_sec;
        sn.date_nsec = tv.tv_usec * 1000;

        ret = bdrv_snapshot_create(bs, &sn);
        if (ret) {
            error_report("Could not create snapshot '%s': %d (%s)",
                snapshot_name, ret, strerror(-ret));
        }
        break;

    case SNAPSHOT_APPLY:
        ret = bdrv_snapshot_goto(bs, snapshot_name);
        if (ret) {
            error_report("Could not apply snapshot '%s': %d (%s)",
                snapshot_name, ret, strerror(-ret));
        }
        break;

    case SNAPSHOT_DELETE:
        bdrv_snapshot_delete_by_id_or_name(bs, snapshot_name, &err);
        if (err) {
            error_reportf_err(err, "Could not delete snapshot '%s': ",
                              snapshot_name);
            ret = 1;
        }
        break;
    }

    /* Cleanup */
    blk_unref(blk);
    if (ret) {
        return 1;
    }
    return 0;
}

static int img_rebase(int argc, char **argv)
{
    BlockBackend *blk = NULL, *blk_old_backing = NULL, *blk_new_backing = NULL;
    uint8_t *buf_old = NULL;
    uint8_t *buf_new = NULL;
    BlockDriverState *bs = NULL;
    char *filename;
    const char *fmt, *cache, *src_cache, *out_basefmt, *out_baseimg;
    int c, flags, src_flags, ret;
    bool writethrough, src_writethrough;
    int unsafe = 0;
    int progress = 0;
    bool quiet = false;
    Error *local_err = NULL;
    bool image_opts = false;

    /* Parse commandline parameters */
    fmt = NULL;
    cache = BDRV_DEFAULT_CACHE;
    src_cache = BDRV_DEFAULT_CACHE;
    out_baseimg = NULL;
    out_basefmt = NULL;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:F:b:upt:T:q",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            return 0;
        case 'f':
            fmt = optarg;
            break;
        case 'F':
            out_basefmt = optarg;
            break;
        case 'b':
            out_baseimg = optarg;
            break;
        case 'u':
            unsafe = 1;
            break;
        case 'p':
            progress = 1;
            break;
        case 't':
            cache = optarg;
            break;
        case 'T':
            src_cache = optarg;
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

    if (quiet) {
        progress = 0;
    }

    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    if (!unsafe && !out_baseimg) {
        error_exit("Must specify backing file (-b) or use unsafe mode (-u)");
    }
    filename = argv[optind++];

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        return 1;
    }

    qemu_progress_init(progress, 2.0);
    qemu_progress_print(0, 100);

    flags = BDRV_O_RDWR | (unsafe ? BDRV_O_NO_BACKING : 0);
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        goto out;
    }

    src_flags = 0;
    ret = bdrv_parse_cache_mode(src_cache, &src_flags, &src_writethrough);
    if (ret < 0) {
        error_report("Invalid source cache option: %s", src_cache);
        goto out;
    }

    /* The source files are opened read-only, don't care about WCE */
    assert((src_flags & BDRV_O_RDWR) == 0);
    (void) src_writethrough;

    /*
     * Open the images.
     *
     * Ignore the old backing file for unsafe rebase in case we want to correct
     * the reference to a renamed or moved backing file.
     */
    blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet);
    if (!blk) {
        ret = -1;
        goto out;
    }
    bs = blk_bs(blk);

    if (out_basefmt != NULL) {
        if (bdrv_find_format(out_basefmt) == NULL) {
            error_report("Invalid format name: '%s'", out_basefmt);
            ret = -1;
            goto out;
        }
    }

    /* For safe rebasing we need to compare old and new backing file */
    if (!unsafe) {
        char backing_name[PATH_MAX];
        QDict *options = NULL;

        if (bs->backing_format[0] != '\0') {
            options = qdict_new();
            qdict_put(options, "driver", qstring_from_str(bs->backing_format));
        }

        bdrv_get_backing_filename(bs, backing_name, sizeof(backing_name));
        blk_old_backing = blk_new_open(backing_name, NULL,
                                       options, src_flags, &local_err);
        if (!blk_old_backing) {
            error_reportf_err(local_err,
                              "Could not open old backing file '%s': ",
                              backing_name);
            goto out;
        }

        if (out_baseimg[0]) {
            if (out_basefmt) {
                options = qdict_new();
                qdict_put(options, "driver", qstring_from_str(out_basefmt));
            } else {
                options = NULL;
            }

            blk_new_backing = blk_new_open(out_baseimg, NULL,
                                           options, src_flags, &local_err);
            if (!blk_new_backing) {
                error_reportf_err(local_err,
                                  "Could not open new backing file '%s': ",
                                  out_baseimg);
                goto out;
            }
        }
    }

    /*
     * Check each unallocated cluster in the COW file. If it is unallocated,
     * accesses go to the backing file. We must therefore compare this cluster
     * in the old and new backing file, and if they differ we need to copy it
     * from the old backing file into the COW file.
     *
     * If qemu-img crashes during this step, no harm is done. The content of
     * the image is the same as the original one at any time.
     */
    if (!unsafe) {
        int64_t num_sectors;
        int64_t old_backing_num_sectors;
        int64_t new_backing_num_sectors = 0;
        uint64_t sector;
        int n;
        float local_progress = 0;

        buf_old = blk_blockalign(blk, IO_BUF_SIZE);
        buf_new = blk_blockalign(blk, IO_BUF_SIZE);

        num_sectors = blk_nb_sectors(blk);
        if (num_sectors < 0) {
            error_report("Could not get size of '%s': %s",
                         filename, strerror(-num_sectors));
            ret = -1;
            goto out;
        }
        old_backing_num_sectors = blk_nb_sectors(blk_old_backing);
        if (old_backing_num_sectors < 0) {
            char backing_name[PATH_MAX];

            bdrv_get_backing_filename(bs, backing_name, sizeof(backing_name));
            error_report("Could not get size of '%s': %s",
                         backing_name, strerror(-old_backing_num_sectors));
            ret = -1;
            goto out;
        }
        if (blk_new_backing) {
            new_backing_num_sectors = blk_nb_sectors(blk_new_backing);
            if (new_backing_num_sectors < 0) {
                error_report("Could not get size of '%s': %s",
                             out_baseimg, strerror(-new_backing_num_sectors));
                ret = -1;
                goto out;
            }
        }

        if (num_sectors != 0) {
            local_progress = (float)100 /
                (num_sectors / MIN(num_sectors, IO_BUF_SIZE / 512));
        }

        for (sector = 0; sector < num_sectors; sector += n) {

            /* How many sectors can we handle with the next read? */
            if (sector + (IO_BUF_SIZE / 512) <= num_sectors) {
                n = (IO_BUF_SIZE / 512);
            } else {
                n = num_sectors - sector;
            }

            /* If the cluster is allocated, we don't need to take action */
            ret = bdrv_is_allocated(bs, sector, n, &n);
            if (ret < 0) {
                error_report("error while reading image metadata: %s",
                             strerror(-ret));
                goto out;
            }
            if (ret) {
                continue;
            }

            /*
             * Read old and new backing file and take into consideration that
             * backing files may be smaller than the COW image.
             */
            if (sector >= old_backing_num_sectors) {
                memset(buf_old, 0, n * BDRV_SECTOR_SIZE);
            } else {
                if (sector + n > old_backing_num_sectors) {
                    n = old_backing_num_sectors - sector;
                }

                ret = blk_pread(blk_old_backing, sector << BDRV_SECTOR_BITS,
                                buf_old, n << BDRV_SECTOR_BITS);
                if (ret < 0) {
                    error_report("error while reading from old backing file");
                    goto out;
                }
            }

            if (sector >= new_backing_num_sectors || !blk_new_backing) {
                memset(buf_new, 0, n * BDRV_SECTOR_SIZE);
            } else {
                if (sector + n > new_backing_num_sectors) {
                    n = new_backing_num_sectors - sector;
                }

                ret = blk_pread(blk_new_backing, sector << BDRV_SECTOR_BITS,
                                buf_new, n << BDRV_SECTOR_BITS);
                if (ret < 0) {
                    error_report("error while reading from new backing file");
                    goto out;
                }
            }

            /* If they differ, we need to write to the COW file */
            uint64_t written = 0;

            while (written < n) {
                int pnum;

                if (compare_sectors(buf_old + written * 512,
                    buf_new + written * 512, n - written, &pnum))
                {
                    ret = blk_pwrite(blk,
                                     (sector + written) << BDRV_SECTOR_BITS,
                                     buf_old + written * 512,
                                     pnum << BDRV_SECTOR_BITS, 0);
                    if (ret < 0) {
                        error_report("Error while writing to COW image: %s",
                            strerror(-ret));
                        goto out;
                    }
                }

                written += pnum;
            }
            qemu_progress_print(local_progress, 100);
        }
    }

    /*
     * Change the backing file. All clusters that are different from the old
     * backing file are overwritten in the COW file now, so the visible content
     * doesn't change when we switch the backing file.
     */
    if (out_baseimg && *out_baseimg) {
        ret = bdrv_change_backing_file(bs, out_baseimg, out_basefmt);
    } else {
        ret = bdrv_change_backing_file(bs, NULL, NULL);
    }

    if (ret == -ENOSPC) {
        error_report("Could not change the backing file to '%s': No "
                     "space left in the file header", out_baseimg);
    } else if (ret < 0) {
        error_report("Could not change the backing file to '%s': %s",
            out_baseimg, strerror(-ret));
    }

    qemu_progress_print(100, 0);
    /*
     * TODO At this point it is possible to check if any clusters that are
     * allocated in the COW file are the same in the backing file. If so, they
     * could be dropped from the COW file. Don't do this before switching the
     * backing file, in case of a crash this would lead to corruption.
     */
out:
    qemu_progress_end();
    /* Cleanup */
    if (!unsafe) {
        blk_unref(blk_old_backing);
        blk_unref(blk_new_backing);
    }
    qemu_vfree(buf_old);
    qemu_vfree(buf_new);

    blk_unref(blk);
    if (ret) {
        return 1;
    }
    return 0;
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
        error_report("Error resizing image (%d)", -ret);
        break;
    }
out:
    blk_unref(blk);
    if (ret) {
        return 1;
    }
    return 0;
}

static void amend_status_cb(BlockDriverState *bs,
                            int64_t offset, int64_t total_work_size,
                            void *opaque)
{
    qemu_progress_print(100.f * offset / total_work_size, 0);
}

static int img_amend(int argc, char **argv)
{
    Error *err = NULL;
    int c, ret = 0;
    char *options = NULL;
    QemuOptsList *create_opts = NULL;
    QemuOpts *opts = NULL;
    const char *fmt = NULL, *filename, *cache;
    int flags;
    bool writethrough;
    bool quiet = false, progress = false;
    BlockBackend *blk = NULL;
    BlockDriverState *bs = NULL;
    bool image_opts = false;

    cache = BDRV_DEFAULT_CACHE;
    for (;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "ho:f:t:pq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
            case 'h':
            case '?':
                help();
                break;
            case 'o':
                if (!is_valid_option_list(optarg)) {
                    error_report("Invalid option list: %s", optarg);
                    ret = -1;
                    goto out_no_progress;
                }
                if (!options) {
                    options = g_strdup(optarg);
                } else {
                    char *old_options = options;
                    options = g_strdup_printf("%s,%s", options, optarg);
                    g_free(old_options);
                }
                break;
            case 'f':
                fmt = optarg;
                break;
            case 't':
                cache = optarg;
                break;
            case 'p':
                progress = true;
                break;
            case 'q':
                quiet = true;
                break;
            case OPTION_OBJECT:
                opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                               optarg, true);
                if (!opts) {
                    ret = -1;
                    goto out_no_progress;
                }
                break;
            case OPTION_IMAGE_OPTS:
                image_opts = true;
                break;
        }
    }

    if (!options) {
        error_exit("Must specify options (-o)");
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        ret = -1;
        goto out_no_progress;
    }

    if (quiet) {
        progress = false;
    }
    qemu_progress_init(progress, 1.0);

    filename = (optind == argc - 1) ? argv[argc - 1] : NULL;
    if (fmt && has_help_option(options)) {
        /* If a format is explicitly specified (and possibly no filename is
         * given), print option help here */
        ret = print_block_option_help(filename, fmt);
        goto out;
    }

    if (optind != argc - 1) {
        error_report("Expecting one image file name");
        ret = -1;
        goto out;
    }

    flags = BDRV_O_RDWR;
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        goto out;
    }

    blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet);
    if (!blk) {
        ret = -1;
        goto out;
    }
    bs = blk_bs(blk);

    fmt = bs->drv->format_name;

    if (has_help_option(options)) {
        /* If the format was auto-detected, print option help here */
        ret = print_block_option_help(filename, fmt);
        goto out;
    }

    if (!bs->drv->create_opts) {
        error_report("Format driver '%s' does not support any options to amend",
                     fmt);
        ret = -1;
        goto out;
    }

    create_opts = qemu_opts_append(create_opts, bs->drv->create_opts);
    opts = qemu_opts_create(create_opts, NULL, 0, &error_abort);
    if (options) {
        qemu_opts_do_parse(opts, options, NULL, &err);
        if (err) {
            error_report_err(err);
            ret = -1;
            goto out;
        }
    }

    /* In case the driver does not call amend_status_cb() */
    qemu_progress_print(0.f, 0);
    ret = bdrv_amend_options(bs, opts, &amend_status_cb, NULL);
    qemu_progress_print(100.f, 0);
    if (ret < 0) {
        error_report("Error while amending options: %s", strerror(-ret));
        goto out;
    }

out:
    qemu_progress_end();

out_no_progress:
    blk_unref(blk);
    qemu_opts_del(opts);
    qemu_opts_free(create_opts);
    g_free(options);

    if (ret) {
        return 1;
    }
    return 0;
}

static const img_cmd_t img_cmds[] = {
#define DEF(option, callback, arg_string)        \
    { option, callback },
#include "qemu-img-cmds.h"
#undef DEF
#undef GEN_DOCS
    { NULL, NULL, },
};

int main(int argc, char **argv)
{
    const img_cmd_t *cmd;
    const char *cmdname;
    Error *local_error = NULL;
    int c;
    static const struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

#ifdef CONFIG_POSIX
    signal(SIGPIPE, SIG_IGN);
#endif

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
    cmdname = argv[1];

    qemu_add_opts(&qemu_object_opts);
    qemu_add_opts(&qemu_source_opts);

    /* find the command */
    for (cmd = img_cmds; cmd->name != NULL; cmd++) {
        if (!strcmp(cmdname, cmd->name)) {
            return cmd->handler(argc - 1, argv + 1);
        }
    }

    c = getopt_long(argc, argv, "h", long_options, NULL);

    if (c == 'h') {
        help();
    }
    if (c == 'v') {
        printf(QEMU_IMG_VERSION);
        return 0;
    }

    /* not found */
    error_exit("Command not found: %s", cmdname);
}
