/*
 *  x86 condition code helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "helper-tcg.h"

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
#include "cc_helper_template.h.inc"
#undef SHIFT

#define SHIFT 1
#include "cc_helper_template.h.inc"
#undef SHIFT

#define SHIFT 2
#include "cc_helper_template.h.inc"
#undef SHIFT

#ifdef TARGET_X86_64

#define SHIFT 3
#include "cc_helper_template.h.inc"
#undef SHIFT

#endif

static target_ulong compute_all_adcx(target_ulong dst, target_ulong src1,
                                     target_ulong src2)
{
    return (src1 & ~CC_C) | (dst * CC_C);
}

static target_ulong compute_all_adox(target_ulong dst, target_ulong src1,
                                     target_ulong src2)
{
    return (src1 & ~CC_O) | (src2 * CC_O);
}

static target_ulong compute_all_adcox(target_ulong dst, target_ulong src1,
                                      target_ulong src2)
{
    return (src1 & ~(CC_C | CC_O)) | (dst * CC_C) | (src2 * CC_O);
}

target_ulong helper_cc_compute_all(target_ulong dst, target_ulong src1,
                                   target_ulong src2, int op)
{
    switch (op) {
    default: /* should never happen */
        return 0;

    case CC_OP_EFLAGS:
        return src1;
    case CC_OP_CLR:
        return CC_Z | CC_P;
    case CC_OP_POPCNT:
        return src1 ? 0 : CC_Z;

    case CC_OP_MULB:
        return compute_all_mulb(dst, src1);
    case CC_OP_MULW:
        return compute_all_mulw(dst, src1);
    case CC_OP_MULL:
        return compute_all_mull(dst, src1);

    case CC_OP_ADDB:
        return compute_all_addb(dst, src1);
    case CC_OP_ADDW:
        return compute_all_addw(dst, src1);
    case CC_OP_ADDL:
        return compute_all_addl(dst, src1);

    case CC_OP_ADCB:
        return compute_all_adcb(dst, src1, src2);
    case CC_OP_ADCW:
        return compute_all_adcw(dst, src1, src2);
    case CC_OP_ADCL:
        return compute_all_adcl(dst, src1, src2);

    case CC_OP_SUBB:
        return compute_all_subb(dst, src1);
    case CC_OP_SUBW:
        return compute_all_subw(dst, src1);
    case CC_OP_SUBL:
        return compute_all_subl(dst, src1);

    case CC_OP_SBBB:
        return compute_all_sbbb(dst, src1, src2);
    case CC_OP_SBBW:
        return compute_all_sbbw(dst, src1, src2);
    case CC_OP_SBBL:
        return compute_all_sbbl(dst, src1, src2);

    case CC_OP_LOGICB:
        return compute_all_logicb(dst, src1);
    case CC_OP_LOGICW:
        return compute_all_logicw(dst, src1);
    case CC_OP_LOGICL:
        return compute_all_logicl(dst, src1);

    case CC_OP_INCB:
        return compute_all_incb(dst, src1);
    case CC_OP_INCW:
        return compute_all_incw(dst, src1);
    case CC_OP_INCL:
        return compute_all_incl(dst, src1);

    case CC_OP_DECB:
        return compute_all_decb(dst, src1);
    case CC_OP_DECW:
        return compute_all_decw(dst, src1);
    case CC_OP_DECL:
        return compute_all_decl(dst, src1);

    case CC_OP_SHLB:
        return compute_all_shlb(dst, src1);
    case CC_OP_SHLW:
        return compute_all_shlw(dst, src1);
    case CC_OP_SHLL:
        return compute_all_shll(dst, src1);

    case CC_OP_SARB:
        return compute_all_sarb(dst, src1);
    case CC_OP_SARW:
        return compute_all_sarw(dst, src1);
    case CC_OP_SARL:
        return compute_all_sarl(dst, src1);

    case CC_OP_BMILGB:
        return compute_all_bmilgb(dst, src1);
    case CC_OP_BMILGW:
        return compute_all_bmilgw(dst, src1);
    case CC_OP_BMILGL:
        return compute_all_bmilgl(dst, src1);

    case CC_OP_ADCX:
        return compute_all_adcx(dst, src1, src2);
    case CC_OP_ADOX:
        return compute_all_adox(dst, src1, src2);
    case CC_OP_ADCOX:
        return compute_all_adcox(dst, src1, src2);

