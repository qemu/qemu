/*
 * plugin-gen.c - TCG-related bits of plugin infrastructure
 *
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * We support instrumentation at an instruction granularity. That is,
 * if a plugin wants to instrument the memory accesses performed by a
 * particular instruction, it can just do that instead of instrumenting
 * all memory accesses. Thus, in order to do this we first have to
 * translate a TB, so that plugins can decide what/where to instrument.
 *
 * Injecting the desired instrumentation could be done with a second
 * translation pass that combined the instrumentation requests, but that
 * would be ugly and inefficient since we would decode the guest code twice.
 * Instead, during TB translation we add "plugin_cb" marker opcodes
 * for all possible instrumentation events, and then once we collect the
 * instrumentation requests from plugins, we generate code for those markers
 * or remove them if they have no requests.
 */
#include "qemu/osdep.h"
#include "qemu/plugin.h"
#include "qemu/log.h"
#include "tcg/tcg.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg/tcg-op-common.h"
#include "exec/plugin-gen.h"
#include "exec/translator.h"
#include "exec/translation-block.h"

enum plugin_gen_from {
    PLUGIN_GEN_FROM_TB,
    PLUGIN_GEN_FROM_INSN,
    PLUGIN_GEN_AFTER_INSN,
    PLUGIN_GEN_AFTER_TB,
};

/* called before finishing a TB with exit_tb, goto_tb or goto_ptr */
void plugin_gen_disable_mem_helpers(void)
{
    if (tcg_ctx->plugin_insn) {
        tcg_gen_plugin_cb(PLUGIN_GEN_AFTER_TB);
    }
}

static void gen_enable_mem_helper(struct qemu_plugin_tb *ptb,
                                  struct qemu_plugin_insn *insn)
{
    GArray *arr;
    size_t len;

    /*
     * Tracking memory accesses performed from helpers requires extra work.
     * If an instruction is emulated with helpers, we do two things:
     * (1) copy the CB descriptors, and keep track of it so that they can be
     * freed later on, and (2) point CPUState.neg.plugin_mem_cbs to the
     * descriptors, so that we can read them at run-time
     * (i.e. when the helper executes).
     * This run-time access is performed from qemu_plugin_vcpu_mem_cb.
     *
     * Note that plugin_gen_disable_mem_helpers undoes (2). Since it
     * is possible that the code we generate after the instruction is
     * dead, we also add checks before generating tb_exit etc.
     */
    if (!insn->calls_helpers) {
        return;
    }

    if (!insn->mem_cbs || !insn->mem_cbs->len) {
        insn->mem_helper = false;
        return;
    }
    insn->mem_helper = true;
    ptb->mem_helper = true;

    /*
     * TODO: It seems like we should be able to use ref/unref
     * to avoid needing to actually copy this array.
     * Alternately, perhaps we could allocate new memory adjacent
     * to the TranslationBlock itself, so that we do not have to
     * actively manage the lifetime after this.
     */
    len = insn->mem_cbs->len;
    arr = g_array_sized_new(false, false,
                            sizeof(struct qemu_plugin_dyn_cb), len);
    g_array_append_vals(arr, insn->mem_cbs->data, len);
    qemu_plugin_add_dyn_cb_arr(arr);

    tcg_gen_st_ptr(tcg_constant_ptr((intptr_t)arr), tcg_env,
                   offsetof(CPUState, neg.plugin_mem_cbs) - sizeof(CPUState));
}

static void gen_disable_mem_helper(void)
{
    tcg_gen_st_ptr(tcg_constant_ptr(0), tcg_env,
                   offsetof(CPUState, neg.plugin_mem_cbs) - sizeof(CPUState));
}

