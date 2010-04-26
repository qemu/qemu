/*
 *  Alpha emulation - PALcode emulation for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "cpu.h"
#include "exec-all.h"

/* Shared handlers */
static void pal_reset (CPUState *env);
/* Console handlers */
static void pal_console_call (CPUState *env, uint32_t palcode);
/* OpenVMS handlers */
static void pal_openvms_call (CPUState *env, uint32_t palcode);
/* UNIX / Linux handlers */
static void pal_unix_call (CPUState *env, uint32_t palcode);

pal_handler_t pal_handlers[] = {
    /* Console handler */
    {
        .reset = &pal_reset,
        .call_pal = &pal_console_call,
    },
    /* OpenVMS handler */
    {
        .reset = &pal_reset,
        .call_pal = &pal_openvms_call,
    },
    /* UNIX / Linux handler */
    {
        .reset = &pal_reset,
        .call_pal = &pal_unix_call,
    },
};

#if 0
/* One must explicitly check that the TB is valid and the FOE bit is reset */
static void update_itb (void)
{
    /* This writes into a temp register, not the actual one */
    mtpr(TB_TAG);
    mtpr(TB_CTL);
    /* This commits the TB update */
    mtpr(ITB_PTE);
}

static void update_dtb (void);
{
    mtpr(TB_CTL);
    /* This write into a temp register, not the actual one */
    mtpr(TB_TAG);
    /* This commits the TB update */
    mtpr(DTB_PTE);
}
#endif

static void pal_reset (CPUState *env)
{
}

static void do_swappal (CPUState *env, uint64_t palid)
{
    pal_handler_t *pal_handler;

    switch (palid) {
    case 0 ... 2:
        pal_handler = &pal_handlers[palid];
        env->pal_handler = pal_handler;
        env->ipr[IPR_PAL_BASE] = -1ULL;
        (*pal_handler->reset)(env);
        break;
    case 3 ... 255:
        /* Unknown identifier */
        env->ir[0] = 1;
        return;
    default:
        /* We were given the entry point address */
        env->pal_handler = NULL;
        env->ipr[IPR_PAL_BASE] = palid;
        env->pc = env->ipr[IPR_PAL_BASE];
        cpu_loop_exit();
    }
}

static void pal_console_call (CPUState *env, uint32_t palcode)
{
    uint64_t palid;

    if (palcode < 0x00000080) {
        /* Privileged palcodes */
        if (!(env->ps >> 3)) {
            /* TODO: generate privilege exception */
        }
    }
    switch (palcode) {
    case 0x00000000:
        /* HALT */
        /* REQUIRED */
        break;
    case 0x00000001:
        /* CFLUSH */
        break;
    case 0x00000002:
        /* DRAINA */
        /* REQUIRED */
        /* Implemented as no-op */
        break;
    case 0x00000009:
        /* CSERVE */
        /* REQUIRED */
        break;
    case 0x0000000A:
        /* SWPPAL */
        /* REQUIRED */
        palid = env->ir[16];
        do_swappal(env, palid);
        break;
    case 0x00000080:
        /* BPT */
        /* REQUIRED */
        break;
    case 0x00000081:
        /* BUGCHK */
        /* REQUIRED */
        break;
    case 0x00000086:
        /* IMB */
        /* REQUIRED */
        /* Implemented as no-op */
        break;
    case 0x0000009E:
        /* RDUNIQUE */
        /* REQUIRED */
        break;
    case 0x0000009F:
        /* WRUNIQUE */
        /* REQUIRED */
        break;
    case 0x000000AA:
        /* GENTRAP */
        /* REQUIRED */
        break;
    default:
        break;
    }
}

