/*
 *  x86 SMM helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "helper.h"

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

    env->hflags |= HF_SMM_MASK;
    cpu_smm_update(env);

    sm_state = env->smbase + 0x8000;

#ifdef TARGET_X86_64
    for (i = 0; i < 6; i++) {
        dt = &env->segs[i];
        offset = 0x7e00 + i * 16;
        stw_phys(cs->as, sm_state + offset, dt->selector);
        stw_phys(cs->as, sm_state + offset + 2, (dt->flags >> 8) & 0xf0ff);
        stl_phys(cs->as, sm_state + offset + 4, dt->limit);
        stq_phys(cs->as, sm_state + offset + 8, dt->base);
    }

    stq_phys(cs->as, sm_state + 0x7e68, env->gdt.base);
    stl_phys(cs->as, sm_state + 0x7e64, env->gdt.limit);

    stw_phys(cs->as, sm_state + 0x7e70, env->ldt.selector);
    stq_phys(cs->as, sm_state + 0x7e78, env->ldt.base);
    stl_phys(cs->as, sm_state + 0x7e74, env->ldt.limit);
    stw_phys(cs->as, sm_state + 0x7e72, (env->ldt.flags >> 8) & 0xf0ff);

    stq_phys(cs->as, sm_state + 0x7e88, env->idt.base);
    stl_phys(cs->as, sm_state + 0x7e84, env->idt.limit);

    stw_phys(cs->as, sm_state + 0x7e90, env->tr.selector);
    stq_phys(cs->as, sm_state + 0x7e98, env->tr.base);
    stl_phys(cs->as, sm_state + 0x7e94, env->tr.limit);
    stw_phys(cs->as, sm_state + 0x7e92, (env->tr.flags >> 8) & 0xf0ff);

    stq_phys(cs->as, sm_state + 0x7ed0, env->efer);

    stq_phys(cs->as, sm_state + 0x7ff8, env->regs[R_EAX]);
    stq_phys(cs->as, sm_state + 0x7ff0, env->regs[R_ECX]);
    stq_phys(cs->as, sm_state + 0x7fe8, env->regs[R_EDX]);
    stq_phys(cs->as, sm_state + 0x7fe0, env->regs[R_EBX]);
    stq_phys(cs->as, sm_state + 0x7fd8, env->regs[R_ESP]);
    stq_phys(cs->as, sm_state + 0x7fd0, env->regs[R_EBP]);
    stq_phys(cs->as, sm_state + 0x7fc8, env->regs[R_ESI]);
    stq_phys(cs->as, sm_state + 0x7fc0, env->regs[R_EDI]);
    for (i = 8; i < 16; i++) {
        stq_phys(cs->as, sm_state + 0x7ff8 - i * 8, env->regs[i]);
    }
    stq_phys(cs->as, sm_state + 0x7f78, env->eip);
    stl_phys(cs->as, sm_state + 0x7f70, cpu_compute_eflags(env));
    stl_phys(cs->as, sm_state + 0x7f68, env->dr[6]);
    stl_phys(cs->as, sm_state + 0x7f60, env->dr[7]);

    stl_phys(cs->as, sm_state + 0x7f48, env->cr[4]);
    stl_phys(cs->as, sm_state + 0x7f50, env->cr[3]);
    stl_phys(cs->as, sm_state + 0x7f58, env->cr[0]);

    stl_phys(cs->as, sm_state + 0x7efc, SMM_REVISION_ID);
    stl_phys(cs->as, sm_state + 0x7f00, env->smbase);
#else
    stl_phys(cs->as, sm_state + 0x7ffc, env->cr[0]);
    stl_phys(cs->as, sm_state + 0x7ff8, env->cr[3]);
    stl_phys(cs->as, sm_state + 0x7ff4, cpu_compute_eflags(env));
    stl_phys(cs->as, sm_state + 0x7ff0, env->eip);
    stl_phys(cs->as, sm_state + 0x7fec, env->regs[R_EDI]);
    stl_phys(cs->as, sm_state + 0x7fe8, env->regs[R_ESI]);
    stl_phys(cs->as, sm_state + 0x7fe4, env->regs[R_EBP]);
    stl_phys(cs->as, sm_state + 0x7fe0, env->regs[R_ESP]);
    stl_phys(cs->as, sm_state + 0x7fdc, env->regs[R_EBX]);
    stl_phys(cs->as, sm_state + 0x7fd8, env->regs[R_EDX]);
    stl_phys(cs->as, sm_state + 0x7fd4, env->regs[R_ECX]);
    stl_phys(cs->as, sm_state + 0x7fd0, env->regs[R_EAX]);
    stl_phys(cs->as, sm_state + 0x7fcc, env->dr[6]);
    stl_phys(cs->as, sm_state + 0x7fc8, env->dr[7]);

    stl_phys(cs->as, sm_state + 0x7fc4, env->tr.selector);
    stl_phys(cs->as, sm_state + 0x7f64, env->tr.base);
    stl_phys(cs->as, sm_state + 0x7f60, env->tr.limit);
    stl_phys(cs->as, sm_state + 0x7f5c, (env->tr.flags >> 8) & 0xf0ff);

    stl_phys(cs->as, sm_state + 0x7fc0, env->ldt.selector);
    stl_phys(cs->as, sm_state + 0x7f80, env->ldt.base);
    stl_phys(cs->as, sm_state + 0x7f7c, env->ldt.limit);
    stl_phys(cs->as, sm_state + 0x7f78, (env->ldt.flags >> 8) & 0xf0ff);

    stl_phys(cs->as, sm_state + 0x7f74, env->gdt.base);
    stl_phys(cs->as, sm_state + 0x7f70, env->gdt.limit);

    stl_phys(cs->as, sm_state + 0x7f58, env->idt.base);
    stl_phys(cs->as, sm_state + 0x7f54, env->idt.limit);

    for (i = 0; i < 6; i++) {
        dt = &env->segs[i];
        if (i < 3) {
            offset = 0x7f84 + i * 12;
        } else {
            offset = 0x7f2c + (i - 3) * 12;
        }
        stl_phys(cs->as, sm_state + 0x7fa8 + i * 4, dt->selector);
        stl_phys(cs->as, sm_state + offset + 8, dt->base);
        stl_phys(cs->as, sm_state + offset + 4, dt->limit);
        stl_phys(cs->as, sm_state + offset, (dt->flags >> 8) & 0xf0ff);
    }
    stl_phys(cs->as, sm_state + 0x7f14, env->cr[4]);

    stl_phys(cs->as, sm_state + 0x7efc, SMM_REVISION_ID);
    stl_phys(cs->as, sm_state + 0x7ef8, env->smbase);
#endif
    /* init SMM cpu state */

