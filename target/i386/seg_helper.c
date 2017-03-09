/*
 *  x86 segmentation related helpers:
 *  TSS, interrupts, system calls, jumps and call/task gates, descriptors
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/log.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/log.h"

//#define DEBUG_PCALL

#ifdef DEBUG_PCALL
# define LOG_PCALL(...) qemu_log_mask(CPU_LOG_PCALL, ## __VA_ARGS__)
# define LOG_PCALL_STATE(cpu)                                  \
    log_cpu_state_mask(CPU_LOG_PCALL, (cpu), CPU_DUMP_CCOP)
#else
# define LOG_PCALL(...) do { } while (0)
# define LOG_PCALL_STATE(cpu) do { } while (0)
#endif

#ifdef CONFIG_USER_ONLY
#define MEMSUFFIX _kernel
#define DATA_SIZE 1
#include "exec/cpu_ldst_useronly_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_useronly_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_useronly_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_useronly_template.h"
#undef MEMSUFFIX
#else
#define CPU_MMU_INDEX (cpu_mmu_index_kernel(env))
#define MEMSUFFIX _kernel
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif

/* return non zero if error */
static inline int load_segment_ra(CPUX86State *env, uint32_t *e1_ptr,
                               uint32_t *e2_ptr, int selector,
                               uintptr_t retaddr)
{
    SegmentCache *dt;
    int index;
    target_ulong ptr;

    if (selector & 0x4) {
        dt = &env->ldt;
    } else {
        dt = &env->gdt;
    }
    index = selector & ~7;
    if ((index + 7) > dt->limit) {
        return -1;
    }
    ptr = dt->base + index;
    *e1_ptr = cpu_ldl_kernel_ra(env, ptr, retaddr);
    *e2_ptr = cpu_ldl_kernel_ra(env, ptr + 4, retaddr);
    return 0;
}

static inline int load_segment(CPUX86State *env, uint32_t *e1_ptr,
                               uint32_t *e2_ptr, int selector)
{
    return load_segment_ra(env, e1_ptr, e2_ptr, selector, 0);
}

static inline unsigned int get_seg_limit(uint32_t e1, uint32_t e2)
{
    unsigned int limit;

    limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & DESC_G_MASK) {
        limit = (limit << 12) | 0xfff;
    }
    return limit;
}

static inline uint32_t get_seg_base(uint32_t e1, uint32_t e2)
{
    return (e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000);
}

static inline void load_seg_cache_raw_dt(SegmentCache *sc, uint32_t e1,
                                         uint32_t e2)
{
    sc->base = get_seg_base(e1, e2);
    sc->limit = get_seg_limit(e1, e2);
    sc->flags = e2;
}

/* init the segment cache in vm86 mode. */
static inline void load_seg_vm(CPUX86State *env, int seg, int selector)
{
    selector &= 0xffff;

    cpu_x86_load_seg_cache(env, seg, selector, (selector << 4), 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK | (3 << DESC_DPL_SHIFT));
}

static inline void get_ss_esp_from_tss(CPUX86State *env, uint32_t *ss_ptr,
                                       uint32_t *esp_ptr, int dpl,
                                       uintptr_t retaddr)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    int type, index, shift;

#if 0
    {
        int i;
        printf("TR: base=%p limit=%x\n", env->tr.base, env->tr.limit);
        for (i = 0; i < env->tr.limit; i++) {
            printf("%02x ", env->tr.base[i]);
            if ((i & 7) == 7) {
                printf("\n");
            }
        }
        printf("\n");
    }
#endif

    if (!(env->tr.flags & DESC_P_MASK)) {
        cpu_abort(CPU(cpu), "invalid tss");
    }
    type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
    if ((type & 7) != 1) {
        cpu_abort(CPU(cpu), "invalid tss type");
    }
    shift = type >> 3;
    index = (dpl * 4 + 2) << shift;
    if (index + (4 << shift) - 1 > env->tr.limit) {
        raise_exception_err_ra(env, EXCP0A_TSS, env->tr.selector & 0xfffc, retaddr);
    }
    if (shift == 0) {
        *esp_ptr = cpu_lduw_kernel_ra(env, env->tr.base + index, retaddr);
        *ss_ptr = cpu_lduw_kernel_ra(env, env->tr.base + index + 2, retaddr);
    } else {
        *esp_ptr = cpu_ldl_kernel_ra(env, env->tr.base + index, retaddr);
        *ss_ptr = cpu_lduw_kernel_ra(env, env->tr.base + index + 4, retaddr);
    }
}

static void tss_load_seg(CPUX86State *env, int seg_reg, int selector, int cpl,
                         uintptr_t retaddr)
{
    uint32_t e1, e2;
    int rpl, dpl;

    if ((selector & 0xfffc) != 0) {
        if (load_segment_ra(env, &e1, &e2, selector, retaddr) != 0) {
            raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
        }
        if (!(e2 & DESC_S_MASK)) {
            raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
        }
        rpl = selector & 3;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (seg_reg == R_CS) {
            if (!(e2 & DESC_CS_MASK)) {
                raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
            }
            if (dpl != rpl) {
                raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
            }
        } else if (seg_reg == R_SS) {
            /* SS must be writable data */
            if ((e2 & DESC_CS_MASK) || !(e2 & DESC_W_MASK)) {
                raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
            }
            if (dpl != cpl || dpl != rpl) {
                raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
            }
        } else {
            /* not readable code */
            if ((e2 & DESC_CS_MASK) && !(e2 & DESC_R_MASK)) {
                raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
            }
            /* if data or non conforming code, checks the rights */
            if (((e2 >> DESC_TYPE_SHIFT) & 0xf) < 12) {
                if (dpl < cpl || dpl < rpl) {
                    raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
                }
            }
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, retaddr);
        }
        cpu_x86_load_seg_cache(env, seg_reg, selector,
                               get_seg_base(e1, e2),
                               get_seg_limit(e1, e2),
                               e2);
    } else {
        if (seg_reg == R_SS || seg_reg == R_CS) {
            raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
        }
    }
}

#define SWITCH_TSS_JMP  0
#define SWITCH_TSS_IRET 1
#define SWITCH_TSS_CALL 2

