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
#include "pmu.h"
#include "internals.h"
#include "time_helper.h"
#include "exec/exec-all.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "fpu/softfloat-helpers.h"
#include "sysemu/kvm.h"
#include "kvm_riscv.h"
#include "tcg/tcg.h"

/* RISC-V CPU definitions */

#define RISCV_CPU_MARCHID   ((QEMU_VERSION_MAJOR << 16) | \
                             (QEMU_VERSION_MINOR << 8)  | \
                             (QEMU_VERSION_MICRO))
#define RISCV_CPU_MIMPID    RISCV_CPU_MARCHID

static const char riscv_single_letter_exts[] = "IEMAFDQCPVH";

struct isa_ext_data {
    const char *name;
    int min_version;
    int ext_enable_offset;
};

#define ISA_EXT_DATA_ENTRY(_name, _min_ver, _prop) \
    {#_name, _min_ver, offsetof(struct RISCVCPUConfig, _prop)}

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
static const struct isa_ext_data isa_edata_arr[] = {
    ISA_EXT_DATA_ENTRY(zicbom, PRIV_VERSION_1_12_0, ext_icbom),
    ISA_EXT_DATA_ENTRY(zicboz, PRIV_VERSION_1_12_0, ext_icboz),
    ISA_EXT_DATA_ENTRY(zicond, PRIV_VERSION_1_12_0, ext_zicond),
    ISA_EXT_DATA_ENTRY(zicsr, PRIV_VERSION_1_10_0, ext_icsr),
    ISA_EXT_DATA_ENTRY(zifencei, PRIV_VERSION_1_10_0, ext_ifencei),
    ISA_EXT_DATA_ENTRY(zihintpause, PRIV_VERSION_1_10_0, ext_zihintpause),
    ISA_EXT_DATA_ENTRY(zawrs, PRIV_VERSION_1_12_0, ext_zawrs),
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
    ISA_EXT_DATA_ENTRY(zve32f, PRIV_VERSION_1_10_0, ext_zve32f),
    ISA_EXT_DATA_ENTRY(zve64f, PRIV_VERSION_1_10_0, ext_zve64f),
    ISA_EXT_DATA_ENTRY(zve64d, PRIV_VERSION_1_10_0, ext_zve64d),
    ISA_EXT_DATA_ENTRY(zvfh, PRIV_VERSION_1_12_0, ext_zvfh),
    ISA_EXT_DATA_ENTRY(zvfhmin, PRIV_VERSION_1_12_0, ext_zvfhmin),
    ISA_EXT_DATA_ENTRY(zhinx, PRIV_VERSION_1_12_0, ext_zhinx),
    ISA_EXT_DATA_ENTRY(zhinxmin, PRIV_VERSION_1_12_0, ext_zhinxmin),
    ISA_EXT_DATA_ENTRY(smaia, PRIV_VERSION_1_12_0, ext_smaia),
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
};

static bool isa_ext_is_enabled(RISCVCPU *cpu,
                               const struct isa_ext_data *edata)
{
    bool *ext_enabled = (void *)&cpu->cfg + edata->ext_enable_offset;

    return *ext_enabled;
}

static void isa_ext_update_enabled(RISCVCPU *cpu,
                                   const struct isa_ext_data *edata, bool en)
{
    bool *ext_enabled = (void *)&cpu->cfg + edata->ext_enable_offset;

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

static void riscv_cpu_add_user_properties(Object *obj);

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

static void set_misa(CPURISCVState *env, RISCVMXL mxl, uint32_t ext)
{
    env->misa_mxl_max = env->misa_mxl = mxl;
    env->misa_ext_mask = env->misa_ext = ext;
}

static void set_priv_version(CPURISCVState *env, int priv_ver)
{
    env->priv_ver = priv_ver;
}

static void set_vext_version(CPURISCVState *env, int vext_ver)
{
    env->vext_ver = vext_ver;
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
    CPURISCVState *env = &RISCV_CPU(obj)->env;
#if defined(TARGET_RISCV32)
    set_misa(env, MXL_RV32, RVI | RVM | RVA | RVF | RVD | RVC | RVU);
#elif defined(TARGET_RISCV64)
    set_misa(env, MXL_RV64, RVI | RVM | RVA | RVF | RVD | RVC | RVU);
#endif

#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj),
        riscv_cpu_mxl(&RISCV_CPU(obj)->env) == MXL_RV32 ?
        VM_1_10_SV32 : VM_1_10_SV57);
#endif

    set_priv_version(env, PRIV_VERSION_1_12_0);
}

#if defined(TARGET_RISCV64)
static void rv64_base_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    /* We set this in the realise function */
    set_misa(env, MXL_RV64, 0);
    riscv_cpu_add_user_properties(obj);
    /* Set latest version of privileged specification */
    set_priv_version(env, PRIV_VERSION_1_12_0);
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV57);
#endif
}

