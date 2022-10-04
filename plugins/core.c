/*
 * QEMU Plugin Core code
 *
 * This is the core code that deals with injecting instrumentation into the code
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019, Linaro
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qapi/error.h"
#include "qemu/lockable.h"
#include "qemu/option.h"
#include "qemu/rcu_queue.h"
#include "qemu/xxhash.h"
#include "qemu/rcu.h"
#include "hw/core/cpu.h"
#include "exec/cpu-common.h"

#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "tcg/tcg.h"
#include "tcg/tcg-op.h"
#include "plugin.h"
#include "qemu/compiler.h"

struct qemu_plugin_cb {
    struct qemu_plugin_ctx *ctx;
    union qemu_plugin_cb_sig f;
    void *udata;
    QLIST_ENTRY(qemu_plugin_cb) entry;
};

struct qemu_plugin_state plugin;

struct qemu_plugin_ctx *plugin_id_to_ctx_locked(qemu_plugin_id_t id)
{
    struct qemu_plugin_ctx *ctx;
    qemu_plugin_id_t *id_p;

    id_p = g_hash_table_lookup(plugin.id_ht, &id);
    ctx = container_of(id_p, struct qemu_plugin_ctx, id);
    if (ctx == NULL) {
        error_report("plugin: invalid plugin id %" PRIu64, id);
        abort();
    }
    return ctx;
}

static void plugin_cpu_update__async(CPUState *cpu, run_on_cpu_data data)
{
    bitmap_copy(cpu->plugin_mask, &data.host_ulong, QEMU_PLUGIN_EV_MAX);
    tcg_flush_jmp_cache(cpu);
}

static void plugin_cpu_update__locked(gpointer k, gpointer v, gpointer udata)
{
    CPUState *cpu = container_of(k, CPUState, cpu_index);
    run_on_cpu_data mask = RUN_ON_CPU_HOST_ULONG(*plugin.mask);

    if (cpu->created) {
        async_run_on_cpu(cpu, plugin_cpu_update__async, mask);
    } else {
        plugin_cpu_update__async(cpu, mask);
    }
}

void plugin_unregister_cb__locked(struct qemu_plugin_ctx *ctx,
                                  enum qemu_plugin_event ev)
{
    struct qemu_plugin_cb *cb = ctx->callbacks[ev];

    if (cb == NULL) {
        return;
    }
    QLIST_REMOVE_RCU(cb, entry);
    g_free(cb);
    ctx->callbacks[ev] = NULL;
    if (QLIST_EMPTY_RCU(&plugin.cb_lists[ev])) {
        clear_bit(ev, plugin.mask);
        g_hash_table_foreach(plugin.cpu_ht, plugin_cpu_update__locked, NULL);
    }
}

/*
 * Disable CFI checks.
 * The callback function has been loaded from an external library so we do not
 * have type information
 */
