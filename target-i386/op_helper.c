/*
 *  i386 helpers
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
#include "dyngen-exec.h"
#include "ioport.h"
#include "qemu-log.h"
#include "cpu-defs.h"
#include "helper.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

//#define DEBUG_PCALL

#ifdef DEBUG_PCALL
# define LOG_PCALL(...) qemu_log_mask(CPU_LOG_PCALL, ## __VA_ARGS__)
# define LOG_PCALL_STATE(env)                                  \
    log_cpu_state_mask(CPU_LOG_PCALL, (env), X86_DUMP_CCOP)
#else
# define LOG_PCALL(...) do { } while (0)
# define LOG_PCALL_STATE(env) do { } while (0)
#endif

/* broken thread support */

static spinlock_t global_cpu_lock = SPIN_LOCK_UNLOCKED;

void helper_lock(void)
{
    spin_lock(&global_cpu_lock);
}

void helper_unlock(void)
{
    spin_unlock(&global_cpu_lock);
}

/* return non zero if error */
static inline int load_segment(uint32_t *e1_ptr, uint32_t *e2_ptr,
                               int selector)
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
    *e1_ptr = ldl_kernel(ptr);
    *e2_ptr = ldl_kernel(ptr + 4);
    return 0;
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
static inline void load_seg_vm(int seg, int selector)
{
    selector &= 0xffff;
    cpu_x86_load_seg_cache(env, seg, selector,
                           (selector << 4), 0xffff, 0);
}

static inline void get_ss_esp_from_tss(uint32_t *ss_ptr,
                                       uint32_t *esp_ptr, int dpl)
{
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
        cpu_abort(env, "invalid tss");
    }
    type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
    if ((type & 7) != 1) {
        cpu_abort(env, "invalid tss type");
    }
    shift = type >> 3;
    index = (dpl * 4 + 2) << shift;
    if (index + (4 << shift) - 1 > env->tr.limit) {
        raise_exception_err(env, EXCP0A_TSS, env->tr.selector & 0xfffc);
    }
    if (shift == 0) {
        *esp_ptr = lduw_kernel(env->tr.base + index);
        *ss_ptr = lduw_kernel(env->tr.base + index + 2);
    } else {
        *esp_ptr = ldl_kernel(env->tr.base + index);
        *ss_ptr = lduw_kernel(env->tr.base + index + 4);
    }
}

/* XXX: merge with load_seg() */
static void tss_load_seg(int seg_reg, int selector)
{
    uint32_t e1, e2;
    int rpl, dpl, cpl;

    if ((selector & 0xfffc) != 0) {
        if (load_segment(&e1, &e2, selector) != 0) {
            raise_exception_err(env, EXCP0A_TSS, selector & 0xfffc);
        }
        if (!(e2 & DESC_S_MASK)) {
            raise_exception_err(env, EXCP0A_TSS, selector & 0xfffc);
        }
        rpl = selector & 3;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        cpl = env->hflags & HF_CPL_MASK;
        if (seg_reg == R_CS) {
            if (!(e2 & DESC_CS_MASK)) {
                raise_exception_err(env, EXCP0A_TSS, selector & 0xfffc);
            }
            /* XXX: is it correct? */
            if (dpl != rpl) {
                raise_exception_err(env, EXCP0A_TSS, selector & 0xfffc);
            }
            if ((e2 & DESC_C_MASK) && dpl > rpl) {
                raise_exception_err(env, EXCP0A_TSS, selector & 0xfffc);
            }
        } else if (seg_reg == R_SS) {
            /* SS must be writable data */
            if ((e2 & DESC_CS_MASK) || !(e2 & DESC_W_MASK)) {
                raise_exception_err(env, EXCP0A_TSS, selector & 0xfffc);
            }
            if (dpl != cpl || dpl != rpl) {
                raise_exception_err(env, EXCP0A_TSS, selector & 0xfffc);
            }
        } else {
            /* not readable code */
            if ((e2 & DESC_CS_MASK) && !(e2 & DESC_R_MASK)) {
                raise_exception_err(env, EXCP0A_TSS, selector & 0xfffc);
            }
            /* if data or non conforming code, checks the rights */
            if (((e2 >> DESC_TYPE_SHIFT) & 0xf) < 12) {
                if (dpl < cpl || dpl < rpl) {
                    raise_exception_err(env, EXCP0A_TSS, selector & 0xfffc);
                }
            }
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0B_NOSEG, selector & 0xfffc);
        }
        cpu_x86_load_seg_cache(env, seg_reg, selector,
                               get_seg_base(e1, e2),
                               get_seg_limit(e1, e2),
                               e2);
    } else {
        if (seg_reg == R_SS || seg_reg == R_CS) {
            raise_exception_err(env, EXCP0A_TSS, selector & 0xfffc);
        }
    }
}

#define SWITCH_TSS_JMP  0
#define SWITCH_TSS_IRET 1
#define SWITCH_TSS_CALL 2

/* XXX: restore CPU state in registers (PowerPC case) */
static void switch_tss(int tss_selector,
                       uint32_t e1, uint32_t e2, int source,
                       uint32_t next_eip)
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
            raise_exception_err(env, EXCP0B_NOSEG, tss_selector & 0xfffc);
        }
        tss_selector = e1 >> 16;
        if (tss_selector & 4) {
            raise_exception_err(env, EXCP0A_TSS, tss_selector & 0xfffc);
        }
        if (load_segment(&e1, &e2, tss_selector) != 0) {
            raise_exception_err(env, EXCP0D_GPF, tss_selector & 0xfffc);
        }
        if (e2 & DESC_S_MASK) {
            raise_exception_err(env, EXCP0D_GPF, tss_selector & 0xfffc);
        }
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        if ((type & 7) != 1) {
            raise_exception_err(env, EXCP0D_GPF, tss_selector & 0xfffc);
        }
    }

    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err(env, EXCP0B_NOSEG, tss_selector & 0xfffc);
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
        raise_exception_err(env, EXCP0A_TSS, tss_selector & 0xfffc);
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
        new_cr3 = ldl_kernel(tss_base + 0x1c);
        new_eip = ldl_kernel(tss_base + 0x20);
        new_eflags = ldl_kernel(tss_base + 0x24);
        for (i = 0; i < 8; i++) {
            new_regs[i] = ldl_kernel(tss_base + (0x28 + i * 4));
        }
        for (i = 0; i < 6; i++) {
            new_segs[i] = lduw_kernel(tss_base + (0x48 + i * 4));
        }
        new_ldt = lduw_kernel(tss_base + 0x60);
        new_trap = ldl_kernel(tss_base + 0x64);
    } else {
        /* 16 bit */
        new_cr3 = 0;
        new_eip = lduw_kernel(tss_base + 0x0e);
        new_eflags = lduw_kernel(tss_base + 0x10);
        for (i = 0; i < 8; i++) {
            new_regs[i] = lduw_kernel(tss_base + (0x12 + i * 2)) | 0xffff0000;
        }
        for (i = 0; i < 4; i++) {
            new_segs[i] = lduw_kernel(tss_base + (0x22 + i * 4));
        }
        new_ldt = lduw_kernel(tss_base + 0x2a);
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

    v1 = ldub_kernel(env->tr.base);
    v2 = ldub_kernel(env->tr.base + old_tss_limit_max);
    stb_kernel(env->tr.base, v1);
    stb_kernel(env->tr.base + old_tss_limit_max, v2);

    /* clear busy bit (it is restartable) */
    if (source == SWITCH_TSS_JMP || source == SWITCH_TSS_IRET) {
        target_ulong ptr;
        uint32_t e2;

        ptr = env->gdt.base + (env->tr.selector & ~7);
        e2 = ldl_kernel(ptr + 4);
        e2 &= ~DESC_TSS_BUSY_MASK;
        stl_kernel(ptr + 4, e2);
    }
    old_eflags = cpu_compute_eflags(env);
    if (source == SWITCH_TSS_IRET) {
        old_eflags &= ~NT_MASK;
    }

    /* save the current state in the old TSS */
    if (type & 8) {
        /* 32 bit */
        stl_kernel(env->tr.base + 0x20, next_eip);
        stl_kernel(env->tr.base + 0x24, old_eflags);
        stl_kernel(env->tr.base + (0x28 + 0 * 4), EAX);
        stl_kernel(env->tr.base + (0x28 + 1 * 4), ECX);
        stl_kernel(env->tr.base + (0x28 + 2 * 4), EDX);
        stl_kernel(env->tr.base + (0x28 + 3 * 4), EBX);
        stl_kernel(env->tr.base + (0x28 + 4 * 4), ESP);
        stl_kernel(env->tr.base + (0x28 + 5 * 4), EBP);
        stl_kernel(env->tr.base + (0x28 + 6 * 4), ESI);
        stl_kernel(env->tr.base + (0x28 + 7 * 4), EDI);
        for (i = 0; i < 6; i++) {
            stw_kernel(env->tr.base + (0x48 + i * 4), env->segs[i].selector);
        }
    } else {
        /* 16 bit */
        stw_kernel(env->tr.base + 0x0e, next_eip);
        stw_kernel(env->tr.base + 0x10, old_eflags);
        stw_kernel(env->tr.base + (0x12 + 0 * 2), EAX);
        stw_kernel(env->tr.base + (0x12 + 1 * 2), ECX);
        stw_kernel(env->tr.base + (0x12 + 2 * 2), EDX);
        stw_kernel(env->tr.base + (0x12 + 3 * 2), EBX);
        stw_kernel(env->tr.base + (0x12 + 4 * 2), ESP);
        stw_kernel(env->tr.base + (0x12 + 5 * 2), EBP);
        stw_kernel(env->tr.base + (0x12 + 6 * 2), ESI);
        stw_kernel(env->tr.base + (0x12 + 7 * 2), EDI);
        for (i = 0; i < 4; i++) {
            stw_kernel(env->tr.base + (0x22 + i * 4), env->segs[i].selector);
        }
    }

    /* now if an exception occurs, it will occurs in the next task
       context */

    if (source == SWITCH_TSS_CALL) {
        stw_kernel(tss_base, env->tr.selector);
        new_eflags |= NT_MASK;
    }

    /* set busy bit */
    if (source == SWITCH_TSS_JMP || source == SWITCH_TSS_CALL) {
        target_ulong ptr;
        uint32_t e2;

        ptr = env->gdt.base + (tss_selector & ~7);
        e2 = ldl_kernel(ptr + 4);
        e2 |= DESC_TSS_BUSY_MASK;
        stl_kernel(ptr + 4, e2);
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
    EAX = new_regs[0];
    ECX = new_regs[1];
    EDX = new_regs[2];
    EBX = new_regs[3];
    ESP = new_regs[4];
    EBP = new_regs[5];
    ESI = new_regs[6];
    EDI = new_regs[7];
    if (new_eflags & VM_MASK) {
        for (i = 0; i < 6; i++) {
            load_seg_vm(i, new_segs[i]);
        }
        /* in vm86, CPL is always 3 */
        cpu_x86_set_cpl(env, 3);
    } else {
        /* CPL is set the RPL of CS */
        cpu_x86_set_cpl(env, new_segs[R_CS] & 3);
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
        raise_exception_err(env, EXCP0A_TSS, new_ldt & 0xfffc);
    }

    if ((new_ldt & 0xfffc) != 0) {
        dt = &env->gdt;
        index = new_ldt & ~7;
        if ((index + 7) > dt->limit) {
            raise_exception_err(env, EXCP0A_TSS, new_ldt & 0xfffc);
        }
        ptr = dt->base + index;
        e1 = ldl_kernel(ptr);
        e2 = ldl_kernel(ptr + 4);
        if ((e2 & DESC_S_MASK) || ((e2 >> DESC_TYPE_SHIFT) & 0xf) != 2) {
            raise_exception_err(env, EXCP0A_TSS, new_ldt & 0xfffc);
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0A_TSS, new_ldt & 0xfffc);
        }
        load_seg_cache_raw_dt(&env->ldt, e1, e2);
    }

    /* load the segments */
    if (!(new_eflags & VM_MASK)) {
        tss_load_seg(R_CS, new_segs[R_CS]);
        tss_load_seg(R_SS, new_segs[R_SS]);
        tss_load_seg(R_ES, new_segs[R_ES]);
        tss_load_seg(R_DS, new_segs[R_DS]);
        tss_load_seg(R_FS, new_segs[R_FS]);
        tss_load_seg(R_GS, new_segs[R_GS]);
    }

    /* check that EIP is in the CS segment limits */
    if (new_eip > env->segs[R_CS].limit) {
        /* XXX: different exception if CALL? */
        raise_exception_err(env, EXCP0D_GPF, 0);
    }

#ifndef CONFIG_USER_ONLY
    /* reset local breakpoints */
    if (env->dr[7] & 0x55) {
        for (i = 0; i < 4; i++) {
            if (hw_breakpoint_enabled(env->dr[7], i) == 0x1) {
                hw_breakpoint_remove(env, i);
            }
        }
        env->dr[7] &= ~0x55;
    }
#endif
}

/* check if Port I/O is allowed in TSS */
static inline void check_io(int addr, int size)
{
    int io_offset, val, mask;

    /* TSS must be a valid 32 bit one */
    if (!(env->tr.flags & DESC_P_MASK) ||
        ((env->tr.flags >> DESC_TYPE_SHIFT) & 0xf) != 9 ||
        env->tr.limit < 103) {
        goto fail;
    }
    io_offset = lduw_kernel(env->tr.base + 0x66);
    io_offset += (addr >> 3);
    /* Note: the check needs two bytes */
    if ((io_offset + 1) > env->tr.limit) {
        goto fail;
    }
    val = lduw_kernel(env->tr.base + io_offset);
    val >>= (addr & 7);
    mask = (1 << size) - 1;
    /* all bits must be zero to allow the I/O */
    if ((val & mask) != 0) {
    fail:
        raise_exception_err(env, EXCP0D_GPF, 0);
    }
}