/* XXX: restore CPU state in registers (PowerPC case) */
static void switch_tss_ra(CPUX86State *env, int tss_selector,
                          uint32_t e1, uint32_t e2, int source,
                          uint32_t next_eip, uintptr_t retaddr)
{
    int tss_limit, tss_limit_max, type, old_tss_limit_max, old_type, v1, v2, i;
    target_ulong tss_base;
    uint32_t new_regs[8], new_segs[6];
    uint32_t new_eflags, new_eip, new_cr3, new_ldt, new_trap;
    uint32_t old_eflags, eflags_mask;
    SegmentCache *dt;
    int index;
    target_ulong ptr;

    type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
    LOG_PCALL("switch_tss: sel=0x%04x type=%d src=%d\n", tss_selector, type,
              source);

    /* if task gate, we read the TSS segment and we load it */
    if (type == 5) {
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, tss_selector & 0xfffc, retaddr);
        }
        tss_selector = e1 >> 16;
        if (tss_selector & 4) {
            raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, retaddr);
        }
        if (load_segment_ra(env, &e1, &e2, tss_selector, retaddr) != 0) {
            raise_exception_err_ra(env, EXCP0D_GPF, tss_selector & 0xfffc, retaddr);
        }
        if (e2 & DESC_S_MASK) {
            raise_exception_err_ra(env, EXCP0D_GPF, tss_selector & 0xfffc, retaddr);
        }
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        if ((type & 7) != 1) {
            raise_exception_err_ra(env, EXCP0D_GPF, tss_selector & 0xfffc, retaddr);
        }
    }

    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err_ra(env, EXCP0B_NOSEG, tss_selector & 0xfffc, retaddr);
    }

    if (type & 8) {
        tss_limit_max = 103;
    } else {
        tss_limit_max = 43;
    }
    tss_limit = get_seg_limit(e1, e2);
    tss_base = get_seg_base(e1, e2);
    if ((tss_selector & 4) != 0 ||
        tss_limit < tss_limit_max) {
        raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, retaddr);
    }
    old_type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
    if (old_type & 8) {
        old_tss_limit_max = 103;
    } else {
        old_tss_limit_max = 43;
    }

    /* read all the registers from the new TSS */
    if (type & 8) {
        /* 32 bit */
        new_cr3 = cpu_ldl_kernel_ra(env, tss_base + 0x1c, retaddr);
        new_eip = cpu_ldl_kernel_ra(env, tss_base + 0x20, retaddr);
        new_eflags = cpu_ldl_kernel_ra(env, tss_base + 0x24, retaddr);
        for (i = 0; i < 8; i++) {
            new_regs[i] = cpu_ldl_kernel_ra(env, tss_base + (0x28 + i * 4),
                                            retaddr);
        }
        for (i = 0; i < 6; i++) {
            new_segs[i] = cpu_lduw_kernel_ra(env, tss_base + (0x48 + i * 4),
                                             retaddr);
        }
        new_ldt = cpu_lduw_kernel_ra(env, tss_base + 0x60, retaddr);
        new_trap = cpu_ldl_kernel_ra(env, tss_base + 0x64, retaddr);
    } else {
        /* 16 bit */
        new_cr3 = 0;
        new_eip = cpu_lduw_kernel_ra(env, tss_base + 0x0e, retaddr);
        new_eflags = cpu_lduw_kernel_ra(env, tss_base + 0x10, retaddr);
        for (i = 0; i < 8; i++) {
            new_regs[i] = cpu_lduw_kernel_ra(env, tss_base + (0x12 + i * 2),
                                             retaddr) | 0xffff0000;
        }
        for (i = 0; i < 4; i++) {
            new_segs[i] = cpu_lduw_kernel_ra(env, tss_base + (0x22 + i * 4),
                                             retaddr);
        }
        new_ldt = cpu_lduw_kernel_ra(env, tss_base + 0x2a, retaddr);
        new_segs[R_FS] = 0;
        new_segs[R_GS] = 0;
        new_trap = 0;
    }
    /* XXX: avoid a compiler warning, see
     http://support.amd.com/us/Processor_TechDocs/24593.pdf
     chapters 12.2.5 and 13.2.4 on how to implement TSS Trap bit */
    (void)new_trap;

    /* NOTE: we must avoid memory exceptions during the task switch,
       so we make dummy accesses before */
    /* XXX: it can still fail in some cases, so a bigger hack is
       necessary to valid the TLB after having done the accesses */

    v1 = cpu_ldub_kernel_ra(env, env->tr.base, retaddr);
    v2 = cpu_ldub_kernel_ra(env, env->tr.base + old_tss_limit_max, retaddr);
    cpu_stb_kernel_ra(env, env->tr.base, v1, retaddr);
    cpu_stb_kernel_ra(env, env->tr.base + old_tss_limit_max, v2, retaddr);

    /* clear busy bit (it is restartable) */
    if (source == SWITCH_TSS_JMP || source == SWITCH_TSS_IRET) {
        target_ulong ptr;
        uint32_t e2;

        ptr = env->gdt.base + (env->tr.selector & ~7);
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, retaddr);
        e2 &= ~DESC_TSS_BUSY_MASK;
        cpu_stl_kernel_ra(env, ptr + 4, e2, retaddr);
    }
    old_eflags = cpu_compute_eflags(env);
    if (source == SWITCH_TSS_IRET) {
        old_eflags &= ~NT_MASK;
    }

    /* save the current state in the old TSS */
    if (type & 8) {
        /* 32 bit */
        cpu_stl_kernel_ra(env, env->tr.base + 0x20, next_eip, retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + 0x24, old_eflags, retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 0 * 4), env->regs[R_EAX], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 1 * 4), env->regs[R_ECX], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 2 * 4), env->regs[R_EDX], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 3 * 4), env->regs[R_EBX], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 4 * 4), env->regs[R_ESP], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 5 * 4), env->regs[R_EBP], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 6 * 4), env->regs[R_ESI], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 7 * 4), env->regs[R_EDI], retaddr);
        for (i = 0; i < 6; i++) {
            cpu_stw_kernel_ra(env, env->tr.base + (0x48 + i * 4),
                              env->segs[i].selector, retaddr);
        }
    } else {
        /* 16 bit */
        cpu_stw_kernel_ra(env, env->tr.base + 0x0e, next_eip, retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + 0x10, old_eflags, retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 0 * 2), env->regs[R_EAX], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 1 * 2), env->regs[R_ECX], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 2 * 2), env->regs[R_EDX], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 3 * 2), env->regs[R_EBX], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 4 * 2), env->regs[R_ESP], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 5 * 2), env->regs[R_EBP], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 6 * 2), env->regs[R_ESI], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 7 * 2), env->regs[R_EDI], retaddr);
        for (i = 0; i < 4; i++) {
            cpu_stw_kernel_ra(env, env->tr.base + (0x22 + i * 4),
                              env->segs[i].selector, retaddr);
        }
    }

    /* now if an exception occurs, it will occurs in the next task
       context */

    if (source == SWITCH_TSS_CALL) {
        cpu_stw_kernel_ra(env, tss_base, env->tr.selector, retaddr);
        new_eflags |= NT_MASK;
    }

    /* set busy bit */
    if (source == SWITCH_TSS_JMP || source == SWITCH_TSS_CALL) {
        target_ulong ptr;
        uint32_t e2;

        ptr = env->gdt.base + (tss_selector & ~7);
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, retaddr);
        e2 |= DESC_TSS_BUSY_MASK;
        cpu_stl_kernel_ra(env, ptr + 4, e2, retaddr);
    }

    /* set the new CPU state */
    /* from this point, any exception which occurs can give problems */
    env->cr[0] |= CR0_TS_MASK;
    env->hflags |= HF_TS_MASK;
    env->tr.selector = tss_selector;
    env->tr.base = tss_base;
    env->tr.limit = tss_limit;
    env->tr.flags = e2 & ~DESC_TSS_BUSY_MASK;

    if ((type & 8) && (env->cr[0] & CR0_PG_MASK)) {
        cpu_x86_update_cr3(env, new_cr3);
    }

    /* load all registers without an exception, then reload them with
       possible exception */
    env->eip = new_eip;
    eflags_mask = TF_MASK | AC_MASK | ID_MASK |
        IF_MASK | IOPL_MASK | VM_MASK | RF_MASK | NT_MASK;
    if (!(type & 8)) {
        eflags_mask &= 0xffff;
    }
    cpu_load_eflags(env, new_eflags, eflags_mask);
    /* XXX: what to do in 16 bit case? */
    env->regs[R_EAX] = new_regs[0];
    env->regs[R_ECX] = new_regs[1];
    env->regs[R_EDX] = new_regs[2];
    env->regs[R_EBX] = new_regs[3];
    env->regs[R_ESP] = new_regs[4];
    env->regs[R_EBP] = new_regs[5];
    env->regs[R_ESI] = new_regs[6];
    env->regs[R_EDI] = new_regs[7];
    if (new_eflags & VM_MASK) {
        for (i = 0; i < 6; i++) {
            load_seg_vm(env, i, new_segs[i]);
        }
    } else {
        /* first just selectors as the rest may trigger exceptions */
        for (i = 0; i < 6; i++) {
            cpu_x86_load_seg_cache(env, i, new_segs[i], 0, 0, 0);
        }
    }

    env->ldt.selector = new_ldt & ~4;
    env->ldt.base = 0;
    env->ldt.limit = 0;
    env->ldt.flags = 0;

    /* load the LDT */
    if (new_ldt & 4) {
        raise_exception_err_ra(env, EXCP0A_TSS, new_ldt & 0xfffc, retaddr);
    }

    if ((new_ldt & 0xfffc) != 0) {
        dt = &env->gdt;
        index = new_ldt & ~7;
        if ((index + 7) > dt->limit) {
            raise_exception_err_ra(env, EXCP0A_TSS, new_ldt & 0xfffc, retaddr);
        }
        ptr = dt->base + index;
        e1 = cpu_ldl_kernel_ra(env, ptr, retaddr);
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, retaddr);
        if ((e2 & DESC_S_MASK) || ((e2 >> DESC_TYPE_SHIFT) & 0xf) != 2) {
            raise_exception_err_ra(env, EXCP0A_TSS, new_ldt & 0xfffc, retaddr);
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0A_TSS, new_ldt & 0xfffc, retaddr);
        }
        load_seg_cache_raw_dt(&env->ldt, e1, e2);
    }

    /* load the segments */
    if (!(new_eflags & VM_MASK)) {
        int cpl = new_segs[R_CS] & 3;
        tss_load_seg(env, R_CS, new_segs[R_CS], cpl, retaddr);
        tss_load_seg(env, R_SS, new_segs[R_SS], cpl, retaddr);
        tss_load_seg(env, R_ES, new_segs[R_ES], cpl, retaddr);
        tss_load_seg(env, R_DS, new_segs[R_DS], cpl, retaddr);
        tss_load_seg(env, R_FS, new_segs[R_FS], cpl, retaddr);
        tss_load_seg(env, R_GS, new_segs[R_GS], cpl, retaddr);
    }

    /* check that env->eip is in the CS segment limits */
    if (new_eip > env->segs[R_CS].limit) {
        /* XXX: different exception if CALL? */
        raise_exception_err_ra(env, EXCP0D_GPF, 0, retaddr);
    }

#ifndef CONFIG_USER_ONLY
    /* reset local breakpoints */
    if (env->dr[7] & DR7_LOCAL_BP_MASK) {
        cpu_x86_update_dr7(env, env->dr[7] & ~DR7_LOCAL_BP_MASK);
    }
#endif
}

static void switch_tss(CPUX86State *env, int tss_selector,
                       uint32_t e1, uint32_t e2, int source,
                        uint32_t next_eip)
{
    switch_tss_ra(env, tss_selector, e1, e2, source, next_eip, 0);
}

static inline unsigned int get_sp_mask(unsigned int e2)
{
    if (e2 & DESC_B_MASK) {
        return 0xffffffff;
    } else {
        return 0xffff;
    }
}

static int exception_has_error_code(int intno)
{
    switch (intno) {
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 17:
        return 1;
    }
    return 0;
}

#ifdef TARGET_X86_64
#define SET_ESP(val, sp_mask)                                   \
    do {                                                        \
        if ((sp_mask) == 0xffff) {                              \
            env->regs[R_ESP] = (env->regs[R_ESP] & ~0xffff) |   \
                ((val) & 0xffff);                               \
        } else if ((sp_mask) == 0xffffffffLL) {                 \
            env->regs[R_ESP] = (uint32_t)(val);                 \
        } else {                                                \
            env->regs[R_ESP] = (val);                           \
        }                                                       \
    } while (0)
#else
#define SET_ESP(val, sp_mask)                                   \
    do {                                                        \
        env->regs[R_ESP] = (env->regs[R_ESP] & ~(sp_mask)) |    \
            ((val) & (sp_mask));                                \
    } while (0)
#endif

/* in 64-bit machines, this can overflow. So this segment addition macro
 * can be used to trim the value to 32-bit whenever needed */
#define SEG_ADDL(ssp, sp, sp_mask) ((uint32_t)((ssp) + (sp & (sp_mask))))

/* XXX: add a is_user flag to have proper security support */
#define PUSHW_RA(ssp, sp, sp_mask, val, ra)                      \
    {                                                            \
        sp -= 2;                                                 \
        cpu_stw_kernel_ra(env, (ssp) + (sp & (sp_mask)), (val), ra); \
    }

#define PUSHL_RA(ssp, sp, sp_mask, val, ra)                             \
    {                                                                   \
        sp -= 4;                                                        \
        cpu_stl_kernel_ra(env, SEG_ADDL(ssp, sp, sp_mask), (uint32_t)(val), ra); \
    }

#define POPW_RA(ssp, sp, sp_mask, val, ra)                       \
    {                                                            \
        val = cpu_lduw_kernel_ra(env, (ssp) + (sp & (sp_mask)), ra); \
        sp += 2;                                                 \
    }

