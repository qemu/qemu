/*
 * QEMU RISC-V CPU
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
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

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/ctype.h"
#include "qemu/log.h"
#include "cpu.h"
#include "cpu_vendorid.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "fpu/softfloat-helpers.h"
#include "sysemu/kvm.h"
#include "sysemu/tcg.h"
#include "kvm/kvm_riscv.h"
#include "tcg/tcg-cpu.h"
#include "tcg/tcg.h"

/* RISC-V CPU definitions */
static const char riscv_single_letter_exts[] = "IEMAFDQCPVH";
const uint32_t misa_bits[] = {RVI, RVE, RVM, RVA, RVF, RVD, RVV,
                              RVC, RVS, RVU, RVH, RVJ, RVG, 0};

/*
 * From vector_helper.c
 * Note that vector data is stored in host-endian 64-bit chunks,
 * so addressing bytes needs a host-endian fixup.
 */
#if HOST_BIG_ENDIAN
#define BYTE(x)   ((x) ^ 7)
#else
#define BYTE(x)   (x)
#endif

#define ISA_EXT_DATA_ENTRY(_name, _min_ver, _prop) \
    {#_name, _min_ver, CPU_CFG_OFFSET(_prop)}

/*
 * Here are the ordering rules of extension naming defined by RISC-V
 * specification :
 * 1. All extensions should be separated from other multi-letter extensions
 *    by an underscore.
 * 2. The first letter following the 'Z' conventionally indicates the most
 *    closely related alphabetical extension category, IMAFDQLCBKJTPVH.
 *    If multiple 'Z' extensions are named, they should be ordered first
 *    by category, then alphabetically within a category.
 * 3. Standard supervisor-level extensions (starts with 'S') should be
 *    listed after standard unprivileged extensions.  If multiple
 *    supervisor-level extensions are listed, they should be ordered
 *    alphabetically.
 * 4. Non-standard extensions (starts with 'X') must be listed after all
 *    standard extensions. They must be separated from other multi-letter
 *    extensions by an underscore.
 *
 * Single letter extensions are checked in riscv_cpu_validate_misa_priv()
 * instead.
 */
const RISCVIsaExtData isa_edata_arr[] = {
    ISA_EXT_DATA_ENTRY(zicbom, PRIV_VERSION_1_12_0, ext_zicbom),
    ISA_EXT_DATA_ENTRY(zicboz, PRIV_VERSION_1_12_0, ext_zicboz),
    ISA_EXT_DATA_ENTRY(zicond, PRIV_VERSION_1_12_0, ext_zicond),
    ISA_EXT_DATA_ENTRY(zicntr, PRIV_VERSION_1_12_0, ext_zicntr),
    ISA_EXT_DATA_ENTRY(zicsr, PRIV_VERSION_1_10_0, ext_zicsr),
    ISA_EXT_DATA_ENTRY(zifencei, PRIV_VERSION_1_10_0, ext_zifencei),
    ISA_EXT_DATA_ENTRY(zihintntl, PRIV_VERSION_1_10_0, ext_zihintntl),
    ISA_EXT_DATA_ENTRY(zihintpause, PRIV_VERSION_1_10_0, ext_zihintpause),
    ISA_EXT_DATA_ENTRY(zihpm, PRIV_VERSION_1_12_0, ext_zihpm),
    ISA_EXT_DATA_ENTRY(zmmul, PRIV_VERSION_1_12_0, ext_zmmul),
    ISA_EXT_DATA_ENTRY(zawrs, PRIV_VERSION_1_12_0, ext_zawrs),
    ISA_EXT_DATA_ENTRY(zfa, PRIV_VERSION_1_12_0, ext_zfa),
    ISA_EXT_DATA_ENTRY(zfbfmin, PRIV_VERSION_1_12_0, ext_zfbfmin),
    ISA_EXT_DATA_ENTRY(zfh, PRIV_VERSION_1_11_0, ext_zfh),
    ISA_EXT_DATA_ENTRY(zfhmin, PRIV_VERSION_1_11_0, ext_zfhmin),
    ISA_EXT_DATA_ENTRY(zfinx, PRIV_VERSION_1_12_0, ext_zfinx),
    ISA_EXT_DATA_ENTRY(zdinx, PRIV_VERSION_1_12_0, ext_zdinx),
    ISA_EXT_DATA_ENTRY(zca, PRIV_VERSION_1_12_0, ext_zca),
    ISA_EXT_DATA_ENTRY(zcb, PRIV_VERSION_1_12_0, ext_zcb),
    ISA_EXT_DATA_ENTRY(zcf, PRIV_VERSION_1_12_0, ext_zcf),
    ISA_EXT_DATA_ENTRY(zcd, PRIV_VERSION_1_12_0, ext_zcd),
    ISA_EXT_DATA_ENTRY(zce, PRIV_VERSION_1_12_0, ext_zce),
    ISA_EXT_DATA_ENTRY(zcmp, PRIV_VERSION_1_12_0, ext_zcmp),
    ISA_EXT_DATA_ENTRY(zcmt, PRIV_VERSION_1_12_0, ext_zcmt),
    ISA_EXT_DATA_ENTRY(zba, PRIV_VERSION_1_12_0, ext_zba),
    ISA_EXT_DATA_ENTRY(zbb, PRIV_VERSION_1_12_0, ext_zbb),
    ISA_EXT_DATA_ENTRY(zbc, PRIV_VERSION_1_12_0, ext_zbc),
    ISA_EXT_DATA_ENTRY(zbkb, PRIV_VERSION_1_12_0, ext_zbkb),
    ISA_EXT_DATA_ENTRY(zbkc, PRIV_VERSION_1_12_0, ext_zbkc),
    ISA_EXT_DATA_ENTRY(zbkx, PRIV_VERSION_1_12_0, ext_zbkx),
    ISA_EXT_DATA_ENTRY(zbs, PRIV_VERSION_1_12_0, ext_zbs),
    ISA_EXT_DATA_ENTRY(zk, PRIV_VERSION_1_12_0, ext_zk),
    ISA_EXT_DATA_ENTRY(zkn, PRIV_VERSION_1_12_0, ext_zkn),
    ISA_EXT_DATA_ENTRY(zknd, PRIV_VERSION_1_12_0, ext_zknd),
    ISA_EXT_DATA_ENTRY(zkne, PRIV_VERSION_1_12_0, ext_zkne),
    ISA_EXT_DATA_ENTRY(zknh, PRIV_VERSION_1_12_0, ext_zknh),
    ISA_EXT_DATA_ENTRY(zkr, PRIV_VERSION_1_12_0, ext_zkr),
    ISA_EXT_DATA_ENTRY(zks, PRIV_VERSION_1_12_0, ext_zks),
    ISA_EXT_DATA_ENTRY(zksed, PRIV_VERSION_1_12_0, ext_zksed),
    ISA_EXT_DATA_ENTRY(zksh, PRIV_VERSION_1_12_0, ext_zksh),
    ISA_EXT_DATA_ENTRY(zkt, PRIV_VERSION_1_12_0, ext_zkt),
    ISA_EXT_DATA_ENTRY(zvbb, PRIV_VERSION_1_12_0, ext_zvbb),
    ISA_EXT_DATA_ENTRY(zvbc, PRIV_VERSION_1_12_0, ext_zvbc),
    ISA_EXT_DATA_ENTRY(zve32f, PRIV_VERSION_1_10_0, ext_zve32f),
    ISA_EXT_DATA_ENTRY(zve64f, PRIV_VERSION_1_10_0, ext_zve64f),
    ISA_EXT_DATA_ENTRY(zve64d, PRIV_VERSION_1_10_0, ext_zve64d),
    ISA_EXT_DATA_ENTRY(zvfbfmin, PRIV_VERSION_1_12_0, ext_zvfbfmin),
    ISA_EXT_DATA_ENTRY(zvfbfwma, PRIV_VERSION_1_12_0, ext_zvfbfwma),
    ISA_EXT_DATA_ENTRY(zvfh, PRIV_VERSION_1_12_0, ext_zvfh),
    ISA_EXT_DATA_ENTRY(zvfhmin, PRIV_VERSION_1_12_0, ext_zvfhmin),
    ISA_EXT_DATA_ENTRY(zvkb, PRIV_VERSION_1_12_0, ext_zvkb),
    ISA_EXT_DATA_ENTRY(zvkg, PRIV_VERSION_1_12_0, ext_zvkg),
    ISA_EXT_DATA_ENTRY(zvkn, PRIV_VERSION_1_12_0, ext_zvkn),
    ISA_EXT_DATA_ENTRY(zvknc, PRIV_VERSION_1_12_0, ext_zvknc),
    ISA_EXT_DATA_ENTRY(zvkned, PRIV_VERSION_1_12_0, ext_zvkned),
    ISA_EXT_DATA_ENTRY(zvkng, PRIV_VERSION_1_12_0, ext_zvkng),
    ISA_EXT_DATA_ENTRY(zvknha, PRIV_VERSION_1_12_0, ext_zvknha),
    ISA_EXT_DATA_ENTRY(zvknhb, PRIV_VERSION_1_12_0, ext_zvknhb),
    ISA_EXT_DATA_ENTRY(zvks, PRIV_VERSION_1_12_0, ext_zvks),
    ISA_EXT_DATA_ENTRY(zvksc, PRIV_VERSION_1_12_0, ext_zvksc),
    ISA_EXT_DATA_ENTRY(zvksed, PRIV_VERSION_1_12_0, ext_zvksed),
    ISA_EXT_DATA_ENTRY(zvksg, PRIV_VERSION_1_12_0, ext_zvksg),
    ISA_EXT_DATA_ENTRY(zvksh, PRIV_VERSION_1_12_0, ext_zvksh),
    ISA_EXT_DATA_ENTRY(zvkt, PRIV_VERSION_1_12_0, ext_zvkt),
    ISA_EXT_DATA_ENTRY(zhinx, PRIV_VERSION_1_12_0, ext_zhinx),
    ISA_EXT_DATA_ENTRY(zhinxmin, PRIV_VERSION_1_12_0, ext_zhinxmin),
    ISA_EXT_DATA_ENTRY(smaia, PRIV_VERSION_1_12_0, ext_smaia),
    ISA_EXT_DATA_ENTRY(smepmp, PRIV_VERSION_1_12_0, ext_smepmp),
    ISA_EXT_DATA_ENTRY(smstateen, PRIV_VERSION_1_12_0, ext_smstateen),
    ISA_EXT_DATA_ENTRY(ssaia, PRIV_VERSION_1_12_0, ext_ssaia),
    ISA_EXT_DATA_ENTRY(sscofpmf, PRIV_VERSION_1_12_0, ext_sscofpmf),
    ISA_EXT_DATA_ENTRY(sstc, PRIV_VERSION_1_12_0, ext_sstc),
    ISA_EXT_DATA_ENTRY(svadu, PRIV_VERSION_1_12_0, ext_svadu),
    ISA_EXT_DATA_ENTRY(svinval, PRIV_VERSION_1_12_0, ext_svinval),
    ISA_EXT_DATA_ENTRY(svnapot, PRIV_VERSION_1_12_0, ext_svnapot),
    ISA_EXT_DATA_ENTRY(svpbmt, PRIV_VERSION_1_12_0, ext_svpbmt),
    ISA_EXT_DATA_ENTRY(xtheadba, PRIV_VERSION_1_11_0, ext_xtheadba),
    ISA_EXT_DATA_ENTRY(xtheadbb, PRIV_VERSION_1_11_0, ext_xtheadbb),
    ISA_EXT_DATA_ENTRY(xtheadbs, PRIV_VERSION_1_11_0, ext_xtheadbs),
    ISA_EXT_DATA_ENTRY(xtheadcmo, PRIV_VERSION_1_11_0, ext_xtheadcmo),
    ISA_EXT_DATA_ENTRY(xtheadcondmov, PRIV_VERSION_1_11_0, ext_xtheadcondmov),
    ISA_EXT_DATA_ENTRY(xtheadfmemidx, PRIV_VERSION_1_11_0, ext_xtheadfmemidx),
    ISA_EXT_DATA_ENTRY(xtheadfmv, PRIV_VERSION_1_11_0, ext_xtheadfmv),
    ISA_EXT_DATA_ENTRY(xtheadmac, PRIV_VERSION_1_11_0, ext_xtheadmac),
    ISA_EXT_DATA_ENTRY(xtheadmemidx, PRIV_VERSION_1_11_0, ext_xtheadmemidx),
    ISA_EXT_DATA_ENTRY(xtheadmempair, PRIV_VERSION_1_11_0, ext_xtheadmempair),
    ISA_EXT_DATA_ENTRY(xtheadsync, PRIV_VERSION_1_11_0, ext_xtheadsync),
    ISA_EXT_DATA_ENTRY(xventanacondops, PRIV_VERSION_1_12_0, ext_XVentanaCondOps),

    DEFINE_PROP_END_OF_LIST(),
};

bool isa_ext_is_enabled(RISCVCPU *cpu, uint32_t ext_offset)
{
    bool *ext_enabled = (void *)&cpu->cfg + ext_offset;

    return *ext_enabled;
}

void isa_ext_update_enabled(RISCVCPU *cpu, uint32_t ext_offset, bool en)
{
    bool *ext_enabled = (void *)&cpu->cfg + ext_offset;

    *ext_enabled = en;
}

const char * const riscv_int_regnames[] = {
    "x0/zero", "x1/ra",  "x2/sp",  "x3/gp",  "x4/tp",  "x5/t0",   "x6/t1",
    "x7/t2",   "x8/s0",  "x9/s1",  "x10/a0", "x11/a1", "x12/a2",  "x13/a3",
    "x14/a4",  "x15/a5", "x16/a6", "x17/a7", "x18/s2", "x19/s3",  "x20/s4",
    "x21/s5",  "x22/s6", "x23/s7", "x24/s8", "x25/s9", "x26/s10", "x27/s11",
    "x28/t3",  "x29/t4", "x30/t5", "x31/t6"
};

const char * const riscv_int_regnamesh[] = {
    "x0h/zeroh", "x1h/rah",  "x2h/sph",   "x3h/gph",   "x4h/tph",  "x5h/t0h",
    "x6h/t1h",   "x7h/t2h",  "x8h/s0h",   "x9h/s1h",   "x10h/a0h", "x11h/a1h",
    "x12h/a2h",  "x13h/a3h", "x14h/a4h",  "x15h/a5h",  "x16h/a6h", "x17h/a7h",
    "x18h/s2h",  "x19h/s3h", "x20h/s4h",  "x21h/s5h",  "x22h/s6h", "x23h/s7h",
    "x24h/s8h",  "x25h/s9h", "x26h/s10h", "x27h/s11h", "x28h/t3h", "x29h/t4h",
    "x30h/t5h",  "x31h/t6h"
};

const char * const riscv_fpr_regnames[] = {
    "f0/ft0",   "f1/ft1",  "f2/ft2",   "f3/ft3",   "f4/ft4",  "f5/ft5",
    "f6/ft6",   "f7/ft7",  "f8/fs0",   "f9/fs1",   "f10/fa0", "f11/fa1",
    "f12/fa2",  "f13/fa3", "f14/fa4",  "f15/fa5",  "f16/fa6", "f17/fa7",
    "f18/fs2",  "f19/fs3", "f20/fs4",  "f21/fs5",  "f22/fs6", "f23/fs7",
    "f24/fs8",  "f25/fs9", "f26/fs10", "f27/fs11", "f28/ft8", "f29/ft9",
    "f30/ft10", "f31/ft11"
};

const char * const riscv_rvv_regnames[] = {
  "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",
  "v7",  "v8",  "v9",  "v10", "v11", "v12", "v13",
  "v14", "v15", "v16", "v17", "v18", "v19", "v20",
  "v21", "v22", "v23", "v24", "v25", "v26", "v27",
  "v28", "v29", "v30", "v31"
};

static const char * const riscv_excp_names[] = {
    "misaligned_fetch",
    "fault_fetch",
    "illegal_instruction",
    "breakpoint",
    "misaligned_load",
    "fault_load",
    "misaligned_store",
    "fault_store",
    "user_ecall",
    "supervisor_ecall",
    "hypervisor_ecall",
    "machine_ecall",
    "exec_page_fault",
    "load_page_fault",
    "reserved",
    "store_page_fault",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "guest_exec_page_fault",
    "guest_load_page_fault",
    "reserved",
    "guest_store_page_fault",
};

static const char * const riscv_intr_names[] = {
    "u_software",
    "s_software",
    "vs_software",
    "m_software",
    "u_timer",
    "s_timer",
    "vs_timer",
    "m_timer",
    "u_external",
    "s_external",
    "vs_external",
    "m_external",
    "reserved",
    "reserved",
    "reserved",
    "reserved"
};

const char *riscv_cpu_get_trap_name(target_ulong cause, bool async)
{
    if (async) {
        return (cause < ARRAY_SIZE(riscv_intr_names)) ?
               riscv_intr_names[cause] : "(unknown)";
    } else {
        return (cause < ARRAY_SIZE(riscv_excp_names)) ?
               riscv_excp_names[cause] : "(unknown)";
    }
}

void riscv_cpu_set_misa(CPURISCVState *env, RISCVMXL mxl, uint32_t ext)
{
    env->misa_mxl_max = env->misa_mxl = mxl;
    env->misa_ext_mask = env->misa_ext = ext;
}

#ifndef CONFIG_USER_ONLY
static uint8_t satp_mode_from_str(const char *satp_mode_str)
{
    if (!strncmp(satp_mode_str, "mbare", 5)) {
        return VM_1_10_MBARE;
    }

    if (!strncmp(satp_mode_str, "sv32", 4)) {
        return VM_1_10_SV32;
    }

    if (!strncmp(satp_mode_str, "sv39", 4)) {
        return VM_1_10_SV39;
    }

    if (!strncmp(satp_mode_str, "sv48", 4)) {
        return VM_1_10_SV48;
    }

    if (!strncmp(satp_mode_str, "sv57", 4)) {
        return VM_1_10_SV57;
    }

    if (!strncmp(satp_mode_str, "sv64", 4)) {
        return VM_1_10_SV64;
    }

    g_assert_not_reached();
}

uint8_t satp_mode_max_from_map(uint32_t map)
{
    /*
     * 'map = 0' will make us return (31 - 32), which C will
     * happily overflow to UINT_MAX. There's no good result to
     * return if 'map = 0' (e.g. returning 0 will be ambiguous
     * with the result for 'map = 1').
     *
     * Assert out if map = 0. Callers will have to deal with
     * it outside of this function.
     */
    g_assert(map > 0);

    /* map here has at least one bit set, so no problem with clz */
    return 31 - __builtin_clz(map);
}

const char *satp_mode_str(uint8_t satp_mode, bool is_32_bit)
{
    if (is_32_bit) {
        switch (satp_mode) {
        case VM_1_10_SV32:
            return "sv32";
        case VM_1_10_MBARE:
            return "none";
        }
    } else {
        switch (satp_mode) {
        case VM_1_10_SV64:
            return "sv64";
        case VM_1_10_SV57:
            return "sv57";
        case VM_1_10_SV48:
            return "sv48";
        case VM_1_10_SV39:
            return "sv39";
        case VM_1_10_MBARE:
            return "none";
        }
    }

    g_assert_not_reached();
}

static void set_satp_mode_max_supported(RISCVCPU *cpu,
                                        uint8_t satp_mode)
{
    bool rv32 = riscv_cpu_mxl(&cpu->env) == MXL_RV32;
    const bool *valid_vm = rv32 ? valid_vm_1_10_32 : valid_vm_1_10_64;

    for (int i = 0; i <= satp_mode; ++i) {
        if (valid_vm[i]) {
            cpu->cfg.satp_mode.supported |= (1 << i);
        }
    }
}

/* Set the satp mode to the max supported */
static void set_satp_mode_default_map(RISCVCPU *cpu)
{
    cpu->cfg.satp_mode.map = cpu->cfg.satp_mode.supported;
}
#endif

static void riscv_any_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
#if defined(TARGET_RISCV32)
    riscv_cpu_set_misa(env, MXL_RV32, RVI | RVM | RVA | RVF | RVD | RVC | RVU);
#elif defined(TARGET_RISCV64)
    riscv_cpu_set_misa(env, MXL_RV64, RVI | RVM | RVA | RVF | RVD | RVC | RVU);
#endif

#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj),
        riscv_cpu_mxl(&RISCV_CPU(obj)->env) == MXL_RV32 ?
        VM_1_10_SV32 : VM_1_10_SV57);
