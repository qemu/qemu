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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "qemu.h"

//#define DEBUG_VM86

#ifdef DEBUG_VM86
#  define LOG_VM86(...) qemu_log(__VA_ARGS__);
#else
#  define LOG_VM86(...) do { } while (0)
#endif


#define set_flags(X,new,mask) \
((X) = ((X) & ~(mask)) | ((new) & (mask)))

#define SAFE_MASK	(0xDD5)
#define RETURN_MASK	(0xDFF)

static inline int is_revectored(int nr, struct target_revectored_struct *bitmap)
{
    return (((uint8_t *)bitmap)[nr >> 3] >> (nr & 7)) & 1;
}

static inline void vm_putw(uint32_t segptr, unsigned int reg16, unsigned int val)
{
    stw(segptr + (reg16 & 0xffff), val);
}

static inline void vm_putl(uint32_t segptr, unsigned int reg16, unsigned int val)
{
    stl(segptr + (reg16 & 0xffff), val);
}

static inline unsigned int vm_getb(uint32_t segptr, unsigned int reg16)
{
    return ldub(segptr + (reg16 & 0xffff));
}

static inline unsigned int vm_getw(uint32_t segptr, unsigned int reg16)
{
    return lduw(segptr + (reg16 & 0xffff));
}

static inline unsigned int vm_getl(uint32_t segptr, unsigned int reg16)
{
    return ldl(segptr + (reg16 & 0xffff));
}