#define POPL_RA(ssp, sp, sp_mask, val, ra)                              \
    {                                                                   \
        val = (uint32_t)cpu_ldl_kernel_ra(env, SEG_ADDL(ssp, sp, sp_mask), ra); \
        sp += 4;                                                        \
    }

#define PUSHW(ssp, sp, sp_mask, val) PUSHW_RA(ssp, sp, sp_mask, val, 0)
#define PUSHL(ssp, sp, sp_mask, val) PUSHL_RA(ssp, sp, sp_mask, val, 0)
#define POPW(ssp, sp, sp_mask, val) POPW_RA(ssp, sp, sp_mask, val, 0)
#define POPL(ssp, sp, sp_mask, val) POPL_RA(ssp, sp, sp_mask, val, 0)

/* protected mode interrupt */
static void do_interrupt_protected(CPUX86State *env, int intno, int is_int,
                                   int error_code, unsigned int next_eip,
                                   int is_hw)
{
    SegmentCache *dt;
    target_ulong ptr, ssp;
    int type, dpl, selector, ss_dpl, cpl;
    int has_error_code, new_stack, shift;
    uint32_t e1, e2, offset, ss = 0, esp, ss_e1 = 0, ss_e2 = 0;
    uint32_t old_eip, sp_mask;
    int vm86 = env->eflags & VM_MASK;

    has_error_code = 0;
    if (!is_int && !is_hw) {
        has_error_code = exception_has_error_code(intno);
    }
    if (is_int) {
        old_eip = next_eip;
    } else {
        old_eip = env->eip;
    }

    dt = &env->idt;
    if (intno * 8 + 7 > dt->limit) {
        raise_exception_err(env, EXCP0D_GPF, intno * 8 + 2);
    }
    ptr = dt->base + intno * 8;
    e1 = cpu_ldl_kernel(env, ptr);
    e2 = cpu_ldl_kernel(env, ptr + 4);
    /* check gate type */
    type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
    switch (type) {
    case 5: /* task gate */
        /* must do that check here to return the correct error code */
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0B_NOSEG, intno * 8 + 2);
        }
        switch_tss(env, intno * 8, e1, e2, SWITCH_TSS_CALL, old_eip);
        if (has_error_code) {
            int type;
            uint32_t mask;

            /* push the error code */
            type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
            shift = type >> 3;
            if (env->segs[R_SS].flags & DESC_B_MASK) {
                mask = 0xffffffff;
            } else {
                mask = 0xffff;
            }
            esp = (env->regs[R_ESP] - (2 << shift)) & mask;
            ssp = env->segs[R_SS].base + esp;
            if (shift) {
                cpu_stl_kernel(env, ssp, error_code);
            } else {
                cpu_stw_kernel(env, ssp, error_code);
            }
            SET_ESP(esp, mask);
        }
        return;
    case 6: /* 286 interrupt gate */
    case 7: /* 286 trap gate */
    case 14: /* 386 interrupt gate */
    case 15: /* 386 trap gate */
        break;
    default:
        raise_exception_err(env, EXCP0D_GPF, intno * 8 + 2);
        break;
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    /* check privilege if software int */
    if (is_int && dpl < cpl) {
        raise_exception_err(env, EXCP0D_GPF, intno * 8 + 2);
    }
    /* check valid bit */
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err(env, EXCP0B_NOSEG, intno * 8 + 2);
    }
    selector = e1 >> 16;
    offset = (e2 & 0xffff0000) | (e1 & 0x0000ffff);
    if ((selector & 0xfffc) == 0) {
        raise_exception_err(env, EXCP0D_GPF, 0);
    }
    if (load_segment(env, &e1, &e2, selector) != 0) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK))) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (dpl > cpl) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err(env, EXCP0B_NOSEG, selector & 0xfffc);
    }
    if (!(e2 & DESC_C_MASK) && dpl < cpl) {
        /* to inner privilege */
        get_ss_esp_from_tss(env, &ss, &esp, dpl, 0);
        if ((ss & 0xfffc) == 0) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        if ((ss & 3) != dpl) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        if (load_segment(env, &ss_e1, &ss_e2, ss) != 0) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        ss_dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
        if (ss_dpl != dpl) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        if (!(ss_e2 & DESC_S_MASK) ||
            (ss_e2 & DESC_CS_MASK) ||
            !(ss_e2 & DESC_W_MASK)) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        if (!(ss_e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        new_stack = 1;
        sp_mask = get_sp_mask(ss_e2);
        ssp = get_seg_base(ss_e1, ss_e2);
    } else if ((e2 & DESC_C_MASK) || dpl == cpl) {
        /* to same privilege */
        if (vm86) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        new_stack = 0;
        sp_mask = get_sp_mask(env->segs[R_SS].flags);
        ssp = env->segs[R_SS].base;
        esp = env->regs[R_ESP];
        dpl = cpl;
    } else {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        new_stack = 0; /* avoid warning */
        sp_mask = 0; /* avoid warning */
        ssp = 0; /* avoid warning */
        esp = 0; /* avoid warning */
    }

    shift = type >> 3;

#if 0
    /* XXX: check that enough room is available */
    push_size = 6 + (new_stack << 2) + (has_error_code << 1);
    if (vm86) {
        push_size += 8;
    }
    push_size <<= shift;
#endif
    if (shift == 1) {
        if (new_stack) {
            if (vm86) {
                PUSHL(ssp, esp, sp_mask, env->segs[R_GS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_FS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_DS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_ES].selector);
            }
            PUSHL(ssp, esp, sp_mask, env->segs[R_SS].selector);
            PUSHL(ssp, esp, sp_mask, env->regs[R_ESP]);
        }
        PUSHL(ssp, esp, sp_mask, cpu_compute_eflags(env));
        PUSHL(ssp, esp, sp_mask, env->segs[R_CS].selector);
        PUSHL(ssp, esp, sp_mask, old_eip);
        if (has_error_code) {
            PUSHL(ssp, esp, sp_mask, error_code);
        }
    } else {
        if (new_stack) {
            if (vm86) {
                PUSHW(ssp, esp, sp_mask, env->segs[R_GS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_FS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_DS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_ES].selector);
            }
            PUSHW(ssp, esp, sp_mask, env->segs[R_SS].selector);
            PUSHW(ssp, esp, sp_mask, env->regs[R_ESP]);
        }
        PUSHW(ssp, esp, sp_mask, cpu_compute_eflags(env));
        PUSHW(ssp, esp, sp_mask, env->segs[R_CS].selector);
        PUSHW(ssp, esp, sp_mask, old_eip);
        if (has_error_code) {
            PUSHW(ssp, esp, sp_mask, error_code);
        }
    }

    /* interrupt gate clear IF mask */
    if ((type & 1) == 0) {
        env->eflags &= ~IF_MASK;
    }
    env->eflags &= ~(TF_MASK | VM_MASK | RF_MASK | NT_MASK);

    if (new_stack) {
        if (vm86) {
            cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0, 0);
            cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0, 0);
            cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0, 0);
            cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0, 0);
        }
        ss = (ss & ~3) | dpl;
        cpu_x86_load_seg_cache(env, R_SS, ss,
                               ssp, get_seg_limit(ss_e1, ss_e2), ss_e2);
    }
    SET_ESP(esp, sp_mask);

    selector = (selector & ~3) | dpl;
    cpu_x86_load_seg_cache(env, R_CS, selector,
                   get_seg_base(e1, e2),
                   get_seg_limit(e1, e2),
                   e2);
    env->eip = offset;
}

#ifdef TARGET_X86_64

#define PUSHQ_RA(sp, val, ra)                   \
    {                                           \
        sp -= 8;                                \
        cpu_stq_kernel_ra(env, sp, (val), ra);  \
    }

#define POPQ_RA(sp, val, ra)                    \
    {                                           \
        val = cpu_ldq_kernel_ra(env, sp, ra);   \
        sp += 8;                                \
    }

#define PUSHQ(sp, val) PUSHQ_RA(sp, val, 0)
#define POPQ(sp, val) POPQ_RA(sp, val, 0)

static inline target_ulong get_rsp_from_tss(CPUX86State *env, int level)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    int index;

#if 0
    printf("TR: base=" TARGET_FMT_lx " limit=%x\n",
           env->tr.base, env->tr.limit);
#endif

    if (!(env->tr.flags & DESC_P_MASK)) {
        cpu_abort(CPU(cpu), "invalid tss");
    }
    index = 8 * level + 4;
    if ((index + 7) > env->tr.limit) {
        raise_exception_err(env, EXCP0A_TSS, env->tr.selector & 0xfffc);
    }
    return cpu_ldq_kernel(env, env->tr.base + index);
}

