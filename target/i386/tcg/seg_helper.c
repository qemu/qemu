/*
 *  x86 segmentation related helpers:
 *  TSS, interrupts, system calls, jumps and call/task gates, descriptors
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
#include "cpu.h"
#include "qemu/log.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "exec/log.h"
#include "helper-tcg.h"
#include "seg_helper.h"
#include "access.h"
#include "tcg-cpu.h"
#include "qemu/plugin.h"

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

/* XXX: use mmu_index to have proper DPL support */
typedef struct StackAccess
{
    CPUX86State *env;
    uintptr_t ra;
    target_ulong ss_base;
    target_ulong sp;
    target_ulong sp_mask;
    int mmu_index;
} StackAccess;

static void pushw(StackAccess *sa, uint16_t val)
{
    sa->sp -= 2;
    cpu_stw_mmuidx_ra(sa->env, sa->ss_base + (sa->sp & sa->sp_mask),
                      val, sa->mmu_index, sa->ra);
}

static void pushl(StackAccess *sa, uint32_t val)
{
    sa->sp -= 4;
    cpu_stl_mmuidx_ra(sa->env, sa->ss_base + (sa->sp & sa->sp_mask),
                      val, sa->mmu_index, sa->ra);
}

static uint16_t popw(StackAccess *sa)
{
    uint16_t ret = cpu_lduw_mmuidx_ra(sa->env,
                                      sa->ss_base + (sa->sp & sa->sp_mask),
                                      sa->mmu_index, sa->ra);
    sa->sp += 2;
    return ret;
}

static uint32_t popl(StackAccess *sa)
{
    uint32_t ret = cpu_ldl_mmuidx_ra(sa->env,
                                     sa->ss_base + (sa->sp & sa->sp_mask),
                                     sa->mmu_index, sa->ra);
    sa->sp += 4;
    return ret;
}

int get_pg_mode(CPUX86State *env)
{
    int pg_mode = PG_MODE_PG;
    if (!(env->cr[0] & CR0_PG_MASK)) {
        return 0;
    }
    if (env->cr[0] & CR0_WP_MASK) {
        pg_mode |= PG_MODE_WP;
    }
    if (env->cr[4] & CR4_PAE_MASK) {
        pg_mode |= PG_MODE_PAE;
        if (env->efer & MSR_EFER_NXE) {
            pg_mode |= PG_MODE_NXE;
        }
    }
    if (env->cr[4] & CR4_PSE_MASK) {
        pg_mode |= PG_MODE_PSE;
    }
    if (env->cr[4] & CR4_SMEP_MASK) {
        pg_mode |= PG_MODE_SMEP;
    }
    if (env->hflags & HF_LMA_MASK) {
        pg_mode |= PG_MODE_LMA;
        if (env->cr[4] & CR4_PKE_MASK) {
            pg_mode |= PG_MODE_PKE;
        }
        if (env->cr[4] & CR4_PKS_MASK) {
            pg_mode |= PG_MODE_PKS;
        }
        if (env->cr[4] & CR4_LA57_MASK) {
            pg_mode |= PG_MODE_LA57;
        }
    }
    return pg_mode;
}

static int x86_mmu_index_kernel_pl(CPUX86State *env, unsigned pl)
{
    int mmu_index_32 = (env->hflags & HF_LMA_MASK) ? 0 : 1;
    int mmu_index_base =
        !(env->hflags & HF_SMAP_MASK) ? MMU_KNOSMAP64_IDX :
        (pl < 3 && (env->eflags & AC_MASK)
         ? MMU_KNOSMAP64_IDX : MMU_KSMAP64_IDX);

    return mmu_index_base + mmu_index_32;
}

int cpu_mmu_index_kernel(CPUX86State *env)
{
    return x86_mmu_index_kernel_pl(env, env->hflags & HF_CPL_MASK);
}

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
    X86CPU *cpu = env_archcpu(env);
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

static void tss_load_seg(CPUX86State *env, X86Seg seg_reg, int selector,
                         int cpl, uintptr_t retaddr)
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

static void tss_set_busy(CPUX86State *env, int tss_selector, bool value,
                         uintptr_t retaddr)
{
    target_ulong ptr = env->gdt.base + (tss_selector & ~7);
    uint32_t e2 = cpu_ldl_kernel_ra(env, ptr + 4, retaddr);

    if (value) {
        e2 |= DESC_TSS_BUSY_MASK;
    } else {
        e2 &= ~DESC_TSS_BUSY_MASK;
    }

    cpu_stl_kernel_ra(env, ptr + 4, e2, retaddr);
}

#define SWITCH_TSS_JMP  0
#define SWITCH_TSS_IRET 1
#define SWITCH_TSS_CALL 2