static void rv64_sifive_u_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, MXL_RV64, RVI | RVM | RVA | RVF | RVD | RVC | RVS | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV39);
#endif
}

static void rv64_sifive_e_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    set_misa(env, MXL_RV64, RVI | RVM | RVA | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    cpu->cfg.mmu = false;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_MBARE);
#endif
}

static void rv64_thead_c906_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    set_misa(env, MXL_RV64, RVG | RVC | RVS | RVU);
    set_priv_version(env, PRIV_VERSION_1_11_0);

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
}

static void rv64_veyron_v1_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    set_misa(env, MXL_RV64, RVG | RVC | RVS | RVU | RVH);
    env->priv_ver = PRIV_VERSION_1_12_0;

    /* Enable ISA extensions */
    cpu->cfg.mmu = true;
    cpu->cfg.ext_icbom = true;
    cpu->cfg.cbom_blocksize = 64;
    cpu->cfg.cboz_blocksize = 64;
    cpu->cfg.ext_icboz = true;
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
    set_misa(env, MXL_RV128, 0);
    riscv_cpu_add_user_properties(obj);
    /* Set latest version of privileged specification */
    set_priv_version(env, PRIV_VERSION_1_12_0);
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV57);
#endif
}
#else
static void rv32_base_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    /* We set this in the realise function */
    set_misa(env, MXL_RV32, 0);
    riscv_cpu_add_user_properties(obj);
    /* Set latest version of privileged specification */
    set_priv_version(env, PRIV_VERSION_1_12_0);
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV32);
#endif
}

static void rv32_sifive_u_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, MXL_RV32, RVI | RVM | RVA | RVF | RVD | RVC | RVS | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV32);
#endif
}

static void rv32_sifive_e_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    set_misa(env, MXL_RV32, RVI | RVM | RVA | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    cpu->cfg.mmu = false;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_MBARE);
#endif
}

static void rv32_ibex_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    set_misa(env, MXL_RV32, RVI | RVM | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_11_0);
    cpu->cfg.mmu = false;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_MBARE);
#endif
    cpu->cfg.epmp = true;
}

static void rv32_imafcu_nommu_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    set_misa(env, MXL_RV32, RVI | RVM | RVA | RVF | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    cpu->cfg.mmu = false;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_MBARE);
#endif
}
#endif

#if defined(CONFIG_KVM)
static void riscv_host_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
#if defined(TARGET_RISCV32)
    set_misa(env, MXL_RV32, 0);
#elif defined(TARGET_RISCV64)
    set_misa(env, MXL_RV64, 0);
#endif
    riscv_cpu_add_user_properties(obj);
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

static void riscv_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    int i;

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

        for (int i = 0; i < ARRAY_SIZE(dump_csrs); ++i) {
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

static void riscv_cpu_synchronize_from_tb(CPUState *cs,
                                          const TranslationBlock *tb)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    RISCVMXL xl = FIELD_EX32(tb->flags, TB_FLAGS, XL);

    tcg_debug_assert(!(cs->tcg_cflags & CF_PCREL));

    if (xl == MXL_RV32) {
        env->pc = (int32_t) tb->pc;
    } else {
        env->pc = tb->pc;
    }
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
    return riscv_cpu_all_pending(env) != 0;
#else
    return true;
#endif
}

static void riscv_restore_state_to_opc(CPUState *cs,
                                       const TranslationBlock *tb,
                                       const uint64_t *data)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    RISCVMXL xl = FIELD_EX32(tb->flags, TB_FLAGS, XL);

    if (xl == MXL_RV32) {
        env->pc = (int32_t)data[0];
    } else {
        env->pc = data[0];
    }
    env->bins = data[1];
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
                   (cpu->cfg.ext_svadu ? MENVCFG_HADE : 0);
    env->henvcfg = (cpu->cfg.ext_svpbmt ? HENVCFG_PBMTE : 0) |
                   (cpu->cfg.ext_svadu ? HENVCFG_HADE : 0);

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
#endif
    env->xl = riscv_cpu_mxl(env);
    riscv_cpu_update_mask(env);
    cs->exception_index = RISCV_EXCP_NONE;
    env->load_res = -1;
    set_default_nan_mode(1, &env->fp_status);

#ifndef CONFIG_USER_ONLY
    if (cpu->cfg.debug) {
        riscv_trigger_init(env);
    }

    if (kvm_enabled()) {
        kvm_riscv_reset_vcpu(cpu);
    }
#endif
}