#ifdef TARGET_X86_64
    case CC_OP_MULQ:
        return compute_all_mulq(dst, src1);
    case CC_OP_ADDQ:
        return compute_all_addq(dst, src1);
    case CC_OP_ADCQ:
        return compute_all_adcq(dst, src1, src2);
    case CC_OP_SUBQ:
        return compute_all_subq(dst, src1);
    case CC_OP_SBBQ:
        return compute_all_sbbq(dst, src1, src2);
    case CC_OP_LOGICQ:
        return compute_all_logicq(dst, src1);
    case CC_OP_INCQ:
        return compute_all_incq(dst, src1);
    case CC_OP_DECQ:
        return compute_all_decq(dst, src1);
    case CC_OP_SHLQ:
        return compute_all_shlq(dst, src1);
    case CC_OP_SARQ:
        return compute_all_sarq(dst, src1);
    case CC_OP_BMILGQ:
        return compute_all_bmilgq(dst, src1);
#endif
    }
}

uint32_t cpu_cc_compute_all(CPUX86State *env, int op)
{
    return helper_cc_compute_all(CC_DST, CC_SRC, CC_SRC2, op);
}

target_ulong helper_cc_compute_c(target_ulong dst, target_ulong src1,
                                 target_ulong src2, int op)
{
    switch (op) {
    default: /* should never happen */
    case CC_OP_LOGICB:
    case CC_OP_LOGICW:
    case CC_OP_LOGICL:
    case CC_OP_LOGICQ:
    case CC_OP_CLR:
    case CC_OP_POPCNT:
        return 0;

    case CC_OP_EFLAGS:
    case CC_OP_SARB:
    case CC_OP_SARW:
    case CC_OP_SARL:
    case CC_OP_SARQ:
    case CC_OP_ADOX:
        return src1 & 1;

    case CC_OP_INCB:
    case CC_OP_INCW:
    case CC_OP_INCL:
    case CC_OP_INCQ:
    case CC_OP_DECB:
    case CC_OP_DECW:
    case CC_OP_DECL:
    case CC_OP_DECQ:
        return src1;

    case CC_OP_MULB:
    case CC_OP_MULW:
    case CC_OP_MULL:
    case CC_OP_MULQ:
        return src1 != 0;

    case CC_OP_ADCX:
    case CC_OP_ADCOX:
        return dst;

    case CC_OP_ADDB:
        return compute_c_addb(dst, src1);
    case CC_OP_ADDW:
        return compute_c_addw(dst, src1);
    case CC_OP_ADDL:
        return compute_c_addl(dst, src1);

    case CC_OP_ADCB:
        return compute_c_adcb(dst, src1, src2);
    case CC_OP_ADCW:
        return compute_c_adcw(dst, src1, src2);
    case CC_OP_ADCL:
        return compute_c_adcl(dst, src1, src2);

    case CC_OP_SUBB:
        return compute_c_subb(dst, src1);
    case CC_OP_SUBW:
        return compute_c_subw(dst, src1);
    case CC_OP_SUBL:
        return compute_c_subl(dst, src1);

    case CC_OP_SBBB:
        return compute_c_sbbb(dst, src1, src2);
    case CC_OP_SBBW:
        return compute_c_sbbw(dst, src1, src2);
    case CC_OP_SBBL:
        return compute_c_sbbl(dst, src1, src2);

    case CC_OP_SHLB:
        return compute_c_shlb(dst, src1);
    case CC_OP_SHLW:
        return compute_c_shlw(dst, src1);
    case CC_OP_SHLL:
        return compute_c_shll(dst, src1);

    case CC_OP_BMILGB:
        return compute_c_bmilgb(dst, src1);
    case CC_OP_BMILGW:
        return compute_c_bmilgw(dst, src1);
    case CC_OP_BMILGL:
        return compute_c_bmilgl(dst, src1);

#ifdef TARGET_X86_64
    case CC_OP_ADDQ:
        return compute_c_addq(dst, src1);
    case CC_OP_ADCQ:
        return compute_c_adcq(dst, src1, src2);
    case CC_OP_SUBQ:
        return compute_c_subq(dst, src1);
    case CC_OP_SBBQ:
        return compute_c_sbbq(dst, src1, src2);
    case CC_OP_SHLQ:
        return compute_c_shlq(dst, src1);
    case CC_OP_BMILGQ:
        return compute_c_bmilgq(dst, src1);
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

    eflags = cpu_cc_compute_all(env, CC_OP);
    eflags |= (env->df & DF_MASK);
    eflags |= env->eflags & ~(VM_MASK | RF_MASK);
    return eflags;
}

void helper_clts(CPUX86State *env)
{
    env->cr[0] &= ~CR0_TS_MASK;
    env->hflags &= ~HF_TS_MASK;
}