void save_v86_state(CPUX86State *env)
{
    TaskState *ts = env->opaque;
    struct target_vm86plus_struct * target_v86;

    if (!lock_user_struct(VERIFY_WRITE, target_v86, ts->target_v86, 0))
        /* FIXME - should return an error */
        return;
    /* put the VM86 registers in the userspace register structure */
    target_v86->regs.eax = tswap32(env->regs[R_EAX]);
    target_v86->regs.ebx = tswap32(env->regs[R_EBX]);
    target_v86->regs.ecx = tswap32(env->regs[R_ECX]);
    target_v86->regs.edx = tswap32(env->regs[R_EDX]);
    target_v86->regs.esi = tswap32(env->regs[R_ESI]);
    target_v86->regs.edi = tswap32(env->regs[R_EDI]);
    target_v86->regs.ebp = tswap32(env->regs[R_EBP]);
    target_v86->regs.esp = tswap32(env->regs[R_ESP]);
    target_v86->regs.eip = tswap32(env->eip);
    target_v86->regs.cs = tswap16(env->segs[R_CS].selector);
    target_v86->regs.ss = tswap16(env->segs[R_SS].selector);
    target_v86->regs.ds = tswap16(env->segs[R_DS].selector);
    target_v86->regs.es = tswap16(env->segs[R_ES].selector);
    target_v86->regs.fs = tswap16(env->segs[R_FS].selector);
    target_v86->regs.gs = tswap16(env->segs[R_GS].selector);
    set_flags(env->eflags, ts->v86flags, VIF_MASK | ts->v86mask);
    target_v86->regs.eflags = tswap32(env->eflags);
    unlock_user_struct(target_v86, ts->target_v86, 1);
    LOG_VM86("save_v86_state: eflags=%08x cs:ip=%04x:%04x\n",
             env->eflags, env->segs[R_CS].selector, env->eip);

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
    LOG_VM86("return_to_32bit: ret=0x%x\n", retval);
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

static inline void clear_AC(CPUX86State *env)
{
    env->eflags &= ~AC_MASK;
}

static inline int set_vflags_long(unsigned long eflags, CPUX86State *env)
{
    TaskState *ts = env->opaque;

    set_flags(ts->v86flags, eflags, ts->v86mask);
    set_flags(env->eflags, eflags, SAFE_MASK);
    if (eflags & IF_MASK)
        return set_IF(env);
    else
        clear_IF(env);
    return 0;
}

static inline int set_vflags_short(unsigned short flags, CPUX86State *env)
{
    TaskState *ts = env->opaque;

    set_flags(ts->v86flags, flags, ts->v86mask & 0xffff);
    set_flags(env->eflags, flags, SAFE_MASK);
    if (flags & IF_MASK)
        return set_IF(env);
    else
        clear_IF(env);
    return 0;
}

static inline unsigned int get_vflags(CPUX86State *env)
{
    TaskState *ts = env->opaque;
    unsigned int flags;

    flags = env->eflags & RETURN_MASK;
    if (ts->v86flags & VIF_MASK)
        flags |= IF_MASK;
    flags |= IOPL_MASK;
    return flags | (ts->v86flags & ts->v86mask);
}

#define ADD16(reg, val) reg = (reg & ~0xffff) | ((reg + (val)) & 0xffff)

/* handle VM86 interrupt (NOTE: the CPU core currently does not
   support TSS interrupt revectoring, so this code is always executed) */
static void do_int(CPUX86State *env, int intno)
{
    TaskState *ts = env->opaque;
    uint32_t int_addr, segoffs, ssp;
    unsigned int sp;

    if (env->segs[R_CS].selector == TARGET_BIOSSEG)
        goto cannot_handle;
    if (is_revectored(intno, &ts->vm86plus.int_revectored))
        goto cannot_handle;
    if (intno == 0x21 && is_revectored((env->regs[R_EAX] >> 8) & 0xff,
                                       &ts->vm86plus.int21_revectored))
        goto cannot_handle;
    int_addr = (intno << 2);
    segoffs = ldl(int_addr);
    if ((segoffs >> 16) == TARGET_BIOSSEG)
        goto cannot_handle;
    LOG_VM86("VM86: emulating int 0x%x. CS:IP=%04x:%04x\n",
             intno, segoffs >> 16, segoffs & 0xffff);
    /* save old state */
    ssp = env->segs[R_SS].selector << 4;
    sp = env->regs[R_ESP] & 0xffff;
    vm_putw(ssp, sp - 2, get_vflags(env));
    vm_putw(ssp, sp - 4, env->segs[R_CS].selector);
    vm_putw(ssp, sp - 6, env->eip);
    ADD16(env->regs[R_ESP], -6);
    /* goto interrupt handler */
    env->eip = segoffs & 0xffff;
    cpu_x86_load_seg(env, R_CS, segoffs >> 16);
    clear_TF(env);
    clear_IF(env);
    clear_AC(env);
    return;
 cannot_handle:
    LOG_VM86("VM86: return to 32 bits int 0x%x\n", intno);
    return_to_32bit(env, TARGET_VM86_INTx | (intno << 8));
}

void handle_vm86_trap(CPUX86State *env, int trapno)
{
    if (trapno == 1 || trapno == 3) {
        return_to_32bit(env, TARGET_VM86_TRAP + (trapno << 8));
    } else {
        do_int(env, trapno);
    }
}

#define CHECK_IF_IN_TRAP() \
      if ((ts->vm86plus.vm86plus.flags & TARGET_vm86dbg_active) && \
          (ts->vm86plus.vm86plus.flags & TARGET_vm86dbg_TFpendig)) \
		newflags |= TF_MASK

#define VM86_FAULT_RETURN \
        if ((ts->vm86plus.vm86plus.flags & TARGET_force_return_for_pic) && \
            (ts->v86flags & (IF_MASK | VIF_MASK))) \
            return_to_32bit(env, TARGET_VM86_PICRETURN); \
        return

void handle_vm86_fault(CPUX86State *env)
{
    TaskState *ts = env->opaque;
    uint32_t csp, ssp;
    unsigned int ip, sp, newflags, newip, newcs, opcode, intno;
    int data32, pref_done;

    csp = env->segs[R_CS].selector << 4;
    ip = env->eip & 0xffff;

    ssp = env->segs[R_SS].selector << 4;
    sp = env->regs[R_ESP] & 0xffff;

    LOG_VM86("VM86 exception %04x:%08x\n",
             env->segs[R_CS].selector, env->eip);

    data32 = 0;
    pref_done = 0;
    do {
        opcode = vm_getb(csp, ip);
        ADD16(ip, 1);
        switch (opcode) {
        case 0x66:      /* 32-bit data */     data32=1; break;
        case 0x67:      /* 32-bit address */  break;
        case 0x2e:      /* CS */              break;
        case 0x3e:      /* DS */              break;
        case 0x26:      /* ES */              break;
        case 0x36:      /* SS */              break;
        case 0x65:      /* GS */              break;
        case 0x64:      /* FS */              break;
        case 0xf2:      /* repnz */	      break;
        case 0xf3:      /* rep */             break;
        default: pref_done = 1;
        }
    } while (!pref_done);

    /* VM86 mode */
    switch(opcode) {
    case 0x9c: /* pushf */
        if (data32) {
            vm_putl(ssp, sp - 4, get_vflags(env));
            ADD16(env->regs[R_ESP], -4);
        } else {
            vm_putw(ssp, sp - 2, get_vflags(env));
            ADD16(env->regs[R_ESP], -2);
        }
        env->eip = ip;
        VM86_FAULT_RETURN;

    case 0x9d: /* popf */
        if (data32) {
            newflags = vm_getl(ssp, sp);
            ADD16(env->regs[R_ESP], 4);
        } else {
            newflags = vm_getw(ssp, sp);
            ADD16(env->regs[R_ESP], 2);
        }
        env->eip = ip;
        CHECK_IF_IN_TRAP();
        if (data32) {
            if (set_vflags_long(newflags, env))
                return;
        } else {
            if (set_vflags_short(newflags, env))
                return;
        }
        VM86_FAULT_RETURN;

    case 0xcd: /* int */
        intno = vm_getb(csp, ip);
        ADD16(ip, 1);
        env->eip = ip;
        if (ts->vm86plus.vm86plus.flags & TARGET_vm86dbg_active) {
            if ( (ts->vm86plus.vm86plus.vm86dbg_intxxtab[intno >> 3] >>
                  (intno &7)) & 1) {
                return_to_32bit(env, TARGET_VM86_INTx + (intno << 8));
                return;
            }
        }
        do_int(env, intno);
        break;

    case 0xcf: /* iret */
        if (data32) {
            newip = vm_getl(ssp, sp) & 0xffff;
            newcs = vm_getl(ssp, sp + 4) & 0xffff;
            newflags = vm_getl(ssp, sp + 8);
            ADD16(env->regs[R_ESP], 12);
        } else {
            newip = vm_getw(ssp, sp);
            newcs = vm_getw(ssp, sp + 2);
            newflags = vm_getw(ssp, sp + 4);
            ADD16(env->regs[R_ESP], 6);
        }
        env->eip = newip;
        cpu_x86_load_seg(env, R_CS, newcs);
        CHECK_IF_IN_TRAP();
        if (data32) {
            if (set_vflags_long(newflags, env))
                return;
        } else {
            if (set_vflags_short(newflags, env))
                return;
        }
        VM86_FAULT_RETURN;

    case 0xfa: /* cli */
        env->eip = ip;
        clear_IF(env);
        VM86_FAULT_RETURN;

    case 0xfb: /* sti */
        env->eip = ip;
        if (set_IF(env))
            return;
        VM86_FAULT_RETURN;

    default:
        /* real VM86 GPF exception */
        return_to_32bit(env, TARGET_VM86_UNKNOWN);
        break;
    }
}

int do_vm86(CPUX86State *env, long subfunction, abi_ulong vm86_addr)
{
    TaskState *ts = env->opaque;
    struct target_vm86plus_struct * target_v86;
    int ret;

    switch (subfunction) {
    case TARGET_VM86_REQUEST_IRQ:
    case TARGET_VM86_FREE_IRQ:
    case TARGET_VM86_GET_IRQ_BITS:
    case TARGET_VM86_GET_AND_RESET_IRQ:
        gemu_log("qemu: unsupported vm86 subfunction (%ld)\n", subfunction);
        ret = -TARGET_EINVAL;
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
    ts->vm86_saved_regs.cs = env->segs[R_CS].selector;
    ts->vm86_saved_regs.ss = env->segs[R_SS].selector;
    ts->vm86_saved_regs.ds = env->segs[R_DS].selector;
    ts->vm86_saved_regs.es = env->segs[R_ES].selector;
    ts->vm86_saved_regs.fs = env->segs[R_FS].selector;
    ts->vm86_saved_regs.gs = env->segs[R_GS].selector;

    ts->target_v86 = vm86_addr;
    if (!lock_user_struct(VERIFY_READ, target_v86, vm86_addr, 1))
        return -TARGET_EFAULT;
    /* build vm86 CPU state */
    ts->v86flags = tswap32(target_v86->regs.eflags);
    env->eflags = (env->eflags & ~SAFE_MASK) |
        (tswap32(target_v86->regs.eflags) & SAFE_MASK) | VM_MASK;

    ts->vm86plus.cpu_type = tswapal(target_v86->cpu_type);
    switch (ts->vm86plus.cpu_type) {
    case TARGET_CPU_286:
        ts->v86mask = 0;
        break;
    case TARGET_CPU_386:
        ts->v86mask = NT_MASK | IOPL_MASK;
        break;
    case TARGET_CPU_486:
        ts->v86mask = AC_MASK | NT_MASK | IOPL_MASK;
        break;
    default:
        ts->v86mask = ID_MASK | AC_MASK | NT_MASK | IOPL_MASK;
        break;
    }

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
    memcpy(&ts->vm86plus.int_revectored,
           &target_v86->int_revectored, 32);
    memcpy(&ts->vm86plus.int21_revectored,
           &target_v86->int21_revectored, 32);
    ts->vm86plus.vm86plus.flags = tswapal(target_v86->vm86plus.flags);
    memcpy(&ts->vm86plus.vm86plus.vm86dbg_intxxtab,
           target_v86->vm86plus.vm86dbg_intxxtab, 32);
    unlock_user_struct(target_v86, vm86_addr, 0);

    LOG_VM86("do_vm86: cs:ip=%04x:%04x\n",
             env->segs[R_CS].selector, env->eip);
    /* now the virtual CPU is ready for vm86 execution ! */
 out:
    return ret;
}
