/*
 * Block protocol for I/O error injection
 *
 * Copyright (C) 2016-2017 Red Hat, Inc.
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
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/config-file.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "sysemu/qtest.h"

typedef struct BDRVBlkdebugState {
    int state;
    int new_state;
    uint64_t align;
    uint64_t max_transfer;
    uint64_t opt_write_zero;
    uint64_t max_write_zero;
    uint64_t opt_discard;
    uint64_t max_discard;

    /* For blkdebug_refresh_filename() */
    char *config_file;

    QLIST_HEAD(, BlkdebugRule) rules[BLKDBG__MAX];
    QSIMPLEQ_HEAD(, BlkdebugRule) active_rules;
    QLIST_HEAD(, BlkdebugSuspendedReq) suspended_reqs;
} BDRVBlkdebugState;

typedef struct BlkdebugAIOCB {
    BlockAIOCB common;
    int ret;
} BlkdebugAIOCB;

typedef struct BlkdebugSuspendedReq {
    Coroutine *co;
    char *tag;
    QLIST_ENTRY(BlkdebugSuspendedReq) next;
} BlkdebugSuspendedReq;

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
            uint64_t iotype_mask;
            int error;
            int immediately;
            int once;
            int64_t offset;
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

QEMU_BUILD_BUG_MSG(BLKDEBUG_IO_TYPE__MAX > 64,
                   "BlkdebugIOType mask does not fit into an uint64_t");

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
            .name = "iotype",
            .type = QEMU_OPT_STRING,
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

struct add_rule_data {
    BDRVBlkdebugState *s;
    int action;
};

