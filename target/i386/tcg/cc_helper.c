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

target_ulong helper_cc_compute_nz(target_ulong dst, target_ulong src1,
                                  int op)
{
    if (CC_OP_HAS_EFLAGS(op)) {
        return ~src1 & CC_Z;
    } else {
        MemOp size = cc_op_size(op);
        target_ulong mask = MAKE_64BIT_MASK(0, 8 << size);

        return dst & mask;
    }
}

/* NOTE: we compute the flags like the P4. On olders CPUs, only OF and
   CF are modified and it is slower to do that.  Note as well that we
   don't truncate SRC1 for computing carry to DATA_TYPE.  */
static inline uint32_t compute_aco_mul(target_long src1)
{
    uint32_t cf, af, of;

    cf = (src1 != 0);
    af = 0; /* undefined */
    of = cf * CC_O;
    return cf + af + of;
}

target_ulong helper_cc_compute_all(target_ulong dst, target_ulong src1,
                                   target_ulong src2, int op)
{
    uint32_t flags = 0;
    int shift = 0;

    switch (op) {
    default: /* should never happen */
        return 0;

    case CC_OP_EFLAGS:
        return src1;
    case CC_OP_POPCNT:
        return dst ? 0 : CC_Z;
    case CC_OP_SBB_SELF:
	/* dst is either all zeros (--Z-P-) or all ones (-S-APC) */
        return (dst & (CC_Z|CC_A|CC_C|CC_S)) ^ (CC_P | CC_Z);

    case CC_OP_ADCX:
        return compute_all_adcx(dst, src1, src2);
    case CC_OP_ADOX:
        return compute_all_adox(dst, src1, src2);
    case CC_OP_ADCOX:
        return compute_all_adcox(dst, src1, src2);

    case CC_OP_MULB:
        flags = compute_aco_mul(src1);
        goto psz_b;
    case CC_OP_MULW:
        flags = compute_aco_mul(src1);
        goto psz_w;
    case CC_OP_MULL:
        flags = compute_aco_mul(src1);
        goto psz_l;

    case CC_OP_ADDB:
        flags = compute_aco_addb(dst, src1);
        goto psz_b;
    case CC_OP_ADDW:
        flags = compute_aco_addw(dst, src1);
        goto psz_w;
    case CC_OP_ADDL:
        flags = compute_aco_addl(dst, src1);
        goto psz_l;

    case CC_OP_ADCB:
        flags = compute_aco_adcb(dst, src1, src2);
        goto psz_b;
    case CC_OP_ADCW:
        flags = compute_aco_adcw(dst, src1, src2);
        goto psz_w;
    case CC_OP_ADCL:
        flags = compute_aco_adcl(dst, src1, src2);
        goto psz_l;

    case CC_OP_SUBB:
        flags = compute_aco_subb(dst, src1);
        goto psz_b;
    case CC_OP_SUBW:
        flags = compute_aco_subw(dst, src1);
        goto psz_w;
    case CC_OP_SUBL:
        flags = compute_aco_subl(dst, src1);
        goto psz_l;

    case CC_OP_SBBB:
        flags = compute_aco_sbbb(dst, src1, src2);
        goto psz_b;
    case CC_OP_SBBW:
        flags = compute_aco_sbbw(dst, src1, src2);
        goto psz_w;
    case CC_OP_SBBL:
        flags = compute_aco_sbbl(dst, src1, src2);
        goto psz_l;

    case CC_OP_LOGICB:
        flags = 0;
        goto psz_b;
    case CC_OP_LOGICW:
        flags = 0;
        goto psz_w;
    case CC_OP_LOGICL:
        flags = 0;
        goto psz_l;

    case CC_OP_INCB:
        flags = compute_aco_incb(dst, src1);
        goto psz_b;
    case CC_OP_INCW:
        flags = compute_aco_incw(dst, src1);
        goto psz_w;
    case CC_OP_INCL:
        flags = compute_aco_incl(dst, src1);
        goto psz_l;

    case CC_OP_DECB:
        flags = compute_aco_decb(dst, src1);
        goto psz_b;
    case CC_OP_DECW:
        flags = compute_aco_decw(dst, src1);
        goto psz_w;
    case CC_OP_DECL:
        flags = compute_aco_decl(dst, src1);
        goto psz_l;

    case CC_OP_SHLB:
        flags = compute_aco_shlb(dst, src1);
        goto psz_b;
    case CC_OP_SHLW:
        flags = compute_aco_shlw(dst, src1);
        goto psz_w;
    case CC_OP_SHLL:
        flags = compute_aco_shll(dst, src1);
        goto psz_l;

    case CC_OP_SARB:
        flags = compute_aco_sarb(dst, src1);
        goto psz_b;
    case CC_OP_SARW:
        flags = compute_aco_sarw(dst, src1);
        goto psz_w;
    case CC_OP_SARL:
        flags = compute_aco_sarl(dst, src1);
        goto psz_l;

    case CC_OP_BMILGB:
        flags = compute_aco_bmilgb(dst, src1);
        goto psz_b;
    case CC_OP_BMILGW:
        flags = compute_aco_bmilgw(dst, src1);
        goto psz_w;
    case CC_OP_BMILGL:
        flags = compute_aco_bmilgl(dst, src1);
        goto psz_l;

    case CC_OP_BLSIB:
        flags = compute_aco_blsib(dst, src1);
        goto psz_b;
    case CC_OP_BLSIW:
        flags = compute_aco_blsiw(dst, src1);
        goto psz_w;
    case CC_OP_BLSIL:
        flags = compute_aco_blsil(dst, src1);
        goto psz_l;

#ifdef TARGET_X86_64
    case CC_OP_MULQ:
        flags = compute_aco_mul(src1);
        goto psz_q;
    case CC_OP_ADDQ:
        flags = compute_aco_addq(dst, src1);
        goto psz_q;
    case CC_OP_ADCQ:
        flags = compute_aco_adcq(dst, src1, src2);
        goto psz_q;
    case CC_OP_SUBQ:
        flags = compute_aco_subq(dst, src1);
        goto psz_q;
    case CC_OP_SBBQ:
        flags = compute_aco_sbbq(dst, src1, src2);
        goto psz_q;
    case CC_OP_INCQ:
        flags = compute_aco_incq(dst, src1);
        goto psz_q;
    case CC_OP_DECQ:
        flags = compute_aco_decq(dst, src1);
        goto psz_q;
    case CC_OP_LOGICQ:
        flags = 0;
        goto psz_q;
    case CC_OP_SHLQ:
        flags = compute_aco_shlq(dst, src1);
        goto psz_q;
    case CC_OP_SARQ:
        flags = compute_aco_sarq(dst, src1);
        goto psz_q;
    case CC_OP_BMILGQ:
        flags = compute_aco_bmilgq(dst, src1);
        goto psz_q;
    case CC_OP_BLSIQ:
        flags = compute_aco_blsiq(dst, src1);
        goto psz_q;
#endif
    }

psz_b:
    shift += 8;
psz_w:
    shift += 16;
psz_l:
#ifdef TARGET_X86_64
    shift += 32;
psz_q:
#endif

    flags += compute_pf(dst);
    dst <<= shift;
    flags += dst == 0 ? CC_Z : 0;
    flags += (target_long)dst < 0 ? CC_S : 0;
    return flags;
}

uint32_t cpu_cc_compute_all(CPUX86State *env)
{
    return helper_cc_compute_all(CC_DST, CC_SRC, CC_SRC2, CC_OP);
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

    case CC_OP_SBB_SELF:
        return dst & 1;

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

    case CC_OP_BLSIB:
        return compute_c_blsib(dst, src1);
    case CC_OP_BLSIW:
        return compute_c_blsiw(dst, src1);
    case CC_OP_BLSIL:
        return compute_c_blsil(dst, src1);

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
    case CC_OP_BLSIQ:
        return compute_c_blsiq(dst, src1);
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

    CC_SRC = eflags = cpu_cc_compute_all(env);
    CC_OP = CC_OP_EFLAGS;

    eflags |= (env->df & DF_MASK);
    eflags |= env->eflags & ~(VM_MASK | RF_MASK);
    return eflags;
}

void helper_clts(CPUX86State *env)
{
    env->cr[0] &= ~CR0_TS_MASK;
    env->hflags &= ~HF_TS_MASK;
}