/* 64 bit interrupt */
static void do_interrupt64(CPUX86State *env, int intno, int is_int,
                           int error_code, target_ulong next_eip, int is_hw)
{
    SegmentCache *dt;
    target_ulong ptr;
    int type, dpl, selector, cpl, ist;
    int has_error_code, new_stack;
    uint32_t e1, e2, e3, ss;
    target_ulong old_eip, esp, offset;

    has_error_code = 0;
    if (!is_int && !is_hw) {
        has_error_code = exception_has_error_code(intno);
    }
    if (is_int) {
        old_eip = next_eip;
    } else {
        old_eip = env->eip;
    }

    dt = &env->idt;
    if (intno * 16 + 15 > dt->limit) {
        raise_exception_err(env, EXCP0D_GPF, intno * 16 + 2);
    }
    ptr = dt->base + intno * 16;
    e1 = cpu_ldl_kernel(env, ptr);
    e2 = cpu_ldl_kernel(env, ptr + 4);
    e3 = cpu_ldl_kernel(env, ptr + 8);
    /* check gate type */
    type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
    switch (type) {
    case 14: /* 386 interrupt gate */
    case 15: /* 386 trap gate */
        break;
    default:
        raise_exception_err(env, EXCP0D_GPF, intno * 16 + 2);
        break;
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    /* check privilege if software int */
    if (is_int && dpl < cpl) {
        raise_exception_err(env, EXCP0D_GPF, intno * 16 + 2);
    }
    /* check valid bit */
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err(env, EXCP0B_NOSEG, intno * 16 + 2);
    }
    selector = e1 >> 16;
    offset = ((target_ulong)e3 << 32) | (e2 & 0xffff0000) | (e1 & 0x0000ffff);
    ist = e2 & 7;
    if ((selector & 0xfffc) == 0) {
        raise_exception_err(env, EXCP0D_GPF, 0);
    }

    if (load_segment(env, &e1, &e2, selector) != 0) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK))) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (dpl > cpl) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err(env, EXCP0B_NOSEG, selector & 0xfffc);
    }
    if (!(e2 & DESC_L_MASK) || (e2 & DESC_B_MASK)) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    if ((!(e2 & DESC_C_MASK) && dpl < cpl) || ist != 0) {
        /* to inner privilege */
        new_stack = 1;
        esp = get_rsp_from_tss(env, ist != 0 ? ist + 3 : dpl);
        ss = 0;
    } else if ((e2 & DESC_C_MASK) || dpl == cpl) {
        /* to same privilege */
        if (env->eflags & VM_MASK) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        new_stack = 0;
        esp = env->regs[R_ESP];
        dpl = cpl;
    } else {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        new_stack = 0; /* avoid warning */
        esp = 0; /* avoid warning */
    }
    esp &= ~0xfLL; /* align stack */

    PUSHQ(esp, env->segs[R_SS].selector);
    PUSHQ(esp, env->regs[R_ESP]);
    PUSHQ(esp, cpu_compute_eflags(env));
    PUSHQ(esp, env->segs[R_CS].selector);
    PUSHQ(esp, old_eip);
    if (has_error_code) {
        PUSHQ(esp, error_code);
    }

    /* interrupt gate clear IF mask */
    if ((type & 1) == 0) {
        env->eflags &= ~IF_MASK;
    }
    env->eflags &= ~(TF_MASK | VM_MASK | RF_MASK | NT_MASK);

    if (new_stack) {
        ss = 0 | dpl;
        cpu_x86_load_seg_cache(env, R_SS, ss, 0, 0, 0);
    }
    env->regs[R_ESP] = esp;

    selector = (selector & ~3) | dpl;
    cpu_x86_load_seg_cache(env, R_CS, selector,
                   get_seg_base(e1, e2),
                   get_seg_limit(e1, e2),
                   e2);
    env->eip = offset;
}
#endif

#ifdef TARGET_X86_64
#if defined(CONFIG_USER_ONLY)
void helper_syscall(CPUX86State *env, int next_eip_addend)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));

    cs->exception_index = EXCP_SYSCALL;
    env->exception_next_eip = env->eip + next_eip_addend;
    cpu_loop_exit(cs);
}
#else
void helper_syscall(CPUX86State *env, int next_eip_addend)
{
    int selector;

    if (!(env->efer & MSR_EFER_SCE)) {
        raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
    }
    selector = (env->star >> 32) & 0xffff;
    if (env->hflags & HF_LMA_MASK) {
        int code64;

        env->regs[R_ECX] = env->eip + next_eip_addend;
        env->regs[11] = cpu_compute_eflags(env);

        code64 = env->hflags & HF_CS64_MASK;

        env->eflags &= ~env->fmask;
        cpu_load_eflags(env, env->eflags, 0);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc,
                           0, 0xffffffff,
                               DESC_G_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
                               DESC_L_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        if (code64) {
            env->eip = env->lstar;
        } else {
            env->eip = env->cstar;
        }
    } else {
        env->regs[R_ECX] = (uint32_t)(env->eip + next_eip_addend);

        env->eflags &= ~(IF_MASK | RF_MASK | VM_MASK);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc,
                           0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        env->eip = (uint32_t)env->star;
    }
}
#endif
#endif

#ifdef TARGET_X86_64
void helper_sysret(CPUX86State *env, int dflag)
{
    int cpl, selector;

    if (!(env->efer & MSR_EFER_SCE)) {
        raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
    }
    cpl = env->hflags & HF_CPL_MASK;
    if (!(env->cr[0] & CR0_PE_MASK) || cpl != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
    selector = (env->star >> 48) & 0xffff;
    if (env->hflags & HF_LMA_MASK) {
        cpu_load_eflags(env, (uint32_t)(env->regs[11]), TF_MASK | AC_MASK
                        | ID_MASK | IF_MASK | IOPL_MASK | VM_MASK | RF_MASK |
                        NT_MASK);
        if (dflag == 2) {
            cpu_x86_load_seg_cache(env, R_CS, (selector + 16) | 3,
                                   0, 0xffffffff,
                                   DESC_G_MASK | DESC_P_MASK |
                                   DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                                   DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
                                   DESC_L_MASK);
            env->eip = env->regs[R_ECX];
        } else {
            cpu_x86_load_seg_cache(env, R_CS, selector | 3,
                                   0, 0xffffffff,
                                   DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                                   DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                                   DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
            env->eip = (uint32_t)env->regs[R_ECX];
        }
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) | 3,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
    } else {
        env->eflags |= IF_MASK;
        cpu_x86_load_seg_cache(env, R_CS, selector | 3,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        env->eip = (uint32_t)env->regs[R_ECX];
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) | 3,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
    }
}
#endif

/* real mode interrupt */
static void do_interrupt_real(CPUX86State *env, int intno, int is_int,
                              int error_code, unsigned int next_eip)
{
    SegmentCache *dt;
    target_ulong ptr, ssp;
    int selector;
    uint32_t offset, esp;
    uint32_t old_cs, old_eip;

    /* real mode (simpler!) */
    dt = &env->idt;
    if (intno * 4 + 3 > dt->limit) {
        raise_exception_err(env, EXCP0D_GPF, intno * 8 + 2);
    }
    ptr = dt->base + intno * 4;
    offset = cpu_lduw_kernel(env, ptr);
    selector = cpu_lduw_kernel(env, ptr + 2);
    esp = env->regs[R_ESP];
    ssp = env->segs[R_SS].base;
    if (is_int) {
        old_eip = next_eip;
    } else {
        old_eip = env->eip;
    }
    old_cs = env->segs[R_CS].selector;
    /* XXX: use SS segment size? */
    PUSHW(ssp, esp, 0xffff, cpu_compute_eflags(env));
    PUSHW(ssp, esp, 0xffff, old_cs);
    PUSHW(ssp, esp, 0xffff, old_eip);

    /* update processor state */
    env->regs[R_ESP] = (env->regs[R_ESP] & ~0xffff) | (esp & 0xffff);
    env->eip = offset;
    env->segs[R_CS].selector = selector;
    env->segs[R_CS].base = (selector << 4);
    env->eflags &= ~(IF_MASK | TF_MASK | AC_MASK | RF_MASK);
}

#if defined(CONFIG_USER_ONLY)
/* fake user mode interrupt. is_int is TRUE if coming from the int
 * instruction. next_eip is the env->eip value AFTER the interrupt
 * instruction. It is only relevant if is_int is TRUE or if intno
 * is EXCP_SYSCALL.
 */
static void do_interrupt_user(CPUX86State *env, int intno, int is_int,
                              int error_code, target_ulong next_eip)
{
    if (is_int) {
        SegmentCache *dt;
        target_ulong ptr;
        int dpl, cpl, shift;
        uint32_t e2;

        dt = &env->idt;
        if (env->hflags & HF_LMA_MASK) {
            shift = 4;
        } else {
            shift = 3;
        }
        ptr = dt->base + (intno << shift);
        e2 = cpu_ldl_kernel(env, ptr + 4);

        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        cpl = env->hflags & HF_CPL_MASK;
        /* check privilege if software int */
        if (dpl < cpl) {
            raise_exception_err(env, EXCP0D_GPF, (intno << shift) + 2);
        }
    }

    /* Since we emulate only user space, we cannot do more than
       exiting the emulation with the suitable exception and error
       code. So update EIP for INT 0x80 and EXCP_SYSCALL. */
    if (is_int || intno == EXCP_SYSCALL) {
        env->eip = next_eip;
    }
}

#else

static void handle_even_inj(CPUX86State *env, int intno, int is_int,
                            int error_code, int is_hw, int rm)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    uint32_t event_inj = x86_ldl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                          control.event_inj));

    if (!(event_inj & SVM_EVTINJ_VALID)) {
        int type;

        if (is_int) {
            type = SVM_EVTINJ_TYPE_SOFT;
        } else {
            type = SVM_EVTINJ_TYPE_EXEPT;
        }
        event_inj = intno | type | SVM_EVTINJ_VALID;
        if (!rm && exception_has_error_code(intno)) {
            event_inj |= SVM_EVTINJ_VALID_ERR;
            x86_stl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                             control.event_inj_err),
                     error_code);
        }
        x86_stl_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.event_inj),
                 event_inj);
    }
}
#endif

/*
 * Begin execution of an interruption. is_int is TRUE if coming from
 * the int instruction. next_eip is the env->eip value AFTER the interrupt
 * instruction. It is only relevant if is_int is TRUE.
 */
static void do_interrupt_all(X86CPU *cpu, int intno, int is_int,
                             int error_code, target_ulong next_eip, int is_hw)
{
    CPUX86State *env = &cpu->env;

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        if ((env->cr[0] & CR0_PE_MASK)) {
            static int count;

            qemu_log("%6d: v=%02x e=%04x i=%d cpl=%d IP=%04x:" TARGET_FMT_lx
                     " pc=" TARGET_FMT_lx " SP=%04x:" TARGET_FMT_lx,
                     count, intno, error_code, is_int,
                     env->hflags & HF_CPL_MASK,
                     env->segs[R_CS].selector, env->eip,
                     (int)env->segs[R_CS].base + env->eip,
                     env->segs[R_SS].selector, env->regs[R_ESP]);
            if (intno == 0x0e) {
                qemu_log(" CR2=" TARGET_FMT_lx, env->cr[2]);
            } else {
                qemu_log(" env->regs[R_EAX]=" TARGET_FMT_lx, env->regs[R_EAX]);
            }
            qemu_log("\n");
            log_cpu_state(CPU(cpu), CPU_DUMP_CCOP);
#if 0
            {
                int i;
                target_ulong ptr;

                qemu_log("       code=");
                ptr = env->segs[R_CS].base + env->eip;
                for (i = 0; i < 16; i++) {
                    qemu_log(" %02x", ldub(ptr + i));
                }
                qemu_log("\n");
            }
#endif
            count++;
        }
    }
    if (env->cr[0] & CR0_PE_MASK) {
#if !defined(CONFIG_USER_ONLY)
        if (env->hflags & HF_SVMI_MASK) {
            handle_even_inj(env, intno, is_int, error_code, is_hw, 0);
        }
#endif
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            do_interrupt64(env, intno, is_int, error_code, next_eip, is_hw);
        } else