#endif

    env->priv_ver = PRIV_VERSION_LATEST;

    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.mmu = true;
    cpu->cfg.pmp = true;
}

static void riscv_max_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    RISCVMXL mlx = MXL_RV64;

#ifdef TARGET_RISCV32
    mlx = MXL_RV32;
#endif
    riscv_cpu_set_misa(env, mlx, 0);
    env->priv_ver = PRIV_VERSION_LATEST;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), mlx == MXL_RV32 ?
                                VM_1_10_SV32 : VM_1_10_SV57);
#endif
}

#if defined(TARGET_RISCV64)
static void rv64_base_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    /* We set this in the realise function */
    riscv_cpu_set_misa(env, MXL_RV64, 0);
    /* Set latest version of privileged specification */
    env->priv_ver = PRIV_VERSION_LATEST;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV57);
#endif
}

static void rv64_sifive_u_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    riscv_cpu_set_misa(env, MXL_RV64,
                       RVI | RVM | RVA | RVF | RVD | RVC | RVS | RVU);
    env->priv_ver = PRIV_VERSION_1_10_0;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV39);
#endif

    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.mmu = true;
    cpu->cfg.pmp = true;
}

static void rv64_sifive_e_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    riscv_cpu_set_misa(env, MXL_RV64, RVI | RVM | RVA | RVC | RVU);
    env->priv_ver = PRIV_VERSION_1_10_0;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_MBARE);