void helper_check_iob(uint32_t t0)
{
    check_io(t0, 1);
}

void helper_check_iow(uint32_t t0)
{
    check_io(t0, 2);
}

void helper_check_iol(uint32_t t0)
{
    check_io(t0, 4);
}

void helper_outb(uint32_t port, uint32_t data)
{
    cpu_outb(port, data & 0xff);
}

target_ulong helper_inb(uint32_t port)
{
    return cpu_inb(port);
}

void helper_outw(uint32_t port, uint32_t data)
{
    cpu_outw(port, data & 0xffff);
}

target_ulong helper_inw(uint32_t port)
{
    return cpu_inw(port);
}

void helper_outl(uint32_t port, uint32_t data)
{
    cpu_outl(port, data);
}

target_ulong helper_inl(uint32_t port)
{
    return cpu_inl(port);
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
#define SET_ESP(val, sp_mask)                           \
    do {                                                \
        if ((sp_mask) == 0xffff) {                      \
            ESP = (ESP & ~0xffff) | ((val) & 0xffff);   \
        } else if ((sp_mask) == 0xffffffffLL) {         \
            ESP = (uint32_t)(val);                      \
        } else {                                        \
            ESP = (val);                                \
        }                                               \
    } while (0)
#else
#define SET_ESP(val, sp_mask)                           \
    do {                                                \
        ESP = (ESP & ~(sp_mask)) | ((val) & (sp_mask)); \
    } while (0)
#endif

/* in 64-bit machines, this can overflow. So this segment addition macro
 * can be used to trim the value to 32-bit whenever needed */
#define SEG_ADDL(ssp, sp, sp_mask) ((uint32_t)((ssp) + (sp & (sp_mask))))

/* XXX: add a is_user flag to have proper security support */
#define PUSHW(ssp, sp, sp_mask, val)                    \
    {                                                   \
        sp -= 2;                                        \
        stw_kernel((ssp) + (sp & (sp_mask)), (val));    \
    }

#define PUSHL(ssp, sp, sp_mask, val)                                    \
    {                                                                   \
        sp -= 4;                                                        \
        stl_kernel(SEG_ADDL(ssp, sp, sp_mask), (uint32_t)(val));        \
    }

#define POPW(ssp, sp, sp_mask, val)                     \
    {                                                   \
        val = lduw_kernel((ssp) + (sp & (sp_mask)));    \
        sp += 2;                                        \
    }

#define POPL(ssp, sp, sp_mask, val)                             \
    {                                                           \
        val = (uint32_t)ldl_kernel(SEG_ADDL(ssp, sp, sp_mask)); \
        sp += 4;                                                \
    }

/* protected mode interrupt */
static void do_interrupt_protected(int intno, int is_int, int error_code,
                                   unsigned int next_eip, int is_hw)
{
    SegmentCache *dt;
    target_ulong ptr, ssp;
    int type, dpl, selector, ss_dpl, cpl;
    int has_error_code, new_stack, shift;
    uint32_t e1, e2, offset, ss = 0, esp, ss_e1 = 0, ss_e2 = 0;
    uint32_t old_eip, sp_mask;

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
    e1 = ldl_kernel(ptr);
    e2 = ldl_kernel(ptr + 4);
    /* check gate type */
    type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
    switch (type) {
    case 5: /* task gate */
        /* must do that check here to return the correct error code */
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0B_NOSEG, intno * 8 + 2);
        }
        switch_tss(intno * 8, e1, e2, SWITCH_TSS_CALL, old_eip);
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
            esp = (ESP - (2 << shift)) & mask;
            ssp = env->segs[R_SS].base + esp;
            if (shift) {
                stl_kernel(ssp, error_code);
            } else {
                stw_kernel(ssp, error_code);
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
    if (load_segment(&e1, &e2, selector) != 0) {
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
        get_ss_esp_from_tss(&ss, &esp, dpl);
        if ((ss & 0xfffc) == 0) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        if ((ss & 3) != dpl) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        if (load_segment(&ss_e1, &ss_e2, ss) != 0) {
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
        if (env->eflags & VM_MASK) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        new_stack = 0;
        sp_mask = get_sp_mask(env->segs[R_SS].flags);
        ssp = env->segs[R_SS].base;
        esp = ESP;
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
    if (env->eflags & VM_MASK) {
        push_size += 8;
    }
    push_size <<= shift;
#endif
    if (shift == 1) {
        if (new_stack) {
            if (env->eflags & VM_MASK) {
                PUSHL(ssp, esp, sp_mask, env->segs[R_GS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_FS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_DS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_ES].selector);
            }
            PUSHL(ssp, esp, sp_mask, env->segs[R_SS].selector);
            PUSHL(ssp, esp, sp_mask, ESP);
        }
        PUSHL(ssp, esp, sp_mask, cpu_compute_eflags(env));
        PUSHL(ssp, esp, sp_mask, env->segs[R_CS].selector);
        PUSHL(ssp, esp, sp_mask, old_eip);
        if (has_error_code) {
            PUSHL(ssp, esp, sp_mask, error_code);
        }
    } else {
        if (new_stack) {
            if (env->eflags & VM_MASK) {
                PUSHW(ssp, esp, sp_mask, env->segs[R_GS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_FS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_DS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_ES].selector);
            }
            PUSHW(ssp, esp, sp_mask, env->segs[R_SS].selector);
            PUSHW(ssp, esp, sp_mask, ESP);
        }
        PUSHW(ssp, esp, sp_mask, cpu_compute_eflags(env));
        PUSHW(ssp, esp, sp_mask, env->segs[R_CS].selector);
        PUSHW(ssp, esp, sp_mask, old_eip);
        if (has_error_code) {
            PUSHW(ssp, esp, sp_mask, error_code);
        }
    }

    if (new_stack) {
        if (env->eflags & VM_MASK) {
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
    cpu_x86_set_cpl(env, dpl);
    env->eip = offset;

    /* interrupt gate clear IF mask */
    if ((type & 1) == 0) {
        env->eflags &= ~IF_MASK;
    }
    env->eflags &= ~(TF_MASK | VM_MASK | RF_MASK | NT_MASK);
}

#ifdef TARGET_X86_64

#define PUSHQ(sp, val)                          \
    {                                           \
        sp -= 8;                                \
        stq_kernel(sp, (val));                  \
    }

#define POPQ(sp, val)                           \
    {                                           \
        val = ldq_kernel(sp);                   \
        sp += 8;                                \
    }

static inline target_ulong get_rsp_from_tss(int level)
{
    int index;

#if 0
    printf("TR: base=" TARGET_FMT_lx " limit=%x\n",
           env->tr.base, env->tr.limit);
#endif

    if (!(env->tr.flags & DESC_P_MASK)) {
        cpu_abort(env, "invalid tss");
    }
    index = 8 * level + 4;
    if ((index + 7) > env->tr.limit) {
        raise_exception_err(env, EXCP0A_TSS, env->tr.selector & 0xfffc);
    }
    return ldq_kernel(env->tr.base + index);
}

/* 64 bit interrupt */
static void do_interrupt64(int intno, int is_int, int error_code,
                           target_ulong next_eip, int is_hw)
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
    e1 = ldl_kernel(ptr);
    e2 = ldl_kernel(ptr + 4);
    e3 = ldl_kernel(ptr + 8);
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

    if (load_segment(&e1, &e2, selector) != 0) {
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
        if (ist != 0) {
            esp = get_rsp_from_tss(ist + 3);
        } else {
            esp = get_rsp_from_tss(dpl);
        }
        esp &= ~0xfLL; /* align stack */
        ss = 0;
        new_stack = 1;
    } else if ((e2 & DESC_C_MASK) || dpl == cpl) {
        /* to same privilege */
        if (env->eflags & VM_MASK) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        new_stack = 0;
        if (ist != 0) {
            esp = get_rsp_from_tss(ist + 3);
        } else {
            esp = ESP;
        }
        esp &= ~0xfLL; /* align stack */
        dpl = cpl;
    } else {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        new_stack = 0; /* avoid warning */
        esp = 0; /* avoid warning */
    }

    PUSHQ(esp, env->segs[R_SS].selector);
    PUSHQ(esp, ESP);
    PUSHQ(esp, cpu_compute_eflags(env));
    PUSHQ(esp, env->segs[R_CS].selector);
    PUSHQ(esp, old_eip);
    if (has_error_code) {
        PUSHQ(esp, error_code);
    }

    if (new_stack) {
        ss = 0 | dpl;
        cpu_x86_load_seg_cache(env, R_SS, ss, 0, 0, 0);
    }
    ESP = esp;

    selector = (selector & ~3) | dpl;
    cpu_x86_load_seg_cache(env, R_CS, selector,
                   get_seg_base(e1, e2),
                   get_seg_limit(e1, e2),
                   e2);
    cpu_x86_set_cpl(env, dpl);
    env->eip = offset;

    /* interrupt gate clear IF mask */
    if ((type & 1) == 0) {
        env->eflags &= ~IF_MASK;
    }
    env->eflags &= ~(TF_MASK | VM_MASK | RF_MASK | NT_MASK);
}
#endif

#ifdef TARGET_X86_64
#if defined(CONFIG_USER_ONLY)
void helper_syscall(int next_eip_addend)
{
    env->exception_index = EXCP_SYSCALL;
    env->exception_next_eip = env->eip + next_eip_addend;
    cpu_loop_exit(env);
}
#else
void helper_syscall(int next_eip_addend)
{
    int selector;

    if (!(env->efer & MSR_EFER_SCE)) {
        raise_exception_err(env, EXCP06_ILLOP, 0);
    }
    selector = (env->star >> 32) & 0xffff;
    if (env->hflags & HF_LMA_MASK) {
        int code64;

        ECX = env->eip + next_eip_addend;
        env->regs[11] = cpu_compute_eflags(env);

        code64 = env->hflags & HF_CS64_MASK;

        cpu_x86_set_cpl(env, 0);
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
        env->eflags &= ~env->fmask;
        cpu_load_eflags(env, env->eflags, 0);
        if (code64) {
            env->eip = env->lstar;
        } else {
            env->eip = env->cstar;
        }
    } else {
        ECX = (uint32_t)(env->eip + next_eip_addend);

        cpu_x86_set_cpl(env, 0);
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
        env->eflags &= ~(IF_MASK | RF_MASK | VM_MASK);
        env->eip = (uint32_t)env->star;
    }
}
#endif
#endif

#ifdef TARGET_X86_64
void helper_sysret(int dflag)
{
    int cpl, selector;

    if (!(env->efer & MSR_EFER_SCE)) {
        raise_exception_err(env, EXCP06_ILLOP, 0);
    }
    cpl = env->hflags & HF_CPL_MASK;
    if (!(env->cr[0] & CR0_PE_MASK) || cpl != 0) {
        raise_exception_err(env, EXCP0D_GPF, 0);
    }
    selector = (env->star >> 48) & 0xffff;
    if (env->hflags & HF_LMA_MASK) {
        if (dflag == 2) {
            cpu_x86_load_seg_cache(env, R_CS, (selector + 16) | 3,
                                   0, 0xffffffff,
                                   DESC_G_MASK | DESC_P_MASK |
                                   DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                                   DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
                                   DESC_L_MASK);
            env->eip = ECX;
        } else {
            cpu_x86_load_seg_cache(env, R_CS, selector | 3,
                                   0, 0xffffffff,
                                   DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                                   DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                                   DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
            env->eip = (uint32_t)ECX;
        }
        cpu_x86_load_seg_cache(env, R_SS, selector + 8,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
        cpu_load_eflags(env, (uint32_t)(env->regs[11]), TF_MASK | AC_MASK
                        | ID_MASK | IF_MASK | IOPL_MASK | VM_MASK | RF_MASK |
                        NT_MASK);
        cpu_x86_set_cpl(env, 3);
    } else {
        cpu_x86_load_seg_cache(env, R_CS, selector | 3,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        env->eip = (uint32_t)ECX;
        cpu_x86_load_seg_cache(env, R_SS, selector + 8,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
        env->eflags |= IF_MASK;
        cpu_x86_set_cpl(env, 3);
    }
}
#endif

/* real mode interrupt */
static void do_interrupt_real(int intno, int is_int, int error_code,
                              unsigned int next_eip)
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
    offset = lduw_kernel(ptr);
    selector = lduw_kernel(ptr + 2);
    esp = ESP;
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
    ESP = (ESP & ~0xffff) | (esp & 0xffff);
    env->eip = offset;
    env->segs[R_CS].selector = selector;
    env->segs[R_CS].base = (selector << 4);
    env->eflags &= ~(IF_MASK | TF_MASK | AC_MASK | RF_MASK);
}

#if defined(CONFIG_USER_ONLY)
/* fake user mode interrupt */
static void do_interrupt_user(int intno, int is_int, int error_code,
                              target_ulong next_eip)
{
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
    e2 = ldl_kernel(ptr + 4);

    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    /* check privilege if software int */
    if (is_int && dpl < cpl) {
        raise_exception_err(env, EXCP0D_GPF, (intno << shift) + 2);
    }

    /* Since we emulate only user space, we cannot do more than
       exiting the emulation with the suitable exception and error
       code */
    if (is_int) {
        EIP = next_eip;
    }
}

#else

static void handle_even_inj(int intno, int is_int, int error_code,
                            int is_hw, int rm)
{
    uint32_t event_inj = ldl_phys(env->vm_vmcb + offsetof(struct vmcb,
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
            stl_phys(env->vm_vmcb + offsetof(struct vmcb,
                                             control.event_inj_err),
                     error_code);
        }
        stl_phys(env->vm_vmcb + offsetof(struct vmcb, control.event_inj),
                 event_inj);
    }
}
#endif

/*
 * Begin execution of an interruption. is_int is TRUE if coming from
 * the int instruction. next_eip is the EIP value AFTER the interrupt
 * instruction. It is only relevant if is_int is TRUE.
 */
static void do_interrupt_all(int intno, int is_int, int error_code,
                             target_ulong next_eip, int is_hw)
{
    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        if ((env->cr[0] & CR0_PE_MASK)) {
            static int count;

            qemu_log("%6d: v=%02x e=%04x i=%d cpl=%d IP=%04x:" TARGET_FMT_lx
                     " pc=" TARGET_FMT_lx " SP=%04x:" TARGET_FMT_lx,
                     count, intno, error_code, is_int,
                     env->hflags & HF_CPL_MASK,
                     env->segs[R_CS].selector, EIP,
                     (int)env->segs[R_CS].base + EIP,
                     env->segs[R_SS].selector, ESP);
            if (intno == 0x0e) {
                qemu_log(" CR2=" TARGET_FMT_lx, env->cr[2]);
            } else {
                qemu_log(" EAX=" TARGET_FMT_lx, EAX);
            }
            qemu_log("\n");
            log_cpu_state(env, X86_DUMP_CCOP);
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
            handle_even_inj(intno, is_int, error_code, is_hw, 0);
        }
#endif
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            do_interrupt64(intno, is_int, error_code, next_eip, is_hw);
        } else
#endif
        {
            do_interrupt_protected(intno, is_int, error_code, next_eip, is_hw);
        }
    } else {
#if !defined(CONFIG_USER_ONLY)
        if (env->hflags & HF_SVMI_MASK) {
            handle_even_inj(intno, is_int, error_code, is_hw, 1);
        }
#endif
        do_interrupt_real(intno, is_int, error_code, next_eip);
    }

#if !defined(CONFIG_USER_ONLY)
    if (env->hflags & HF_SVMI_MASK) {
        uint32_t event_inj = ldl_phys(env->vm_vmcb +
                                      offsetof(struct vmcb,
                                               control.event_inj));

        stl_phys(env->vm_vmcb + offsetof(struct vmcb, control.event_inj),
                 event_inj & ~SVM_EVTINJ_VALID);
    }
#endif
}

void do_interrupt(CPUX86State *env1)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = env1;
#if defined(CONFIG_USER_ONLY)
    /* if user mode only, we simulate a fake exception
       which will be handled outside the cpu execution
       loop */
    do_interrupt_user(env->exception_index,
                      env->exception_is_int,
                      env->error_code,
                      env->exception_next_eip);
    /* successfully delivered */
    env->old_exception = -1;
#else
    /* simulate a real cpu exception. On i386, it can
       trigger new exceptions, but we do not handle
       double or triple faults yet. */
    do_interrupt_all(env->exception_index,
                     env->exception_is_int,
                     env->error_code,
                     env->exception_next_eip, 0);
    /* successfully delivered */
    env->old_exception = -1;
#endif
    env = saved_env;
}

void do_interrupt_x86_hardirq(CPUX86State *env1, int intno, int is_hw)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = env1;
    do_interrupt_all(intno, 0, 0, 0, is_hw);
    env = saved_env;
}

/* SMM support */

#if defined(CONFIG_USER_ONLY)

void do_smm_enter(CPUX86State *env1)
{
}

void helper_rsm(void)
{
}

#else

#ifdef TARGET_X86_64
#define SMM_REVISION_ID 0x00020064
#else
#define SMM_REVISION_ID 0x00020000
#endif

void do_smm_enter(CPUX86State *env1)
{
    target_ulong sm_state;
    SegmentCache *dt;
    int i, offset;
    CPUX86State *saved_env;

    saved_env = env;
    env = env1;

    qemu_log_mask(CPU_LOG_INT, "SMM: enter\n");
    log_cpu_state_mask(CPU_LOG_INT, env, X86_DUMP_CCOP);

    env->hflags |= HF_SMM_MASK;
    cpu_smm_update(env);

    sm_state = env->smbase + 0x8000;

#ifdef TARGET_X86_64
    for (i = 0; i < 6; i++) {
        dt = &env->segs[i];
        offset = 0x7e00 + i * 16;
        stw_phys(sm_state + offset, dt->selector);
        stw_phys(sm_state + offset + 2, (dt->flags >> 8) & 0xf0ff);
        stl_phys(sm_state + offset + 4, dt->limit);
        stq_phys(sm_state + offset + 8, dt->base);
    }

    stq_phys(sm_state + 0x7e68, env->gdt.base);
    stl_phys(sm_state + 0x7e64, env->gdt.limit);

    stw_phys(sm_state + 0x7e70, env->ldt.selector);
    stq_phys(sm_state + 0x7e78, env->ldt.base);
    stl_phys(sm_state + 0x7e74, env->ldt.limit);
    stw_phys(sm_state + 0x7e72, (env->ldt.flags >> 8) & 0xf0ff);

    stq_phys(sm_state + 0x7e88, env->idt.base);
    stl_phys(sm_state + 0x7e84, env->idt.limit);

    stw_phys(sm_state + 0x7e90, env->tr.selector);
    stq_phys(sm_state + 0x7e98, env->tr.base);
    stl_phys(sm_state + 0x7e94, env->tr.limit);
    stw_phys(sm_state + 0x7e92, (env->tr.flags >> 8) & 0xf0ff);

    stq_phys(sm_state + 0x7ed0, env->efer);

    stq_phys(sm_state + 0x7ff8, EAX);
    stq_phys(sm_state + 0x7ff0, ECX);
    stq_phys(sm_state + 0x7fe8, EDX);
    stq_phys(sm_state + 0x7fe0, EBX);
    stq_phys(sm_state + 0x7fd8, ESP);
    stq_phys(sm_state + 0x7fd0, EBP);
    stq_phys(sm_state + 0x7fc8, ESI);
    stq_phys(sm_state + 0x7fc0, EDI);
    for (i = 8; i < 16; i++) {
        stq_phys(sm_state + 0x7ff8 - i * 8, env->regs[i]);
    }
    stq_phys(sm_state + 0x7f78, env->eip);
    stl_phys(sm_state + 0x7f70, cpu_compute_eflags(env));
    stl_phys(sm_state + 0x7f68, env->dr[6]);
    stl_phys(sm_state + 0x7f60, env->dr[7]);

    stl_phys(sm_state + 0x7f48, env->cr[4]);
    stl_phys(sm_state + 0x7f50, env->cr[3]);
    stl_phys(sm_state + 0x7f58, env->cr[0]);

    stl_phys(sm_state + 0x7efc, SMM_REVISION_ID);
    stl_phys(sm_state + 0x7f00, env->smbase);
#else
    stl_phys(sm_state + 0x7ffc, env->cr[0]);
    stl_phys(sm_state + 0x7ff8, env->cr[3]);
    stl_phys(sm_state + 0x7ff4, cpu_compute_eflags(env));
    stl_phys(sm_state + 0x7ff0, env->eip);
    stl_phys(sm_state + 0x7fec, EDI);
    stl_phys(sm_state + 0x7fe8, ESI);
    stl_phys(sm_state + 0x7fe4, EBP);
    stl_phys(sm_state + 0x7fe0, ESP);
    stl_phys(sm_state + 0x7fdc, EBX);
    stl_phys(sm_state + 0x7fd8, EDX);
    stl_phys(sm_state + 0x7fd4, ECX);
    stl_phys(sm_state + 0x7fd0, EAX);
    stl_phys(sm_state + 0x7fcc, env->dr[6]);
    stl_phys(sm_state + 0x7fc8, env->dr[7]);

    stl_phys(sm_state + 0x7fc4, env->tr.selector);
    stl_phys(sm_state + 0x7f64, env->tr.base);
    stl_phys(sm_state + 0x7f60, env->tr.limit);
    stl_phys(sm_state + 0x7f5c, (env->tr.flags >> 8) & 0xf0ff);

    stl_phys(sm_state + 0x7fc0, env->ldt.selector);
    stl_phys(sm_state + 0x7f80, env->ldt.base);
    stl_phys(sm_state + 0x7f7c, env->ldt.limit);
    stl_phys(sm_state + 0x7f78, (env->ldt.flags >> 8) & 0xf0ff);

    stl_phys(sm_state + 0x7f74, env->gdt.base);
    stl_phys(sm_state + 0x7f70, env->gdt.limit);

    stl_phys(sm_state + 0x7f58, env->idt.base);
    stl_phys(sm_state + 0x7f54, env->idt.limit);

    for (i = 0; i < 6; i++) {
        dt = &env->segs[i];
        if (i < 3) {
            offset = 0x7f84 + i * 12;
        } else {
            offset = 0x7f2c + (i - 3) * 12;
        }
        stl_phys(sm_state + 0x7fa8 + i * 4, dt->selector);
        stl_phys(sm_state + offset + 8, dt->base);
        stl_phys(sm_state + offset + 4, dt->limit);
        stl_phys(sm_state + offset, (dt->flags >> 8) & 0xf0ff);
    }
    stl_phys(sm_state + 0x7f14, env->cr[4]);

    stl_phys(sm_state + 0x7efc, SMM_REVISION_ID);
    stl_phys(sm_state + 0x7ef8, env->smbase);
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
    env = saved_env;
}

void helper_rsm(void)
{
    target_ulong sm_state;
    int i, offset;
    uint32_t val;

    sm_state = env->smbase + 0x8000;
#ifdef TARGET_X86_64
    cpu_load_efer(env, ldq_phys(sm_state + 0x7ed0));

    for (i = 0; i < 6; i++) {
        offset = 0x7e00 + i * 16;
        cpu_x86_load_seg_cache(env, i,
                               lduw_phys(sm_state + offset),
                               ldq_phys(sm_state + offset + 8),
                               ldl_phys(sm_state + offset + 4),
                               (lduw_phys(sm_state + offset + 2) &
                                0xf0ff) << 8);
    }

    env->gdt.base = ldq_phys(sm_state + 0x7e68);
    env->gdt.limit = ldl_phys(sm_state + 0x7e64);

    env->ldt.selector = lduw_phys(sm_state + 0x7e70);
    env->ldt.base = ldq_phys(sm_state + 0x7e78);
    env->ldt.limit = ldl_phys(sm_state + 0x7e74);
    env->ldt.flags = (lduw_phys(sm_state + 0x7e72) & 0xf0ff) << 8;

    env->idt.base = ldq_phys(sm_state + 0x7e88);
    env->idt.limit = ldl_phys(sm_state + 0x7e84);

    env->tr.selector = lduw_phys(sm_state + 0x7e90);
    env->tr.base = ldq_phys(sm_state + 0x7e98);
    env->tr.limit = ldl_phys(sm_state + 0x7e94);
    env->tr.flags = (lduw_phys(sm_state + 0x7e92) & 0xf0ff) << 8;

    EAX = ldq_phys(sm_state + 0x7ff8);
    ECX = ldq_phys(sm_state + 0x7ff0);
    EDX = ldq_phys(sm_state + 0x7fe8);
    EBX = ldq_phys(sm_state + 0x7fe0);
    ESP = ldq_phys(sm_state + 0x7fd8);
    EBP = ldq_phys(sm_state + 0x7fd0);
    ESI = ldq_phys(sm_state + 0x7fc8);
    EDI = ldq_phys(sm_state + 0x7fc0);
    for (i = 8; i < 16; i++) {
        env->regs[i] = ldq_phys(sm_state + 0x7ff8 - i * 8);
    }
    env->eip = ldq_phys(sm_state + 0x7f78);
    cpu_load_eflags(env, ldl_phys(sm_state + 0x7f70),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    env->dr[6] = ldl_phys(sm_state + 0x7f68);
    env->dr[7] = ldl_phys(sm_state + 0x7f60);

    cpu_x86_update_cr4(env, ldl_phys(sm_state + 0x7f48));
    cpu_x86_update_cr3(env, ldl_phys(sm_state + 0x7f50));
    cpu_x86_update_cr0(env, ldl_phys(sm_state + 0x7f58));

    val = ldl_phys(sm_state + 0x7efc); /* revision ID */
    if (val & 0x20000) {
        env->smbase = ldl_phys(sm_state + 0x7f00) & ~0x7fff;
    }
#else
    cpu_x86_update_cr0(env, ldl_phys(sm_state + 0x7ffc));
    cpu_x86_update_cr3(env, ldl_phys(sm_state + 0x7ff8));
    cpu_load_eflags(env, ldl_phys(sm_state + 0x7ff4),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    env->eip = ldl_phys(sm_state + 0x7ff0);
    EDI = ldl_phys(sm_state + 0x7fec);
    ESI = ldl_phys(sm_state + 0x7fe8);
    EBP = ldl_phys(sm_state + 0x7fe4);
    ESP = ldl_phys(sm_state + 0x7fe0);
    EBX = ldl_phys(sm_state + 0x7fdc);
    EDX = ldl_phys(sm_state + 0x7fd8);
    ECX = ldl_phys(sm_state + 0x7fd4);
    EAX = ldl_phys(sm_state + 0x7fd0);
    env->dr[6] = ldl_phys(sm_state + 0x7fcc);
    env->dr[7] = ldl_phys(sm_state + 0x7fc8);

    env->tr.selector = ldl_phys(sm_state + 0x7fc4) & 0xffff;
    env->tr.base = ldl_phys(sm_state + 0x7f64);
    env->tr.limit = ldl_phys(sm_state + 0x7f60);
    env->tr.flags = (ldl_phys(sm_state + 0x7f5c) & 0xf0ff) << 8;

    env->ldt.selector = ldl_phys(sm_state + 0x7fc0) & 0xffff;
    env->ldt.base = ldl_phys(sm_state + 0x7f80);
    env->ldt.limit = ldl_phys(sm_state + 0x7f7c);
    env->ldt.flags = (ldl_phys(sm_state + 0x7f78) & 0xf0ff) << 8;

    env->gdt.base = ldl_phys(sm_state + 0x7f74);
    env->gdt.limit = ldl_phys(sm_state + 0x7f70);

    env->idt.base = ldl_phys(sm_state + 0x7f58);
    env->idt.limit = ldl_phys(sm_state + 0x7f54);

    for (i = 0; i < 6; i++) {
        if (i < 3) {
            offset = 0x7f84 + i * 12;
        } else {
            offset = 0x7f2c + (i - 3) * 12;
        }
        cpu_x86_load_seg_cache(env, i,
                               ldl_phys(sm_state + 0x7fa8 + i * 4) & 0xffff,
                               ldl_phys(sm_state + offset + 8),
                               ldl_phys(sm_state + offset + 4),
                               (ldl_phys(sm_state + offset) & 0xf0ff) << 8);
    }
    cpu_x86_update_cr4(env, ldl_phys(sm_state + 0x7f14));

    val = ldl_phys(sm_state + 0x7efc); /* revision ID */
    if (val & 0x20000) {
        env->smbase = ldl_phys(sm_state + 0x7ef8) & ~0x7fff;
    }
#endif
    CC_OP = CC_OP_EFLAGS;
    env->hflags &= ~HF_SMM_MASK;
    cpu_smm_update(env);

    qemu_log_mask(CPU_LOG_INT, "SMM: after RSM\n");
    log_cpu_state_mask(CPU_LOG_INT, env, X86_DUMP_CCOP);
}

#endif /* !CONFIG_USER_ONLY */

void helper_into(int next_eip_addend)
{
    int eflags;

    eflags = helper_cc_compute_all(CC_OP);
    if (eflags & CC_O) {
        raise_interrupt(env, EXCP04_INTO, 1, 0, next_eip_addend);
    }
}

void helper_cmpxchg8b(target_ulong a0)
{
    uint64_t d;
    int eflags;

    eflags = helper_cc_compute_all(CC_OP);
    d = ldq(a0);
    if (d == (((uint64_t)EDX << 32) | (uint32_t)EAX)) {
        stq(a0, ((uint64_t)ECX << 32) | (uint32_t)EBX);
        eflags |= CC_Z;
    } else {
        /* always do the store */
        stq(a0, d);
        EDX = (uint32_t)(d >> 32);
        EAX = (uint32_t)d;
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
}

#ifdef TARGET_X86_64
void helper_cmpxchg16b(target_ulong a0)
{
    uint64_t d0, d1;
    int eflags;

    if ((a0 & 0xf) != 0) {
        raise_exception(env, EXCP0D_GPF);
    }
    eflags = helper_cc_compute_all(CC_OP);
    d0 = ldq(a0);
    d1 = ldq(a0 + 8);
    if (d0 == EAX && d1 == EDX) {
        stq(a0, EBX);
        stq(a0 + 8, ECX);
        eflags |= CC_Z;
    } else {
        /* always do the store */
        stq(a0, d0);
        stq(a0 + 8, d1);
        EDX = d1;
        EAX = d0;
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
}
#endif

void helper_single_step(void)
{
#ifndef CONFIG_USER_ONLY
    check_hw_breakpoints(env, 1);
    env->dr[6] |= DR6_BS;
#endif
    raise_exception(env, EXCP01_DB);
}

void helper_cpuid(void)
{
    uint32_t eax, ebx, ecx, edx;

    helper_svm_check_intercept_param(SVM_EXIT_CPUID, 0);

    cpu_x86_cpuid(env, (uint32_t)EAX, (uint32_t)ECX, &eax, &ebx, &ecx, &edx);
    EAX = eax;
    EBX = ebx;
    ECX = ecx;
    EDX = edx;
}

void helper_enter_level(int level, int data32, target_ulong t1)
{
    target_ulong ssp;
    uint32_t esp_mask, esp, ebp;

    esp_mask = get_sp_mask(env->segs[R_SS].flags);
    ssp = env->segs[R_SS].base;
    ebp = EBP;
    esp = ESP;
    if (data32) {
        /* 32 bit */
        esp -= 4;
        while (--level) {
            esp -= 4;
            ebp -= 4;
            stl(ssp + (esp & esp_mask), ldl(ssp + (ebp & esp_mask)));
        }
        esp -= 4;
        stl(ssp + (esp & esp_mask), t1);
    } else {
        /* 16 bit */
        esp -= 2;
        while (--level) {
            esp -= 2;
            ebp -= 2;
            stw(ssp + (esp & esp_mask), lduw(ssp + (ebp & esp_mask)));
        }
        esp -= 2;
        stw(ssp + (esp & esp_mask), t1);
    }
}

#ifdef TARGET_X86_64
void helper_enter64_level(int level, int data64, target_ulong t1)
{
    target_ulong esp, ebp;

    ebp = EBP;
    esp = ESP;

    if (data64) {
        /* 64 bit */
        esp -= 8;
        while (--level) {
            esp -= 8;
            ebp -= 8;
            stq(esp, ldq(ebp));
        }
        esp -= 8;
        stq(esp, t1);
    } else {
        /* 16 bit */
        esp -= 2;
        while (--level) {
            esp -= 2;
            ebp -= 2;
            stw(esp, lduw(ebp));
        }
        esp -= 2;
        stw(esp, t1);
    }
}
#endif

void helper_lldt(int selector)
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
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
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
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        ptr = dt->base + index;
        e1 = ldl_kernel(ptr);
        e2 = ldl_kernel(ptr + 4);
        if ((e2 & DESC_S_MASK) || ((e2 >> DESC_TYPE_SHIFT) & 0xf) != 2) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0B_NOSEG, selector & 0xfffc);
        }
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint32_t e3;

            e3 = ldl_kernel(ptr + 8);
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

void helper_ltr(int selector)
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
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
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
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        ptr = dt->base + index;
        e1 = ldl_kernel(ptr);
        e2 = ldl_kernel(ptr + 4);
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        if ((e2 & DESC_S_MASK) ||
            (type != 1 && type != 9)) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0B_NOSEG, selector & 0xfffc);
        }
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint32_t e3, e4;

            e3 = ldl_kernel(ptr + 8);
            e4 = ldl_kernel(ptr + 12);
            if ((e4 >> DESC_TYPE_SHIFT) & 0xf) {
                raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
            }
            load_seg_cache_raw_dt(&env->tr, e1, e2);
            env->tr.base |= (target_ulong)e3 << 32;
        } else
#endif
        {
            load_seg_cache_raw_dt(&env->tr, e1, e2);
        }
        e2 |= DESC_TSS_BUSY_MASK;
        stl_kernel(ptr + 4, e2);
    }
    env->tr.selector = selector;
}