#endif
        {
            do_interrupt_protected(env, intno, is_int, error_code, next_eip,
                                   is_hw);
        }
    } else {
#if !defined(CONFIG_USER_ONLY)
        if (env->hflags & HF_SVMI_MASK) {
            handle_even_inj(env, intno, is_int, error_code, is_hw, 1);
        }
#endif
        do_interrupt_real(env, intno, is_int, error_code, next_eip);
    }

#if !defined(CONFIG_USER_ONLY)
    if (env->hflags & HF_SVMI_MASK) {
        CPUState *cs = CPU(cpu);
        uint32_t event_inj = x86_ldl_phys(cs, env->vm_vmcb +
                                      offsetof(struct vmcb,
                                               control.event_inj));

        x86_stl_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.event_inj),
                 event_inj & ~SVM_EVTINJ_VALID);
    }
#endif
}

void x86_cpu_do_interrupt(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

#if defined(CONFIG_USER_ONLY)
    /* if user mode only, we simulate a fake exception
       which will be handled outside the cpu execution
       loop */
    do_interrupt_user(env, cs->exception_index,
                      env->exception_is_int,
                      env->error_code,
                      env->exception_next_eip);
    /* successfully delivered */
    env->old_exception = -1;
#else
    if (cs->exception_index >= EXCP_VMEXIT) {
        assert(env->old_exception == -1);
        do_vmexit(env, cs->exception_index - EXCP_VMEXIT, env->error_code);
    } else {
        do_interrupt_all(cpu, cs->exception_index,
                         env->exception_is_int,
                         env->error_code,
                         env->exception_next_eip, 0);
        /* successfully delivered */
        env->old_exception = -1;
    }
#endif
}

void do_interrupt_x86_hardirq(CPUX86State *env, int intno, int is_hw)
{
    do_interrupt_all(x86_env_get_cpu(env), intno, 0, 0, 0, is_hw);
}

bool x86_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    bool ret = false;

#if !defined(CONFIG_USER_ONLY)
    if (interrupt_request & CPU_INTERRUPT_POLL) {
        cs->interrupt_request &= ~CPU_INTERRUPT_POLL;
        apic_poll_irq(cpu->apic_state);
        /* Don't process multiple interrupt requests in a single call.
           This is required to make icount-driven execution deterministic. */
        return true;
    }
#endif
    if (interrupt_request & CPU_INTERRUPT_SIPI) {
        do_cpu_sipi(cpu);
        ret = true;
    } else if (env->hflags2 & HF2_GIF_MASK) {
        if ((interrupt_request & CPU_INTERRUPT_SMI) &&
            !(env->hflags & HF_SMM_MASK)) {
            cpu_svm_check_intercept_param(env, SVM_EXIT_SMI, 0, 0);
            cs->interrupt_request &= ~CPU_INTERRUPT_SMI;
            do_smm_enter(cpu);
            ret = true;
        } else if ((interrupt_request & CPU_INTERRUPT_NMI) &&
                   !(env->hflags2 & HF2_NMI_MASK)) {
            cs->interrupt_request &= ~CPU_INTERRUPT_NMI;
            env->hflags2 |= HF2_NMI_MASK;
            do_interrupt_x86_hardirq(env, EXCP02_NMI, 1);
            ret = true;
        } else if (interrupt_request & CPU_INTERRUPT_MCE) {
            cs->interrupt_request &= ~CPU_INTERRUPT_MCE;
            do_interrupt_x86_hardirq(env, EXCP12_MCHK, 0);
            ret = true;
        } else if ((interrupt_request & CPU_INTERRUPT_HARD) &&
                   (((env->hflags2 & HF2_VINTR_MASK) &&
                     (env->hflags2 & HF2_HIF_MASK)) ||
                    (!(env->hflags2 & HF2_VINTR_MASK) &&
                     (env->eflags & IF_MASK &&
                      !(env->hflags & HF_INHIBIT_IRQ_MASK))))) {
            int intno;
            cpu_svm_check_intercept_param(env, SVM_EXIT_INTR, 0, 0);
            cs->interrupt_request &= ~(CPU_INTERRUPT_HARD |
                                       CPU_INTERRUPT_VIRQ);
            intno = cpu_get_pic_interrupt(env);
            qemu_log_mask(CPU_LOG_TB_IN_ASM,
                          "Servicing hardware INT=0x%02x\n", intno);
            do_interrupt_x86_hardirq(env, intno, 1);
            /* ensure that no TB jump will be modified as
               the program flow was changed */
            ret = true;
#if !defined(CONFIG_USER_ONLY)
        } else if ((interrupt_request & CPU_INTERRUPT_VIRQ) &&
                   (env->eflags & IF_MASK) &&
                   !(env->hflags & HF_INHIBIT_IRQ_MASK)) {
            int intno;
            /* FIXME: this should respect TPR */
            cpu_svm_check_intercept_param(env, SVM_EXIT_VINTR, 0, 0);
            intno = x86_ldl_phys(cs, env->vm_vmcb
                             + offsetof(struct vmcb, control.int_vector));
            qemu_log_mask(CPU_LOG_TB_IN_ASM,
                          "Servicing virtual hardware INT=0x%02x\n", intno);
            do_interrupt_x86_hardirq(env, intno, 1);
            cs->interrupt_request &= ~CPU_INTERRUPT_VIRQ;
            ret = true;
#endif
        }
    }

    return ret;
}

void helper_lldt(CPUX86State *env, int selector)
{
    SegmentCache *dt;
    uint32_t e1, e2;
    int index, entry_limit;
    target_ulong ptr;

    selector &= 0xffff;
    if ((selector & 0xfffc) == 0) {
        /* XXX: NULL selector case: invalid LDT */
        env->ldt.base = 0;
        env->ldt.limit = 0;
    } else {
        if (selector & 0x4) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        dt = &env->gdt;
        index = selector & ~7;
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            entry_limit = 15;
        } else
#endif
        {
            entry_limit = 7;
        }
        if ((index + entry_limit) > dt->limit) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        ptr = dt->base + index;
        e1 = cpu_ldl_kernel_ra(env, ptr, GETPC());
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, GETPC());
        if ((e2 & DESC_S_MASK) || ((e2 >> DESC_TYPE_SHIFT) & 0xf) != 2) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, GETPC());
        }
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint32_t e3;

            e3 = cpu_ldl_kernel_ra(env, ptr + 8, GETPC());
            load_seg_cache_raw_dt(&env->ldt, e1, e2);
            env->ldt.base |= (target_ulong)e3 << 32;
        } else
#endif
        {
            load_seg_cache_raw_dt(&env->ldt, e1, e2);
        }
    }
    env->ldt.selector = selector;
}

void helper_ltr(CPUX86State *env, int selector)
{
    SegmentCache *dt;
    uint32_t e1, e2;
    int index, type, entry_limit;
    target_ulong ptr;

    selector &= 0xffff;
    if ((selector & 0xfffc) == 0) {
        /* NULL selector case: invalid TR */
        env->tr.base = 0;
        env->tr.limit = 0;
        env->tr.flags = 0;
    } else {
        if (selector & 0x4) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        dt = &env->gdt;
        index = selector & ~7;
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            entry_limit = 15;
        } else
#endif
        {
            entry_limit = 7;
        }
        if ((index + entry_limit) > dt->limit) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        ptr = dt->base + index;
        e1 = cpu_ldl_kernel_ra(env, ptr, GETPC());
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, GETPC());
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        if ((e2 & DESC_S_MASK) ||
            (type != 1 && type != 9)) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, GETPC());
        }
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint32_t e3, e4;

            e3 = cpu_ldl_kernel_ra(env, ptr + 8, GETPC());
            e4 = cpu_ldl_kernel_ra(env, ptr + 12, GETPC());
            if ((e4 >> DESC_TYPE_SHIFT) & 0xf) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }
            load_seg_cache_raw_dt(&env->tr, e1, e2);
            env->tr.base |= (target_ulong)e3 << 32;
        } else
#endif
        {
            load_seg_cache_raw_dt(&env->tr, e1, e2);
        }
        e2 |= DESC_TSS_BUSY_MASK;
        cpu_stl_kernel_ra(env, ptr + 4, e2, GETPC());
    }
    env->tr.selector = selector;
}

/* only works if protected mode and not VM86. seg_reg must be != R_CS */
void helper_load_seg(CPUX86State *env, int seg_reg, int selector)
{
    uint32_t e1, e2;
    int cpl, dpl, rpl;
    SegmentCache *dt;
    int index;
    target_ulong ptr;

    selector &= 0xffff;
    cpl = env->hflags & HF_CPL_MASK;
    if ((selector & 0xfffc) == 0) {
        /* null selector case */
        if (seg_reg == R_SS
#ifdef TARGET_X86_64
            && (!(env->hflags & HF_CS64_MASK) || cpl == 3)
#endif
            ) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
        }
        cpu_x86_load_seg_cache(env, seg_reg, selector, 0, 0, 0);
    } else {

        if (selector & 0x4) {
            dt = &env->ldt;
        } else {
            dt = &env->gdt;
        }
        index = selector & ~7;
        if ((index + 7) > dt->limit) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        ptr = dt->base + index;
        e1 = cpu_ldl_kernel_ra(env, ptr, GETPC());
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, GETPC());

        if (!(e2 & DESC_S_MASK)) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        rpl = selector & 3;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (seg_reg == R_SS) {
            /* must be writable segment */
            if ((e2 & DESC_CS_MASK) || !(e2 & DESC_W_MASK)) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }
            if (rpl != cpl || dpl != cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }
        } else {
            /* must be readable segment */
            if ((e2 & (DESC_CS_MASK | DESC_R_MASK)) == DESC_CS_MASK) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }

            if (!(e2 & DESC_CS_MASK) || !(e2 & DESC_C_MASK)) {
                /* if not conforming code, test rights */
                if (dpl < cpl || dpl < rpl) {
                    raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
                }
            }
        }

        if (!(e2 & DESC_P_MASK)) {
            if (seg_reg == R_SS) {
                raise_exception_err_ra(env, EXCP0C_STACK, selector & 0xfffc, GETPC());
            } else {
                raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, GETPC());
            }
        }

        /* set the access bit if not already set */
        if (!(e2 & DESC_A_MASK)) {
            e2 |= DESC_A_MASK;
            cpu_stl_kernel_ra(env, ptr + 4, e2, GETPC());
        }

        cpu_x86_load_seg_cache(env, seg_reg, selector,
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
#if 0
        qemu_log("load_seg: sel=0x%04x base=0x%08lx limit=0x%08lx flags=%08x\n",
                selector, (unsigned long)sc->base, sc->limit, sc->flags);
#endif
    }
}