static void switch_tss_ra(CPUX86State *env, int tss_selector,
                          uint32_t e1, uint32_t e2, int source,
                          uint32_t next_eip, bool has_error_code,
                          uint32_t error_code, uintptr_t retaddr)
{
    int tss_limit, tss_limit_max, type, old_tss_limit_max, old_type, i;
    target_ulong tss_base;
    uint32_t new_regs[8], new_segs[6];
    uint32_t new_eflags, new_eip, new_cr3, new_ldt, new_trap;
    uint32_t old_eflags, eflags_mask;
    SegmentCache *dt;
    int mmu_index, index;
    target_ulong ptr;
    X86Access old, new;

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

    /* new TSS must be busy iff the source is an IRET instruction  */
    if (!!(e2 & DESC_TSS_BUSY_MASK) != (source == SWITCH_TSS_IRET)) {
        raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, retaddr);
    }

    /* X86Access avoids memory exceptions during the task switch */
    mmu_index = cpu_mmu_index_kernel(env);
    access_prepare_mmu(&old, env, env->tr.base, old_tss_limit_max + 1,
                       MMU_DATA_STORE, mmu_index, retaddr);

    if (source == SWITCH_TSS_CALL) {
        /* Probe for future write of parent task */
        probe_access(env, tss_base, 2, MMU_DATA_STORE,
                     mmu_index, retaddr);
    }
    /* While true tss_limit may be larger, we don't access the iopb here. */
    access_prepare_mmu(&new, env, tss_base, tss_limit_max + 1,
                       MMU_DATA_LOAD, mmu_index, retaddr);

    /* save the current state in the old TSS */
    old_eflags = cpu_compute_eflags(env);
    if (old_type & 8) {
        /* 32 bit */
        access_stl(&old, env->tr.base + 0x20, next_eip);
        access_stl(&old, env->tr.base + 0x24, old_eflags);
        access_stl(&old, env->tr.base + (0x28 + 0 * 4), env->regs[R_EAX]);
        access_stl(&old, env->tr.base + (0x28 + 1 * 4), env->regs[R_ECX]);
        access_stl(&old, env->tr.base + (0x28 + 2 * 4), env->regs[R_EDX]);
        access_stl(&old, env->tr.base + (0x28 + 3 * 4), env->regs[R_EBX]);
        access_stl(&old, env->tr.base + (0x28 + 4 * 4), env->regs[R_ESP]);
        access_stl(&old, env->tr.base + (0x28 + 5 * 4), env->regs[R_EBP]);
        access_stl(&old, env->tr.base + (0x28 + 6 * 4), env->regs[R_ESI]);
        access_stl(&old, env->tr.base + (0x28 + 7 * 4), env->regs[R_EDI]);
        for (i = 0; i < 6; i++) {
            access_stw(&old, env->tr.base + (0x48 + i * 4),
                       env->segs[i].selector);
        }
    } else {
        /* 16 bit */
        access_stw(&old, env->tr.base + 0x0e, next_eip);
        access_stw(&old, env->tr.base + 0x10, old_eflags);
        access_stw(&old, env->tr.base + (0x12 + 0 * 2), env->regs[R_EAX]);
        access_stw(&old, env->tr.base + (0x12 + 1 * 2), env->regs[R_ECX]);
        access_stw(&old, env->tr.base + (0x12 + 2 * 2), env->regs[R_EDX]);
        access_stw(&old, env->tr.base + (0x12 + 3 * 2), env->regs[R_EBX]);
        access_stw(&old, env->tr.base + (0x12 + 4 * 2), env->regs[R_ESP]);
        access_stw(&old, env->tr.base + (0x12 + 5 * 2), env->regs[R_EBP]);
        access_stw(&old, env->tr.base + (0x12 + 6 * 2), env->regs[R_ESI]);
        access_stw(&old, env->tr.base + (0x12 + 7 * 2), env->regs[R_EDI]);
        for (i = 0; i < 4; i++) {
            access_stw(&old, env->tr.base + (0x22 + i * 2),
                       env->segs[i].selector);
        }
    }

    /* read all the registers from the new TSS */
    if (type & 8) {
        /* 32 bit */
        new_cr3 = access_ldl(&new, tss_base + 0x1c);
        new_eip = access_ldl(&new, tss_base + 0x20);
        new_eflags = access_ldl(&new, tss_base + 0x24);
        for (i = 0; i < 8; i++) {
            new_regs[i] = access_ldl(&new, tss_base + (0x28 + i * 4));
        }
        for (i = 0; i < 6; i++) {
            new_segs[i] = access_ldw(&new, tss_base + (0x48 + i * 4));
        }
        new_ldt = access_ldw(&new, tss_base + 0x60);
        new_trap = access_ldw(&new, tss_base + 0x64) & 1;
    } else {
        /* 16 bit */
        new_cr3 = 0;
        new_eip = access_ldw(&new, tss_base + 0x0e);
        new_eflags = access_ldw(&new, tss_base + 0x10);
        for (i = 0; i < 8; i++) {
            new_regs[i] = access_ldw(&new, tss_base + (0x12 + i * 2));
        }
        for (i = 0; i < 4; i++) {
            new_segs[i] = access_ldw(&new, tss_base + (0x22 + i * 2));
        }
        new_ldt = access_ldw(&new, tss_base + 0x2a);
        new_segs[R_FS] = 0;
        new_segs[R_GS] = 0;
        new_trap = 0;
    }

    /* clear busy bit (it is restartable) */
    if (source == SWITCH_TSS_JMP || source == SWITCH_TSS_IRET) {
        tss_set_busy(env, env->tr.selector, 0, retaddr);
    }

    if (source == SWITCH_TSS_IRET) {
        old_eflags &= ~NT_MASK;
        if (old_type & 8) {
            access_stl(&old, env->tr.base + 0x24, old_eflags);
        } else {
            access_stw(&old, env->tr.base + 0x10, old_eflags);
	}
    }

    if (source == SWITCH_TSS_CALL) {
        /*
         * Thanks to the probe_access above, we know the first two
         * bytes addressed by &new are writable too.
         */
        access_stw(&new, tss_base, env->tr.selector);
        new_eflags |= NT_MASK;
    }

    /* set busy bit */
    if (source == SWITCH_TSS_JMP || source == SWITCH_TSS_CALL) {
        tss_set_busy(env, tss_selector, 1, retaddr);
    }

    /* set the new CPU state */

    /* now if an exception occurs, it will occur in the next task context */

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
    if (type & 8) {
        cpu_load_eflags(env, new_eflags, eflags_mask);
        for (i = 0; i < 8; i++) {
            env->regs[i] = new_regs[i];
        }
    } else {
        cpu_load_eflags(env, new_eflags, eflags_mask & 0xffff);
        for (i = 0; i < 8; i++) {
            env->regs[i] = (env->regs[i] & 0xffff0000) | new_regs[i];
        }
    }
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

    if (has_error_code) {
        int cpl = env->hflags & HF_CPL_MASK;
        StackAccess sa;

        /* push the error code */
        sa.env = env;
        sa.ra = retaddr;
        sa.mmu_index = x86_mmu_index_pl(env, cpl);
        sa.sp = env->regs[R_ESP];
        if (env->segs[R_SS].flags & DESC_B_MASK) {
            sa.sp_mask = 0xffffffff;
        } else {
            sa.sp_mask = 0xffff;
        }
        sa.ss_base = env->segs[R_SS].base;
        if (type & 8) {
            pushl(&sa, error_code);
        } else {
            pushw(&sa, error_code);
        }
        SET_ESP(sa.sp, sa.sp_mask);
    }

    if (new_trap) {
        env->dr[6] |= DR6_BT;
        raise_exception_ra(env, EXCP01_DB, retaddr);
    }
}

