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

#define tswapreg(ptr)   tswapal(ptr)

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUX86State *env)
{
    r->regs[0] = tswapreg(env->regs[R_EBX]);
    r->regs[1] = tswapreg(env->regs[R_ECX]);
    r->regs[2] = tswapreg(env->regs[R_EDX]);
    r->regs[3] = tswapreg(env->regs[R_ESI]);
    r->regs[4] = tswapreg(env->regs[R_EDI]);
    r->regs[5] = tswapreg(env->regs[R_EBP]);
    r->regs[6] = tswapreg(env->regs[R_EAX]);
    r->regs[7] = tswapreg(env->segs[R_DS].selector & 0xffff);
    r->regs[8] = tswapreg(env->segs[R_ES].selector & 0xffff);
    r->regs[9] = tswapreg(env->segs[R_FS].selector & 0xffff);
    r->regs[10] = tswapreg(env->segs[R_GS].selector & 0xffff);
    r->regs[11] = tswapreg(get_task_state(env_cpu_const(env))->orig_ax);
    r->regs[12] = tswapreg(env->eip);
    r->regs[13] = tswapreg(env->segs[R_CS].selector & 0xffff);
    r->regs[14] = tswapreg(env->eflags);
    r->regs[15] = tswapreg(env->regs[R_ESP]);
    r->regs[16] = tswapreg(env->segs[R_SS].selector & 0xffff);
}