/* only works if protected mode and not VM86. seg_reg must be != R_CS */
void helper_load_seg(int seg_reg, int selector)
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
            raise_exception_err(env, EXCP0D_GPF, 0);
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
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        ptr = dt->base + index;
        e1 = ldl_kernel(ptr);
        e2 = ldl_kernel(ptr + 4);

        if (!(e2 & DESC_S_MASK)) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        rpl = selector & 3;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (seg_reg == R_SS) {
            /* must be writable segment */
            if ((e2 & DESC_CS_MASK) || !(e2 & DESC_W_MASK)) {
                raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
            }
            if (rpl != cpl || dpl != cpl) {
                raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
            }
        } else {
            /* must be readable segment */
            if ((e2 & (DESC_CS_MASK | DESC_R_MASK)) == DESC_CS_MASK) {
                raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
            }

            if (!(e2 & DESC_CS_MASK) || !(e2 & DESC_C_MASK)) {
                /* if not conforming code, test rights */
                if (dpl < cpl || dpl < rpl) {
                    raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
                }
            }
        }

        if (!(e2 & DESC_P_MASK)) {
            if (seg_reg == R_SS) {
                raise_exception_err(env, EXCP0C_STACK, selector & 0xfffc);
            } else {
                raise_exception_err(env, EXCP0B_NOSEG, selector & 0xfffc);
            }
        }

        /* set the access bit if not already set */
        if (!(e2 & DESC_A_MASK)) {
            e2 |= DESC_A_MASK;
            stl_kernel(ptr + 4, e2);
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
void helper_ljmp_protected(int new_cs, target_ulong new_eip,
                           int next_eip_addend)
{
    int gate_cs, type;
    uint32_t e1, e2, cpl, dpl, rpl, limit;
    target_ulong next_eip;

    if ((new_cs & 0xfffc) == 0) {
        raise_exception_err(env, EXCP0D_GPF, 0);
    }
    if (load_segment(&e1, &e2, new_cs) != 0) {
        raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
    }
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK)) {
            raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
        }
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_C_MASK) {
            /* conforming code segment */
            if (dpl > cpl) {
                raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            }
        } else {
            /* non conforming code segment */
            rpl = new_cs & 3;
            if (rpl > cpl) {
                raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            }
            if (dpl != cpl) {
                raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            }
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0B_NOSEG, new_cs & 0xfffc);
        }
        limit = get_seg_limit(e1, e2);
        if (new_eip > limit &&
            !(env->hflags & HF_LMA_MASK) && !(e2 & DESC_L_MASK)) {
            raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
        }
        cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                       get_seg_base(e1, e2), limit, e2);
        EIP = new_eip;
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
                raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            }
            next_eip = env->eip + next_eip_addend;
            switch_tss(new_cs, e1, e2, SWITCH_TSS_JMP, next_eip);
            CC_OP = CC_OP_EFLAGS;
            break;
        case 4: /* 286 call gate */
        case 12: /* 386 call gate */
            if ((dpl < cpl) || (dpl < rpl)) {
                raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            }
            if (!(e2 & DESC_P_MASK)) {
                raise_exception_err(env, EXCP0B_NOSEG, new_cs & 0xfffc);
            }
            gate_cs = e1 >> 16;
            new_eip = (e1 & 0xffff);
            if (type == 12) {
                new_eip |= (e2 & 0xffff0000);
            }
            if (load_segment(&e1, &e2, gate_cs) != 0) {
                raise_exception_err(env, EXCP0D_GPF, gate_cs & 0xfffc);
            }
            dpl = (e2 >> DESC_DPL_SHIFT) & 3;
            /* must be code segment */
            if (((e2 & (DESC_S_MASK | DESC_CS_MASK)) !=
                 (DESC_S_MASK | DESC_CS_MASK))) {
                raise_exception_err(env, EXCP0D_GPF, gate_cs & 0xfffc);
            }
            if (((e2 & DESC_C_MASK) && (dpl > cpl)) ||
                (!(e2 & DESC_C_MASK) && (dpl != cpl))) {
                raise_exception_err(env, EXCP0D_GPF, gate_cs & 0xfffc);
            }
            if (!(e2 & DESC_P_MASK)) {
                raise_exception_err(env, EXCP0D_GPF, gate_cs & 0xfffc);
            }
            limit = get_seg_limit(e1, e2);
            if (new_eip > limit) {
                raise_exception_err(env, EXCP0D_GPF, 0);
            }
            cpu_x86_load_seg_cache(env, R_CS, (gate_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2), limit, e2);
            EIP = new_eip;
            break;
        default:
            raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            break;
        }
    }
}