static void pal_openvms_call (CPUState *env, uint32_t palcode)
{
    uint64_t palid, val, oldval;

    if (palcode < 0x00000080) {
        /* Privileged palcodes */
        if (!(env->ps >> 3)) {
            /* TODO: generate privilege exception */
        }
    }
    switch (palcode) {
    case 0x00000000:
        /* HALT */
        /* REQUIRED */
        break;
    case 0x00000001:
        /* CFLUSH */
        break;
    case 0x00000002:
        /* DRAINA */
        /* REQUIRED */
        /* Implemented as no-op */
        break;
    case 0x00000003:
        /* LDQP */
        break;
    case 0x00000004:
        /* STQP */
        break;
    case 0x00000005:
        /* SWPCTX */
        break;
    case 0x00000006:
        /* MFPR_ASN */
        if (cpu_alpha_mfpr(env, IPR_ASN, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000007:
        /* MTPR_ASTEN */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_ASTEN, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000008:
        /* MTPR_ASTSR */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_ASTSR, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000009:
        /* CSERVE */
        /* REQUIRED */
        break;
    case 0x0000000A:
        /* SWPPAL */
        /* REQUIRED */
        palid = env->ir[16];
        do_swappal(env, palid);
        break;
    case 0x0000000B:
        /* MFPR_FEN */
        if (cpu_alpha_mfpr(env, IPR_FEN, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x0000000C:
        /* MTPR_FEN */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_FEN, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000000D:
        /* MTPR_IPIR */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_IPIR, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000000E:
        /* MFPR_IPL */
        if (cpu_alpha_mfpr(env, IPR_IPL, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x0000000F:
        /* MTPR_IPL */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_IPL, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000010:
        /* MFPR_MCES */
        if (cpu_alpha_mfpr(env, IPR_MCES, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000011:
        /* MTPR_MCES */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_MCES, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000012:
        /* MFPR_PCBB */
        if (cpu_alpha_mfpr(env, IPR_PCBB, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000013:
        /* MFPR_PRBR */
        if (cpu_alpha_mfpr(env, IPR_PRBR, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000014:
        /* MTPR_PRBR */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_PRBR, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000015:
        /* MFPR_PTBR */
        if (cpu_alpha_mfpr(env, IPR_PTBR, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000016:
        /* MFPR_SCBB */
        if (cpu_alpha_mfpr(env, IPR_SCBB, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000017:
        /* MTPR_SCBB */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_SCBB, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000018:
        /* MTPR_SIRR */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_SIRR, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000019:
        /* MFPR_SISR */
        if (cpu_alpha_mfpr(env, IPR_SISR, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x0000001A:
        /* MFPR_TBCHK */
        if (cpu_alpha_mfpr(env, IPR_TBCHK, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x0000001B:
        /* MTPR_TBIA */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_TBIA, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000001C:
        /* MTPR_TBIAP */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_TBIAP, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000001D:
        /* MTPR_TBIS */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_TBIS, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000001E:
        /* MFPR_ESP */
        if (cpu_alpha_mfpr(env, IPR_ESP, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x0000001F:
        /* MTPR_ESP */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_ESP, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000020:
        /* MFPR_SSP */
        if (cpu_alpha_mfpr(env, IPR_SSP, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000021:
        /* MTPR_SSP */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_SSP, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000022:
        /* MFPR_USP */
        if (cpu_alpha_mfpr(env, IPR_USP, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000023:
        /* MTPR_USP */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_USP, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000024:
        /* MTPR_TBISD */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_TBISD, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000025:
        /* MTPR_TBISI */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_TBISI, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000026:
        /* MFPR_ASTEN */
        if (cpu_alpha_mfpr(env, IPR_ASTEN, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000027:
        /* MFPR_ASTSR */
        if (cpu_alpha_mfpr(env, IPR_ASTSR, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000029:
        /* MFPR_VPTB */
        if (cpu_alpha_mfpr(env, IPR_VPTB, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x0000002A:
        /* MTPR_VPTB */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_VPTB, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000002B:
        /* MTPR_PERFMON */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_PERFMON, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000002E:
        /* MTPR_DATFX */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_DATFX, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000003E:
        /* WTINT */
        break;
    case 0x0000003F:
        /* MFPR_WHAMI */
        if (cpu_alpha_mfpr(env, IPR_WHAMI, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000080:
        /* BPT */
        /* REQUIRED */
        break;
    case 0x00000081:
        /* BUGCHK */
        /* REQUIRED */
        break;
    case 0x00000082:
        /* CHME */
        break;
    case 0x00000083:
        /* CHMK */
        break;
    case 0x00000084:
        /* CHMS */
        break;
    case 0x00000085:
        /* CHMU */
        break;
    case 0x00000086:
        /* IMB */
        /* REQUIRED */
        /* Implemented as no-op */
        break;
    case 0x00000087:
        /* INSQHIL */
        break;
    case 0x00000088:
        /* INSQTIL */
        break;
    case 0x00000089:
        /* INSQHIQ */
        break;
    case 0x0000008A:
        /* INSQTIQ */
        break;
    case 0x0000008B:
        /* INSQUEL */
        break;
    case 0x0000008C:
        /* INSQUEQ */
        break;
    case 0x0000008D:
        /* INSQUEL/D */
        break;
    case 0x0000008E:
        /* INSQUEQ/D */
        break;
    case 0x0000008F:
        /* PROBER */
        break;
    case 0x00000090:
        /* PROBEW */
        break;
    case 0x00000091:
        /* RD_PS */
        break;
    case 0x00000092:
        /* REI */
        break;
    case 0x00000093:
        /* REMQHIL */
        break;
    case 0x00000094:
        /* REMQTIL */
        break;
    case 0x00000095:
        /* REMQHIQ */
        break;
    case 0x00000096:
        /* REMQTIQ */
        break;
    case 0x00000097:
        /* REMQUEL */
        break;
    case 0x00000098:
        /* REMQUEQ */
        break;
    case 0x00000099:
        /* REMQUEL/D */
        break;
    case 0x0000009A:
        /* REMQUEQ/D */
        break;
    case 0x0000009B:
        /* SWASTEN */
        break;
    case 0x0000009C:
        /* WR_PS_SW */
        break;
    case 0x0000009D:
        /* RSCC */
        break;
    case 0x0000009E:
        /* READ_UNQ */
        /* REQUIRED */
        break;
    case 0x0000009F:
        /* WRITE_UNQ */
        /* REQUIRED */
        break;
    case 0x000000A0:
        /* AMOVRR */
        break;
    case 0x000000A1:
        /* AMOVRM */
        break;
    case 0x000000A2:
        /* INSQHILR */
        break;
    case 0x000000A3:
        /* INSQTILR */
        break;
    case 0x000000A4:
        /* INSQHIQR */
        break;
    case 0x000000A5:
        /* INSQTIQR */
        break;
    case 0x000000A6:
        /* REMQHILR */
        break;
    case 0x000000A7:
        /* REMQTILR */
        break;
    case 0x000000A8:
        /* REMQHIQR */
        break;
    case 0x000000A9:
        /* REMQTIQR */
        break;
    case 0x000000AA:
        /* GENTRAP */
        /* REQUIRED */
        break;
    case 0x000000AE:
        /* CLRFEN */
        break;
    default:
        break;
    }
}

static void pal_unix_call (CPUState *env, uint32_t palcode)
{
    uint64_t palid, val, oldval;

    if (palcode < 0x00000080) {
        /* Privileged palcodes */
        if (!(env->ps >> 3)) {
            /* TODO: generate privilege exception */
        }
    }
    switch (palcode) {
    case 0x00000000:
        /* HALT */
        /* REQUIRED */
        break;
    case 0x00000001:
        /* CFLUSH */
        break;
    case 0x00000002:
        /* DRAINA */
        /* REQUIRED */
        /* Implemented as no-op */
        break;
    case 0x00000009:
        /* CSERVE */
        /* REQUIRED */
        break;
    case 0x0000000A:
        /* SWPPAL */
        /* REQUIRED */
        palid = env->ir[16];
        do_swappal(env, palid);
        break;
    case 0x0000000D:
        /* WRIPIR */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_IPIR, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000010:
        /* RDMCES */
        if (cpu_alpha_mfpr(env, IPR_MCES, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000011:
        /* WRMCES */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_MCES, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000002B:
        /* WRFEN */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_PERFMON, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000002D:
        /* WRVPTPTR */
        break;
    case 0x00000030:
        /* SWPCTX */
        break;
    case 0x00000031:
        /* WRVAL */
        break;
    case 0x00000032:
        /* RDVAL */
        break;
    case 0x00000033:
        /* TBI */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_TBIS, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000034:
        /* WRENT */
        break;
    case 0x00000035:
        /* SWPIPL */
        break;
    case 0x00000036:
        /* RDPS */
        break;
    case 0x00000037:
        /* WRKGP */
        break;
    case 0x00000038:
        /* WRUSP */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_USP, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x00000039:
        /* WRPERFMON */
        val = env->ir[16];
        if (cpu_alpha_mtpr(env, IPR_PERFMON, val, &oldval) == 1)
            env->ir[0] = val;
        break;
    case 0x0000003A:
        /* RDUSP */
        if (cpu_alpha_mfpr(env, IPR_USP, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x0000003C:
        /* WHAMI */
        if (cpu_alpha_mfpr(env, IPR_WHAMI, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x0000003D:
        /* RETSYS */
        break;
    case 0x0000003E:
        /* WTINT */
        break;
    case 0x0000003F:
        /* RTI */
        if (cpu_alpha_mfpr(env, IPR_WHAMI, &val) == 0)
            env->ir[0] = val;
        break;
    case 0x00000080:
        /* BPT */
        /* REQUIRED */
        break;
    case 0x00000081:
        /* BUGCHK */
        /* REQUIRED */
        break;
    case 0x00000083:
        /* CALLSYS */
        break;
    case 0x00000086:
        /* IMB */
        /* REQUIRED */
        /* Implemented as no-op */
        break;
    case 0x00000092:
        /* URTI */
        break;
    case 0x0000009E:
        /* RDUNIQUE */
        /* REQUIRED */
        break;
    case 0x0000009F:
        /* WRUNIQUE */
        /* REQUIRED */
        break;
    case 0x000000AA:
        /* GENTRAP */
        /* REQUIRED */
        break;
    case 0x000000AE:
        /* CLRFEN */
        break;
    default:
        break;
    }
}

void call_pal (CPUState *env)
{
    pal_handler_t *pal_handler = env->pal_handler;

    switch (env->exception_index) {
    case EXCP_RESET:
        (*pal_handler->reset)(env);
        break;
    case EXCP_MCHK:
        (*pal_handler->machine_check)(env);
        break;
    case EXCP_ARITH:
        (*pal_handler->arithmetic)(env);
        break;
    case EXCP_INTERRUPT:
        (*pal_handler->interrupt)(env);
        break;
    case EXCP_DFAULT:
        (*pal_handler->dfault)(env);
        break;
    case EXCP_DTB_MISS_PAL:
        (*pal_handler->dtb_miss_pal)(env);
        break;
    case EXCP_DTB_MISS_NATIVE:
        (*pal_handler->dtb_miss_native)(env);
        break;
    case EXCP_UNALIGN:
        (*pal_handler->unalign)(env);
        break;
    case EXCP_ITB_MISS:
        (*pal_handler->itb_miss)(env);
        break;
    case EXCP_ITB_ACV:
        (*pal_handler->itb_acv)(env);
        break;
    case EXCP_OPCDEC:
        (*pal_handler->opcdec)(env);
        break;
    case EXCP_FEN:
        (*pal_handler->fen)(env);
        break;
    default:
        if (env->exception_index >= EXCP_CALL_PAL &&
            env->exception_index < EXCP_CALL_PALP) {
            /* Unprivileged PAL call */
            (*pal_handler->call_pal)
                (env, (env->exception_index - EXCP_CALL_PAL) >> 6);
        } else if (env->exception_index >= EXCP_CALL_PALP &&
                   env->exception_index < EXCP_CALL_PALE) {
            /* Privileged PAL call */
            (*pal_handler->call_pal)
                (env, ((env->exception_index - EXCP_CALL_PALP) >> 6) + 0x80);
        } else {
            /* Should never happen */
        }
        break;
    }
    env->ipr[IPR_EXC_ADDR] &= ~1;
}

void pal_init (CPUState *env)
{
    do_swappal(env, 0);
}

#if 0
static uint64_t get_ptebase (CPUState *env, uint64_t vaddr)
{
    uint64_t virbnd, ptbr;

    if ((env->features & FEATURE_VIRBND)) {
        cpu_alpha_mfpr(env, IPR_VIRBND, &virbnd);
        if (vaddr >= virbnd)
            cpu_alpha_mfpr(env, IPR_SYSPTBR, &ptbr);
        else
            cpu_alpha_mfpr(env, IPR_PTBR, &ptbr);
    } else {
        cpu_alpha_mfpr(env, IPR_PTBR, &ptbr);
    }

    return ptbr;
}

static int get_page_bits (CPUState *env)
{
    /* XXX */
    return 13;
}

static int get_pte (uint64_t *pfnp, int *zbitsp, int *protp,
                    uint64_t ptebase, int page_bits, uint64_t level,
                    int mmu_idx, int rw)
{
    uint64_t pteaddr, pte, pfn;
    uint8_t gh;
    int ure, uwe, kre, kwe, foE, foR, foW, v, ret, ar, is_user;

    /* XXX: TOFIX */
    is_user = mmu_idx == MMU_USER_IDX;
    pteaddr = (ptebase << page_bits) + (8 * level);
    pte = ldq_raw(pteaddr);
    /* Decode all interresting PTE fields */
    pfn = pte >> 32;
    uwe = (pte >> 13) & 1;
    kwe = (pte >> 12) & 1;
    ure = (pte >> 9) & 1;
    kre = (pte >> 8) & 1;
    gh = (pte >> 5) & 3;
    foE = (pte >> 3) & 1;
    foW = (pte >> 2) & 1;
    foR = (pte >> 1) & 1;
    v = pte & 1;
    ret = 0;
    if (!v)
        ret = 0x1;
    /* Check access rights */
    ar = 0;
    if (is_user) {
        if (ure)
            ar |= PAGE_READ;
        if (uwe)
            ar |= PAGE_WRITE;
        if (rw == 1 && !uwe)
            ret |= 0x2;
        if (rw != 1 && !ure)
            ret |= 0x2;
    } else {
        if (kre)
            ar |= PAGE_READ;
        if (kwe)
            ar |= PAGE_WRITE;
        if (rw == 1 && !kwe)
            ret |= 0x2;
        if (rw != 1 && !kre)
            ret |= 0x2;
    }
    if (rw == 0 && foR)
        ret |= 0x4;
    if (rw == 2 && foE)
        ret |= 0x8;
    if (rw == 1 && foW)
        ret |= 0xC;
    *pfnp = pfn;
    if (zbitsp != NULL)
        *zbitsp = page_bits + (3 * gh);
    if (protp != NULL)
        *protp = ar;

    return ret;
}

static int paddr_from_pte (uint64_t *paddr, int *zbitsp, int *prot,
                           uint64_t ptebase, int page_bits,
                           uint64_t vaddr, int mmu_idx, int rw)
{
    uint64_t pfn, page_mask, lvl_mask, level1, level2, level3;
    int lvl_bits, ret;

    page_mask = (1ULL << page_bits) - 1ULL;
    lvl_bits = page_bits - 3;
    lvl_mask = (1ULL << lvl_bits) - 1ULL;
    level3 = (vaddr >> page_bits) & lvl_mask;
    level2 = (vaddr >> (page_bits + lvl_bits)) & lvl_mask;
    level1 = (vaddr >> (page_bits + (2 * lvl_bits))) & lvl_mask;
    /* Level 1 PTE */
    ret = get_pte(&pfn, NULL, NULL, ptebase, page_bits, level1, 0, 0);
    switch (ret) {
    case 3:
        /* Access violation */
        return 2;
    case 2:
        /* translation not valid */
        return 1;
    default:
        /* OK */
        break;
    }
    /* Level 2 PTE */
    ret = get_pte(&pfn, NULL, NULL, pfn, page_bits, level2, 0, 0);
    switch (ret) {
    case 3:
        /* Access violation */
        return 2;
    case 2:
        /* translation not valid */
        return 1;
    default:
        /* OK */
        break;
    }
    /* Level 3 PTE */
    ret = get_pte(&pfn, zbitsp, prot, pfn, page_bits, level3, mmu_idx, rw);
    if (ret & 0x1) {
        /* Translation not valid */
        ret = 1;
    } else if (ret & 2) {
        /* Access violation */
        ret = 2;
    } else {
        switch (ret & 0xC) {
        case 0:
            /* OK */
            ret = 0;
            break;
        case 0x4:
            /* Fault on read */
            ret = 3;
            break;
        case 0x8:
            /* Fault on execute */
            ret = 4;
            break;
        case 0xC:
            /* Fault on write */
            ret = 5;
            break;
        }
    }
    *paddr = (pfn << page_bits) | (vaddr & page_mask);

    return 0;
}

static int virtual_to_physical (CPUState *env, uint64_t *physp,
                                int *zbitsp, int *protp,
                                uint64_t virtual, int mmu_idx, int rw)
{
    uint64_t sva, ptebase;
    int seg, page_bits, ret;

    sva = ((int64_t)(virtual << (64 - VA_BITS))) >> (64 - VA_BITS);
    if (sva != virtual)
        seg = -1;
    else
        seg = sva >> (VA_BITS - 2);
    virtual &= ~(0xFFFFFC0000000000ULL << (VA_BITS - 43));
    ptebase = get_ptebase(env, virtual);
    page_bits = get_page_bits(env);
    ret = 0;
    switch (seg) {
    case 0:
        /* seg1: 3 levels of PTE */
        ret = paddr_from_pte(physp, zbitsp, protp, ptebase, page_bits,
                             virtual, mmu_idx, rw);
        break;
    case 1:
        /* seg1: 2 levels of PTE */
        ret = paddr_from_pte(physp, zbitsp, protp, ptebase, page_bits,
                             virtual, mmu_idx, rw);
        break;
    case 2:
        /* kernel segment */
        if (mmu_idx != 0) {
            ret = 2;
        } else {
            *physp = virtual;
        }
        break;
    case 3:
        /* seg1: TB mapped */
        ret = paddr_from_pte(physp, zbitsp, protp, ptebase, page_bits,
                             virtual, mmu_idx, rw);
        break;
    default:
        ret = 1;
        break;
    }

    return ret;
}

/* XXX: code provision */
int cpu_ppc_handle_mmu_fault (CPUState *env, uint32_t address, int rw,
                              int mmu_idx, int is_softmmu)
{
    uint64_t physical, page_size, end;
    int prot, zbits, ret;

    ret = virtual_to_physical(env, &physical, &zbits, &prot,
                              address, mmu_idx, rw);

    switch (ret) {
    case 0:
        /* No fault */
        page_size = 1ULL << zbits;
        address &= ~(page_size - 1);
        /* FIXME: page_size should probably be passed to tlb_set_page,
           and this loop removed.   */
        for (end = physical + page_size; physical < end; physical += 0x1000) {
            tlb_set_page(env, address, physical, prot, mmu_idx,
                         TARGET_PAGE_SIZE);
            address += 0x1000;
        }
        ret = 0;
        break;
#if 0
    case 1:
        env->exception_index = EXCP_DFAULT;
        env->ipr[IPR_EXC_ADDR] = address;
        ret = 1;
        break;
    case 2:
        env->exception_index = EXCP_ACCESS_VIOLATION;
        env->ipr[IPR_EXC_ADDR] = address;
        ret = 1;
        break;
    case 3:
        env->exception_index = EXCP_FAULT_ON_READ;
        env->ipr[IPR_EXC_ADDR] = address;
        ret = 1;
        break;
    case 4:
        env->exception_index = EXCP_FAULT_ON_EXECUTE;
        env->ipr[IPR_EXC_ADDR] = address;
        ret = 1;
    case 5:
        env->exception_index = EXCP_FAULT_ON_WRITE;
        env->ipr[IPR_EXC_ADDR] = address;
        ret = 1;
#endif
    default:
        /* Should never happen */
        env->exception_index = EXCP_MCHK;
        env->ipr[IPR_EXC_ADDR] = address;
        ret = 1;
        break;
    }

    return ret;
}
#endif
