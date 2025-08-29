/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
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
    static const char elf_platform[4][5] = { "i386", "i486", "i586", "i686" };
    int family = object_property_get_int(OBJECT(cs), "family", NULL);

    family = MAX(MIN(family, 6), 3);
    return elf_platform[family - 3];
}

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUX86State *env)
{
    r->pt.bx = tswapal(env->regs[R_EBX]);
    r->pt.cx = tswapal(env->regs[R_ECX]);
    r->pt.dx = tswapal(env->regs[R_EDX]);
    r->pt.si = tswapal(env->regs[R_ESI]);
    r->pt.di = tswapal(env->regs[R_EDI]);
    r->pt.bp = tswapal(env->regs[R_EBP]);
    r->pt.ax = tswapal(env->regs[R_EAX]);
    r->pt.ds = tswapal(env->segs[R_DS].selector & 0xffff);
    r->pt.es = tswapal(env->segs[R_ES].selector & 0xffff);
    r->pt.fs = tswapal(env->segs[R_FS].selector & 0xffff);
    r->pt.gs = tswapal(env->segs[R_GS].selector & 0xffff);
    r->pt.orig_ax = tswapal(get_task_state(env_cpu_const(env))->orig_ax);
    r->pt.ip = tswapal(env->eip);
    r->pt.cs = tswapal(env->segs[R_CS].selector & 0xffff);
    r->pt.flags = tswapal(env->eflags);
    r->pt.sp = tswapal(env->regs[R_ESP]);
    r->pt.ss = tswapal(env->segs[R_SS].selector & 0xffff);
}