/* real mode call */
void helper_lcall_real(int new_cs, target_ulong new_eip1,
                       int shift, int next_eip)
{
    int new_eip;
    uint32_t esp, esp_mask;
    target_ulong ssp;

    new_eip = new_eip1;
    esp = ESP;
    esp_mask = get_sp_mask(env->segs[R_SS].flags);
    ssp = env->segs[R_SS].base;
    if (shift) {
        PUSHL(ssp, esp, esp_mask, env->segs[R_CS].selector);
        PUSHL(ssp, esp, esp_mask, next_eip);
    } else {
        PUSHW(ssp, esp, esp_mask, env->segs[R_CS].selector);
        PUSHW(ssp, esp, esp_mask, next_eip);
    }

    SET_ESP(esp, esp_mask);
    env->eip = new_eip;
    env->segs[R_CS].selector = new_cs;
    env->segs[R_CS].base = (new_cs << 4);
}

/* protected mode call */
void helper_lcall_protected(int new_cs, target_ulong new_eip,
                            int shift, int next_eip_addend)
{
    int new_stack, i;
    uint32_t e1, e2, cpl, dpl, rpl, selector, offset, param_count;
    uint32_t ss = 0, ss_e1 = 0, ss_e2 = 0, sp, type, ss_dpl, sp_mask;
    uint32_t val, limit, old_sp_mask;
    target_ulong ssp, old_ssp, next_eip;

    next_eip = env->eip + next_eip_addend;
    LOG_PCALL("lcall %04x:%08x s=%d\n", new_cs, (uint32_t)new_eip, shift);
    LOG_PCALL_STATE(env);
    if ((new_cs & 0xfffc) == 0) {
        raise_exception_err(env, EXCP0D_GPF, 0);
    }
    if (load_segment(&e1, &e2, new_cs) != 0) {
        raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
    }
    cpl = env->hflags & HF_CPL_MASK;
    LOG_PCALL("desc=%08x:%08x\n", e1, e2);
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK)) {
            raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
        }
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_C_MASK) {
            /* conforming code segment */
            if (dpl > cpl) {
                raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            }
        } else {
            /* non conforming code segment */
            rpl = new_cs & 3;
            if (rpl > cpl) {
                raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            }
            if (dpl != cpl) {
                raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            }
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0B_NOSEG, new_cs & 0xfffc);
        }

