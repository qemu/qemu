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
#include "block_int.h"
#include "module.h"

typedef struct BlkdebugVars {
    int state;

    /* If inject_errno != 0, an error is injected for requests */
    int inject_errno;

    /* Decides if all future requests fail (false) or only the next one and
     * after the next request inject_errno is reset to 0 (true) */
    bool inject_once;

    /* Decides if aio_readv/writev fails right away (true) or returns an error
     * return value only in the callback (false) */
    bool inject_immediately;
} BlkdebugVars;

typedef struct BDRVBlkdebugState {
    BlkdebugVars vars;
    QLIST_HEAD(list, BlkdebugRule) rules[BLKDBG_EVENT_MAX];
} BDRVBlkdebugState;

typedef struct BlkdebugAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    int ret;
} BlkdebugAIOCB;

static void blkdebug_aio_cancel(BlockDriverAIOCB *blockacb);

static AIOPool blkdebug_aio_pool = {
    .aiocb_size = sizeof(BlkdebugAIOCB),
    .cancel     = blkdebug_aio_cancel,
};

enum {
    ACTION_INJECT_ERROR,
    ACTION_SET_STATE,
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
        } inject;
        struct {
            int new_state;
        } set_state;
    } options;
    QLIST_ENTRY(BlkdebugRule) next;
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

    [BLKDBG_READ]                           = "read",
    [BLKDBG_READ_AIO]                       = "read_aio",
    [BLKDBG_READ_BACKING]                   = "read_backing",
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
        break;

    case ACTION_SET_STATE:
        rule->options.set_state.new_state =
            qemu_opt_get_number(opts, "new_state", 0);
        break;
    };

    /* Add the rule */
    QLIST_INSERT_HEAD(&s->rules[event], rule, next);

    return 0;
}

static int read_config(BDRVBlkdebugState *s, const char *filename)
{
    FILE *f;
    int ret;
    struct add_rule_data d;

    f = fopen(filename, "r");
    if (f == NULL) {
        return -errno;
    }

    ret = qemu_config_parse(f, config_groups, filename);
    if (ret < 0) {
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
    fclose(f);
    return ret;
}

/* Valid blkdebug filenames look like blkdebug:path/to/config:path/to/image */
static int blkdebug_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVBlkdebugState *s = bs->opaque;
    int ret;
    char *config, *c;

    /* Parse the blkdebug: prefix */
    if (strncmp(filename, "blkdebug:", strlen("blkdebug:"))) {
        return -EINVAL;
    }
    filename += strlen("blkdebug:");

    /* Read rules from config file */
    c = strchr(filename, ':');
    if (c == NULL) {
        return -EINVAL;
    }

    config = strdup(filename);
    config[c - filename] = '\0';
    ret = read_config(s, config);
    free(config);
    if (ret < 0) {
        return ret;
    }
    filename = c + 1;

    /* Set initial state */
    s->vars.state = 1;

    /* Open the backing file */
    ret = bdrv_file_open(&bs->file, filename, flags);
    if (ret < 0) {
        return ret;
    }

    return 0;
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
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;
    int error = s->vars.inject_errno;
    struct BlkdebugAIOCB *acb;
    QEMUBH *bh;

    if (s->vars.inject_once) {
        s->vars.inject_errno = 0;
    }

    if (s->vars.inject_immediately) {
        return NULL;
    }

    acb = qemu_aio_get(&blkdebug_aio_pool, bs, cb, opaque);
    acb->ret = -error;

    bh = qemu_bh_new(error_callback_bh, acb);
    acb->bh = bh;
    qemu_bh_schedule(bh);

    return &acb->common;
}

static BlockDriverAIOCB *blkdebug_aio_readv(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;

    if (s->vars.inject_errno) {
        return inject_error(bs, cb, opaque);
    }

    BlockDriverAIOCB *acb =
        bdrv_aio_readv(bs->file, sector_num, qiov, nb_sectors, cb, opaque);
    return acb;
}

static BlockDriverAIOCB *blkdebug_aio_writev(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;

    if (s->vars.inject_errno) {
        return inject_error(bs, cb, opaque);
    }

    BlockDriverAIOCB *acb =
        bdrv_aio_writev(bs->file, sector_num, qiov, nb_sectors, cb, opaque);
    return acb;
}

static void blkdebug_close(BlockDriverState *bs)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugRule *rule, *next;
    int i;

    for (i = 0; i < BLKDBG_EVENT_MAX; i++) {
        QLIST_FOREACH_SAFE(rule, &s->rules[i], next, next) {
            QLIST_REMOVE(rule, next);
            g_free(rule);
        }
    }
}

static BlockDriverAIOCB *blkdebug_aio_flush(BlockDriverState *bs,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_flush(bs->file, cb, opaque);
}

static void process_rule(BlockDriverState *bs, struct BlkdebugRule *rule,
    BlkdebugVars *old_vars)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlkdebugVars *vars = &s->vars;

    /* Only process rules for the current state */
    if (rule->state && rule->state != old_vars->state) {
        return;
    }

    /* Take the action */
    switch (rule->action) {
    case ACTION_INJECT_ERROR:
        vars->inject_errno       = rule->options.inject.error;
        vars->inject_once        = rule->options.inject.once;
        vars->inject_immediately = rule->options.inject.immediately;
        break;

    case ACTION_SET_STATE:
        vars->state              = rule->options.set_state.new_state;
        break;
    }
}

static void blkdebug_debug_event(BlockDriverState *bs, BlkDebugEvent event)
{
    BDRVBlkdebugState *s = bs->opaque;
    struct BlkdebugRule *rule;
    BlkdebugVars old_vars = s->vars;

    assert((int)event >= 0 && event < BLKDBG_EVENT_MAX);

    QLIST_FOREACH(rule, &s->rules[event], next) {
        process_rule(bs, rule, &old_vars);
    }
}

static BlockDriver bdrv_blkdebug = {
    .format_name        = "blkdebug",
    .protocol_name      = "blkdebug",

    .instance_size      = sizeof(BDRVBlkdebugState),

    .bdrv_file_open     = blkdebug_open,
    .bdrv_close         = blkdebug_close,

    .bdrv_aio_readv     = blkdebug_aio_readv,
    .bdrv_aio_writev    = blkdebug_aio_writev,
    .bdrv_aio_flush     = blkdebug_aio_flush,

    .bdrv_debug_event   = blkdebug_debug_event,
};

static void bdrv_blkdebug_init(void)
{
    bdrv_register(&bdrv_blkdebug);
}

block_init(bdrv_blkdebug_init);