#endif

    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.pmp = true;
}

static void rv64_thead_c906_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    riscv_cpu_set_misa(env, MXL_RV64, RVG | RVC | RVS | RVU);
    env->priv_ver = PRIV_VERSION_1_11_0;

    cpu->cfg.ext_zfa = true;
    cpu->cfg.ext_zfh = true;
    cpu->cfg.mmu = true;
    cpu->cfg.ext_xtheadba = true;
    cpu->cfg.ext_xtheadbb = true;
    cpu->cfg.ext_xtheadbs = true;
    cpu->cfg.ext_xtheadcmo = true;
    cpu->cfg.ext_xtheadcondmov = true;
    cpu->cfg.ext_xtheadfmemidx = true;
    cpu->cfg.ext_xtheadmac = true;
    cpu->cfg.ext_xtheadmemidx = true;
    cpu->cfg.ext_xtheadmempair = true;
    cpu->cfg.ext_xtheadsync = true;

    cpu->cfg.mvendorid = THEAD_VENDOR_ID;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_SV39);
#endif

    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.pmp = true;
}

static void rv64_veyron_v1_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    riscv_cpu_set_misa(env, MXL_RV64, RVG | RVC | RVS | RVU | RVH);
    env->priv_ver = PRIV_VERSION_1_12_0;

    /* Enable ISA extensions */
    cpu->cfg.mmu = true;
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.pmp = true;
    cpu->cfg.ext_zicbom = true;
    cpu->cfg.cbom_blocksize = 64;
    cpu->cfg.cboz_blocksize = 64;
    cpu->cfg.ext_zicboz = true;
    cpu->cfg.ext_smaia = true;
    cpu->cfg.ext_ssaia = true;
    cpu->cfg.ext_sscofpmf = true;
    cpu->cfg.ext_sstc = true;
    cpu->cfg.ext_svinval = true;
    cpu->cfg.ext_svnapot = true;
    cpu->cfg.ext_svpbmt = true;
    cpu->cfg.ext_smstateen = true;
    cpu->cfg.ext_zba = true;
    cpu->cfg.ext_zbb = true;
    cpu->cfg.ext_zbc = true;
    cpu->cfg.ext_zbs = true;
    cpu->cfg.ext_XVentanaCondOps = true;

    cpu->cfg.mvendorid = VEYRON_V1_MVENDORID;
    cpu->cfg.marchid = VEYRON_V1_MARCHID;
    cpu->cfg.mimpid = VEYRON_V1_MIMPID;

#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_SV48);
#endif
}

static void rv128_base_cpu_init(Object *obj)
{
    if (qemu_tcg_mttcg_enabled()) {
        /* Missing 128-bit aligned atomics */
        error_report("128-bit RISC-V currently does not work with Multi "
                     "Threaded TCG. Please use: -accel tcg,thread=single");
        exit(EXIT_FAILURE);
    }
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    /* We set this in the realise function */
    riscv_cpu_set_misa(env, MXL_RV128, 0);
    /* Set latest version of privileged specification */
    env->priv_ver = PRIV_VERSION_LATEST;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV57);
#endif
}
#else
static void rv32_base_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    /* We set this in the realise function */
    riscv_cpu_set_misa(env, MXL_RV32, 0);
    /* Set latest version of privileged specification */
    env->priv_ver = PRIV_VERSION_LATEST;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV32);
#endif
}

static void rv32_sifive_u_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    riscv_cpu_set_misa(env, MXL_RV32,
                       RVI | RVM | RVA | RVF | RVD | RVC | RVS | RVU);
    env->priv_ver = PRIV_VERSION_1_10_0;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV32);
#endif

    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.mmu = true;
    cpu->cfg.pmp = true;
}

