/*
 * Generic intermediate code generation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "exec/exec-all.h"
#include "exec/translator.h"
#include "exec/plugin-gen.h"
#include "tcg/tcg-op-common.h"
#include "internal-target.h"

static void set_can_do_io(DisasContextBase *db, bool val)
{
    if (db->saved_can_do_io != val) {
        db->saved_can_do_io = val;

        QEMU_BUILD_BUG_ON(sizeof_field(CPUState, neg.can_do_io) != 1);
        tcg_gen_st8_i32(tcg_constant_i32(val), tcg_env,
                        offsetof(ArchCPU, parent_obj.neg.can_do_io) -
                        offsetof(ArchCPU, env));
    }
}

bool translator_io_start(DisasContextBase *db)
{
    set_can_do_io(db, true);

    /*
     * Ensure that this instruction will be the last in the TB.
     * The target may override this to something more forceful.
     */
    if (db->is_jmp == DISAS_NEXT) {
        db->is_jmp = DISAS_TOO_MANY;
    }
    return true;
}

static TCGOp *gen_tb_start(DisasContextBase *db, uint32_t cflags)
{
    TCGv_i32 count = NULL;
    TCGOp *icount_start_insn = NULL;

    if ((cflags & CF_USE_ICOUNT) || !(cflags & CF_NOIRQ)) {
        count = tcg_temp_new_i32();
        tcg_gen_ld_i32(count, tcg_env,
                       offsetof(ArchCPU, parent_obj.neg.icount_decr.u32)
                       - offsetof(ArchCPU, env));
    }

    if (cflags & CF_USE_ICOUNT) {
        /*
         * We emit a sub with a dummy immediate argument. Keep the insn index
         * of the sub so that we later (when we know the actual insn count)
         * can update the argument with the actual insn count.
         */
        tcg_gen_sub_i32(count, count, tcg_constant_i32(0));
        icount_start_insn = tcg_last_op();
    }

    /*
     * Emit the check against icount_decr.u32 to see if we should exit
     * unless we suppress the check with CF_NOIRQ. If we are using
     * icount and have suppressed interruption the higher level code
     * should have ensured we don't run more instructions than the
     * budget.
     */
    if (cflags & CF_NOIRQ) {
        tcg_ctx->exitreq_label = NULL;
    } else {
        tcg_ctx->exitreq_label = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_LT, count, 0, tcg_ctx->exitreq_label);
    }

    if (cflags & CF_USE_ICOUNT) {
        tcg_gen_st16_i32(count, tcg_env,
                         offsetof(ArchCPU, parent_obj.neg.icount_decr.u16.low)
                         - offsetof(ArchCPU, env));
    }

    /*
     * cpu->neg.can_do_io is set automatically here at the beginning of
     * each translation block.  The cost is minimal, plus it would be
     * very easy to forget doing it in the translator.
     */
    set_can_do_io(db, db->max_insns == 1);

    return icount_start_insn;
}

static void gen_tb_end(const TranslationBlock *tb, uint32_t cflags,
                       TCGOp *icount_start_insn, int num_insns)
{
    if (cflags & CF_USE_ICOUNT) {
        /*
         * Update the num_insn immediate parameter now that we know
         * the actual insn count.
         */
        tcg_set_insn_param(icount_start_insn, 2,
                           tcgv_i32_arg(tcg_constant_i32(num_insns)));
    }

    if (tcg_ctx->exitreq_label) {
        gen_set_label(tcg_ctx->exitreq_label);
        tcg_gen_exit_tb(tb, TB_EXIT_REQUESTED);
    }
}

bool translator_use_goto_tb(DisasContextBase *db, vaddr dest)
{
    /* Suppress goto_tb if requested. */
    if (tb_cflags(db->tb) & CF_NO_GOTO_TB) {
        return false;
    }

    /* Check for the dest on the same page as the start of the TB.  */
    return ((db->pc_first ^ dest) & TARGET_PAGE_MASK) == 0;
}

