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

struct RISCVCPUConfig {
#define BOOL_FIELD(x) bool x;
#define TYPED_FIELD(type, x, default) type x;
#include "cpu_cfg_fields.h.inc"
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
