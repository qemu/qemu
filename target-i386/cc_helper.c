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
#include "dyngen-exec.h"
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

static int compute_all_eflags(void)
{
    return CC_SRC;
}

static int compute_c_eflags(void)
{
    return CC_SRC & CC_C;
}

uint32_t helper_cc_compute_all(int op)
{
    switch (op) {
    default: /* should never happen */
        return 0;

    case CC_OP_EFLAGS:
        return compute_all_eflags();

    case CC_OP_MULB:
        return compute_all_mulb();
    case CC_OP_MULW:
        return compute_all_mulw();
    case CC_OP_MULL:
        return compute_all_mull();

    case CC_OP_ADDB:
        return compute_all_addb();
    case CC_OP_ADDW:
        return compute_all_addw();
    case CC_OP_ADDL:
        return compute_all_addl();

    case CC_OP_ADCB:
        return compute_all_adcb();
    case CC_OP_ADCW:
        return compute_all_adcw();
    case CC_OP_ADCL:
        return compute_all_adcl();

    case CC_OP_SUBB:
        return compute_all_subb();
    case CC_OP_SUBW:
        return compute_all_subw();
    case CC_OP_SUBL:
        return compute_all_subl();

    case CC_OP_SBBB:
        return compute_all_sbbb();
    case CC_OP_SBBW:
        return compute_all_sbbw();
    case CC_OP_SBBL:
        return compute_all_sbbl();

    case CC_OP_LOGICB:
        return compute_all_logicb();
    case CC_OP_LOGICW:
        return compute_all_logicw();
    case CC_OP_LOGICL:
        return compute_all_logicl();

    case CC_OP_INCB:
        return compute_all_incb();
    case CC_OP_INCW:
        return compute_all_incw();
    case CC_OP_INCL:
        return compute_all_incl();

    case CC_OP_DECB:
        return compute_all_decb();
    case CC_OP_DECW:
        return compute_all_decw();
    case CC_OP_DECL:
        return compute_all_decl();

    case CC_OP_SHLB:
        return compute_all_shlb();
    case CC_OP_SHLW:
        return compute_all_shlw();
    case CC_OP_SHLL:
        return compute_all_shll();

    case CC_OP_SARB:
        return compute_all_sarb();
    case CC_OP_SARW:
        return compute_all_sarw();
    case CC_OP_SARL:
        return compute_all_sarl();

#ifdef TARGET_X86_64
    case CC_OP_MULQ:
        return compute_all_mulq();

    case CC_OP_ADDQ:
        return compute_all_addq();

    case CC_OP_ADCQ:
        return compute_all_adcq();

    case CC_OP_SUBQ:
        return compute_all_subq();

    case CC_OP_SBBQ:
        return compute_all_sbbq();

    case CC_OP_LOGICQ:
        return compute_all_logicq();

    case CC_OP_INCQ:
        return compute_all_incq();

    case CC_OP_DECQ:
        return compute_all_decq();

    case CC_OP_SHLQ:
        return compute_all_shlq();

    case CC_OP_SARQ:
        return compute_all_sarq();
#endif
    }
}

uint32_t cpu_cc_compute_all(CPUX86State *env1, int op)
{
    CPUX86State *saved_env;
    uint32_t ret;

    saved_env = env;
    env = env1;
    ret = helper_cc_compute_all(op);
    env = saved_env;
    return ret;
}

uint32_t helper_cc_compute_c(int op)
{
    switch (op) {
    default: /* should never happen */
        return 0;

    case CC_OP_EFLAGS:
        return compute_c_eflags();

    case CC_OP_MULB:
        return compute_c_mull();
    case CC_OP_MULW:
        return compute_c_mull();
    case CC_OP_MULL:
        return compute_c_mull();

    case CC_OP_ADDB:
        return compute_c_addb();
    case CC_OP_ADDW:
        return compute_c_addw();
    case CC_OP_ADDL:
        return compute_c_addl();

    case CC_OP_ADCB:
        return compute_c_adcb();
    case CC_OP_ADCW:
        return compute_c_adcw();
    case CC_OP_ADCL:
        return compute_c_adcl();

    case CC_OP_SUBB:
        return compute_c_subb();
    case CC_OP_SUBW:
        return compute_c_subw();
    case CC_OP_SUBL:
        return compute_c_subl();

    case CC_OP_SBBB:
        return compute_c_sbbb();
    case CC_OP_SBBW:
        return compute_c_sbbw();
    case CC_OP_SBBL:
        return compute_c_sbbl();

    case CC_OP_LOGICB:
        return compute_c_logicb();
    case CC_OP_LOGICW:
        return compute_c_logicw();
    case CC_OP_LOGICL:
        return compute_c_logicl();

    case CC_OP_INCB:
        return compute_c_incl();
    case CC_OP_INCW:
        return compute_c_incl();
    case CC_OP_INCL:
        return compute_c_incl();

    case CC_OP_DECB:
        return compute_c_incl();
    case CC_OP_DECW:
        return compute_c_incl();
    case CC_OP_DECL:
        return compute_c_incl();

    case CC_OP_SHLB:
        return compute_c_shlb();
    case CC_OP_SHLW:
        return compute_c_shlw();
    case CC_OP_SHLL:
        return compute_c_shll();

    case CC_OP_SARB:
        return compute_c_sarl();
    case CC_OP_SARW:
        return compute_c_sarl();
    case CC_OP_SARL:
        return compute_c_sarl();

#ifdef TARGET_X86_64
    case CC_OP_MULQ:
        return compute_c_mull();

    case CC_OP_ADDQ:
        return compute_c_addq();

    case CC_OP_ADCQ:
        return compute_c_adcq();

    case CC_OP_SUBQ:
        return compute_c_subq();

    case CC_OP_SBBQ:
        return compute_c_sbbq();

    case CC_OP_LOGICQ:
        return compute_c_logicq();

    case CC_OP_INCQ:
        return compute_c_incl();

    case CC_OP_DECQ:
        return compute_c_incl();

    case CC_OP_SHLQ:
        return compute_c_shlq();

    case CC_OP_SARQ:
        return compute_c_sarl();
#endif
    }
}

void helper_write_eflags(target_ulong t0, uint32_t update_mask)
{
    cpu_load_eflags(env, t0, update_mask);
}

target_ulong helper_read_eflags(void)
{
    uint32_t eflags;

    eflags = helper_cc_compute_all(CC_OP);
    eflags |= (DF & DF_MASK);
    eflags |= env->eflags & ~(VM_MASK | RF_MASK);
    return eflags;
}

void helper_clts(void)
{
    env->cr[0] &= ~CR0_TS_MASK;
    env->hflags &= ~HF_TS_MASK;
}

void helper_reset_rf(void)
{
    env->eflags &= ~RF_MASK;
}

void helper_cli(void)
{
    env->eflags &= ~IF_MASK;
}

void helper_sti(void)
{
    env->eflags |= IF_MASK;
}

#if 0
/* vm86plus instructions */
void helper_cli_vm(void)
{
    env->eflags &= ~VIF_MASK;
}

void helper_sti_vm(void)
{
    env->eflags |= VIF_MASK;
    if (env->eflags & VIP_MASK) {
        raise_exception(env, EXCP0D_GPF);
    }
}
#endif

void helper_set_inhibit_irq(void)
{
    env->hflags |= HF_INHIBIT_IRQ_MASK;
}

void helper_reset_inhibit_irq(void)
{
    env->hflags &= ~HF_INHIBIT_IRQ_MASK;
}
