/*
 *  vm86 linux syscall support
 * 
 *  Copyright (c) 2003 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "qemu.h"

//#define DEBUG_VM86

#define set_flags(X,new,mask) \
((X) = ((X) & ~(mask)) | ((new) & (mask)))

#define SAFE_MASK	(0xDD5)
#define RETURN_MASK	(0xDFF)

static inline int is_revectored(int nr, struct target_revectored_struct *bitmap)
{
    return (tswap32(bitmap->__map[nr >> 5]) >> (nr & 0x1f)) & 1;
}

static inline void vm_putw(uint8_t *segptr, unsigned int reg16, unsigned int val)
{
    *(uint16_t *)(segptr + (reg16 & 0xffff)) = tswap16(val);
}

static inline void vm_putl(uint8_t *segptr, unsigned int reg16, unsigned int val)
{
    *(uint32_t *)(segptr + (reg16 & 0xffff)) = tswap32(val);
}

static inline unsigned int vm_getw(uint8_t *segptr, unsigned int reg16)
{
    return tswap16(*(uint16_t *)(segptr + (reg16 & 0xffff)));
}

static inline unsigned int vm_getl(uint8_t *segptr, unsigned int reg16)
{
    return tswap32(*(uint16_t *)(segptr + (reg16 & 0xffff)));
}

void save_v86_state(CPUX86State *env)
{
    TaskState *ts = env->opaque;

    /* put the VM86 registers in the userspace register structure */
    ts->target_v86->regs.eax = tswap32(env->regs[R_EAX]);
    ts->target_v86->regs.ebx = tswap32(env->regs[R_EBX]);
    ts->target_v86->regs.ecx = tswap32(env->regs[R_ECX]);
    ts->target_v86->regs.edx = tswap32(env->regs[R_EDX]);
    ts->target_v86->regs.esi = tswap32(env->regs[R_ESI]);
    ts->target_v86->regs.edi = tswap32(env->regs[R_EDI]);
    ts->target_v86->regs.ebp = tswap32(env->regs[R_EBP]);
    ts->target_v86->regs.esp = tswap32(env->regs[R_ESP]);
    ts->target_v86->regs.eip = tswap32(env->eip);
    ts->target_v86->regs.cs = tswap16(env->segs[R_CS]);
    ts->target_v86->regs.ss = tswap16(env->segs[R_SS]);
    ts->target_v86->regs.ds = tswap16(env->segs[R_DS]);
    ts->target_v86->regs.es = tswap16(env->segs[R_ES]);
    ts->target_v86->regs.fs = tswap16(env->segs[R_FS]);
    ts->target_v86->regs.gs = tswap16(env->segs[R_GS]);
    set_flags(env->eflags, ts->v86flags, VIF_MASK | ts->v86mask);
    ts->target_v86->regs.eflags = tswap32(env->eflags);
#ifdef DEBUG_VM86
    fprintf(logfile, "save_v86_state: eflags=%08x cs:ip=%04x:%04x\n", 
            env->eflags, env->segs[R_CS], env->eip);
#endif

    /* restore 32 bit registers */
    env->regs[R_EAX] = ts->vm86_saved_regs.eax;
    env->regs[R_EBX] = ts->vm86_saved_regs.ebx;
    env->regs[R_ECX] = ts->vm86_saved_regs.ecx;
    env->regs[R_EDX] = ts->vm86_saved_regs.edx;
    env->regs[R_ESI] = ts->vm86_saved_regs.esi;
    env->regs[R_EDI] = ts->vm86_saved_regs.edi;
    env->regs[R_EBP] = ts->vm86_saved_regs.ebp;
    env->regs[R_ESP] = ts->vm86_saved_regs.esp;
    env->eflags = ts->vm86_saved_regs.eflags;
    env->eip = ts->vm86_saved_regs.eip;

    cpu_x86_load_seg(env, R_CS, ts->vm86_saved_regs.cs);
    cpu_x86_load_seg(env, R_SS, ts->vm86_saved_regs.ss);
    cpu_x86_load_seg(env, R_DS, ts->vm86_saved_regs.ds);
    cpu_x86_load_seg(env, R_ES, ts->vm86_saved_regs.es);
    cpu_x86_load_seg(env, R_FS, ts->vm86_saved_regs.fs);
    cpu_x86_load_seg(env, R_GS, ts->vm86_saved_regs.gs);
}