void translator_loop(CPUState *cpu, TranslationBlock *tb, int *max_insns,
                     vaddr pc, void *host_pc, const TranslatorOps *ops,
                     DisasContextBase *db)
{
    uint32_t cflags = tb_cflags(tb);
    TCGOp *icount_start_insn;
    bool plugin_enabled;

    /* Initialize DisasContext */
    db->tb = tb;
    db->pc_first = pc;
    db->pc_next = pc;
    db->is_jmp = DISAS_NEXT;
    db->num_insns = 0;
    db->max_insns = *max_insns;
    db->singlestep_enabled = cflags & CF_SINGLE_STEP;
    db->saved_can_do_io = -1;
    db->host_addr[0] = host_pc;
    db->host_addr[1] = NULL;

    ops->init_disas_context(db, cpu);
    tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

    /* Start translating.  */
    icount_start_insn = gen_tb_start(db, cflags);
    ops->tb_start(db, cpu);
    tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

    plugin_enabled = plugin_gen_tb_start(cpu, db, cflags & CF_MEMI_ONLY);
    db->plugin_enabled = plugin_enabled;

    while (true) {
        *max_insns = ++db->num_insns;
        ops->insn_start(db, cpu);
        tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

        if (plugin_enabled) {
            plugin_gen_insn_start(cpu, db);
        }

        /*
         * Disassemble one instruction.  The translate_insn hook should
         * update db->pc_next and db->is_jmp to indicate what should be
         * done next -- either exiting this loop or locate the start of
         * the next instruction.
         */
        if (db->num_insns == db->max_insns) {
            /* Accept I/O on the last instruction.  */
            set_can_do_io(db, true);
        }
        ops->translate_insn(db, cpu);

        /*
         * We can't instrument after instructions that change control
         * flow although this only really affects post-load operations.
         *
         * Calling plugin_gen_insn_end() before we possibly stop translation
         * is important. Even if this ends up as dead code, plugin generation
         * needs to see a matching plugin_gen_insn_{start,end}() pair in order
         * to accurately track instrumented helpers that might access memory.
         */
        if (plugin_enabled) {
            plugin_gen_insn_end();
        }

        /* Stop translation if translate_insn so indicated.  */
        if (db->is_jmp != DISAS_NEXT) {
            break;
        }

        /* Stop translation if the output buffer is full,
           or we have executed all of the allowed instructions.  */
        if (tcg_op_buf_full() || db->num_insns >= db->max_insns) {
            db->is_jmp = DISAS_TOO_MANY;
            break;
        }
    }

    /* Emit code to exit the TB, as indicated by db->is_jmp.  */
    ops->tb_stop(db, cpu);
    gen_tb_end(tb, cflags, icount_start_insn, db->num_insns);

    if (plugin_enabled) {
        plugin_gen_tb_end(cpu, db->num_insns);
    }

    /* The disas_log hook may use these values rather than recompute.  */
    tb->size = db->pc_next - db->pc_first;
    tb->icount = db->num_insns;

    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)
        && qemu_log_in_addr_range(db->pc_first)) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            fprintf(logfile, "----------------\n");
            ops->disas_log(db, cpu, logfile);
            fprintf(logfile, "\n");
            qemu_log_unlock(logfile);
        }
    }
}

