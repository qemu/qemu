/*
 *  PowerPC emulation helpers header for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if defined(MEMSUFFIX)

/* Memory load/store helpers */
void glue(do_lsw, MEMSUFFIX) (int dst);
void glue(do_stsw, MEMSUFFIX) (int src);
void glue(do_lmw, MEMSUFFIX) (int dst);
void glue(do_lmw_le, MEMSUFFIX) (int dst);
void glue(do_stmw, MEMSUFFIX) (int src);
void glue(do_stmw_le, MEMSUFFIX) (int src);
void glue(do_icbi, MEMSUFFIX) (void);
void glue(do_dcbz, MEMSUFFIX) (void);
void glue(do_POWER_lscbx, MEMSUFFIX) (int dest, int ra, int rb);
void glue(do_POWER2_lfq, MEMSUFFIX) (void);
void glue(do_POWER2_lfq_le, MEMSUFFIX) (void);
void glue(do_POWER2_stfq, MEMSUFFIX) (void);
void glue(do_POWER2_stfq_le, MEMSUFFIX) (void);

#if defined(TARGET_PPC64)
void glue(do_lsw_64, MEMSUFFIX) (int dst);
void glue(do_stsw_64, MEMSUFFIX) (int src);
void glue(do_lmw_64, MEMSUFFIX) (int dst);
void glue(do_lmw_le_64, MEMSUFFIX) (int dst);
void glue(do_stmw_64, MEMSUFFIX) (int src);
void glue(do_stmw_le_64, MEMSUFFIX) (int src);
void glue(do_icbi_64, MEMSUFFIX) (void);
void glue(do_dcbz_64, MEMSUFFIX) (void);
#endif

#else

void do_print_mem_EA (target_ulong EA);

/* Registers load and stores */
void do_load_cr (void);
void do_store_cr (uint32_t mask);
#if defined(TARGET_PPC64)
void do_store_pri (int prio);
#endif
void do_fpscr_setbit (int bit);
void do_store_fpscr (uint32_t mask);
target_ulong ppc_load_dump_spr (int sprn);
void ppc_store_dump_spr (int sprn, target_ulong val);

/* Integer arithmetic helpers */
void do_adde (void);
void do_addmeo (void);
void do_divwo (void);
void do_divwuo (void);
void do_mullwo (void);
void do_nego (void);
void do_subfe (void);
void do_subfmeo (void);
void do_subfzeo (void);
void do_cntlzw (void);
#if defined(TARGET_PPC64)
void do_cntlzd (void);
#endif
void do_sraw (void);
#if defined(TARGET_PPC64)
void do_adde_64 (void);
void do_addmeo_64 (void);
void do_divdo (void);
void do_divduo (void);
void do_mulldo (void);
void do_nego_64 (void);
void do_subfe_64 (void);
void do_subfmeo_64 (void);
void do_subfzeo_64 (void);
void do_srad (void);
#endif
void do_popcntb (void);
#if defined(TARGET_PPC64)
void do_popcntb_64 (void);
#endif

/* Floating-point arithmetic helpers */
void do_compute_fprf (int set_class);
#ifdef CONFIG_SOFTFLOAT
void do_float_check_status (void);
#endif
#if USE_PRECISE_EMULATION
void do_fadd (void);
void do_fsub (void);
void do_fmul (void);
void do_fdiv (void);
#endif
void do_fsqrt (void);
void do_fre (void);
void do_fres (void);
void do_frsqrte (void);
void do_fsel (void);
#if USE_PRECISE_EMULATION
void do_fmadd (void);
void do_fmsub (void);
#endif
void do_fnmadd (void);
void do_fnmsub (void);
#if USE_PRECISE_EMULATION
void do_frsp (void);
#endif
void do_fctiw (void);
void do_fctiwz (void);
#if defined(TARGET_PPC64)
void do_fcfid (void);
void do_fctid (void);
void do_fctidz (void);
#endif
void do_frin (void);
void do_friz (void);
void do_frip (void);
void do_frim (void);
void do_fcmpu (void);
void do_fcmpo (void);

/* Misc */
void do_tw (int flags);
#if defined(TARGET_PPC64)
void do_td (int flags);
#endif
#if !defined(CONFIG_USER_ONLY)
void do_store_msr (void);
void do_rfi (void);
#if defined(TARGET_PPC64)
void do_rfid (void);
void do_hrfid (void);
#endif
void do_load_6xx_tlb (int is_code);
void do_load_74xx_tlb (int is_code);
#endif

/* POWER / PowerPC 601 specific helpers */
void do_POWER_abso (void);
void do_POWER_clcs (void);
void do_POWER_div (void);
void do_POWER_divo (void);
void do_POWER_divs (void);
void do_POWER_divso (void);
void do_POWER_dozo (void);
void do_POWER_maskg (void);
void do_POWER_mulo (void);
#if !defined(CONFIG_USER_ONLY)
void do_POWER_rac (void);
void do_POWER_rfsvc (void);
void do_store_hid0_601 (void);
#endif

/* PowerPC 602 specific helper */
#if !defined(CONFIG_USER_ONLY)
void do_op_602_mfrom (void);
#endif

/* PowerPC 440 specific helpers */
#if !defined(CONFIG_USER_ONLY)
void do_440_tlbre (int word);
void do_440_tlbwe (int word);
#endif