static void switch_tss(CPUX86State *env, int tss_selector,
                       uint32_t e1, uint32_t e2, int source,
                       uint32_t next_eip, bool has_error_code,
                       int error_code)
{
    switch_tss_ra(env, tss_selector, e1, e2, source, next_eip,
                  has_error_code, error_code, 0);
}

static inline unsigned int get_sp_mask(unsigned int e2)
{
#ifdef TARGET_X86_64
    if (e2 & DESC_L_MASK) {
        return 0;
    } else
#endif
    if (e2 & DESC_B_MASK) {
        return 0xffffffff;
    } else {
        return 0xffff;
    }
}

static int exception_is_fault(int intno)
{
    switch (intno) {
        /*
         * #DB can be both fault- and trap-like, but it never sets RF=1
         * in the RFLAGS value pushed on the stack.
         */
    case EXCP01_DB:
    case EXCP03_INT3:
    case EXCP04_INTO:
    case EXCP08_DBLE:
    case EXCP12_MCHK:
        return 0;
    }
    /* Everything else including reserved exception is a fault.  */
    return 1;
}

int exception_has_error_code(int intno)
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

/* protected mode interrupt */
static void do_interrupt_protected(CPUX86State *env, int intno, int is_int,
                                   int error_code, unsigned int next_eip,
                                   int is_hw)
{
    SegmentCache *dt;
    target_ulong ptr;
    int type, dpl, selector, ss_dpl, cpl;
    int has_error_code, new_stack, shift;
    uint32_t e1, e2, offset, ss = 0, ss_e1 = 0, ss_e2 = 0;
    uint32_t old_eip, eflags;
    int vm86 = env->eflags & VM_MASK;
    StackAccess sa;
    bool set_rf;

    has_error_code = 0;
    if (!is_int && !is_hw) {
        has_error_code = exception_has_error_code(intno);
    }
    if (is_int) {
        old_eip = next_eip;
        set_rf = false;
    } else {
        old_eip = env->eip;
        set_rf = exception_is_fault(intno);
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

    sa.env = env;
    sa.ra = 0;

    if (type == 5) {
        /* task gate */
        /* must do that check here to return the correct error code */
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0B_NOSEG, intno * 8 + 2);
        }
        switch_tss(env, intno * 8, e1, e2, SWITCH_TSS_CALL, old_eip,
                   has_error_code, error_code);
        return;
    }

    /* Otherwise, trap or interrupt gate */

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
    if (e2 & DESC_C_MASK) {
        dpl = cpl;
    }
    sa.mmu_index = x86_mmu_index_pl(env, dpl);
    if (dpl < cpl) {
        /* to inner privilege */
        uint32_t esp;
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
        sa.sp = esp;
        sa.sp_mask = get_sp_mask(ss_e2);
        sa.ss_base = get_seg_base(ss_e1, ss_e2);
    } else  {
        /* to same privilege */
        if (vm86) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        new_stack = 0;
        sa.sp = env->regs[R_ESP];
        sa.sp_mask = get_sp_mask(env->segs[R_SS].flags);
        sa.ss_base = env->segs[R_SS].base;
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
    eflags = cpu_compute_eflags(env);
    /*
     * AMD states that code breakpoint #DBs clear RF=0, Intel leaves it
     * as is.  AMD behavior could be implemented in check_hw_breakpoints().
     */
    if (set_rf) {
        eflags |= RF_MASK;
    }

    if (shift == 1) {
        if (new_stack) {
            if (vm86) {
                pushl(&sa, env->segs[R_GS].selector);
                pushl(&sa, env->segs[R_FS].selector);
                pushl(&sa, env->segs[R_DS].selector);
                pushl(&sa, env->segs[R_ES].selector);
            }
            pushl(&sa, env->segs[R_SS].selector);
            pushl(&sa, env->regs[R_ESP]);
        }
        pushl(&sa, eflags);
        pushl(&sa, env->segs[R_CS].selector);
        pushl(&sa, old_eip);
        if (has_error_code) {
            pushl(&sa, error_code);
        }
    } else {
        if (new_stack) {
            if (vm86) {
                pushw(&sa, env->segs[R_GS].selector);
                pushw(&sa, env->segs[R_FS].selector);
                pushw(&sa, env->segs[R_DS].selector);
                pushw(&sa, env->segs[R_ES].selector);
            }
            pushw(&sa, env->segs[R_SS].selector);
            pushw(&sa, env->regs[R_ESP]);
        }
        pushw(&sa, eflags);
        pushw(&sa, env->segs[R_CS].selector);
        pushw(&sa, old_eip);
        if (has_error_code) {
            pushw(&sa, error_code);
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
        cpu_x86_load_seg_cache(env, R_SS, ss, sa.ss_base,
                               get_seg_limit(ss_e1, ss_e2), ss_e2);
    }
    SET_ESP(sa.sp, sa.sp_mask);

    selector = (selector & ~3) | dpl;
    cpu_x86_load_seg_cache(env, R_CS, selector,
                   get_seg_base(e1, e2),
                   get_seg_limit(e1, e2),
                   e2);
    env->eip = offset;
}

#ifdef TARGET_X86_64

static void pushq(StackAccess *sa, uint64_t val)
{
    sa->sp -= 8;
    cpu_stq_mmuidx_ra(sa->env, sa->sp, val, sa->mmu_index, sa->ra);
}

static uint64_t popq(StackAccess *sa)
{
    uint64_t ret = cpu_ldq_mmuidx_ra(sa->env, sa->sp, sa->mmu_index, sa->ra);
    sa->sp += 8;
    return ret;
}

static inline target_ulong get_rsp_from_tss(CPUX86State *env, int level)
{
    X86CPU *cpu = env_archcpu(env);
    int index, pg_mode;
    target_ulong rsp;
    int32_t sext;

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

    rsp = cpu_ldq_kernel(env, env->tr.base + index);

    /* test virtual address sign extension */
    pg_mode = get_pg_mode(env);
    sext = (int64_t)rsp >> (pg_mode & PG_MODE_LA57 ? 56 : 47);
    if (sext != 0 && sext != -1) {
        raise_exception_err(env, EXCP0C_STACK, 0);
    }

    return rsp;
}

/* 64 bit interrupt */
static void do_interrupt64(CPUX86State *env, int intno, int is_int,
                           int error_code, target_ulong next_eip, int is_hw)
{
    SegmentCache *dt;
    target_ulong ptr;
    int type, dpl, selector, cpl, ist;
    int has_error_code, new_stack;
    uint32_t e1, e2, e3, eflags;
    target_ulong old_eip, offset;
    bool set_rf;
    StackAccess sa;

    has_error_code = 0;
    if (!is_int && !is_hw) {
        has_error_code = exception_has_error_code(intno);
    }
    if (is_int) {
        old_eip = next_eip;
        set_rf = false;
    } else {
        old_eip = env->eip;
        set_rf = exception_is_fault(intno);
    }

    dt = &env->idt;
    if (intno * 16 + 15 > dt->limit) {
        raise_exception_err(env, EXCP0D_GPF, intno * 8 + 2);
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
    if (e2 & DESC_C_MASK) {
        dpl = cpl;
    }

    sa.env = env;
    sa.ra = 0;
    sa.mmu_index = x86_mmu_index_pl(env, dpl);
    sa.sp_mask = -1;
    sa.ss_base = 0;
    if (dpl < cpl || ist != 0) {
        /* to inner privilege */
        new_stack = 1;
        sa.sp = get_rsp_from_tss(env, ist != 0 ? ist + 3 : dpl);
    } else {
        /* to same privilege */
        if (env->eflags & VM_MASK) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        new_stack = 0;
        sa.sp = env->regs[R_ESP];
    }
    sa.sp &= ~0xfLL; /* align stack */

    /* See do_interrupt_protected.  */
    eflags = cpu_compute_eflags(env);
    if (set_rf) {
        eflags |= RF_MASK;
    }

    pushq(&sa, env->segs[R_SS].selector);
    pushq(&sa, env->regs[R_ESP]);
    pushq(&sa, eflags);
    pushq(&sa, env->segs[R_CS].selector);
    pushq(&sa, old_eip);
    if (has_error_code) {
        pushq(&sa, error_code);
    }

    /* interrupt gate clear IF mask */
    if ((type & 1) == 0) {
        env->eflags &= ~IF_MASK;
    }
    env->eflags &= ~(TF_MASK | VM_MASK | RF_MASK | NT_MASK);

    if (new_stack) {
        uint32_t ss = 0 | dpl; /* SS = NULL selector with RPL = new CPL */
        cpu_x86_load_seg_cache(env, R_SS, ss, 0, 0, dpl << DESC_DPL_SHIFT);
    }
    env->regs[R_ESP] = sa.sp;

    selector = (selector & ~3) | dpl;
    cpu_x86_load_seg_cache(env, R_CS, selector,
                   get_seg_base(e1, e2),
                   get_seg_limit(e1, e2),
                   e2);
    env->eip = offset;
}
#endif /* TARGET_X86_64 */

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
#ifdef TARGET_X86_64
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
    } else
#endif
    {
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

/* real mode interrupt */
static void do_interrupt_real(CPUX86State *env, int intno, int is_int,
                              int error_code, unsigned int next_eip)
{
    SegmentCache *dt;
    target_ulong ptr;
    int selector;
    uint32_t offset;
    uint32_t old_cs, old_eip;
    StackAccess sa;

    /* real mode (simpler!) */
    dt = &env->idt;
    if (intno * 4 + 3 > dt->limit) {
        raise_exception_err(env, EXCP0D_GPF, intno * 8 + 2);
    }
    ptr = dt->base + intno * 4;
    offset = cpu_lduw_kernel(env, ptr);
    selector = cpu_lduw_kernel(env, ptr + 2);

    sa.env = env;
    sa.ra = 0;
    sa.sp = env->regs[R_ESP];
    sa.sp_mask = get_sp_mask(env->segs[R_SS].flags);
    sa.ss_base = env->segs[R_SS].base;
    sa.mmu_index = x86_mmu_index_pl(env, 0);

    if (is_int) {
        old_eip = next_eip;
    } else {
        old_eip = env->eip;
    }
    old_cs = env->segs[R_CS].selector;
    /* XXX: use SS segment size? */
    pushw(&sa, cpu_compute_eflags(env));
    pushw(&sa, old_cs);
    pushw(&sa, old_eip);

    /* update processor state */
    SET_ESP(sa.sp, sa.sp_mask);
    env->eip = offset;
    env->segs[R_CS].selector = selector;
    env->segs[R_CS].base = (selector << 4);
    env->eflags &= ~(IF_MASK | TF_MASK | AC_MASK | RF_MASK);
}

/*
 * Begin execution of an interruption. is_int is TRUE if coming from
 * the int instruction. next_eip is the env->eip value AFTER the interrupt
 * instruction. It is only relevant if is_int is TRUE.
 */
void do_interrupt_all(X86CPU *cpu, int intno, int is_int,
                      int error_code, target_ulong next_eip, int is_hw)
{
    CPUX86State *env = &cpu->env;
    uint64_t last_pc = env->eip + env->segs[R_CS].base;

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
        if (env->hflags & HF_GUEST_MASK) {
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
        if (env->hflags & HF_GUEST_MASK) {
            handle_even_inj(env, intno, is_int, error_code, is_hw, 1);
        }
#endif
        do_interrupt_real(env, intno, is_int, error_code, next_eip);
    }

#if !defined(CONFIG_USER_ONLY)
    if (env->hflags & HF_GUEST_MASK) {
        CPUState *cs = CPU(cpu);
        uint32_t event_inj = x86_ldl_phys(cs, env->vm_vmcb +
                                      offsetof(struct vmcb,
                                               control.event_inj));

        x86_stl_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.event_inj),
                 event_inj & ~SVM_EVTINJ_VALID);
    }
#endif

    qemu_plugin_vcpu_interrupt_cb(CPU(cpu), last_pc);
}