static int add_rule(void *opaque, QemuOpts *opts, Error **errp)
{
    struct add_rule_data *d = opaque;
    BDRVBlkdebugState *s = d->s;
    const char* event_name;
    int event;
    struct BlkdebugRule *rule;
    int64_t sector;
    BlkdebugIOType iotype;
    Error *local_error = NULL;

    /* Find the right event for the rule */
    event_name = qemu_opt_get(opts, "event");
    if (!event_name) {
        error_setg(errp, "Missing event name for rule");
        return -1;
    }
    event = qapi_enum_parse(&BlkdebugEvent_lookup, event_name, -1, errp);
    if (event < 0) {
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
        sector = qemu_opt_get_number(opts, "sector", -1);
        rule->options.inject.offset =
            sector == -1 ? -1 : sector * BDRV_SECTOR_SIZE;

        iotype = qapi_enum_parse(&BlkdebugIOType_lookup,
                                 qemu_opt_get(opts, "iotype"),
                                 BLKDEBUG_IO_TYPE__MAX, &local_error);
        if (local_error) {
            error_propagate(errp, local_error);
            return -1;
        }
        if (iotype != BLKDEBUG_IO_TYPE__MAX) {
            rule->options.inject.iotype_mask = (1ull << iotype);
        } else {
            /* Apply the default */
            rule->options.inject.iotype_mask =
                (1ull << BLKDEBUG_IO_TYPE_READ)
                | (1ull << BLKDEBUG_IO_TYPE_WRITE)
                | (1ull << BLKDEBUG_IO_TYPE_WRITE_ZEROES)
                | (1ull << BLKDEBUG_IO_TYPE_DISCARD)
                | (1ull << BLKDEBUG_IO_TYPE_FLUSH);
        }

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
        qdict_put_str(options, "x-image", filename);
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
        config_path = qstring_from_substr(filename, 0, c - filename);
        qdict_put(options, "config", config_path);
    }

    /* TODO Allow multi-level nesting and set file.filename here */
    filename = c + 1;
    qdict_put_str(options, "x-image", filename);
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
        {
            .name = "max-transfer",
            .type = QEMU_OPT_SIZE,
            .help = "Maximum transfer size in bytes",
        },
        {
            .name = "opt-write-zero",
            .type = QEMU_OPT_SIZE,
            .help = "Optimum write zero alignment in bytes",
        },
        {
            .name = "max-write-zero",
            .type = QEMU_OPT_SIZE,
            .help = "Maximum write zero size in bytes",
        },
        {
            .name = "opt-discard",
            .type = QEMU_OPT_SIZE,
            .help = "Optimum discard alignment in bytes",
        },
        {
            .name = "max-discard",
            .type = QEMU_OPT_SIZE,
            .help = "Maximum discard size in bytes",
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
    int ret;
    uint64_t align;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }

    /* Read rules from config file or command line options */
    s->config_file = g_strdup(qemu_opt_get(opts, "config"));
    ret = read_config(s, s->config_file, options, errp);
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

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);
    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->file->bs->supported_zero_flags);
    ret = -EINVAL;

    /* Set alignment overrides */
    s->align = qemu_opt_get_size(opts, "align", 0);
    if (s->align && (s->align >= INT_MAX || !is_power_of_2(s->align))) {
        error_setg(errp, "Cannot meet constraints with align %" PRIu64,
                   s->align);
        goto out;
    }
    align = MAX(s->align, bs->file->bs->bl.request_alignment);

    s->max_transfer = qemu_opt_get_size(opts, "max-transfer", 0);
    if (s->max_transfer &&
        (s->max_transfer >= INT_MAX ||
         !QEMU_IS_ALIGNED(s->max_transfer, align))) {
        error_setg(errp, "Cannot meet constraints with max-transfer %" PRIu64,
                   s->max_transfer);
        goto out;
    }

    s->opt_write_zero = qemu_opt_get_size(opts, "opt-write-zero", 0);
    if (s->opt_write_zero &&
        (s->opt_write_zero >= INT_MAX ||
         !QEMU_IS_ALIGNED(s->opt_write_zero, align))) {
        error_setg(errp, "Cannot meet constraints with opt-write-zero %" PRIu64,
                   s->opt_write_zero);
        goto out;
    }

    s->max_write_zero = qemu_opt_get_size(opts, "max-write-zero", 0);
    if (s->max_write_zero &&
        (s->max_write_zero >= INT_MAX ||
         !QEMU_IS_ALIGNED(s->max_write_zero,
                          MAX(s->opt_write_zero, align)))) {
        error_setg(errp, "Cannot meet constraints with max-write-zero %" PRIu64,
                   s->max_write_zero);
        goto out;
    }

    s->opt_discard = qemu_opt_get_size(opts, "opt-discard", 0);
    if (s->opt_discard &&
        (s->opt_discard >= INT_MAX ||
         !QEMU_IS_ALIGNED(s->opt_discard, align))) {
        error_setg(errp, "Cannot meet constraints with opt-discard %" PRIu64,
                   s->opt_discard);
        goto out;
    }

    s->max_discard = qemu_opt_get_size(opts, "max-discard", 0);
    if (s->max_discard &&
        (s->max_discard >= INT_MAX ||
         !QEMU_IS_ALIGNED(s->max_discard,
                          MAX(s->opt_discard, align)))) {
        error_setg(errp, "Cannot meet constraints with max-discard %" PRIu64,
                   s->max_discard);
        goto out;
    }

    bdrv_debug_event(bs, BLKDBG_NONE);

    ret = 0;
out:
    if (ret < 0) {
        g_free(s->config_file);
    }
    qemu_opts_del(opts);
    return ret;
}

static int rule_check(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
                      BlkdebugIOType iotype)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugRule *rule = NULL;
    int error;
    bool immediately;

    QSIMPLEQ_FOREACH(rule, &s->active_rules, active_next) {
        uint64_t inject_offset = rule->options.inject.offset;

        if ((inject_offset == -1 ||
             (bytes && inject_offset >= offset &&
              inject_offset < offset + bytes)) &&
            (rule->options.inject.iotype_mask & (1ull << iotype)))
        {
            break;
        }
    }

    if (!rule || !rule->options.inject.error) {
        return 0;
    }

    immediately = rule->options.inject.immediately;
    error = rule->options.inject.error;

    if (rule->options.inject.once) {
        QSIMPLEQ_REMOVE(&s->active_rules, rule, BlkdebugRule, active_next);
        remove_rule(rule);
    }

    if (!immediately) {
        aio_co_schedule(qemu_get_current_aio_context(), qemu_coroutine_self());
        qemu_coroutine_yield();
    }

    return -error;
}

