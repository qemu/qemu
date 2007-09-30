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
void glue(do_lsw_le, MEMSUFFIX) (int dst);
void glue(do_stsw, MEMSUFFIX) (int src);
void glue(do_stsw_le, MEMSUFFIX) (int src);
void glue(do_lmw, MEMSUFFIX) (int dst);
void glue(do_lmw_le, MEMSUFFIX) (int dst);
void glue(do_stmw, MEMSUFFIX) (int src);
void glue(do_stmw_le, MEMSUFFIX) (int src);
void glue(do_icbi, MEMSUFFIX) (void);
void glue(do_POWER_lscbx, MEMSUFFIX) (int dest, int ra, int rb);
void glue(do_POWER2_lfq, MEMSUFFIX) (void);
void glue(do_POWER2_lfq_le, MEMSUFFIX) (void);
void glue(do_POWER2_stfq, MEMSUFFIX) (void);
void glue(do_POWER2_stfq_le, MEMSUFFIX) (void);

#if defined(TARGET_PPC64)
void glue(do_lsw_64, MEMSUFFIX) (int dst);
void glue(do_lsw_le_64, MEMSUFFIX) (int dst);
void glue(do_stsw_64, MEMSUFFIX) (int src);
void glue(do_stsw_le_64, MEMSUFFIX) (int src);
void glue(do_lmw_64, MEMSUFFIX) (int dst);
void glue(do_lmw_le_64, MEMSUFFIX) (int dst);
void glue(do_stmw_64, MEMSUFFIX) (int src);
void glue(do_stmw_le_64, MEMSUFFIX) (int src);
void glue(do_icbi_64, MEMSUFFIX) (void);
#endif

#else

void do_print_mem_EA (target_ulong EA);

/* Registers load and stores */
void do_load_cr (void);
void do_store_cr (uint32_t mask);
void do_load_xer (void);
void do_store_xer (void);
#if defined(TARGET_PPC64)
void do_store_pri (int prio);
#endif
void do_load_fpscr (void);
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
void do_sraw (void);
#if defined(TARGET_PPC64)
void do_adde_64 (void);
void do_addmeo_64 (void);
void do_imul64 (uint64_t *tl, uint64_t *th);
void do_mul64 (uint64_t *tl, uint64_t *th);
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
void do_rfi (void);
#if defined(TARGET_PPC64)
void do_rfid (void);
#endif
#if defined(TARGET_PPC64H)
void do_hrfid (void);
#endif
void do_tlbia (void);
void do_tlbie (void);
#if defined(TARGET_PPC64)
void do_tlbie_64 (void);
#endif
void do_load_6xx_tlb (int is_code);
#if defined(TARGET_PPC64)
void do_slbia (void);
void do_slbie (void);
#endif
#endif

/* POWER / PowerPC 601 specific helpers */
void do_store_601_batu (int nr);
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
#endif

/* PowerPC 602 specific helper */
#if !defined(CONFIG_USER_ONLY)
void do_op_602_mfrom (void);
#endif

/* PowerPC 440 specific helpers */
#if !defined(CONFIG_USER_ONLY)
void do_440_tlbre (int word);
void do_440_tlbsx (void);
void do_440_tlbsx_ (void);
void do_440_tlbwe (int word);
#endif

/* PowerPC 4xx specific helpers */
void do_405_check_ov (void);
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
void do_4xx_tlbsx (void);
void do_4xx_tlbsx_ (void);
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

#if defined(TARGET_PPCEMB)
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
#endif /* defined(TARGET_PPCEMB) */

/* Inlined helpers: used in micro-operation as well as helpers */
/* Generic fixed-point helpers */
static inline int _do_cntlzw (uint32_t val)
{
    int cnt = 0;
    if (!(val & 0xFFFF0000UL)) {
        cnt += 16;
        val <<= 16;
    }
    if (!(val & 0xFF000000UL)) {
        cnt += 8;
        val <<= 8;
    }
    if (!(val & 0xF0000000UL)) {
        cnt += 4;
        val <<= 4;
    }
    if (!(val & 0xC0000000UL)) {
        cnt += 2;
        val <<= 2;
    }
    if (!(val & 0x80000000UL)) {
        cnt++;
        val <<= 1;
    }
    if (!(val & 0x80000000UL)) {
        cnt++;
    }
    return cnt;
}