void do_interrupt_x86_hardirq(CPUX86State *env, int intno, int is_hw)
{
    do_interrupt_all(env_archcpu(env), intno, 0, 0, 0, is_hw);
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
            (!(env->hflags & HF_LMA_MASK) || !(e2 & DESC_L_MASK))) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
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

#ifdef TARGET_X86_64
        if (env->efer & MSR_EFER_LMA) {
            if (type != 12) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        }
#endif
        switch (type) {
        case 1: /* 286 TSS */
        case 9: /* 386 TSS */
        case 5: /* task gate */
            if (dpl < cpl || dpl < rpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            switch_tss_ra(env, new_cs, e1, e2, SWITCH_TSS_JMP, next_eip,
                          false, 0, GETPC());
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

#ifdef TARGET_X86_64
            if (env->efer & MSR_EFER_LMA) {
                /* load the upper 8 bytes of the 64-bit call gate */
                if (load_segment_ra(env, &e1, &e2, new_cs + 8, GETPC())) {
                    raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc,
                                           GETPC());
                }
                type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
                if (type != 0) {
                    raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc,
                                           GETPC());
                }
                new_eip |= ((target_ulong)e1) << 32;
            }
#endif

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
#ifdef TARGET_X86_64
            if (env->efer & MSR_EFER_LMA) {
                if (!(e2 & DESC_L_MASK)) {
                    raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
                }
                if (e2 & DESC_B_MASK) {
                    raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
                }
            }