static int coroutine_fn
blkdebug_co_preadv(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
                   QEMUIOVector *qiov, int flags)
{
    int err;

    /* Sanity check block layer guarantees */
    assert(QEMU_IS_ALIGNED(offset, bs->bl.request_alignment));
    assert(QEMU_IS_ALIGNED(bytes, bs->bl.request_alignment));
    if (bs->bl.max_transfer) {
        assert(bytes <= bs->bl.max_transfer);
    }

    err = rule_check(bs, offset, bytes, BLKDEBUG_IO_TYPE_READ);
    if (err) {
        return err;
    }

    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn
blkdebug_co_pwritev(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
                    QEMUIOVector *qiov, int flags)
{
    int err;

    /* Sanity check block layer guarantees */
    assert(QEMU_IS_ALIGNED(offset, bs->bl.request_alignment));
    assert(QEMU_IS_ALIGNED(bytes, bs->bl.request_alignment));
    if (bs->bl.max_transfer) {
        assert(bytes <= bs->bl.max_transfer);
    }

    err = rule_check(bs, offset, bytes, BLKDEBUG_IO_TYPE_WRITE);
    if (err) {
        return err;
    }

    return bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
}

static int blkdebug_co_flush(BlockDriverState *bs)
{
    int err = rule_check(bs, 0, 0, BLKDEBUG_IO_TYPE_FLUSH);

    if (err) {
        return err;
    }

    return bdrv_co_flush(bs->file->bs);
}

static int coroutine_fn blkdebug_co_pwrite_zeroes(BlockDriverState *bs,
                                                  int64_t offset, int bytes,
                                                  BdrvRequestFlags flags)
{
    uint32_t align = MAX(bs->bl.request_alignment,
                         bs->bl.pwrite_zeroes_alignment);
    int err;

    /* Only pass through requests that are larger than requested
     * preferred alignment (so that we test the fallback to writes on
     * unaligned portions), and check that the block layer never hands
     * us anything unaligned that crosses an alignment boundary.  */
    if (bytes < align) {
        assert(QEMU_IS_ALIGNED(offset, align) ||
               QEMU_IS_ALIGNED(offset + bytes, align) ||
               DIV_ROUND_UP(offset, align) ==
               DIV_ROUND_UP(offset + bytes, align));
        return -ENOTSUP;
    }
    assert(QEMU_IS_ALIGNED(offset, align));
    assert(QEMU_IS_ALIGNED(bytes, align));
    if (bs->bl.max_pwrite_zeroes) {
        assert(bytes <= bs->bl.max_pwrite_zeroes);
    }

    err = rule_check(bs, offset, bytes, BLKDEBUG_IO_TYPE_WRITE_ZEROES);
    if (err) {
        return err;
    }

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static int coroutine_fn blkdebug_co_pdiscard(BlockDriverState *bs,
                                             int64_t offset, int bytes)
{
    uint32_t align = bs->bl.pdiscard_alignment;
    int err;

    /* Only pass through requests that are larger than requested
     * minimum alignment, and ensure that unaligned requests do not
     * cross optimum discard boundaries. */
    if (bytes < bs->bl.request_alignment) {
        assert(QEMU_IS_ALIGNED(offset, align) ||
               QEMU_IS_ALIGNED(offset + bytes, align) ||
               DIV_ROUND_UP(offset, align) ==
               DIV_ROUND_UP(offset + bytes, align));
        return -ENOTSUP;
    }
    assert(QEMU_IS_ALIGNED(offset, bs->bl.request_alignment));
    assert(QEMU_IS_ALIGNED(bytes, bs->bl.request_alignment));
    if (align && bytes >= align) {
        assert(QEMU_IS_ALIGNED(offset, align));
        assert(QEMU_IS_ALIGNED(bytes, align));
    }
    if (bs->bl.max_pdiscard) {
        assert(bytes <= bs->bl.max_pdiscard);
    }

    err = rule_check(bs, offset, bytes, BLKDEBUG_IO_TYPE_DISCARD);
    if (err) {
        return err;
    }

    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static int coroutine_fn blkdebug_co_block_status(BlockDriverState *bs,
                                                 bool want_zero,
                                                 int64_t offset,
                                                 int64_t bytes,
                                                 int64_t *pnum,
                                                 int64_t *map,
                                                 BlockDriverState **file)
{
    int err;

    assert(QEMU_IS_ALIGNED(offset | bytes, bs->bl.request_alignment));

    err = rule_check(bs, offset, bytes, BLKDEBUG_IO_TYPE_BLOCK_STATUS);
    if (err) {
        return err;
    }

    return bdrv_co_block_status_from_file(bs, want_zero, offset, bytes,
                                          pnum, map, file);
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

    g_free(s->config_file);
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
    int blkdebug_event;

    blkdebug_event = qapi_enum_parse(&BlkdebugEvent_lookup, event, -1, NULL);
    if (blkdebug_event < 0) {
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
            qemu_coroutine_enter(r->co);
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
            qemu_coroutine_enter(r->co);
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

static void blkdebug_refresh_filename(BlockDriverState *bs)
{
    BDRVBlkdebugState *s = bs->opaque;
    const QDictEntry *e;
    int ret;

    if (!bs->file->bs->exact_filename[0]) {
        return;
    }

    for (e = qdict_first(bs->full_open_options); e;
         e = qdict_next(bs->full_open_options, e))
    {
        /* Real child options are under "image", but "x-image" may
         * contain a filename */
        if (strcmp(qdict_entry_key(e), "config") &&
            strcmp(qdict_entry_key(e), "image") &&
            strcmp(qdict_entry_key(e), "x-image") &&
            strcmp(qdict_entry_key(e), "driver"))
        {
            return;
        }
    }

    ret = snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                   "blkdebug:%s:%s",
                   s->config_file ?: "", bs->file->bs->exact_filename);
    if (ret >= sizeof(bs->exact_filename)) {
        /* An overflow makes the filename unusable, so do not report any */
        bs->exact_filename[0] = 0;
    }
}

static void blkdebug_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVBlkdebugState *s = bs->opaque;

    if (s->align) {
        bs->bl.request_alignment = s->align;
    }
    if (s->max_transfer) {
        bs->bl.max_transfer = s->max_transfer;
    }
    if (s->opt_write_zero) {
        bs->bl.pwrite_zeroes_alignment = s->opt_write_zero;
    }
    if (s->max_write_zero) {
        bs->bl.max_pwrite_zeroes = s->max_write_zero;
    }
    if (s->opt_discard) {
        bs->bl.pdiscard_alignment = s->opt_discard;
    }
    if (s->max_discard) {
        bs->bl.max_pdiscard = s->max_discard;
    }
}

static int blkdebug_reopen_prepare(BDRVReopenState *reopen_state,
                                   BlockReopenQueue *queue, Error **errp)
{
    return 0;
}

static const char *const blkdebug_strong_runtime_opts[] = {
    "config",
    "inject-error.",
    "set-state.",
    "align",
    "max-transfer",
    "opt-write-zero",
    "max-write-zero",
    "opt-discard",
    "max-discard",

    NULL
};

static BlockDriver bdrv_blkdebug = {
    .format_name            = "blkdebug",
    .protocol_name          = "blkdebug",
    .instance_size          = sizeof(BDRVBlkdebugState),
    .is_filter              = true,

    .bdrv_parse_filename    = blkdebug_parse_filename,
    .bdrv_file_open         = blkdebug_open,
    .bdrv_close             = blkdebug_close,
    .bdrv_reopen_prepare    = blkdebug_reopen_prepare,
    .bdrv_child_perm        = bdrv_filter_default_perms,

    .bdrv_getlength         = blkdebug_getlength,
    .bdrv_refresh_filename  = blkdebug_refresh_filename,
    .bdrv_refresh_limits    = blkdebug_refresh_limits,

    .bdrv_co_preadv         = blkdebug_co_preadv,
    .bdrv_co_pwritev        = blkdebug_co_pwritev,
    .bdrv_co_flush_to_disk  = blkdebug_co_flush,
    .bdrv_co_pwrite_zeroes  = blkdebug_co_pwrite_zeroes,
    .bdrv_co_pdiscard       = blkdebug_co_pdiscard,
    .bdrv_co_block_status   = blkdebug_co_block_status,

    .bdrv_debug_event           = blkdebug_debug_event,
    .bdrv_debug_breakpoint      = blkdebug_debug_breakpoint,
    .bdrv_debug_remove_breakpoint
                                = blkdebug_debug_remove_breakpoint,
    .bdrv_debug_resume          = blkdebug_debug_resume,
    .bdrv_debug_is_suspended    = blkdebug_debug_is_suspended,

    .strong_runtime_opts        = blkdebug_strong_runtime_opts,
};

static void bdrv_blkdebug_init(void)
{
    bdrv_register(&bdrv_blkdebug);
}

block_init(bdrv_blkdebug_init);