static TCGv_i32 gen_cpu_index(void)
{
    /*
     * Optimize when we run with a single vcpu. All values using cpu_index,
     * including scoreboard index, will be optimized out.
     * User-mode flushes all TBs when setting this flag.
     * In system-mode, all vcpus are created before generating code.
     */
    if (!tcg_cflags_has(current_cpu, CF_PARALLEL)) {
        return tcg_constant_i32(current_cpu->cpu_index);
    }
    TCGv_i32 cpu_index = tcg_temp_ebb_new_i32();
    tcg_gen_ld_i32(cpu_index, tcg_env,
                   offsetof(CPUState, cpu_index) - sizeof(CPUState));
    return cpu_index;
}

static void gen_udata_cb(struct qemu_plugin_regular_cb *cb)
{
    TCGv_i32 cpu_index = gen_cpu_index();
    enum qemu_plugin_cb_flags cb_flags =
        tcg_call_to_qemu_plugin_cb_flags(cb->info->flags);
    TCGv_i32 flags = tcg_constant_i32(cb_flags);
    TCGv_i32 clear_flags = tcg_constant_i32(QEMU_PLUGIN_CB_NO_REGS);
    tcg_gen_st_i32(flags, tcg_env,
           offsetof(CPUState, neg.plugin_cb_flags) - sizeof(CPUState));
    tcg_gen_call2(cb->f.vcpu_udata, cb->info, NULL,
                  tcgv_i32_temp(cpu_index),
                  tcgv_ptr_temp(tcg_constant_ptr(cb->userp)));
    tcg_gen_st_i32(clear_flags, tcg_env,
           offsetof(CPUState, neg.plugin_cb_flags) - sizeof(CPUState));
    tcg_temp_free_i32(cpu_index);
    tcg_temp_free_i32(flags);
    tcg_temp_free_i32(clear_flags);
}

static TCGv_ptr gen_plugin_u64_ptr(qemu_plugin_u64 entry)
{
    TCGv_ptr ptr = tcg_temp_ebb_new_ptr();

    GArray *arr = entry.score->data;
    char *base_ptr = arr->data + entry.offset;
    size_t entry_size = g_array_get_element_size(arr);

    TCGv_i32 cpu_index = gen_cpu_index();
    tcg_gen_muli_i32(cpu_index, cpu_index, entry_size);
    tcg_gen_ext_i32_ptr(ptr, cpu_index);
    tcg_temp_free_i32(cpu_index);
    tcg_gen_addi_ptr(ptr, ptr, (intptr_t) base_ptr);

    return ptr;
}

static TCGCond plugin_cond_to_tcgcond(enum qemu_plugin_cond cond)
{
    switch (cond) {
    case QEMU_PLUGIN_COND_EQ:
        return TCG_COND_EQ;
    case QEMU_PLUGIN_COND_NE:
        return TCG_COND_NE;
    case QEMU_PLUGIN_COND_LT:
        return TCG_COND_LTU;
    case QEMU_PLUGIN_COND_LE:
        return TCG_COND_LEU;
    case QEMU_PLUGIN_COND_GT:
        return TCG_COND_GTU;
    case QEMU_PLUGIN_COND_GE:
        return TCG_COND_GEU;
    default:
        /* ALWAYS and NEVER conditions should never reach */
        g_assert_not_reached();
    }
}