static void riscv_cpu_disas_set_info(CPUState *s, disassemble_info *info)
{
    RISCVCPU *cpu = RISCV_CPU(s);

    switch (riscv_cpu_mxl(&cpu->env)) {
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

/*
 * Check consistency between chosen extensions while setting
 * cpu->cfg accordingly.
 */
static void riscv_cpu_validate_set_extensions(RISCVCPU *cpu, Error **errp)
{
    CPURISCVState *env = &cpu->env;

    /* Do some ISA extension error checking */
    if (riscv_has_ext(env, RVG) &&
        !(riscv_has_ext(env, RVI) && riscv_has_ext(env, RVM) &&
          riscv_has_ext(env, RVA) && riscv_has_ext(env, RVF) &&
          riscv_has_ext(env, RVD) &&
          cpu->cfg.ext_icsr && cpu->cfg.ext_ifencei)) {
        warn_report("Setting G will also set IMAFD_Zicsr_Zifencei");
        cpu->cfg.ext_icsr = true;
        cpu->cfg.ext_ifencei = true;

        env->misa_ext |= RVI | RVM | RVA | RVF | RVD;
        env->misa_ext_mask = env->misa_ext;
    }

    if (riscv_has_ext(env, RVI) && riscv_has_ext(env, RVE)) {
        error_setg(errp,
                   "I and E extensions are incompatible");
        return;
    }

    if (!riscv_has_ext(env, RVI) && !riscv_has_ext(env, RVE)) {
        error_setg(errp,
                   "Either I or E extension must be set");
        return;
    }

    if (riscv_has_ext(env, RVS) && !riscv_has_ext(env, RVU)) {
        error_setg(errp,
                   "Setting S extension without U extension is illegal");
        return;
    }

    if (riscv_has_ext(env, RVH) && !riscv_has_ext(env, RVI)) {
        error_setg(errp,
                   "H depends on an I base integer ISA with 32 x registers");
        return;
    }

    if (riscv_has_ext(env, RVH) && !riscv_has_ext(env, RVS)) {
        error_setg(errp, "H extension implicitly requires S-mode");
        return;
    }

    if (riscv_has_ext(env, RVF) && !cpu->cfg.ext_icsr) {
        error_setg(errp, "F extension requires Zicsr");
        return;
    }

    if ((cpu->cfg.ext_zawrs) && !riscv_has_ext(env, RVA)) {
        error_setg(errp, "Zawrs extension requires A extension");
        return;
    }

    if (cpu->cfg.ext_zfh) {
        cpu->cfg.ext_zfhmin = true;
    }

    if (cpu->cfg.ext_zfhmin && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "Zfh/Zfhmin extensions require F extension");
        return;
    }

    if (riscv_has_ext(env, RVD) && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "D extension requires F extension");
        return;
    }

    /* The V vector extension depends on the Zve64d extension */
    if (riscv_has_ext(env, RVV)) {
        cpu->cfg.ext_zve64d = true;
    }

    /* The Zve64d extension depends on the Zve64f extension */
    if (cpu->cfg.ext_zve64d) {
        cpu->cfg.ext_zve64f = true;
    }

    /* The Zve64f extension depends on the Zve32f extension */
    if (cpu->cfg.ext_zve64f) {
        cpu->cfg.ext_zve32f = true;
    }

    if (cpu->cfg.ext_zve64d && !riscv_has_ext(env, RVD)) {
        error_setg(errp, "Zve64d/V extensions require D extension");
        return;
    }

    if (cpu->cfg.ext_zve32f && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "Zve32f/Zve64f extensions require F extension");
        return;
    }

    if (cpu->cfg.ext_zvfh) {
        cpu->cfg.ext_zvfhmin = true;
    }

    if (cpu->cfg.ext_zvfhmin && !cpu->cfg.ext_zve32f) {
        error_setg(errp, "Zvfh/Zvfhmin extensions require Zve32f extension");
        return;
    }

    if (cpu->cfg.ext_zvfh && !cpu->cfg.ext_zfhmin) {
        error_setg(errp, "Zvfh extensions requires Zfhmin extension");
        return;
    }

    /* Set the ISA extensions, checks should have happened above */
    if (cpu->cfg.ext_zhinx) {
        cpu->cfg.ext_zhinxmin = true;
    }

    if ((cpu->cfg.ext_zdinx || cpu->cfg.ext_zhinxmin) && !cpu->cfg.ext_zfinx) {
        error_setg(errp, "Zdinx/Zhinx/Zhinxmin extensions require Zfinx");
        return;
    }

    if (cpu->cfg.ext_zfinx) {
        if (!cpu->cfg.ext_icsr) {
            error_setg(errp, "Zfinx extension requires Zicsr");
            return;
        }
        if (riscv_has_ext(env, RVF)) {
            error_setg(errp,
                       "Zfinx cannot be supported together with F extension");
            return;
        }
    }

    if (cpu->cfg.ext_zce) {
        cpu->cfg.ext_zca = true;
        cpu->cfg.ext_zcb = true;
        cpu->cfg.ext_zcmp = true;
        cpu->cfg.ext_zcmt = true;
        if (riscv_has_ext(env, RVF) && env->misa_mxl_max == MXL_RV32) {
            cpu->cfg.ext_zcf = true;
        }
    }

    if (riscv_has_ext(env, RVC)) {
        cpu->cfg.ext_zca = true;
        if (riscv_has_ext(env, RVF) && env->misa_mxl_max == MXL_RV32) {
            cpu->cfg.ext_zcf = true;
        }
        if (riscv_has_ext(env, RVD)) {
            cpu->cfg.ext_zcd = true;
        }
    }

    if (env->misa_mxl_max != MXL_RV32 && cpu->cfg.ext_zcf) {
        error_setg(errp, "Zcf extension is only relevant to RV32");
        return;
    }

    if (!riscv_has_ext(env, RVF) && cpu->cfg.ext_zcf) {
        error_setg(errp, "Zcf extension requires F extension");
        return;
    }

    if (!riscv_has_ext(env, RVD) && cpu->cfg.ext_zcd) {
        error_setg(errp, "Zcd extension requires D extension");
        return;
    }

    if ((cpu->cfg.ext_zcf || cpu->cfg.ext_zcd || cpu->cfg.ext_zcb ||
         cpu->cfg.ext_zcmp || cpu->cfg.ext_zcmt) && !cpu->cfg.ext_zca) {
        error_setg(errp, "Zcf/Zcd/Zcb/Zcmp/Zcmt extensions require Zca "
                         "extension");
        return;
    }

    if (cpu->cfg.ext_zcd && (cpu->cfg.ext_zcmp || cpu->cfg.ext_zcmt)) {
        error_setg(errp, "Zcmp/Zcmt extensions are incompatible with "
                         "Zcd extension");
        return;
    }

    if (cpu->cfg.ext_zcmt && !cpu->cfg.ext_icsr) {
        error_setg(errp, "Zcmt extension requires Zicsr extension");
        return;
    }

    if (cpu->cfg.ext_zk) {
        cpu->cfg.ext_zkn = true;
        cpu->cfg.ext_zkr = true;
        cpu->cfg.ext_zkt = true;
    }

    if (cpu->cfg.ext_zkn) {
        cpu->cfg.ext_zbkb = true;
        cpu->cfg.ext_zbkc = true;
        cpu->cfg.ext_zbkx = true;
        cpu->cfg.ext_zkne = true;
        cpu->cfg.ext_zknd = true;
        cpu->cfg.ext_zknh = true;
    }

    if (cpu->cfg.ext_zks) {
        cpu->cfg.ext_zbkb = true;
        cpu->cfg.ext_zbkc = true;
        cpu->cfg.ext_zbkx = true;
        cpu->cfg.ext_zksed = true;
        cpu->cfg.ext_zksh = true;
    }

    if (riscv_has_ext(env, RVV)) {
        int vext_version = VEXT_VERSION_1_00_0;
        if (!is_power_of_2(cpu->cfg.vlen)) {
            error_setg(errp,
                       "Vector extension VLEN must be power of 2");
            return;
        }
        if (cpu->cfg.vlen > RV_VLEN_MAX || cpu->cfg.vlen < 128) {
            error_setg(errp,
                       "Vector extension implementation only supports VLEN "
                       "in the range [128, %d]", RV_VLEN_MAX);
            return;
        }
        if (!is_power_of_2(cpu->cfg.elen)) {
            error_setg(errp,
                       "Vector extension ELEN must be power of 2");
            return;
        }
        if (cpu->cfg.elen > 64 || cpu->cfg.elen < 8) {
            error_setg(errp,
                       "Vector extension implementation only supports ELEN "
                       "in the range [8, 64]");
            return;
        }
        if (cpu->cfg.vext_spec) {
            if (!g_strcmp0(cpu->cfg.vext_spec, "v1.0")) {
                vext_version = VEXT_VERSION_1_00_0;
            } else {
                error_setg(errp,
                           "Unsupported vector spec version '%s'",
                           cpu->cfg.vext_spec);
                return;
            }
        } else {
            qemu_log("vector version is not specified, "
                     "use the default value v1.0\n");
        }
        set_vext_version(env, vext_version);
    }
}

