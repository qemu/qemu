/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/gdbstub.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "qemu/host-utils.h"

static struct XtensaConfigList *xtensa_cores;

static void add_translator_to_hash(GHashTable *translator,
                                   const char *name,
                                   const XtensaOpcodeOps *opcode)
{
    if (!g_hash_table_insert(translator, (void *)name, (void *)opcode)) {
        error_report("Multiple definitions of '%s' opcode in a single table",
                     name);
    }
}

static GHashTable *hash_opcode_translators(const XtensaOpcodeTranslators *t)
{
    unsigned i, j;
    GHashTable *translator = g_hash_table_new(g_str_hash, g_str_equal);

    for (i = 0; i < t->num_opcodes; ++i) {
        if (t->opcode[i].op_flags & XTENSA_OP_NAME_ARRAY) {
            const char * const *name = t->opcode[i].name;

            for (j = 0; name[j]; ++j) {
                add_translator_to_hash(translator,
                                       (void *)name[j],
                                       (void *)(t->opcode + i));
            }
        } else {
            add_translator_to_hash(translator,
                                   (void *)t->opcode[i].name,
                                   (void *)(t->opcode + i));
        }
    }
    return translator;
}

static XtensaOpcodeOps *
xtensa_find_opcode_ops(const XtensaOpcodeTranslators *t,
                       const char *name)
{
    static GHashTable *translators;
    GHashTable *translator;

    if (translators == NULL) {
        translators = g_hash_table_new(g_direct_hash, g_direct_equal);
    }
    translator = g_hash_table_lookup(translators, t);
    if (translator == NULL) {
        translator = hash_opcode_translators(t);
        g_hash_table_insert(translators, (void *)t, translator);
    }
    return g_hash_table_lookup(translator, name);
}

static void init_libisa(XtensaConfig *config)
{
    unsigned i, j;
    unsigned opcodes;
    unsigned formats;
    unsigned regfiles;

    config->isa = xtensa_isa_init(config->isa_internal, NULL, NULL);
    assert(xtensa_isa_maxlength(config->isa) <= MAX_INSN_LENGTH);
    assert(xtensa_insnbuf_size(config->isa) <= MAX_INSNBUF_LENGTH);
    opcodes = xtensa_isa_num_opcodes(config->isa);
    formats = xtensa_isa_num_formats(config->isa);
    regfiles = xtensa_isa_num_regfiles(config->isa);
    config->opcode_ops = g_new(XtensaOpcodeOps *, opcodes);

    for (i = 0; i < formats; ++i) {
        assert(xtensa_format_num_slots(config->isa, i) <= MAX_INSN_SLOTS);
    }

    for (i = 0; i < opcodes; ++i) {
        const char *opc_name = xtensa_opcode_name(config->isa, i);
        XtensaOpcodeOps *ops = NULL;

        assert(xtensa_opcode_num_operands(config->isa, i) <= MAX_OPCODE_ARGS);
        if (!config->opcode_translators) {
            ops = xtensa_find_opcode_ops(&xtensa_core_opcodes, opc_name);
        } else {
            for (j = 0; !ops && config->opcode_translators[j]; ++j) {
                ops = xtensa_find_opcode_ops(config->opcode_translators[j],
                                             opc_name);
            }
        }
#ifdef DEBUG
        if (ops == NULL) {
            fprintf(stderr,
                    "opcode translator not found for %s's opcode '%s'\n",
                    config->name, opc_name);
        }
#endif
        config->opcode_ops[i] = ops;
    }
    config->a_regfile = xtensa_regfile_lookup(config->isa, "AR");

    config->regfile = g_new(void **, regfiles);
    for (i = 0; i < regfiles; ++i) {
        const char *name = xtensa_regfile_name(config->isa, i);
        int entries = xtensa_regfile_num_entries(config->isa, i);
        int bits = xtensa_regfile_num_bits(config->isa, i);

        config->regfile[i] = xtensa_get_regfile_by_name(name, entries, bits);
#ifdef DEBUG
        if (config->regfile[i] == NULL) {
            fprintf(stderr, "regfile '%s' not found for %s\n",
                    name, config->name);
        }
#endif
    }
    xtensa_collect_sr_names(config);
}

static void xtensa_finalize_config(XtensaConfig *config)
{
    if (config->isa_internal) {
        init_libisa(config);
    }

    if (config->gdb_regmap.num_regs == 0 ||
        config->gdb_regmap.num_core_regs == 0) {
        unsigned n_regs = 0;
        unsigned n_core_regs = 0;

        xtensa_count_regs(config, &n_regs, &n_core_regs);
        if (config->gdb_regmap.num_regs == 0) {
            config->gdb_regmap.num_regs = n_regs;
        }
        if (config->gdb_regmap.num_core_regs == 0) {
            config->gdb_regmap.num_core_regs = n_core_regs;
        }
    }
}