#endif
            if (!(e2 & DESC_P_MASK)) {
                raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
            }
            limit = get_seg_limit(e1, e2);
            if (new_eip > limit &&
                (!(env->hflags & HF_LMA_MASK) || !(e2 & DESC_L_MASK))) {
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
void helper_lcall_real(CPUX86State *env, uint32_t new_cs, uint32_t new_eip,
                       int shift, uint32_t next_eip)
{
    StackAccess sa;

    sa.env = env;
    sa.ra = GETPC();
    sa.sp = env->regs[R_ESP];
    sa.sp_mask = get_sp_mask(env->segs[R_SS].flags);
    sa.ss_base = env->segs[R_SS].base;
    sa.mmu_index = x86_mmu_index_pl(env, 0);

    if (shift) {
        pushl(&sa, env->segs[R_CS].selector);
        pushl(&sa, next_eip);
    } else {
        pushw(&sa, env->segs[R_CS].selector);
        pushw(&sa, next_eip);
    }

    SET_ESP(sa.sp, sa.sp_mask);
    env->eip = new_eip;
    env->segs[R_CS].selector = new_cs;
    env->segs[R_CS].base = (new_cs << 4);
}

/* protected mode call */
void helper_lcall_protected(CPUX86State *env, int new_cs, target_ulong new_eip,
                            int shift, target_ulong next_eip)
{
    int new_stack, i;
    uint32_t e1, e2, cpl, dpl, rpl, selector, param_count;
    uint32_t ss = 0, ss_e1 = 0, ss_e2 = 0, type, ss_dpl;
    uint32_t val, limit, old_sp_mask;
    target_ulong old_ssp, offset;
    StackAccess sa;

    LOG_PCALL("lcall %04x:" TARGET_FMT_lx " s=%d\n", new_cs, new_eip, shift);
    LOG_PCALL_STATE(env_cpu(env));
    if ((new_cs & 0xfffc) == 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
    if (load_segment_ra(env, &e1, &e2, new_cs, GETPC()) != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
    }
    cpl = env->hflags & HF_CPL_MASK;
    LOG_PCALL("desc=%08x:%08x\n", e1, e2);

    sa.env = env;
    sa.ra = GETPC();

    if (e2 & DESC_S_MASK) {
        /* "normal" far call, no stack switch possible */
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

        sa.mmu_index = x86_mmu_index_pl(env, cpl);
#ifdef TARGET_X86_64
        /* XXX: check 16/32 bit cases in long mode */
        if (shift == 2) {
            /* 64 bit case */
            sa.sp = env->regs[R_ESP];
            sa.sp_mask = -1;
            sa.ss_base = 0;
            pushq(&sa, env->segs[R_CS].selector);
            pushq(&sa, next_eip);
            /* from this point, not restartable */
            env->regs[R_ESP] = sa.sp;
            cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2),
                                   get_seg_limit(e1, e2), e2);
            env->eip = new_eip;
        } else
#endif
        {
            sa.sp = env->regs[R_ESP];
            sa.sp_mask = get_sp_mask(env->segs[R_SS].flags);
            sa.ss_base = env->segs[R_SS].base;
            if (shift) {
                pushl(&sa, env->segs[R_CS].selector);
                pushl(&sa, next_eip);
            } else {
                pushw(&sa, env->segs[R_CS].selector);
                pushw(&sa, next_eip);
            }

            limit = get_seg_limit(e1, e2);
            if (new_eip > limit) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            /* from this point, not restartable */
            SET_ESP(sa.sp, sa.sp_mask);
            cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2), limit, e2);
            env->eip = new_eip;
        }
    } else {
        /* check gate type */
        type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        rpl = new_cs & 3;

#ifdef TARGET_X86_64
        if (env->efer & MSR_EFER_LMA) {
            if (type != 12) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        }
#endif

        switch (type) {
        case 1: /* available 286 TSS */
        case 9: /* available 386 TSS */
        case 5: /* task gate */
            if (dpl < cpl || dpl < rpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            switch_tss_ra(env, new_cs, e1, e2, SWITCH_TSS_CALL, next_eip,
                          false, 0, GETPC());
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
        param_count = e2 & 0x1f;
        offset = (e2 & 0xffff0000) | (e1 & 0x0000ffff);
#ifdef TARGET_X86_64
        if (env->efer & MSR_EFER_LMA) {
            /* load the upper 8 bytes of the 64-bit call gate */
            if (load_segment_ra(env, &e1, &e2, new_cs + 8, GETPC())) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc,
                                       GETPC());
            }
            type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
            if (type != 0) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc,
                                       GETPC());
            }
            offset |= ((target_ulong)e1) << 32;
        }