#ifdef TARGET_X86_64
    cpu_load_efer(env, 0);
#endif
    cpu_load_eflags(env, 0, ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C |
                              DF_MASK));
    env->eip = 0x00008000;
    cpu_x86_load_seg_cache(env, R_CS, (env->smbase >> 4) & 0xffff, env->smbase,
                           0xffffffff, 0);
    cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0xffffffff, 0);
    cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0xffffffff, 0);
    cpu_x86_load_seg_cache(env, R_SS, 0, 0, 0xffffffff, 0);
    cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0xffffffff, 0);
    cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0xffffffff, 0);

    cpu_x86_update_cr0(env,
                       env->cr[0] & ~(CR0_PE_MASK | CR0_EM_MASK | CR0_TS_MASK |
                                      CR0_PG_MASK));
    cpu_x86_update_cr4(env, 0);
    env->dr[7] = 0x00000400;
    CC_OP = CC_OP_EFLAGS;
}

void helper_rsm(CPUX86State *env)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    target_ulong sm_state;
    int i, offset;
    uint32_t val;

    sm_state = env->smbase + 0x8000;
#ifdef TARGET_X86_64
    cpu_load_efer(env, ldq_phys(cs->as, sm_state + 0x7ed0));

    for (i = 0; i < 6; i++) {
        offset = 0x7e00 + i * 16;
        cpu_x86_load_seg_cache(env, i,
                               lduw_phys(cs->as, sm_state + offset),
                               ldq_phys(cs->as, sm_state + offset + 8),
                               ldl_phys(cs->as, sm_state + offset + 4),
                               (lduw_phys(cs->as, sm_state + offset + 2) &
                                0xf0ff) << 8);
    }

    env->gdt.base = ldq_phys(cs->as, sm_state + 0x7e68);
    env->gdt.limit = ldl_phys(cs->as, sm_state + 0x7e64);

    env->ldt.selector = lduw_phys(cs->as, sm_state + 0x7e70);
    env->ldt.base = ldq_phys(cs->as, sm_state + 0x7e78);
    env->ldt.limit = ldl_phys(cs->as, sm_state + 0x7e74);
    env->ldt.flags = (lduw_phys(cs->as, sm_state + 0x7e72) & 0xf0ff) << 8;

    env->idt.base = ldq_phys(cs->as, sm_state + 0x7e88);
    env->idt.limit = ldl_phys(cs->as, sm_state + 0x7e84);

    env->tr.selector = lduw_phys(cs->as, sm_state + 0x7e90);
    env->tr.base = ldq_phys(cs->as, sm_state + 0x7e98);
    env->tr.limit = ldl_phys(cs->as, sm_state + 0x7e94);
    env->tr.flags = (lduw_phys(cs->as, sm_state + 0x7e92) & 0xf0ff) << 8;

    env->regs[R_EAX] = ldq_phys(cs->as, sm_state + 0x7ff8);
    env->regs[R_ECX] = ldq_phys(cs->as, sm_state + 0x7ff0);
    env->regs[R_EDX] = ldq_phys(cs->as, sm_state + 0x7fe8);
    env->regs[R_EBX] = ldq_phys(cs->as, sm_state + 0x7fe0);
    env->regs[R_ESP] = ldq_phys(cs->as, sm_state + 0x7fd8);
    env->regs[R_EBP] = ldq_phys(cs->as, sm_state + 0x7fd0);
    env->regs[R_ESI] = ldq_phys(cs->as, sm_state + 0x7fc8);
    env->regs[R_EDI] = ldq_phys(cs->as, sm_state + 0x7fc0);
    for (i = 8; i < 16; i++) {
        env->regs[i] = ldq_phys(cs->as, sm_state + 0x7ff8 - i * 8);
    }
    env->eip = ldq_phys(cs->as, sm_state + 0x7f78);
    cpu_load_eflags(env, ldl_phys(cs->as, sm_state + 0x7f70),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    env->dr[6] = ldl_phys(cs->as, sm_state + 0x7f68);
    env->dr[7] = ldl_phys(cs->as, sm_state + 0x7f60);

    cpu_x86_update_cr4(env, ldl_phys(cs->as, sm_state + 0x7f48));
    cpu_x86_update_cr3(env, ldl_phys(cs->as, sm_state + 0x7f50));
    cpu_x86_update_cr0(env, ldl_phys(cs->as, sm_state + 0x7f58));

    val = ldl_phys(cs->as, sm_state + 0x7efc); /* revision ID */
    if (val & 0x20000) {
        env->smbase = ldl_phys(cs->as, sm_state + 0x7f00) & ~0x7fff;
    }
#else
    cpu_x86_update_cr0(env, ldl_phys(cs->as, sm_state + 0x7ffc));
    cpu_x86_update_cr3(env, ldl_phys(cs->as, sm_state + 0x7ff8));
    cpu_load_eflags(env, ldl_phys(cs->as, sm_state + 0x7ff4),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    env->eip = ldl_phys(cs->as, sm_state + 0x7ff0);
    env->regs[R_EDI] = ldl_phys(cs->as, sm_state + 0x7fec);
    env->regs[R_ESI] = ldl_phys(cs->as, sm_state + 0x7fe8);
    env->regs[R_EBP] = ldl_phys(cs->as, sm_state + 0x7fe4);
    env->regs[R_ESP] = ldl_phys(cs->as, sm_state + 0x7fe0);
    env->regs[R_EBX] = ldl_phys(cs->as, sm_state + 0x7fdc);
    env->regs[R_EDX] = ldl_phys(cs->as, sm_state + 0x7fd8);
    env->regs[R_ECX] = ldl_phys(cs->as, sm_state + 0x7fd4);
    env->regs[R_EAX] = ldl_phys(cs->as, sm_state + 0x7fd0);
    env->dr[6] = ldl_phys(cs->as, sm_state + 0x7fcc);
    env->dr[7] = ldl_phys(cs->as, sm_state + 0x7fc8);

    env->tr.selector = ldl_phys(cs->as, sm_state + 0x7fc4) & 0xffff;
    env->tr.base = ldl_phys(cs->as, sm_state + 0x7f64);
    env->tr.limit = ldl_phys(cs->as, sm_state + 0x7f60);
    env->tr.flags = (ldl_phys(cs->as, sm_state + 0x7f5c) & 0xf0ff) << 8;

    env->ldt.selector = ldl_phys(cs->as, sm_state + 0x7fc0) & 0xffff;
    env->ldt.base = ldl_phys(cs->as, sm_state + 0x7f80);
    env->ldt.limit = ldl_phys(cs->as, sm_state + 0x7f7c);
    env->ldt.flags = (ldl_phys(cs->as, sm_state + 0x7f78) & 0xf0ff) << 8;

    env->gdt.base = ldl_phys(cs->as, sm_state + 0x7f74);
    env->gdt.limit = ldl_phys(cs->as, sm_state + 0x7f70);

    env->idt.base = ldl_phys(cs->as, sm_state + 0x7f58);
    env->idt.limit = ldl_phys(cs->as, sm_state + 0x7f54);

    for (i = 0; i < 6; i++) {
        if (i < 3) {
            offset = 0x7f84 + i * 12;
        } else {
            offset = 0x7f2c + (i - 3) * 12;
        }
        cpu_x86_load_seg_cache(env, i,
                               ldl_phys(cs->as,
                                        sm_state + 0x7fa8 + i * 4) & 0xffff,
                               ldl_phys(cs->as, sm_state + offset + 8),
                               ldl_phys(cs->as, sm_state + offset + 4),
                               (ldl_phys(cs->as,
                                         sm_state + offset) & 0xf0ff) << 8);
    }
    cpu_x86_update_cr4(env, ldl_phys(cs->as, sm_state + 0x7f14));

    val = ldl_phys(cs->as, sm_state + 0x7efc); /* revision ID */
    if (val & 0x20000) {
        env->smbase = ldl_phys(cs->as, sm_state + 0x7ef8) & ~0x7fff;
    }
#endif
    CC_OP = CC_OP_EFLAGS;
    env->hflags &= ~HF_SMM_MASK;
    cpu_smm_update(env);

    qemu_log_mask(CPU_LOG_INT, "SMM: after RSM\n");
    log_cpu_state_mask(CPU_LOG_INT, CPU(cpu), CPU_DUMP_CCOP);
}

#endif /* !CONFIG_USER_ONLY */