static void gen_udata_cond_cb(struct qemu_plugin_conditional_cb *cb)
{
    TCGv_ptr ptr = gen_plugin_u64_ptr(cb->entry);
    TCGv_i64 val = tcg_temp_ebb_new_i64();
    TCGLabel *after_cb = gen_new_label();

    /* Condition should be negated, as calling the cb is the "else" path */
    TCGCond cond = tcg_invert_cond(plugin_cond_to_tcgcond(cb->cond));

    tcg_gen_ld_i64(val, ptr, 0);
    tcg_gen_brcondi_i64(cond, val, cb->imm, after_cb);
    TCGv_i32 cpu_index = gen_cpu_index();
    enum qemu_plugin_cb_flags cb_flags =
        tcg_call_to_qemu_plugin_cb_flags(cb->info->flags);
    TCGv_i32 flags = tcg_constant_i32(cb_flags);
    TCGv_i32 clear_flags = tcg_constant_i32(QEMU_PLUGIN_CB_NO_REGS);
    tcg_gen_st_i32(flags, tcg_env,
           offsetof(CPUState, neg.plugin_cb_flags) - sizeof(CPUState));
    tcg_gen_call2(cb->f.vcpu_udata, cb->info, NULL,
                  tcgv_i32_temp(cpu_index),
                  tcgv_ptr_temp(tcg_constant_ptr(cb->userp)));
    tcg_gen_st_i32(clear_flags, tcg_env,
           offsetof(CPUState, neg.plugin_cb_flags) - sizeof(CPUState));
    tcg_temp_free_i32(cpu_index);
    tcg_temp_free_i32(flags);
    tcg_temp_free_i32(clear_flags);
    gen_set_label(after_cb);

    tcg_temp_free_i64(val);
    tcg_temp_free_ptr(ptr);
}

static void gen_inline_add_u64_cb(struct qemu_plugin_inline_cb *cb)
{
    TCGv_ptr ptr = gen_plugin_u64_ptr(cb->entry);
    TCGv_i64 val = tcg_temp_ebb_new_i64();

    tcg_gen_ld_i64(val, ptr, 0);
    tcg_gen_addi_i64(val, val, cb->imm);
    tcg_gen_st_i64(val, ptr, 0);

    tcg_temp_free_i64(val);
    tcg_temp_free_ptr(ptr);
}

static void gen_inline_store_u64_cb(struct qemu_plugin_inline_cb *cb)
{
    TCGv_ptr ptr = gen_plugin_u64_ptr(cb->entry);
    TCGv_i64 val = tcg_constant_i64(cb->imm);

    tcg_gen_st_i64(val, ptr, 0);

    tcg_temp_free_ptr(ptr);
}

static void gen_mem_cb(struct qemu_plugin_regular_cb *cb,
                       qemu_plugin_meminfo_t meminfo, TCGv_i64 addr)
{
    TCGv_i32 cpu_index = gen_cpu_index();
    enum qemu_plugin_cb_flags cb_flags =
        tcg_call_to_qemu_plugin_cb_flags(cb->info->flags);
    TCGv_i32 flags = tcg_constant_i32(cb_flags);
    TCGv_i32 clear_flags = tcg_constant_i32(QEMU_PLUGIN_CB_NO_REGS);
    tcg_gen_st_i32(flags, tcg_env,
           offsetof(CPUState, neg.plugin_cb_flags) - sizeof(CPUState));
    tcg_gen_call4(cb->f.vcpu_mem, cb->info, NULL,
                  tcgv_i32_temp(cpu_index),
                  tcgv_i32_temp(tcg_constant_i32(meminfo)),
                  tcgv_i64_temp(addr),
                  tcgv_ptr_temp(tcg_constant_ptr(cb->userp)));
    tcg_gen_st_i32(clear_flags, tcg_env,
           offsetof(CPUState, neg.plugin_cb_flags) - sizeof(CPUState));
    tcg_temp_free_i32(cpu_index);
    tcg_temp_free_i32(flags);
    tcg_temp_free_i32(clear_flags);
}

static void inject_cb(struct qemu_plugin_dyn_cb *cb)

{
    switch (cb->type) {
    case PLUGIN_CB_REGULAR:
        gen_udata_cb(&cb->regular);
        break;
    case PLUGIN_CB_COND:
        gen_udata_cond_cb(&cb->cond);
        break;
    case PLUGIN_CB_INLINE_ADD_U64:
        gen_inline_add_u64_cb(&cb->inline_insn);
        break;
    case PLUGIN_CB_INLINE_STORE_U64:
        gen_inline_store_u64_cb(&cb->inline_insn);
        break;
    default:
        g_assert_not_reached();
    }
}