QEMU_DISABLE_CFI
static void plugin_vcpu_cb__simple(CPUState *cpu, enum qemu_plugin_event ev)
{
    struct qemu_plugin_cb *cb, *next;

    switch (ev) {
    case QEMU_PLUGIN_EV_VCPU_INIT:
    case QEMU_PLUGIN_EV_VCPU_EXIT:
    case QEMU_PLUGIN_EV_VCPU_IDLE:
    case QEMU_PLUGIN_EV_VCPU_RESUME:
        /* iterate safely; plugins might uninstall themselves at any time */
        QLIST_FOREACH_SAFE_RCU(cb, &plugin.cb_lists[ev], entry, next) {
            qemu_plugin_vcpu_simple_cb_t func = cb->f.vcpu_simple;

            func(cb->ctx->id, cpu->cpu_index);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * Disable CFI checks.
 * The callback function has been loaded from an external library so we do not
 * have type information
 */
QEMU_DISABLE_CFI
static void plugin_cb__simple(enum qemu_plugin_event ev)
{
    struct qemu_plugin_cb *cb, *next;

    switch (ev) {
    case QEMU_PLUGIN_EV_FLUSH:
        QLIST_FOREACH_SAFE_RCU(cb, &plugin.cb_lists[ev], entry, next) {
            qemu_plugin_simple_cb_t func = cb->f.simple;

            func(cb->ctx->id);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * Disable CFI checks.
 * The callback function has been loaded from an external library so we do not
 * have type information
 */
QEMU_DISABLE_CFI
static void plugin_cb__udata(enum qemu_plugin_event ev)
{
    struct qemu_plugin_cb *cb, *next;

    switch (ev) {
    case QEMU_PLUGIN_EV_ATEXIT:
        QLIST_FOREACH_SAFE_RCU(cb, &plugin.cb_lists[ev], entry, next) {
            qemu_plugin_udata_cb_t func = cb->f.udata;

            func(cb->ctx->id, cb->udata);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void
do_plugin_register_cb(qemu_plugin_id_t id, enum qemu_plugin_event ev,
                      void *func, void *udata)
{
    struct qemu_plugin_ctx *ctx;

    QEMU_LOCK_GUARD(&plugin.lock);
    ctx = plugin_id_to_ctx_locked(id);
    /* if the plugin is on its way out, ignore this request */
    if (unlikely(ctx->uninstalling)) {
        return;
    }
    if (func) {
        struct qemu_plugin_cb *cb = ctx->callbacks[ev];

        if (cb) {
            cb->f.generic = func;
            cb->udata = udata;
        } else {
            cb = g_new(struct qemu_plugin_cb, 1);
            cb->ctx = ctx;
            cb->f.generic = func;
            cb->udata = udata;
            ctx->callbacks[ev] = cb;
            QLIST_INSERT_HEAD_RCU(&plugin.cb_lists[ev], cb, entry);
            if (!test_bit(ev, plugin.mask)) {
                set_bit(ev, plugin.mask);
                g_hash_table_foreach(plugin.cpu_ht, plugin_cpu_update__locked,
                                     NULL);
            }
        }
    } else {
        plugin_unregister_cb__locked(ctx, ev);
    }
}

void plugin_register_cb(qemu_plugin_id_t id, enum qemu_plugin_event ev,
                        void *func)
{
    do_plugin_register_cb(id, ev, func, NULL);
}

void
plugin_register_cb_udata(qemu_plugin_id_t id, enum qemu_plugin_event ev,
                         void *func, void *udata)
{
    do_plugin_register_cb(id, ev, func, udata);
}

void qemu_plugin_vcpu_init_hook(CPUState *cpu)
{
    bool success;

    qemu_rec_mutex_lock(&plugin.lock);
    plugin_cpu_update__locked(&cpu->cpu_index, NULL, NULL);
    success = g_hash_table_insert(plugin.cpu_ht, &cpu->cpu_index,
                                  &cpu->cpu_index);
    g_assert(success);
    qemu_rec_mutex_unlock(&plugin.lock);

    plugin_vcpu_cb__simple(cpu, QEMU_PLUGIN_EV_VCPU_INIT);
}

void qemu_plugin_vcpu_exit_hook(CPUState *cpu)
{
    bool success;

    plugin_vcpu_cb__simple(cpu, QEMU_PLUGIN_EV_VCPU_EXIT);

    qemu_rec_mutex_lock(&plugin.lock);
    success = g_hash_table_remove(plugin.cpu_ht, &cpu->cpu_index);
    g_assert(success);
    qemu_rec_mutex_unlock(&plugin.lock);
}

struct plugin_for_each_args {
    struct qemu_plugin_ctx *ctx;
    qemu_plugin_vcpu_simple_cb_t cb;
};

static void plugin_vcpu_for_each(gpointer k, gpointer v, gpointer udata)
{
    struct plugin_for_each_args *args = udata;
    int cpu_index = *(int *)k;

    args->cb(args->ctx->id, cpu_index);
}

void qemu_plugin_vcpu_for_each(qemu_plugin_id_t id,
                               qemu_plugin_vcpu_simple_cb_t cb)
{
    struct plugin_for_each_args args;

    if (cb == NULL) {
        return;
    }
    qemu_rec_mutex_lock(&plugin.lock);
    args.ctx = plugin_id_to_ctx_locked(id);
    args.cb = cb;
    g_hash_table_foreach(plugin.cpu_ht, plugin_vcpu_for_each, &args);
    qemu_rec_mutex_unlock(&plugin.lock);
}

/* Allocate and return a callback record */
static struct qemu_plugin_dyn_cb *plugin_get_dyn_cb(GArray **arr)
{
    GArray *cbs = *arr;

    if (!cbs) {
        cbs = g_array_sized_new(false, false,
                                sizeof(struct qemu_plugin_dyn_cb), 1);
        *arr = cbs;
    }

    g_array_set_size(cbs, cbs->len + 1);
    return &g_array_index(cbs, struct qemu_plugin_dyn_cb, cbs->len - 1);
}

void plugin_register_inline_op(GArray **arr,
                               enum qemu_plugin_mem_rw rw,
                               enum qemu_plugin_op op, void *ptr,
                               uint64_t imm)
{
    struct qemu_plugin_dyn_cb *dyn_cb;

    dyn_cb = plugin_get_dyn_cb(arr);
    dyn_cb->userp = ptr;
    dyn_cb->type = PLUGIN_CB_INLINE;
    dyn_cb->rw = rw;
    dyn_cb->inline_insn.op = op;
    dyn_cb->inline_insn.imm = imm;
}

void plugin_register_dyn_cb__udata(GArray **arr,
                                   qemu_plugin_vcpu_udata_cb_t cb,
                                   enum qemu_plugin_cb_flags flags,
                                   void *udata)
{
    struct qemu_plugin_dyn_cb *dyn_cb = plugin_get_dyn_cb(arr);

    dyn_cb->userp = udata;
    /* Note flags are discarded as unused. */
    dyn_cb->f.vcpu_udata = cb;
    dyn_cb->type = PLUGIN_CB_REGULAR;
}

void plugin_register_vcpu_mem_cb(GArray **arr,
                                 void *cb,
                                 enum qemu_plugin_cb_flags flags,
                                 enum qemu_plugin_mem_rw rw,
                                 void *udata)
{
    struct qemu_plugin_dyn_cb *dyn_cb;

    dyn_cb = plugin_get_dyn_cb(arr);
    dyn_cb->userp = udata;
    /* Note flags are discarded as unused. */
    dyn_cb->type = PLUGIN_CB_REGULAR;
    dyn_cb->rw = rw;
    dyn_cb->f.generic = cb;
}

/*
 * Disable CFI checks.
 * The callback function has been loaded from an external library so we do not
 * have type information
 */
QEMU_DISABLE_CFI
void qemu_plugin_tb_trans_cb(CPUState *cpu, struct qemu_plugin_tb *tb)
{
    struct qemu_plugin_cb *cb, *next;
    enum qemu_plugin_event ev = QEMU_PLUGIN_EV_VCPU_TB_TRANS;

    /* no plugin_mask check here; caller should have checked */

    QLIST_FOREACH_SAFE_RCU(cb, &plugin.cb_lists[ev], entry, next) {
        qemu_plugin_vcpu_tb_trans_cb_t func = cb->f.vcpu_tb_trans;

        func(cb->ctx->id, tb);
    }
}

/*
 * Disable CFI checks.
 * The callback function has been loaded from an external library so we do not
 * have type information
 */
QEMU_DISABLE_CFI
void
qemu_plugin_vcpu_syscall(CPUState *cpu, int64_t num, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5,
                         uint64_t a6, uint64_t a7, uint64_t a8)
{
    struct qemu_plugin_cb *cb, *next;
    enum qemu_plugin_event ev = QEMU_PLUGIN_EV_VCPU_SYSCALL;

    if (!test_bit(ev, cpu->plugin_mask)) {
        return;
    }

    QLIST_FOREACH_SAFE_RCU(cb, &plugin.cb_lists[ev], entry, next) {
        qemu_plugin_vcpu_syscall_cb_t func = cb->f.vcpu_syscall;

        func(cb->ctx->id, cpu->cpu_index, num, a1, a2, a3, a4, a5, a6, a7, a8);
    }
}

/*
 * Disable CFI checks.
 * The callback function has been loaded from an external library so we do not
 * have type information
 */
QEMU_DISABLE_CFI
void qemu_plugin_vcpu_syscall_ret(CPUState *cpu, int64_t num, int64_t ret)
{
    struct qemu_plugin_cb *cb, *next;
    enum qemu_plugin_event ev = QEMU_PLUGIN_EV_VCPU_SYSCALL_RET;

    if (!test_bit(ev, cpu->plugin_mask)) {
        return;
    }

    QLIST_FOREACH_SAFE_RCU(cb, &plugin.cb_lists[ev], entry, next) {
        qemu_plugin_vcpu_syscall_ret_cb_t func = cb->f.vcpu_syscall_ret;

        func(cb->ctx->id, cpu->cpu_index, num, ret);
    }
}

void qemu_plugin_vcpu_idle_cb(CPUState *cpu)
{
    plugin_vcpu_cb__simple(cpu, QEMU_PLUGIN_EV_VCPU_IDLE);
}

void qemu_plugin_vcpu_resume_cb(CPUState *cpu)
{
    plugin_vcpu_cb__simple(cpu, QEMU_PLUGIN_EV_VCPU_RESUME);
}

void qemu_plugin_register_vcpu_idle_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_IDLE, cb);
}

void qemu_plugin_register_vcpu_resume_cb(qemu_plugin_id_t id,
                                         qemu_plugin_vcpu_simple_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_RESUME, cb);
}

void qemu_plugin_register_flush_cb(qemu_plugin_id_t id,
                                   qemu_plugin_simple_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_FLUSH, cb);
}

static bool free_dyn_cb_arr(void *p, uint32_t h, void *userp)
{
    g_array_free((GArray *) p, true);
    return true;
}

void qemu_plugin_flush_cb(void)
{
    qht_iter_remove(&plugin.dyn_cb_arr_ht, free_dyn_cb_arr, NULL);
    qht_reset(&plugin.dyn_cb_arr_ht);

    plugin_cb__simple(QEMU_PLUGIN_EV_FLUSH);
}

void exec_inline_op(struct qemu_plugin_dyn_cb *cb)
{
    uint64_t *val = cb->userp;

    switch (cb->inline_insn.op) {
    case QEMU_PLUGIN_INLINE_ADD_U64:
        *val += cb->inline_insn.imm;
        break;
    default:
        g_assert_not_reached();
    }
}

void qemu_plugin_vcpu_mem_cb(CPUState *cpu, uint64_t vaddr,
                             MemOpIdx oi, enum qemu_plugin_mem_rw rw)
{
    GArray *arr = cpu->plugin_mem_cbs;
    size_t i;

    if (arr == NULL) {
        return;
    }
    for (i = 0; i < arr->len; i++) {
        struct qemu_plugin_dyn_cb *cb =
            &g_array_index(arr, struct qemu_plugin_dyn_cb, i);

        if (!(rw & cb->rw)) {
                break;
        }
        switch (cb->type) {
        case PLUGIN_CB_REGULAR:
            cb->f.vcpu_mem(cpu->cpu_index, make_plugin_meminfo(oi, rw),
                           vaddr, cb->userp);
            break;
        case PLUGIN_CB_INLINE:
            exec_inline_op(cb);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

void qemu_plugin_atexit_cb(void)
{
    plugin_cb__udata(QEMU_PLUGIN_EV_ATEXIT);
}

void qemu_plugin_register_atexit_cb(qemu_plugin_id_t id,
                                    qemu_plugin_udata_cb_t cb,
                                    void *udata)
{
    plugin_register_cb_udata(id, QEMU_PLUGIN_EV_ATEXIT, cb, udata);
}

/*
 * Handle exit from linux-user. Unlike the normal atexit() mechanism
 * we need to handle the clean-up manually as it's possible threads
 * are still running. We need to remove all callbacks from code
 * generation, flush the current translations and then we can safely
 * trigger the exit callbacks.
 */

void qemu_plugin_user_exit(void)
{
    enum qemu_plugin_event ev;
    CPUState *cpu;

    QEMU_LOCK_GUARD(&plugin.lock);

    start_exclusive();

    /* un-register all callbacks except the final AT_EXIT one */
    for (ev = 0; ev < QEMU_PLUGIN_EV_MAX; ev++) {
        if (ev != QEMU_PLUGIN_EV_ATEXIT) {
            struct qemu_plugin_ctx *ctx;
            QTAILQ_FOREACH(ctx, &plugin.ctxs, entry) {
                plugin_unregister_cb__locked(ctx, ev);
            }
        }
    }

    tb_flush(current_cpu);

    CPU_FOREACH(cpu) {
        qemu_plugin_disable_mem_helpers(cpu);
    }

    end_exclusive();

    /* now it's safe to handle the exit case */
    qemu_plugin_atexit_cb();
}

/*
 * Helpers for *-user to ensure locks are sane across fork() events.
 */

void qemu_plugin_user_prefork_lock(void)
{
    qemu_rec_mutex_lock(&plugin.lock);
}

void qemu_plugin_user_postfork(bool is_child)
{
    if (is_child) {
        /* should we just reset via plugin_init? */
        qemu_rec_mutex_init(&plugin.lock);
    } else {
        qemu_rec_mutex_unlock(&plugin.lock);
    }
}


/*
 * Call this function after longjmp'ing to the main loop. It's possible that the
 * last instruction of a TB might have used helpers, and therefore the
 * "disable" instruction will never execute because it ended up as dead code.
 */
void qemu_plugin_disable_mem_helpers(CPUState *cpu)
{
    cpu->plugin_mem_cbs = NULL;
}

static bool plugin_dyn_cb_arr_cmp(const void *ap, const void *bp)
{
    return ap == bp;
}

static void __attribute__((__constructor__)) plugin_init(void)
{
    int i;

    for (i = 0; i < QEMU_PLUGIN_EV_MAX; i++) {
        QLIST_INIT(&plugin.cb_lists[i]);
    }
    qemu_rec_mutex_init(&plugin.lock);
    plugin.id_ht = g_hash_table_new(g_int64_hash, g_int64_equal);
    plugin.cpu_ht = g_hash_table_new(g_int_hash, g_int_equal);
    QTAILQ_INIT(&plugin.ctxs);
    qht_init(&plugin.dyn_cb_arr_ht, plugin_dyn_cb_arr_cmp, 16,
             QHT_MODE_AUTO_RESIZE);
    atexit(qemu_plugin_atexit_cb);
}
