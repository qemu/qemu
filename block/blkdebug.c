/*
 * Block protocol for I/O error injection
 *
 * Copyright (c) 2010 Kevin Wolf <kwolf@redhat.com>
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

#include "qemu-common.h"
#include "qemu/config-file.h"
#include "block/block_int.h"
#include "qemu/module.h"

typedef struct BDRVBlkdebugState {
    int state;
    int new_state;

    QLIST_HEAD(, BlkdebugRule) rules[BLKDBG_EVENT_MAX];
    QSIMPLEQ_HEAD(, BlkdebugRule) active_rules;
    QLIST_HEAD(, BlkdebugSuspendedReq) suspended_reqs;
} BDRVBlkdebugState;

typedef struct BlkdebugAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    int ret;
} BlkdebugAIOCB;

typedef struct BlkdebugSuspendedReq {
    Coroutine *co;
    char *tag;
    QLIST_ENTRY(BlkdebugSuspendedReq) next;
} BlkdebugSuspendedReq;

static void blkdebug_aio_cancel(BlockDriverAIOCB *blockacb);

static const AIOCBInfo blkdebug_aiocb_info = {
    .aiocb_size = sizeof(BlkdebugAIOCB),
    .cancel     = blkdebug_aio_cancel,
};

enum {
    ACTION_INJECT_ERROR,
    ACTION_SET_STATE,
    ACTION_SUSPEND,
};

typedef struct BlkdebugRule {
    BlkDebugEvent event;
    int action;
    int state;
    union {
        struct {
            int error;
            int immediately;
            int once;
            int64_t sector;
        } inject;
        struct {
            int new_state;
        } set_state;
        struct {
            char *tag;
        } suspend;
    } options;
    QLIST_ENTRY(BlkdebugRule) next;
    QSIMPLEQ_ENTRY(BlkdebugRule) active_next;
} BlkdebugRule;

static QemuOptsList inject_error_opts = {
    .name = "inject-error",
    .head = QTAILQ_HEAD_INITIALIZER(inject_error_opts.head),
    .desc = {
        {
            .name = "event",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "state",
            .type = QEMU_OPT_NUMBER,
        },
        {
            .name = "errno",
            .type = QEMU_OPT_NUMBER,
        },
        {
            .name = "sector",
            .type = QEMU_OPT_NUMBER,
        },
        {
            .name = "once",
            .type = QEMU_OPT_BOOL,
        },
        {
            .name = "immediately",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

static QemuOptsList set_state_opts = {
    .name = "set-state",
    .head = QTAILQ_HEAD_INITIALIZER(set_state_opts.head),
    .desc = {
        {
            .name = "event",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "state",
            .type = QEMU_OPT_NUMBER,
        },
        {
            .name = "new_state",
            .type = QEMU_OPT_NUMBER,
        },
        { /* end of list */ }
    },
};

static QemuOptsList *config_groups[] = {
    &inject_error_opts,
    &set_state_opts,
    NULL
};