#ifndef CONFIG_USER_ONLY
static void riscv_cpu_satp_mode_finalize(RISCVCPU *cpu, Error **errp)
{
    bool rv32 = riscv_cpu_mxl(&cpu->env) == MXL_RV32;
    uint8_t satp_mode_map_max;
    uint8_t satp_mode_supported_max =
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

static void riscv_cpu_finalize_features(RISCVCPU *cpu, Error **errp)
{
#ifndef CONFIG_USER_ONLY
    Error *local_err = NULL;

    riscv_cpu_satp_mode_finalize(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
#endif
}

static void riscv_cpu_validate_misa_priv(CPURISCVState *env, Error **errp)
{
    if (riscv_has_ext(env, RVH) && env->priv_ver < PRIV_VERSION_1_12_0) {
        error_setg(errp, "H extension requires priv spec 1.12.0");
        return;
    }
}

static void riscv_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    RISCVCPU *cpu = RISCV_CPU(dev);
    CPURISCVState *env = &cpu->env;
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(dev);
    CPUClass *cc = CPU_CLASS(mcc);
    int i, priv_version = -1;
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    if (cpu->cfg.priv_spec) {
        if (!g_strcmp0(cpu->cfg.priv_spec, "v1.12.0")) {
            priv_version = PRIV_VERSION_1_12_0;
        } else if (!g_strcmp0(cpu->cfg.priv_spec, "v1.11.0")) {
            priv_version = PRIV_VERSION_1_11_0;
        } else if (!g_strcmp0(cpu->cfg.priv_spec, "v1.10.0")) {
            priv_version = PRIV_VERSION_1_10_0;
        } else {
            error_setg(errp,
                       "Unsupported privilege spec version '%s'",
                       cpu->cfg.priv_spec);
            return;
        }
    }

    if (priv_version >= PRIV_VERSION_1_10_0) {
        set_priv_version(env, priv_version);
    }

    riscv_cpu_validate_misa_priv(env, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    /* Force disable extensions if priv spec version does not match */
    for (i = 0; i < ARRAY_SIZE(isa_edata_arr); i++) {
        if (isa_ext_is_enabled(cpu, &isa_edata_arr[i]) &&
            (env->priv_ver < isa_edata_arr[i].min_version)) {
            isa_ext_update_enabled(cpu, &isa_edata_arr[i], false);
#ifndef CONFIG_USER_ONLY
            warn_report("disabling %s extension for hart 0x" TARGET_FMT_lx
                        " because privilege spec version does not match",
                        isa_edata_arr[i].name, env->mhartid);
#else
            warn_report("disabling %s extension because "
                        "privilege spec version does not match",
                        isa_edata_arr[i].name);
#endif
        }
    }

    if (cpu->cfg.epmp && !cpu->cfg.pmp) {
        /*
         * Enhanced PMP should only be available
         * on harts with PMP support
         */
        error_setg(errp, "Invalid configuration: EPMP requires PMP support");
        return;
    }


#ifndef CONFIG_USER_ONLY
    if (cpu->cfg.ext_sstc) {
        riscv_timer_init(cpu);
    }
#endif /* CONFIG_USER_ONLY */

    /* Validate that MISA_MXL is set properly. */
    switch (env->misa_mxl_max) {
#ifdef TARGET_RISCV64
    case MXL_RV64:
    case MXL_RV128:
        cc->gdb_core_xml_file = "riscv-64bit-cpu.xml";
        break;
#endif
    case MXL_RV32:
        cc->gdb_core_xml_file = "riscv-32bit-cpu.xml";
        break;
    default:
        g_assert_not_reached();
    }
    assert(env->misa_mxl_max == env->misa_mxl);

    riscv_cpu_validate_set_extensions(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

#ifndef CONFIG_USER_ONLY
    if (cpu->cfg.pmu_num) {
        if (!riscv_pmu_init(cpu, cpu->cfg.pmu_num) && cpu->cfg.ext_sscofpmf) {
            cpu->pmu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          riscv_pmu_timer_cb, cpu);
        }
     }
#endif

    riscv_cpu_finalize_features(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    riscv_cpu_register_gdb_regs_for_features(cs);

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    mcc->parent_realize(dev, errp);
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

static void riscv_add_satp_mode_properties(Object *obj)
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

static void riscv_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);

    cpu->cfg.ext_ifencei = true;
    cpu->cfg.ext_icsr = true;
    cpu->cfg.mmu = true;
    cpu->cfg.pmp = true;

    cpu_set_cpustate_pointers(cpu);

#ifndef CONFIG_USER_ONLY
    qdev_init_gpio_in(DEVICE(cpu), riscv_cpu_set_irq,
                      IRQ_LOCAL_MAX + IRQ_LOCAL_GUEST_MAX);
#endif /* CONFIG_USER_ONLY */
}

typedef struct RISCVCPUMisaExtConfig {
    const char *name;
    const char *description;
    target_ulong misa_bit;
    bool enabled;
} RISCVCPUMisaExtConfig;

static void cpu_set_misa_ext_cfg(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    const RISCVCPUMisaExtConfig *misa_ext_cfg = opaque;
    target_ulong misa_bit = misa_ext_cfg->misa_bit;
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    if (value) {
        env->misa_ext |= misa_bit;
        env->misa_ext_mask |= misa_bit;
    } else {
        env->misa_ext &= ~misa_bit;
        env->misa_ext_mask &= ~misa_bit;
    }
}

static void cpu_get_misa_ext_cfg(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    const RISCVCPUMisaExtConfig *misa_ext_cfg = opaque;
    target_ulong misa_bit = misa_ext_cfg->misa_bit;
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    bool value;

    value = env->misa_ext & misa_bit;

    visit_type_bool(v, name, &value, errp);
}

static const RISCVCPUMisaExtConfig misa_ext_cfgs[] = {
    {.name = "a", .description = "Atomic instructions",
     .misa_bit = RVA, .enabled = true},
    {.name = "c", .description = "Compressed instructions",
     .misa_bit = RVC, .enabled = true},
    {.name = "d", .description = "Double-precision float point",
     .misa_bit = RVD, .enabled = true},
    {.name = "f", .description = "Single-precision float point",
     .misa_bit = RVF, .enabled = true},
    {.name = "i", .description = "Base integer instruction set",
     .misa_bit = RVI, .enabled = true},
    {.name = "e", .description = "Base integer instruction set (embedded)",
     .misa_bit = RVE, .enabled = false},
    {.name = "m", .description = "Integer multiplication and division",
     .misa_bit = RVM, .enabled = true},
    {.name = "s", .description = "Supervisor-level instructions",
     .misa_bit = RVS, .enabled = true},
    {.name = "u", .description = "User-level instructions",
     .misa_bit = RVU, .enabled = true},
    {.name = "h", .description = "Hypervisor",
     .misa_bit = RVH, .enabled = true},
    {.name = "x-j", .description = "Dynamic translated languages",
     .misa_bit = RVJ, .enabled = false},
    {.name = "v", .description = "Vector operations",
     .misa_bit = RVV, .enabled = false},
    {.name = "g", .description = "General purpose (IMAFD_Zicsr_Zifencei)",
     .misa_bit = RVG, .enabled = false},
};

static void riscv_cpu_add_misa_properties(Object *cpu_obj)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(misa_ext_cfgs); i++) {
        const RISCVCPUMisaExtConfig *misa_cfg = &misa_ext_cfgs[i];

        object_property_add(cpu_obj, misa_cfg->name, "bool",
                            cpu_get_misa_ext_cfg,
                            cpu_set_misa_ext_cfg,
                            NULL, (void *)misa_cfg);
        object_property_set_description(cpu_obj, misa_cfg->name,
                                        misa_cfg->description);
        object_property_set_bool(cpu_obj, misa_cfg->name,
                                 misa_cfg->enabled, NULL);
    }
}

