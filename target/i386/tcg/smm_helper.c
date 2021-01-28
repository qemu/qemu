/*
 *  x86 SMM helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/log.h"
#include "helper-tcg.h"


/* SMM support */

#if defined(CONFIG_USER_ONLY)

void do_smm_enter(X86CPU *cpu)
{
}

void helper_rsm(CPUX86State *env)
{
}

#else

#ifdef TARGET_X86_64
#define SMM_REVISION_ID 0x00020064
#else
#define SMM_REVISION_ID 0x00020000
#endif

void do_smm_enter(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    target_ulong sm_state;
    SegmentCache *dt;
    int i, offset;

    qemu_log_mask(CPU_LOG_INT, "SMM: enter\n");
    log_cpu_state_mask(CPU_LOG_INT, CPU(cpu), CPU_DUMP_CCOP);

    env->msr_smi_count++;
    env->hflags |= HF_SMM_MASK;
    if (env->hflags2 & HF2_NMI_MASK) {
        env->hflags2 |= HF2_SMM_INSIDE_NMI_MASK;
    } else {
        env->hflags2 |= HF2_NMI_MASK;
    }

    sm_state = env->smbase + 0x8000;

#ifdef TARGET_X86_64
    for (i = 0; i < 6; i++) {
        dt = &env->segs[i];
        offset = 0x7e00 + i * 16;
        x86_stw_phys(cs, sm_state + offset, dt->selector);
        x86_stw_phys(cs, sm_state + offset + 2, (dt->flags >> 8) & 0xf0ff);
        x86_stl_phys(cs, sm_state + offset + 4, dt->limit);
        x86_stq_phys(cs, sm_state + offset + 8, dt->base);
    }

    x86_stq_phys(cs, sm_state + 0x7e68, env->gdt.base);
    x86_stl_phys(cs, sm_state + 0x7e64, env->gdt.limit);

    x86_stw_phys(cs, sm_state + 0x7e70, env->ldt.selector);
    x86_stq_phys(cs, sm_state + 0x7e78, env->ldt.base);
    x86_stl_phys(cs, sm_state + 0x7e74, env->ldt.limit);
    x86_stw_phys(cs, sm_state + 0x7e72, (env->ldt.flags >> 8) & 0xf0ff);

    x86_stq_phys(cs, sm_state + 0x7e88, env->idt.base);
    x86_stl_phys(cs, sm_state + 0x7e84, env->idt.limit);

    x86_stw_phys(cs, sm_state + 0x7e90, env->tr.selector);
    x86_stq_phys(cs, sm_state + 0x7e98, env->tr.base);
    x86_stl_phys(cs, sm_state + 0x7e94, env->tr.limit);
    x86_stw_phys(cs, sm_state + 0x7e92, (env->tr.flags >> 8) & 0xf0ff);

    /* ??? Vol 1, 16.5.6 Intel MPX and SMM says that IA32_BNDCFGS
       is saved at offset 7ED0.  Vol 3, 34.4.1.1, Table 32-2, has
       7EA0-7ED7 as "reserved".  What's this, and what's really
       supposed to happen?  */
    x86_stq_phys(cs, sm_state + 0x7ed0, env->efer);

    x86_stq_phys(cs, sm_state + 0x7ff8, env->regs[R_EAX]);
    x86_stq_phys(cs, sm_state + 0x7ff0, env->regs[R_ECX]);
    x86_stq_phys(cs, sm_state + 0x7fe8, env->regs[R_EDX]);
    x86_stq_phys(cs, sm_state + 0x7fe0, env->regs[R_EBX]);
    x86_stq_phys(cs, sm_state + 0x7fd8, env->regs[R_ESP]);
    x86_stq_phys(cs, sm_state + 0x7fd0, env->regs[R_EBP]);
    x86_stq_phys(cs, sm_state + 0x7fc8, env->regs[R_ESI]);
    x86_stq_phys(cs, sm_state + 0x7fc0, env->regs[R_EDI]);
    for (i = 8; i < 16; i++) {
        x86_stq_phys(cs, sm_state + 0x7ff8 - i * 8, env->regs[i]);
    }
    x86_stq_phys(cs, sm_state + 0x7f78, env->eip);
    x86_stl_phys(cs, sm_state + 0x7f70, cpu_compute_eflags(env));
    x86_stl_phys(cs, sm_state + 0x7f68, env->dr[6]);
    x86_stl_phys(cs, sm_state + 0x7f60, env->dr[7]);

    x86_stl_phys(cs, sm_state + 0x7f48, env->cr[4]);
    x86_stq_phys(cs, sm_state + 0x7f50, env->cr[3]);
    x86_stl_phys(cs, sm_state + 0x7f58, env->cr[0]);

    x86_stl_phys(cs, sm_state + 0x7efc, SMM_REVISION_ID);
    x86_stl_phys(cs, sm_state + 0x7f00, env->smbase);
#else
    x86_stl_phys(cs, sm_state + 0x7ffc, env->cr[0]);
    x86_stl_phys(cs, sm_state + 0x7ff8, env->cr[3]);
    x86_stl_phys(cs, sm_state + 0x7ff4, cpu_compute_eflags(env));
    x86_stl_phys(cs, sm_state + 0x7ff0, env->eip);
    x86_stl_phys(cs, sm_state + 0x7fec, env->regs[R_EDI]);
    x86_stl_phys(cs, sm_state + 0x7fe8, env->regs[R_ESI]);
    x86_stl_phys(cs, sm_state + 0x7fe4, env->regs[R_EBP]);
    x86_stl_phys(cs, sm_state + 0x7fe0, env->regs[R_ESP]);
    x86_stl_phys(cs, sm_state + 0x7fdc, env->regs[R_EBX]);
    x86_stl_phys(cs, sm_state + 0x7fd8, env->regs[R_EDX]);
    x86_stl_phys(cs, sm_state + 0x7fd4, env->regs[R_ECX]);
    x86_stl_phys(cs, sm_state + 0x7fd0, env->regs[R_EAX]);
    x86_stl_phys(cs, sm_state + 0x7fcc, env->dr[6]);
    x86_stl_phys(cs, sm_state + 0x7fc8, env->dr[7]);

    x86_stl_phys(cs, sm_state + 0x7fc4, env->tr.selector);
    x86_stl_phys(cs, sm_state + 0x7f64, env->tr.base);
    x86_stl_phys(cs, sm_state + 0x7f60, env->tr.limit);
    x86_stl_phys(cs, sm_state + 0x7f5c, (env->tr.flags >> 8) & 0xf0ff);

    x86_stl_phys(cs, sm_state + 0x7fc0, env->ldt.selector);
    x86_stl_phys(cs, sm_state + 0x7f80, env->ldt.base);
    x86_stl_phys(cs, sm_state + 0x7f7c, env->ldt.limit);
    x86_stl_phys(cs, sm_state + 0x7f78, (env->ldt.flags >> 8) & 0xf0ff);

    x86_stl_phys(cs, sm_state + 0x7f74, env->gdt.base);
    x86_stl_phys(cs, sm_state + 0x7f70, env->gdt.limit);

    x86_stl_phys(cs, sm_state + 0x7f58, env->idt.base);
    x86_stl_phys(cs, sm_state + 0x7f54, env->idt.limit);

    for (i = 0; i < 6; i++) {
        dt = &env->segs[i];
        if (i < 3) {
            offset = 0x7f84 + i * 12;
        } else {
            offset = 0x7f2c + (i - 3) * 12;
        }
        x86_stl_phys(cs, sm_state + 0x7fa8 + i * 4, dt->selector);
        x86_stl_phys(cs, sm_state + offset + 8, dt->base);
        x86_stl_phys(cs, sm_state + offset + 4, dt->limit);
        x86_stl_phys(cs, sm_state + offset, (dt->flags >> 8) & 0xf0ff);
    }
    x86_stl_phys(cs, sm_state + 0x7f14, env->cr[4]);

    x86_stl_phys(cs, sm_state + 0x7efc, SMM_REVISION_ID);
    x86_stl_phys(cs, sm_state + 0x7ef8, env->smbase);
#endif
    /* init SMM cpu state */

#ifdef TARGET_X86_64
    cpu_load_efer(env, 0);
#endif
    cpu_load_eflags(env, 0, ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C |
                              DF_MASK));
    env->eip = 0x00008000;
    cpu_x86_update_cr0(env,
                       env->cr[0] & ~(CR0_PE_MASK | CR0_EM_MASK | CR0_TS_MASK |
                                      CR0_PG_MASK));
    cpu_x86_update_cr4(env, 0);
    env->dr[7] = 0x00000400;

    cpu_x86_load_seg_cache(env, R_CS, (env->smbase >> 4) & 0xffff, env->smbase,
                           0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_SS, 0, 0, 0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
}