static const char *event_names[BLKDBG_EVENT_MAX] = {
    [BLKDBG_L1_UPDATE]                      = "l1_update",
    [BLKDBG_L1_GROW_ALLOC_TABLE]            = "l1_grow.alloc_table",
    [BLKDBG_L1_GROW_WRITE_TABLE]            = "l1_grow.write_table",
    [BLKDBG_L1_GROW_ACTIVATE_TABLE]         = "l1_grow.activate_table",

    [BLKDBG_L2_LOAD]                        = "l2_load",
    [BLKDBG_L2_UPDATE]                      = "l2_update",
    [BLKDBG_L2_UPDATE_COMPRESSED]           = "l2_update_compressed",
    [BLKDBG_L2_ALLOC_COW_READ]              = "l2_alloc.cow_read",
    [BLKDBG_L2_ALLOC_WRITE]                 = "l2_alloc.write",

    [BLKDBG_READ_AIO]                       = "read_aio",
    [BLKDBG_READ_BACKING_AIO]               = "read_backing_aio",
    [BLKDBG_READ_COMPRESSED]                = "read_compressed",

    [BLKDBG_WRITE_AIO]                      = "write_aio",
    [BLKDBG_WRITE_COMPRESSED]               = "write_compressed",

    [BLKDBG_VMSTATE_LOAD]                   = "vmstate_load",
    [BLKDBG_VMSTATE_SAVE]                   = "vmstate_save",

    [BLKDBG_COW_READ]                       = "cow_read",
    [BLKDBG_COW_WRITE]                      = "cow_write",

    [BLKDBG_REFTABLE_LOAD]                  = "reftable_load",
    [BLKDBG_REFTABLE_GROW]                  = "reftable_grow",
    [BLKDBG_REFTABLE_UPDATE]                = "reftable_update",

    [BLKDBG_REFBLOCK_LOAD]                  = "refblock_load",
    [BLKDBG_REFBLOCK_UPDATE]                = "refblock_update",
    [BLKDBG_REFBLOCK_UPDATE_PART]           = "refblock_update_part",
    [BLKDBG_REFBLOCK_ALLOC]                 = "refblock_alloc",
    [BLKDBG_REFBLOCK_ALLOC_HOOKUP]          = "refblock_alloc.hookup",
    [BLKDBG_REFBLOCK_ALLOC_WRITE]           = "refblock_alloc.write",
    [BLKDBG_REFBLOCK_ALLOC_WRITE_BLOCKS]    = "refblock_alloc.write_blocks",
    [BLKDBG_REFBLOCK_ALLOC_WRITE_TABLE]     = "refblock_alloc.write_table",
    [BLKDBG_REFBLOCK_ALLOC_SWITCH_TABLE]    = "refblock_alloc.switch_table",

    [BLKDBG_CLUSTER_ALLOC]                  = "cluster_alloc",
    [BLKDBG_CLUSTER_ALLOC_BYTES]            = "cluster_alloc_bytes",
    [BLKDBG_CLUSTER_FREE]                   = "cluster_free",

    [BLKDBG_FLUSH_TO_OS]                    = "flush_to_os",
    [BLKDBG_FLUSH_TO_DISK]                  = "flush_to_disk",

    [BLKDBG_PWRITEV_RMW_HEAD]               = "pwritev_rmw.head",
    [BLKDBG_PWRITEV_RMW_AFTER_HEAD]         = "pwritev_rmw.after_head",
    [BLKDBG_PWRITEV_RMW_TAIL]               = "pwritev_rmw.tail",
    [BLKDBG_PWRITEV_RMW_AFTER_TAIL]         = "pwritev_rmw.after_tail",
    [BLKDBG_PWRITEV]                        = "pwritev",
    [BLKDBG_PWRITEV_ZERO]                   = "pwritev_zero",
    [BLKDBG_PWRITEV_DONE]                   = "pwritev_done",
};

static int get_event_by_name(const char *name, BlkDebugEvent *event)
{
    int i;

    for (i = 0; i < BLKDBG_EVENT_MAX; i++) {
        if (!strcmp(event_names[i], name)) {
            *event = i;
            return 0;
        }
    }

    return -1;
}

struct add_rule_data {
    BDRVBlkdebugState *s;
    int action;
};

static int add_rule(QemuOpts *opts, void *opaque)
{
    struct add_rule_data *d = opaque;
    BDRVBlkdebugState *s = d->s;
    const char* event_name;
    BlkDebugEvent event;
    struct BlkdebugRule *rule;

    /* Find the right event for the rule */
    event_name = qemu_opt_get(opts, "event");
    if (!event_name || get_event_by_name(event_name, &event) < 0) {
        return -1;
    }

    /* Set attributes common for all actions */
    rule = g_malloc0(sizeof(*rule));
    *rule = (struct BlkdebugRule) {
        .event  = event,
        .action = d->action,
        .state  = qemu_opt_get_number(opts, "state", 0),
    };

    /* Parse action-specific options */
    switch (d->action) {
    case ACTION_INJECT_ERROR:
        rule->options.inject.error = qemu_opt_get_number(opts, "errno", EIO);
        rule->options.inject.once  = qemu_opt_get_bool(opts, "once", 0);
        rule->options.inject.immediately =
            qemu_opt_get_bool(opts, "immediately", 0);
        rule->options.inject.sector = qemu_opt_get_number(opts, "sector", -1);
        break;

    case ACTION_SET_STATE:
        rule->options.set_state.new_state =
            qemu_opt_get_number(opts, "new_state", 0);
        break;

    case ACTION_SUSPEND:
        rule->options.suspend.tag =
            g_strdup(qemu_opt_get(opts, "tag"));
        break;
    };

    /* Add the rule */
    QLIST_INSERT_HEAD(&s->rules[event], rule, next);

    return 0;
}