/* PowerPC 4xx specific helpers */
void do_405_check_sat (void);
void do_load_dcr (void);
void do_store_dcr (void);
#if !defined(CONFIG_USER_ONLY)
void do_40x_rfci (void);
void do_rfci (void);
void do_rfdi (void);
void do_rfmci (void);
void do_4xx_tlbre_lo (void);
void do_4xx_tlbre_hi (void);
void do_4xx_tlbwe_lo (void);
void do_4xx_tlbwe_hi (void);
#endif

/* PowerPC 440 specific helpers */
void do_440_dlmzb (void);

/* PowerPC 403 specific helpers */
#if !defined(CONFIG_USER_ONLY)
void do_load_403_pb (int num);
void do_store_403_pb (int num);
#endif

/* SPE extension helpers */
void do_brinc (void);
/* Fixed-point vector helpers */
void do_evabs (void);
void do_evaddw (void);
void do_evcntlsw (void);
void do_evcntlzw (void);
void do_evneg (void);
void do_evrlw (void);
void do_evsel (void);
void do_evrndw (void);
void do_evslw (void);
void do_evsrws (void);
void do_evsrwu (void);
void do_evsubfw (void);
void do_evcmpeq (void);
void do_evcmpgts (void);
void do_evcmpgtu (void);
void do_evcmplts (void);
void do_evcmpltu (void);

/* Single precision floating-point helpers */
void do_efscmplt (void);
void do_efscmpgt (void);
void do_efscmpeq (void);
void do_efscfsf (void);
void do_efscfuf (void);
void do_efsctsf (void);
void do_efsctuf (void);

void do_efscfsi (void);
void do_efscfui (void);
void do_efsctsi (void);
void do_efsctui (void);
void do_efsctsiz (void);
void do_efsctuiz (void);

/* Double precision floating-point helpers */
void do_efdcmplt (void);
void do_efdcmpgt (void);
void do_efdcmpeq (void);
void do_efdcfsf (void);
void do_efdcfuf (void);
void do_efdctsf (void);
void do_efdctuf (void);

void do_efdcfsi (void);
void do_efdcfui (void);
void do_efdctsi (void);
void do_efdctui (void);
void do_efdctsiz (void);
void do_efdctuiz (void);

void do_efdcfs (void);
void do_efscfd (void);

/* Floating-point vector helpers */
void do_evfsabs (void);
void do_evfsnabs (void);
void do_evfsneg (void);
void do_evfsadd (void);
void do_evfssub (void);
void do_evfsmul (void);
void do_evfsdiv (void);
void do_evfscmplt (void);
void do_evfscmpgt (void);
void do_evfscmpeq (void);
void do_evfststlt (void);
void do_evfststgt (void);
void do_evfststeq (void);
void do_evfscfsi (void);
void do_evfscfui (void);
void do_evfscfsf (void);
void do_evfscfuf (void);
void do_evfsctsf (void);
void do_evfsctuf (void);
void do_evfsctsi (void);
void do_evfsctui (void);
void do_evfsctsiz (void);
void do_evfsctuiz (void);

/* SPE extension */
/* Single precision floating-point helpers */
static always_inline uint32_t _do_efsabs (uint32_t val)
{
    return val & ~0x80000000;
}
static always_inline uint32_t _do_efsnabs (uint32_t val)
{
    return val | 0x80000000;
}
static always_inline uint32_t _do_efsneg (uint32_t val)
{
    return val ^ 0x80000000;
}
static always_inline uint32_t _do_efsadd (uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;
    u1.l = op1;
    u2.l = op2;
    u1.f = float32_add(u1.f, u2.f, &env->spe_status);
    return u1.l;
}
static always_inline uint32_t _do_efssub (uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;
    u1.l = op1;
    u2.l = op2;
    u1.f = float32_sub(u1.f, u2.f, &env->spe_status);
    return u1.l;
}
static always_inline uint32_t _do_efsmul (uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;
    u1.l = op1;
    u2.l = op2;
    u1.f = float32_mul(u1.f, u2.f, &env->spe_status);
    return u1.l;
}
static always_inline uint32_t _do_efsdiv (uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;
    u1.l = op1;
    u2.l = op2;
    u1.f = float32_div(u1.f, u2.f, &env->spe_status);
    return u1.l;
}

static always_inline int _do_efststlt (uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;
    u1.l = op1;
    u2.l = op2;
    return float32_lt(u1.f, u2.f, &env->spe_status) ? 4 : 0;
}
static always_inline int _do_efststgt (uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;
    u1.l = op1;
    u2.l = op2;
    return float32_le(u1.f, u2.f, &env->spe_status) ? 0 : 4;
}
static always_inline int _do_efststeq (uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;
    u1.l = op1;
    u2.l = op2;
    return float32_eq(u1.f, u2.f, &env->spe_status) ? 4 : 0;
}
/* Double precision floating-point helpers */
static always_inline int _do_efdtstlt (uint64_t op1, uint64_t op2)
{
    CPU_DoubleU u1, u2;
    u1.ll = op1;
    u2.ll = op2;
    return float64_lt(u1.d, u2.d, &env->spe_status) ? 4 : 0;
}
static always_inline int _do_efdtstgt (uint64_t op1, uint64_t op2)
{
    CPU_DoubleU u1, u2;
    u1.ll = op1;
    u2.ll = op2;
    return float64_le(u1.d, u2.d, &env->spe_status) ? 0 : 4;
}
static always_inline int _do_efdtsteq (uint64_t op1, uint64_t op2)
{
    CPU_DoubleU u1, u2;
    u1.ll = op1;
    u2.ll = op2;
    return float64_eq(u1.d, u2.d, &env->spe_status) ? 4 : 0;
}
#endif