static Property riscv_cpu_extensions[] = {
    /* Defaults for standard extensions */
    DEFINE_PROP_UINT8("pmu-num", RISCVCPU, cfg.pmu_num, 16),
    DEFINE_PROP_BOOL("sscofpmf", RISCVCPU, cfg.ext_sscofpmf, false),
    DEFINE_PROP_BOOL("Zifencei", RISCVCPU, cfg.ext_ifencei, true),
    DEFINE_PROP_BOOL("Zicsr", RISCVCPU, cfg.ext_icsr, true),
    DEFINE_PROP_BOOL("Zihintpause", RISCVCPU, cfg.ext_zihintpause, true),
    DEFINE_PROP_BOOL("Zawrs", RISCVCPU, cfg.ext_zawrs, true),
    DEFINE_PROP_BOOL("Zfh", RISCVCPU, cfg.ext_zfh, false),
    DEFINE_PROP_BOOL("Zfhmin", RISCVCPU, cfg.ext_zfhmin, false),
    DEFINE_PROP_BOOL("Zve32f", RISCVCPU, cfg.ext_zve32f, false),
    DEFINE_PROP_BOOL("Zve64f", RISCVCPU, cfg.ext_zve64f, false),
    DEFINE_PROP_BOOL("Zve64d", RISCVCPU, cfg.ext_zve64d, false),
    DEFINE_PROP_BOOL("mmu", RISCVCPU, cfg.mmu, true),
    DEFINE_PROP_BOOL("pmp", RISCVCPU, cfg.pmp, true),
    DEFINE_PROP_BOOL("sstc", RISCVCPU, cfg.ext_sstc, true),

    DEFINE_PROP_STRING("priv_spec", RISCVCPU, cfg.priv_spec),
    DEFINE_PROP_STRING("vext_spec", RISCVCPU, cfg.vext_spec),
    DEFINE_PROP_UINT16("vlen", RISCVCPU, cfg.vlen, 128),
    DEFINE_PROP_UINT16("elen", RISCVCPU, cfg.elen, 64),

    DEFINE_PROP_BOOL("svadu", RISCVCPU, cfg.ext_svadu, true),

    DEFINE_PROP_BOOL("svinval", RISCVCPU, cfg.ext_svinval, false),
    DEFINE_PROP_BOOL("svnapot", RISCVCPU, cfg.ext_svnapot, false),
    DEFINE_PROP_BOOL("svpbmt", RISCVCPU, cfg.ext_svpbmt, false),

    DEFINE_PROP_BOOL("zba", RISCVCPU, cfg.ext_zba, true),
    DEFINE_PROP_BOOL("zbb", RISCVCPU, cfg.ext_zbb, true),
    DEFINE_PROP_BOOL("zbc", RISCVCPU, cfg.ext_zbc, true),
    DEFINE_PROP_BOOL("zbkb", RISCVCPU, cfg.ext_zbkb, false),
    DEFINE_PROP_BOOL("zbkc", RISCVCPU, cfg.ext_zbkc, false),
    DEFINE_PROP_BOOL("zbkx", RISCVCPU, cfg.ext_zbkx, false),
    DEFINE_PROP_BOOL("zbs", RISCVCPU, cfg.ext_zbs, true),
    DEFINE_PROP_BOOL("zk", RISCVCPU, cfg.ext_zk, false),
    DEFINE_PROP_BOOL("zkn", RISCVCPU, cfg.ext_zkn, false),
    DEFINE_PROP_BOOL("zknd", RISCVCPU, cfg.ext_zknd, false),
    DEFINE_PROP_BOOL("zkne", RISCVCPU, cfg.ext_zkne, false),
    DEFINE_PROP_BOOL("zknh", RISCVCPU, cfg.ext_zknh, false),
    DEFINE_PROP_BOOL("zkr", RISCVCPU, cfg.ext_zkr, false),
    DEFINE_PROP_BOOL("zks", RISCVCPU, cfg.ext_zks, false),
    DEFINE_PROP_BOOL("zksed", RISCVCPU, cfg.ext_zksed, false),
    DEFINE_PROP_BOOL("zksh", RISCVCPU, cfg.ext_zksh, false),
    DEFINE_PROP_BOOL("zkt", RISCVCPU, cfg.ext_zkt, false),

    DEFINE_PROP_BOOL("zdinx", RISCVCPU, cfg.ext_zdinx, false),
    DEFINE_PROP_BOOL("zfinx", RISCVCPU, cfg.ext_zfinx, false),
    DEFINE_PROP_BOOL("zhinx", RISCVCPU, cfg.ext_zhinx, false),
    DEFINE_PROP_BOOL("zhinxmin", RISCVCPU, cfg.ext_zhinxmin, false),

    DEFINE_PROP_BOOL("zicbom", RISCVCPU, cfg.ext_icbom, true),
    DEFINE_PROP_UINT16("cbom_blocksize", RISCVCPU, cfg.cbom_blocksize, 64),
    DEFINE_PROP_BOOL("zicboz", RISCVCPU, cfg.ext_icboz, true),
    DEFINE_PROP_UINT16("cboz_blocksize", RISCVCPU, cfg.cboz_blocksize, 64),

    DEFINE_PROP_BOOL("zmmul", RISCVCPU, cfg.ext_zmmul, false),

    DEFINE_PROP_BOOL("zca", RISCVCPU, cfg.ext_zca, false),
    DEFINE_PROP_BOOL("zcb", RISCVCPU, cfg.ext_zcb, false),
    DEFINE_PROP_BOOL("zcd", RISCVCPU, cfg.ext_zcd, false),
    DEFINE_PROP_BOOL("zce", RISCVCPU, cfg.ext_zce, false),
    DEFINE_PROP_BOOL("zcf", RISCVCPU, cfg.ext_zcf, false),
    DEFINE_PROP_BOOL("zcmp", RISCVCPU, cfg.ext_zcmp, false),
    DEFINE_PROP_BOOL("zcmt", RISCVCPU, cfg.ext_zcmt, false),

    /* Vendor-specific custom extensions */
    DEFINE_PROP_BOOL("xtheadba", RISCVCPU, cfg.ext_xtheadba, false),
    DEFINE_PROP_BOOL("xtheadbb", RISCVCPU, cfg.ext_xtheadbb, false),
    DEFINE_PROP_BOOL("xtheadbs", RISCVCPU, cfg.ext_xtheadbs, false),
    DEFINE_PROP_BOOL("xtheadcmo", RISCVCPU, cfg.ext_xtheadcmo, false),
    DEFINE_PROP_BOOL("xtheadcondmov", RISCVCPU, cfg.ext_xtheadcondmov, false),
    DEFINE_PROP_BOOL("xtheadfmemidx", RISCVCPU, cfg.ext_xtheadfmemidx, false),
    DEFINE_PROP_BOOL("xtheadfmv", RISCVCPU, cfg.ext_xtheadfmv, false),
    DEFINE_PROP_BOOL("xtheadmac", RISCVCPU, cfg.ext_xtheadmac, false),
    DEFINE_PROP_BOOL("xtheadmemidx", RISCVCPU, cfg.ext_xtheadmemidx, false),
    DEFINE_PROP_BOOL("xtheadmempair", RISCVCPU, cfg.ext_xtheadmempair, false),
    DEFINE_PROP_BOOL("xtheadsync", RISCVCPU, cfg.ext_xtheadsync, false),
    DEFINE_PROP_BOOL("xventanacondops", RISCVCPU, cfg.ext_XVentanaCondOps, false),

    /* These are experimental so mark with 'x-' */
    DEFINE_PROP_BOOL("x-zicond", RISCVCPU, cfg.ext_zicond, false),

    /* ePMP 0.9.3 */
    DEFINE_PROP_BOOL("x-epmp", RISCVCPU, cfg.epmp, false),
    DEFINE_PROP_BOOL("x-smaia", RISCVCPU, cfg.ext_smaia, false),
    DEFINE_PROP_BOOL("x-ssaia", RISCVCPU, cfg.ext_ssaia, false),

    DEFINE_PROP_BOOL("x-zvfh", RISCVCPU, cfg.ext_zvfh, false),
    DEFINE_PROP_BOOL("x-zvfhmin", RISCVCPU, cfg.ext_zvfhmin, false),

    DEFINE_PROP_END_OF_LIST(),
};

