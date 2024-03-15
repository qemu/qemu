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
 * Instead, during TB translation we add "empty" instrumentation calls for all
 * possible instrumentation events, and then once we collect the instrumentation
 * requests from plugins, we either "fill in" those empty events or remove them
 * if they have no requests.
 *
 * When "filling in" an event we first copy the empty callback's TCG ops. This
 * might seem unnecessary, but it is done to support an arbitrary number
 * of callbacks per event. Take for example a regular instruction callback.
 * We first generate a callback to an empty helper function. Then, if two
 * plugins register one callback each for this instruction, we make two copies
 * of the TCG ops generated for the empty callback, substituting the function
 * pointer that points to the empty helper function with the plugins' desired
 * callback functions. After that we remove the empty callback's ops.
 *
 * Note that the location in TCGOp.args[] of the pointer to a helper function
 * varies across different guest and host architectures. Instead of duplicating
 * the logic that figures this out, we rely on the fact that the empty
 * callbacks point to empty functions that are unique pointers in the program.
 * Thus, to find the right location we just have to look for a match in
 * TCGOp.args[]. This is the main reason why we first copy an empty callback's
 * TCG ops and then fill them in; regardless of whether we have one or many
 * callbacks for that event, the logic to add all of them is the same.
 *
 * When generating more than one callback per event, we make a small
 * optimization to avoid generating redundant operations. For instance, for the
 * second and all subsequent callbacks of an event, we do not need to reload the
 * CPU's index into a TCG temp, since the first callback did it already.
 */
#include "qemu/osdep.h"
#include "qemu/plugin.h"
#include "cpu.h"
#include "tcg/tcg.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg/tcg-op.h"
#include "exec/exec-all.h"
#include "exec/plugin-gen.h"
#include "exec/translator.h"

enum plugin_gen_from {
    PLUGIN_GEN_FROM_TB,
    PLUGIN_GEN_FROM_INSN,
    PLUGIN_GEN_AFTER_INSN,
    PLUGIN_GEN_AFTER_TB,
};