static void remove_rule(BlkdebugRule *rule)
{
    switch (rule->action) {
    case ACTION_INJECT_ERROR:
    case ACTION_SET_STATE:
        break;
    case ACTION_SUSPEND:
        g_free(rule->options.suspend.tag);
        break;
    }

    QLIST_REMOVE(rule, next);
    g_free(rule);
}

static int read_config(BDRVBlkdebugState *s, const char *filename,
                       QDict *options, Error **errp)
{
    FILE *f = NULL;
    int ret;
    struct add_rule_data d;
    Error *local_err = NULL;

    if (filename) {
        f = fopen(filename, "r");
        if (f == NULL) {
            error_setg_errno(errp, errno, "Could not read blkdebug config file");
            return -errno;
        }

        ret = qemu_config_parse(f, config_groups, filename);
        if (ret < 0) {
            error_setg(errp, "Could not parse blkdebug config file");
            ret = -EINVAL;
            goto fail;
        }
    }

    qemu_config_parse_qdict(options, config_groups, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    d.s = s;
    d.action = ACTION_INJECT_ERROR;
    qemu_opts_foreach(&inject_error_opts, add_rule, &d, 0);

    d.action = ACTION_SET_STATE;
    qemu_opts_foreach(&set_state_opts, add_rule, &d, 0);

    ret = 0;
fail:
    qemu_opts_reset(&inject_error_opts);
    qemu_opts_reset(&set_state_opts);
    if (f) {
        fclose(f);
    }
    return ret;
}

/* Valid blkdebug filenames look like blkdebug:path/to/config:path/to/image */
static void blkdebug_parse_filename(const char *filename, QDict *options,
                                    Error **errp)
{
    const char *c;

    /* Parse the blkdebug: prefix */
    if (!strstart(filename, "blkdebug:", &filename)) {
        /* There was no prefix; therefore, all options have to be already
           present in the QDict (except for the filename) */
        qdict_put(options, "x-image", qstring_from_str(filename));
        return;
    }

    /* Parse config file path */
    c = strchr(filename, ':');
    if (c == NULL) {
        error_setg(errp, "blkdebug requires both config file and image path");
        return;
    }

    if (c != filename) {
        QString *config_path;
        config_path = qstring_from_substr(filename, 0, c - filename - 1);
        qdict_put(options, "config", config_path);
    }

    /* TODO Allow multi-level nesting and set file.filename here */
    filename = c + 1;
    qdict_put(options, "x-image", qstring_from_str(filename));
}

static QemuOptsList runtime_opts = {
    .name = "blkdebug",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "config",
            .type = QEMU_OPT_STRING,
            .help = "Path to the configuration file",
        },
        {
            .name = "x-image",
            .type = QEMU_OPT_STRING,
            .help = "[internal use only, will be removed]",
        },
        {
            .name = "align",
            .type = QEMU_OPT_SIZE,
            .help = "Required alignment in bytes",
        },
        { /* end of list */ }
    },
};

static int blkdebug_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp)
{
    BDRVBlkdebugState *s = bs->opaque;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *config;
    uint64_t align;
    int ret;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }

    /* Read rules from config file or command line options */
    config = qemu_opt_get(opts, "config");
    ret = read_config(s, config, options, errp);
    if (ret) {
        goto out;
    }

    /* Set initial state */
    s->state = 1;

    /* Open the backing file */
    assert(bs->file == NULL);
    ret = bdrv_open_image(&bs->file, qemu_opt_get(opts, "x-image"), options, "image",
                          flags | BDRV_O_PROTOCOL, false, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto out;
    }

    /* Set request alignment */
    align = qemu_opt_get_size(opts, "align", bs->request_alignment);
    if (align > 0 && align < INT_MAX && !(align & (align - 1))) {
        bs->request_alignment = align;
    } else {
        error_setg(errp, "Invalid alignment");
        ret = -EINVAL;
        goto fail_unref;
    }

    ret = 0;
    goto out;

