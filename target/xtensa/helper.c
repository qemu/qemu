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
#include "qemu/host-utils.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif

static struct XtensaConfigList *xtensa_cores;

static void xtensa_core_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    XtensaCPUClass *xcc = XTENSA_CPU_CLASS(oc);
    const XtensaConfig *config = data;

    xcc->config = config;

    /* Use num_core_regs to see only non-privileged registers in an unmodified
     * gdb. Use num_regs to see all registers. gdb modification is required
     * for that: reset bit 0 in the 'flags' field of the registers definitions
     * in the gdb/xtensa-config.c inside gdb source tree or inside gdb overlay.
     */
    cc->gdb_num_core_regs = config->gdb_regmap.num_regs;
}

static void init_libisa(XtensaConfig *config)
{
    unsigned i, j;
    unsigned opcodes;
    unsigned formats;

    config->isa = xtensa_isa_init(config->isa_internal, NULL, NULL);
    assert(xtensa_isa_maxlength(config->isa) <= MAX_INSN_LENGTH);
    opcodes = xtensa_isa_num_opcodes(config->isa);
    formats = xtensa_isa_num_formats(config->isa);
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
}

void xtensa_finalize_config(XtensaConfig *config)
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

void xtensa_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    XtensaConfigList *core = xtensa_cores;
    cpu_fprintf(f, "Available CPUs:\n");
    for (; core; core = core->next) {
        cpu_fprintf(f, "  %s\n", core->config->name);
    }
}

#ifndef CONFIG_USER_ONLY

static uint32_t relocated_vector(CPUXtensaState *env, uint32_t vector)
{
    if (xtensa_option_enabled(env->config,
                XTENSA_OPTION_RELOCATABLE_VECTOR)) {
        return vector - env->config->vecbase + env->sregs[VECBASE];
    } else {
        return vector;
    }
}

/*!
 * Handle penging IRQ.
 * For the high priority interrupt jump to the corresponding interrupt vector.
 * For the level-1 interrupt convert it to either user, kernel or double
 * exception with the 'level-1 interrupt' exception cause.
 */
static void handle_interrupt(CPUXtensaState *env)
{
    int level = env->pending_irq_level;

    if (level > xtensa_get_cintlevel(env) &&
            level <= env->config->nlevel &&
            (env->config->level_mask[level] &
             env->sregs[INTSET] &
             env->sregs[INTENABLE])) {
        CPUState *cs = CPU(xtensa_env_get_cpu(env));

        if (level > 1) {
            env->sregs[EPC1 + level - 1] = env->pc;
            env->sregs[EPS2 + level - 2] = env->sregs[PS];
            env->sregs[PS] =
                (env->sregs[PS] & ~PS_INTLEVEL) | level | PS_EXCM;
            env->pc = relocated_vector(env,
                    env->config->interrupt_vector[level]);
        } else {
            env->sregs[EXCCAUSE] = LEVEL1_INTERRUPT_CAUSE;

            if (env->sregs[PS] & PS_EXCM) {
                if (env->config->ndepc) {
                    env->sregs[DEPC] = env->pc;
                } else {
                    env->sregs[EPC1] = env->pc;
                }
                cs->exception_index = EXC_DOUBLE;
            } else {
                env->sregs[EPC1] = env->pc;
                cs->exception_index =
                    (env->sregs[PS] & PS_UM) ? EXC_USER : EXC_KERNEL;
            }
            env->sregs[PS] |= PS_EXCM;
        }
        env->exception_taken = 1;
    }
}

/* Called from cpu_handle_interrupt with BQL held */
void xtensa_cpu_do_interrupt(CPUState *cs)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    if (cs->exception_index == EXC_IRQ) {
        qemu_log_mask(CPU_LOG_INT,
                "%s(EXC_IRQ) level = %d, cintlevel = %d, "
                "pc = %08x, a0 = %08x, ps = %08x, "
                "intset = %08x, intenable = %08x, "
                "ccount = %08x\n",
                __func__, env->pending_irq_level, xtensa_get_cintlevel(env),
                env->pc, env->regs[0], env->sregs[PS],
                env->sregs[INTSET], env->sregs[INTENABLE],
                env->sregs[CCOUNT]);
        handle_interrupt(env);
    }

    switch (cs->exception_index) {
    case EXC_WINDOW_OVERFLOW4:
    case EXC_WINDOW_UNDERFLOW4:
    case EXC_WINDOW_OVERFLOW8:
    case EXC_WINDOW_UNDERFLOW8:
    case EXC_WINDOW_OVERFLOW12:
    case EXC_WINDOW_UNDERFLOW12:
    case EXC_KERNEL:
    case EXC_USER:
    case EXC_DOUBLE:
    case EXC_DEBUG:
        qemu_log_mask(CPU_LOG_INT, "%s(%d) "
                "pc = %08x, a0 = %08x, ps = %08x, ccount = %08x\n",
                __func__, cs->exception_index,
                env->pc, env->regs[0], env->sregs[PS], env->sregs[CCOUNT]);
        if (env->config->exception_vector[cs->exception_index]) {
            env->pc = relocated_vector(env,
                    env->config->exception_vector[cs->exception_index]);
            env->exception_taken = 1;
        } else {
            qemu_log_mask(CPU_LOG_INT, "%s(pc = %08x) bad exception_index: %d\n",
                          __func__, env->pc, cs->exception_index);
        }
        break;

    case EXC_IRQ:
        break;

    default:
        qemu_log("%s(pc = %08x) unknown exception_index: %d\n",
                __func__, env->pc, cs->exception_index);
        break;
    }
    check_interrupts(env);
}
#else
void xtensa_cpu_do_interrupt(CPUState *cs)
{
}
#endif

bool xtensa_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        cs->exception_index = EXC_IRQ;
        xtensa_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}

#ifdef CONFIG_USER_ONLY

int xtensa_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int size, int rw,
                                int mmu_idx)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    qemu_log_mask(CPU_LOG_INT,
                  "%s: rw = %d, address = 0x%08" VADDR_PRIx ", size = %d\n",
                  __func__, rw, address, size);
    env->sregs[EXCVADDR] = address;
    env->sregs[EXCCAUSE] = rw ? STORE_PROHIBITED_CAUSE : LOAD_PROHIBITED_CAUSE;
    cs->exception_index = EXC_USER;
    return 1;
}

#else

void xtensa_runstall(CPUXtensaState *env, bool runstall)
{
    CPUState *cpu = CPU(xtensa_env_get_cpu(env));

    env->runstall = runstall;
    cpu->halted = runstall;
    if (runstall) {
        cpu_interrupt(cpu, CPU_INTERRUPT_HALT);
    } else {
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_HALT);
    }
}
#endif