static void plugin_gen_empty_callback(enum plugin_gen_from from)
{
    switch (from) {
    case PLUGIN_GEN_AFTER_INSN:
    case PLUGIN_GEN_FROM_TB:
    case PLUGIN_GEN_FROM_INSN:
        tcg_gen_plugin_cb(from);
        break;
    default:
        g_assert_not_reached();
    }
}

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
    GArray *cbs[2];
    GArray *arr;
    size_t n_cbs;

    /*
     * Tracking memory accesses performed from helpers requires extra work.
     * If an instruction is emulated with helpers, we do two things:
     * (1) copy the CB descriptors, and keep track of it so that they can be
     * freed later on, and (2) point CPUState.plugin_mem_cbs to the
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

    cbs[0] = insn->cbs[PLUGIN_CB_MEM][PLUGIN_CB_REGULAR];
    cbs[1] = insn->cbs[PLUGIN_CB_MEM][PLUGIN_CB_INLINE];
    n_cbs = cbs[0]->len + cbs[1]->len;

    if (n_cbs == 0) {
        insn->mem_helper = false;
        return;
    }
    insn->mem_helper = true;
    ptb->mem_helper = true;

    arr = g_array_sized_new(false, false,
                            sizeof(struct qemu_plugin_dyn_cb), n_cbs);
    g_array_append_vals(arr, cbs[0]->data, cbs[0]->len);
    g_array_append_vals(arr, cbs[1]->data, cbs[1]->len);

    qemu_plugin_add_dyn_cb_arr(arr);

    tcg_gen_st_ptr(tcg_constant_ptr((intptr_t)arr), tcg_env,
                   offsetof(CPUState, plugin_mem_cbs) -
                   offsetof(ArchCPU, env));
}

static void gen_disable_mem_helper(void)
{
    tcg_gen_st_ptr(tcg_constant_ptr(0), tcg_env,
                   offsetof(CPUState, plugin_mem_cbs) -
                   offsetof(ArchCPU, env));
}

static void gen_udata_cb(struct qemu_plugin_dyn_cb *cb)
{
    TCGv_i32 cpu_index = tcg_temp_ebb_new_i32();

    tcg_gen_ld_i32(cpu_index, tcg_env,
                   -offsetof(ArchCPU, env) + offsetof(CPUState, cpu_index));
    tcg_gen_call2(cb->regular.f.vcpu_udata, cb->regular.info, NULL,
                  tcgv_i32_temp(cpu_index),
                  tcgv_ptr_temp(tcg_constant_ptr(cb->userp)));
    tcg_temp_free_i32(cpu_index);
}

static void gen_inline_cb(struct qemu_plugin_dyn_cb *cb)
{
    GArray *arr = cb->inline_insn.entry.score->data;
    size_t offset = cb->inline_insn.entry.offset;
    TCGv_i32 cpu_index = tcg_temp_ebb_new_i32();
    TCGv_i64 val = tcg_temp_ebb_new_i64();
    TCGv_ptr ptr = tcg_temp_ebb_new_ptr();

    tcg_gen_ld_i32(cpu_index, tcg_env,
                   -offsetof(ArchCPU, env) + offsetof(CPUState, cpu_index));
    tcg_gen_muli_i32(cpu_index, cpu_index, g_array_get_element_size(arr));
    tcg_gen_ext_i32_ptr(ptr, cpu_index);
    tcg_temp_free_i32(cpu_index);

    tcg_gen_addi_ptr(ptr, ptr, (intptr_t)arr->data);
    tcg_gen_ld_i64(val, ptr, offset);
    tcg_gen_addi_i64(val, val, cb->inline_insn.imm);
    tcg_gen_st_i64(val, ptr, offset);

    tcg_temp_free_i64(val);
    tcg_temp_free_ptr(ptr);
}

static void gen_mem_cb(struct qemu_plugin_dyn_cb *cb,
                       qemu_plugin_meminfo_t meminfo, TCGv_i64 addr)
{
    TCGv_i32 cpu_index = tcg_temp_ebb_new_i32();

    tcg_gen_ld_i32(cpu_index, tcg_env,
                   -offsetof(ArchCPU, env) + offsetof(CPUState, cpu_index));
    tcg_gen_call4(cb->regular.f.vcpu_mem, cb->regular.info, NULL,
                  tcgv_i32_temp(cpu_index),
                  tcgv_i32_temp(tcg_constant_i32(meminfo)),
                  tcgv_i64_temp(addr),
                  tcgv_ptr_temp(tcg_constant_ptr(cb->userp)));
    tcg_temp_free_i32(cpu_index);
}

/* #define DEBUG_PLUGIN_GEN_OPS */
static void pr_ops(void)
{
#ifdef DEBUG_PLUGIN_GEN_OPS
    TCGOp *op;
    int i = 0;

    QTAILQ_FOREACH(op, &tcg_ctx->ops, link) {
        const char *name = "";
        const char *type = "";

        if (op->opc == INDEX_op_plugin_cb_start) {
            switch (op->args[0]) {
            case PLUGIN_GEN_FROM_TB:
                name = "tb";
                break;
            case PLUGIN_GEN_FROM_INSN:
                name = "insn";
                break;
            case PLUGIN_GEN_FROM_MEM:
                name = "mem";
                break;
            case PLUGIN_GEN_AFTER_INSN:
                name = "after insn";
                break;
            default:
                break;
            }
            switch (op->args[1]) {
            case PLUGIN_GEN_CB_UDATA:
                type = "udata";
                break;
            case PLUGIN_GEN_CB_INLINE:
                type = "inline";
                break;
            case PLUGIN_GEN_CB_MEM:
                type = "mem";
                break;
            case PLUGIN_GEN_ENABLE_MEM_HELPER:
                type = "enable mem helper";
                break;
            case PLUGIN_GEN_DISABLE_MEM_HELPER:
                type = "disable mem helper";
                break;
            default:
                break;
            }
        }
        printf("op[%2i]: %s %s %s\n", i, tcg_op_defs[op->opc].name, name, type);
        i++;
    }
#endif
}

