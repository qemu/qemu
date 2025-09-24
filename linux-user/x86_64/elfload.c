/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "max";
}

abi_ulong get_elf_hwcap(CPUState *cs)
{
    return cpu_env(cs)->features[FEAT_1_EDX];
}

const char *get_elf_platform(CPUState *cs)
{
    return "x86_64";
}

bool init_guest_commpage(void)
{
    /*
     * The vsyscall page is at a high negative address aka kernel space,
     * which means that we cannot actually allocate it with target_mmap.
     * We still should be able to use page_set_flags, unless the user
     * has specified -R reserved_va, which would trigger an assert().
     */
    if (reserved_va != 0 &&
        TARGET_VSYSCALL_PAGE + TARGET_PAGE_SIZE - 1 > reserved_va) {
        error_report("Cannot allocate vsyscall page");
        exit(EXIT_FAILURE);
    }
    page_set_flags(TARGET_VSYSCALL_PAGE,
                   TARGET_VSYSCALL_PAGE | ~TARGET_PAGE_MASK,
                   PAGE_EXEC | PAGE_VALID, PAGE_VALID);
    return true;
}

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUX86State *env)
{
    r->pt.r15 = tswapal(env->regs[15]);
    r->pt.r14 = tswapal(env->regs[14]);
    r->pt.r13 = tswapal(env->regs[13]);
    r->pt.r12 = tswapal(env->regs[12]);
    r->pt.bp = tswapal(env->regs[R_EBP]);
    r->pt.bx = tswapal(env->regs[R_EBX]);
    r->pt.r11 = tswapal(env->regs[11]);
    r->pt.r10 = tswapal(env->regs[10]);
    r->pt.r9 = tswapal(env->regs[9]);
    r->pt.r8 = tswapal(env->regs[8]);
    r->pt.ax = tswapal(env->regs[R_EAX]);
    r->pt.cx = tswapal(env->regs[R_ECX]);
    r->pt.dx = tswapal(env->regs[R_EDX]);
    r->pt.si = tswapal(env->regs[R_ESI]);
    r->pt.di = tswapal(env->regs[R_EDI]);
    r->pt.orig_ax = tswapal(get_task_state(env_cpu_const(env))->orig_ax);
    r->pt.ip = tswapal(env->eip);
    r->pt.cs = tswapal(env->segs[R_CS].selector & 0xffff);
    r->pt.flags = tswapal(env->eflags);
    r->pt.sp = tswapal(env->regs[R_ESP]);
    r->pt.ss = tswapal(env->segs[R_SS].selector & 0xffff);
    r->pt.fs_base = tswapal(env->segs[R_FS].base);
    r->pt.gs_base = tswapal(env->segs[R_GS].base);
    r->pt.ds = tswapal(env->segs[R_DS].selector & 0xffff);
    r->pt.es = tswapal(env->segs[R_ES].selector & 0xffff);
    r->pt.fs = tswapal(env->segs[R_FS].selector & 0xffff);
    r->pt.gs = tswapal(env->segs[R_GS].selector & 0xffff);
}