#ifdef TARGET_X86_64
        /* XXX: check 16/32 bit cases in long mode */
        if (shift == 2) {
            target_ulong rsp;

            /* 64 bit case */
            rsp = ESP;
            PUSHQ(rsp, env->segs[R_CS].selector);
            PUSHQ(rsp, next_eip);
            /* from this point, not restartable */
            ESP = rsp;
            cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2),
                                   get_seg_limit(e1, e2), e2);
            EIP = new_eip;
        } else
#endif
        {
            sp = ESP;
            sp_mask = get_sp_mask(env->segs[R_SS].flags);
            ssp = env->segs[R_SS].base;
            if (shift) {
                PUSHL(ssp, sp, sp_mask, env->segs[R_CS].selector);
                PUSHL(ssp, sp, sp_mask, next_eip);
            } else {
                PUSHW(ssp, sp, sp_mask, env->segs[R_CS].selector);
                PUSHW(ssp, sp, sp_mask, next_eip);
            }

            limit = get_seg_limit(e1, e2);
            if (new_eip > limit) {
                raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            }
            /* from this point, not restartable */
            SET_ESP(sp, sp_mask);
            cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2), limit, e2);
            EIP = new_eip;
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
                raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            }
            switch_tss(new_cs, e1, e2, SWITCH_TSS_CALL, next_eip);
            CC_OP = CC_OP_EFLAGS;
            return;
        case 4: /* 286 call gate */
        case 12: /* 386 call gate */
            break;
        default:
            raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
            break;
        }
        shift = type >> 3;

        if (dpl < cpl || dpl < rpl) {
            raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
        }
        /* check valid bit */
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0B_NOSEG,  new_cs & 0xfffc);
        }
        selector = e1 >> 16;
        offset = (e2 & 0xffff0000) | (e1 & 0x0000ffff);
        param_count = e2 & 0x1f;
        if ((selector & 0xfffc) == 0) {
            raise_exception_err(env, EXCP0D_GPF, 0);
        }

        if (load_segment(&e1, &e2, selector) != 0) {
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
            get_ss_esp_from_tss(&ss, &sp, dpl);
            LOG_PCALL("new ss:esp=%04x:%08x param_count=%d ESP=" TARGET_FMT_lx
                      "\n",
                      ss, sp, param_count, ESP);
            if ((ss & 0xfffc) == 0) {
                raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
            }
            if ((ss & 3) != dpl) {
                raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
            }
            if (load_segment(&ss_e1, &ss_e2, ss) != 0) {
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

            /* push_size = ((param_count * 2) + 8) << shift; */

            old_sp_mask = get_sp_mask(env->segs[R_SS].flags);
            old_ssp = env->segs[R_SS].base;

            sp_mask = get_sp_mask(ss_e2);
            ssp = get_seg_base(ss_e1, ss_e2);
            if (shift) {
                PUSHL(ssp, sp, sp_mask, env->segs[R_SS].selector);
                PUSHL(ssp, sp, sp_mask, ESP);
                for (i = param_count - 1; i >= 0; i--) {
                    val = ldl_kernel(old_ssp + ((ESP + i * 4) & old_sp_mask));
                    PUSHL(ssp, sp, sp_mask, val);
                }
            } else {
                PUSHW(ssp, sp, sp_mask, env->segs[R_SS].selector);
                PUSHW(ssp, sp, sp_mask, ESP);
                for (i = param_count - 1; i >= 0; i--) {
                    val = lduw_kernel(old_ssp + ((ESP + i * 2) & old_sp_mask));
                    PUSHW(ssp, sp, sp_mask, val);
                }
            }
            new_stack = 1;
        } else {
            /* to same privilege */
            sp = ESP;
            sp_mask = get_sp_mask(env->segs[R_SS].flags);
            ssp = env->segs[R_SS].base;
            /* push_size = (4 << shift); */
            new_stack = 0;
        }

        if (shift) {
            PUSHL(ssp, sp, sp_mask, env->segs[R_CS].selector);
            PUSHL(ssp, sp, sp_mask, next_eip);
        } else {
            PUSHW(ssp, sp, sp_mask, env->segs[R_CS].selector);
            PUSHW(ssp, sp, sp_mask, next_eip);
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
        cpu_x86_set_cpl(env, dpl);
        SET_ESP(sp, sp_mask);
        EIP = offset;
    }
}

