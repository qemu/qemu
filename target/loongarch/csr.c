/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */
#include <stddef.h>
#include "qemu/osdep.h"
#include "cpu.h"
#include "csr.h"

#define CSR_OFF_FUNCS(NAME, FL, RD, WR)                    \
    [LOONGARCH_CSR_##NAME] = {                             \
        .name   = (stringify(NAME)),                       \
        .offset = offsetof(CPULoongArchState, CSR_##NAME), \
        .flags = FL, .readfn = RD, .writefn = WR           \
    }

#define CSR_OFF_ARRAY(NAME, N)                                \
    [LOONGARCH_CSR_##NAME(N)] = {                             \
        .name   = (stringify(NAME##N)),                       \
        .offset = offsetof(CPULoongArchState, CSR_##NAME[N]), \
        .flags = 0, .readfn = NULL, .writefn = NULL           \
    }

#define CSR_OFF_FLAGS(NAME, FL)   CSR_OFF_FUNCS(NAME, FL, NULL, NULL)
#define CSR_OFF(NAME)             CSR_OFF_FLAGS(NAME, 0)

static CSRInfo csr_info[] = {
    CSR_OFF_FLAGS(CRMD, CSRFL_EXITTB),
    CSR_OFF(PRMD),
    CSR_OFF_FLAGS(EUEN, CSRFL_EXITTB),
    CSR_OFF_FLAGS(MISC, CSRFL_READONLY),
    CSR_OFF(ECFG),
    CSR_OFF_FLAGS(ESTAT, CSRFL_EXITTB),
    CSR_OFF(ERA),
    CSR_OFF(BADV),
    CSR_OFF_FLAGS(BADI, CSRFL_READONLY),
    CSR_OFF(EENTRY),
    CSR_OFF(TLBIDX),
    CSR_OFF(TLBEHI),
    CSR_OFF(TLBELO0),
    CSR_OFF(TLBELO1),
    CSR_OFF_FLAGS(ASID, CSRFL_EXITTB),
    CSR_OFF(PGDL),
    CSR_OFF(PGDH),
    CSR_OFF_FLAGS(PGD, CSRFL_READONLY),
    CSR_OFF(PWCL),
    CSR_OFF(PWCH),
    CSR_OFF(STLBPS),
    CSR_OFF(RVACFG),
    CSR_OFF_FLAGS(CPUID, CSRFL_READONLY),
    CSR_OFF_FLAGS(PRCFG1, CSRFL_READONLY),
    CSR_OFF_FLAGS(PRCFG2, CSRFL_READONLY),
    CSR_OFF_FLAGS(PRCFG3, CSRFL_READONLY),
    CSR_OFF_ARRAY(SAVE, 0),
    CSR_OFF_ARRAY(SAVE, 1),
    CSR_OFF_ARRAY(SAVE, 2),
    CSR_OFF_ARRAY(SAVE, 3),
    CSR_OFF_ARRAY(SAVE, 4),
    CSR_OFF_ARRAY(SAVE, 5),
    CSR_OFF_ARRAY(SAVE, 6),
    CSR_OFF_ARRAY(SAVE, 7),
    CSR_OFF_ARRAY(SAVE, 8),
    CSR_OFF_ARRAY(SAVE, 9),
    CSR_OFF_ARRAY(SAVE, 10),
    CSR_OFF_ARRAY(SAVE, 11),
    CSR_OFF_ARRAY(SAVE, 12),
    CSR_OFF_ARRAY(SAVE, 13),
    CSR_OFF_ARRAY(SAVE, 14),
    CSR_OFF_ARRAY(SAVE, 15),
    CSR_OFF(TID),
    CSR_OFF_FLAGS(TCFG, CSRFL_IO),
    CSR_OFF_FLAGS(TVAL, CSRFL_READONLY | CSRFL_IO),
    CSR_OFF(CNTC),
    CSR_OFF_FLAGS(TICLR, CSRFL_IO),
    CSR_OFF(LLBCTL),
    CSR_OFF(IMPCTL1),
    CSR_OFF(IMPCTL2),
    CSR_OFF(TLBRENTRY),
    CSR_OFF(TLBRBADV),
    CSR_OFF(TLBRERA),
    CSR_OFF(TLBRSAVE),
    CSR_OFF(TLBRELO0),
    CSR_OFF(TLBRELO1),
    CSR_OFF(TLBREHI),
    CSR_OFF(TLBRPRMD),
    CSR_OFF(MERRCTL),
    CSR_OFF(MERRINFO1),
    CSR_OFF(MERRINFO2),
    CSR_OFF(MERRENTRY),
    CSR_OFF(MERRERA),
    CSR_OFF(MERRSAVE),
    CSR_OFF(CTAG),
    CSR_OFF_ARRAY(DMW, 0),
    CSR_OFF_ARRAY(DMW, 1),
    CSR_OFF_ARRAY(DMW, 2),
    CSR_OFF_ARRAY(DMW, 3),
    CSR_OFF(DBG),
    CSR_OFF(DERA),
    CSR_OFF(DSAVE),
    CSR_OFF_ARRAY(MSGIS, 0),
    CSR_OFF_ARRAY(MSGIS, 1),
    CSR_OFF_ARRAY(MSGIS, 2),
    CSR_OFF_ARRAY(MSGIS, 3),
    CSR_OFF(MSGIR),
};

CSRInfo *get_csr(unsigned int csr_num)
{
    CSRInfo *csr;

    if (csr_num >= ARRAY_SIZE(csr_info)) {
        return NULL;
    }

    csr = &csr_info[csr_num];
    if (csr->offset == 0) {
        return NULL;
    }

    return csr;
}

bool set_csr_flag(unsigned int csr_num, int flag)
{
    CSRInfo *csr;

    csr = get_csr(csr_num);
    if (!csr) {
        return false;
    }

    csr->flags |= flag;
    return true;
}