#endif
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
#ifdef TARGET_X86_64
        if (env->efer & MSR_EFER_LMA) {
            if (!(e2 & DESC_L_MASK)) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }
            if (e2 & DESC_B_MASK) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }
            shift++;
        }
#endif
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, GETPC());
        }

        if (!(e2 & DESC_C_MASK) && dpl < cpl) {
            /* to inner privilege */
            sa.mmu_index = x86_mmu_index_pl(env, dpl);
#ifdef TARGET_X86_64
            if (shift == 2) {
                ss = dpl;  /* SS = NULL selector with RPL = new CPL */
                new_stack = 1;
                sa.sp = get_rsp_from_tss(env, dpl);
                sa.sp_mask = -1;
                sa.ss_base = 0;  /* SS base is always zero in IA-32e mode */
                LOG_PCALL("new ss:rsp=%04x:%016llx env->regs[R_ESP]="
                          TARGET_FMT_lx "\n", ss, sa.sp, env->regs[R_ESP]);
            } else
#endif
            {
                uint32_t sp32;
                get_ss_esp_from_tss(env, &ss, &sp32, dpl, GETPC());
                LOG_PCALL("new ss:esp=%04x:%08x param_count=%d env->regs[R_ESP]="
                          TARGET_FMT_lx "\n", ss, sp32, param_count,
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

                sa.sp = sp32;
                sa.sp_mask = get_sp_mask(ss_e2);
                sa.ss_base = get_seg_base(ss_e1, ss_e2);
            }

            /* push_size = ((param_count * 2) + 8) << shift; */
            old_sp_mask = get_sp_mask(env->segs[R_SS].flags);
            old_ssp = env->segs[R_SS].base;

#ifdef TARGET_X86_64
            if (shift == 2) {
                /* XXX: verify if new stack address is canonical */
                pushq(&sa, env->segs[R_SS].selector);
                pushq(&sa, env->regs[R_ESP]);
                /* parameters aren't supported for 64-bit call gates */
            } else
#endif
            if (shift == 1) {
                pushl(&sa, env->segs[R_SS].selector);
                pushl(&sa, env->regs[R_ESP]);
                for (i = param_count - 1; i >= 0; i--) {
                    val = cpu_ldl_data_ra(env,
                                          old_ssp + ((env->regs[R_ESP] + i * 4) & old_sp_mask),
                                          GETPC());
                    pushl(&sa, val);
                }
            } else {
                pushw(&sa, env->segs[R_SS].selector);
                pushw(&sa, env->regs[R_ESP]);
                for (i = param_count - 1; i >= 0; i--) {
                    val = cpu_lduw_data_ra(env,
                                           old_ssp + ((env->regs[R_ESP] + i * 2) & old_sp_mask),
                                           GETPC());
                    pushw(&sa, val);
                }
            }
            new_stack = 1;
        } else {
            /* to same privilege */
            sa.mmu_index = x86_mmu_index_pl(env, cpl);
            sa.sp = env->regs[R_ESP];
            sa.sp_mask = get_sp_mask(env->segs[R_SS].flags);
            sa.ss_base = env->segs[R_SS].base;
            /* push_size = (4 << shift); */
            new_stack = 0;
        }

#ifdef TARGET_X86_64
        if (shift == 2) {
            pushq(&sa, env->segs[R_CS].selector);
            pushq(&sa, next_eip);
        } else
#endif
        if (shift == 1) {
            pushl(&sa, env->segs[R_CS].selector);
            pushl(&sa, next_eip);
        } else {
            pushw(&sa, env->segs[R_CS].selector);
            pushw(&sa, next_eip);
        }

        /* from this point, not restartable */

        if (new_stack) {
#ifdef TARGET_X86_64
            if (shift == 2) {
                cpu_x86_load_seg_cache(env, R_SS, ss, 0, 0, 0);
            } else
#endif
            {
                ss = (ss & ~3) | dpl;
                cpu_x86_load_seg_cache(env, R_SS, ss,
                                       sa.ss_base,
                                       get_seg_limit(ss_e1, ss_e2),
                                       ss_e2);
            }
        }

        selector = (selector & ~3) | dpl;
        cpu_x86_load_seg_cache(env, R_CS, selector,
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
        SET_ESP(sa.sp, sa.sp_mask);
        env->eip = offset;
    }
}