/* real and vm86 mode iret */
void helper_iret_real(int shift)
{
    uint32_t sp, new_cs, new_eip, new_eflags, sp_mask;
    target_ulong ssp;
    int eflags_mask;

    sp_mask = 0xffff; /* XXXX: use SS segment size? */
    sp = ESP;
    ssp = env->segs[R_SS].base;
    if (shift == 1) {
        /* 32 bits */
        POPL(ssp, sp, sp_mask, new_eip);
        POPL(ssp, sp, sp_mask, new_cs);
        new_cs &= 0xffff;
        POPL(ssp, sp, sp_mask, new_eflags);
    } else {
        /* 16 bits */
        POPW(ssp, sp, sp_mask, new_eip);
        POPW(ssp, sp, sp_mask, new_cs);
        POPW(ssp, sp, sp_mask, new_eflags);
    }
    ESP = (ESP & ~sp_mask) | (sp & sp_mask);
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

static inline void validate_seg(int seg_reg, int cpl)
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
static inline void helper_ret_protected(int shift, int is_iret, int addend)
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
    sp = ESP;
    ssp = env->segs[R_SS].base;
    new_eflags = 0; /* avoid warning */
#ifdef TARGET_X86_64
    if (shift == 2) {
        POPQ(sp, new_eip);
        POPQ(sp, new_cs);
        new_cs &= 0xffff;
        if (is_iret) {
            POPQ(sp, new_eflags);
        }
    } else
#endif
    {
        if (shift == 1) {
            /* 32 bits */
            POPL(ssp, sp, sp_mask, new_eip);
            POPL(ssp, sp, sp_mask, new_cs);
            new_cs &= 0xffff;
            if (is_iret) {
                POPL(ssp, sp, sp_mask, new_eflags);
                if (new_eflags & VM_MASK) {
                    goto return_to_vm86;
                }
            }
        } else {
            /* 16 bits */
            POPW(ssp, sp, sp_mask, new_eip);
            POPW(ssp, sp, sp_mask, new_cs);
            if (is_iret) {
                POPW(ssp, sp, sp_mask, new_eflags);
            }
        }
    }
    LOG_PCALL("lret new %04x:" TARGET_FMT_lx " s=%d addend=0x%x\n",
              new_cs, new_eip, shift, addend);
    LOG_PCALL_STATE(env);
    if ((new_cs & 0xfffc) == 0) {
        raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
    }
    if (load_segment(&e1, &e2, new_cs) != 0) {
        raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
    }
    if (!(e2 & DESC_S_MASK) ||
        !(e2 & DESC_CS_MASK)) {
        raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
    }
    cpl = env->hflags & HF_CPL_MASK;
    rpl = new_cs & 3;
    if (rpl < cpl) {
        raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (e2 & DESC_C_MASK) {
        if (dpl > rpl) {
            raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
        }
    } else {
        if (dpl != rpl) {
            raise_exception_err(env, EXCP0D_GPF, new_cs & 0xfffc);
        }
    }
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err(env, EXCP0B_NOSEG, new_cs & 0xfffc);
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
            POPQ(sp, new_esp);
            POPQ(sp, new_ss);
            new_ss &= 0xffff;
        } else
#endif
        {
            if (shift == 1) {
                /* 32 bits */
                POPL(ssp, sp, sp_mask, new_esp);
                POPL(ssp, sp, sp_mask, new_ss);
                new_ss &= 0xffff;
            } else {
                /* 16 bits */
                POPW(ssp, sp, sp_mask, new_esp);
                POPW(ssp, sp, sp_mask, new_ss);
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
                raise_exception_err(env, EXCP0D_GPF, 0);
            }
        } else {
            if ((new_ss & 3) != rpl) {
                raise_exception_err(env, EXCP0D_GPF, new_ss & 0xfffc);
            }
            if (load_segment(&ss_e1, &ss_e2, new_ss) != 0) {
                raise_exception_err(env, EXCP0D_GPF, new_ss & 0xfffc);
            }
            if (!(ss_e2 & DESC_S_MASK) ||
                (ss_e2 & DESC_CS_MASK) ||
                !(ss_e2 & DESC_W_MASK)) {
                raise_exception_err(env, EXCP0D_GPF, new_ss & 0xfffc);
            }
            dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
            if (dpl != rpl) {
                raise_exception_err(env, EXCP0D_GPF, new_ss & 0xfffc);
            }
            if (!(ss_e2 & DESC_P_MASK)) {
                raise_exception_err(env, EXCP0B_NOSEG, new_ss & 0xfffc);
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
        cpu_x86_set_cpl(env, rpl);
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
        validate_seg(R_ES, rpl);
        validate_seg(R_DS, rpl);
        validate_seg(R_FS, rpl);
        validate_seg(R_GS, rpl);

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
    POPL(ssp, sp, sp_mask, new_esp);
    POPL(ssp, sp, sp_mask, new_ss);
    POPL(ssp, sp, sp_mask, new_es);
    POPL(ssp, sp, sp_mask, new_ds);
    POPL(ssp, sp, sp_mask, new_fs);
    POPL(ssp, sp, sp_mask, new_gs);

    /* modify processor state */
    cpu_load_eflags(env, new_eflags, TF_MASK | AC_MASK | ID_MASK |
                    IF_MASK | IOPL_MASK | VM_MASK | NT_MASK | VIF_MASK |
                    VIP_MASK);
    load_seg_vm(R_CS, new_cs & 0xffff);
    cpu_x86_set_cpl(env, 3);
    load_seg_vm(R_SS, new_ss & 0xffff);
    load_seg_vm(R_ES, new_es & 0xffff);
    load_seg_vm(R_DS, new_ds & 0xffff);
    load_seg_vm(R_FS, new_fs & 0xffff);
    load_seg_vm(R_GS, new_gs & 0xffff);

    env->eip = new_eip & 0xffff;
    ESP = new_esp;
}

void helper_iret_protected(int shift, int next_eip)
{
    int tss_selector, type;
    uint32_t e1, e2;

    /* specific case for TSS */
    if (env->eflags & NT_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            raise_exception_err(env, EXCP0D_GPF, 0);
        }
#endif
        tss_selector = lduw_kernel(env->tr.base + 0);
        if (tss_selector & 4) {
            raise_exception_err(env, EXCP0A_TSS, tss_selector & 0xfffc);
        }
        if (load_segment(&e1, &e2, tss_selector) != 0) {
            raise_exception_err(env, EXCP0A_TSS, tss_selector & 0xfffc);
        }
        type = (e2 >> DESC_TYPE_SHIFT) & 0x17;
        /* NOTE: we check both segment and busy TSS */
        if (type != 3) {
            raise_exception_err(env, EXCP0A_TSS, tss_selector & 0xfffc);
        }
        switch_tss(tss_selector, e1, e2, SWITCH_TSS_IRET, next_eip);
    } else {
        helper_ret_protected(shift, 1, 0);
    }
    env->hflags2 &= ~HF2_NMI_MASK;
}

void helper_lret_protected(int shift, int addend)
{
    helper_ret_protected(shift, 0, addend);
}

void helper_sysenter(void)
{
    if (env->sysenter_cs == 0) {
        raise_exception_err(env, EXCP0D_GPF, 0);
    }
    env->eflags &= ~(VM_MASK | IF_MASK | RF_MASK);
    cpu_x86_set_cpl(env, 0);

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
    ESP = env->sysenter_esp;
    EIP = env->sysenter_eip;
}

void helper_sysexit(int dflag)
{
    int cpl;

    cpl = env->hflags & HF_CPL_MASK;
    if (env->sysenter_cs == 0 || cpl != 0) {
        raise_exception_err(env, EXCP0D_GPF, 0);
    }
    cpu_x86_set_cpl(env, 3);
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
    ESP = ECX;
    EIP = EDX;
}

#if defined(CONFIG_USER_ONLY)
target_ulong helper_read_crN(int reg)
{
    return 0;
}

void helper_write_crN(int reg, target_ulong t0)
{
}

void helper_movl_drN_T0(int reg, target_ulong t0)
{
}
#else
target_ulong helper_read_crN(int reg)
{
    target_ulong val;

    helper_svm_check_intercept_param(SVM_EXIT_READ_CR0 + reg, 0);
    switch (reg) {
    default:
        val = env->cr[reg];
        break;
    case 8:
        if (!(env->hflags2 & HF2_VINTR_MASK)) {
            val = cpu_get_apic_tpr(env->apic_state);
        } else {
            val = env->v_tpr;
        }
        break;
    }
    return val;
}

void helper_write_crN(int reg, target_ulong t0)
{
    helper_svm_check_intercept_param(SVM_EXIT_WRITE_CR0 + reg, 0);
    switch (reg) {
    case 0:
        cpu_x86_update_cr0(env, t0);
        break;
    case 3:
        cpu_x86_update_cr3(env, t0);
        break;
    case 4:
        cpu_x86_update_cr4(env, t0);
        break;
    case 8:
        if (!(env->hflags2 & HF2_VINTR_MASK)) {
            cpu_set_apic_tpr(env->apic_state, t0);
        }
        env->v_tpr = t0 & 0x0f;
        break;
    default:
        env->cr[reg] = t0;
        break;
    }
}

void helper_movl_drN_T0(int reg, target_ulong t0)
{
    int i;

    if (reg < 4) {
        hw_breakpoint_remove(env, reg);
        env->dr[reg] = t0;
        hw_breakpoint_insert(env, reg);
    } else if (reg == 7) {
        for (i = 0; i < 4; i++) {
            hw_breakpoint_remove(env, i);
        }
        env->dr[7] = t0;
        for (i = 0; i < 4; i++) {
            hw_breakpoint_insert(env, i);
        }
    } else {
        env->dr[reg] = t0;
    }
}
#endif

void helper_lmsw(target_ulong t0)
{
    /* only 4 lower bits of CR0 are modified. PE cannot be set to zero
       if already set to one. */
    t0 = (env->cr[0] & ~0xe) | (t0 & 0xf);
    helper_write_crN(0, t0);
}

void helper_invlpg(target_ulong addr)
{
    helper_svm_check_intercept_param(SVM_EXIT_INVLPG, 0);
    tlb_flush_page(env, addr);
}

void helper_rdtsc(void)
{
    uint64_t val;

    if ((env->cr[4] & CR4_TSD_MASK) && ((env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception(env, EXCP0D_GPF);
    }
    helper_svm_check_intercept_param(SVM_EXIT_RDTSC, 0);

    val = cpu_get_tsc(env) + env->tsc_offset;
    EAX = (uint32_t)(val);
    EDX = (uint32_t)(val >> 32);
}

void helper_rdtscp(void)
{
    helper_rdtsc();
    ECX = (uint32_t)(env->tsc_aux);
}

void helper_rdpmc(void)
{
    if ((env->cr[4] & CR4_PCE_MASK) && ((env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception(env, EXCP0D_GPF);
    }
    helper_svm_check_intercept_param(SVM_EXIT_RDPMC, 0);

    /* currently unimplemented */
    qemu_log_mask(LOG_UNIMP, "x86: unimplemented rdpmc\n");
    raise_exception_err(env, EXCP06_ILLOP, 0);
}

#if defined(CONFIG_USER_ONLY)
void helper_wrmsr(void)
{
}

void helper_rdmsr(void)
{
}
#else
void helper_wrmsr(void)
{
    uint64_t val;

    helper_svm_check_intercept_param(SVM_EXIT_MSR, 1);

    val = ((uint32_t)EAX) | ((uint64_t)((uint32_t)EDX) << 32);

    switch ((uint32_t)ECX) {
    case MSR_IA32_SYSENTER_CS:
        env->sysenter_cs = val & 0xffff;
        break;
    case MSR_IA32_SYSENTER_ESP:
        env->sysenter_esp = val;
        break;
    case MSR_IA32_SYSENTER_EIP:
        env->sysenter_eip = val;
        break;
    case MSR_IA32_APICBASE:
        cpu_set_apic_base(env->apic_state, val);
        break;
    case MSR_EFER:
        {
            uint64_t update_mask;

            update_mask = 0;
            if (env->cpuid_ext2_features & CPUID_EXT2_SYSCALL) {
                update_mask |= MSR_EFER_SCE;
            }
            if (env->cpuid_ext2_features & CPUID_EXT2_LM) {
                update_mask |= MSR_EFER_LME;
            }
            if (env->cpuid_ext2_features & CPUID_EXT2_FFXSR) {
                update_mask |= MSR_EFER_FFXSR;
            }
            if (env->cpuid_ext2_features & CPUID_EXT2_NX) {
                update_mask |= MSR_EFER_NXE;
            }
            if (env->cpuid_ext3_features & CPUID_EXT3_SVM) {
                update_mask |= MSR_EFER_SVME;
            }
            if (env->cpuid_ext2_features & CPUID_EXT2_FFXSR) {
                update_mask |= MSR_EFER_FFXSR;
            }
            cpu_load_efer(env, (env->efer & ~update_mask) |
                          (val & update_mask));
        }
        break;
    case MSR_STAR:
        env->star = val;
        break;
    case MSR_PAT:
        env->pat = val;
        break;
    case MSR_VM_HSAVE_PA:
        env->vm_hsave = val;
        break;
#ifdef TARGET_X86_64
    case MSR_LSTAR:
        env->lstar = val;
        break;
    case MSR_CSTAR:
        env->cstar = val;
        break;
    case MSR_FMASK:
        env->fmask = val;
        break;
    case MSR_FSBASE:
        env->segs[R_FS].base = val;
        break;
    case MSR_GSBASE:
        env->segs[R_GS].base = val;
        break;
    case MSR_KERNELGSBASE:
        env->kernelgsbase = val;
        break;
#endif
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        env->mtrr_var[((uint32_t)ECX - MSR_MTRRphysBase(0)) / 2].base = val;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        env->mtrr_var[((uint32_t)ECX - MSR_MTRRphysMask(0)) / 2].mask = val;
        break;
    case MSR_MTRRfix64K_00000:
        env->mtrr_fixed[(uint32_t)ECX - MSR_MTRRfix64K_00000] = val;
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        env->mtrr_fixed[(uint32_t)ECX - MSR_MTRRfix16K_80000 + 1] = val;
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        env->mtrr_fixed[(uint32_t)ECX - MSR_MTRRfix4K_C0000 + 3] = val;
        break;
    case MSR_MTRRdefType:
        env->mtrr_deftype = val;
        break;
    case MSR_MCG_STATUS:
        env->mcg_status = val;
        break;
    case MSR_MCG_CTL:
        if ((env->mcg_cap & MCG_CTL_P)
            && (val == 0 || val == ~(uint64_t)0)) {
            env->mcg_ctl = val;
        }
        break;
    case MSR_TSC_AUX:
        env->tsc_aux = val;
        break;
    case MSR_IA32_MISC_ENABLE:
        env->msr_ia32_misc_enable = val;
        break;
    default:
        if ((uint32_t)ECX >= MSR_MC0_CTL
            && (uint32_t)ECX < MSR_MC0_CTL + (4 * env->mcg_cap & 0xff)) {
            uint32_t offset = (uint32_t)ECX - MSR_MC0_CTL;
            if ((offset & 0x3) != 0
                || (val == 0 || val == ~(uint64_t)0)) {
                env->mce_banks[offset] = val;
            }
            break;
        }
        /* XXX: exception? */
        break;
    }
}

void helper_rdmsr(void)
{
    uint64_t val;

    helper_svm_check_intercept_param(SVM_EXIT_MSR, 0);

    switch ((uint32_t)ECX) {
    case MSR_IA32_SYSENTER_CS:
        val = env->sysenter_cs;
        break;
    case MSR_IA32_SYSENTER_ESP:
        val = env->sysenter_esp;
        break;
    case MSR_IA32_SYSENTER_EIP:
        val = env->sysenter_eip;
        break;
    case MSR_IA32_APICBASE:
        val = cpu_get_apic_base(env->apic_state);
        break;
    case MSR_EFER:
        val = env->efer;
        break;
    case MSR_STAR:
        val = env->star;
        break;
    case MSR_PAT:
        val = env->pat;
        break;
    case MSR_VM_HSAVE_PA:
        val = env->vm_hsave;
        break;
    case MSR_IA32_PERF_STATUS:
        /* tsc_increment_by_tick */
        val = 1000ULL;
        /* CPU multiplier */
        val |= (((uint64_t)4ULL) << 40);
        break;
#ifdef TARGET_X86_64
    case MSR_LSTAR:
        val = env->lstar;
        break;
    case MSR_CSTAR:
        val = env->cstar;
        break;
    case MSR_FMASK:
        val = env->fmask;
        break;
    case MSR_FSBASE:
        val = env->segs[R_FS].base;
        break;
    case MSR_GSBASE:
        val = env->segs[R_GS].base;
        break;
    case MSR_KERNELGSBASE:
        val = env->kernelgsbase;
        break;
    case MSR_TSC_AUX:
        val = env->tsc_aux;
        break;
#endif
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        val = env->mtrr_var[((uint32_t)ECX - MSR_MTRRphysBase(0)) / 2].base;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        val = env->mtrr_var[((uint32_t)ECX - MSR_MTRRphysMask(0)) / 2].mask;
        break;
    case MSR_MTRRfix64K_00000:
        val = env->mtrr_fixed[0];
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        val = env->mtrr_fixed[(uint32_t)ECX - MSR_MTRRfix16K_80000 + 1];
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        val = env->mtrr_fixed[(uint32_t)ECX - MSR_MTRRfix4K_C0000 + 3];
        break;
    case MSR_MTRRdefType:
        val = env->mtrr_deftype;
        break;
    case MSR_MTRRcap:
        if (env->cpuid_features & CPUID_MTRR) {
            val = MSR_MTRRcap_VCNT | MSR_MTRRcap_FIXRANGE_SUPPORT |
                MSR_MTRRcap_WC_SUPPORTED;
        } else {
            /* XXX: exception? */
            val = 0;
        }
        break;
    case MSR_MCG_CAP:
        val = env->mcg_cap;
        break;
    case MSR_MCG_CTL:
        if (env->mcg_cap & MCG_CTL_P) {
            val = env->mcg_ctl;
        } else {
            val = 0;
        }
        break;
    case MSR_MCG_STATUS:
        val = env->mcg_status;
        break;
    case MSR_IA32_MISC_ENABLE:
        val = env->msr_ia32_misc_enable;
        break;
    default:
        if ((uint32_t)ECX >= MSR_MC0_CTL
            && (uint32_t)ECX < MSR_MC0_CTL + (4 * env->mcg_cap & 0xff)) {
            uint32_t offset = (uint32_t)ECX - MSR_MC0_CTL;
            val = env->mce_banks[offset];
            break;
        }
        /* XXX: exception? */
        val = 0;
        break;
    }
    EAX = (uint32_t)(val);
    EDX = (uint32_t)(val >> 32);
}
#endif

target_ulong helper_lsl(target_ulong selector1)
{
    unsigned int limit;
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl, type;

    selector = selector1 & 0xffff;
    eflags = helper_cc_compute_all(CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment(&e1, &e2, selector) != 0) {
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

target_ulong helper_lar(target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl, type;

    selector = selector1 & 0xffff;
    eflags = helper_cc_compute_all(CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment(&e1, &e2, selector) != 0) {
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

void helper_verr(target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl;

    selector = selector1 & 0xffff;
    eflags = helper_cc_compute_all(CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment(&e1, &e2, selector) != 0) {
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

void helper_verw(target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl;

    selector = selector1 & 0xffff;
    eflags = helper_cc_compute_all(CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment(&e1, &e2, selector) != 0) {
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
void cpu_x86_load_seg(CPUX86State *s, int seg_reg, int selector)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = s;
    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK)) {
        selector &= 0xffff;
        cpu_x86_load_seg_cache(env, seg_reg, selector,
                               (selector << 4), 0xffff, 0);
    } else {
        helper_load_seg(seg_reg, selector);
    }
    env = saved_env;
}
#endif

static void do_hlt(void)
{
    env->hflags &= ~HF_INHIBIT_IRQ_MASK; /* needed if sti is just before */
    env->halted = 1;
    env->exception_index = EXCP_HLT;
    cpu_loop_exit(env);
}

void helper_hlt(int next_eip_addend)
{
    helper_svm_check_intercept_param(SVM_EXIT_HLT, 0);
    EIP += next_eip_addend;

    do_hlt();
}

void helper_monitor(target_ulong ptr)
{
    if ((uint32_t)ECX != 0) {
        raise_exception(env, EXCP0D_GPF);
    }
    /* XXX: store address? */
    helper_svm_check_intercept_param(SVM_EXIT_MONITOR, 0);
}

void helper_mwait(int next_eip_addend)
{
    if ((uint32_t)ECX != 0) {
        raise_exception(env, EXCP0D_GPF);
    }
    helper_svm_check_intercept_param(SVM_EXIT_MWAIT, 0);
    EIP += next_eip_addend;

    /* XXX: not complete but not completely erroneous */
    if (env->cpu_index != 0 || env->next_cpu != NULL) {
        /* more than one CPU: do not sleep because another CPU may
           wake this one */
    } else {
        do_hlt();
    }
}

void helper_debug(void)
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit(env);
}

void helper_boundw(target_ulong a0, int v)
{
    int low, high;

    low = ldsw(a0);
    high = ldsw(a0 + 2);
    v = (int16_t)v;
    if (v < low || v > high) {
        raise_exception(env, EXCP05_BOUND);
    }
}

void helper_boundl(target_ulong a0, int v)
{
    int low, high;

    low = ldl(a0);
    high = ldl(a0 + 4);
    if (v < low || v > high) {
        raise_exception(env, EXCP05_BOUND);
    }
}

#if !defined(CONFIG_USER_ONLY)

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

#endif

#if !defined(CONFIG_USER_ONLY)
/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUX86State *env1, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    TranslationBlock *tb;
    int ret;
    CPUX86State *saved_env;

    saved_env = env;
    env = env1;

    ret = cpu_x86_handle_mmu_fault(env, addr, is_write, mmu_idx);
    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault */
            tb = tb_find_pc(retaddr);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, retaddr);
            }
        }
        raise_exception_err(env, env->exception_index, env->error_code);
    }
    env = saved_env;
}
#endif

/* Secure Virtual Machine helpers */

#if defined(CONFIG_USER_ONLY)

void helper_vmrun(int aflag, int next_eip_addend)
{
}

void helper_vmmcall(void)
{
}

void helper_vmload(int aflag)
{
}

void helper_vmsave(int aflag)
{
}

void helper_stgi(void)
{
}

void helper_clgi(void)
{
}

void helper_skinit(void)
{
}

void helper_invlpga(int aflag)
{
}

void helper_vmexit(uint32_t exit_code, uint64_t exit_info_1)
{
}

void cpu_vmexit(CPUX86State *nenv, uint32_t exit_code, uint64_t exit_info_1)
{
}

void helper_svm_check_intercept_param(uint32_t type, uint64_t param)
{
}

void cpu_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                   uint64_t param)
{
}

void helper_svm_check_io(uint32_t port, uint32_t param,
                         uint32_t next_eip_addend)
{
}
#else

static inline void svm_save_seg(target_phys_addr_t addr,
                                const SegmentCache *sc)
{
    stw_phys(addr + offsetof(struct vmcb_seg, selector),
             sc->selector);
    stq_phys(addr + offsetof(struct vmcb_seg, base),
             sc->base);
    stl_phys(addr + offsetof(struct vmcb_seg, limit),
             sc->limit);
    stw_phys(addr + offsetof(struct vmcb_seg, attrib),
             ((sc->flags >> 8) & 0xff) | ((sc->flags >> 12) & 0x0f00));
}

static inline void svm_load_seg(target_phys_addr_t addr, SegmentCache *sc)
{
    unsigned int flags;

    sc->selector = lduw_phys(addr + offsetof(struct vmcb_seg, selector));
    sc->base = ldq_phys(addr + offsetof(struct vmcb_seg, base));
    sc->limit = ldl_phys(addr + offsetof(struct vmcb_seg, limit));
    flags = lduw_phys(addr + offsetof(struct vmcb_seg, attrib));
    sc->flags = ((flags & 0xff) << 8) | ((flags & 0x0f00) << 12);
}

static inline void svm_load_seg_cache(target_phys_addr_t addr,
                                      CPUX86State *env, int seg_reg)
{
    SegmentCache sc1, *sc = &sc1;

    svm_load_seg(addr, sc);
    cpu_x86_load_seg_cache(env, seg_reg, sc->selector,
                           sc->base, sc->limit, sc->flags);
}

void helper_vmrun(int aflag, int next_eip_addend)
{
    target_ulong addr;
    uint32_t event_inj;
    uint32_t int_ctl;

    helper_svm_check_intercept_param(SVM_EXIT_VMRUN, 0);

    if (aflag == 2) {
        addr = EAX;
    } else {
        addr = (uint32_t)EAX;
    }

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmrun! " TARGET_FMT_lx "\n", addr);

    env->vm_vmcb = addr;

    /* save the current CPU state in the hsave page */
    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.gdtr.base),
             env->gdt.base);
    stl_phys(env->vm_hsave + offsetof(struct vmcb, save.gdtr.limit),
             env->gdt.limit);

    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.idtr.base),
             env->idt.base);
    stl_phys(env->vm_hsave + offsetof(struct vmcb, save.idtr.limit),
             env->idt.limit);

    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.cr0), env->cr[0]);
    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.cr2), env->cr[2]);
    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.cr3), env->cr[3]);
    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.cr4), env->cr[4]);
    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.dr6), env->dr[6]);
    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.dr7), env->dr[7]);

    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.efer), env->efer);
    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.rflags),
             cpu_compute_eflags(env));

    svm_save_seg(env->vm_hsave + offsetof(struct vmcb, save.es),
                 &env->segs[R_ES]);
    svm_save_seg(env->vm_hsave + offsetof(struct vmcb, save.cs),
                 &env->segs[R_CS]);
    svm_save_seg(env->vm_hsave + offsetof(struct vmcb, save.ss),
                 &env->segs[R_SS]);
    svm_save_seg(env->vm_hsave + offsetof(struct vmcb, save.ds),
                 &env->segs[R_DS]);

    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.rip),
             EIP + next_eip_addend);
    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.rsp), ESP);
    stq_phys(env->vm_hsave + offsetof(struct vmcb, save.rax), EAX);

    /* load the interception bitmaps so we do not need to access the
       vmcb in svm mode */
    env->intercept = ldq_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                      control.intercept));
    env->intercept_cr_read = lduw_phys(env->vm_vmcb +
                                       offsetof(struct vmcb,
                                                control.intercept_cr_read));
    env->intercept_cr_write = lduw_phys(env->vm_vmcb +
                                        offsetof(struct vmcb,
                                                 control.intercept_cr_write));
    env->intercept_dr_read = lduw_phys(env->vm_vmcb +
                                       offsetof(struct vmcb,
                                                control.intercept_dr_read));
    env->intercept_dr_write = lduw_phys(env->vm_vmcb +
                                        offsetof(struct vmcb,
                                                 control.intercept_dr_write));
    env->intercept_exceptions = ldl_phys(env->vm_vmcb +
                                         offsetof(struct vmcb,
                                                  control.intercept_exceptions
                                                  ));

    /* enable intercepts */
    env->hflags |= HF_SVMI_MASK;

    env->tsc_offset = ldq_phys(env->vm_vmcb +
                               offsetof(struct vmcb, control.tsc_offset));

    env->gdt.base  = ldq_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                      save.gdtr.base));
    env->gdt.limit = ldl_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                      save.gdtr.limit));

    env->idt.base  = ldq_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                      save.idtr.base));
    env->idt.limit = ldl_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                      save.idtr.limit));

    /* clear exit_info_2 so we behave like the real hardware */
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2), 0);

    cpu_x86_update_cr0(env, ldq_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                             save.cr0)));
    cpu_x86_update_cr4(env, ldq_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                             save.cr4)));
    cpu_x86_update_cr3(env, ldq_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                             save.cr3)));
    env->cr[2] = ldq_phys(env->vm_vmcb + offsetof(struct vmcb, save.cr2));
    int_ctl = ldl_phys(env->vm_vmcb + offsetof(struct vmcb, control.int_ctl));
    env->hflags2 &= ~(HF2_HIF_MASK | HF2_VINTR_MASK);
    if (int_ctl & V_INTR_MASKING_MASK) {
        env->v_tpr = int_ctl & V_TPR_MASK;
        env->hflags2 |= HF2_VINTR_MASK;
        if (env->eflags & IF_MASK) {
            env->hflags2 |= HF2_HIF_MASK;
        }
    }

    cpu_load_efer(env,
                  ldq_phys(env->vm_vmcb + offsetof(struct vmcb, save.efer)));
    env->eflags = 0;
    cpu_load_eflags(env, ldq_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                          save.rflags)),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    CC_OP = CC_OP_EFLAGS;

    svm_load_seg_cache(env->vm_vmcb + offsetof(struct vmcb, save.es),
                       env, R_ES);
    svm_load_seg_cache(env->vm_vmcb + offsetof(struct vmcb, save.cs),
                       env, R_CS);
    svm_load_seg_cache(env->vm_vmcb + offsetof(struct vmcb, save.ss),
                       env, R_SS);
    svm_load_seg_cache(env->vm_vmcb + offsetof(struct vmcb, save.ds),
                       env, R_DS);

    EIP = ldq_phys(env->vm_vmcb + offsetof(struct vmcb, save.rip));
    env->eip = EIP;
    ESP = ldq_phys(env->vm_vmcb + offsetof(struct vmcb, save.rsp));
    EAX = ldq_phys(env->vm_vmcb + offsetof(struct vmcb, save.rax));
    env->dr[7] = ldq_phys(env->vm_vmcb + offsetof(struct vmcb, save.dr7));
    env->dr[6] = ldq_phys(env->vm_vmcb + offsetof(struct vmcb, save.dr6));
    cpu_x86_set_cpl(env, ldub_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                           save.cpl)));

    /* FIXME: guest state consistency checks */

    switch (ldub_phys(env->vm_vmcb + offsetof(struct vmcb, control.tlb_ctl))) {
    case TLB_CONTROL_DO_NOTHING:
        break;
    case TLB_CONTROL_FLUSH_ALL_ASID:
        /* FIXME: this is not 100% correct but should work for now */
        tlb_flush(env, 1);
        break;
    }

    env->hflags2 |= HF2_GIF_MASK;

    if (int_ctl & V_IRQ_MASK) {
        env->interrupt_request |= CPU_INTERRUPT_VIRQ;
    }

    /* maybe we need to inject an event */
    event_inj = ldl_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                 control.event_inj));
    if (event_inj & SVM_EVTINJ_VALID) {
        uint8_t vector = event_inj & SVM_EVTINJ_VEC_MASK;
        uint16_t valid_err = event_inj & SVM_EVTINJ_VALID_ERR;
        uint32_t event_inj_err = ldl_phys(env->vm_vmcb +
                                          offsetof(struct vmcb,
                                                   control.event_inj_err));

        qemu_log_mask(CPU_LOG_TB_IN_ASM, "Injecting(%#hx): ", valid_err);
        /* FIXME: need to implement valid_err */
        switch (event_inj & SVM_EVTINJ_TYPE_MASK) {
        case SVM_EVTINJ_TYPE_INTR:
            env->exception_index = vector;
            env->error_code = event_inj_err;
            env->exception_is_int = 0;
            env->exception_next_eip = -1;
            qemu_log_mask(CPU_LOG_TB_IN_ASM, "INTR");
            /* XXX: is it always correct? */
            do_interrupt_x86_hardirq(env, vector, 1);
            break;
        case SVM_EVTINJ_TYPE_NMI:
            env->exception_index = EXCP02_NMI;
            env->error_code = event_inj_err;
            env->exception_is_int = 0;
            env->exception_next_eip = EIP;
            qemu_log_mask(CPU_LOG_TB_IN_ASM, "NMI");
            cpu_loop_exit(env);
            break;
        case SVM_EVTINJ_TYPE_EXEPT:
            env->exception_index = vector;
            env->error_code = event_inj_err;
            env->exception_is_int = 0;
            env->exception_next_eip = -1;
            qemu_log_mask(CPU_LOG_TB_IN_ASM, "EXEPT");
            cpu_loop_exit(env);
            break;
        case SVM_EVTINJ_TYPE_SOFT:
            env->exception_index = vector;
            env->error_code = event_inj_err;
            env->exception_is_int = 1;
            env->exception_next_eip = EIP;
            qemu_log_mask(CPU_LOG_TB_IN_ASM, "SOFT");
            cpu_loop_exit(env);
            break;
        }
        qemu_log_mask(CPU_LOG_TB_IN_ASM, " %#x %#x\n", env->exception_index,
                      env->error_code);
    }
}