/*
 * Add CPU properties with user-facing flags.
 *
 * This will overwrite existing env->misa_ext values with the
 * defaults set via riscv_cpu_add_misa_properties().
 */
static void riscv_cpu_add_user_properties(Object *obj)
{
    Property *prop;
    DeviceState *dev = DEVICE(obj);

    riscv_cpu_add_misa_properties(obj);

    for (prop = riscv_cpu_extensions; prop && prop->name; prop++) {
        qdev_property_add_static(dev, prop);
    }

#ifndef CONFIG_USER_ONLY
    riscv_add_satp_mode_properties(obj);
#endif
}

static Property riscv_cpu_properties[] = {
    DEFINE_PROP_BOOL("debug", RISCVCPU, cfg.debug, true),

    DEFINE_PROP_UINT32("mvendorid", RISCVCPU, cfg.mvendorid, 0),
    DEFINE_PROP_UINT64("marchid", RISCVCPU, cfg.marchid, RISCV_CPU_MARCHID),
    DEFINE_PROP_UINT64("mimpid", RISCVCPU, cfg.mimpid, RISCV_CPU_MIMPID),

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

static gchar *riscv_gdb_arch_name(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        return g_strdup("riscv:rv32");
    case MXL_RV64:
    case MXL_RV128:
        return g_strdup("riscv:rv64");
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

#include "hw/core/tcg-cpu-ops.h"

static const struct TCGCPUOps riscv_tcg_ops = {
    .initialize = riscv_translate_init,
    .synchronize_from_tb = riscv_cpu_synchronize_from_tb,
    .restore_state_to_opc = riscv_restore_state_to_opc,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = riscv_cpu_tlb_fill,
    .cpu_exec_interrupt = riscv_cpu_exec_interrupt,
    .do_interrupt = riscv_cpu_do_interrupt,
    .do_transaction_failed = riscv_cpu_do_transaction_failed,
    .do_unaligned_access = riscv_cpu_do_unaligned_access,
    .debug_excp_handler = riscv_cpu_debug_excp_handler,
    .debug_check_breakpoint = riscv_cpu_debug_check_breakpoint,
    .debug_check_watchpoint = riscv_cpu_debug_check_watchpoint,
#endif /* !CONFIG_USER_ONLY */
};

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
    cc->tcg_ops = &riscv_tcg_ops;

    device_class_set_props(dc, riscv_cpu_properties);
}

static void riscv_isa_string_ext(RISCVCPU *cpu, char **isa_str,
                                 int max_str_len)
{
    char *old = *isa_str;
    char *new = *isa_str;
    int i;

    for (i = 0; i < ARRAY_SIZE(isa_edata_arr); i++) {
        if (isa_ext_is_enabled(cpu, &isa_edata_arr[i])) {
            new = g_strconcat(old, "_", isa_edata_arr[i].name, NULL);
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
        .instance_align = __alignof__(RISCVCPU),
        .instance_init = riscv_cpu_init,
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
#if defined(CONFIG_KVM)
    DEFINE_CPU(TYPE_RISCV_CPU_HOST,             riscv_host_cpu_init),
#endif
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