static void rv32_sifive_e_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    riscv_cpu_set_misa(env, MXL_RV32, RVI | RVM | RVA | RVC | RVU);
    env->priv_ver = PRIV_VERSION_1_10_0;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_MBARE);
#endif

    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.pmp = true;
}

static void rv32_ibex_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    riscv_cpu_set_misa(env, MXL_RV32, RVI | RVM | RVC | RVU);
    env->priv_ver = PRIV_VERSION_1_11_0;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_MBARE);
#endif
    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.pmp = true;
    cpu->cfg.ext_smepmp = true;
}

static void rv32_imafcu_nommu_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    riscv_cpu_set_misa(env, MXL_RV32, RVI | RVM | RVA | RVF | RVC | RVU);
    env->priv_ver = PRIV_VERSION_1_10_0;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_MBARE);
#endif

    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.pmp = true;
}
#endif

static ObjectClass *riscv_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;
    char **cpuname;

    cpuname = g_strsplit(cpu_model, ",", 1);
    typename = g_strdup_printf(RISCV_CPU_TYPE_NAME("%s"), cpuname[0]);
    oc = object_class_by_name(typename);
    g_strfreev(cpuname);
    g_free(typename);
    if (!oc || !object_class_dynamic_cast(oc, TYPE_RISCV_CPU) ||
        object_class_is_abstract(oc)) {
        return NULL;
    }
    return oc;
}

char *riscv_cpu_get_name(RISCVCPU *cpu)
{
    RISCVCPUClass *rcc = RISCV_CPU_GET_CLASS(cpu);
    const char *typename = object_class_get_name(OBJECT_CLASS(rcc));

    g_assert(g_str_has_suffix(typename, RISCV_CPU_TYPE_SUFFIX));

    return g_strndup(typename,
                     strlen(typename) - strlen(RISCV_CPU_TYPE_SUFFIX));
}

static void riscv_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    int i, j;
    uint8_t *p;

#if !defined(CONFIG_USER_ONLY)
    if (riscv_has_ext(env, RVH)) {
        qemu_fprintf(f, " %s %d\n", "V      =  ", env->virt_enabled);
    }
#endif
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "pc      ", env->pc);
#ifndef CONFIG_USER_ONLY
    {
        static const int dump_csrs[] = {
            CSR_MHARTID,
            CSR_MSTATUS,
            CSR_MSTATUSH,
            /*
             * CSR_SSTATUS is intentionally omitted here as its value
             * can be figured out by looking at CSR_MSTATUS
             */
            CSR_HSTATUS,
            CSR_VSSTATUS,
            CSR_MIP,
            CSR_MIE,
            CSR_MIDELEG,
            CSR_HIDELEG,
            CSR_MEDELEG,
            CSR_HEDELEG,
            CSR_MTVEC,
            CSR_STVEC,
            CSR_VSTVEC,
            CSR_MEPC,
            CSR_SEPC,
            CSR_VSEPC,
            CSR_MCAUSE,
            CSR_SCAUSE,
            CSR_VSCAUSE,
            CSR_MTVAL,
            CSR_STVAL,
            CSR_HTVAL,
            CSR_MTVAL2,
            CSR_MSCRATCH,
            CSR_SSCRATCH,
            CSR_SATP,
            CSR_MMTE,
            CSR_UPMBASE,
            CSR_UPMMASK,
            CSR_SPMBASE,
            CSR_SPMMASK,
            CSR_MPMBASE,
            CSR_MPMMASK,
        };

        for (i = 0; i < ARRAY_SIZE(dump_csrs); ++i) {
            int csrno = dump_csrs[i];
            target_ulong val = 0;
            RISCVException res = riscv_csrrw_debug(env, csrno, &val, 0, 0);

            /*
             * Rely on the smode, hmode, etc, predicates within csr.c
             * to do the filtering of the registers that are present.
             */
            if (res == RISCV_EXCP_NONE) {
                qemu_fprintf(f, " %-8s " TARGET_FMT_lx "\n",
                             csr_ops[csrno].name, val);
            }
        }
    }
#endif

    for (i = 0; i < 32; i++) {
        qemu_fprintf(f, " %-8s " TARGET_FMT_lx,
                     riscv_int_regnames[i], env->gpr[i]);
        if ((i & 3) == 3) {
            qemu_fprintf(f, "\n");
        }
    }
    if (flags & CPU_DUMP_FPU) {
        for (i = 0; i < 32; i++) {
            qemu_fprintf(f, " %-8s %016" PRIx64,
                         riscv_fpr_regnames[i], env->fpr[i]);
            if ((i & 3) == 3) {
                qemu_fprintf(f, "\n");
            }
        }
    }
    if (riscv_has_ext(env, RVV) && (flags & CPU_DUMP_VPU)) {
        static const int dump_rvv_csrs[] = {
                    CSR_VSTART,
                    CSR_VXSAT,
                    CSR_VXRM,
                    CSR_VCSR,
                    CSR_VL,
                    CSR_VTYPE,
                    CSR_VLENB,
                };
        for (i = 0; i < ARRAY_SIZE(dump_rvv_csrs); ++i) {
            int csrno = dump_rvv_csrs[i];
            target_ulong val = 0;
            RISCVException res = riscv_csrrw_debug(env, csrno, &val, 0, 0);

            /*
             * Rely on the smode, hmode, etc, predicates within csr.c
             * to do the filtering of the registers that are present.
             */
            if (res == RISCV_EXCP_NONE) {
                qemu_fprintf(f, " %-8s " TARGET_FMT_lx "\n",
                             csr_ops[csrno].name, val);
            }
        }
        uint16_t vlenb = cpu->cfg.vlen >> 3;

        for (i = 0; i < 32; i++) {
            qemu_fprintf(f, " %-8s ", riscv_rvv_regnames[i]);
            p = (uint8_t *)env->vreg;
            for (j = vlenb - 1 ; j >= 0; j--) {
                qemu_fprintf(f, "%02x", *(p + i * vlenb + BYTE(j)));
            }
            qemu_fprintf(f, "\n");
        }
    }
}

static void riscv_cpu_set_pc(CPUState *cs, vaddr value)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (env->xl == MXL_RV32) {
        env->pc = (int32_t)value;
    } else {
        env->pc = value;
    }
}

static vaddr riscv_cpu_get_pc(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    /* Match cpu_get_tb_cpu_state. */
    if (env->xl == MXL_RV32) {
        return env->pc & UINT32_MAX;
    }
    return env->pc;
}

static bool riscv_cpu_has_work(CPUState *cs)
{
#ifndef CONFIG_USER_ONLY
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    /*
     * Definition of the WFI instruction requires it to ignore the privilege
     * mode and delegation registers, but respect individual enables
     */
    return riscv_cpu_all_pending(env) != 0 ||
        riscv_cpu_sirq_pending(env) != RISCV_EXCP_NONE ||
        riscv_cpu_vsirq_pending(env) != RISCV_EXCP_NONE;
#else
    return true;
#endif
}

