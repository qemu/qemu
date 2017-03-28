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

typedef enum OutputFormat {
    OFORMAT_JSON,
    OFORMAT_HUMAN,
} OutputFormat;

#define BDRV_DEFAULT_CACHE "writeback"

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

static void QEMU_NORETURN help(void)
{
    const char *help_msg =
            "usage: qcow2-img command [command options]\n"
            "create [-o options] {-t <template file> -l <layer UUID> -s <size>} filename\n"
            "resize filename [+ | -]size\n"
            "info filename\n";
    printf("%s", help_msg);
    exit(EXIT_SUCCESS);
}

static char *generate_encoded_backingfile(const char* template, const char* layer_uuid)
{
    char *backing_string = malloc(PATH_MAX*2);
    snprintf(backing_string, PATH_MAX*2, "qcow2://%s?layer=%s", template, layer_uuid);
    return backing_string;
}

static int get_encoded_backingfile(char* name, char* template, char* layer_uuid)
{
    return sscanf(name, "qcow2://%[^?]?layer=%s", template, layer_uuid);
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
            BDRV_O_NO_BACKING | BDRV_O_RDWR, false, quiet);
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

static gboolean str_equal_func(gconstpointer a, gconstpointer b)
{
    return strcmp(a, b) == 0;
}

static void dump_snapshots(QEMUSnapshotInfo *sn_tab, int nb_sns)
{
    if (!sn_tab) {
        return;
    }
    QEMUSnapshotInfo *sn;
    int i;
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
    QString *str;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_ImageInfoList(v, NULL, &list, &error_abort);
    visit_complete(v, &obj);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    printf("%s\n", qstring_get_str(str));
    qobject_decref(obj);
    visit_free(v);
    QDECREF(str);
}

static void dump_json_image_info(ImageInfo *info)
{
    QString *str;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_ImageInfo(v, NULL, &info, &error_abort);
    visit_complete(v, &obj);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    printf("%s\n", qstring_get_str(str));
    qobject_decref(obj);
    visit_free(v);
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

static ImageInfoList *collect_image_info_list(bool image_opts,
                                              const char *filename,
                                              const char *fmt,
                                              bool chain,
                                              QEMUSnapshotInfo **sn_tab,
                                              int *nb_sns)
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
        *nb_sns = bdrv_snapshot_list(bs, sn_tab);

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

    QEMUSnapshotInfo *sn_tab = NULL;
    int nb_sns = 0;
    list = collect_image_info_list(image_opts, filename, fmt, chain, &sn_tab, &nb_sns);
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
    dump_snapshots(sn_tab, nb_sns);
    qapi_free_ImageInfoList(list);
    return 0;
}

typedef struct CommonBlockJobCBInfo {
    BlockDriverState *bs;
    Error **errp;
} CommonBlockJobCBInfo;

static int get_last_snapshot_uuid(char* uuid, ImageInfo *info)
{
    if (!info->snapshots) {
        return 3;
    }
    SnapshotInfoList *cur = info->snapshots;
    while(cur->next){
        cur = cur->next;
    }
    char p_uuid[PATH_MAX], commit_msg[PATH_MAX];
    return sscanf(cur->value->name, "%[^,],%[^,],%s", uuid, p_uuid, commit_msg);
}

static int search_snapshot_by_name(char* uuid, int* index, char* puuid, char* msg, ImageInfo *info)
{
    if (!info->snapshots) {
        return -1;
    }

    int count = 0;
    SnapshotInfoList *cur = info->snapshots;
    do{
        char tmp_uuid[PATH_MAX] = "", tmp_puuid[PATH_MAX] = "", commit_msg[PATH_MAX] = "";
        int ret = sscanf(cur->value->name, "%[^,],%[^,],%s", tmp_uuid, tmp_puuid, commit_msg);
        if(ret != 3){
            return -1;
        }
        if (0 == strcmp(uuid, tmp_uuid)) {
            if(puuid){
                strcpy(puuid, tmp_puuid);
            }
            if(msg){
                strcpy(msg, commit_msg);
            }
            *index = count;
            return 0;
        }
        count++;
        cur = cur->next;
    }while(cur);
    return -1;
}

