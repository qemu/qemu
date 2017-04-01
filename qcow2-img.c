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
#include "block/qcow2.h"
#include "qcow2-img-utils.h"
#include "block/block.h"
#include "io/channel-socket.h"
#include "block/nbd.h"
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
            "info filename\n"
            "commit [-t <cache>] [-s <snapshot>] -m <commit-message> filename\n"
            "layerdump -t <template file> -l <layer UUID> filename\n"
            "layerremove -l <layer UUID> filename\n"
            "mount -c </dev/nbdx> filename\n"
            ;
    printf("%s", help_msg);
    exit(EXIT_SUCCESS);
}

static char *generate_encoded_backingfile(const char* template, const char* layer_uuid)
{
    char *backing_string = malloc(PATH_MAX*2);
    snprintf(backing_string, PATH_MAX*2, "qcow2://%s?layer=%s", template, (layer_uuid ? layer_uuid : ""));
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

static int64_t search_snapshot_by_name(char* uuid, int* index, char* puuid, char* msg, uint64_t* disk_size, ImageInfo *info)
{
    if (!info->snapshots) {
        return -1L;
    }

    int count = 0;
    SnapshotInfoList *cur = info->snapshots;
    do{
        char tmp_uuid[PATH_MAX] = "", tmp_puuid[PATH_MAX] = "", commit_msg[PATH_MAX] = "";
        int ret = sscanf(cur->value->name, "%[^,],%[^,],%s", tmp_uuid, tmp_puuid, commit_msg);
        if(ret < 1){
            error_report("sscanf %s ret %d", cur->value->name, ret);
            return -1L;
        }
        if (0 == strcmp(uuid, tmp_uuid)) {
            if(puuid){
                strcpy(puuid, tmp_puuid);
            }
            if(msg){
                strcpy(msg, commit_msg);
            }
            if(disk_size){
                *disk_size = cur->value->disk_size;
            }
            *index = count;
            return atol(cur->value->id);
        }
        count++;
        cur = cur->next;
    }while(cur);

    return -1L;
}

static int64_t search_snapshot_by_pname(char* puuid, int* index, char* uuid, char* msg, uint64_t* disk_size, ImageInfo *info)
{
    if (!info->snapshots) {
        return -1L;
    }

    int count = 0;
    SnapshotInfoList *cur = info->snapshots;
    do{
        char tmp_uuid[PATH_MAX] = "", tmp_puuid[PATH_MAX] = "", commit_msg[PATH_MAX] = "";
        int ret = sscanf(cur->value->name, "%[^,],%[^,],%s", tmp_uuid, tmp_puuid, commit_msg);
        if(ret < 1){
            error_report("sscanf %s ret %d", cur->value->name, ret);
            return -1L;
        }
        if (0 == strcmp(tmp_puuid, puuid)) {
            if(uuid){
                strcpy(uuid, tmp_uuid);
            }
            if(msg){
                strcpy(msg, commit_msg);
            }
            if(disk_size){
                *disk_size = cur->value->disk_size;
            }
            if(index){
                *index = count;
            }
            return atol(cur->value->id);
        }
        count++;
        cur = cur->next;
    }while(cur);

    return -1L;
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
        c = getopt_long(argc, argv, "ht:b:dpqm:s:",
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
        case 's':
            snapshot_uuid = optarg;
            break;
        case 'm':
            commit_msg = optarg;
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
    if (ret != 2 && ret != 1) {
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
    if (ret < 1) {
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

    ret = bdrv_snapshot_create(base_bs, &sn);
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
    if (local_err) {
        error_report_err(local_err);
        return 1;
    }

    qemu_progress_end();

    blk_unref(blk);
    blk_unref(base_blk);

    qprintf(quiet, "Image committed.\n");
    return 0;
}

static int open_template(char* tmplate_name, char* layername,
                         BlockBackend **blk, BlockDriverState **bs,
                         uint64_t *disk_size, Error **errp);

static int _img_create(const char *filename, const char *fmt,
                       const char *base_filename, const char *base_fmt,
                       char *options, uint64_t img_size, int flags,
                       Error **errp, bool quiet)
{
    BlockDriver *drv, *proto_drv;
    int ret = 0;
    QemuOptsList *create_opts = NULL;
    QemuOpts *opts = NULL;

    /* Find driver and parse its options */
    drv = bdrv_find_format(fmt);
    if (!drv) {
        error_setg(errp, "Unknown file format '%s'", "qcow2");
        goto fail;
    }

    proto_drv = bdrv_find_protocol(filename, true, errp);
    if (!proto_drv) {
        goto fail;
    }

    if (!drv->create_opts) {
        error_setg(errp, "Format driver '%s' does not support image creation",
                   drv->format_name);
        goto fail;
    }

    if (!proto_drv->create_opts) {
        error_setg(errp, "Protocol driver '%s' does not support image creation",
                   proto_drv->format_name);
        goto fail;
    }

    create_opts = qemu_opts_append(create_opts, drv->create_opts);
    create_opts = qemu_opts_append(create_opts, proto_drv->create_opts);

    opts = qemu_opts_create(create_opts, NULL, 0, &error_abort);
    qemu_opt_set_number(opts, BLOCK_OPT_SIZE, img_size, &error_abort);

    if(base_filename){
        qemu_opt_set(opts, BLOCK_OPT_BACKING_FILE, base_filename, errp);
    }
    ret = bdrv_create(drv, filename, opts, errp);
    if (ret == -EFBIG) {
        error_reportf_err(*errp, "%s: ", filename);
        goto fail;
    }
    return 0;
fail:
    return -1;
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
        BlockBackend *base_blk;
        BlockDriverState *base_bs;
        uint64_t template_size;
        int ret = open_template((char*)template_filename, (char*)layer_uuid, &base_blk, &base_bs, &template_size, &local_err);
        if(ret != 0){
            goto fail;
        }
        if(img_size == -1){
            img_size = template_size;
        }
    }

    int ret = _img_create(filename, "qcow2", backing_string, NULL, options, img_size, 0,
                          &local_err, quiet);
    if(ret != 0){
        goto fail;
    }

    g_free(options);
    return 0;

fail:
    g_free(options);
    return 1;
}

static int bdrv_write_zeros(BlockDriverState *bs, int64_t offset, int bytes)
{
    int ret = bs->drv->bdrv_co_pwrite_zeroes(bs, offset>>BDRV_SECTOR_BITS, bytes>>BDRV_SECTOR_BITS, BDRV_REQ_ZERO_WRITE);
    if(ret < 0){
        return ret;
    }
    return bytes;
}

static int img_layer_dump(int argc, char **argv)
{
    int c;
    char *options = NULL;
    BlockDriverState *bs, *base_bs;
    BlockBackend *blk, *base_blk = NULL;
    const char *filename = NULL;
    const char *fmt = "qcow2";
    char *template_filename = NULL;
    bool quiet = false;
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

    base_blk = img_open(image_opts, template_filename, fmt, BDRV_O_RDWR, writethrough, quiet);
    if (!base_blk) {
        error_report("error open img: %s", template_filename);
        return 1;
    }
    base_bs = blk_bs(base_blk);
    ImageInfo *base_info;
    bdrv_query_image_info(base_bs, &base_info, &local_err);
    if (local_err) {
        error_report_err(local_err);
        goto fail;
    }

    char parent_snapshot_uuid[PATH_MAX] = "";
    int snapshot_index, parent_snapshot_index = -1;
    uint64_t snapshot_disk_size;
    int64_t sn_id = search_snapshot_by_name(layer_uuid, &snapshot_index, parent_snapshot_uuid, NULL, &snapshot_disk_size, base_info);
    if(sn_id < 0){
        error_exit("search_snapshot %s failed", layer_uuid);
    }
    if(parent_snapshot_uuid[0]){
        sn_id = search_snapshot_by_name(parent_snapshot_uuid, &parent_snapshot_index, NULL, NULL, NULL, base_info);
        if(sn_id < 0){
            error_exit("search_snapshot %s failed", parent_snapshot_uuid);
        }
    }

    char *backing_str = generate_encoded_backingfile(template_filename, layer_uuid);
    int ret = _img_create(filename, "qcow2", backing_str, NULL, options, snapshot_disk_size, 0,
                          &local_err, quiet);
   if(ret != 0){
       error_report_err(local_err);
       goto fail;
   }

    blk = img_open(image_opts, filename, fmt, BDRV_O_NO_BACKING | BDRV_O_RDWR, writethrough, quiet);
    if (!blk) {
        error_report("error open img: %s", filename);
        return 1;
    }
    bs = blk_bs(blk);

    Snapshot_cache_t cache, parent_cache;
    uint64_t increament_cluster_count;
    init_cache(&cache, snapshot_index);
    init_cache(&parent_cache, parent_snapshot_index);
    uint64_t total_cluster_nb = get_layer_cluster_nb(base_bs, snapshot_index);
    BDRVQcow2State *s = base_bs->opaque;
    const int data_size = sizeof(ClusterData_t) + s->cluster_size;
    ClusterData_t *data = malloc(data_size);
    ret = count_increment_clusters(base_bs, &cache, &parent_cache, &increament_cluster_count, 0);
    if(ret < 0){
        error_exit("count_increment_clusters failed");
    }

    uint64_t i;
    for(i = 0; i < total_cluster_nb; i++) {
        bool is_cluster_0_offset;
        ret = read_snapshot_cluster_increment(base_bs, &cache, &parent_cache, i, data, &is_cluster_0_offset);
        if(ret < 0){
            error_report("error read snapshot cluster");
            goto fail;
        }
        if(ret == 0){
            continue;
        }
        uint64_t off = data->cluset_index << s->cluster_bits;
        ret = ret == 1 ? bdrv_pwrite(bs->file, off, data->buf, s->cluster_size) :
                         bdrv_write_zeros(bs, off, s->cluster_size);
        if(ret < s->cluster_size){
            error_report("error bdrv_pwrite ret is %d", ret);
            goto fail;
        }
    }

    blk_unref(blk);
    blk_unref(base_blk);

    return 0;
fail:
    g_free(options);
    return 1;
}

static int img_layer_remove(int argc, char **argv)
{
    int c;
    bool image_opts = false;
    char *layer_uuid = NULL;
    const char *filename = NULL;
    char *options = NULL;
    const char *fmt = "qcow2";
    bool writethrough = false;
    bool quiet = false;
    Error *local_err = NULL;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "l:h",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
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

    if (layer_uuid == NULL) {
        error_exit("need to input layer_uuid");
    }

    BlockBackend *blk = img_open(image_opts, filename, fmt, BDRV_O_RDWR, writethrough, quiet);
    if (!blk) {
        error_report("error open img: %s", filename);
        return 1;
    }
    BlockDriverState *bs = blk_bs(blk);
    ImageInfo *info;
    bdrv_query_image_info(bs, &info, &local_err);
    if (local_err) {
        error_report_err(local_err);
        goto fail;
    }

    char parent_snapshot_uuid[PATH_MAX] = "";
    int snapshot_index;
    uint64_t snapshot_disk_size;
    int64_t sn_id = search_snapshot_by_name(layer_uuid, &snapshot_index, parent_snapshot_uuid, NULL, &snapshot_disk_size, info);
    if(sn_id < 0){
        error_exit("search_snapshot %s failed", layer_uuid);
    }

    char id[32] = "";
    sprintf(id, "%ld", sn_id);
    int ret = bdrv_snapshot_delete_by_id_or_name(bs, id, &local_err);
    if(ret != 0){
         goto fail;
    }

    int64_t child_sn_id;
    do {
        bdrv_query_image_info(bs, &info, &local_err);
        if (local_err) {
            error_report_err(local_err);
            goto fail;
        }

        char child_snapshot_uuid[PATH_MAX] = "", child_msg[PATH_MAX] = "";
        child_sn_id = search_snapshot_by_pname(layer_uuid, NULL, child_snapshot_uuid, child_msg, NULL, info);
        if(child_sn_id < 0){
            break;
        }
        char child_id[32] = "";
        sprintf(child_id, "%ld", child_sn_id);
        char enforced_snapshot_uuid[PATH_MAX*2];
        generate_enforced_snapshotname(enforced_snapshot_uuid, parent_snapshot_uuid, child_snapshot_uuid, child_msg);
        ret = bdrv_snapshot_rename(bs, child_id, enforced_snapshot_uuid, &local_err);
        if(ret < 0){
            goto fail;
        }
    } while(1);

    return ret;

fail:
    error_report_err(local_err);
    g_free(options);
    return 1;
}

static int open_template(char* tmplate_name, char* layername, BlockBackend **blk, BlockDriverState **bs, uint64_t *disk_size, Error **errp)
{
    int ret;
    int backing_flags =  BDRV_O_UNMAP | BDRV_O_NO_BACKING;
    BlockBackend *base_blk = img_open(false, tmplate_name, "qcow2", backing_flags, true, true);
    if (!base_blk) {
        error_setg_errno(errp, -1, "error open backing file %s, can't commit", tmplate_name);
        return -1;
    }
    ImageInfo *base_info;
    BlockDriverState *base_bs = blk_bs(base_blk);
    Error *err = NULL;
    bdrv_query_image_info(base_bs, &base_info, &err);
    if (err) {
        error_setg_errno(errp, -1, "error get image info from backing file, can't commit");
        return -1;
    }

    if(disk_size){
        *disk_size = blk_getlength(base_blk);
    }

    if(layername && layername[0]){
        int index;
        int64_t id = search_snapshot_by_name(layername, &index, NULL, NULL, disk_size, base_info);
        if(id < 0){
            error_setg_errno(errp, -1, "error search snapshot by name");
            return -1;
        }
        char snapshotid[32];
        sprintf(snapshotid, "%ld", id);
        ret = bdrv_snapshot_load_tmp_by_id_or_name(base_bs, snapshotid, errp);
        if(ret < 0){
            error_report("error qcow2_snapshot_load_tmp %s %d %s", snapshotid, ret, *(char**)(*errp));
            return ret;
        }
    }
    *blk = base_blk;
    *bs = base_bs;
    return 0;
}

static BlockBackend *blk_open_enforced_img(const char *filename, Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;

    int flags = BDRV_O_RDWR | BDRV_O_UNMAP | BDRV_O_NO_BACKING;
    blk = img_open(false, filename, "qcow2", flags, true, true);
    if (!blk) {
        return NULL;
    }
    bs = blk_bs(blk);

    ImageInfo *info;
    bdrv_query_image_info(bs, &info, errp);
    if (*errp) {
        error_report_err(*errp);
        return NULL;
    }

    if(!info->has_backing_filename){
        return blk;
    }

    char tmplate_name[PATH_MAX] = "", layername[PATH_MAX] = "";
    int ret = get_encoded_backingfile(info->backing_filename, tmplate_name, layername);
    if (ret != 2 && ret != 1) {
        error_setg_errno(errp, -1, "error get get_encoded_backingfile, can't commit");
        return NULL;
    }

    BlockBackend *base_blk;
    BlockDriverState *base_bs;
    ret = open_template(tmplate_name, layername, &base_blk, &base_bs, NULL, errp);
    if(ret != 0){
        return NULL;
    }

    bdrv_set_backing_hd(bs, base_bs);
    bdrv_unref(base_bs);

    return blk;
}


#define SOCKET_PATH                "/var/lock/qemu-nbd-%s"
static SocketAddress *saddr;
static char *srcpath;
static NBDExport *exp;
static bool newproto;
static QIOChannelSocket *server_ioc;
static int verbose;
static int server_watch = -1;
static int shared = 1;
static int nb_fds;
static int persistent = 0;
static enum { RUNNING, TERMINATE, TERMINATING, TERMINATED } state;
static QCryptoTLSCreds *tlscreds;

static void *show_parts(void *arg)
{
    char *device = arg;
    int nbd;

    /* linux just needs an open() to trigger
     * the partition table update
     * but remember to load the module with max_part != 0 :
     *     modprobe nbd max_part=63
     */
    nbd = open(device, O_RDWR);
    if (nbd >= 0) {
        close(nbd);
    }
    return NULL;
}

static void *nbd_client_thread(void *arg)
{
    char *device = arg;
    off_t size;
    uint16_t nbdflags;
    QIOChannelSocket *sioc;
    int fd;
    int ret;
    pthread_t show_parts_thread;
    Error *local_error = NULL;

    sioc = qio_channel_socket_new();
    if (qio_channel_socket_connect_sync(sioc,
                                        saddr,
                                        &local_error) < 0) {
        error_report_err(local_error);
        goto out;
    }

    ret = nbd_receive_negotiate(QIO_CHANNEL(sioc), NULL, &nbdflags,
                                NULL, NULL, NULL,
                                &size, &local_error);
    if (ret < 0) {
        if (local_error) {
            error_report_err(local_error);
        }
        goto out_socket;
    }

    fd = open(device, O_RDWR);
    if (fd < 0) {
        /* Linux-only, we can use %m in printf.  */
        error_report("Failed to open %s: %m", device);
        goto out_socket;
    }

    ret = nbd_init(fd, sioc, nbdflags, size);
    if (ret < 0) {
        goto out_fd;
    }

    /* update partition table */
    pthread_create(&show_parts_thread, NULL, show_parts, device);

    if (verbose) {
        fprintf(stderr, "NBD device %s is now connected to %s\n",
                device, srcpath);
    } else {
        /* Close stderr so that the qemu-nbd process exits.  */
        dup2(STDOUT_FILENO, STDERR_FILENO);
    }

    ret = nbd_client(fd);
    if (ret) {
        goto out_fd;
    }
    close(fd);
    object_unref(OBJECT(sioc));
    kill(getpid(), SIGTERM);
    return (void *) EXIT_SUCCESS;

out_fd:
    close(fd);
out_socket:
    object_unref(OBJECT(sioc));
out:
    kill(getpid(), SIGTERM);
    return (void *) EXIT_FAILURE;
}

static QemuOptsList file_opts = {
    .name = "file",
    .implied_opt_name = "file",
    .head = QTAILQ_HEAD_INITIALIZER(file_opts.head),
    .desc = {
        /* no elements => accept any params */
        { /* end of list */ }
    },
};

static void nbd_export_closed(NBDExport *exp)
{
    assert(state == TERMINATING);
    state = TERMINATED;
}

static SocketAddress *nbd_build_socket_address(const char *sockpath,
                                               const char *bindto,
                                               const char *port)
{
    SocketAddress *saddr;

    saddr = g_new0(SocketAddress, 1);
    if (sockpath) {
        saddr->type = SOCKET_ADDRESS_KIND_UNIX;
        saddr->u.q_unix.data = g_new0(UnixSocketAddress, 1);
        saddr->u.q_unix.data->path = g_strdup(sockpath);
    } else {
        InetSocketAddress *inet;
        saddr->type = SOCKET_ADDRESS_KIND_INET;
        inet = saddr->u.inet.data = g_new0(InetSocketAddress, 1);
        inet->host = g_strdup(bindto);
        if (port) {
            inet->port = g_strdup(port);
        } else  {
            inet->port = g_strdup_printf("%d", NBD_DEFAULT_PORT);
        }
    }

    return saddr;
}

static int nbd_can_accept(void)
{
    return nb_fds < shared;
}

static void nbd_update_server_watch(void);
static void nbd_client_closed(NBDClient *client)
{
    nb_fds--;
    if (nb_fds == 0 && !persistent && state == RUNNING) {
        state = TERMINATE;
    }
    nbd_update_server_watch();
    nbd_client_put(client);
}

static gboolean nbd_accept(QIOChannel *ioc, GIOCondition cond, gpointer opaque)
{
    QIOChannelSocket *cioc;

    cioc = qio_channel_socket_accept(QIO_CHANNEL_SOCKET(ioc),
                                     NULL);
    if (!cioc) {
        return TRUE;
    }

    if (state >= TERMINATE) {
        object_unref(OBJECT(cioc));
        return TRUE;
    }

    nb_fds++;
    nbd_update_server_watch();
    nbd_client_new(newproto ? NULL : exp, cioc,
                   tlscreds, NULL, nbd_client_closed);
    object_unref(OBJECT(cioc));

    return TRUE;
}

static void nbd_update_server_watch(void)
{
    if (nbd_can_accept()) {
        if (server_watch == -1) {
            server_watch = qio_channel_add_watch(QIO_CHANNEL(server_ioc),
                                                 G_IO_IN,
                                                 nbd_accept,
                                                 NULL, NULL);
        }
    } else {
        if (server_watch != -1) {
            g_source_remove(server_watch);
            server_watch = -1;
        }
    }
}

static int mount(int argc, char **argv)
{
    int c;
    BlockBackend *blk;
    BlockDriverState *bs;
    const char *filename = NULL;
    bool image_opts = false;
    char *options = NULL;
    char *device = NULL;
    int old_stderr = -1;
    const char *bindto = "0.0.0.0";
    const char *port = NULL;
    char *sockpath = NULL;
    Error *local_err = NULL;
    bool writethrough = true;
    off_t dev_offset = 0;
    off_t fd_size;
    pthread_t client_thread;
    uint16_t nbdflags = 0;
    const char *export_name = NULL;
    const char *export_description = NULL;
    bool fork_process = false;
    BlockdevDetectZeroesOptions detect_zeroes = BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "c:h",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            break;
        case 'c':
            device = optarg;
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

    if(!device) {
        error_exit("device can't be null");
    }

    if (device) {
        int stderr_fd[2];
        pid_t pid;
        int ret;

        if (qemu_pipe(stderr_fd) < 0) {
            error_report("Error setting up communication pipe: %s",
                         strerror(errno));
            exit(EXIT_FAILURE);
        }

        /* Now daemonize, but keep a communication channel open to
         * print errors and exit with the proper status code.
         */
        pid = fork();
        if (pid < 0) {
            error_report("Failed to fork: %s", strerror(errno));
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            close(stderr_fd[0]);
            ret = qemu_daemon(1, 0);

            /* Temporarily redirect stderr to the parent's pipe...  */
            old_stderr = dup(STDERR_FILENO);
            dup2(stderr_fd[1], STDERR_FILENO);
            if (ret < 0) {
                error_report("Failed to daemonize: %s", strerror(errno));
                exit(EXIT_FAILURE);
            }

            /* ... close the descriptor we inherited and go on.  */
            close(stderr_fd[1]);
        } else {
            bool errors = false;
            char *buf;

            /* In the parent.  Print error messages from the child until
             * it closes the pipe.
             */
            close(stderr_fd[1]);
            buf = g_malloc(1024);
            while ((ret = read(stderr_fd[0], buf, 1024)) > 0) {
                errors = true;
                ret = qemu_write_full(STDERR_FILENO, buf, ret);
                if (ret < 0) {
                    exit(EXIT_FAILURE);
                }
            }
            if (ret < 0) {
                error_report("Cannot read from daemon: %s",
                             strerror(errno));
                exit(EXIT_FAILURE);
            }

            /* Usually the daemon should not print any message.
             * Exit with zero status in that case.
             */
            exit(errors);
        }
    }

    if (device != NULL && sockpath == NULL) {
        sockpath = g_malloc(128);
        snprintf(sockpath, 128, SOCKET_PATH, basename(device));
    }

    saddr = nbd_build_socket_address(sockpath, bindto, port);

    atexit(bdrv_close_all);

    blk = blk_open_enforced_img(filename, &local_err);
    if (!blk) {
        error_reportf_err(local_err, "Failed to blk_new_open '%s': ",
                          argv[optind]);
        exit(EXIT_FAILURE);
    }
    bs = blk_bs(blk);

    blk_set_enable_write_cache(blk, !writethrough);

    bs->detect_zeroes = detect_zeroes;
    fd_size = blk_getlength(blk);
    if (fd_size < 0) {
        error_report("Failed to determine the image length: %s",
                     strerror(-fd_size));
        exit(EXIT_FAILURE);
    }

    if (dev_offset >= fd_size) {
        error_report("Offset (%lld) has to be smaller than the image size "
                     "(%lld)",
                     (long long int)dev_offset, (long long int)fd_size);
        exit(EXIT_FAILURE);
    }
    fd_size -= dev_offset;

    exp = nbd_export_new(bs, dev_offset, fd_size, nbdflags, nbd_export_closed,
                         writethrough, NULL, &local_err);
    if (!exp) {
        error_report_err(local_err);
        exit(EXIT_FAILURE);
    }
    if (export_name) {
        nbd_export_set_name(exp, export_name);
        nbd_export_set_description(exp, export_description);
        newproto = true;
    } else if (export_description) {
        error_report("Export description requires an export name");
        exit(EXIT_FAILURE);
    }

    server_ioc = qio_channel_socket_new();
    if (qio_channel_socket_listen_sync(server_ioc, saddr, &local_err) < 0) {
        object_unref(OBJECT(server_ioc));
        error_report_err(local_err);
        return 1;
    }

    if (device) {
        int ret;

        ret = pthread_create(&client_thread, NULL, nbd_client_thread, device);
        if (ret != 0) {
            error_report("Failed to create client thread: %s", strerror(ret));
            exit(EXIT_FAILURE);
        }
    } else {
        /* Shut up GCC warnings.  */
        memset(&client_thread, 0, sizeof(client_thread));
    }

    nbd_update_server_watch();

    /* now when the initialization is (almost) complete, chdir("/")
     * to free any busy filesystems */
    if (chdir("/") < 0) {
        error_report("Could not chdir to root directory: %s",
                     strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (fork_process) {
        dup2(old_stderr, STDERR_FILENO);
        close(old_stderr);
    }

    state = RUNNING;
    do {
        main_loop_wait(false);
        if (state == TERMINATE) {
            state = TERMINATING;
            nbd_export_close(exp);
            nbd_export_put(exp);
            exp = NULL;
        }
    } while (state != TERMINATED);

    blk_unref(blk);
    if (sockpath) {
        unlink(sockpath);
    }

    if (device) {
        void *ret;
        pthread_join(client_thread, &ret);
        exit(ret != NULL);
    } else {
        exit(EXIT_SUCCESS);
    }

    return 0;
fail:
    return 1;
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
    DEF("mount", mount, "mount -c </dev/nbdx> filename")
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
