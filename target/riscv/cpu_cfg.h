/*
 * QEMU RISC-V CPU CFG
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 * Copyright (c) 2021-2023 PLCT Lab
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RISCV_CPU_CFG_H
#define RISCV_CPU_CFG_H

/*
 * map is a 16-bit bitmap: the most significant set bit in map is the maximum
 * satp mode that is supported. It may be chosen by the user and must respect
 * what qemu implements (valid_1_10_32/64) and what the hw is capable of
 * (supported bitmap below).
 *
 * init is a 16-bit bitmap used to make sure the user selected a correct
 * configuration as per the specification.
 *
 * supported is a 16-bit bitmap used to reflect the hw capabilities.
 */
typedef struct {
    uint16_t map, init, supported;
} RISCVSATPMap;

struct RISCVCPUConfig {
    bool ext_zba;
    bool ext_zbb;
    bool ext_zbc;
    bool ext_zbkb;
    bool ext_zbkc;
    bool ext_zbkx;
    bool ext_zbs;
    bool ext_zca;
    bool ext_zcb;
    bool ext_zcd;
    bool ext_zce;
    bool ext_zcf;
    bool ext_zcmp;
    bool ext_zcmt;
    bool ext_zk;
    bool ext_zkn;
    bool ext_zknd;
    bool ext_zkne;
    bool ext_zknh;
    bool ext_zkr;
    bool ext_zks;
    bool ext_zksed;
    bool ext_zksh;
    bool ext_zkt;
    bool ext_zifencei;
    bool ext_zicntr;
    bool ext_zicsr;
    bool ext_zicbom;
    bool ext_zicbop;
    bool ext_zicboz;
    bool ext_zicond;
    bool ext_zihintntl;
    bool ext_zihintpause;
    bool ext_zihpm;
    bool ext_smstateen;
    bool ext_sstc;
    bool ext_svadu;
    bool ext_svinval;
    bool ext_svnapot;
    bool ext_svpbmt;
    bool ext_zdinx;
    bool ext_zacas;
    bool ext_zawrs;
    bool ext_zfa;
    bool ext_zfbfmin;
    bool ext_zfh;
    bool ext_zfhmin;
    bool ext_zfinx;
    bool ext_zhinx;
    bool ext_zhinxmin;
    bool ext_zve32f;
    bool ext_zve64f;
    bool ext_zve64d;
    bool ext_zvbb;
    bool ext_zvbc;
    bool ext_zvkb;
    bool ext_zvkg;
    bool ext_zvkned;
    bool ext_zvknha;
    bool ext_zvknhb;
    bool ext_zvksed;
    bool ext_zvksh;
    bool ext_zvkt;
    bool ext_zvkn;
    bool ext_zvknc;
    bool ext_zvkng;
    bool ext_zvks;
    bool ext_zvksc;
    bool ext_zvksg;
    bool ext_zmmul;
    bool ext_zvfbfmin;
    bool ext_zvfbfwma;
    bool ext_zvfh;
    bool ext_zvfhmin;
    bool ext_smaia;
    bool ext_ssaia;
    bool ext_sscofpmf;
    bool ext_smepmp;
    bool rvv_ta_all_1s;
    bool rvv_ma_all_1s;
    bool svade;
    bool zic64b;

    uint32_t mvendorid;
    uint64_t marchid;
    uint64_t mimpid;

    /* Vendor-specific custom extensions */
    bool ext_xtheadba;
    bool ext_xtheadbb;
    bool ext_xtheadbs;
    bool ext_xtheadcmo;
    bool ext_xtheadcondmov;
    bool ext_xtheadfmemidx;
    bool ext_xtheadfmv;
    bool ext_xtheadmac;
    bool ext_xtheadmemidx;
    bool ext_xtheadmempair;
    bool ext_xtheadsync;
    bool ext_XVentanaCondOps;

    uint32_t pmu_mask;
    uint16_t vlen;
    uint16_t elen;
    uint16_t cbom_blocksize;
    uint16_t cbop_blocksize;
    uint16_t cboz_blocksize;
    bool mmu;
    bool pmp;
    bool debug;
    bool misa_w;

    bool short_isa_string;

#ifndef CONFIG_USER_ONLY
    RISCVSATPMap satp_mode;
#endif
};

typedef struct RISCVCPUConfig RISCVCPUConfig;

/* Helper functions to test for extensions.  */

static inline bool always_true_p(const RISCVCPUConfig *cfg __attribute__((__unused__)))
{
    return true;
}

static inline bool has_xthead_p(const RISCVCPUConfig *cfg)
{
    return cfg->ext_xtheadba || cfg->ext_xtheadbb ||
           cfg->ext_xtheadbs || cfg->ext_xtheadcmo ||
           cfg->ext_xtheadcondmov ||
           cfg->ext_xtheadfmemidx || cfg->ext_xtheadfmv ||
           cfg->ext_xtheadmac || cfg->ext_xtheadmemidx ||
           cfg->ext_xtheadmempair || cfg->ext_xtheadsync;
}

#define MATERIALISE_EXT_PREDICATE(ext) \
    static inline bool has_ ## ext ## _p(const RISCVCPUConfig *cfg) \
    { \
        return cfg->ext_ ## ext ; \
    }

MATERIALISE_EXT_PREDICATE(xtheadba)
MATERIALISE_EXT_PREDICATE(xtheadbb)
MATERIALISE_EXT_PREDICATE(xtheadbs)
MATERIALISE_EXT_PREDICATE(xtheadcmo)
MATERIALISE_EXT_PREDICATE(xtheadcondmov)
MATERIALISE_EXT_PREDICATE(xtheadfmemidx)
MATERIALISE_EXT_PREDICATE(xtheadfmv)
MATERIALISE_EXT_PREDICATE(xtheadmac)
MATERIALISE_EXT_PREDICATE(xtheadmemidx)
MATERIALISE_EXT_PREDICATE(xtheadmempair)
MATERIALISE_EXT_PREDICATE(xtheadsync)
MATERIALISE_EXT_PREDICATE(XVentanaCondOps)

#endif