fail_unref:
    bdrv_unref(bs->file);
out:
    qemu_opts_del(opts);
    return ret;
}

static void error_callback_bh(void *opaque)
{
    struct BlkdebugAIOCB *acb = opaque;
    qemu_bh_delete(acb->bh);
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_aio_release(acb);
}

static void blkdebug_aio_cancel(BlockDriverAIOCB *blockacb)
{
    BlkdebugAIOCB *acb = container_of(blockacb, BlkdebugAIOCB, common);
    qemu_aio_release(acb);
}

static BlockDriverAIOCB *inject_error(BlockDriverState *bs,
    BlockDriverCompletionFunc *cb, void *opaque, BlkdebugRule *rule)
{
    BDRVBlkdebugState *s = bs->opaque;
    int error = rule->options.inject.error;
    struct BlkdebugAIOCB *acb;
    QEMUBH *bh;

    if (rule->options.inject.once) {
        QSIMPLEQ_INIT(&s->active_rules);
    }

    if (rule->options.inject.immediately) {
        return NULL;
    }

    acb = qemu_aio_get(&blkdebug_aiocb_info, bs, cb, opaque);
    acb->ret = -error;

    bh = aio_bh_new(bdrv_get_aio_context(bs), error_callback_bh, acb);
    acb->bh = bh;
    qemu_bh_schedule(bh);

    return &acb->common;
}

static BlockDriverAIOCB *blkdebug_aio_readv(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugRule *rule = NULL;

    QSIMPLEQ_FOREACH(rule, &s->active_rules, active_next) {
        if (rule->options.inject.sector == -1 ||
            (rule->options.inject.sector >= sector_num &&
             rule->options.inject.sector < sector_num + nb_sectors)) {
            break;
        }
    }

    if (rule && rule->options.inject.error) {
        return inject_error(bs, cb, opaque, rule);
    }

    return bdrv_aio_readv(bs->file, sector_num, qiov, nb_sectors, cb, opaque);
}

static BlockDriverAIOCB *blkdebug_aio_writev(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugRule *rule = NULL;

    QSIMPLEQ_FOREACH(rule, &s->active_rules, active_next) {
        if (rule->options.inject.sector == -1 ||
            (rule->options.inject.sector >= sector_num &&
             rule->options.inject.sector < sector_num + nb_sectors)) {
            break;
        }
    }

    if (rule && rule->options.inject.error) {
        return inject_error(bs, cb, opaque, rule);
    }

    return bdrv_aio_writev(bs->file, sector_num, qiov, nb_sectors, cb, opaque);
}


static void blkdebug_close(BlockDriverState *bs)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugRule *rule, *next;
    int i;

    for (i = 0; i < BLKDBG_EVENT_MAX; i++) {
        QLIST_FOREACH_SAFE(rule, &s->rules[i], next, next) {
            remove_rule(rule);
        }
    }
}

static void suspend_request(BlockDriverState *bs, BlkdebugRule *rule)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugSuspendedReq r;

    r = (BlkdebugSuspendedReq) {
        .co         = qemu_coroutine_self(),
        .tag        = g_strdup(rule->options.suspend.tag),
    };

    remove_rule(rule);
    QLIST_INSERT_HEAD(&s->suspended_reqs, &r, next);

    printf("blkdebug: Suspended request '%s'\n", r.tag);
    qemu_coroutine_yield();
    printf("blkdebug: Resuming request '%s'\n", r.tag);

    QLIST_REMOVE(&r, next);
    g_free(r.tag);
}

static bool process_rule(BlockDriverState *bs, struct BlkdebugRule *rule,
    bool injected)
{
    BDRVBlkdebugState *s = bs->opaque;

    /* Only process rules for the current state */
    if (rule->state && rule->state != s->state) {
        return injected;
    }

    /* Take the action */
    switch (rule->action) {
    case ACTION_INJECT_ERROR:
        if (!injected) {
            QSIMPLEQ_INIT(&s->active_rules);
            injected = true;
        }
        QSIMPLEQ_INSERT_HEAD(&s->active_rules, rule, active_next);
        break;

    case ACTION_SET_STATE:
        s->new_state = rule->options.set_state.new_state;
        break;

    case ACTION_SUSPEND:
        suspend_request(bs, rule);
        break;
    }
    return injected;
}