static void xtensa_core_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    XtensaCPUClass *xcc = XTENSA_CPU_CLASS(oc);
    XtensaConfig *config = data;

    xtensa_finalize_config(config);
    xcc->config = config;

    /*
     * Use num_core_regs to see only non-privileged registers in an unmodified
     * gdb. Use num_regs to see all registers. gdb modification is required
     * for that: reset bit 0 in the 'flags' field of the registers definitions
     * in the gdb/xtensa-config.c inside gdb source tree or inside gdb overlay.
     */
    cc->gdb_num_core_regs = config->gdb_regmap.num_regs;
}

void xtensa_register_core(XtensaConfigList *node)
{
    TypeInfo type = {
        .parent = TYPE_XTENSA_CPU,
        .class_init = xtensa_core_class_init,
        .class_data = (void *)node->config,
    };

    node->next = xtensa_cores;
    xtensa_cores = node;
    type.name = g_strdup_printf(XTENSA_CPU_TYPE_NAME("%s"), node->config->name);
    type_register(&type);
    g_free((gpointer)type.name);
}

static uint32_t check_hw_breakpoints(CPUXtensaState *env)
{
    unsigned i;

    for (i = 0; i < env->config->ndbreak; ++i) {
        if (env->cpu_watchpoint[i] &&
                env->cpu_watchpoint[i]->flags & BP_WATCHPOINT_HIT) {
            return DEBUGCAUSE_DB | (i << DEBUGCAUSE_DBNUM_SHIFT);
        }
    }
    return 0;
}

void xtensa_breakpoint_handler(CPUState *cs)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    if (cs->watchpoint_hit) {
        if (cs->watchpoint_hit->flags & BP_CPU) {
            uint32_t cause;

            cs->watchpoint_hit = NULL;
            cause = check_hw_breakpoints(env);
            if (cause) {
                debug_exception_env(env, cause);
            }
            cpu_loop_exit_noexc(cs);
        }
    }
}

void xtensa_cpu_list(void)
{
    XtensaConfigList *core = xtensa_cores;
    qemu_printf("Available CPUs:\n");
    for (; core; core = core->next) {
        qemu_printf("  %s\n", core->config->name);
    }
}

#ifdef CONFIG_USER_ONLY

bool xtensa_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                         MMUAccessType access_type, int mmu_idx,
                         bool probe, uintptr_t retaddr)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    qemu_log_mask(CPU_LOG_INT,
                  "%s: rw = %d, address = 0x%08" VADDR_PRIx ", size = %d\n",
                  __func__, access_type, address, size);
    env->sregs[EXCVADDR] = address;
    env->sregs[EXCCAUSE] = (access_type == MMU_DATA_STORE ?
                            STORE_PROHIBITED_CAUSE : LOAD_PROHIBITED_CAUSE);
    cs->exception_index = EXC_USER;
    cpu_loop_exit_restore(cs, retaddr);
}

#else /* !CONFIG_USER_ONLY */

void xtensa_cpu_do_unaligned_access(CPUState *cs,
                                    vaddr addr, MMUAccessType access_type,
                                    int mmu_idx, uintptr_t retaddr)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    assert(xtensa_option_enabled(env->config,
                                 XTENSA_OPTION_UNALIGNED_EXCEPTION));
    cpu_restore_state(CPU(cpu), retaddr, true);
    HELPER(exception_cause_vaddr)(env,
                                  env->pc, LOAD_STORE_ALIGNMENT_CAUSE,
                                  addr);
}

bool xtensa_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                         MMUAccessType access_type, int mmu_idx,
                         bool probe, uintptr_t retaddr)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;
    uint32_t paddr;
    uint32_t page_size;
    unsigned access;
    int ret = xtensa_get_physical_addr(env, true, address, access_type,
                                       mmu_idx, &paddr, &page_size, &access);

    qemu_log_mask(CPU_LOG_MMU, "%s(%08" VADDR_PRIx
                  ", %d, %d) -> %08x, ret = %d\n",
                  __func__, address, access_type, mmu_idx, paddr, ret);

    if (ret == 0) {
        tlb_set_page(cs,
                     address & TARGET_PAGE_MASK,
                     paddr & TARGET_PAGE_MASK,
                     access, mmu_idx, page_size);
        return true;
    } else if (probe) {
        return false;
    } else {
        cpu_restore_state(cs, retaddr, true);
        HELPER(exception_cause_vaddr)(env, env->pc, ret, address);
    }
}

void xtensa_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr, vaddr addr,
                                      unsigned size, MMUAccessType access_type,
                                      int mmu_idx, MemTxAttrs attrs,
                                      MemTxResult response, uintptr_t retaddr)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    cpu_restore_state(cs, retaddr, true);
    HELPER(exception_cause_vaddr)(env, env->pc,
                                  access_type == MMU_INST_FETCH ?
                                  INSTR_PIF_ADDR_ERROR_CAUSE :
                                  LOAD_STORE_PIF_ADDR_ERROR_CAUSE,
                                  addr);
}

void xtensa_runstall(CPUXtensaState *env, bool runstall)
{
    CPUState *cpu = env_cpu(env);

    env->runstall = runstall;
    cpu->halted = runstall;
    if (runstall) {
        cpu_interrupt(cpu, CPU_INTERRUPT_HALT);
    } else {
        qemu_cpu_kick(cpu);
    }
}
#endif /* !CONFIG_USER_ONLY */
