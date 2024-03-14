/*
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#ifndef QEMU_PLUGIN_H
#define QEMU_PLUGIN_H

#include "qemu/config-file.h"
#include "qemu/qemu-plugin.h"
#include "qemu/error-report.h"
#include "qemu/queue.h"
#include "qemu/option.h"
#include "qemu/plugin-event.h"
#include "qemu/bitmap.h"
#include "exec/memopidx.h"
#include "hw/core/cpu.h"

/*
 * Option parsing/processing.
 * Note that we can load an arbitrary number of plugins.
 */
struct qemu_plugin_desc;
typedef QTAILQ_HEAD(, qemu_plugin_desc) QemuPluginList;

/*
 * Construct a qemu_plugin_meminfo_t.
 */
static inline qemu_plugin_meminfo_t
make_plugin_meminfo(MemOpIdx oi, enum qemu_plugin_mem_rw rw)
{
    return oi | (rw << 16);
}

/*
 * Extract the memory operation direction from a qemu_plugin_meminfo_t.
 * Other portions may be extracted via get_memop and get_mmuidx.
 */
static inline enum qemu_plugin_mem_rw
get_plugin_meminfo_rw(qemu_plugin_meminfo_t i)
{
    return i >> 16;
}

#ifdef CONFIG_PLUGIN
extern QemuOptsList qemu_plugin_opts;

static inline void qemu_plugin_add_opts(void)
{
    qemu_add_opts(&qemu_plugin_opts);
}

void qemu_plugin_opt_parse(const char *optstr, QemuPluginList *head);
int qemu_plugin_load_list(QemuPluginList *head, Error **errp);

union qemu_plugin_cb_sig {
    qemu_plugin_simple_cb_t          simple;
    qemu_plugin_udata_cb_t           udata;
    qemu_plugin_vcpu_simple_cb_t     vcpu_simple;
    qemu_plugin_vcpu_udata_cb_t      vcpu_udata;
    qemu_plugin_vcpu_tb_trans_cb_t   vcpu_tb_trans;
    qemu_plugin_vcpu_mem_cb_t        vcpu_mem;
    qemu_plugin_vcpu_syscall_cb_t    vcpu_syscall;
    qemu_plugin_vcpu_syscall_ret_cb_t vcpu_syscall_ret;
    void *generic;
};

enum plugin_dyn_cb_type {
    PLUGIN_CB_INSN,
    PLUGIN_CB_MEM,
    PLUGIN_N_CB_TYPES,
};

enum plugin_dyn_cb_subtype {
    PLUGIN_CB_REGULAR,
    PLUGIN_CB_INLINE,
    PLUGIN_N_CB_SUBTYPES,
};

/*
 * A dynamic callback has an insertion point that is determined at run-time.
 * Usually the insertion point is somewhere in the code cache; think for
 * instance of a callback to be called upon the execution of a particular TB.
 */
struct qemu_plugin_dyn_cb {
    void *userp;
    enum plugin_dyn_cb_subtype type;
    /* @rw applies to mem callbacks only (both regular and inline) */
    enum qemu_plugin_mem_rw rw;
    /* fields specific to each dyn_cb type go here */
    union {
        struct {
            union qemu_plugin_cb_sig f;
            TCGHelperInfo *info;
        } regular;
        struct {
            qemu_plugin_u64 entry;
            enum qemu_plugin_op op;
            uint64_t imm;
        } inline_insn;
    };
};

/* Internal context for instrumenting an instruction */
struct qemu_plugin_insn {
    GByteArray *data;
    uint64_t vaddr;
    void *haddr;
    GArray *cbs[PLUGIN_N_CB_TYPES][PLUGIN_N_CB_SUBTYPES];
    bool calls_helpers;

    /* if set, the instruction calls helpers that might access guest memory */
    bool mem_helper;

    bool mem_only;
};

/* A scoreboard is an array of values, indexed by vcpu_index */
struct qemu_plugin_scoreboard {
    GArray *data;
    QLIST_ENTRY(qemu_plugin_scoreboard) entry;
};

/*
 * qemu_plugin_insn allocate and cleanup functions. We don't expect to
 * cleanup many of these structures. They are reused for each fresh
 * translation.
 */

static inline void qemu_plugin_insn_cleanup_fn(gpointer data)
{
    struct qemu_plugin_insn *insn = (struct qemu_plugin_insn *) data;
    g_byte_array_free(insn->data, true);
}

static inline struct qemu_plugin_insn *qemu_plugin_insn_alloc(void)
{
    int i, j;
    struct qemu_plugin_insn *insn = g_new0(struct qemu_plugin_insn, 1);
    insn->data = g_byte_array_sized_new(4);

    for (i = 0; i < PLUGIN_N_CB_TYPES; i++) {
        for (j = 0; j < PLUGIN_N_CB_SUBTYPES; j++) {
            insn->cbs[i][j] = g_array_new(false, false,
                                          sizeof(struct qemu_plugin_dyn_cb));
        }
    }
    return insn;
}

/* Internal context for this TranslationBlock */
struct qemu_plugin_tb {
    GPtrArray *insns;
    size_t n;
    uint64_t vaddr;
    uint64_t vaddr2;
    void *haddr1;
    void *haddr2;
    bool mem_only;

    /* if set, the TB calls helpers that might access guest memory */
    bool mem_helper;

    GArray *cbs[PLUGIN_N_CB_SUBTYPES];
};

/**
 * qemu_plugin_tb_insn_get(): get next plugin record for translation.
 * @tb: the internal tb context
 * @pc: address of instruction
 */