/* protected mode jump */
void helper_ljmp_protected(CPUX86State *env, int new_cs, target_ulong new_eip,
                           target_ulong next_eip)
{
    int gate_cs, type;
    uint32_t e1, e2, cpl, dpl, rpl, limit;

    if ((new_cs & 0xfffc) == 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
    if (load_segment_ra(env, &e1, &e2, new_cs, GETPC()) != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
    }
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK)) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
        }
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_C_MASK) {
            /* conforming code segment */
            if (dpl > cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        } else {
            /* non conforming code segment */
            rpl = new_cs & 3;
            if (rpl > cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            if (dpl != cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, new_cs & 0xfffc, GETPC());
        }
        limit = get_seg_limit(e1, e2);
        if (new_eip > limit &&
            !(env->hflags & HF_LMA_MASK) && !(e2 & DESC_L_MASK)) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
        }
        cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                       get_seg_base(e1, e2), limit, e2);
        env->eip = new_eip;
    } else {
        /* jump to call or task gate */
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        rpl = new_cs & 3;
        cpl = env->hflags & HF_CPL_MASK;
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        switch (type) {
        case 1: /* 286 TSS */
        case 9: /* 386 TSS */
        case 5: /* task gate */
            if (dpl < cpl || dpl < rpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            switch_tss_ra(env, new_cs, e1, e2, SWITCH_TSS_JMP, next_eip, GETPC());
            break;
        case 4: /* 286 call gate */
        case 12: /* 386 call gate */
            if ((dpl < cpl) || (dpl < rpl)) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            if (!(e2 & DESC_P_MASK)) {
                raise_exception_err_ra(env, EXCP0B_NOSEG, new_cs & 0xfffc, GETPC());
            }
            gate_cs = e1 >> 16;
            new_eip = (e1 & 0xffff);
            if (type == 12) {
                new_eip |= (e2 & 0xffff0000);
            }
            if (load_segment_ra(env, &e1, &e2, gate_cs, GETPC()) != 0) {
                raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
            }
            dpl = (e2 >> DESC_DPL_SHIFT) & 3;
            /* must be code segment */
            if (((e2 & (DESC_S_MASK | DESC_CS_MASK)) !=
                 (DESC_S_MASK | DESC_CS_MASK))) {
                raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
            }
            if (((e2 & DESC_C_MASK) && (dpl > cpl)) ||
                (!(e2 & DESC_C_MASK) && (dpl != cpl))) {
                raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
            }
            if (!(e2 & DESC_P_MASK)) {
                raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
            }
            limit = get_seg_limit(e1, e2);
            if (new_eip > limit) {
                raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
            }
            cpu_x86_load_seg_cache(env, R_CS, (gate_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2), limit, e2);
            env->eip = new_eip;
            break;
        default:
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            break;
        }
    }
}

/* real mode call */
void helper_lcall_real(CPUX86State *env, int new_cs, target_ulong new_eip1,
                       int shift, int next_eip)
{
    int new_eip;
    uint32_t esp, esp_mask;
    target_ulong ssp;

    new_eip = new_eip1;
    esp = env->regs[R_ESP];
    esp_mask = get_sp_mask(env->segs[R_SS].flags);
    ssp = env->segs[R_SS].base;
    if (shift) {
        PUSHL_RA(ssp, esp, esp_mask, env->segs[R_CS].selector, GETPC());
        PUSHL_RA(ssp, esp, esp_mask, next_eip, GETPC());
    } else {
        PUSHW_RA(ssp, esp, esp_mask, env->segs[R_CS].selector, GETPC());
        PUSHW_RA(ssp, esp, esp_mask, next_eip, GETPC());
    }

    SET_ESP(esp, esp_mask);
    env->eip = new_eip;
    env->segs[R_CS].selector = new_cs;
    env->segs[R_CS].base = (new_cs << 4);
}

/* protected mode call */
void helper_lcall_protected(CPUX86State *env, int new_cs, target_ulong new_eip,
                            int shift, target_ulong next_eip)
{
    int new_stack, i;
    uint32_t e1, e2, cpl, dpl, rpl, selector, offset, param_count;
    uint32_t ss = 0, ss_e1 = 0, ss_e2 = 0, sp, type, ss_dpl, sp_mask;
    uint32_t val, limit, old_sp_mask;
    target_ulong ssp, old_ssp;

    LOG_PCALL("lcall %04x:%08x s=%d\n", new_cs, (uint32_t)new_eip, shift);
    LOG_PCALL_STATE(CPU(x86_env_get_cpu(env)));
    if ((new_cs & 0xfffc) == 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
    if (load_segment_ra(env, &e1, &e2, new_cs, GETPC()) != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
    }
    cpl = env->hflags & HF_CPL_MASK;
    LOG_PCALL("desc=%08x:%08x\n", e1, e2);
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK)) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
        }
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_C_MASK) {
            /* conforming code segment */
            if (dpl > cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        } else {
            /* non conforming code segment */
            rpl = new_cs & 3;
            if (rpl > cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            if (dpl != cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, new_cs & 0xfffc, GETPC());
        }

#ifdef TARGET_X86_64
        /* XXX: check 16/32 bit cases in long mode */
        if (shift == 2) {
            target_ulong rsp;

            /* 64 bit case */
            rsp = env->regs[R_ESP];
            PUSHQ_RA(rsp, env->segs[R_CS].selector, GETPC());
            PUSHQ_RA(rsp, next_eip, GETPC());
            /* from this point, not restartable */
            env->regs[R_ESP] = rsp;
            cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2),
                                   get_seg_limit(e1, e2), e2);
            env->eip = new_eip;
        } else
#endif
        {
            sp = env->regs[R_ESP];
            sp_mask = get_sp_mask(env->segs[R_SS].flags);
            ssp = env->segs[R_SS].base;
            if (shift) {
                PUSHL_RA(ssp, sp, sp_mask, env->segs[R_CS].selector, GETPC());
                PUSHL_RA(ssp, sp, sp_mask, next_eip, GETPC());
            } else {
                PUSHW_RA(ssp, sp, sp_mask, env->segs[R_CS].selector, GETPC());
                PUSHW_RA(ssp, sp, sp_mask, next_eip, GETPC());
            }

            limit = get_seg_limit(e1, e2);
            if (new_eip > limit) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            /* from this point, not restartable */
            SET_ESP(sp, sp_mask);
            cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2), limit, e2);
            env->eip = new_eip;
        }
    } else {
        /* check gate type */
        type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        rpl = new_cs & 3;
        switch (type) {
        case 1: /* available 286 TSS */
        case 9: /* available 386 TSS */
        case 5: /* task gate */
            if (dpl < cpl || dpl < rpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            switch_tss_ra(env, new_cs, e1, e2, SWITCH_TSS_CALL, next_eip, GETPC());
            return;
        case 4: /* 286 call gate */
        case 12: /* 386 call gate */
            break;
        default:
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            break;
        }
        shift = type >> 3;

        if (dpl < cpl || dpl < rpl) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
        }
        /* check valid bit */
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG,  new_cs & 0xfffc, GETPC());
        }
        selector = e1 >> 16;
        offset = (e2 & 0xffff0000) | (e1 & 0x0000ffff);
        param_count = e2 & 0x1f;
        if ((selector & 0xfffc) == 0) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
        }

        if (load_segment_ra(env, &e1, &e2, selector, GETPC()) != 0) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK))) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (dpl > cpl) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, GETPC());
        }

        if (!(e2 & DESC_C_MASK) && dpl < cpl) {
            /* to inner privilege */
            get_ss_esp_from_tss(env, &ss, &sp, dpl, GETPC());
            LOG_PCALL("new ss:esp=%04x:%08x param_count=%d env->regs[R_ESP]="
                      TARGET_FMT_lx "\n", ss, sp, param_count,
                      env->regs[R_ESP]);
            if ((ss & 0xfffc) == 0) {
                raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
            }
            if ((ss & 3) != dpl) {
                raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
            }
            if (load_segment_ra(env, &ss_e1, &ss_e2, ss, GETPC()) != 0) {
                raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
            }
            ss_dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
            if (ss_dpl != dpl) {
                raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
            }
            if (!(ss_e2 & DESC_S_MASK) ||
                (ss_e2 & DESC_CS_MASK) ||
                !(ss_e2 & DESC_W_MASK)) {
                raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
            }
            if (!(ss_e2 & DESC_P_MASK)) {
                raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
            }

            /* push_size = ((param_count * 2) + 8) << shift; */

            old_sp_mask = get_sp_mask(env->segs[R_SS].flags);
            old_ssp = env->segs[R_SS].base;

            sp_mask = get_sp_mask(ss_e2);
            ssp = get_seg_base(ss_e1, ss_e2);
            if (shift) {
                PUSHL_RA(ssp, sp, sp_mask, env->segs[R_SS].selector, GETPC());
                PUSHL_RA(ssp, sp, sp_mask, env->regs[R_ESP], GETPC());
                for (i = param_count - 1; i >= 0; i--) {
                    val = cpu_ldl_kernel_ra(env, old_ssp +
                                            ((env->regs[R_ESP] + i * 4) &
                                             old_sp_mask), GETPC());
                    PUSHL_RA(ssp, sp, sp_mask, val, GETPC());
                }
            } else {
                PUSHW_RA(ssp, sp, sp_mask, env->segs[R_SS].selector, GETPC());
                PUSHW_RA(ssp, sp, sp_mask, env->regs[R_ESP], GETPC());
                for (i = param_count - 1; i >= 0; i--) {
                    val = cpu_lduw_kernel_ra(env, old_ssp +
                                             ((env->regs[R_ESP] + i * 2) &
                                              old_sp_mask), GETPC());
                    PUSHW_RA(ssp, sp, sp_mask, val, GETPC());
                }
            }
            new_stack = 1;
        } else {
            /* to same privilege */
            sp = env->regs[R_ESP];
            sp_mask = get_sp_mask(env->segs[R_SS].flags);
            ssp = env->segs[R_SS].base;
            /* push_size = (4 << shift); */
            new_stack = 0;
        }

        if (shift) {
            PUSHL_RA(ssp, sp, sp_mask, env->segs[R_CS].selector, GETPC());
            PUSHL_RA(ssp, sp, sp_mask, next_eip, GETPC());
        } else {
            PUSHW_RA(ssp, sp, sp_mask, env->segs[R_CS].selector, GETPC());
            PUSHW_RA(ssp, sp, sp_mask, next_eip, GETPC());
        }

        /* from this point, not restartable */

        if (new_stack) {
            ss = (ss & ~3) | dpl;
            cpu_x86_load_seg_cache(env, R_SS, ss,
                                   ssp,
                                   get_seg_limit(ss_e1, ss_e2),
                                   ss_e2);
        }

        selector = (selector & ~3) | dpl;
        cpu_x86_load_seg_cache(env, R_CS, selector,
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
        SET_ESP(sp, sp_mask);
        env->eip = offset;
    }
}