/* return from vm86 mode to 32 bit. The vm86() syscall will return
   'retval' */
static inline void return_to_32bit(CPUX86State *env, int retval)
{
#ifdef DEBUG_VM86
    fprintf(logfile, "return_to_32bit: ret=0x%x\n", retval);
#endif
    save_v86_state(env);
    env->regs[R_EAX] = retval;
}

static inline int set_IF(CPUX86State *env)
{
    TaskState *ts = env->opaque;
    
    ts->v86flags |= VIF_MASK;
    if (ts->v86flags & VIP_MASK) {
        return_to_32bit(env, TARGET_VM86_STI);
        return 1;
    }
    return 0;
}

static inline void clear_IF(CPUX86State *env)
{
    TaskState *ts = env->opaque;

    ts->v86flags &= ~VIF_MASK;
}

static inline void clear_TF(CPUX86State *env)
{
    env->eflags &= ~TF_MASK;
}

static inline int set_vflags_long(unsigned long eflags, CPUX86State *env)
{
    TaskState *ts = env->opaque;

    set_flags(ts->v86flags, eflags, ts->v86mask);
    set_flags(env->eflags, eflags, SAFE_MASK);
    if (eflags & IF_MASK)
        return set_IF(env);
    return 0;
}

static inline int set_vflags_short(unsigned short flags, CPUX86State *env)
{
    TaskState *ts = env->opaque;

    set_flags(ts->v86flags, flags, ts->v86mask & 0xffff);
    set_flags(env->eflags, flags, SAFE_MASK);
    if (flags & IF_MASK)
        return set_IF(env);
    return 0;
}

static inline unsigned int get_vflags(CPUX86State *env)
{
    TaskState *ts = env->opaque;
    unsigned int flags;

    flags = env->eflags & RETURN_MASK;
    if (ts->v86flags & VIF_MASK)
        flags |= IF_MASK;
    return flags | (ts->v86flags & ts->v86mask);
}

#define ADD16(reg, val) reg = (reg & ~0xffff) | ((reg + (val)) & 0xffff)

/* handle VM86 interrupt (NOTE: the CPU core currently does not
   support TSS interrupt revectoring, so this code is always executed) */
void do_int(CPUX86State *env, int intno)
{
    TaskState *ts = env->opaque;
    uint32_t *int_ptr, segoffs;
    uint8_t *ssp;
    unsigned int sp;

#if 1
    if (intno == 0xe6 && (env->regs[R_EAX] & 0xffff) == 0x00c0)
        loglevel = 1;
#endif

    if (env->segs[R_CS] == TARGET_BIOSSEG)
        goto cannot_handle;
    if (is_revectored(intno, &ts->target_v86->int_revectored))
        goto cannot_handle;
    if (intno == 0x21 && is_revectored((env->regs[R_EAX] >> 8) & 0xff, 
                                       &ts->target_v86->int21_revectored))
        goto cannot_handle;
    int_ptr = (uint32_t *)(intno << 2);
    segoffs = tswap32(*int_ptr);
    if ((segoffs >> 16) == TARGET_BIOSSEG)
        goto cannot_handle;
#if defined(DEBUG_VM86)
    fprintf(logfile, "VM86: emulating int 0x%x. CS:IP=%04x:%04x\n", 
            intno, segoffs >> 16, segoffs & 0xffff);
#endif
    /* save old state */
    ssp = (uint8_t *)(env->segs[R_SS] << 4);
    sp = env->regs[R_ESP] & 0xffff;
    vm_putw(ssp, sp - 2, get_vflags(env));
    vm_putw(ssp, sp - 4, env->segs[R_CS]);
    vm_putw(ssp, sp - 6, env->eip);
    ADD16(env->regs[R_ESP], -6);
    /* goto interrupt handler */
    env->eip = segoffs & 0xffff;
    cpu_x86_load_seg(env, R_CS, segoffs >> 16);
    clear_TF(env);
    clear_IF(env);
    return;
 cannot_handle:
#if defined(DEBUG_VM86)
    fprintf(logfile, "VM86: return to 32 bits int 0x%x\n", intno);
#endif
    return_to_32bit(env, TARGET_VM86_INTx | (intno << 8));
}