static void riscv_cpu_reset_hold(Object *obj)
{
#ifndef CONFIG_USER_ONLY
    uint8_t iprio;
    int i, irq, rdzero;
#endif
    CPUState *cs = CPU(obj);
    RISCVCPU *cpu = RISCV_CPU(cs);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    CPURISCVState *env = &cpu->env;

    if (mcc->parent_phases.hold) {
        mcc->parent_phases.hold(obj);
    }
#ifndef CONFIG_USER_ONLY
    env->misa_mxl = env->misa_mxl_max;
    env->priv = PRV_M;
    env->mstatus &= ~(MSTATUS_MIE | MSTATUS_MPRV);
    if (env->misa_mxl > MXL_RV32) {
        /*
         * The reset status of SXL/UXL is undefined, but mstatus is WARL
         * and we must ensure that the value after init is valid for read.
         */
        env->mstatus = set_field(env->mstatus, MSTATUS64_SXL, env->misa_mxl);
        env->mstatus = set_field(env->mstatus, MSTATUS64_UXL, env->misa_mxl);
        if (riscv_has_ext(env, RVH)) {
            env->vsstatus = set_field(env->vsstatus,
                                      MSTATUS64_SXL, env->misa_mxl);
            env->vsstatus = set_field(env->vsstatus,
                                      MSTATUS64_UXL, env->misa_mxl);
            env->mstatus_hs = set_field(env->mstatus_hs,
                                        MSTATUS64_SXL, env->misa_mxl);
            env->mstatus_hs = set_field(env->mstatus_hs,
                                        MSTATUS64_UXL, env->misa_mxl);
        }
    }
    env->mcause = 0;
    env->miclaim = MIP_SGEIP;
    env->pc = env->resetvec;
    env->bins = 0;
    env->two_stage_lookup = false;

    env->menvcfg = (cpu->cfg.ext_svpbmt ? MENVCFG_PBMTE : 0) |
                   (cpu->cfg.ext_svadu ? MENVCFG_ADUE : 0);
    env->henvcfg = (cpu->cfg.ext_svpbmt ? HENVCFG_PBMTE : 0) |
                   (cpu->cfg.ext_svadu ? HENVCFG_ADUE : 0);

    /* Initialized default priorities of local interrupts. */
    for (i = 0; i < ARRAY_SIZE(env->miprio); i++) {
        iprio = riscv_cpu_default_priority(i);
        env->miprio[i] = (i == IRQ_M_EXT) ? 0 : iprio;
        env->siprio[i] = (i == IRQ_S_EXT) ? 0 : iprio;
        env->hviprio[i] = 0;
    }
    i = 0;
    while (!riscv_cpu_hviprio_index2irq(i, &irq, &rdzero)) {
        if (!rdzero) {
            env->hviprio[irq] = env->miprio[irq];
        }
        i++;
    }
    /* mmte is supposed to have pm.current hardwired to 1 */
    env->mmte |= (EXT_STATUS_INITIAL | MMTE_M_PM_CURRENT);

    /*
     * Clear mseccfg and unlock all the PMP entries upon reset.
     * This is allowed as per the priv and smepmp specifications
     * and is needed to clear stale entries across reboots.
     */
    if (riscv_cpu_cfg(env)->ext_smepmp) {
        env->mseccfg = 0;
    }

    pmp_unlock_entries(env);
#endif
    env->xl = riscv_cpu_mxl(env);
    riscv_cpu_update_mask(env);
    cs->exception_index = RISCV_EXCP_NONE;
    env->load_res = -1;
    set_default_nan_mode(1, &env->fp_status);

#ifndef CONFIG_USER_ONLY
    if (cpu->cfg.debug) {
        riscv_trigger_reset_hold(env);
    }

    if (kvm_enabled()) {
        kvm_riscv_reset_vcpu(cpu);
    }
#endif
}

static void riscv_cpu_disas_set_info(CPUState *s, disassemble_info *info)
{
    RISCVCPU *cpu = RISCV_CPU(s);
    CPURISCVState *env = &cpu->env;
    info->target_info = &cpu->cfg;

    switch (env->xl) {
    case MXL_RV32:
        info->print_insn = print_insn_riscv32;
        break;
    case MXL_RV64:
        info->print_insn = print_insn_riscv64;
        break;
    case MXL_RV128:
        info->print_insn = print_insn_riscv128;
        break;
    default:
        g_assert_not_reached();
    }
}

