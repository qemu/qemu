/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "elf.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    if (eflags == 0 || (eflags & EF_M68K_M68000)) {
        /* 680x0 */
        return "m68040";
    }

    /* Coldfire */
    return "any";
}

#define tswapreg(ptr)   tswapal(ptr)

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUM68KState *env)
{
    r->regs[0] = tswapreg(env->dregs[1]);
    r->regs[1] = tswapreg(env->dregs[2]);
    r->regs[2] = tswapreg(env->dregs[3]);
    r->regs[3] = tswapreg(env->dregs[4]);
    r->regs[4] = tswapreg(env->dregs[5]);
    r->regs[5] = tswapreg(env->dregs[6]);
    r->regs[6] = tswapreg(env->dregs[7]);
    r->regs[7] = tswapreg(env->aregs[0]);
    r->regs[8] = tswapreg(env->aregs[1]);
    r->regs[9] = tswapreg(env->aregs[2]);
    r->regs[10] = tswapreg(env->aregs[3]);
    r->regs[11] = tswapreg(env->aregs[4]);
    r->regs[12] = tswapreg(env->aregs[5]);
    r->regs[13] = tswapreg(env->aregs[6]);
    r->regs[14] = tswapreg(env->dregs[0]);
    r->regs[15] = tswapreg(env->aregs[7]);
    r->regs[16] = tswapreg(env->dregs[0]); /* FIXME: orig_d0 */
    r->regs[17] = tswapreg(env->sr);
    r->regs[18] = tswapreg(env->pc);
    r->regs[19] = 0;  /* FIXME: regs->format | regs->vector */
}
