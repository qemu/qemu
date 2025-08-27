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
    return "x86_64";
}

#define tswapreg(ptr)   tswapal(ptr)

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUX86State *env)
{
    r->regs[0] = tswapreg(env->regs[15]);
    r->regs[1] = tswapreg(env->regs[14]);
    r->regs[2] = tswapreg(env->regs[13]);
    r->regs[3] = tswapreg(env->regs[12]);
    r->regs[4] = tswapreg(env->regs[R_EBP]);
    r->regs[5] = tswapreg(env->regs[R_EBX]);
    r->regs[6] = tswapreg(env->regs[11]);
    r->regs[7] = tswapreg(env->regs[10]);
    r->regs[8] = tswapreg(env->regs[9]);
    r->regs[9] = tswapreg(env->regs[8]);
    r->regs[10] = tswapreg(env->regs[R_EAX]);
    r->regs[11] = tswapreg(env->regs[R_ECX]);
    r->regs[12] = tswapreg(env->regs[R_EDX]);
    r->regs[13] = tswapreg(env->regs[R_ESI]);
    r->regs[14] = tswapreg(env->regs[R_EDI]);
    r->regs[15] = tswapreg(get_task_state(env_cpu_const(env))->orig_ax);
    r->regs[16] = tswapreg(env->eip);
    r->regs[17] = tswapreg(env->segs[R_CS].selector & 0xffff);
    r->regs[18] = tswapreg(env->eflags);
    r->regs[19] = tswapreg(env->regs[R_ESP]);
    r->regs[20] = tswapreg(env->segs[R_SS].selector & 0xffff);
    r->regs[21] = tswapreg(env->segs[R_FS].selector & 0xffff);
    r->regs[22] = tswapreg(env->segs[R_GS].selector & 0xffff);
    r->regs[23] = tswapreg(env->segs[R_DS].selector & 0xffff);
    r->regs[24] = tswapreg(env->segs[R_ES].selector & 0xffff);
    r->regs[25] = tswapreg(env->segs[R_FS].selector & 0xffff);
    r->regs[26] = tswapreg(env->segs[R_GS].selector & 0xffff);
}