static void *translator_access(CPUArchState *env, DisasContextBase *db,
                               vaddr pc, size_t len)
{
    void *host;
    vaddr base, end;
    TranslationBlock *tb;

    tb = db->tb;

    /* Use slow path if first page is MMIO. */
    if (unlikely(tb_page_addr0(tb) == -1)) {
        return NULL;
    }

    end = pc + len - 1;
    if (likely(is_same_page(db, end))) {
        host = db->host_addr[0];
        base = db->pc_first;
    } else {
        host = db->host_addr[1];
        base = TARGET_PAGE_ALIGN(db->pc_first);
        if (host == NULL) {
            tb_page_addr_t page0, old_page1, new_page1;

            new_page1 = get_page_addr_code_hostp(env, base, &db->host_addr[1]);

            /*
             * If the second page is MMIO, treat as if the first page
             * was MMIO as well, so that we do not cache the TB.
             */
            if (unlikely(new_page1 == -1)) {
                tb_unlock_pages(tb);
                tb_set_page_addr0(tb, -1);
                return NULL;
            }

            /*
             * If this is not the first time around, and page1 matches,
             * then we already have the page locked.  Alternately, we're
             * not doing anything to prevent the PTE from changing, so
             * we might wind up with a different page, requiring us to
             * re-do the locking.
             */
            old_page1 = tb_page_addr1(tb);
            if (likely(new_page1 != old_page1)) {
                page0 = tb_page_addr0(tb);
                if (unlikely(old_page1 != -1)) {
                    tb_unlock_page1(page0, old_page1);
                }
                tb_set_page_addr1(tb, new_page1);
                tb_lock_page1(page0, new_page1);
            }
            host = db->host_addr[1];
        }

        /* Use slow path when crossing pages. */
        if (is_same_page(db, pc)) {
            return NULL;
        }
    }

    tcg_debug_assert(pc >= base);
    return host + (pc - base);
}

static void plugin_insn_append(abi_ptr pc, const void *from, size_t size)
{
#ifdef CONFIG_PLUGIN
    struct qemu_plugin_insn *insn = tcg_ctx->plugin_insn;
    abi_ptr off;

    if (insn == NULL) {
        return;
    }
    off = pc - insn->vaddr;
    if (off < insn->data->len) {
        g_byte_array_set_size(insn->data, off);
    } else if (off > insn->data->len) {
        /* we have an unexpected gap */
        g_assert_not_reached();
    }

    insn->data = g_byte_array_append(insn->data, from, size);
#endif
}

uint8_t translator_ldub(CPUArchState *env, DisasContextBase *db, abi_ptr pc)
{
    uint8_t ret;
    void *p = translator_access(env, db, pc, sizeof(ret));

    if (p) {
        plugin_insn_append(pc, p, sizeof(ret));
        return ldub_p(p);
    }
    ret = cpu_ldub_code(env, pc);
    plugin_insn_append(pc, &ret, sizeof(ret));
    return ret;
}

uint16_t translator_lduw(CPUArchState *env, DisasContextBase *db, abi_ptr pc)
{
    uint16_t ret, plug;
    void *p = translator_access(env, db, pc, sizeof(ret));

    if (p) {
        plugin_insn_append(pc, p, sizeof(ret));
        return lduw_p(p);
    }
    ret = cpu_lduw_code(env, pc);
    plug = tswap16(ret);
    plugin_insn_append(pc, &plug, sizeof(ret));
    return ret;
}

uint32_t translator_ldl(CPUArchState *env, DisasContextBase *db, abi_ptr pc)
{
    uint32_t ret, plug;
    void *p = translator_access(env, db, pc, sizeof(ret));

    if (p) {
        plugin_insn_append(pc, p, sizeof(ret));
        return ldl_p(p);
    }
    ret = cpu_ldl_code(env, pc);
    plug = tswap32(ret);
    plugin_insn_append(pc, &plug, sizeof(ret));
    return ret;
}

uint64_t translator_ldq(CPUArchState *env, DisasContextBase *db, abi_ptr pc)
{
    uint64_t ret, plug;
    void *p = translator_access(env, db, pc, sizeof(ret));

    if (p) {
        plugin_insn_append(pc, p, sizeof(ret));
        return ldq_p(p);
    }
    ret = cpu_ldq_code(env, pc);
    plug = tswap64(ret);
    plugin_insn_append(pc, &plug, sizeof(ret));
    return ret;
}

void translator_fake_ldb(uint8_t insn8, abi_ptr pc)
{
    plugin_insn_append(pc, &insn8, sizeof(insn8));
}