void helper_vmmcall(void)
{
    helper_svm_check_intercept_param(SVM_EXIT_VMMCALL, 0);
    raise_exception(env, EXCP06_ILLOP);
}

void helper_vmload(int aflag)
{
    target_ulong addr;

    helper_svm_check_intercept_param(SVM_EXIT_VMLOAD, 0);

    if (aflag == 2) {
        addr = EAX;
    } else {
        addr = (uint32_t)EAX;
    }

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmload! " TARGET_FMT_lx
                  "\nFS: %016" PRIx64 " | " TARGET_FMT_lx "\n",
                  addr, ldq_phys(addr + offsetof(struct vmcb, save.fs.base)),
                  env->segs[R_FS].base);

    svm_load_seg_cache(addr + offsetof(struct vmcb, save.fs),
                       env, R_FS);
    svm_load_seg_cache(addr + offsetof(struct vmcb, save.gs),
                       env, R_GS);
    svm_load_seg(addr + offsetof(struct vmcb, save.tr),
                 &env->tr);
    svm_load_seg(addr + offsetof(struct vmcb, save.ldtr),
                 &env->ldt);

#ifdef TARGET_X86_64
    env->kernelgsbase = ldq_phys(addr + offsetof(struct vmcb,
                                                 save.kernel_gs_base));
    env->lstar = ldq_phys(addr + offsetof(struct vmcb, save.lstar));
    env->cstar = ldq_phys(addr + offsetof(struct vmcb, save.cstar));
    env->fmask = ldq_phys(addr + offsetof(struct vmcb, save.sfmask));
#endif
    env->star = ldq_phys(addr + offsetof(struct vmcb, save.star));
    env->sysenter_cs = ldq_phys(addr + offsetof(struct vmcb, save.sysenter_cs));
    env->sysenter_esp = ldq_phys(addr + offsetof(struct vmcb,
                                                 save.sysenter_esp));
    env->sysenter_eip = ldq_phys(addr + offsetof(struct vmcb,
                                                 save.sysenter_eip));
}