/* real and vm86 mode iret */
void helper_iret_real(CPUX86State *env, int shift)
{
    uint32_t sp, new_cs, new_eip, new_eflags, sp_mask;
    target_ulong ssp;
    int eflags_mask;

    sp_mask = 0xffff; /* XXXX: use SS segment size? */
    sp = env->regs[R_ESP];
    ssp = env->segs[R_SS].base;
    if (shift == 1) {
        /* 32 bits */
        POPL_RA(ssp, sp, sp_mask, new_eip, GETPC());
        POPL_RA(ssp, sp, sp_mask, new_cs, GETPC());
        new_cs &= 0xffff;
        POPL_RA(ssp, sp, sp_mask, new_eflags, GETPC());
    } else {
        /* 16 bits */
        POPW_RA(ssp, sp, sp_mask, new_eip, GETPC());
        POPW_RA(ssp, sp, sp_mask, new_cs, GETPC());
        POPW_RA(ssp, sp, sp_mask, new_eflags, GETPC());
    }
    env->regs[R_ESP] = (env->regs[R_ESP] & ~sp_mask) | (sp & sp_mask);
    env->segs[R_CS].selector = new_cs;
    env->segs[R_CS].base = (new_cs << 4);
    env->eip = new_eip;
    if (env->eflags & VM_MASK) {
        eflags_mask = TF_MASK | AC_MASK | ID_MASK | IF_MASK | RF_MASK |
            NT_MASK;
    } else {
        eflags_mask = TF_MASK | AC_MASK | ID_MASK | IF_MASK | IOPL_MASK |
            RF_MASK | NT_MASK;
    }
    if (shift == 0) {
        eflags_mask &= 0xffff;
    }
    cpu_load_eflags(env, new_eflags, eflags_mask);
    env->hflags2 &= ~HF2_NMI_MASK;
}

static inline void validate_seg(CPUX86State *env, int seg_reg, int cpl)
{
    int dpl;
    uint32_t e2;

    /* XXX: on x86_64, we do not want to nullify FS and GS because
       they may still contain a valid base. I would be interested to
       know how a real x86_64 CPU behaves */
    if ((seg_reg == R_FS || seg_reg == R_GS) &&
        (env->segs[seg_reg].selector & 0xfffc) == 0) {
        return;
    }

    e2 = env->segs[seg_reg].flags;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (!(e2 & DESC_CS_MASK) || !(e2 & DESC_C_MASK)) {
        /* data or non conforming code segment */
        if (dpl < cpl) {
            cpu_x86_load_seg_cache(env, seg_reg, 0, 0, 0, 0);
        }
    }
}

/* protected mode iret */
static inline void helper_ret_protected(CPUX86State *env, int shift,
                                        int is_iret, int addend,
                                        uintptr_t retaddr)
{
    uint32_t new_cs, new_eflags, new_ss;
    uint32_t new_es, new_ds, new_fs, new_gs;
    uint32_t e1, e2, ss_e1, ss_e2;
    int cpl, dpl, rpl, eflags_mask, iopl;
    target_ulong ssp, sp, new_eip, new_esp, sp_mask;

#ifdef TARGET_X86_64
    if (shift == 2) {
        sp_mask = -1;
    } else
#endif
    {
        sp_mask = get_sp_mask(env->segs[R_SS].flags);
    }
    sp = env->regs[R_ESP];
    ssp = env->segs[R_SS].base;
    new_eflags = 0; /* avoid warning */
#ifdef TARGET_X86_64
    if (shift == 2) {
        POPQ_RA(sp, new_eip, retaddr);
        POPQ_RA(sp, new_cs, retaddr);
        new_cs &= 0xffff;
        if (is_iret) {
            POPQ_RA(sp, new_eflags, retaddr);
        }
    } else
#endif
    {
        if (shift == 1) {
            /* 32 bits */
            POPL_RA(ssp, sp, sp_mask, new_eip, retaddr);
            POPL_RA(ssp, sp, sp_mask, new_cs, retaddr);
            new_cs &= 0xffff;
            if (is_iret) {
                POPL_RA(ssp, sp, sp_mask, new_eflags, retaddr);
                if (new_eflags & VM_MASK) {
                    goto return_to_vm86;
                }
            }
        } else {
            /* 16 bits */
            POPW_RA(ssp, sp, sp_mask, new_eip, retaddr);
            POPW_RA(ssp, sp, sp_mask, new_cs, retaddr);
            if (is_iret) {
                POPW_RA(ssp, sp, sp_mask, new_eflags, retaddr);
            }
        }
    }
    LOG_PCALL("lret new %04x:" TARGET_FMT_lx " s=%d addend=0x%x\n",
              new_cs, new_eip, shift, addend);
    LOG_PCALL_STATE(CPU(x86_env_get_cpu(env)));
    if ((new_cs & 0xfffc) == 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
    }
    if (load_segment_ra(env, &e1, &e2, new_cs, retaddr) != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
    }
    if (!(e2 & DESC_S_MASK) ||
        !(e2 & DESC_CS_MASK)) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
    }
    cpl = env->hflags & HF_CPL_MASK;
    rpl = new_cs & 3;
    if (rpl < cpl) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (e2 & DESC_C_MASK) {
        if (dpl > rpl) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
        }
    } else {
        if (dpl != rpl) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
        }
    }
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err_ra(env, EXCP0B_NOSEG, new_cs & 0xfffc, retaddr);
    }

    sp += addend;
    if (rpl == cpl && (!(env->hflags & HF_CS64_MASK) ||
                       ((env->hflags & HF_CS64_MASK) && !is_iret))) {
        /* return to same privilege level */
        cpu_x86_load_seg_cache(env, R_CS, new_cs,
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
    } else {
        /* return to different privilege level */
#ifdef TARGET_X86_64
        if (shift == 2) {
            POPQ_RA(sp, new_esp, retaddr);
            POPQ_RA(sp, new_ss, retaddr);
            new_ss &= 0xffff;
        } else
#endif
        {
            if (shift == 1) {
                /* 32 bits */
                POPL_RA(ssp, sp, sp_mask, new_esp, retaddr);
                POPL_RA(ssp, sp, sp_mask, new_ss, retaddr);
                new_ss &= 0xffff;
            } else {
                /* 16 bits */
                POPW_RA(ssp, sp, sp_mask, new_esp, retaddr);
                POPW_RA(ssp, sp, sp_mask, new_ss, retaddr);
            }
        }
        LOG_PCALL("new ss:esp=%04x:" TARGET_FMT_lx "\n",
                  new_ss, new_esp);
        if ((new_ss & 0xfffc) == 0) {
#ifdef TARGET_X86_64
            /* NULL ss is allowed in long mode if cpl != 3 */
            /* XXX: test CS64? */
            if ((env->hflags & HF_LMA_MASK) && rpl != 3) {
                cpu_x86_load_seg_cache(env, R_SS, new_ss,
                                       0, 0xffffffff,
                                       DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                                       DESC_S_MASK | (rpl << DESC_DPL_SHIFT) |
                                       DESC_W_MASK | DESC_A_MASK);
                ss_e2 = DESC_B_MASK; /* XXX: should not be needed? */
            } else
#endif
            {
                raise_exception_err_ra(env, EXCP0D_GPF, 0, retaddr);
            }
        } else {
            if ((new_ss & 3) != rpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_ss & 0xfffc, retaddr);
            }
            if (load_segment_ra(env, &ss_e1, &ss_e2, new_ss, retaddr) != 0) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_ss & 0xfffc, retaddr);
            }
            if (!(ss_e2 & DESC_S_MASK) ||
                (ss_e2 & DESC_CS_MASK) ||
                !(ss_e2 & DESC_W_MASK)) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_ss & 0xfffc, retaddr);
            }
            dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
            if (dpl != rpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_ss & 0xfffc, retaddr);
            }
            if (!(ss_e2 & DESC_P_MASK)) {
                raise_exception_err_ra(env, EXCP0B_NOSEG, new_ss & 0xfffc, retaddr);
            }
            cpu_x86_load_seg_cache(env, R_SS, new_ss,
                                   get_seg_base(ss_e1, ss_e2),
                                   get_seg_limit(ss_e1, ss_e2),
                                   ss_e2);
        }

        cpu_x86_load_seg_cache(env, R_CS, new_cs,
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
        sp = new_esp;
#ifdef TARGET_X86_64
        if (env->hflags & HF_CS64_MASK) {
            sp_mask = -1;
        } else
#endif
        {
            sp_mask = get_sp_mask(ss_e2);
        }

        /* validate data segments */
        validate_seg(env, R_ES, rpl);
        validate_seg(env, R_DS, rpl);
        validate_seg(env, R_FS, rpl);
        validate_seg(env, R_GS, rpl);

        sp += addend;
    }
    SET_ESP(sp, sp_mask);
    env->eip = new_eip;
    if (is_iret) {
        /* NOTE: 'cpl' is the _old_ CPL */
        eflags_mask = TF_MASK | AC_MASK | ID_MASK | RF_MASK | NT_MASK;
        if (cpl == 0) {
            eflags_mask |= IOPL_MASK;
        }
        iopl = (env->eflags >> IOPL_SHIFT) & 3;
        if (cpl <= iopl) {
            eflags_mask |= IF_MASK;
        }
        if (shift == 0) {
            eflags_mask &= 0xffff;
        }
        cpu_load_eflags(env, new_eflags, eflags_mask);
    }
    return;

 return_to_vm86:
    POPL_RA(ssp, sp, sp_mask, new_esp, retaddr);
    POPL_RA(ssp, sp, sp_mask, new_ss, retaddr);
    POPL_RA(ssp, sp, sp_mask, new_es, retaddr);
    POPL_RA(ssp, sp, sp_mask, new_ds, retaddr);
    POPL_RA(ssp, sp, sp_mask, new_fs, retaddr);
    POPL_RA(ssp, sp, sp_mask, new_gs, retaddr);

    /* modify processor state */
    cpu_load_eflags(env, new_eflags, TF_MASK | AC_MASK | ID_MASK |
                    IF_MASK | IOPL_MASK | VM_MASK | NT_MASK | VIF_MASK |
                    VIP_MASK);
    load_seg_vm(env, R_CS, new_cs & 0xffff);
    load_seg_vm(env, R_SS, new_ss & 0xffff);
    load_seg_vm(env, R_ES, new_es & 0xffff);
    load_seg_vm(env, R_DS, new_ds & 0xffff);
    load_seg_vm(env, R_FS, new_fs & 0xffff);
    load_seg_vm(env, R_GS, new_gs & 0xffff);

    env->eip = new_eip & 0xffff;
    env->regs[R_ESP] = new_esp;
}

