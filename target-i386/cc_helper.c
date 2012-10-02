/*
 *  x86 condition code helpers
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
#include "helper.h"

const uint8_t parity_table[256] = {
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
};

#define SHIFT 0
#include "cc_helper_template.h"
#undef SHIFT

#define SHIFT 1
#include "cc_helper_template.h"
#undef SHIFT

#define SHIFT 2
#include "cc_helper_template.h"
#undef SHIFT

#ifdef TARGET_X86_64

#define SHIFT 3
#include "cc_helper_template.h"
#undef SHIFT

#endif

static int compute_all_eflags(CPUX86State *env)
{
    return CC_SRC;
}

static int compute_c_eflags(CPUX86State *env)
{
    return CC_SRC & CC_C;
}

uint32_t helper_cc_compute_all(CPUX86State *env, int op)
{
    switch (op) {
    default: /* should never happen */
        return 0;

    case CC_OP_EFLAGS:
        return compute_all_eflags(env);

    case CC_OP_MULB:
        return compute_all_mulb(env);
    case CC_OP_MULW:
        return compute_all_mulw(env);
    case CC_OP_MULL:
        return compute_all_mull(env);

    case CC_OP_ADDB:
        return compute_all_addb(env);
    case CC_OP_ADDW:
        return compute_all_addw(env);
    case CC_OP_ADDL:
        return compute_all_addl(env);

    case CC_OP_ADCB:
        return compute_all_adcb(env);
    case CC_OP_ADCW:
        return compute_all_adcw(env);
    case CC_OP_ADCL:
        return compute_all_adcl(env);

    case CC_OP_SUBB:
        return compute_all_subb(env);
    case CC_OP_SUBW:
        return compute_all_subw(env);
    case CC_OP_SUBL:
        return compute_all_subl(env);

    case CC_OP_SBBB:
        return compute_all_sbbb(env);
    case CC_OP_SBBW:
        return compute_all_sbbw(env);
    case CC_OP_SBBL:
        return compute_all_sbbl(env);

    case CC_OP_LOGICB:
        return compute_all_logicb(env);
    case CC_OP_LOGICW:
        return compute_all_logicw(env);
    case CC_OP_LOGICL:
        return compute_all_logicl(env);

    case CC_OP_INCB:
        return compute_all_incb(env);
    case CC_OP_INCW:
        return compute_all_incw(env);
    case CC_OP_INCL:
        return compute_all_incl(env);

    case CC_OP_DECB:
        return compute_all_decb(env);
    case CC_OP_DECW:
        return compute_all_decw(env);
    case CC_OP_DECL:
        return compute_all_decl(env);

    case CC_OP_SHLB:
        return compute_all_shlb(env);
    case CC_OP_SHLW:
        return compute_all_shlw(env);
    case CC_OP_SHLL:
        return compute_all_shll(env);

    case CC_OP_SARB:
        return compute_all_sarb(env);
    case CC_OP_SARW:
        return compute_all_sarw(env);
    case CC_OP_SARL:
        return compute_all_sarl(env);

#ifdef TARGET_X86_64
    case CC_OP_MULQ:
        return compute_all_mulq(env);

    case CC_OP_ADDQ:
        return compute_all_addq(env);

    case CC_OP_ADCQ:
        return compute_all_adcq(env);

    case CC_OP_SUBQ:
        return compute_all_subq(env);

    case CC_OP_SBBQ:
        return compute_all_sbbq(env);

    case CC_OP_LOGICQ:
        return compute_all_logicq(env);

    case CC_OP_INCQ:
        return compute_all_incq(env);

    case CC_OP_DECQ:
        return compute_all_decq(env);

    case CC_OP_SHLQ:
        return compute_all_shlq(env);

    case CC_OP_SARQ:
        return compute_all_sarq(env);
#endif
    }
}

uint32_t cpu_cc_compute_all(CPUX86State *env, int op)
{
    return helper_cc_compute_all(env, op);
}