void helper_vmsave(int aflag)
{
    target_ulong addr;

    helper_svm_check_intercept_param(SVM_EXIT_VMSAVE, 0);

    if (aflag == 2) {
        addr = EAX;
    } else {
        addr = (uint32_t)EAX;
    }

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmsave! " TARGET_FMT_lx
                  "\nFS: %016" PRIx64 " | " TARGET_FMT_lx "\n",
                  addr, ldq_phys(addr + offsetof(struct vmcb, save.fs.base)),
                  env->segs[R_FS].base);

    svm_save_seg(addr + offsetof(struct vmcb, save.fs),
                 &env->segs[R_FS]);
    svm_save_seg(addr + offsetof(struct vmcb, save.gs),
                 &env->segs[R_GS]);
    svm_save_seg(addr + offsetof(struct vmcb, save.tr),
                 &env->tr);
    svm_save_seg(addr + offsetof(struct vmcb, save.ldtr),
                 &env->ldt);

#ifdef TARGET_X86_64
    stq_phys(addr + offsetof(struct vmcb, save.kernel_gs_base),
             env->kernelgsbase);
    stq_phys(addr + offsetof(struct vmcb, save.lstar), env->lstar);
    stq_phys(addr + offsetof(struct vmcb, save.cstar), env->cstar);
    stq_phys(addr + offsetof(struct vmcb, save.sfmask), env->fmask);
#endif
    stq_phys(addr + offsetof(struct vmcb, save.star), env->star);
    stq_phys(addr + offsetof(struct vmcb, save.sysenter_cs), env->sysenter_cs);
    stq_phys(addr + offsetof(struct vmcb, save.sysenter_esp),
             env->sysenter_esp);
    stq_phys(addr + offsetof(struct vmcb, save.sysenter_eip),
             env->sysenter_eip);
}

void helper_stgi(void)
{
    helper_svm_check_intercept_param(SVM_EXIT_STGI, 0);
    env->hflags2 |= HF2_GIF_MASK;
}

void helper_clgi(void)
{
    helper_svm_check_intercept_param(SVM_EXIT_CLGI, 0);
    env->hflags2 &= ~HF2_GIF_MASK;
}

void helper_skinit(void)
{
    helper_svm_check_intercept_param(SVM_EXIT_SKINIT, 0);
    /* XXX: not implemented */
    raise_exception(env, EXCP06_ILLOP);
}

void helper_invlpga(int aflag)
{
    target_ulong addr;

    helper_svm_check_intercept_param(SVM_EXIT_INVLPGA, 0);

    if (aflag == 2) {
        addr = EAX;
    } else {
        addr = (uint32_t)EAX;
    }

    /* XXX: could use the ASID to see if it is needed to do the
       flush */
    tlb_flush_page(env, addr);
}

void helper_svm_check_intercept_param(uint32_t type, uint64_t param)
{
    if (likely(!(env->hflags & HF_SVMI_MASK))) {
        return;
    }
    switch (type) {
    case SVM_EXIT_READ_CR0 ... SVM_EXIT_READ_CR0 + 8:
        if (env->intercept_cr_read & (1 << (type - SVM_EXIT_READ_CR0))) {
            helper_vmexit(type, param);
        }
        break;
    case SVM_EXIT_WRITE_CR0 ... SVM_EXIT_WRITE_CR0 + 8:
        if (env->intercept_cr_write & (1 << (type - SVM_EXIT_WRITE_CR0))) {
            helper_vmexit(type, param);
        }
        break;
    case SVM_EXIT_READ_DR0 ... SVM_EXIT_READ_DR0 + 7:
        if (env->intercept_dr_read & (1 << (type - SVM_EXIT_READ_DR0))) {
            helper_vmexit(type, param);
        }
        break;
    case SVM_EXIT_WRITE_DR0 ... SVM_EXIT_WRITE_DR0 + 7:
        if (env->intercept_dr_write & (1 << (type - SVM_EXIT_WRITE_DR0))) {
            helper_vmexit(type, param);
        }
        break;
    case SVM_EXIT_EXCP_BASE ... SVM_EXIT_EXCP_BASE + 31:
        if (env->intercept_exceptions & (1 << (type - SVM_EXIT_EXCP_BASE))) {
            helper_vmexit(type, param);
        }
        break;
    case SVM_EXIT_MSR:
        if (env->intercept & (1ULL << (SVM_EXIT_MSR - SVM_EXIT_INTR))) {
            /* FIXME: this should be read in at vmrun (faster this way?) */
            uint64_t addr = ldq_phys(env->vm_vmcb +
                                     offsetof(struct vmcb,
                                              control.msrpm_base_pa));
            uint32_t t0, t1;

            switch ((uint32_t)ECX) {
            case 0 ... 0x1fff:
                t0 = (ECX * 2) % 8;
                t1 = (ECX * 2) / 8;
                break;
            case 0xc0000000 ... 0xc0001fff:
                t0 = (8192 + ECX - 0xc0000000) * 2;
                t1 = (t0 / 8);
                t0 %= 8;
                break;
            case 0xc0010000 ... 0xc0011fff:
                t0 = (16384 + ECX - 0xc0010000) * 2;
                t1 = (t0 / 8);
                t0 %= 8;
                break;
            default:
                helper_vmexit(type, param);
                t0 = 0;
                t1 = 0;
                break;
            }
            if (ldub_phys(addr + t1) & ((1 << param) << t0)) {
                helper_vmexit(type, param);
            }
        }
        break;
    default:
        if (env->intercept & (1ULL << (type - SVM_EXIT_INTR))) {
            helper_vmexit(type, param);
        }
        break;
    }
}

void cpu_svm_check_intercept_param(CPUX86State *env1, uint32_t type,
                                   uint64_t param)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = env1;
    helper_svm_check_intercept_param(type, param);
    env = saved_env;
}

void helper_svm_check_io(uint32_t port, uint32_t param,
                         uint32_t next_eip_addend)
{
    if (env->intercept & (1ULL << (SVM_EXIT_IOIO - SVM_EXIT_INTR))) {
        /* FIXME: this should be read in at vmrun (faster this way?) */
        uint64_t addr = ldq_phys(env->vm_vmcb +
                                 offsetof(struct vmcb, control.iopm_base_pa));
        uint16_t mask = (1 << ((param >> 4) & 7)) - 1;

        if (lduw_phys(addr + port / 8) & (mask << (port & 7))) {
            /* next EIP */
            stq_phys(env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2),
                     env->eip + next_eip_addend);
            helper_vmexit(SVM_EXIT_IOIO, param | (port << 16));
        }
    }
}

/* Note: currently only 32 bits of exit_code are used */
void helper_vmexit(uint32_t exit_code, uint64_t exit_info_1)
{
    uint32_t int_ctl;

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmexit(%08x, %016" PRIx64 ", %016"
                  PRIx64 ", " TARGET_FMT_lx ")!\n",
                  exit_code, exit_info_1,
                  ldq_phys(env->vm_vmcb + offsetof(struct vmcb,
                                                   control.exit_info_2)),
                  EIP);

    if (env->hflags & HF_INHIBIT_IRQ_MASK) {
        stl_phys(env->vm_vmcb + offsetof(struct vmcb, control.int_state),
                 SVM_INTERRUPT_SHADOW_MASK);
        env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    } else {
        stl_phys(env->vm_vmcb + offsetof(struct vmcb, control.int_state), 0);
    }

    /* Save the VM state in the vmcb */
    svm_save_seg(env->vm_vmcb + offsetof(struct vmcb, save.es),
                 &env->segs[R_ES]);
    svm_save_seg(env->vm_vmcb + offsetof(struct vmcb, save.cs),
                 &env->segs[R_CS]);
    svm_save_seg(env->vm_vmcb + offsetof(struct vmcb, save.ss),
                 &env->segs[R_SS]);
    svm_save_seg(env->vm_vmcb + offsetof(struct vmcb, save.ds),
                 &env->segs[R_DS]);

    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.gdtr.base),
             env->gdt.base);
    stl_phys(env->vm_vmcb + offsetof(struct vmcb, save.gdtr.limit),
             env->gdt.limit);

    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.idtr.base),
             env->idt.base);
    stl_phys(env->vm_vmcb + offsetof(struct vmcb, save.idtr.limit),
             env->idt.limit);

    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.efer), env->efer);
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.cr0), env->cr[0]);
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.cr2), env->cr[2]);
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.cr3), env->cr[3]);
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.cr4), env->cr[4]);

    int_ctl = ldl_phys(env->vm_vmcb + offsetof(struct vmcb, control.int_ctl));
    int_ctl &= ~(V_TPR_MASK | V_IRQ_MASK);
    int_ctl |= env->v_tpr & V_TPR_MASK;
    if (env->interrupt_request & CPU_INTERRUPT_VIRQ) {
        int_ctl |= V_IRQ_MASK;
    }
    stl_phys(env->vm_vmcb + offsetof(struct vmcb, control.int_ctl), int_ctl);

    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.rflags),
             cpu_compute_eflags(env));
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.rip), env->eip);
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.rsp), ESP);
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.rax), EAX);
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.dr7), env->dr[7]);
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, save.dr6), env->dr[6]);
    stb_phys(env->vm_vmcb + offsetof(struct vmcb, save.cpl),
             env->hflags & HF_CPL_MASK);

    /* Reload the host state from vm_hsave */
    env->hflags2 &= ~(HF2_HIF_MASK | HF2_VINTR_MASK);
    env->hflags &= ~HF_SVMI_MASK;
    env->intercept = 0;
    env->intercept_exceptions = 0;
    env->interrupt_request &= ~CPU_INTERRUPT_VIRQ;
    env->tsc_offset = 0;

    env->gdt.base  = ldq_phys(env->vm_hsave + offsetof(struct vmcb,
                                                       save.gdtr.base));
    env->gdt.limit = ldl_phys(env->vm_hsave + offsetof(struct vmcb,
                                                       save.gdtr.limit));

    env->idt.base  = ldq_phys(env->vm_hsave + offsetof(struct vmcb,
                                                       save.idtr.base));
    env->idt.limit = ldl_phys(env->vm_hsave + offsetof(struct vmcb,
                                                       save.idtr.limit));

    cpu_x86_update_cr0(env, ldq_phys(env->vm_hsave + offsetof(struct vmcb,
                                                              save.cr0)) |
                       CR0_PE_MASK);
    cpu_x86_update_cr4(env, ldq_phys(env->vm_hsave + offsetof(struct vmcb,
                                                              save.cr4)));
    cpu_x86_update_cr3(env, ldq_phys(env->vm_hsave + offsetof(struct vmcb,
                                                              save.cr3)));
    /* we need to set the efer after the crs so the hidden flags get
       set properly */
    cpu_load_efer(env, ldq_phys(env->vm_hsave + offsetof(struct vmcb,
                                                         save.efer)));
    env->eflags = 0;
    cpu_load_eflags(env, ldq_phys(env->vm_hsave + offsetof(struct vmcb,
                                                           save.rflags)),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    CC_OP = CC_OP_EFLAGS;

    svm_load_seg_cache(env->vm_hsave + offsetof(struct vmcb, save.es),
                       env, R_ES);
    svm_load_seg_cache(env->vm_hsave + offsetof(struct vmcb, save.cs),
                       env, R_CS);
    svm_load_seg_cache(env->vm_hsave + offsetof(struct vmcb, save.ss),
                       env, R_SS);
    svm_load_seg_cache(env->vm_hsave + offsetof(struct vmcb, save.ds),
                       env, R_DS);

    EIP = ldq_phys(env->vm_hsave + offsetof(struct vmcb, save.rip));
    ESP = ldq_phys(env->vm_hsave + offsetof(struct vmcb, save.rsp));
    EAX = ldq_phys(env->vm_hsave + offsetof(struct vmcb, save.rax));

    env->dr[6] = ldq_phys(env->vm_hsave + offsetof(struct vmcb, save.dr6));
    env->dr[7] = ldq_phys(env->vm_hsave + offsetof(struct vmcb, save.dr7));

    /* other setups */
    cpu_x86_set_cpl(env, 0);
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, control.exit_code),
             exit_code);
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, control.exit_info_1),
             exit_info_1);

    stl_phys(env->vm_vmcb + offsetof(struct vmcb, control.exit_int_info),
             ldl_phys(env->vm_vmcb + offsetof(struct vmcb,
                                              control.event_inj)));
    stl_phys(env->vm_vmcb + offsetof(struct vmcb, control.exit_int_info_err),
             ldl_phys(env->vm_vmcb + offsetof(struct vmcb,
                                              control.event_inj_err)));
    stl_phys(env->vm_vmcb + offsetof(struct vmcb, control.event_inj), 0);

    env->hflags2 &= ~HF2_GIF_MASK;
    /* FIXME: Resets the current ASID register to zero (host ASID). */

    /* Clears the V_IRQ and V_INTR_MASKING bits inside the processor. */

    /* Clears the TSC_OFFSET inside the processor. */

    /* If the host is in PAE mode, the processor reloads the host's PDPEs
       from the page table indicated the host's CR3. If the PDPEs contain
       illegal state, the processor causes a shutdown. */

    /* Forces CR0.PE = 1, RFLAGS.VM = 0. */
    env->cr[0] |= CR0_PE_MASK;
    env->eflags &= ~VM_MASK;

    /* Disables all breakpoints in the host DR7 register. */

    /* Checks the reloaded host state for consistency. */

    /* If the host's rIP reloaded by #VMEXIT is outside the limit of the
       host's code segment or non-canonical (in the case of long mode), a
       #GP fault is delivered inside the host. */

    /* remove any pending exception */
    env->exception_index = -1;
    env->error_code = 0;
    env->old_exception = -1;

    cpu_loop_exit(env);
}

void cpu_vmexit(CPUX86State *nenv, uint32_t exit_code, uint64_t exit_info_1)
{
    env = nenv;
    helper_vmexit(exit_code, exit_info_1);
}

#endif