/* real and vm86 mode iret */
void helper_iret_real(CPUX86State *env, int shift)
{
    uint32_t new_cs, new_eip, new_eflags;
    int eflags_mask;
    StackAccess sa;

    sa.env = env;
    sa.ra = GETPC();
    sa.mmu_index = x86_mmu_index_pl(env, 0);
    sa.sp_mask = get_sp_mask(env->segs[R_SS].flags);
    sa.sp = env->regs[R_ESP];
    sa.ss_base = env->segs[R_SS].base;

    if (shift == 1) {
        /* 32 bits */
        new_eip = popl(&sa);
        new_cs = popl(&sa) & 0xffff;
        new_eflags = popl(&sa);
    } else {
        /* 16 bits */
        new_eip = popw(&sa);
        new_cs = popw(&sa);
        new_eflags = popw(&sa);
    }
    SET_ESP(sa.sp, sa.sp_mask);
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

static inline void validate_seg(CPUX86State *env, X86Seg seg_reg, int cpl)
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
            cpu_x86_load_seg_cache(env, seg_reg, 0,
                                   env->segs[seg_reg].base,
                                   env->segs[seg_reg].limit,
                                   env->segs[seg_reg].flags & ~DESC_P_MASK);
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
    target_ulong new_eip, new_esp;
    StackAccess sa;

    cpl = env->hflags & HF_CPL_MASK;

    sa.env = env;
    sa.ra = retaddr;
    sa.mmu_index = x86_mmu_index_pl(env, cpl);

#ifdef TARGET_X86_64
    if (shift == 2) {
        sa.sp_mask = -1;
    } else
#endif
    {
        sa.sp_mask = get_sp_mask(env->segs[R_SS].flags);
    }
    sa.sp = env->regs[R_ESP];
    sa.ss_base = env->segs[R_SS].base;
    new_eflags = 0; /* avoid warning */
#ifdef TARGET_X86_64
    if (shift == 2) {
        new_eip = popq(&sa);
        new_cs = popq(&sa) & 0xffff;
        if (is_iret) {
            new_eflags = popq(&sa);
        }
    } else
#endif
    {
        if (shift == 1) {
            /* 32 bits */
            new_eip = popl(&sa);
            new_cs = popl(&sa) & 0xffff;
            if (is_iret) {
                new_eflags = popl(&sa);
                if (new_eflags & VM_MASK) {
                    goto return_to_vm86;
                }
            }
        } else {
            /* 16 bits */
            new_eip = popw(&sa);
            new_cs = popw(&sa);
            if (is_iret) {
                new_eflags = popw(&sa);
            }
        }
    }
    LOG_PCALL("lret new %04x:" TARGET_FMT_lx " s=%d addend=0x%x\n",
              new_cs, new_eip, shift, addend);
    LOG_PCALL_STATE(env_cpu(env));
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

    sa.sp += addend;
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
            new_esp = popq(&sa);
            new_ss = popq(&sa) & 0xffff;
        } else