static void inject_mem_cb(struct qemu_plugin_dyn_cb *cb,
                          enum qemu_plugin_mem_rw rw,
                          qemu_plugin_meminfo_t meminfo, TCGv_i64 addr)
{
    switch (cb->type) {
    case PLUGIN_CB_MEM_REGULAR:
        if (rw & cb->regular.rw) {
            gen_mem_cb(&cb->regular, meminfo, addr);
        }
        break;
    case PLUGIN_CB_INLINE_ADD_U64:
    case PLUGIN_CB_INLINE_STORE_U64:
        if (rw & cb->inline_insn.rw) {
            inject_cb(cb);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void plugin_gen_inject(struct qemu_plugin_tb *plugin_tb)
{
    TCGOp *op, *next;
    int insn_idx = -1;

    if (unlikely(qemu_loglevel_mask(LOG_TB_OP_PLUGIN)
                 && qemu_log_in_addr_range(tcg_ctx->plugin_db->pc_first))) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            fprintf(logfile, "OP before plugin injection:\n");
            tcg_dump_ops(tcg_ctx, logfile, false);
            fprintf(logfile, "\n");
            qemu_log_unlock(logfile);
        }
    }

    /*
     * While injecting code, we cannot afford to reuse any ebb temps
     * that might be live within the existing opcode stream.
     * The simplest solution is to release them all and create new.
     */
    tcg_temp_ebb_reset_freed(tcg_ctx);

    QTAILQ_FOREACH_SAFE(op, &tcg_ctx->ops, link, next) {
        switch (op->opc) {
        case INDEX_op_insn_start:
            insn_idx++;
            break;

        case INDEX_op_plugin_cb:
        {
            enum plugin_gen_from from = op->args[0];
            struct qemu_plugin_insn *insn = NULL;
            const GArray *cbs;
            int i, n;

            if (insn_idx >= 0) {
                insn = g_ptr_array_index(plugin_tb->insns, insn_idx);
            }

            tcg_ctx->emit_before_op = op;

            switch (from) {
            case PLUGIN_GEN_AFTER_TB:
                if (plugin_tb->mem_helper) {
                    gen_disable_mem_helper();
                }
                break;

            case PLUGIN_GEN_AFTER_INSN:
                assert(insn != NULL);
                if (insn->mem_helper) {
                    gen_disable_mem_helper();
                }
                break;

            case PLUGIN_GEN_FROM_TB:
                assert(insn == NULL);

                cbs = plugin_tb->cbs;
                for (i = 0, n = (cbs ? cbs->len : 0); i < n; i++) {
                    inject_cb(
                        &g_array_index(cbs, struct qemu_plugin_dyn_cb, i));
                }
                break;

            case PLUGIN_GEN_FROM_INSN:
                assert(insn != NULL);

                gen_enable_mem_helper(plugin_tb, insn);

                cbs = insn->insn_cbs;
                for (i = 0, n = (cbs ? cbs->len : 0); i < n; i++) {
                    inject_cb(
                        &g_array_index(cbs, struct qemu_plugin_dyn_cb, i));
                }
                break;

            default:
                g_assert_not_reached();
            }

            tcg_ctx->emit_before_op = NULL;
            tcg_op_remove(tcg_ctx, op);
            break;
        }

        case INDEX_op_plugin_mem_cb:
        {
            TCGv_i64 addr = temp_tcgv_i64(arg_temp(op->args[0]));
            qemu_plugin_meminfo_t meminfo = op->args[1];
            enum qemu_plugin_mem_rw rw =
                (qemu_plugin_mem_is_store(meminfo)
                 ? QEMU_PLUGIN_MEM_W : QEMU_PLUGIN_MEM_R);
            struct qemu_plugin_insn *insn;
            const GArray *cbs;
            int i, n;

            assert(insn_idx >= 0);
            insn = g_ptr_array_index(plugin_tb->insns, insn_idx);

            tcg_ctx->emit_before_op = op;

            cbs = insn->mem_cbs;
            for (i = 0, n = (cbs ? cbs->len : 0); i < n; i++) {
                inject_mem_cb(&g_array_index(cbs, struct qemu_plugin_dyn_cb, i),
                              rw, meminfo, addr);
            }

            tcg_ctx->emit_before_op = NULL;
            tcg_op_remove(tcg_ctx, op);
            break;
        }

        default:
            /* plugins don't care about any other ops */
            break;
        }
    }
}

bool plugin_gen_tb_start(CPUState *cpu, const DisasContextBase *db)
{
    struct qemu_plugin_tb *ptb;

    if (!test_bit(QEMU_PLUGIN_EV_VCPU_TB_TRANS,
                  cpu->plugin_state->event_mask)) {
        return false;
    }

    tcg_ctx->plugin_db = db;
    tcg_ctx->plugin_insn = NULL;
    ptb = tcg_ctx->plugin_tb;

    if (ptb) {
        /* Reset callbacks */
        if (ptb->cbs) {
            g_array_set_size(ptb->cbs, 0);
        }
        ptb->n = 0;
        ptb->mem_helper = false;
    } else {
        ptb = g_new0(struct qemu_plugin_tb, 1);
        tcg_ctx->plugin_tb = ptb;
        ptb->insns = g_ptr_array_new();
    }

    tcg_gen_plugin_cb(PLUGIN_GEN_FROM_TB);
    return true;
}

void plugin_gen_insn_start(CPUState *cpu, const DisasContextBase *db)
{
    struct qemu_plugin_tb *ptb = tcg_ctx->plugin_tb;
    struct qemu_plugin_insn *insn;
    size_t n = db->num_insns;
    vaddr pc;

    assert(n >= 1);
    ptb->n = n;
    if (n <= ptb->insns->len) {
        insn = g_ptr_array_index(ptb->insns, n - 1);
    } else {
        assert(n - 1 == ptb->insns->len);
        insn = g_new0(struct qemu_plugin_insn, 1);
        g_ptr_array_add(ptb->insns, insn);
    }

    tcg_ctx->plugin_insn = insn;
    insn->calls_helpers = false;
    insn->mem_helper = false;
    if (insn->insn_cbs) {
        g_array_set_size(insn->insn_cbs, 0);
    }
    if (insn->mem_cbs) {
        g_array_set_size(insn->mem_cbs, 0);
    }

    pc = db->pc_next;
    insn->vaddr = pc;

    tcg_gen_plugin_cb(PLUGIN_GEN_FROM_INSN);
}

void plugin_gen_insn_end(void)
{
    const DisasContextBase *db = tcg_ctx->plugin_db;
    struct qemu_plugin_insn *pinsn = tcg_ctx->plugin_insn;

    pinsn->len = db->fake_insn ? db->record_len : db->pc_next - pinsn->vaddr;

    tcg_gen_plugin_cb(PLUGIN_GEN_AFTER_INSN);
}

/*
 * There are cases where we never get to finalise a translation - for
 * example a page fault during translation. As a result we shouldn't
 * do any clean-up here and make sure things are reset in
 * plugin_gen_tb_start.
 */
void plugin_gen_tb_end(CPUState *cpu, size_t num_insns)
{
    struct qemu_plugin_tb *ptb = tcg_ctx->plugin_tb;

    /* translator may have removed instructions, update final count */
    g_assert(num_insns <= ptb->n);
    ptb->n = num_insns;

    /* collect instrumentation requests */
    qemu_plugin_tb_trans_cb(cpu, ptb);

    /* inject the instrumentation at the appropriate places */
    plugin_gen_inject(ptb);

    /* reset plugin translation state (plugin_tb is reused between blocks) */
    tcg_ctx->plugin_db = NULL;
    tcg_ctx->plugin_insn = NULL;
}