static inline
struct qemu_plugin_insn *qemu_plugin_tb_insn_get(struct qemu_plugin_tb *tb,
                                                 uint64_t pc)
{
    struct qemu_plugin_insn *insn;
    int i, j;

    if (unlikely(tb->n == tb->insns->len)) {
        struct qemu_plugin_insn *new_insn = qemu_plugin_insn_alloc();
        g_ptr_array_add(tb->insns, new_insn);
    }
    insn = g_ptr_array_index(tb->insns, tb->n++);
    g_byte_array_set_size(insn->data, 0);
    insn->calls_helpers = false;
    insn->mem_helper = false;
    insn->vaddr = pc;

    for (i = 0; i < PLUGIN_N_CB_TYPES; i++) {
        for (j = 0; j < PLUGIN_N_CB_SUBTYPES; j++) {
            g_array_set_size(insn->cbs[i][j], 0);
        }
    }

    return insn;
}

/**
 * struct CPUPluginState - per-CPU state for plugins
 * @event_mask: plugin event bitmap. Modified only via async work.
 */
struct CPUPluginState {
    DECLARE_BITMAP(event_mask, QEMU_PLUGIN_EV_MAX);
};

/**
 * qemu_plugin_create_vcpu_state: allocate plugin state
 */
CPUPluginState *qemu_plugin_create_vcpu_state(void);

void qemu_plugin_vcpu_init_hook(CPUState *cpu);
void qemu_plugin_vcpu_exit_hook(CPUState *cpu);
void qemu_plugin_tb_trans_cb(CPUState *cpu, struct qemu_plugin_tb *tb);
void qemu_plugin_vcpu_idle_cb(CPUState *cpu);
void qemu_plugin_vcpu_resume_cb(CPUState *cpu);
void
qemu_plugin_vcpu_syscall(CPUState *cpu, int64_t num, uint64_t a1,
                         uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5,
                         uint64_t a6, uint64_t a7, uint64_t a8);
void qemu_plugin_vcpu_syscall_ret(CPUState *cpu, int64_t num, int64_t ret);

void qemu_plugin_vcpu_mem_cb(CPUState *cpu, uint64_t vaddr,
                             MemOpIdx oi, enum qemu_plugin_mem_rw rw);

void qemu_plugin_flush_cb(void);

void qemu_plugin_atexit_cb(void);

void qemu_plugin_add_dyn_cb_arr(GArray *arr);

static inline void qemu_plugin_disable_mem_helpers(CPUState *cpu)
{
    cpu->plugin_mem_cbs = NULL;
}

/**
 * qemu_plugin_user_exit(): clean-up callbacks before calling exit callbacks
 *
 * This is a user-mode only helper that ensure we have fully cleared
 * callbacks from all threads before calling the exit callbacks. This
 * is so the plugins themselves don't have to jump through hoops to
 * guard against race conditions.
 */
void qemu_plugin_user_exit(void);

/**
 * qemu_plugin_user_prefork_lock(): take plugin lock before forking
 *
 * This is a user-mode only helper to take the internal plugin lock
 * before a fork event. This is ensure a consistent lock state
 */
void qemu_plugin_user_prefork_lock(void);

/**
 * qemu_plugin_user_postfork(): reset the plugin lock
 * @is_child: is this thread the child
 *
 * This user-mode only helper resets the lock state after a fork so we
 * can continue using the plugin interface.
 */
void qemu_plugin_user_postfork(bool is_child);

#else /* !CONFIG_PLUGIN */

static inline void qemu_plugin_add_opts(void)
{ }

static inline void qemu_plugin_opt_parse(const char *optstr,
                                         QemuPluginList *head)
{
    error_report("plugin interface not enabled in this build");
    exit(1);
}

static inline int qemu_plugin_load_list(QemuPluginList *head, Error **errp)
{
    return 0;
}

static inline void qemu_plugin_vcpu_init_hook(CPUState *cpu)
{ }

static inline void qemu_plugin_vcpu_exit_hook(CPUState *cpu)
{ }

static inline void qemu_plugin_tb_trans_cb(CPUState *cpu,
                                           struct qemu_plugin_tb *tb)
{ }

static inline void qemu_plugin_vcpu_idle_cb(CPUState *cpu)
{ }

static inline void qemu_plugin_vcpu_resume_cb(CPUState *cpu)
{ }

static inline void
qemu_plugin_vcpu_syscall(CPUState *cpu, int64_t num, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                         uint64_t a7, uint64_t a8)
{ }

static inline
void qemu_plugin_vcpu_syscall_ret(CPUState *cpu, int64_t num, int64_t ret)
{ }

static inline void qemu_plugin_vcpu_mem_cb(CPUState *cpu, uint64_t vaddr,
                                           MemOpIdx oi,
                                           enum qemu_plugin_mem_rw rw)
{ }

static inline void qemu_plugin_flush_cb(void)
{ }

static inline void qemu_plugin_atexit_cb(void)
{ }

static inline
void qemu_plugin_add_dyn_cb_arr(GArray *arr)
{ }

static inline void qemu_plugin_disable_mem_helpers(CPUState *cpu)
{ }

static inline void qemu_plugin_user_exit(void)
{ }

static inline void qemu_plugin_user_prefork_lock(void)
{ }

static inline void qemu_plugin_user_postfork(bool is_child)
{ }

#endif /* !CONFIG_PLUGIN */

#endif /* QEMU_PLUGIN_H */