static void plugin_gen_inject(struct qemu_plugin_tb *plugin_tb)
{
    TCGOp *op, *next;
    int insn_idx = -1;

    pr_ops();

    /*
     * While injecting code, we cannot afford to reuse any ebb temps
     * that might be live within the existing opcode stream.
     * The simplest solution is to release them all and create new.
     */
    memset(tcg_ctx->free_temps, 0, sizeof(tcg_ctx->free_temps));

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

                cbs = plugin_tb->cbs[PLUGIN_CB_REGULAR];
                for (i = 0, n = (cbs ? cbs->len : 0); i < n; i++) {
                    struct qemu_plugin_dyn_cb *cb =
                        &g_array_index(cbs, struct qemu_plugin_dyn_cb, i);
                    gen_udata_cb(cb);
                }

                cbs = plugin_tb->cbs[PLUGIN_CB_INLINE];
                for (i = 0, n = (cbs ? cbs->len : 0); i < n; i++) {
                    struct qemu_plugin_dyn_cb *cb =
                        &g_array_index(cbs, struct qemu_plugin_dyn_cb, i);
                    gen_inline_cb(cb);
                }
                break;

            case PLUGIN_GEN_FROM_INSN:
                assert(insn != NULL);

                gen_enable_mem_helper(plugin_tb, insn);

                cbs = insn->cbs[PLUGIN_CB_INSN][PLUGIN_CB_REGULAR];
                for (i = 0, n = (cbs ? cbs->len : 0); i < n; i++) {
                    struct qemu_plugin_dyn_cb *cb =
                        &g_array_index(cbs, struct qemu_plugin_dyn_cb, i);
                    gen_udata_cb(cb);
                }

                cbs = insn->cbs[PLUGIN_CB_INSN][PLUGIN_CB_INLINE];
                for (i = 0, n = (cbs ? cbs->len : 0); i < n; i++) {
                    struct qemu_plugin_dyn_cb *cb =
                        &g_array_index(cbs, struct qemu_plugin_dyn_cb, i);
                    gen_inline_cb(cb);
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
            struct qemu_plugin_insn *insn;
            const GArray *cbs;
            int i, n, rw;

            assert(insn_idx >= 0);
            insn = g_ptr_array_index(plugin_tb->insns, insn_idx);
            rw = qemu_plugin_mem_is_store(meminfo) ? 2 : 1;

            tcg_ctx->emit_before_op = op;

            cbs = insn->cbs[PLUGIN_CB_MEM][PLUGIN_CB_REGULAR];
            for (i = 0, n = (cbs ? cbs->len : 0); i < n; i++) {
                struct qemu_plugin_dyn_cb *cb =
                    &g_array_index(cbs, struct qemu_plugin_dyn_cb, i);
                if (cb->rw & rw) {
                    gen_mem_cb(cb, meminfo, addr);
                }
            }

            cbs = insn->cbs[PLUGIN_CB_MEM][PLUGIN_CB_INLINE];
            for (i = 0, n = (cbs ? cbs->len : 0); i < n; i++) {
                struct qemu_plugin_dyn_cb *cb =
                    &g_array_index(cbs, struct qemu_plugin_dyn_cb, i);
                if (cb->rw & rw) {
                    gen_inline_cb(cb);
                }
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
    pr_ops();
}

bool plugin_gen_tb_start(CPUState *cpu, const DisasContextBase *db,
                         bool mem_only)
{
    bool ret = false;

    if (test_bit(QEMU_PLUGIN_EV_VCPU_TB_TRANS, cpu->plugin_state->event_mask)) {
        struct qemu_plugin_tb *ptb = tcg_ctx->plugin_tb;
        int i;

        /* reset callbacks */
        for (i = 0; i < PLUGIN_N_CB_SUBTYPES; i++) {
            if (ptb->cbs[i]) {
                g_array_set_size(ptb->cbs[i], 0);
            }
        }
        ptb->n = 0;

        ret = true;

        ptb->vaddr = db->pc_first;
        ptb->vaddr2 = -1;
        ptb->haddr1 = db->host_addr[0];
        ptb->haddr2 = NULL;
        ptb->mem_only = mem_only;
        ptb->mem_helper = false;

        plugin_gen_empty_callback(PLUGIN_GEN_FROM_TB);
    }

    tcg_ctx->plugin_insn = NULL;

    return ret;
}

void plugin_gen_insn_start(CPUState *cpu, const DisasContextBase *db)
{
    struct qemu_plugin_tb *ptb = tcg_ctx->plugin_tb;
    struct qemu_plugin_insn *pinsn;

    pinsn = qemu_plugin_tb_insn_get(ptb, db->pc_next);
    tcg_ctx->plugin_insn = pinsn;
    plugin_gen_empty_callback(PLUGIN_GEN_FROM_INSN);

    /*
     * Detect page crossing to get the new host address.
     * Note that we skip this when haddr1 == NULL, e.g. when we're
     * fetching instructions from a region not backed by RAM.
     */
    if (ptb->haddr1 == NULL) {
        pinsn->haddr = NULL;
    } else if (is_same_page(db, db->pc_next)) {
        pinsn->haddr = ptb->haddr1 + pinsn->vaddr - ptb->vaddr;
    } else {
        if (ptb->vaddr2 == -1) {
            ptb->vaddr2 = TARGET_PAGE_ALIGN(db->pc_first);
            get_page_addr_code_hostp(cpu_env(cpu), ptb->vaddr2, &ptb->haddr2);
        }
        pinsn->haddr = ptb->haddr2 + pinsn->vaddr - ptb->vaddr2;
    }
}

void plugin_gen_insn_end(void)
{
    plugin_gen_empty_callback(PLUGIN_GEN_AFTER_INSN);
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
}