void helper_iret_protected(CPUX86State *env, int shift, int next_eip)
{
    int tss_selector, type;
    uint32_t e1, e2;

    /* specific case for TSS */
    if (env->eflags & NT_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
        }
#endif
        tss_selector = cpu_lduw_kernel_ra(env, env->tr.base + 0, GETPC());
        if (tss_selector & 4) {
            raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, GETPC());
        }
        if (load_segment_ra(env, &e1, &e2, tss_selector, GETPC()) != 0) {
            raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, GETPC());
        }
        type = (e2 >> DESC_TYPE_SHIFT) & 0x17;
        /* NOTE: we check both segment and busy TSS */
        if (type != 3) {
            raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, GETPC());
        }
        switch_tss_ra(env, tss_selector, e1, e2, SWITCH_TSS_IRET, next_eip, GETPC());
    } else {
        helper_ret_protected(env, shift, 1, 0, GETPC());
    }
    env->hflags2 &= ~HF2_NMI_MASK;
}

void helper_lret_protected(CPUX86State *env, int shift, int addend)
{
    helper_ret_protected(env, shift, 0, addend, GETPC());
}

void helper_sysenter(CPUX86State *env)
{
    if (env->sysenter_cs == 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
    env->eflags &= ~(VM_MASK | IF_MASK | RF_MASK);

#ifdef TARGET_X86_64
    if (env->hflags & HF_LMA_MASK) {
        cpu_x86_load_seg_cache(env, R_CS, env->sysenter_cs & 0xfffc,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
                               DESC_L_MASK);
    } else
#endif
    {
        cpu_x86_load_seg_cache(env, R_CS, env->sysenter_cs & 0xfffc,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
    }
    cpu_x86_load_seg_cache(env, R_SS, (env->sysenter_cs + 8) & 0xfffc,
                           0, 0xffffffff,
                           DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                           DESC_S_MASK |
                           DESC_W_MASK | DESC_A_MASK);
    env->regs[R_ESP] = env->sysenter_esp;
    env->eip = env->sysenter_eip;
}

void helper_sysexit(CPUX86State *env, int dflag)
{
    int cpl;

    cpl = env->hflags & HF_CPL_MASK;
    if (env->sysenter_cs == 0 || cpl != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
#ifdef TARGET_X86_64
    if (dflag == 2) {
        cpu_x86_load_seg_cache(env, R_CS, ((env->sysenter_cs + 32) & 0xfffc) |
                               3, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
                               DESC_L_MASK);
        cpu_x86_load_seg_cache(env, R_SS, ((env->sysenter_cs + 40) & 0xfffc) |
                               3, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
    } else
#endif
    {
        cpu_x86_load_seg_cache(env, R_CS, ((env->sysenter_cs + 16) & 0xfffc) |
                               3, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        cpu_x86_load_seg_cache(env, R_SS, ((env->sysenter_cs + 24) & 0xfffc) |
                               3, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
    }
    env->regs[R_ESP] = env->regs[R_ECX];
    env->eip = env->regs[R_EDX];
}

target_ulong helper_lsl(CPUX86State *env, target_ulong selector1)
{
    unsigned int limit;
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl, type;

    selector = selector1 & 0xffff;
    eflags = cpu_cc_compute_all(env, CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment_ra(env, &e1, &e2, selector, GETPC()) != 0) {
        goto fail;
    }
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if ((e2 & DESC_CS_MASK) && (e2 & DESC_C_MASK)) {
            /* conforming */
        } else {
            if (dpl < cpl || dpl < rpl) {
                goto fail;
            }
        }
    } else {
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        switch (type) {
        case 1:
        case 2:
        case 3:
        case 9:
        case 11:
            break;
        default:
            goto fail;
        }
        if (dpl < cpl || dpl < rpl) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return 0;
        }
    }
    limit = get_seg_limit(e1, e2);
    CC_SRC = eflags | CC_Z;
    return limit;
}

target_ulong helper_lar(CPUX86State *env, target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl, type;

    selector = selector1 & 0xffff;
    eflags = cpu_cc_compute_all(env, CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment_ra(env, &e1, &e2, selector, GETPC()) != 0) {
        goto fail;
    }
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if ((e2 & DESC_CS_MASK) && (e2 & DESC_C_MASK)) {
            /* conforming */
        } else {
            if (dpl < cpl || dpl < rpl) {
                goto fail;
            }
        }
    } else {
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        switch (type) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 9:
        case 11:
        case 12:
            break;
        default:
            goto fail;
        }
        if (dpl < cpl || dpl < rpl) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return 0;
        }
    }
    CC_SRC = eflags | CC_Z;
    return e2 & 0x00f0ff00;
}

void helper_verr(CPUX86State *env, target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl;

    selector = selector1 & 0xffff;
    eflags = cpu_cc_compute_all(env, CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment_ra(env, &e1, &e2, selector, GETPC()) != 0) {
        goto fail;
    }
    if (!(e2 & DESC_S_MASK)) {
        goto fail;
    }
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_CS_MASK) {
        if (!(e2 & DESC_R_MASK)) {
            goto fail;
        }
        if (!(e2 & DESC_C_MASK)) {
            if (dpl < cpl || dpl < rpl) {
                goto fail;
            }
        }
    } else {
        if (dpl < cpl || dpl < rpl) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return;
        }
    }
    CC_SRC = eflags | CC_Z;
}

void helper_verw(CPUX86State *env, target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl;

    selector = selector1 & 0xffff;
    eflags = cpu_cc_compute_all(env, CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment_ra(env, &e1, &e2, selector, GETPC()) != 0) {
        goto fail;
    }
    if (!(e2 & DESC_S_MASK)) {
        goto fail;
    }
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_CS_MASK) {
        goto fail;
    } else {
        if (dpl < cpl || dpl < rpl) {
            goto fail;
        }
        if (!(e2 & DESC_W_MASK)) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return;
        }
    }
    CC_SRC = eflags | CC_Z;
}

#if defined(CONFIG_USER_ONLY)
void cpu_x86_load_seg(CPUX86State *env, int seg_reg, int selector)
{
    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK)) {
        int dpl = (env->eflags & VM_MASK) ? 3 : 0;
        selector &= 0xffff;
        cpu_x86_load_seg_cache(env, seg_reg, selector,
                               (selector << 4), 0xffff,
                               DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                               DESC_A_MASK | (dpl << DESC_DPL_SHIFT));
    } else {
        helper_load_seg(env, seg_reg, selector);
    }
}
#endif

/* check if Port I/O is allowed in TSS */
static inline void check_io(CPUX86State *env, int addr, int size,
                            uintptr_t retaddr)
{
    int io_offset, val, mask;

    /* TSS must be a valid 32 bit one */
    if (!(env->tr.flags & DESC_P_MASK) ||
        ((env->tr.flags >> DESC_TYPE_SHIFT) & 0xf) != 9 ||
        env->tr.limit < 103) {
        goto fail;
    }
    io_offset = cpu_lduw_kernel_ra(env, env->tr.base + 0x66, retaddr);
    io_offset += (addr >> 3);
    /* Note: the check needs two bytes */
    if ((io_offset + 1) > env->tr.limit) {
        goto fail;
    }
    val = cpu_lduw_kernel_ra(env, env->tr.base + io_offset, retaddr);
    val >>= (addr & 7);
    mask = (1 << size) - 1;
    /* all bits must be zero to allow the I/O */
    if ((val & mask) != 0) {
    fail:
        raise_exception_err_ra(env, EXCP0D_GPF, 0, retaddr);
    }
}

void helper_check_iob(CPUX86State *env, uint32_t t0)
{
    check_io(env, t0, 1, GETPC());
}

void helper_check_iow(CPUX86State *env, uint32_t t0)
{
    check_io(env, t0, 2, GETPC());
}

void helper_check_iol(CPUX86State *env, uint32_t t0)
{
    check_io(env, t0, 4, GETPC());
}