#ifndef CONFIG_USER_ONLY
static void riscv_cpu_satp_mode_finalize(RISCVCPU *cpu, Error **errp)
{
    bool rv32 = riscv_cpu_mxl(&cpu->env) == MXL_RV32;
    uint8_t satp_mode_map_max, satp_mode_supported_max;

    /* The CPU wants the OS to decide which satp mode to use */
    if (cpu->cfg.satp_mode.supported == 0) {
        return;
    }

    satp_mode_supported_max =
                    satp_mode_max_from_map(cpu->cfg.satp_mode.supported);

    if (cpu->cfg.satp_mode.map == 0) {
        if (cpu->cfg.satp_mode.init == 0) {
            /* If unset by the user, we fallback to the default satp mode. */
            set_satp_mode_default_map(cpu);
        } else {
            /*
             * Find the lowest level that was disabled and then enable the
             * first valid level below which can be found in
             * valid_vm_1_10_32/64.
             */
            for (int i = 1; i < 16; ++i) {
                if ((cpu->cfg.satp_mode.init & (1 << i)) &&
                    (cpu->cfg.satp_mode.supported & (1 << i))) {
                    for (int j = i - 1; j >= 0; --j) {
                        if (cpu->cfg.satp_mode.supported & (1 << j)) {
                            cpu->cfg.satp_mode.map |= (1 << j);
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }

    satp_mode_map_max = satp_mode_max_from_map(cpu->cfg.satp_mode.map);

    /* Make sure the user asked for a supported configuration (HW and qemu) */
    if (satp_mode_map_max > satp_mode_supported_max) {
        error_setg(errp, "satp_mode %s is higher than hw max capability %s",
                   satp_mode_str(satp_mode_map_max, rv32),
                   satp_mode_str(satp_mode_supported_max, rv32));
        return;
    }

    /*
     * Make sure the user did not ask for an invalid configuration as per
     * the specification.
     */
    if (!rv32) {
        for (int i = satp_mode_map_max - 1; i >= 0; --i) {
            if (!(cpu->cfg.satp_mode.map & (1 << i)) &&
                (cpu->cfg.satp_mode.init & (1 << i)) &&
                (cpu->cfg.satp_mode.supported & (1 << i))) {
                error_setg(errp, "cannot disable %s satp mode if %s "
                           "is enabled", satp_mode_str(i, false),
                           satp_mode_str(satp_mode_map_max, false));
                return;
            }
        }
    }

    /* Finally expand the map so that all valid modes are set */
    for (int i = satp_mode_map_max - 1; i >= 0; --i) {
        if (cpu->cfg.satp_mode.supported & (1 << i)) {
            cpu->cfg.satp_mode.map |= (1 << i);
        }
    }
}
#endif

void riscv_cpu_finalize_features(RISCVCPU *cpu, Error **errp)
{
    Error *local_err = NULL;

    /*
     * KVM accel does not have a specialized finalize()
     * callback because its extensions are validated
     * in the get()/set() callbacks of each property.
     */
    if (tcg_enabled()) {
        riscv_tcg_cpu_finalize_features(cpu, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            return;
        }
    }

#ifndef CONFIG_USER_ONLY
    riscv_cpu_satp_mode_finalize(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
#endif
}

static void riscv_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    RISCVCPU *cpu = RISCV_CPU(dev);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    if (object_dynamic_cast(OBJECT(dev), TYPE_RISCV_CPU_ANY) != NULL) {
        warn_report("The 'any' CPU is deprecated and will be "
                    "removed in the future.");
    }

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    riscv_cpu_finalize_features(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    riscv_cpu_register_gdb_regs_for_features(cs);

#ifndef CONFIG_USER_ONLY
    if (cpu->cfg.debug) {
        riscv_trigger_realize(&cpu->env);
    }
#endif

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    mcc->parent_realize(dev, errp);
}

bool riscv_cpu_accelerator_compatible(RISCVCPU *cpu)
{
    if (tcg_enabled()) {
        return riscv_cpu_tcg_compatible(cpu);
    }

    return true;
}

#ifndef CONFIG_USER_ONLY
static void cpu_riscv_get_satp(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    RISCVSATPMap *satp_map = opaque;
    uint8_t satp = satp_mode_from_str(name);
    bool value;

    value = satp_map->map & (1 << satp);

    visit_type_bool(v, name, &value, errp);
}

static void cpu_riscv_set_satp(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    RISCVSATPMap *satp_map = opaque;
    uint8_t satp = satp_mode_from_str(name);
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    satp_map->map = deposit32(satp_map->map, satp, 1, value);
    satp_map->init |= 1 << satp;
}

void riscv_add_satp_mode_properties(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);

    if (cpu->env.misa_mxl == MXL_RV32) {
        object_property_add(obj, "sv32", "bool", cpu_riscv_get_satp,
                            cpu_riscv_set_satp, NULL, &cpu->cfg.satp_mode);
    } else {
        object_property_add(obj, "sv39", "bool", cpu_riscv_get_satp,
                            cpu_riscv_set_satp, NULL, &cpu->cfg.satp_mode);
        object_property_add(obj, "sv48", "bool", cpu_riscv_get_satp,
                            cpu_riscv_set_satp, NULL, &cpu->cfg.satp_mode);
        object_property_add(obj, "sv57", "bool", cpu_riscv_get_satp,
                            cpu_riscv_set_satp, NULL, &cpu->cfg.satp_mode);
        object_property_add(obj, "sv64", "bool", cpu_riscv_get_satp,
                            cpu_riscv_set_satp, NULL, &cpu->cfg.satp_mode);
    }
}

static void riscv_cpu_set_irq(void *opaque, int irq, int level)
{
    RISCVCPU *cpu = RISCV_CPU(opaque);
    CPURISCVState *env = &cpu->env;

    if (irq < IRQ_LOCAL_MAX) {
        switch (irq) {
        case IRQ_U_SOFT:
        case IRQ_S_SOFT:
        case IRQ_VS_SOFT:
        case IRQ_M_SOFT:
        case IRQ_U_TIMER:
        case IRQ_S_TIMER:
        case IRQ_VS_TIMER:
        case IRQ_M_TIMER:
        case IRQ_U_EXT:
        case IRQ_VS_EXT:
        case IRQ_M_EXT:
            if (kvm_enabled()) {
                kvm_riscv_set_irq(cpu, irq, level);
            } else {
                riscv_cpu_update_mip(env, 1 << irq, BOOL_TO_MASK(level));
            }
             break;
        case IRQ_S_EXT:
            if (kvm_enabled()) {
                kvm_riscv_set_irq(cpu, irq, level);
            } else {
                env->external_seip = level;
                riscv_cpu_update_mip(env, 1 << irq,
                                     BOOL_TO_MASK(level | env->software_seip));
            }
            break;
        default:
            g_assert_not_reached();
        }
    } else if (irq < (IRQ_LOCAL_MAX + IRQ_LOCAL_GUEST_MAX)) {
        /* Require H-extension for handling guest local interrupts */
        if (!riscv_has_ext(env, RVH)) {
            g_assert_not_reached();
        }

        /* Compute bit position in HGEIP CSR */
        irq = irq - IRQ_LOCAL_MAX + 1;
        if (env->geilen < irq) {
            g_assert_not_reached();
        }

        /* Update HGEIP CSR */
        env->hgeip &= ~((target_ulong)1 << irq);
        if (level) {
            env->hgeip |= (target_ulong)1 << irq;
        }

        /* Update mip.SGEIP bit */
        riscv_cpu_update_mip(env, MIP_SGEIP,
                             BOOL_TO_MASK(!!(env->hgeie & env->hgeip)));
    } else {
        g_assert_not_reached();
    }
}
#endif /* CONFIG_USER_ONLY */

static bool riscv_cpu_is_dynamic(Object *cpu_obj)
{
    return object_dynamic_cast(cpu_obj, TYPE_RISCV_DYNAMIC_CPU) != NULL;
}

static void riscv_cpu_post_init(Object *obj)
{
    accel_cpu_instance_init(CPU(obj));
}

static void riscv_cpu_init(Object *obj)
{
#ifndef CONFIG_USER_ONLY
    qdev_init_gpio_in(DEVICE(obj), riscv_cpu_set_irq,
                      IRQ_LOCAL_MAX + IRQ_LOCAL_GUEST_MAX);
#endif /* CONFIG_USER_ONLY */

    /*
     * The timer and performance counters extensions were supported
     * in QEMU before they were added as discrete extensions in the
     * ISA. To keep compatibility we'll always default them to 'true'
     * for all CPUs. Each accelerator will decide what to do when
     * users disable them.
     */
    RISCV_CPU(obj)->cfg.ext_zicntr = true;
    RISCV_CPU(obj)->cfg.ext_zihpm = true;
}

typedef struct misa_ext_info {
    const char *name;
    const char *description;
} MISAExtInfo;

#define MISA_INFO_IDX(_bit) \
    __builtin_ctz(_bit)

#define MISA_EXT_INFO(_bit, _propname, _descr) \
    [MISA_INFO_IDX(_bit)] = {.name = _propname, .description = _descr}

static const MISAExtInfo misa_ext_info_arr[] = {
    MISA_EXT_INFO(RVA, "a", "Atomic instructions"),
    MISA_EXT_INFO(RVC, "c", "Compressed instructions"),
    MISA_EXT_INFO(RVD, "d", "Double-precision float point"),
    MISA_EXT_INFO(RVF, "f", "Single-precision float point"),
    MISA_EXT_INFO(RVI, "i", "Base integer instruction set"),
    MISA_EXT_INFO(RVE, "e", "Base integer instruction set (embedded)"),
    MISA_EXT_INFO(RVM, "m", "Integer multiplication and division"),
    MISA_EXT_INFO(RVS, "s", "Supervisor-level instructions"),
    MISA_EXT_INFO(RVU, "u", "User-level instructions"),
    MISA_EXT_INFO(RVH, "h", "Hypervisor"),
    MISA_EXT_INFO(RVJ, "x-j", "Dynamic translated languages"),
    MISA_EXT_INFO(RVV, "v", "Vector operations"),
    MISA_EXT_INFO(RVG, "g", "General purpose (IMAFD_Zicsr_Zifencei)"),
};

static int riscv_validate_misa_info_idx(uint32_t bit)
{
    int idx;

    /*
     * Our lowest valid input (RVA) is 1 and
     * __builtin_ctz() is UB with zero.
     */
    g_assert(bit != 0);
    idx = MISA_INFO_IDX(bit);

    g_assert(idx < ARRAY_SIZE(misa_ext_info_arr));
    return idx;
}

const char *riscv_get_misa_ext_name(uint32_t bit)
{
    int idx = riscv_validate_misa_info_idx(bit);
    const char *val = misa_ext_info_arr[idx].name;

    g_assert(val != NULL);
    return val;
}

const char *riscv_get_misa_ext_description(uint32_t bit)
{
    int idx = riscv_validate_misa_info_idx(bit);
    const char *val = misa_ext_info_arr[idx].description;

    g_assert(val != NULL);
    return val;
}

#define MULTI_EXT_CFG_BOOL(_name, _prop, _defval) \
    {.name = _name, .offset = CPU_CFG_OFFSET(_prop), \
     .enabled = _defval}

const RISCVCPUMultiExtConfig riscv_cpu_extensions[] = {
    /* Defaults for standard extensions */
    MULTI_EXT_CFG_BOOL("sscofpmf", ext_sscofpmf, false),
    MULTI_EXT_CFG_BOOL("zifencei", ext_zifencei, true),
    MULTI_EXT_CFG_BOOL("zicsr", ext_zicsr, true),
    MULTI_EXT_CFG_BOOL("zihintntl", ext_zihintntl, true),
    MULTI_EXT_CFG_BOOL("zihintpause", ext_zihintpause, true),
    MULTI_EXT_CFG_BOOL("zawrs", ext_zawrs, true),
    MULTI_EXT_CFG_BOOL("zfa", ext_zfa, true),
    MULTI_EXT_CFG_BOOL("zfh", ext_zfh, false),
    MULTI_EXT_CFG_BOOL("zfhmin", ext_zfhmin, false),
    MULTI_EXT_CFG_BOOL("zve32f", ext_zve32f, false),
    MULTI_EXT_CFG_BOOL("zve64f", ext_zve64f, false),
    MULTI_EXT_CFG_BOOL("zve64d", ext_zve64d, false),
    MULTI_EXT_CFG_BOOL("sstc", ext_sstc, true),

    MULTI_EXT_CFG_BOOL("smepmp", ext_smepmp, false),
    MULTI_EXT_CFG_BOOL("smstateen", ext_smstateen, false),
    MULTI_EXT_CFG_BOOL("svadu", ext_svadu, true),
    MULTI_EXT_CFG_BOOL("svinval", ext_svinval, false),
    MULTI_EXT_CFG_BOOL("svnapot", ext_svnapot, false),
    MULTI_EXT_CFG_BOOL("svpbmt", ext_svpbmt, false),

    MULTI_EXT_CFG_BOOL("zicntr", ext_zicntr, true),
    MULTI_EXT_CFG_BOOL("zihpm", ext_zihpm, true),

    MULTI_EXT_CFG_BOOL("zba", ext_zba, true),
    MULTI_EXT_CFG_BOOL("zbb", ext_zbb, true),
    MULTI_EXT_CFG_BOOL("zbc", ext_zbc, true),
    MULTI_EXT_CFG_BOOL("zbkb", ext_zbkb, false),
    MULTI_EXT_CFG_BOOL("zbkc", ext_zbkc, false),
    MULTI_EXT_CFG_BOOL("zbkx", ext_zbkx, false),
    MULTI_EXT_CFG_BOOL("zbs", ext_zbs, true),
    MULTI_EXT_CFG_BOOL("zk", ext_zk, false),
    MULTI_EXT_CFG_BOOL("zkn", ext_zkn, false),
    MULTI_EXT_CFG_BOOL("zknd", ext_zknd, false),
    MULTI_EXT_CFG_BOOL("zkne", ext_zkne, false),
    MULTI_EXT_CFG_BOOL("zknh", ext_zknh, false),
    MULTI_EXT_CFG_BOOL("zkr", ext_zkr, false),
    MULTI_EXT_CFG_BOOL("zks", ext_zks, false),
    MULTI_EXT_CFG_BOOL("zksed", ext_zksed, false),
    MULTI_EXT_CFG_BOOL("zksh", ext_zksh, false),
    MULTI_EXT_CFG_BOOL("zkt", ext_zkt, false),

    MULTI_EXT_CFG_BOOL("zdinx", ext_zdinx, false),
    MULTI_EXT_CFG_BOOL("zfinx", ext_zfinx, false),
    MULTI_EXT_CFG_BOOL("zhinx", ext_zhinx, false),
    MULTI_EXT_CFG_BOOL("zhinxmin", ext_zhinxmin, false),

    MULTI_EXT_CFG_BOOL("zicbom", ext_zicbom, true),
    MULTI_EXT_CFG_BOOL("zicboz", ext_zicboz, true),

    MULTI_EXT_CFG_BOOL("zmmul", ext_zmmul, false),

    MULTI_EXT_CFG_BOOL("zca", ext_zca, false),
    MULTI_EXT_CFG_BOOL("zcb", ext_zcb, false),
    MULTI_EXT_CFG_BOOL("zcd", ext_zcd, false),
    MULTI_EXT_CFG_BOOL("zce", ext_zce, false),
    MULTI_EXT_CFG_BOOL("zcf", ext_zcf, false),
    MULTI_EXT_CFG_BOOL("zcmp", ext_zcmp, false),
    MULTI_EXT_CFG_BOOL("zcmt", ext_zcmt, false),
    MULTI_EXT_CFG_BOOL("zicond", ext_zicond, false),

    /* Vector cryptography extensions */
    MULTI_EXT_CFG_BOOL("zvbb", ext_zvbb, false),
    MULTI_EXT_CFG_BOOL("zvbc", ext_zvbc, false),
    MULTI_EXT_CFG_BOOL("zvkb", ext_zvkg, false),
    MULTI_EXT_CFG_BOOL("zvkg", ext_zvkg, false),
    MULTI_EXT_CFG_BOOL("zvkned", ext_zvkned, false),
    MULTI_EXT_CFG_BOOL("zvknha", ext_zvknha, false),
    MULTI_EXT_CFG_BOOL("zvknhb", ext_zvknhb, false),
    MULTI_EXT_CFG_BOOL("zvksed", ext_zvksed, false),
    MULTI_EXT_CFG_BOOL("zvksh", ext_zvksh, false),
    MULTI_EXT_CFG_BOOL("zvkt", ext_zvkt, false),
    MULTI_EXT_CFG_BOOL("zvkn", ext_zvkn, false),
    MULTI_EXT_CFG_BOOL("zvknc", ext_zvknc, false),
    MULTI_EXT_CFG_BOOL("zvkng", ext_zvkng, false),
    MULTI_EXT_CFG_BOOL("zvks", ext_zvks, false),
    MULTI_EXT_CFG_BOOL("zvksc", ext_zvksc, false),
    MULTI_EXT_CFG_BOOL("zvksg", ext_zvksg, false),

    DEFINE_PROP_END_OF_LIST(),
};

const RISCVCPUMultiExtConfig riscv_cpu_vendor_exts[] = {
    MULTI_EXT_CFG_BOOL("xtheadba", ext_xtheadba, false),
    MULTI_EXT_CFG_BOOL("xtheadbb", ext_xtheadbb, false),
    MULTI_EXT_CFG_BOOL("xtheadbs", ext_xtheadbs, false),
    MULTI_EXT_CFG_BOOL("xtheadcmo", ext_xtheadcmo, false),
    MULTI_EXT_CFG_BOOL("xtheadcondmov", ext_xtheadcondmov, false),
    MULTI_EXT_CFG_BOOL("xtheadfmemidx", ext_xtheadfmemidx, false),
    MULTI_EXT_CFG_BOOL("xtheadfmv", ext_xtheadfmv, false),
    MULTI_EXT_CFG_BOOL("xtheadmac", ext_xtheadmac, false),
    MULTI_EXT_CFG_BOOL("xtheadmemidx", ext_xtheadmemidx, false),
    MULTI_EXT_CFG_BOOL("xtheadmempair", ext_xtheadmempair, false),
    MULTI_EXT_CFG_BOOL("xtheadsync", ext_xtheadsync, false),
    MULTI_EXT_CFG_BOOL("xventanacondops", ext_XVentanaCondOps, false),

    DEFINE_PROP_END_OF_LIST(),
};

/* These are experimental so mark with 'x-' */
const RISCVCPUMultiExtConfig riscv_cpu_experimental_exts[] = {
    MULTI_EXT_CFG_BOOL("x-smaia", ext_smaia, false),
    MULTI_EXT_CFG_BOOL("x-ssaia", ext_ssaia, false),

    MULTI_EXT_CFG_BOOL("x-zvfh", ext_zvfh, false),
    MULTI_EXT_CFG_BOOL("x-zvfhmin", ext_zvfhmin, false),

    MULTI_EXT_CFG_BOOL("x-zfbfmin", ext_zfbfmin, false),
    MULTI_EXT_CFG_BOOL("x-zvfbfmin", ext_zvfbfmin, false),
    MULTI_EXT_CFG_BOOL("x-zvfbfwma", ext_zvfbfwma, false),

    DEFINE_PROP_END_OF_LIST(),
};

/* Deprecated entries marked for future removal */
const RISCVCPUMultiExtConfig riscv_cpu_deprecated_exts[] = {
    MULTI_EXT_CFG_BOOL("Zifencei", ext_zifencei, true),
    MULTI_EXT_CFG_BOOL("Zicsr", ext_zicsr, true),
    MULTI_EXT_CFG_BOOL("Zihintntl", ext_zihintntl, true),
    MULTI_EXT_CFG_BOOL("Zihintpause", ext_zihintpause, true),
    MULTI_EXT_CFG_BOOL("Zawrs", ext_zawrs, true),
    MULTI_EXT_CFG_BOOL("Zfa", ext_zfa, true),
    MULTI_EXT_CFG_BOOL("Zfh", ext_zfh, false),
    MULTI_EXT_CFG_BOOL("Zfhmin", ext_zfhmin, false),
    MULTI_EXT_CFG_BOOL("Zve32f", ext_zve32f, false),
    MULTI_EXT_CFG_BOOL("Zve64f", ext_zve64f, false),
    MULTI_EXT_CFG_BOOL("Zve64d", ext_zve64d, false),

    DEFINE_PROP_END_OF_LIST(),
};

Property riscv_cpu_options[] = {
    DEFINE_PROP_UINT8("pmu-num", RISCVCPU, cfg.pmu_num, 16),

    DEFINE_PROP_BOOL("mmu", RISCVCPU, cfg.mmu, true),
    DEFINE_PROP_BOOL("pmp", RISCVCPU, cfg.pmp, true),

    DEFINE_PROP_STRING("priv_spec", RISCVCPU, cfg.priv_spec),
    DEFINE_PROP_STRING("vext_spec", RISCVCPU, cfg.vext_spec),

    DEFINE_PROP_UINT16("vlen", RISCVCPU, cfg.vlen, 128),
    DEFINE_PROP_UINT16("elen", RISCVCPU, cfg.elen, 64),

    DEFINE_PROP_UINT16("cbom_blocksize", RISCVCPU, cfg.cbom_blocksize, 64),
    DEFINE_PROP_UINT16("cboz_blocksize", RISCVCPU, cfg.cboz_blocksize, 64),

    DEFINE_PROP_END_OF_LIST(),
};

static Property riscv_cpu_properties[] = {
    DEFINE_PROP_BOOL("debug", RISCVCPU, cfg.debug, true),

#ifndef CONFIG_USER_ONLY
    DEFINE_PROP_UINT64("resetvec", RISCVCPU, env.resetvec, DEFAULT_RSTVEC),
#endif

    DEFINE_PROP_BOOL("short-isa-string", RISCVCPU, cfg.short_isa_string, false),

    DEFINE_PROP_BOOL("rvv_ta_all_1s", RISCVCPU, cfg.rvv_ta_all_1s, false),
    DEFINE_PROP_BOOL("rvv_ma_all_1s", RISCVCPU, cfg.rvv_ma_all_1s, false),

    /*
     * write_misa() is marked as experimental for now so mark
     * it with -x and default to 'false'.
     */
    DEFINE_PROP_BOOL("x-misa-w", RISCVCPU, cfg.misa_w, false),
    DEFINE_PROP_END_OF_LIST(),
};

static const gchar *riscv_gdb_arch_name(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        return "riscv:rv32";
    case MXL_RV64:
    case MXL_RV128:
        return "riscv:rv64";
    default:
        g_assert_not_reached();
    }
}

static const char *riscv_gdb_get_dynamic_xml(CPUState *cs, const char *xmlname)
{
    RISCVCPU *cpu = RISCV_CPU(cs);

    if (strcmp(xmlname, "riscv-csr.xml") == 0) {
        return cpu->dyn_csr_xml;
    } else if (strcmp(xmlname, "riscv-vector.xml") == 0) {
        return cpu->dyn_vreg_xml;
    }

    return NULL;
}

#ifndef CONFIG_USER_ONLY
static int64_t riscv_get_arch_id(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);

    return cpu->env.mhartid;
}

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps riscv_sysemu_ops = {
    .get_phys_page_debug = riscv_cpu_get_phys_page_debug,
    .write_elf64_note = riscv_cpu_write_elf64_note,
    .write_elf32_note = riscv_cpu_write_elf32_note,
    .legacy_vmsd = &vmstate_riscv_cpu,
};
#endif

static void cpu_set_mvendorid(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    bool dynamic_cpu = riscv_cpu_is_dynamic(obj);
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint32_t prev_val = cpu->cfg.mvendorid;
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    if (!dynamic_cpu && prev_val != value) {
        error_setg(errp, "Unable to change %s mvendorid (0x%x)",
                   object_get_typename(obj), prev_val);
        return;
    }

    cpu->cfg.mvendorid = value;
}

static void cpu_get_mvendorid(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    bool value = RISCV_CPU(obj)->cfg.mvendorid;

    visit_type_bool(v, name, &value, errp);
}

static void cpu_set_mimpid(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    bool dynamic_cpu = riscv_cpu_is_dynamic(obj);
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint64_t prev_val = cpu->cfg.mimpid;
    uint64_t value;

    if (!visit_type_uint64(v, name, &value, errp)) {
        return;
    }

    if (!dynamic_cpu && prev_val != value) {
        error_setg(errp, "Unable to change %s mimpid (0x%" PRIu64 ")",
                   object_get_typename(obj), prev_val);
        return;
    }

    cpu->cfg.mimpid = value;
}

static void cpu_get_mimpid(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    bool value = RISCV_CPU(obj)->cfg.mimpid;

    visit_type_bool(v, name, &value, errp);
}

static void cpu_set_marchid(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    bool dynamic_cpu = riscv_cpu_is_dynamic(obj);
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint64_t prev_val = cpu->cfg.marchid;
    uint64_t value, invalid_val;
    uint32_t mxlen = 0;

    if (!visit_type_uint64(v, name, &value, errp)) {
        return;
    }

    if (!dynamic_cpu && prev_val != value) {
        error_setg(errp, "Unable to change %s marchid (0x%" PRIu64 ")",
                   object_get_typename(obj), prev_val);
        return;
    }

    switch (riscv_cpu_mxl(&cpu->env)) {
    case MXL_RV32:
        mxlen = 32;
        break;
    case MXL_RV64:
    case MXL_RV128:
        mxlen = 64;
        break;
    default:
        g_assert_not_reached();
    }

    invalid_val = 1LL << (mxlen - 1);

    if (value == invalid_val) {
        error_setg(errp, "Unable to set marchid with MSB (%u) bit set "
                         "and the remaining bits zero", mxlen);
        return;
    }

    cpu->cfg.marchid = value;
}

static void cpu_get_marchid(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    bool value = RISCV_CPU(obj)->cfg.marchid;

    visit_type_bool(v, name, &value, errp);
}

static void riscv_cpu_class_init(ObjectClass *c, void *data)
{
    RISCVCPUClass *mcc = RISCV_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);
    ResettableClass *rc = RESETTABLE_CLASS(c);

    device_class_set_parent_realize(dc, riscv_cpu_realize,
                                    &mcc->parent_realize);

    resettable_class_set_parent_phases(rc, NULL, riscv_cpu_reset_hold, NULL,
                                       &mcc->parent_phases);

    cc->class_by_name = riscv_cpu_class_by_name;
    cc->has_work = riscv_cpu_has_work;
    cc->dump_state = riscv_cpu_dump_state;
    cc->set_pc = riscv_cpu_set_pc;
    cc->get_pc = riscv_cpu_get_pc;
    cc->gdb_read_register = riscv_cpu_gdb_read_register;
    cc->gdb_write_register = riscv_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 33;
    cc->gdb_stop_before_watchpoint = true;
    cc->disas_set_info = riscv_cpu_disas_set_info;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &riscv_sysemu_ops;
    cc->get_arch_id = riscv_get_arch_id;
#endif
    cc->gdb_arch_name = riscv_gdb_arch_name;
    cc->gdb_get_dynamic_xml = riscv_gdb_get_dynamic_xml;

    object_class_property_add(c, "mvendorid", "uint32", cpu_get_mvendorid,
                              cpu_set_mvendorid, NULL, NULL);

    object_class_property_add(c, "mimpid", "uint64", cpu_get_mimpid,
                              cpu_set_mimpid, NULL, NULL);

    object_class_property_add(c, "marchid", "uint64", cpu_get_marchid,
                              cpu_set_marchid, NULL, NULL);

    device_class_set_props(dc, riscv_cpu_properties);
}

static void riscv_isa_string_ext(RISCVCPU *cpu, char **isa_str,
                                 int max_str_len)
{
    const RISCVIsaExtData *edata;
    char *old = *isa_str;
    char *new = *isa_str;

    for (edata = isa_edata_arr; edata && edata->name; edata++) {
        if (isa_ext_is_enabled(cpu, edata->ext_enable_offset)) {
            new = g_strconcat(old, "_", edata->name, NULL);
            g_free(old);
            old = new;
        }
    }

    *isa_str = new;
}

char *riscv_isa_string(RISCVCPU *cpu)
{
    int i;
    const size_t maxlen = sizeof("rv128") + sizeof(riscv_single_letter_exts);
    char *isa_str = g_new(char, maxlen);
    char *p = isa_str + snprintf(isa_str, maxlen, "rv%d", TARGET_LONG_BITS);
    for (i = 0; i < sizeof(riscv_single_letter_exts) - 1; i++) {
        if (cpu->env.misa_ext & RV(riscv_single_letter_exts[i])) {
            *p++ = qemu_tolower(riscv_single_letter_exts[i]);
        }
    }
    *p = '\0';
    if (!cpu->cfg.short_isa_string) {
        riscv_isa_string_ext(cpu, &isa_str, maxlen);
    }
    return isa_str;
}

static gint riscv_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    return strcmp(name_a, name_b);
}

static void riscv_cpu_list_entry(gpointer data, gpointer user_data)
{
    const char *typename = object_class_get_name(OBJECT_CLASS(data));
    int len = strlen(typename) - strlen(RISCV_CPU_TYPE_SUFFIX);

    qemu_printf("%.*s\n", len, typename);
}

void riscv_cpu_list(void)
{
    GSList *list;

    list = object_class_get_list(TYPE_RISCV_CPU, false);
    list = g_slist_sort(list, riscv_cpu_list_compare);
    g_slist_foreach(list, riscv_cpu_list_entry, NULL);
    g_slist_free(list);
}

#define DEFINE_CPU(type_name, initfn)      \
    {                                      \
        .name = type_name,                 \
        .parent = TYPE_RISCV_CPU,          \
        .instance_init = initfn            \
    }

#define DEFINE_DYNAMIC_CPU(type_name, initfn) \
    {                                         \
        .name = type_name,                    \
        .parent = TYPE_RISCV_DYNAMIC_CPU,     \
        .instance_init = initfn               \
    }

static const TypeInfo riscv_cpu_type_infos[] = {
    {
        .name = TYPE_RISCV_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(RISCVCPU),
        .instance_align = __alignof(RISCVCPU),
        .instance_init = riscv_cpu_init,
        .instance_post_init = riscv_cpu_post_init,
        .abstract = true,
        .class_size = sizeof(RISCVCPUClass),
        .class_init = riscv_cpu_class_init,
    },
    {
        .name = TYPE_RISCV_DYNAMIC_CPU,
        .parent = TYPE_RISCV_CPU,
        .abstract = true,
    },
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_ANY,      riscv_any_cpu_init),
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_MAX,      riscv_max_cpu_init),
#if defined(TARGET_RISCV32)
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_BASE32,   rv32_base_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_IBEX,             rv32_ibex_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_E31,       rv32_sifive_e_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_E34,       rv32_imafcu_nommu_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_U34,       rv32_sifive_u_cpu_init),
#elif defined(TARGET_RISCV64)
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_BASE64,   rv64_base_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_E51,       rv64_sifive_e_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_U54,       rv64_sifive_u_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SHAKTI_C,         rv64_sifive_u_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_THEAD_C906,       rv64_thead_c906_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_VEYRON_V1,        rv64_veyron_v1_cpu_init),
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_BASE128,  rv128_base_cpu_init),
#endif
};

DEFINE_TYPES(riscv_cpu_type_infos)
