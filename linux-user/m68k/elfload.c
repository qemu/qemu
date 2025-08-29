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

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUM68KState *env)
{
    r->d1 = tswapal(env->dregs[1]);
    r->d2 = tswapal(env->dregs[2]);
    r->d3 = tswapal(env->dregs[3]);
    r->d4 = tswapal(env->dregs[4]);
    r->d5 = tswapal(env->dregs[5]);
    r->d6 = tswapal(env->dregs[6]);
    r->d7 = tswapal(env->dregs[7]);
    r->a0 = tswapal(env->aregs[0]);
    r->a1 = tswapal(env->aregs[1]);
    r->a2 = tswapal(env->aregs[2]);
    r->a3 = tswapal(env->aregs[3]);
    r->a4 = tswapal(env->aregs[4]);
    r->a5 = tswapal(env->aregs[5]);
    r->a6 = tswapal(env->aregs[6]);
    r->d0 = tswapal(env->dregs[0]);
    r->usp = tswapal(env->aregs[7]);
    r->orig_d0 = tswapal(env->dregs[0]); /* FIXME */
    r->sr = tswapal(env->sr);
    r->pc = tswapal(env->pc);
    /* FIXME: regs->format | regs->vector */
}