#endif
        {
            if (shift == 1) {
                /* 32 bits */
                new_esp = popl(&sa);
                new_ss = popl(&sa) & 0xffff;
            } else {
                /* 16 bits */
                new_esp = popw(&sa);
                new_ss = popw(&sa);
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
        sa.sp = new_esp;
#ifdef TARGET_X86_64
        if (env->hflags & HF_CS64_MASK) {
            sa.sp_mask = -1;
        } else
#endif
        {
            sa.sp_mask = get_sp_mask(ss_e2);
        }

        /* validate data segments */
        validate_seg(env, R_ES, rpl);
        validate_seg(env, R_DS, rpl);
        validate_seg(env, R_FS, rpl);
        validate_seg(env, R_GS, rpl);

        sa.sp += addend;
    }
    SET_ESP(sa.sp, sa.sp_mask);
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
    new_esp = popl(&sa);
    new_ss = popl(&sa);
    new_es = popl(&sa);
    new_ds = popl(&sa);
    new_fs = popl(&sa);
    new_gs = popl(&sa);

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
        switch_tss_ra(env, tss_selector, e1, e2, SWITCH_TSS_IRET, next_eip,
                      false, 0, GETPC());
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
    uint32_t e1, e2, selector;
    int rpl, dpl, cpl, type;

    selector = selector1 & 0xffff;
    assert(CC_OP == CC_OP_EFLAGS);
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
            CC_SRC &= ~CC_Z;
            return 0;
        }
    }
    limit = get_seg_limit(e1, e2);
    CC_SRC |= CC_Z;
    return limit;
}

target_ulong helper_lar(CPUX86State *env, target_ulong selector1)
{
    uint32_t e1, e2, selector;
    int rpl, dpl, cpl, type;

    selector = selector1 & 0xffff;
    assert(CC_OP == CC_OP_EFLAGS);
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
            CC_SRC &= ~CC_Z;
            return 0;
        }
    }
    CC_SRC |= CC_Z;
    return e2 & 0x00f0ff00;
}

void helper_verr(CPUX86State *env, target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl;

    selector = selector1 & 0xffff;
    eflags = cpu_cc_compute_all(env) | CC_Z;
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
            eflags &= ~CC_Z;
        }
    }
    CC_SRC = eflags;
    CC_OP = CC_OP_EFLAGS;
}

void helper_verw(CPUX86State *env, target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl;

    selector = selector1 & 0xffff;
    eflags = cpu_cc_compute_all(env) | CC_Z;
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
            eflags &= ~CC_Z;
        }
    }
    CC_SRC = eflags;
    CC_OP = CC_OP_EFLAGS;
}