uint32_t helper_cc_compute_c(CPUX86State *env, int op)
{
    switch (op) {
    default: /* should never happen */
        return 0;

    case CC_OP_EFLAGS:
        return compute_c_eflags(env);

    case CC_OP_MULB:
        return compute_c_mull(env);
    case CC_OP_MULW:
        return compute_c_mull(env);
    case CC_OP_MULL:
        return compute_c_mull(env);

    case CC_OP_ADDB:
        return compute_c_addb(env);
    case CC_OP_ADDW:
        return compute_c_addw(env);
    case CC_OP_ADDL:
        return compute_c_addl(env);

    case CC_OP_ADCB:
        return compute_c_adcb(env);
    case CC_OP_ADCW:
        return compute_c_adcw(env);
    case CC_OP_ADCL:
        return compute_c_adcl(env);

    case CC_OP_SUBB:
        return compute_c_subb(env);
    case CC_OP_SUBW:
        return compute_c_subw(env);
    case CC_OP_SUBL:
        return compute_c_subl(env);

    case CC_OP_SBBB:
        return compute_c_sbbb(env);
    case CC_OP_SBBW:
        return compute_c_sbbw(env);
    case CC_OP_SBBL:
        return compute_c_sbbl(env);

    case CC_OP_LOGICB:
        return compute_c_logicb();
    case CC_OP_LOGICW:
        return compute_c_logicw();
    case CC_OP_LOGICL:
        return compute_c_logicl();

    case CC_OP_INCB:
        return compute_c_incl(env);
    case CC_OP_INCW:
        return compute_c_incl(env);
    case CC_OP_INCL:
        return compute_c_incl(env);

    case CC_OP_DECB:
        return compute_c_incl(env);
    case CC_OP_DECW:
        return compute_c_incl(env);
    case CC_OP_DECL:
        return compute_c_incl(env);

    case CC_OP_SHLB:
        return compute_c_shlb(env);
    case CC_OP_SHLW:
        return compute_c_shlw(env);
    case CC_OP_SHLL:
        return compute_c_shll(env);

    case CC_OP_SARB:
        return compute_c_sarl(env);
    case CC_OP_SARW:
        return compute_c_sarl(env);
    case CC_OP_SARL:
        return compute_c_sarl(env);

#ifdef TARGET_X86_64
    case CC_OP_MULQ:
        return compute_c_mull(env);

    case CC_OP_ADDQ:
        return compute_c_addq(env);

    case CC_OP_ADCQ:
        return compute_c_adcq(env);

    case CC_OP_SUBQ:
        return compute_c_subq(env);

    case CC_OP_SBBQ:
        return compute_c_sbbq(env);

    case CC_OP_LOGICQ:
        return compute_c_logicq();

    case CC_OP_INCQ:
        return compute_c_incl(env);

    case CC_OP_DECQ:
        return compute_c_incl(env);

    case CC_OP_SHLQ:
        return compute_c_shlq(env);

    case CC_OP_SARQ:
        return compute_c_sarl(env);
#endif
    }
}

void helper_write_eflags(CPUX86State *env, target_ulong t0,
                         uint32_t update_mask)
{
    cpu_load_eflags(env, t0, update_mask);
}

target_ulong helper_read_eflags(CPUX86State *env)
{
    uint32_t eflags;

    eflags = helper_cc_compute_all(env, CC_OP);
    eflags |= (DF & DF_MASK);
    eflags |= env->eflags & ~(VM_MASK | RF_MASK);
    return eflags;
}

void helper_clts(CPUX86State *env)
{
    env->cr[0] &= ~CR0_TS_MASK;
    env->hflags &= ~HF_TS_MASK;
}

void helper_reset_rf(CPUX86State *env)
{
    env->eflags &= ~RF_MASK;
}

void helper_cli(CPUX86State *env)
{
    env->eflags &= ~IF_MASK;
}

void helper_sti(CPUX86State *env)
{
    env->eflags |= IF_MASK;
}

void helper_clac(CPUX86State *env)
{
    env->eflags &= ~AC_MASK;
}

void helper_stac(CPUX86State *env)
{
    env->eflags |= AC_MASK;
}

#if 0
/* vm86plus instructions */
void helper_cli_vm(CPUX86State *env)
{
    env->eflags &= ~VIF_MASK;
}

void helper_sti_vm(CPUX86State *env)
{
    env->eflags |= VIF_MASK;
    if (env->eflags & VIP_MASK) {
        raise_exception(env, EXCP0D_GPF);
    }
}
#endif

void helper_set_inhibit_irq(CPUX86State *env)
{
    env->hflags |= HF_INHIBIT_IRQ_MASK;
}

void helper_reset_inhibit_irq(CPUX86State *env)
{
    env->hflags &= ~HF_INHIBIT_IRQ_MASK;
}
