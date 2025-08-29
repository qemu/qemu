/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "elf.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "qemu";
}

#define GET_FEATURE(_feat, _hwcap) \
    do { if (s390_has_feat(_feat)) { hwcap |= _hwcap; } } while (0)

abi_ulong get_elf_hwcap(CPUState *cs)
{
    /*
     * Let's assume we always have esan3 and zarch.
     * 31-bit processes can use 64-bit registers (high gprs).
     */
    uint32_t hwcap = HWCAP_S390_ESAN3 | HWCAP_S390_ZARCH | HWCAP_S390_HIGH_GPRS;

    GET_FEATURE(S390_FEAT_STFLE, HWCAP_S390_STFLE);
    GET_FEATURE(S390_FEAT_MSA, HWCAP_S390_MSA);
    GET_FEATURE(S390_FEAT_LONG_DISPLACEMENT, HWCAP_S390_LDISP);
    GET_FEATURE(S390_FEAT_EXTENDED_IMMEDIATE, HWCAP_S390_EIMM);
    if (s390_has_feat(S390_FEAT_EXTENDED_TRANSLATION_3) &&
        s390_has_feat(S390_FEAT_ETF3_ENH)) {
        hwcap |= HWCAP_S390_ETF3EH;
    }
    GET_FEATURE(S390_FEAT_VECTOR, HWCAP_S390_VXRS);
    GET_FEATURE(S390_FEAT_VECTOR_ENH, HWCAP_S390_VXRS_EXT);
    GET_FEATURE(S390_FEAT_VECTOR_ENH2, HWCAP_S390_VXRS_EXT2);

    return hwcap;
}

const char *elf_hwcap_str(uint32_t bit)
{
    static const char *hwcap_str[] = {
        [HWCAP_S390_NR_ESAN3]     = "esan3",
        [HWCAP_S390_NR_ZARCH]     = "zarch",
        [HWCAP_S390_NR_STFLE]     = "stfle",
        [HWCAP_S390_NR_MSA]       = "msa",
        [HWCAP_S390_NR_LDISP]     = "ldisp",
        [HWCAP_S390_NR_EIMM]      = "eimm",
        [HWCAP_S390_NR_DFP]       = "dfp",
        [HWCAP_S390_NR_HPAGE]     = "edat",
        [HWCAP_S390_NR_ETF3EH]    = "etf3eh",
        [HWCAP_S390_NR_HIGH_GPRS] = "highgprs",
        [HWCAP_S390_NR_TE]        = "te",
        [HWCAP_S390_NR_VXRS]      = "vx",
        [HWCAP_S390_NR_VXRS_BCD]  = "vxd",
        [HWCAP_S390_NR_VXRS_EXT]  = "vxe",
        [HWCAP_S390_NR_GS]        = "gs",
        [HWCAP_S390_NR_VXRS_EXT2] = "vxe2",
        [HWCAP_S390_NR_VXRS_PDE]  = "vxp",
        [HWCAP_S390_NR_SORT]      = "sort",
        [HWCAP_S390_NR_DFLT]      = "dflt",
        [HWCAP_S390_NR_NNPA]      = "nnpa",
        [HWCAP_S390_NR_PCI_MIO]   = "pcimio",
        [HWCAP_S390_NR_SIE]       = "sie",
    };

    return bit < ARRAY_SIZE(hwcap_str) ? hwcap_str[bit] : NULL;
}

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUS390XState *env)
{
    r->pt.psw.mask = tswapal(env->psw.mask);
    r->pt.psw.addr = tswapal(env->psw.addr);
    for (int i = 0; i < 16; i++) {
        r->pt.gprs[i] = tswapal(env->regs[i]);
    }
    for (int i = 0; i < 16; i++) {
        r->pt.acrs[i] = tswap32(env->aregs[i]);
    }
    r->pt.orig_gpr2 = 0;
}
