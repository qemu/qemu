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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qstring.h"
#include "sysemu/qtest.h"

typedef struct BDRVBlkdebugState {
    int state;
    int new_state;

    QLIST_HEAD(, BlkdebugRule) rules[BLKDBG__MAX];
    QSIMPLEQ_HEAD(, BlkdebugRule) active_rules;
    QLIST_HEAD(, BlkdebugSuspendedReq) suspended_reqs;
} BDRVBlkdebugState;

typedef struct BlkdebugAIOCB {
    BlockAIOCB common;
    QEMUBH *bh;
    int ret;
} BlkdebugAIOCB;

typedef struct BlkdebugSuspendedReq {
    Coroutine *co;
    char *tag;
    QLIST_ENTRY(BlkdebugSuspendedReq) next;
} BlkdebugSuspendedReq;

static const AIOCBInfo blkdebug_aiocb_info = {
    .aiocb_size    = sizeof(BlkdebugAIOCB),
};

enum {
    ACTION_INJECT_ERROR,
    ACTION_SET_STATE,
    ACTION_SUSPEND,
};

typedef struct BlkdebugRule {
    BlkdebugEvent event;
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

static int get_event_by_name(const char *name, BlkdebugEvent *event)
{
    int i;

    for (i = 0; i < BLKDBG__MAX; i++) {
        if (!strcmp(BlkdebugEvent_lookup[i], name)) {
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

static int add_rule(void *opaque, QemuOpts *opts, Error **errp)
{
    struct add_rule_data *d = opaque;
    BDRVBlkdebugState *s = d->s;
    const char* event_name;
    BlkdebugEvent event;
    struct BlkdebugRule *rule;

    /* Find the right event for the rule */
    event_name = qemu_opt_get(opts, "event");
    if (!event_name) {
        error_setg(errp, "Missing event name for rule");
        return -1;
    } else if (get_event_by_name(event_name, &event) < 0) {
        error_setg(errp, "Invalid event name \"%s\"", event_name);
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
    qemu_opts_foreach(&inject_error_opts, add_rule, &d, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    d.action = ACTION_SET_STATE;
    qemu_opts_foreach(&set_state_opts, add_rule, &d, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

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

    /* Open the image file */
    bs->file = bdrv_open_child(qemu_opt_get(opts, "x-image"), options, "image",
                               bs, &child_file, false, &local_err);
    if (local_err) {
        ret = -EINVAL;
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
    bdrv_unref_child(bs, bs->file);
out:
    qemu_opts_del(opts);
    return ret;
}

static void error_callback_bh(void *opaque)
{
    struct BlkdebugAIOCB *acb = opaque;
    qemu_bh_delete(acb->bh);
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_aio_unref(acb);
}

static BlockAIOCB *inject_error(BlockDriverState *bs,
    BlockCompletionFunc *cb, void *opaque, BlkdebugRule *rule)
{
    BDRVBlkdebugState *s = bs->opaque;
    int error = rule->options.inject.error;
    struct BlkdebugAIOCB *acb;
    QEMUBH *bh;
    bool immediately = rule->options.inject.immediately;

    if (rule->options.inject.once) {
        QSIMPLEQ_REMOVE(&s->active_rules, rule, BlkdebugRule, active_next);
        remove_rule(rule);
    }

    if (immediately) {
        return NULL;
    }

    acb = qemu_aio_get(&blkdebug_aiocb_info, bs, cb, opaque);
    acb->ret = -error;

    bh = aio_bh_new(bdrv_get_aio_context(bs), error_callback_bh, acb);
    acb->bh = bh;
    qemu_bh_schedule(bh);

    return &acb->common;
}

static BlockAIOCB *blkdebug_aio_readv(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockCompletionFunc *cb, void *opaque)
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

    return bdrv_aio_readv(bs->file->bs, sector_num, qiov, nb_sectors,
                          cb, opaque);
}

static BlockAIOCB *blkdebug_aio_writev(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockCompletionFunc *cb, void *opaque)
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

    return bdrv_aio_writev(bs->file->bs, sector_num, qiov, nb_sectors,
                           cb, opaque);
}

static BlockAIOCB *blkdebug_aio_flush(BlockDriverState *bs,
    BlockCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugRule *rule = NULL;

    QSIMPLEQ_FOREACH(rule, &s->active_rules, active_next) {
        if (rule->options.inject.sector == -1) {
            break;
        }
    }

    if (rule && rule->options.inject.error) {
        return inject_error(bs, cb, opaque, rule);
    }

    return bdrv_aio_flush(bs->file->bs, cb, opaque);
}


static void blkdebug_close(BlockDriverState *bs)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugRule *rule, *next;
    int i;

    for (i = 0; i < BLKDBG__MAX; i++) {
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

    if (!qtest_enabled()) {
        printf("blkdebug: Suspended request '%s'\n", r.tag);
    }
    qemu_coroutine_yield();
    if (!qtest_enabled()) {
        printf("blkdebug: Resuming request '%s'\n", r.tag);
    }

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

static void blkdebug_debug_event(BlockDriverState *bs, BlkdebugEvent event)
{
    BDRVBlkdebugState *s = bs->opaque;
    struct BlkdebugRule *rule, *next;
    bool injected;

    assert((int)event >= 0 && event < BLKDBG__MAX);

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
    BlkdebugEvent blkdebug_event;

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

    for (i = 0; i < BLKDBG__MAX; i++) {
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
    return bdrv_getlength(bs->file->bs);
}

static int blkdebug_truncate(BlockDriverState *bs, int64_t offset)
{
    return bdrv_truncate(bs->file->bs, offset);
}

static void blkdebug_refresh_filename(BlockDriverState *bs, QDict *options)
{
    QDict *opts;
    const QDictEntry *e;
    bool force_json = false;

    for (e = qdict_first(options); e; e = qdict_next(options, e)) {
        if (strcmp(qdict_entry_key(e), "config") &&
            strcmp(qdict_entry_key(e), "x-image"))
        {
            force_json = true;
            break;
        }
    }

    if (force_json && !bs->file->bs->full_open_options) {
        /* The config file cannot be recreated, so creating a plain filename
         * is impossible */
        return;
    }

    if (!force_json && bs->file->bs->exact_filename[0]) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "blkdebug:%s:%s",
                 qdict_get_try_str(options, "config") ?: "",
                 bs->file->bs->exact_filename);
    }

    opts = qdict_new();
    qdict_put_obj(opts, "driver", QOBJECT(qstring_from_str("blkdebug")));

    QINCREF(bs->file->bs->full_open_options);
    qdict_put_obj(opts, "image", QOBJECT(bs->file->bs->full_open_options));

    for (e = qdict_first(options); e; e = qdict_next(options, e)) {
        if (strcmp(qdict_entry_key(e), "x-image")) {
            qobject_incref(qdict_entry_value(e));
            qdict_put_obj(opts, qdict_entry_key(e), qdict_entry_value(e));
        }
    }

    bs->full_open_options = opts;
}

static int blkdebug_reopen_prepare(BDRVReopenState *reopen_state,
                                   BlockReopenQueue *queue, Error **errp)
{
    return 0;
}

static BlockDriver bdrv_blkdebug = {
    .format_name            = "blkdebug",
    .protocol_name          = "blkdebug",
    .instance_size          = sizeof(BDRVBlkdebugState),

    .bdrv_parse_filename    = blkdebug_parse_filename,
    .bdrv_file_open         = blkdebug_open,
    .bdrv_close             = blkdebug_close,
    .bdrv_reopen_prepare    = blkdebug_reopen_prepare,
    .bdrv_getlength         = blkdebug_getlength,
    .bdrv_truncate          = blkdebug_truncate,
    .bdrv_refresh_filename  = blkdebug_refresh_filename,

    .bdrv_aio_readv         = blkdebug_aio_readv,
    .bdrv_aio_writev        = blkdebug_aio_writev,
    .bdrv_aio_flush         = blkdebug_aio_flush,

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