void helper_rsm(CPUX86State *env)
{
    X86CPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);
    target_ulong sm_state;
    int i, offset;
    uint32_t val;

    sm_state = env->smbase + 0x8000;
#ifdef TARGET_X86_64
    cpu_load_efer(env, x86_ldq_phys(cs, sm_state + 0x7ed0));

    env->gdt.base = x86_ldq_phys(cs, sm_state + 0x7e68);
    env->gdt.limit = x86_ldl_phys(cs, sm_state + 0x7e64);

    env->ldt.selector = x86_lduw_phys(cs, sm_state + 0x7e70);
    env->ldt.base = x86_ldq_phys(cs, sm_state + 0x7e78);
    env->ldt.limit = x86_ldl_phys(cs, sm_state + 0x7e74);
    env->ldt.flags = (x86_lduw_phys(cs, sm_state + 0x7e72) & 0xf0ff) << 8;

    env->idt.base = x86_ldq_phys(cs, sm_state + 0x7e88);
    env->idt.limit = x86_ldl_phys(cs, sm_state + 0x7e84);

    env->tr.selector = x86_lduw_phys(cs, sm_state + 0x7e90);
    env->tr.base = x86_ldq_phys(cs, sm_state + 0x7e98);
    env->tr.limit = x86_ldl_phys(cs, sm_state + 0x7e94);
    env->tr.flags = (x86_lduw_phys(cs, sm_state + 0x7e92) & 0xf0ff) << 8;

    env->regs[R_EAX] = x86_ldq_phys(cs, sm_state + 0x7ff8);
    env->regs[R_ECX] = x86_ldq_phys(cs, sm_state + 0x7ff0);
    env->regs[R_EDX] = x86_ldq_phys(cs, sm_state + 0x7fe8);
    env->regs[R_EBX] = x86_ldq_phys(cs, sm_state + 0x7fe0);
    env->regs[R_ESP] = x86_ldq_phys(cs, sm_state + 0x7fd8);
    env->regs[R_EBP] = x86_ldq_phys(cs, sm_state + 0x7fd0);
    env->regs[R_ESI] = x86_ldq_phys(cs, sm_state + 0x7fc8);
    env->regs[R_EDI] = x86_ldq_phys(cs, sm_state + 0x7fc0);
    for (i = 8; i < 16; i++) {
        env->regs[i] = x86_ldq_phys(cs, sm_state + 0x7ff8 - i * 8);
    }
    env->eip = x86_ldq_phys(cs, sm_state + 0x7f78);
    cpu_load_eflags(env, x86_ldl_phys(cs, sm_state + 0x7f70),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    env->dr[6] = x86_ldl_phys(cs, sm_state + 0x7f68);
    env->dr[7] = x86_ldl_phys(cs, sm_state + 0x7f60);

    cpu_x86_update_cr4(env, x86_ldl_phys(cs, sm_state + 0x7f48));
    cpu_x86_update_cr3(env, x86_ldq_phys(cs, sm_state + 0x7f50));
    cpu_x86_update_cr0(env, x86_ldl_phys(cs, sm_state + 0x7f58));

    for (i = 0; i < 6; i++) {
        offset = 0x7e00 + i * 16;
        cpu_x86_load_seg_cache(env, i,
                               x86_lduw_phys(cs, sm_state + offset),
                               x86_ldq_phys(cs, sm_state + offset + 8),
                               x86_ldl_phys(cs, sm_state + offset + 4),
                               (x86_lduw_phys(cs, sm_state + offset + 2) &
                                0xf0ff) << 8);
    }

    val = x86_ldl_phys(cs, sm_state + 0x7efc); /* revision ID */
    if (val & 0x20000) {
        env->smbase = x86_ldl_phys(cs, sm_state + 0x7f00);
    }
#else
    cpu_x86_update_cr0(env, x86_ldl_phys(cs, sm_state + 0x7ffc));
    cpu_x86_update_cr3(env, x86_ldl_phys(cs, sm_state + 0x7ff8));
    cpu_load_eflags(env, x86_ldl_phys(cs, sm_state + 0x7ff4),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    env->eip = x86_ldl_phys(cs, sm_state + 0x7ff0);
    env->regs[R_EDI] = x86_ldl_phys(cs, sm_state + 0x7fec);
    env->regs[R_ESI] = x86_ldl_phys(cs, sm_state + 0x7fe8);
    env->regs[R_EBP] = x86_ldl_phys(cs, sm_state + 0x7fe4);
    env->regs[R_ESP] = x86_ldl_phys(cs, sm_state + 0x7fe0);
    env->regs[R_EBX] = x86_ldl_phys(cs, sm_state + 0x7fdc);
    env->regs[R_EDX] = x86_ldl_phys(cs, sm_state + 0x7fd8);
    env->regs[R_ECX] = x86_ldl_phys(cs, sm_state + 0x7fd4);
    env->regs[R_EAX] = x86_ldl_phys(cs, sm_state + 0x7fd0);
    env->dr[6] = x86_ldl_phys(cs, sm_state + 0x7fcc);
    env->dr[7] = x86_ldl_phys(cs, sm_state + 0x7fc8);

    env->tr.selector = x86_ldl_phys(cs, sm_state + 0x7fc4) & 0xffff;
    env->tr.base = x86_ldl_phys(cs, sm_state + 0x7f64);
    env->tr.limit = x86_ldl_phys(cs, sm_state + 0x7f60);
    env->tr.flags = (x86_ldl_phys(cs, sm_state + 0x7f5c) & 0xf0ff) << 8;

    env->ldt.selector = x86_ldl_phys(cs, sm_state + 0x7fc0) & 0xffff;
    env->ldt.base = x86_ldl_phys(cs, sm_state + 0x7f80);
    env->ldt.limit = x86_ldl_phys(cs, sm_state + 0x7f7c);
    env->ldt.flags = (x86_ldl_phys(cs, sm_state + 0x7f78) & 0xf0ff) << 8;

    env->gdt.base = x86_ldl_phys(cs, sm_state + 0x7f74);
    env->gdt.limit = x86_ldl_phys(cs, sm_state + 0x7f70);

    env->idt.base = x86_ldl_phys(cs, sm_state + 0x7f58);
    env->idt.limit = x86_ldl_phys(cs, sm_state + 0x7f54);

    for (i = 0; i < 6; i++) {
        if (i < 3) {
            offset = 0x7f84 + i * 12;
        } else {
            offset = 0x7f2c + (i - 3) * 12;
        }
        cpu_x86_load_seg_cache(env, i,
                               x86_ldl_phys(cs,
                                        sm_state + 0x7fa8 + i * 4) & 0xffff,
                               x86_ldl_phys(cs, sm_state + offset + 8),
                               x86_ldl_phys(cs, sm_state + offset + 4),
                               (x86_ldl_phys(cs,
                                         sm_state + offset) & 0xf0ff) << 8);
    }
    cpu_x86_update_cr4(env, x86_ldl_phys(cs, sm_state + 0x7f14));

    val = x86_ldl_phys(cs, sm_state + 0x7efc); /* revision ID */
    if (val & 0x20000) {
        env->smbase = x86_ldl_phys(cs, sm_state + 0x7ef8);
    }
#endif
    if ((env->hflags2 & HF2_SMM_INSIDE_NMI_MASK) == 0) {
        env->hflags2 &= ~HF2_NMI_MASK;
    }
    env->hflags2 &= ~HF2_SMM_INSIDE_NMI_MASK;
    env->hflags &= ~HF_SMM_MASK;

    qemu_log_mask(CPU_LOG_INT, "SMM: after RSM\n");
    log_cpu_state_mask(CPU_LOG_INT, CPU(cpu), CPU_DUMP_CCOP);
}

#endif /* !CONFIG_USER_ONLY */