static void blkdebug_debug_event(BlockDriverState *bs, BlkDebugEvent event)
{
    BDRVBlkdebugState *s = bs->opaque;
    struct BlkdebugRule *rule, *next;
    bool injected;

    assert((int)event >= 0 && event < BLKDBG_EVENT_MAX);

    injected = false;
    s->new_state = s->state;
    QLIST_FOREACH_SAFE(rule, &s->rules[event], next, next) {
        injected = process_rule(bs, rule, injected);
    }
    s->state = s->new_state;
}

static int blkdebug_debug_breakpoint(BlockDriverState *bs, const char *event,
                                     const char *tag)
{
    BDRVBlkdebugState *s = bs->opaque;
    struct BlkdebugRule *rule;
    BlkDebugEvent blkdebug_event;

    if (get_event_by_name(event, &blkdebug_event) < 0) {
        return -ENOENT;
    }


    rule = g_malloc(sizeof(*rule));
    *rule = (struct BlkdebugRule) {
        .event  = blkdebug_event,
        .action = ACTION_SUSPEND,
        .state  = 0,
        .options.suspend.tag = g_strdup(tag),
    };

    QLIST_INSERT_HEAD(&s->rules[blkdebug_event], rule, next);

    return 0;
}

static int blkdebug_debug_resume(BlockDriverState *bs, const char *tag)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugSuspendedReq *r, *next;

    QLIST_FOREACH_SAFE(r, &s->suspended_reqs, next, next) {
        if (!strcmp(r->tag, tag)) {
            qemu_coroutine_enter(r->co, NULL);
            return 0;
        }
    }
    return -ENOENT;
}

static int blkdebug_debug_remove_breakpoint(BlockDriverState *bs,
                                            const char *tag)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugSuspendedReq *r, *r_next;
    BlkdebugRule *rule, *next;
    int i, ret = -ENOENT;

    for (i = 0; i < BLKDBG_EVENT_MAX; i++) {
        QLIST_FOREACH_SAFE(rule, &s->rules[i], next, next) {
            if (rule->action == ACTION_SUSPEND &&
                !strcmp(rule->options.suspend.tag, tag)) {
                remove_rule(rule);
                ret = 0;
            }
        }
    }
    QLIST_FOREACH_SAFE(r, &s->suspended_reqs, next, r_next) {
        if (!strcmp(r->tag, tag)) {
            qemu_coroutine_enter(r->co, NULL);
            ret = 0;
        }
    }
    return ret;
}

static bool blkdebug_debug_is_suspended(BlockDriverState *bs, const char *tag)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugSuspendedReq *r;

    QLIST_FOREACH(r, &s->suspended_reqs, next) {
        if (!strcmp(r->tag, tag)) {
            return true;
        }
    }
    return false;
}

static int64_t blkdebug_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file);
}

static BlockDriver bdrv_blkdebug = {
    .format_name            = "blkdebug",
    .protocol_name          = "blkdebug",
    .instance_size          = sizeof(BDRVBlkdebugState),

    .bdrv_parse_filename    = blkdebug_parse_filename,
    .bdrv_file_open         = blkdebug_open,
    .bdrv_close             = blkdebug_close,
    .bdrv_getlength         = blkdebug_getlength,

    .bdrv_aio_readv         = blkdebug_aio_readv,
    .bdrv_aio_writev        = blkdebug_aio_writev,

    .bdrv_debug_event           = blkdebug_debug_event,
    .bdrv_debug_breakpoint      = blkdebug_debug_breakpoint,
    .bdrv_debug_remove_breakpoint
                                = blkdebug_debug_remove_breakpoint,
    .bdrv_debug_resume          = blkdebug_debug_resume,
    .bdrv_debug_is_suspended    = blkdebug_debug_is_suspended,
};

static void bdrv_blkdebug_init(void)
{
    bdrv_register(&bdrv_blkdebug);
}

block_init(bdrv_blkdebug_init);