#define CHECK_IF_IN_TRAP(disp) \
      if ((tswap32(ts->target_v86->vm86plus.flags) & TARGET_vm86dbg_active) && \
          (tswap32(ts->target_v86->vm86plus.flags) & TARGET_vm86dbg_TFpendig)) \
		vm_putw(ssp,sp + disp,vm_getw(ssp,sp + disp) | TF_MASK)

#define VM86_FAULT_RETURN \
        if ((tswap32(ts->target_v86->vm86plus.flags) & TARGET_force_return_for_pic) && \
            (ts->v86flags & (IF_MASK | VIF_MASK))) \
            return_to_32bit(env, TARGET_VM86_PICRETURN); \
        return

void handle_vm86_fault(CPUX86State *env)
{
    TaskState *ts = env->opaque;
    uint8_t *csp, *pc, *ssp;
    unsigned int ip, sp;

    csp = (uint8_t *)(env->segs[R_CS] << 4);
    ip = env->eip & 0xffff;
    pc = csp + ip;
    
    ssp = (uint8_t *)(env->segs[R_SS] << 4);
    sp = env->regs[R_ESP] & 0xffff;

#if defined(DEBUG_VM86)
    fprintf(logfile, "VM86 exception %04x:%08x %02x %02x\n",
            env->segs[R_CS], env->eip, pc[0], pc[1]);
#endif

    /* VM86 mode */
    switch(pc[0]) {
    case 0x66:
        switch(pc[1]) {
        case 0x9c: /* pushfd */
            ADD16(env->eip, 2);
            ADD16(env->regs[R_ESP], -4);
            vm_putl(ssp, sp - 4, get_vflags(env));
            VM86_FAULT_RETURN;

        case 0x9d: /* popfd */
            ADD16(env->eip, 2);
            ADD16(env->regs[R_ESP], 4);
            CHECK_IF_IN_TRAP(0);
            if (set_vflags_long(vm_getl(ssp, sp), env))
                return;
            VM86_FAULT_RETURN;

        case 0xcf: /* iretd */
            ADD16(env->regs[R_ESP], 12);
            env->eip = vm_getl(ssp, sp) & 0xffff;
            cpu_x86_load_seg(env, R_CS, vm_getl(ssp, sp + 4) & 0xffff);
            CHECK_IF_IN_TRAP(8);
            if (set_vflags_long(vm_getl(ssp, sp + 8), env))
                return;
            VM86_FAULT_RETURN;

        default:
            goto vm86_gpf;
        }
        break;
    case 0x9c: /* pushf */
        ADD16(env->eip, 1);
        ADD16(env->regs[R_ESP], -2);
        vm_putw(ssp, sp - 2, get_vflags(env));
        VM86_FAULT_RETURN;

    case 0x9d: /* popf */
        ADD16(env->eip, 1);
        ADD16(env->regs[R_ESP], 2);
        CHECK_IF_IN_TRAP(0);
        if (set_vflags_short(vm_getw(ssp, sp), env))
            return;
        VM86_FAULT_RETURN;

    case 0xcd: /* int */
        ADD16(env->eip, 2);
        do_int(env, pc[1]);
        break;

    case 0xcf: /* iret */
        ADD16(env->regs[R_ESP], 6);
        env->eip = vm_getw(ssp, sp);
        cpu_x86_load_seg(env, R_CS, vm_getw(ssp, sp + 2));
        CHECK_IF_IN_TRAP(4);
        if (set_vflags_short(vm_getw(ssp, sp + 4), env))
            return;
        VM86_FAULT_RETURN;

    case 0xfa: /* cli */
        ADD16(env->eip, 1);
        clear_IF(env);
        VM86_FAULT_RETURN;
        
    case 0xfb: /* sti */
        ADD16(env->eip, 1);
        if (set_IF(env))
            return;
        VM86_FAULT_RETURN;

    default:
    vm86_gpf:
        /* real VM86 GPF exception */
        return_to_32bit(env, TARGET_VM86_UNKNOWN);
        break;
    }
}