static int generate_enforced_snapshotname(char* buf, char* p_uuid, char* uuid, char* commit_msg)
{
    return sprintf(buf, "%s,%s,%s", uuid, p_uuid, commit_msg);
}

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

    aio_context_acquire(aio_context);
    do {
        aio_poll(aio_context, true);
        qemu_progress_print(job->len ?
                            ((float)job->offset / job->len * 100.f) : 0.0f, 0);
    } while (!job->ready);

    block_job_complete_sync(job, errp);
    aio_context_release(aio_context);

    /* A block job may finish instantaneously without publishing any progress,
     * so just signal completion here */
    qemu_progress_print(100.f, 0);
}

static int img_commit(int argc, char **argv)
{
    int c, ret, flags;
    const char *filename, *fmt, *cache, *base;
    BlockBackend *blk, *base_blk = NULL;
    BlockDriverState *bs, *base_bs;
    bool progress = false, quiet = false, drop = false;
    bool writethrough;
    char *commit_msg = NULL, *snapshot_uuid = NULL;
    Error *local_err = NULL;
    CommonBlockJobCBInfo cbi;
    bool image_opts = false;
    AioContext *aio_context;

    fmt = "qcow2";
    cache = BDRV_DEFAULT_CACHE;
    base = NULL;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "ht:b:dpq",
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
        case 'm':

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

    flags = BDRV_O_RDWR | BDRV_O_UNMAP | BDRV_O_NO_BACKING;
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        return 1;
    }

    if (commit_msg == NULL) {
        error_report("commit_msg can't be none");
        return 1;
    }

    if (snapshot_uuid == NULL) {
        error_report("snapshot_uuid can't be none");
        return 1;
    }

    blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet);
    if (!blk) {
        return 1;
    }
    bs = blk_bs(blk);
    ImageInfo *info, *base_info;
    bdrv_query_image_info(bs, &info, &local_err);
    if (local_err) {
        error_report_err(local_err);
        goto done;
    }

    if (!info->has_backing_filename) {
        error_setg_errno(&local_err, -1, "No backing file found, can't commit");
        goto done;
    }

    char tmplate_name[PATH_MAX] = "", layername[PATH_MAX] = "";
    ret = get_encoded_backingfile(info->backing_filename, tmplate_name, layername);
    if (ret != 2) {
        error_setg_errno(&local_err, -1, "error get get_encoded_backingfile, can't commit");
        goto done;
    }

    base_blk = img_open(image_opts, tmplate_name, fmt, flags, writethrough, quiet);
    if (!base_blk) {
        error_setg_errno(&local_err, -1, "error open backing file %s, can't commit", tmplate_name);
        goto done;
    }
    base_bs = blk_bs(base_blk);

    bdrv_query_image_info(base_bs, &base_info, &local_err);
    if (local_err) {
        error_setg_errno(&local_err, -1, "error get image info from backing file, can't commit");
        goto done;
    }

    char last_snapshot_uuid[PATH_MAX] = "";
    ret = get_last_snapshot_uuid(last_snapshot_uuid, base_info);
    if (ret != 3) {
        error_setg_errno(&local_err, -1, "error get last from backing file, can't commit");
        goto done;
    }

    if (strcmp(layername, last_snapshot_uuid) != 0) {
        error_setg_errno(&local_err, -1, "error backing file is not the last uuid "
                         "(%s) (%s) , can't commit", last_snapshot_uuid, layername);
        goto done;
    }

    cbi = (CommonBlockJobCBInfo){
        .errp = &local_err,
        .bs   = bs,
    };

    qemu_progress_init(progress, 1.f);
    qemu_progress_print(0.f, 100);

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);
    commit_active_start("commit", bs, base_bs, BLOCK_JOB_DEFAULT, 0,
                        BLOCKDEV_ON_ERROR_REPORT, common_block_job_cb, &cbi,
                        &local_err, false);
    aio_context_release(aio_context);
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

    char enforced_snapshot_name[PATH_MAX*2];
    generate_enforced_snapshotname(enforced_snapshot_name, layername, snapshot_uuid, commit_msg);
    QEMUSnapshotInfo sn;
    qemu_timeval tv;
    memset(&sn, 0, sizeof(sn));
    pstrcpy(sn.name, sizeof(sn.name), enforced_snapshot_name);
    qemu_gettimeofday(&tv);
    sn.date_sec = tv.tv_sec;
    sn.date_nsec = tv.tv_usec * 1000;

    ret = bdrv_snapshot_create(bs, &sn);
    if (ret) {
        error_report("Could not create snapshot '%s': %d (%s)",
                enforced_snapshot_name, ret, strerror(-ret));
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
    blk_unref(base_blk);

    if (local_err) {
        error_report_err(local_err);
        return 1;
    }

    qprintf(quiet, "Image committed.\n");
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
    if (template_filename) {
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

static int img_layer_dump(int argc, char **argv)
{
    int c, flags;
    char *options = NULL;
    BlockDriverState *bs, *base_bs;
    BlockBackend *blk, *base_blk = NULL;
    const char *filename = NULL;
    const char *fmt = "qcow2";
    char *template_filename = NULL;
    bool progress = false, quiet = false, drop = false;
    bool image_opts = false;
    Error *local_err = NULL;
    bool writethrough = false;
    char *layer_uuid = NULL;
    for(;;) {
            static const struct option long_options[] = {
                {"help", no_argument, 0, 'h'},
                {"object", required_argument, 0, OPTION_OBJECT},
                {0, 0, 0, 0}
            };
            c = getopt_long(argc, argv, "t:l:h",
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
            case OPTION_IMAGE_OPTS:
                image_opts = true;
                break;
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

    if (template_filename == NULL) {
        error_report("template_filename can't be none");
        return 1;
    }

    if (layer_uuid == NULL) {
        error_report("layer_uuid can't be none");
        return 1;
    }

    if (optind != argc) {
        error_exit("Unexpected argument: %s", argv[optind]);
    }

    base_blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet);
    if (!base_blk) {
        return 1;
    }
    base_bs = blk_bs(base_blk);
    ImageInfo *info, *base_info;
    bdrv_query_image_info(base_bs, &base_info, &local_err);
    if (local_err) {
        error_report_err(local_err);
        goto done;
    }

    char snapshot_uuid[PATH_MAX] = "";
    char parent_snapshot_uuid[PATH_MAX] = "";
    int snapshot_index, parent_snapshot_index;
    int ret = search_snapshot_by_name(layer_uuid, &snapshot_index, &parent_snapshot_uuid, NULL, base_info);
    if(ret < 0){
        error_exit("search_snapshot %s failed", layer_uuid);
    }
    if(parent_snapshot_uuid[0]){
        ret = search_snapshot_by_name(parent_snapshot_uuid, &parent_snapshot_index, NULL, NULL, base_info);
        if(ret < 0){
            error_exit("search_snapshot %s failed", parent_snapshot_uuid);
        }
    }

done:
    blk_unref(blk);

    return 0;
fail:
    g_free(options);
    return 1;
}

static int img_layer_remove(int argc, char **argv)
{

    return 0;
}

static const img_cmd_t img_cmds[] = {
#define DEF(option, callback, arg_string)        \
    { option, callback },
// #include "qemu-img-cmds.h"
    DEF("create", img_create, "create [-o options] {-t <template file> -l <layer UUID> -s <size>} filename")
    DEF("resize", img_resize, "resize filename [+ | -]size")
    DEF("info", img_info, "info filename")
    DEF("commit", img_commit, "commit [-t <cache>] [-s <snapshot>] -m <commit-message> filename")
    DEF("layerdump", img_layer_dump, "layerdump -t <template file> -l <layer UUID> filename")
    DEF("layerremove", img_layer_remove, "layerremove -l <layer UUID> filename")
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