static inline int _do_cntlzd (uint64_t val)
{
    int cnt = 0;
#if HOST_LONG_BITS == 64
    if (!(val & 0xFFFFFFFF00000000ULL)) {
        cnt += 32;
        val <<= 32;
    }
    if (!(val & 0xFFFF000000000000ULL)) {
        cnt += 16;
        val <<= 16;
    }
    if (!(val & 0xFF00000000000000ULL)) {
        cnt += 8;
        val <<= 8;
    }
    if (!(val & 0xF000000000000000ULL)) {
        cnt += 4;
        val <<= 4;
    }
    if (!(val & 0xC000000000000000ULL)) {
        cnt += 2;
        val <<= 2;
    }
    if (!(val & 0x8000000000000000ULL)) {
        cnt++;
        val <<= 1;
    }
    if (!(val & 0x8000000000000000ULL)) {
        cnt++;
    }
#else
    /* Make it easier on 32 bits host machines */
    if (!(val >> 32))
        cnt = _do_cntlzw(val) + 32;
    else
        cnt = _do_cntlzw(val >> 32);
#endif
    return cnt;
}

#if defined(TARGET_PPCEMB)
/* SPE extension */
/* Single precision floating-point helpers */
static inline uint32_t _do_efsabs (uint32_t val)
{
    return val & ~0x80000000;
}
static inline uint32_t _do_efsnabs (uint32_t val)
{
    return val | 0x80000000;
}
static inline uint32_t _do_efsneg (uint32_t val)
{
    return val ^ 0x80000000;
}
static inline uint32_t _do_efsadd (uint32_t op1, uint32_t op2)
{
    union {
        uint32_t u;
        float32 f;
    } u1, u2;
    u1.u = op1;
    u2.u = op2;
    u1.f = float32_add(u1.f, u2.f, &env->spe_status);
    return u1.u;
}
static inline uint32_t _do_efssub (uint32_t op1, uint32_t op2)
{
    union {
        uint32_t u;
        float32 f;
    } u1, u2;
    u1.u = op1;
    u2.u = op2;
    u1.f = float32_sub(u1.f, u2.f, &env->spe_status);
    return u1.u;
}
static inline uint32_t _do_efsmul (uint32_t op1, uint32_t op2)
{
    union {
        uint32_t u;
        float32 f;
    } u1, u2;
    u1.u = op1;
    u2.u = op2;
    u1.f = float32_mul(u1.f, u2.f, &env->spe_status);
    return u1.u;
}
static inline uint32_t _do_efsdiv (uint32_t op1, uint32_t op2)
{
    union {
        uint32_t u;
        float32 f;
    } u1, u2;
    u1.u = op1;
    u2.u = op2;
    u1.f = float32_div(u1.f, u2.f, &env->spe_status);
    return u1.u;
}

static inline int _do_efststlt (uint32_t op1, uint32_t op2)
{
    union {
        uint32_t u;
        float32 f;
    } u1, u2;
    u1.u = op1;
    u2.u = op2;
    return float32_lt(u1.f, u2.f, &env->spe_status) ? 1 : 0;
}
static inline int _do_efststgt (uint32_t op1, uint32_t op2)
{
    union {
        uint32_t u;
        float32 f;
    } u1, u2;
    u1.u = op1;
    u2.u = op2;
    return float32_le(u1.f, u2.f, &env->spe_status) ? 0 : 1;
}
static inline int _do_efststeq (uint32_t op1, uint32_t op2)
{
    union {
        uint32_t u;
        float32 f;
    } u1, u2;
    u1.u = op1;
    u2.u = op2;
    return float32_eq(u1.f, u2.f, &env->spe_status) ? 1 : 0;
}
/* Double precision floating-point helpers */
static inline int _do_efdtstlt (uint64_t op1, uint64_t op2)
{
    union {
        uint64_t u;
        float64 f;
    } u1, u2;
    u1.u = op1;
    u2.u = op2;
    return float64_lt(u1.f, u2.f, &env->spe_status) ? 1 : 0;
}
static inline int _do_efdtstgt (uint64_t op1, uint64_t op2)
{
    union {
        uint64_t u;
        float64 f;
    } u1, u2;
    u1.u = op1;
    u2.u = op2;
    return float64_le(u1.f, u2.f, &env->spe_status) ? 0 : 1;
}
static inline int _do_efdtsteq (uint64_t op1, uint64_t op2)
{
    union {
        uint64_t u;
        float64 f;
    } u1, u2;
    u1.u = op1;
    u2.u = op2;
    return float64_eq(u1.f, u2.f, &env->spe_status) ? 1 : 0;
}
#endif /* defined(TARGET_PPCEMB) */
#endif