int do_vm86(CPUX86State *env, long subfunction, 
            struct target_vm86plus_struct * target_v86)
{
    TaskState *ts = env->opaque;
    int ret;
    
    switch (subfunction) {
    case TARGET_VM86_REQUEST_IRQ:
    case TARGET_VM86_FREE_IRQ:
    case TARGET_VM86_GET_IRQ_BITS:
    case TARGET_VM86_GET_AND_RESET_IRQ:
        gemu_log("qemu: unsupported vm86 subfunction (%ld)\n", subfunction);
        ret = -EINVAL;
        goto out;
    case TARGET_VM86_PLUS_INSTALL_CHECK:
        /* NOTE: on old vm86 stuff this will return the error
           from verify_area(), because the subfunction is
           interpreted as (invalid) address to vm86_struct.
           So the installation check works.
            */
        ret = 0;
        goto out;
    }

    ts->target_v86 = target_v86;
    /* save current CPU regs */
    ts->vm86_saved_regs.eax = 0; /* default vm86 syscall return code */
    ts->vm86_saved_regs.ebx = env->regs[R_EBX];
    ts->vm86_saved_regs.ecx = env->regs[R_ECX];
    ts->vm86_saved_regs.edx = env->regs[R_EDX];
    ts->vm86_saved_regs.esi = env->regs[R_ESI];
    ts->vm86_saved_regs.edi = env->regs[R_EDI];
    ts->vm86_saved_regs.ebp = env->regs[R_EBP];
    ts->vm86_saved_regs.esp = env->regs[R_ESP];
    ts->vm86_saved_regs.eflags = env->eflags;
    ts->vm86_saved_regs.eip  = env->eip;
    ts->vm86_saved_regs.cs = env->segs[R_CS];
    ts->vm86_saved_regs.ss = env->segs[R_SS];
    ts->vm86_saved_regs.ds = env->segs[R_DS];
    ts->vm86_saved_regs.es = env->segs[R_ES];
    ts->vm86_saved_regs.fs = env->segs[R_FS];
    ts->vm86_saved_regs.gs = env->segs[R_GS];

    /* build vm86 CPU state */
    ts->v86flags = tswap32(target_v86->regs.eflags);
    env->eflags = (env->eflags & ~SAFE_MASK) | 
        (tswap32(target_v86->regs.eflags) & SAFE_MASK) | VM_MASK;
    ts->v86mask = ID_MASK | AC_MASK | NT_MASK | IOPL_MASK;

    env->regs[R_EBX] = tswap32(target_v86->regs.ebx);
    env->regs[R_ECX] = tswap32(target_v86->regs.ecx);
    env->regs[R_EDX] = tswap32(target_v86->regs.edx);
    env->regs[R_ESI] = tswap32(target_v86->regs.esi);
    env->regs[R_EDI] = tswap32(target_v86->regs.edi);
    env->regs[R_EBP] = tswap32(target_v86->regs.ebp);
    env->regs[R_ESP] = tswap32(target_v86->regs.esp);
    env->eip = tswap32(target_v86->regs.eip);
    cpu_x86_load_seg(env, R_CS, tswap16(target_v86->regs.cs));
    cpu_x86_load_seg(env, R_SS, tswap16(target_v86->regs.ss));
    cpu_x86_load_seg(env, R_DS, tswap16(target_v86->regs.ds));
    cpu_x86_load_seg(env, R_ES, tswap16(target_v86->regs.es));
    cpu_x86_load_seg(env, R_FS, tswap16(target_v86->regs.fs));
    cpu_x86_load_seg(env, R_GS, tswap16(target_v86->regs.gs));
    ret = tswap32(target_v86->regs.eax); /* eax will be restored at
                                            the end of the syscall */
#ifdef DEBUG_VM86
    fprintf(logfile, "do_vm86: cs:ip=%04x:%04x\n", env->segs[R_CS], env->eip);
#endif
    /* now the virtual CPU is ready for vm86 execution ! */
 out:
    return ret;
}

