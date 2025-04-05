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
#include "hw/core/qdev-prop-internal.h"
#include "migration/vmstate.h"
#include "fpu/softfloat-helpers.h"
#include "system/device_tree.h"
#include "system/kvm.h"
#include "system/tcg.h"
#include "kvm/kvm_riscv.h"
#include "tcg/tcg-cpu.h"
#include "tcg/tcg.h"

/* RISC-V CPU definitions */
static const char riscv_single_letter_exts[] = "IEMAFDQCBPVH";
const uint32_t misa_bits[] = {RVI, RVE, RVM, RVA, RVF, RVD, RVV,
                              RVC, RVS, RVU, RVH, RVG, RVB, 0};

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

bool riscv_cpu_is_32bit(RISCVCPU *cpu)
{
    return riscv_cpu_mxl(&cpu->env) == MXL_RV32;
}

/* Hash that stores general user set numeric options */
static GHashTable *general_user_opts;

static void cpu_option_add_user_setting(const char *optname, uint32_t value)
{
    g_hash_table_insert(general_user_opts, (gpointer)optname,
                        GUINT_TO_POINTER(value));
}

bool riscv_cpu_option_set(const char *optname)
{
    return g_hash_table_contains(general_user_opts, optname);
}

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
    ISA_EXT_DATA_ENTRY(zic64b, PRIV_VERSION_1_12_0, ext_zic64b),
    ISA_EXT_DATA_ENTRY(zicbom, PRIV_VERSION_1_12_0, ext_zicbom),
    ISA_EXT_DATA_ENTRY(zicbop, PRIV_VERSION_1_12_0, ext_zicbop),
    ISA_EXT_DATA_ENTRY(zicboz, PRIV_VERSION_1_12_0, ext_zicboz),
    ISA_EXT_DATA_ENTRY(ziccamoa, PRIV_VERSION_1_11_0, has_priv_1_11),
    ISA_EXT_DATA_ENTRY(ziccif, PRIV_VERSION_1_11_0, has_priv_1_11),
    ISA_EXT_DATA_ENTRY(zicclsm, PRIV_VERSION_1_11_0, has_priv_1_11),
    ISA_EXT_DATA_ENTRY(ziccrse, PRIV_VERSION_1_11_0, ext_ziccrse),
    ISA_EXT_DATA_ENTRY(zicfilp, PRIV_VERSION_1_12_0, ext_zicfilp),
    ISA_EXT_DATA_ENTRY(zicfiss, PRIV_VERSION_1_13_0, ext_zicfiss),
    ISA_EXT_DATA_ENTRY(zicond, PRIV_VERSION_1_12_0, ext_zicond),
    ISA_EXT_DATA_ENTRY(zicntr, PRIV_VERSION_1_12_0, ext_zicntr),
    ISA_EXT_DATA_ENTRY(zicsr, PRIV_VERSION_1_10_0, ext_zicsr),
    ISA_EXT_DATA_ENTRY(zifencei, PRIV_VERSION_1_10_0, ext_zifencei),
    ISA_EXT_DATA_ENTRY(zihintntl, PRIV_VERSION_1_10_0, ext_zihintntl),
    ISA_EXT_DATA_ENTRY(zihintpause, PRIV_VERSION_1_10_0, ext_zihintpause),
    ISA_EXT_DATA_ENTRY(zihpm, PRIV_VERSION_1_12_0, ext_zihpm),
    ISA_EXT_DATA_ENTRY(zimop, PRIV_VERSION_1_13_0, ext_zimop),
    ISA_EXT_DATA_ENTRY(zmmul, PRIV_VERSION_1_12_0, ext_zmmul),
    ISA_EXT_DATA_ENTRY(za64rs, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(zaamo, PRIV_VERSION_1_12_0, ext_zaamo),
    ISA_EXT_DATA_ENTRY(zabha, PRIV_VERSION_1_13_0, ext_zabha),
    ISA_EXT_DATA_ENTRY(zacas, PRIV_VERSION_1_12_0, ext_zacas),
    ISA_EXT_DATA_ENTRY(zama16b, PRIV_VERSION_1_13_0, ext_zama16b),
    ISA_EXT_DATA_ENTRY(zalrsc, PRIV_VERSION_1_12_0, ext_zalrsc),
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
    ISA_EXT_DATA_ENTRY(zcmop, PRIV_VERSION_1_13_0, ext_zcmop),
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
    ISA_EXT_DATA_ENTRY(ztso, PRIV_VERSION_1_12_0, ext_ztso),
    ISA_EXT_DATA_ENTRY(zvbb, PRIV_VERSION_1_12_0, ext_zvbb),
    ISA_EXT_DATA_ENTRY(zvbc, PRIV_VERSION_1_12_0, ext_zvbc),
    ISA_EXT_DATA_ENTRY(zve32f, PRIV_VERSION_1_10_0, ext_zve32f),
    ISA_EXT_DATA_ENTRY(zve32x, PRIV_VERSION_1_10_0, ext_zve32x),
    ISA_EXT_DATA_ENTRY(zve64f, PRIV_VERSION_1_10_0, ext_zve64f),
    ISA_EXT_DATA_ENTRY(zve64d, PRIV_VERSION_1_10_0, ext_zve64d),
    ISA_EXT_DATA_ENTRY(zve64x, PRIV_VERSION_1_10_0, ext_zve64x),
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
    ISA_EXT_DATA_ENTRY(shcounterenw, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(sha, PRIV_VERSION_1_12_0, ext_sha),
    ISA_EXT_DATA_ENTRY(shgatpa, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(shtvala, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(shvsatpa, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(shvstvala, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(shvstvecd, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(smaia, PRIV_VERSION_1_12_0, ext_smaia),
    ISA_EXT_DATA_ENTRY(smcdeleg, PRIV_VERSION_1_13_0, ext_smcdeleg),
    ISA_EXT_DATA_ENTRY(smcntrpmf, PRIV_VERSION_1_12_0, ext_smcntrpmf),
    ISA_EXT_DATA_ENTRY(smcsrind, PRIV_VERSION_1_13_0, ext_smcsrind),
    ISA_EXT_DATA_ENTRY(smdbltrp, PRIV_VERSION_1_13_0, ext_smdbltrp),
    ISA_EXT_DATA_ENTRY(smepmp, PRIV_VERSION_1_12_0, ext_smepmp),
    ISA_EXT_DATA_ENTRY(smrnmi, PRIV_VERSION_1_12_0, ext_smrnmi),
    ISA_EXT_DATA_ENTRY(smmpm, PRIV_VERSION_1_13_0, ext_smmpm),
    ISA_EXT_DATA_ENTRY(smnpm, PRIV_VERSION_1_13_0, ext_smnpm),
    ISA_EXT_DATA_ENTRY(smstateen, PRIV_VERSION_1_12_0, ext_smstateen),
    ISA_EXT_DATA_ENTRY(ssaia, PRIV_VERSION_1_12_0, ext_ssaia),
    ISA_EXT_DATA_ENTRY(ssccfg, PRIV_VERSION_1_13_0, ext_ssccfg),
    ISA_EXT_DATA_ENTRY(ssccptr, PRIV_VERSION_1_11_0, has_priv_1_11),
    ISA_EXT_DATA_ENTRY(sscofpmf, PRIV_VERSION_1_12_0, ext_sscofpmf),
    ISA_EXT_DATA_ENTRY(sscounterenw, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(sscsrind, PRIV_VERSION_1_12_0, ext_sscsrind),
    ISA_EXT_DATA_ENTRY(ssdbltrp, PRIV_VERSION_1_13_0, ext_ssdbltrp),
    ISA_EXT_DATA_ENTRY(ssnpm, PRIV_VERSION_1_13_0, ext_ssnpm),
    ISA_EXT_DATA_ENTRY(sspm, PRIV_VERSION_1_13_0, ext_sspm),
    ISA_EXT_DATA_ENTRY(ssstateen, PRIV_VERSION_1_12_0, ext_ssstateen),
    ISA_EXT_DATA_ENTRY(sstc, PRIV_VERSION_1_12_0, ext_sstc),
    ISA_EXT_DATA_ENTRY(sstvala, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(sstvecd, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(ssu64xl, PRIV_VERSION_1_12_0, has_priv_1_12),
    ISA_EXT_DATA_ENTRY(supm, PRIV_VERSION_1_13_0, ext_supm),
    ISA_EXT_DATA_ENTRY(svade, PRIV_VERSION_1_11_0, ext_svade),
    ISA_EXT_DATA_ENTRY(smctr, PRIV_VERSION_1_12_0, ext_smctr),
    ISA_EXT_DATA_ENTRY(ssctr, PRIV_VERSION_1_12_0, ext_ssctr),
    ISA_EXT_DATA_ENTRY(svadu, PRIV_VERSION_1_12_0, ext_svadu),
    ISA_EXT_DATA_ENTRY(svinval, PRIV_VERSION_1_12_0, ext_svinval),
    ISA_EXT_DATA_ENTRY(svnapot, PRIV_VERSION_1_12_0, ext_svnapot),
    ISA_EXT_DATA_ENTRY(svpbmt, PRIV_VERSION_1_12_0, ext_svpbmt),
    ISA_EXT_DATA_ENTRY(svukte, PRIV_VERSION_1_13_0, ext_svukte),
    ISA_EXT_DATA_ENTRY(svvptc, PRIV_VERSION_1_13_0, ext_svvptc),
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

    { },
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

bool riscv_cpu_is_vendor(Object *cpu_obj)
{
    return object_dynamic_cast(cpu_obj, TYPE_RISCV_VENDOR_CPU) != NULL;
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
    "double_trap",
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

void riscv_cpu_set_misa_ext(CPURISCVState *env, uint32_t ext)
{
    env->misa_ext_mask = env->misa_ext = ext;
}

int riscv_cpu_max_xlen(RISCVCPUClass *mcc)
{
    return 16 << mcc->misa_mxl_max;
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
    /*
     * Bare CPUs do not default to the max available.
     * Users must set a valid satp_mode in the command
     * line.
     */
    if (object_dynamic_cast(OBJECT(cpu), TYPE_RISCV_BARE_CPU) != NULL) {
        warn_report("No satp mode set. Defaulting to 'bare'");
        cpu->cfg.satp_mode.map = (1 << VM_1_10_MBARE);
        return;
    }

    cpu->cfg.satp_mode.map = cpu->cfg.satp_mode.supported;
}
#endif

static void riscv_max_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;

    cpu->cfg.mmu = true;
    cpu->cfg.pmp = true;

    env->priv_ver = PRIV_VERSION_LATEST;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(RISCV_CPU(obj),
        riscv_cpu_mxl(&RISCV_CPU(obj)->env) == MXL_RV32 ?
        VM_1_10_SV32 : VM_1_10_SV57);
#endif
}

#if defined(TARGET_RISCV64)
static void rv64_base_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;

    cpu->cfg.mmu = true;
    cpu->cfg.pmp = true;

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
    riscv_cpu_set_misa_ext(env, RVI | RVM | RVA | RVF | RVD | RVC | RVS | RVU);
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

    riscv_cpu_set_misa_ext(env, RVI | RVM | RVA | RVC | RVU);
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

    riscv_cpu_set_misa_ext(env, RVG | RVC | RVS | RVU);
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
    th_register_custom_csrs(cpu);
#endif

    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.pmp = true;
}

static void rv64_veyron_v1_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    riscv_cpu_set_misa_ext(env, RVG | RVC | RVS | RVU | RVH);
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

/* Tenstorrent Ascalon */
static void rv64_tt_ascalon_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    riscv_cpu_set_misa_ext(env, RVG | RVC | RVS | RVU | RVH | RVV);
    env->priv_ver = PRIV_VERSION_1_13_0;

    /* Enable ISA extensions */
    cpu->cfg.mmu = true;
    cpu->cfg.vlenb = 256 >> 3;
    cpu->cfg.elen = 64;
    cpu->env.vext_ver = VEXT_VERSION_1_00_0;
    cpu->cfg.rvv_ma_all_1s = true;
    cpu->cfg.rvv_ta_all_1s = true;
    cpu->cfg.misa_w = true;
    cpu->cfg.pmp = true;
    cpu->cfg.cbom_blocksize = 64;
    cpu->cfg.cbop_blocksize = 64;
    cpu->cfg.cboz_blocksize = 64;
    cpu->cfg.ext_zic64b = true;
    cpu->cfg.ext_zicbom = true;
    cpu->cfg.ext_zicbop = true;
    cpu->cfg.ext_zicboz = true;
    cpu->cfg.ext_zicntr = true;
    cpu->cfg.ext_zicond = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zihintntl = true;
    cpu->cfg.ext_zihintpause = true;
    cpu->cfg.ext_zihpm = true;
    cpu->cfg.ext_zimop = true;
    cpu->cfg.ext_zawrs = true;
    cpu->cfg.ext_zfa = true;
    cpu->cfg.ext_zfbfmin = true;
    cpu->cfg.ext_zfh = true;
    cpu->cfg.ext_zfhmin = true;
    cpu->cfg.ext_zcb = true;
    cpu->cfg.ext_zcmop = true;
    cpu->cfg.ext_zba = true;
    cpu->cfg.ext_zbb = true;
    cpu->cfg.ext_zbs = true;
    cpu->cfg.ext_zkt = true;
    cpu->cfg.ext_zvbb = true;
    cpu->cfg.ext_zvbc = true;
    cpu->cfg.ext_zvfbfmin = true;
    cpu->cfg.ext_zvfbfwma = true;
    cpu->cfg.ext_zvfh = true;
    cpu->cfg.ext_zvfhmin = true;
    cpu->cfg.ext_zvkng = true;
    cpu->cfg.ext_smaia = true;
    cpu->cfg.ext_smstateen = true;
    cpu->cfg.ext_ssaia = true;
    cpu->cfg.ext_sscofpmf = true;
    cpu->cfg.ext_sstc = true;
    cpu->cfg.ext_svade = true;
    cpu->cfg.ext_svinval = true;
    cpu->cfg.ext_svnapot = true;
    cpu->cfg.ext_svpbmt = true;

#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_SV57);
#endif
}

static void rv64_xiangshan_nanhu_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    riscv_cpu_set_misa_ext(env, RVG | RVC | RVB | RVS | RVU);
    env->priv_ver = PRIV_VERSION_1_12_0;

    /* Enable ISA extensions */
    cpu->cfg.ext_zbc = true;
    cpu->cfg.ext_zbkb = true;
    cpu->cfg.ext_zbkc = true;
    cpu->cfg.ext_zbkx = true;
    cpu->cfg.ext_zknd = true;
    cpu->cfg.ext_zkne = true;
    cpu->cfg.ext_zknh = true;
    cpu->cfg.ext_zksed = true;
    cpu->cfg.ext_zksh = true;
    cpu->cfg.ext_svinval = true;

    cpu->cfg.mmu = true;
    cpu->cfg.pmp = true;

#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_SV39);
#endif
}

#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
static void rv128_base_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;

    cpu->cfg.mmu = true;
    cpu->cfg.pmp = true;

    /* Set latest version of privileged specification */
    env->priv_ver = PRIV_VERSION_LATEST;
    set_satp_mode_max_supported(RISCV_CPU(obj), VM_1_10_SV57);
}
#endif /* CONFIG_TCG && !CONFIG_USER_ONLY */

static void rv64i_bare_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    riscv_cpu_set_misa_ext(env, RVI);
}

static void rv64e_bare_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    riscv_cpu_set_misa_ext(env, RVE);
}

#endif /* !TARGET_RISCV64 */

#if defined(TARGET_RISCV32) || \
    (defined(TARGET_RISCV64) && !defined(CONFIG_USER_ONLY))

static void rv32_base_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;

    cpu->cfg.mmu = true;
    cpu->cfg.pmp = true;

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
    riscv_cpu_set_misa_ext(env, RVI | RVM | RVA | RVF | RVD | RVC | RVS | RVU);
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

    riscv_cpu_set_misa_ext(env, RVI | RVM | RVA | RVC | RVU);
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

    riscv_cpu_set_misa_ext(env, RVI | RVM | RVC | RVU);
    env->priv_ver = PRIV_VERSION_1_12_0;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_MBARE);
#endif
    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.pmp = true;
    cpu->cfg.ext_smepmp = true;

    cpu->cfg.ext_zba = true;
    cpu->cfg.ext_zbb = true;
    cpu->cfg.ext_zbc = true;
    cpu->cfg.ext_zbs = true;
}

static void rv32_imafcu_nommu_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    RISCVCPU *cpu = RISCV_CPU(obj);

    riscv_cpu_set_misa_ext(env, RVI | RVM | RVA | RVF | RVC | RVU);
    env->priv_ver = PRIV_VERSION_1_10_0;
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_MBARE);
#endif

    /* inherited from parent obj via riscv_cpu_init() */
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    cpu->cfg.pmp = true;
}

static void rv32i_bare_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    riscv_cpu_set_misa_ext(env, RVI);
}

static void rv32e_bare_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    riscv_cpu_set_misa_ext(env, RVE);
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

    return oc;
}

char *riscv_cpu_get_name(RISCVCPU *cpu)
{
    RISCVCPUClass *rcc = RISCV_CPU_GET_CLASS(cpu);
    const char *typename = object_class_get_name(OBJECT_CLASS(rcc));

    g_assert(g_str_has_suffix(typename, RISCV_CPU_TYPE_SUFFIX));

    return cpu_model_from_type(typename);
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
        target_ulong val = 0;
        RISCVException res = riscv_csrrw_debug(env, CSR_FCSR, &val, 0, 0);
        if (res == RISCV_EXCP_NONE) {
            qemu_fprintf(f, " %-8s " TARGET_FMT_lx "\n",
                    csr_ops[CSR_FCSR].name, val);
        }
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
        uint16_t vlenb = cpu->cfg.vlenb;

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

#ifndef CONFIG_USER_ONLY
bool riscv_cpu_has_work(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    /*
     * Definition of the WFI instruction requires it to ignore the privilege
     * mode and delegation registers, but respect individual enables
     */
    return riscv_cpu_all_pending(env) != 0 ||
        riscv_cpu_sirq_pending(env) != RISCV_EXCP_NONE ||
        riscv_cpu_vsirq_pending(env) != RISCV_EXCP_NONE;
}
#endif /* !CONFIG_USER_ONLY */

static void riscv_cpu_reset_hold(Object *obj, ResetType type)
{
#ifndef CONFIG_USER_ONLY
    uint8_t iprio;
    int i, irq, rdzero;
#endif
    CPUState *cs = CPU(obj);
    RISCVCPU *cpu = RISCV_CPU(cs);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(obj);
    CPURISCVState *env = &cpu->env;

    if (mcc->parent_phases.hold) {
        mcc->parent_phases.hold(obj, type);
    }
#ifndef CONFIG_USER_ONLY
    env->misa_mxl = mcc->misa_mxl_max;
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
        if (riscv_cpu_cfg(env)->ext_smdbltrp) {
            env->mstatus = set_field(env->mstatus, MSTATUS_MDT, 1);
        }
    }
    env->mcause = 0;
    env->miclaim = MIP_SGEIP;
    env->pc = env->resetvec;
    env->bins = 0;
    env->two_stage_lookup = false;

    env->menvcfg = (cpu->cfg.ext_svpbmt ? MENVCFG_PBMTE : 0) |
                   (!cpu->cfg.ext_svade && cpu->cfg.ext_svadu ?
                    MENVCFG_ADUE : 0);
    env->henvcfg = 0;

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

    /*
     * Bits 10, 6, 2 and 12 of mideleg are read only 1 when the Hypervisor
     * extension is enabled.
     */
    if (riscv_has_ext(env, RVH)) {
        env->mideleg |= HS_MODE_INTERRUPTS;
    }

    /*
     * Clear mseccfg and unlock all the PMP entries upon reset.
     * This is allowed as per the priv and smepmp specifications
     * and is needed to clear stale entries across reboots.
     */
    if (riscv_cpu_cfg(env)->ext_smepmp) {
        env->mseccfg = 0;
    }

    pmp_unlock_entries(env);
#else
    env->priv = PRV_U;
    env->senvcfg = 0;
    env->menvcfg = 0;
#endif

    /* on reset elp is clear */
    env->elp = false;
    /* on reset ssp is set to 0 */
    env->ssp = 0;

    env->xl = riscv_cpu_mxl(env);
    cs->exception_index = RISCV_EXCP_NONE;
    env->load_res = -1;
    set_default_nan_mode(1, &env->fp_status);
    /* Default NaN value: sign bit clear, frac msb set */
    set_float_default_nan_pattern(0b01000000, &env->fp_status);
    env->vill = true;

#ifndef CONFIG_USER_ONLY
    if (cpu->cfg.debug) {
        riscv_trigger_reset_hold(env);
    }

    if (cpu->cfg.ext_smrnmi) {
        env->rnmip = 0;
        env->mnstatus = set_field(env->mnstatus, MNSTATUS_NMIE, false);
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

    /*
     * A couple of bits in MSTATUS set the endianness:
     *  - MSTATUS_UBE (User-mode),
     *  - MSTATUS_SBE (Supervisor-mode),
     *  - MSTATUS_MBE (Machine-mode)
     * but we don't implement that yet.
     */
    info->endian = BFD_ENDIAN_LITTLE;

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
    bool rv32 = riscv_cpu_is_32bit(cpu);
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

#ifndef CONFIG_USER_ONLY
    riscv_cpu_satp_mode_finalize(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
#endif

    if (tcg_enabled()) {
        riscv_tcg_cpu_finalize_features(cpu, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            return;
        }
        riscv_tcg_cpu_finalize_dynamic_decoder(cpu);
    } else if (kvm_enabled()) {
        riscv_kvm_cpu_finalize_features(cpu, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

static void riscv_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    RISCVCPU *cpu = RISCV_CPU(dev);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

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

static void riscv_cpu_set_nmi(void *opaque, int irq, int level)
{
    riscv_cpu_set_rnmi(RISCV_CPU(opaque), irq, level);
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
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(obj);
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;

    env->misa_mxl = mcc->misa_mxl_max;

#ifndef CONFIG_USER_ONLY
    qdev_init_gpio_in(DEVICE(obj), riscv_cpu_set_irq,
                      IRQ_LOCAL_MAX + IRQ_LOCAL_GUEST_MAX);
    qdev_init_gpio_in_named(DEVICE(cpu), riscv_cpu_set_nmi,
                            "riscv.cpu.rnmi", RNMI_MAX);
#endif /* CONFIG_USER_ONLY */

    general_user_opts = g_hash_table_new(g_str_hash, g_str_equal);

    /*
     * The timer and performance counters extensions were supported
     * in QEMU before they were added as discrete extensions in the
     * ISA. To keep compatibility we'll always default them to 'true'
     * for all CPUs. Each accelerator will decide what to do when
     * users disable them.
     */
    RISCV_CPU(obj)->cfg.ext_zicntr = true;
    RISCV_CPU(obj)->cfg.ext_zihpm = true;

    /* Default values for non-bool cpu properties */
    cpu->cfg.pmu_mask = MAKE_64BIT_MASK(3, 16);
    cpu->cfg.vlenb = 128 >> 3;
    cpu->cfg.elen = 64;
    cpu->cfg.cbom_blocksize = 64;
    cpu->cfg.cbop_blocksize = 64;
    cpu->cfg.cboz_blocksize = 64;
    cpu->env.vext_ver = VEXT_VERSION_1_00_0;
}

static void riscv_bare_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);

    /*
     * Bare CPUs do not inherit the timer and performance
     * counters from the parent class (see riscv_cpu_init()
     * for info on why the parent enables them).
     *
     * Users have to explicitly enable these counters for
     * bare CPUs.
     */
    cpu->cfg.ext_zicntr = false;
    cpu->cfg.ext_zihpm = false;

    /* Set to QEMU's first supported priv version */
    cpu->env.priv_ver = PRIV_VERSION_1_10_0;

    /*
     * Support all available satp_mode settings. The default
     * value will be set to MBARE if the user doesn't set
     * satp_mode manually (see set_satp_mode_default()).
     */
#ifndef CONFIG_USER_ONLY
    set_satp_mode_max_supported(cpu, VM_1_10_SV64);
#endif
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
    MISA_EXT_INFO(RVV, "v", "Vector operations"),
    MISA_EXT_INFO(RVG, "g", "General purpose (IMAFD_Zicsr_Zifencei)"),
    MISA_EXT_INFO(RVB, "b", "Bit manipulation (Zba_Zbb_Zbs)")
};

static void riscv_cpu_validate_misa_mxl(RISCVCPUClass *mcc)
{
    CPUClass *cc = CPU_CLASS(mcc);

    /* Validate that MISA_MXL is set properly. */
    switch (mcc->misa_mxl_max) {
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
}

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
    MULTI_EXT_CFG_BOOL("smcntrpmf", ext_smcntrpmf, false),
    MULTI_EXT_CFG_BOOL("smcsrind", ext_smcsrind, false),
    MULTI_EXT_CFG_BOOL("smcdeleg", ext_smcdeleg, false),
    MULTI_EXT_CFG_BOOL("sscsrind", ext_sscsrind, false),
    MULTI_EXT_CFG_BOOL("ssccfg", ext_ssccfg, false),
    MULTI_EXT_CFG_BOOL("smctr", ext_smctr, false),
    MULTI_EXT_CFG_BOOL("ssctr", ext_ssctr, false),
    MULTI_EXT_CFG_BOOL("zifencei", ext_zifencei, true),
    MULTI_EXT_CFG_BOOL("zicfilp", ext_zicfilp, false),
    MULTI_EXT_CFG_BOOL("zicfiss", ext_zicfiss, false),
    MULTI_EXT_CFG_BOOL("zicsr", ext_zicsr, true),
    MULTI_EXT_CFG_BOOL("zihintntl", ext_zihintntl, true),
    MULTI_EXT_CFG_BOOL("zihintpause", ext_zihintpause, true),
    MULTI_EXT_CFG_BOOL("zimop", ext_zimop, false),
    MULTI_EXT_CFG_BOOL("zcmop", ext_zcmop, false),
    MULTI_EXT_CFG_BOOL("zacas", ext_zacas, false),
    MULTI_EXT_CFG_BOOL("zama16b", ext_zama16b, false),
    MULTI_EXT_CFG_BOOL("zabha", ext_zabha, false),
    MULTI_EXT_CFG_BOOL("zaamo", ext_zaamo, false),
    MULTI_EXT_CFG_BOOL("zalrsc", ext_zalrsc, false),
    MULTI_EXT_CFG_BOOL("zawrs", ext_zawrs, true),
    MULTI_EXT_CFG_BOOL("zfa", ext_zfa, true),
    MULTI_EXT_CFG_BOOL("zfbfmin", ext_zfbfmin, false),
    MULTI_EXT_CFG_BOOL("zfh", ext_zfh, false),
    MULTI_EXT_CFG_BOOL("zfhmin", ext_zfhmin, false),
    MULTI_EXT_CFG_BOOL("zve32f", ext_zve32f, false),
    MULTI_EXT_CFG_BOOL("zve32x", ext_zve32x, false),
    MULTI_EXT_CFG_BOOL("zve64f", ext_zve64f, false),
    MULTI_EXT_CFG_BOOL("zve64d", ext_zve64d, false),
    MULTI_EXT_CFG_BOOL("zve64x", ext_zve64x, false),
    MULTI_EXT_CFG_BOOL("zvfbfmin", ext_zvfbfmin, false),
    MULTI_EXT_CFG_BOOL("zvfbfwma", ext_zvfbfwma, false),
    MULTI_EXT_CFG_BOOL("zvfh", ext_zvfh, false),
    MULTI_EXT_CFG_BOOL("zvfhmin", ext_zvfhmin, false),
    MULTI_EXT_CFG_BOOL("sstc", ext_sstc, true),
    MULTI_EXT_CFG_BOOL("ssnpm", ext_ssnpm, false),
    MULTI_EXT_CFG_BOOL("sspm", ext_sspm, false),
    MULTI_EXT_CFG_BOOL("supm", ext_supm, false),

    MULTI_EXT_CFG_BOOL("smaia", ext_smaia, false),
    MULTI_EXT_CFG_BOOL("smdbltrp", ext_smdbltrp, false),
    MULTI_EXT_CFG_BOOL("smepmp", ext_smepmp, false),
    MULTI_EXT_CFG_BOOL("smrnmi", ext_smrnmi, false),
    MULTI_EXT_CFG_BOOL("smmpm", ext_smmpm, false),
    MULTI_EXT_CFG_BOOL("smnpm", ext_smnpm, false),
    MULTI_EXT_CFG_BOOL("smstateen", ext_smstateen, false),
    MULTI_EXT_CFG_BOOL("ssaia", ext_ssaia, false),
    MULTI_EXT_CFG_BOOL("ssdbltrp", ext_ssdbltrp, false),
    MULTI_EXT_CFG_BOOL("svade", ext_svade, false),
    MULTI_EXT_CFG_BOOL("svadu", ext_svadu, true),
    MULTI_EXT_CFG_BOOL("svinval", ext_svinval, false),
    MULTI_EXT_CFG_BOOL("svnapot", ext_svnapot, false),
    MULTI_EXT_CFG_BOOL("svpbmt", ext_svpbmt, false),
    MULTI_EXT_CFG_BOOL("svvptc", ext_svvptc, true),

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
    MULTI_EXT_CFG_BOOL("ztso", ext_ztso, false),

    MULTI_EXT_CFG_BOOL("zdinx", ext_zdinx, false),
    MULTI_EXT_CFG_BOOL("zfinx", ext_zfinx, false),
    MULTI_EXT_CFG_BOOL("zhinx", ext_zhinx, false),
    MULTI_EXT_CFG_BOOL("zhinxmin", ext_zhinxmin, false),

    MULTI_EXT_CFG_BOOL("zicbom", ext_zicbom, true),
    MULTI_EXT_CFG_BOOL("zicbop", ext_zicbop, true),
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
    MULTI_EXT_CFG_BOOL("zvkb", ext_zvkb, false),
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

    { },
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

    { },
};

/* These are experimental so mark with 'x-' */
const RISCVCPUMultiExtConfig riscv_cpu_experimental_exts[] = {
    MULTI_EXT_CFG_BOOL("x-svukte", ext_svukte, false),

    { },
};

/*
 * 'Named features' is the name we give to extensions that we
 * don't want to expose to users. They are either immutable
 * (always enabled/disable) or they'll vary depending on
 * the resulting CPU state. They have riscv,isa strings
 * and priv_ver like regular extensions.
 */
const RISCVCPUMultiExtConfig riscv_cpu_named_features[] = {
    MULTI_EXT_CFG_BOOL("zic64b", ext_zic64b, true),
    MULTI_EXT_CFG_BOOL("ssstateen", ext_ssstateen, true),
    MULTI_EXT_CFG_BOOL("sha", ext_sha, true),
    MULTI_EXT_CFG_BOOL("ziccrse", ext_ziccrse, true),

    { },
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

    { },
};

static void cpu_set_prop_err(RISCVCPU *cpu, const char *propname,
                             Error **errp)
{
    g_autofree char *cpuname = riscv_cpu_get_name(cpu);
    error_setg(errp, "CPU '%s' does not allow changing the value of '%s'",
               cpuname, propname);
}

static void prop_pmu_num_set(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint8_t pmu_num, curr_pmu_num;
    uint32_t pmu_mask;

    visit_type_uint8(v, name, &pmu_num, errp);

    curr_pmu_num = ctpop32(cpu->cfg.pmu_mask);

    if (pmu_num != curr_pmu_num && riscv_cpu_is_vendor(obj)) {
        cpu_set_prop_err(cpu, name, errp);
        error_append_hint(errp, "Current '%s' val: %u\n",
                          name, curr_pmu_num);
        return;
    }

    if (pmu_num > (RV_MAX_MHPMCOUNTERS - 3)) {
        error_setg(errp, "Number of counters exceeds maximum available");
        return;
    }

    if (pmu_num == 0) {
        pmu_mask = 0;
    } else {
        pmu_mask = MAKE_64BIT_MASK(3, pmu_num);
    }

    warn_report("\"pmu-num\" property is deprecated; use \"pmu-mask\"");
    cpu->cfg.pmu_mask = pmu_mask;
    cpu_option_add_user_setting("pmu-mask", pmu_mask);
}

static void prop_pmu_num_get(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint8_t pmu_num = ctpop32(cpu->cfg.pmu_mask);

    visit_type_uint8(v, name, &pmu_num, errp);
}

static const PropertyInfo prop_pmu_num = {
    .type = "int8",
    .description = "pmu-num",
    .get = prop_pmu_num_get,
    .set = prop_pmu_num_set,
};

static void prop_pmu_mask_set(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint32_t value;
    uint8_t pmu_num;

    visit_type_uint32(v, name, &value, errp);

    if (value != cpu->cfg.pmu_mask && riscv_cpu_is_vendor(obj)) {
        cpu_set_prop_err(cpu, name, errp);
        error_append_hint(errp, "Current '%s' val: %x\n",
                          name, cpu->cfg.pmu_mask);
        return;
    }

    pmu_num = ctpop32(value);

    if (pmu_num > (RV_MAX_MHPMCOUNTERS - 3)) {
        error_setg(errp, "Number of counters exceeds maximum available");
        return;
    }

    cpu_option_add_user_setting(name, value);
    cpu->cfg.pmu_mask = value;
}

static void prop_pmu_mask_get(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    uint8_t pmu_mask = RISCV_CPU(obj)->cfg.pmu_mask;

    visit_type_uint8(v, name, &pmu_mask, errp);
}

static const PropertyInfo prop_pmu_mask = {
    .type = "int8",
    .description = "pmu-mask",
    .get = prop_pmu_mask_get,
    .set = prop_pmu_mask_set,
};

static void prop_mmu_set(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    bool value;

    visit_type_bool(v, name, &value, errp);

    if (cpu->cfg.mmu != value && riscv_cpu_is_vendor(obj)) {
        cpu_set_prop_err(cpu, "mmu", errp);
        return;
    }

    cpu_option_add_user_setting(name, value);
    cpu->cfg.mmu = value;
}

static void prop_mmu_get(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    bool value = RISCV_CPU(obj)->cfg.mmu;

    visit_type_bool(v, name, &value, errp);
}

static const PropertyInfo prop_mmu = {
    .type = "bool",
    .description = "mmu",
    .get = prop_mmu_get,
    .set = prop_mmu_set,
};

static void prop_pmp_set(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    bool value;

    visit_type_bool(v, name, &value, errp);

    if (cpu->cfg.pmp != value && riscv_cpu_is_vendor(obj)) {
        cpu_set_prop_err(cpu, name, errp);
        return;
    }

    cpu_option_add_user_setting(name, value);
    cpu->cfg.pmp = value;
}

static void prop_pmp_get(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    bool value = RISCV_CPU(obj)->cfg.pmp;

    visit_type_bool(v, name, &value, errp);
}

static const PropertyInfo prop_pmp = {
    .type = "bool",
    .description = "pmp",
    .get = prop_pmp_get,
    .set = prop_pmp_set,
};

static int priv_spec_from_str(const char *priv_spec_str)
{
    int priv_version = -1;

    if (!g_strcmp0(priv_spec_str, PRIV_VER_1_13_0_STR)) {
        priv_version = PRIV_VERSION_1_13_0;
    } else if (!g_strcmp0(priv_spec_str, PRIV_VER_1_12_0_STR)) {
        priv_version = PRIV_VERSION_1_12_0;
    } else if (!g_strcmp0(priv_spec_str, PRIV_VER_1_11_0_STR)) {
        priv_version = PRIV_VERSION_1_11_0;
    } else if (!g_strcmp0(priv_spec_str, PRIV_VER_1_10_0_STR)) {
        priv_version = PRIV_VERSION_1_10_0;
    }

    return priv_version;
}

const char *priv_spec_to_str(int priv_version)
{
    switch (priv_version) {
    case PRIV_VERSION_1_10_0:
        return PRIV_VER_1_10_0_STR;
    case PRIV_VERSION_1_11_0:
        return PRIV_VER_1_11_0_STR;
    case PRIV_VERSION_1_12_0:
        return PRIV_VER_1_12_0_STR;
    case PRIV_VERSION_1_13_0:
        return PRIV_VER_1_13_0_STR;
    default:
        return NULL;
    }
}

static void prop_priv_spec_set(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    g_autofree char *value = NULL;
    int priv_version = -1;

    visit_type_str(v, name, &value, errp);

    priv_version = priv_spec_from_str(value);
    if (priv_version < 0) {
        error_setg(errp, "Unsupported privilege spec version '%s'", value);
        return;
    }

    if (priv_version != cpu->env.priv_ver && riscv_cpu_is_vendor(obj)) {
        cpu_set_prop_err(cpu, name, errp);
        error_append_hint(errp, "Current '%s' val: %s\n", name,
                          object_property_get_str(obj, name, NULL));
        return;
    }

    cpu_option_add_user_setting(name, priv_version);
    cpu->env.priv_ver = priv_version;
}

static void prop_priv_spec_get(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    const char *value = priv_spec_to_str(cpu->env.priv_ver);

    visit_type_str(v, name, (char **)&value, errp);
}

static const PropertyInfo prop_priv_spec = {
    .type = "str",
    .description = "priv_spec",
    /* FIXME enum? */
    .get = prop_priv_spec_get,
    .set = prop_priv_spec_set,
};

static void prop_vext_spec_set(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    g_autofree char *value = NULL;

    visit_type_str(v, name, &value, errp);

    if (g_strcmp0(value, VEXT_VER_1_00_0_STR) != 0) {
        error_setg(errp, "Unsupported vector spec version '%s'", value);
        return;
    }

    cpu_option_add_user_setting(name, VEXT_VERSION_1_00_0);
    cpu->env.vext_ver = VEXT_VERSION_1_00_0;
}

static void prop_vext_spec_get(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    const char *value = VEXT_VER_1_00_0_STR;

    visit_type_str(v, name, (char **)&value, errp);
}

static const PropertyInfo prop_vext_spec = {
    .type = "str",
    .description = "vext_spec",
    /* FIXME enum? */
    .get = prop_vext_spec_get,
    .set = prop_vext_spec_set,
};

static void prop_vlen_set(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint16_t cpu_vlen = cpu->cfg.vlenb << 3;
    uint16_t value;

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    if (!is_power_of_2(value)) {
        error_setg(errp, "Vector extension VLEN must be power of 2");
        return;
    }

    if (value != cpu_vlen && riscv_cpu_is_vendor(obj)) {
        cpu_set_prop_err(cpu, name, errp);
        error_append_hint(errp, "Current '%s' val: %u\n",
                          name, cpu_vlen);
        return;
    }

    cpu_option_add_user_setting(name, value);
    cpu->cfg.vlenb = value >> 3;
}

static void prop_vlen_get(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    uint16_t value = RISCV_CPU(obj)->cfg.vlenb << 3;

    visit_type_uint16(v, name, &value, errp);
}

static const PropertyInfo prop_vlen = {
    .type = "uint16",
    .description = "vlen",
    .get = prop_vlen_get,
    .set = prop_vlen_set,
};

static void prop_elen_set(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint16_t value;

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    if (!is_power_of_2(value)) {
        error_setg(errp, "Vector extension ELEN must be power of 2");
        return;
    }

    if (value != cpu->cfg.elen && riscv_cpu_is_vendor(obj)) {
        cpu_set_prop_err(cpu, name, errp);
        error_append_hint(errp, "Current '%s' val: %u\n",
                          name, cpu->cfg.elen);
        return;
    }

    cpu_option_add_user_setting(name, value);
    cpu->cfg.elen = value;
}

static void prop_elen_get(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    uint16_t value = RISCV_CPU(obj)->cfg.elen;

    visit_type_uint16(v, name, &value, errp);
}

static const PropertyInfo prop_elen = {
    .type = "uint16",
    .description = "elen",
    .get = prop_elen_get,
    .set = prop_elen_set,
};

static void prop_cbom_blksize_set(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint16_t value;

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    if (value != cpu->cfg.cbom_blocksize && riscv_cpu_is_vendor(obj)) {
        cpu_set_prop_err(cpu, name, errp);
        error_append_hint(errp, "Current '%s' val: %u\n",
                          name, cpu->cfg.cbom_blocksize);
        return;
    }

    cpu_option_add_user_setting(name, value);
    cpu->cfg.cbom_blocksize = value;
}

static void prop_cbom_blksize_get(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    uint16_t value = RISCV_CPU(obj)->cfg.cbom_blocksize;

    visit_type_uint16(v, name, &value, errp);
}

static const PropertyInfo prop_cbom_blksize = {
    .type = "uint16",
    .description = "cbom_blocksize",
    .get = prop_cbom_blksize_get,
    .set = prop_cbom_blksize_set,
};

static void prop_cbop_blksize_set(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint16_t value;

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    if (value != cpu->cfg.cbop_blocksize && riscv_cpu_is_vendor(obj)) {
        cpu_set_prop_err(cpu, name, errp);
        error_append_hint(errp, "Current '%s' val: %u\n",
                          name, cpu->cfg.cbop_blocksize);
        return;
    }

    cpu_option_add_user_setting(name, value);
    cpu->cfg.cbop_blocksize = value;
}

static void prop_cbop_blksize_get(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    uint16_t value = RISCV_CPU(obj)->cfg.cbop_blocksize;

    visit_type_uint16(v, name, &value, errp);
}

static const PropertyInfo prop_cbop_blksize = {
    .type = "uint16",
    .description = "cbop_blocksize",
    .get = prop_cbop_blksize_get,
    .set = prop_cbop_blksize_set,
};

static void prop_cboz_blksize_set(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint16_t value;

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    if (value != cpu->cfg.cboz_blocksize && riscv_cpu_is_vendor(obj)) {
        cpu_set_prop_err(cpu, name, errp);
        error_append_hint(errp, "Current '%s' val: %u\n",
                          name, cpu->cfg.cboz_blocksize);
        return;
    }

    cpu_option_add_user_setting(name, value);
    cpu->cfg.cboz_blocksize = value;
}

static void prop_cboz_blksize_get(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    uint16_t value = RISCV_CPU(obj)->cfg.cboz_blocksize;

    visit_type_uint16(v, name, &value, errp);
}

static const PropertyInfo prop_cboz_blksize = {
    .type = "uint16",
    .description = "cboz_blocksize",
    .get = prop_cboz_blksize_get,
    .set = prop_cboz_blksize_set,
};

static void prop_mvendorid_set(Object *obj, Visitor *v, const char *name,
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

static void prop_mvendorid_get(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    uint32_t value = RISCV_CPU(obj)->cfg.mvendorid;

    visit_type_uint32(v, name, &value, errp);
}

static const PropertyInfo prop_mvendorid = {
    .type = "uint32",
    .description = "mvendorid",
    .get = prop_mvendorid_get,
    .set = prop_mvendorid_set,
};

static void prop_mimpid_set(Object *obj, Visitor *v, const char *name,
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

static void prop_mimpid_get(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    uint64_t value = RISCV_CPU(obj)->cfg.mimpid;

    visit_type_uint64(v, name, &value, errp);
}

static const PropertyInfo prop_mimpid = {
    .type = "uint64",
    .description = "mimpid",
    .get = prop_mimpid_get,
    .set = prop_mimpid_set,
};

static void prop_marchid_set(Object *obj, Visitor *v, const char *name,
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

static void prop_marchid_get(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    uint64_t value = RISCV_CPU(obj)->cfg.marchid;

    visit_type_uint64(v, name, &value, errp);
}

static const PropertyInfo prop_marchid = {
    .type = "uint64",
    .description = "marchid",
    .get = prop_marchid_get,
    .set = prop_marchid_set,
};

/*
 * RVA22U64 defines some 'named features' that are cache
 * related: Za64rs, Zic64b, Ziccif, Ziccrse, Ziccamoa
 * and Zicclsm. They are always implemented in TCG and
 * doesn't need to be manually enabled by the profile.
 */
static RISCVCPUProfile RVA22U64 = {
    .u_parent = NULL,
    .s_parent = NULL,
    .name = "rva22u64",
    .misa_ext = RVI | RVM | RVA | RVF | RVD | RVC | RVB | RVU,
    .priv_spec = RISCV_PROFILE_ATTR_UNUSED,
    .satp_mode = RISCV_PROFILE_ATTR_UNUSED,
    .ext_offsets = {
        CPU_CFG_OFFSET(ext_zicsr), CPU_CFG_OFFSET(ext_zihintpause),
        CPU_CFG_OFFSET(ext_zba), CPU_CFG_OFFSET(ext_zbb),
        CPU_CFG_OFFSET(ext_zbs), CPU_CFG_OFFSET(ext_zfhmin),
        CPU_CFG_OFFSET(ext_zkt), CPU_CFG_OFFSET(ext_zicntr),
        CPU_CFG_OFFSET(ext_zihpm), CPU_CFG_OFFSET(ext_zicbom),
        CPU_CFG_OFFSET(ext_zicbop), CPU_CFG_OFFSET(ext_zicboz),

        /* mandatory named features for this profile */
        CPU_CFG_OFFSET(ext_zic64b),

        RISCV_PROFILE_EXT_LIST_END
    }
};

/*
 * As with RVA22U64, RVA22S64 also defines 'named features'.
 *
 * Cache related features that we consider enabled since we don't
 * implement cache: Ssccptr
 *
 * Other named features that we already implement: Sstvecd, Sstvala,
 * Sscounterenw
 *
 * The remaining features/extensions comes from RVA22U64.
 */
static RISCVCPUProfile RVA22S64 = {
    .u_parent = &RVA22U64,
    .s_parent = NULL,
    .name = "rva22s64",
    .misa_ext = RVS,
    .priv_spec = PRIV_VERSION_1_12_0,
    .satp_mode = VM_1_10_SV39,
    .ext_offsets = {
        /* rva22s64 exts */
        CPU_CFG_OFFSET(ext_zifencei), CPU_CFG_OFFSET(ext_svpbmt),
        CPU_CFG_OFFSET(ext_svinval), CPU_CFG_OFFSET(ext_svade),

        RISCV_PROFILE_EXT_LIST_END
    }
};

/*
 * All mandatory extensions from RVA22U64 are present
 * in RVA23U64 so set RVA22 as a parent. We need to
 * declare just the newly added mandatory extensions.
 */
static RISCVCPUProfile RVA23U64 = {
    .u_parent = &RVA22U64,
    .s_parent = NULL,
    .name = "rva23u64",
    .misa_ext = RVV,
    .priv_spec = RISCV_PROFILE_ATTR_UNUSED,
    .satp_mode = RISCV_PROFILE_ATTR_UNUSED,
    .ext_offsets = {
        CPU_CFG_OFFSET(ext_zvfhmin), CPU_CFG_OFFSET(ext_zvbb),
        CPU_CFG_OFFSET(ext_zvkt), CPU_CFG_OFFSET(ext_zihintntl),
        CPU_CFG_OFFSET(ext_zicond), CPU_CFG_OFFSET(ext_zimop),
        CPU_CFG_OFFSET(ext_zcmop), CPU_CFG_OFFSET(ext_zcb),
        CPU_CFG_OFFSET(ext_zfa), CPU_CFG_OFFSET(ext_zawrs),
        CPU_CFG_OFFSET(ext_supm),

        RISCV_PROFILE_EXT_LIST_END
    }
};

/*
 * As with RVA23U64, RVA23S64 also defines 'named features'.
 *
 * Cache related features that we consider enabled since we don't
 * implement cache: Ssccptr
 *
 * Other named features that we already implement: Sstvecd, Sstvala,
 * Sscounterenw, Ssu64xl
 *
 * The remaining features/extensions comes from RVA23S64.
 */
static RISCVCPUProfile RVA23S64 = {
    .u_parent = &RVA23U64,
    .s_parent = &RVA22S64,
    .name = "rva23s64",
    .misa_ext = RVS,
    .priv_spec = PRIV_VERSION_1_13_0,
    .satp_mode = VM_1_10_SV39,
    .ext_offsets = {
        /* New in RVA23S64 */
        CPU_CFG_OFFSET(ext_svnapot), CPU_CFG_OFFSET(ext_sstc),
        CPU_CFG_OFFSET(ext_sscofpmf), CPU_CFG_OFFSET(ext_ssnpm),

        /* Named features: Sha */
        CPU_CFG_OFFSET(ext_sha),

        RISCV_PROFILE_EXT_LIST_END
    }
};

RISCVCPUProfile *riscv_profiles[] = {
    &RVA22U64,
    &RVA22S64,
    &RVA23U64,
    &RVA23S64,
    NULL,
};

static RISCVCPUImpliedExtsRule RVA_IMPLIED = {
    .is_misa = true,
    .ext = RVA,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zalrsc), CPU_CFG_OFFSET(ext_zaamo),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule RVD_IMPLIED = {
    .is_misa = true,
    .ext = RVD,
    .implied_misa_exts = RVF,
    .implied_multi_exts = { RISCV_IMPLIED_EXTS_RULE_END },
};

static RISCVCPUImpliedExtsRule RVF_IMPLIED = {
    .is_misa = true,
    .ext = RVF,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zicsr),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule RVM_IMPLIED = {
    .is_misa = true,
    .ext = RVM,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zmmul),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule RVV_IMPLIED = {
    .is_misa = true,
    .ext = RVV,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zve64d),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZCB_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zcb),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zca),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZCD_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zcd),
    .implied_misa_exts = RVD,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zca),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZCE_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zce),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zcb), CPU_CFG_OFFSET(ext_zcmp),
        CPU_CFG_OFFSET(ext_zcmt),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZCF_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zcf),
    .implied_misa_exts = RVF,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zca),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZCMP_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zcmp),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zca),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZCMT_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zcmt),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zca), CPU_CFG_OFFSET(ext_zicsr),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZDINX_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zdinx),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zfinx),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZFA_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zfa),
    .implied_misa_exts = RVF,
    .implied_multi_exts = { RISCV_IMPLIED_EXTS_RULE_END },
};

static RISCVCPUImpliedExtsRule ZFBFMIN_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zfbfmin),
    .implied_misa_exts = RVF,
    .implied_multi_exts = { RISCV_IMPLIED_EXTS_RULE_END },
};

static RISCVCPUImpliedExtsRule ZFH_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zfh),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zfhmin),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZFHMIN_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zfhmin),
    .implied_misa_exts = RVF,
    .implied_multi_exts = { RISCV_IMPLIED_EXTS_RULE_END },
};

static RISCVCPUImpliedExtsRule ZFINX_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zfinx),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zicsr),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZHINX_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zhinx),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zhinxmin),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZHINXMIN_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zhinxmin),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zfinx),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZICNTR_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zicntr),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zicsr),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZIHPM_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zihpm),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zicsr),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZK_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zk),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zkn), CPU_CFG_OFFSET(ext_zkr),
        CPU_CFG_OFFSET(ext_zkt),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZKN_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zkn),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zbkb), CPU_CFG_OFFSET(ext_zbkc),
        CPU_CFG_OFFSET(ext_zbkx), CPU_CFG_OFFSET(ext_zkne),
        CPU_CFG_OFFSET(ext_zknd), CPU_CFG_OFFSET(ext_zknh),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZKS_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zks),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zbkb), CPU_CFG_OFFSET(ext_zbkc),
        CPU_CFG_OFFSET(ext_zbkx), CPU_CFG_OFFSET(ext_zksed),
        CPU_CFG_OFFSET(ext_zksh),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVBB_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvbb),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zvkb),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVE32F_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zve32f),
    .implied_misa_exts = RVF,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zve32x),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVE32X_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zve32x),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zicsr),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVE64D_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zve64d),
    .implied_misa_exts = RVD,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zve64f),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVE64F_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zve64f),
    .implied_misa_exts = RVF,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zve32f), CPU_CFG_OFFSET(ext_zve64x),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVE64X_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zve64x),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zve32x),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVFBFMIN_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvfbfmin),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zve32f),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVFBFWMA_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvfbfwma),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zvfbfmin), CPU_CFG_OFFSET(ext_zfbfmin),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVFH_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvfh),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zvfhmin), CPU_CFG_OFFSET(ext_zfhmin),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVFHMIN_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvfhmin),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zve32f),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVKN_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvkn),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zvkned), CPU_CFG_OFFSET(ext_zvknhb),
        CPU_CFG_OFFSET(ext_zvkb), CPU_CFG_OFFSET(ext_zvkt),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVKNC_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvknc),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zvkn), CPU_CFG_OFFSET(ext_zvbc),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVKNG_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvkng),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zvkn), CPU_CFG_OFFSET(ext_zvkg),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVKNHB_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvknhb),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zve64x),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVKS_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvks),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zvksed), CPU_CFG_OFFSET(ext_zvksh),
        CPU_CFG_OFFSET(ext_zvkb), CPU_CFG_OFFSET(ext_zvkt),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVKSC_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvksc),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zvks), CPU_CFG_OFFSET(ext_zvbc),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule ZVKSG_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_zvksg),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_zvks), CPU_CFG_OFFSET(ext_zvkg),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule SSCFG_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_ssccfg),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_smcsrind), CPU_CFG_OFFSET(ext_sscsrind),
        CPU_CFG_OFFSET(ext_smcdeleg),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule SUPM_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_supm),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_ssnpm), CPU_CFG_OFFSET(ext_smnpm),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule SSPM_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_sspm),
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_smnpm),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule SMCTR_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_smctr),
    .implied_misa_exts = RVS,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_sscsrind),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

static RISCVCPUImpliedExtsRule SSCTR_IMPLIED = {
    .ext = CPU_CFG_OFFSET(ext_ssctr),
    .implied_misa_exts = RVS,
    .implied_multi_exts = {
        CPU_CFG_OFFSET(ext_sscsrind),

        RISCV_IMPLIED_EXTS_RULE_END
    },
};

RISCVCPUImpliedExtsRule *riscv_misa_ext_implied_rules[] = {
    &RVA_IMPLIED, &RVD_IMPLIED, &RVF_IMPLIED,
    &RVM_IMPLIED, &RVV_IMPLIED, NULL
};

RISCVCPUImpliedExtsRule *riscv_multi_ext_implied_rules[] = {
    &ZCB_IMPLIED, &ZCD_IMPLIED, &ZCE_IMPLIED,
    &ZCF_IMPLIED, &ZCMP_IMPLIED, &ZCMT_IMPLIED,
    &ZDINX_IMPLIED, &ZFA_IMPLIED, &ZFBFMIN_IMPLIED,
    &ZFH_IMPLIED, &ZFHMIN_IMPLIED, &ZFINX_IMPLIED,
    &ZHINX_IMPLIED, &ZHINXMIN_IMPLIED, &ZICNTR_IMPLIED,
    &ZIHPM_IMPLIED, &ZK_IMPLIED, &ZKN_IMPLIED,
    &ZKS_IMPLIED, &ZVBB_IMPLIED, &ZVE32F_IMPLIED,
    &ZVE32X_IMPLIED, &ZVE64D_IMPLIED, &ZVE64F_IMPLIED,
    &ZVE64X_IMPLIED, &ZVFBFMIN_IMPLIED, &ZVFBFWMA_IMPLIED,
    &ZVFH_IMPLIED, &ZVFHMIN_IMPLIED, &ZVKN_IMPLIED,
    &ZVKNC_IMPLIED, &ZVKNG_IMPLIED, &ZVKNHB_IMPLIED,
    &ZVKS_IMPLIED,  &ZVKSC_IMPLIED, &ZVKSG_IMPLIED, &SSCFG_IMPLIED,
    &SUPM_IMPLIED, &SSPM_IMPLIED, &SMCTR_IMPLIED, &SSCTR_IMPLIED,
    NULL
};

static const Property riscv_cpu_properties[] = {
    DEFINE_PROP_BOOL("debug", RISCVCPU, cfg.debug, true),

    {.name = "pmu-mask", .info = &prop_pmu_mask},
    {.name = "pmu-num", .info = &prop_pmu_num}, /* Deprecated */

    {.name = "mmu", .info = &prop_mmu},
    {.name = "pmp", .info = &prop_pmp},

    {.name = "priv_spec", .info = &prop_priv_spec},
    {.name = "vext_spec", .info = &prop_vext_spec},

    {.name = "vlen", .info = &prop_vlen},
    {.name = "elen", .info = &prop_elen},

    {.name = "cbom_blocksize", .info = &prop_cbom_blksize},
    {.name = "cbop_blocksize", .info = &prop_cbop_blksize},
    {.name = "cboz_blocksize", .info = &prop_cboz_blksize},

    {.name = "mvendorid", .info = &prop_mvendorid},
    {.name = "mimpid", .info = &prop_mimpid},
    {.name = "marchid", .info = &prop_marchid},

#ifndef CONFIG_USER_ONLY
    DEFINE_PROP_UINT64("resetvec", RISCVCPU, env.resetvec, DEFAULT_RSTVEC),
    DEFINE_PROP_UINT64("rnmi-interrupt-vector", RISCVCPU, env.rnmi_irqvec,
                       DEFAULT_RNMI_IRQVEC),
    DEFINE_PROP_UINT64("rnmi-exception-vector", RISCVCPU, env.rnmi_excpvec,
                       DEFAULT_RNMI_EXCPVEC),
#endif

    DEFINE_PROP_BOOL("short-isa-string", RISCVCPU, cfg.short_isa_string, false),

    DEFINE_PROP_BOOL("rvv_ta_all_1s", RISCVCPU, cfg.rvv_ta_all_1s, false),
    DEFINE_PROP_BOOL("rvv_ma_all_1s", RISCVCPU, cfg.rvv_ma_all_1s, false),
    DEFINE_PROP_BOOL("rvv_vl_half_avl", RISCVCPU, cfg.rvv_vl_half_avl, false),

    /*
     * write_misa() is marked as experimental for now so mark
     * it with -x and default to 'false'.
     */
    DEFINE_PROP_BOOL("x-misa-w", RISCVCPU, cfg.misa_w, false),
};

#if defined(TARGET_RISCV64)
static void rva22u64_profile_cpu_init(Object *obj)
{
    rv64i_bare_cpu_init(obj);

    RVA22U64.enabled = true;
}

static void rva22s64_profile_cpu_init(Object *obj)
{
    rv64i_bare_cpu_init(obj);

    RVA22S64.enabled = true;
}

static void rva23u64_profile_cpu_init(Object *obj)
{
    rv64i_bare_cpu_init(obj);

    RVA23U64.enabled = true;
}

static void rva23s64_profile_cpu_init(Object *obj)
{
    rv64i_bare_cpu_init(obj);

    RVA23S64.enabled = true;
}
#endif

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

#ifndef CONFIG_USER_ONLY
static int64_t riscv_get_arch_id(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);

    return cpu->env.mhartid;
}

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps riscv_sysemu_ops = {
    .has_work = riscv_cpu_has_work,
    .get_phys_page_debug = riscv_cpu_get_phys_page_debug,
    .write_elf64_note = riscv_cpu_write_elf64_note,
    .write_elf32_note = riscv_cpu_write_elf32_note,
    .legacy_vmsd = &vmstate_riscv_cpu,
};
#endif

static void riscv_cpu_common_class_init(ObjectClass *c, void *data)
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
    cc->dump_state = riscv_cpu_dump_state;
    cc->set_pc = riscv_cpu_set_pc;
    cc->get_pc = riscv_cpu_get_pc;
    cc->gdb_read_register = riscv_cpu_gdb_read_register;
    cc->gdb_write_register = riscv_cpu_gdb_write_register;
    cc->gdb_stop_before_watchpoint = true;
    cc->disas_set_info = riscv_cpu_disas_set_info;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &riscv_sysemu_ops;
    cc->get_arch_id = riscv_get_arch_id;
#endif
    cc->gdb_arch_name = riscv_gdb_arch_name;
#ifdef CONFIG_TCG
    cc->tcg_ops = &riscv_tcg_ops;
#endif /* CONFIG_TCG */

    device_class_set_props(dc, riscv_cpu_properties);
}

static void riscv_cpu_class_init(ObjectClass *c, void *data)
{
    RISCVCPUClass *mcc = RISCV_CPU_CLASS(c);

    mcc->misa_mxl_max = (RISCVMXL)GPOINTER_TO_UINT(data);
    riscv_cpu_validate_misa_mxl(mcc);
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
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    int i;
    const size_t maxlen = sizeof("rv128") + sizeof(riscv_single_letter_exts);
    char *isa_str = g_new(char, maxlen);
    int xlen = riscv_cpu_max_xlen(mcc);
    char *p = isa_str + snprintf(isa_str, maxlen, "rv%d", xlen);

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

#ifndef CONFIG_USER_ONLY
static char **riscv_isa_extensions_list(RISCVCPU *cpu, int *count)
{
    int maxlen = ARRAY_SIZE(riscv_single_letter_exts) + ARRAY_SIZE(isa_edata_arr);
    char **extensions = g_new(char *, maxlen);

    for (int i = 0; i < sizeof(riscv_single_letter_exts) - 1; i++) {
        if (cpu->env.misa_ext & RV(riscv_single_letter_exts[i])) {
            extensions[*count] = g_new(char, 2);
            snprintf(extensions[*count], 2, "%c",
                     qemu_tolower(riscv_single_letter_exts[i]));
            (*count)++;
        }
    }

    for (const RISCVIsaExtData *edata = isa_edata_arr; edata->name; edata++) {
        if (isa_ext_is_enabled(cpu, edata->ext_enable_offset)) {
            extensions[*count] = g_strdup(edata->name);
            (*count)++;
        }
    }

    return extensions;
}

void riscv_isa_write_fdt(RISCVCPU *cpu, void *fdt, char *nodename)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    const size_t maxlen = sizeof("rv128i");
    g_autofree char *isa_base = g_new(char, maxlen);
    g_autofree char *riscv_isa;
    char **isa_extensions;
    int count = 0;
    int xlen = riscv_cpu_max_xlen(mcc);

    riscv_isa = riscv_isa_string(cpu);
    qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", riscv_isa);

    snprintf(isa_base, maxlen, "rv%di", xlen);
    qemu_fdt_setprop_string(fdt, nodename, "riscv,isa-base", isa_base);

    isa_extensions = riscv_isa_extensions_list(cpu, &count);
    qemu_fdt_setprop_string_array(fdt, nodename, "riscv,isa-extensions",
                                  isa_extensions, count);

    for (int i = 0; i < count; i++) {
        g_free(isa_extensions[i]);
    }

    g_free(isa_extensions);
}
#endif

#define DEFINE_DYNAMIC_CPU(type_name, misa_mxl_max, initfn) \
    {                                                       \
        .name = (type_name),                                \
        .parent = TYPE_RISCV_DYNAMIC_CPU,                   \
        .instance_init = (initfn),                          \
        .class_init = riscv_cpu_class_init,                 \
        .class_data = GUINT_TO_POINTER(misa_mxl_max)        \
    }

#define DEFINE_VENDOR_CPU(type_name, misa_mxl_max, initfn)  \
    {                                                       \
        .name = (type_name),                                \
        .parent = TYPE_RISCV_VENDOR_CPU,                    \
        .instance_init = (initfn),                          \
        .class_init = riscv_cpu_class_init,                 \
        .class_data = GUINT_TO_POINTER(misa_mxl_max)        \
    }

#define DEFINE_BARE_CPU(type_name, misa_mxl_max, initfn)    \
    {                                                       \
        .name = (type_name),                                \
        .parent = TYPE_RISCV_BARE_CPU,                      \
        .instance_init = (initfn),                          \
        .class_init = riscv_cpu_class_init,                 \
        .class_data = GUINT_TO_POINTER(misa_mxl_max)        \
    }

#define DEFINE_PROFILE_CPU(type_name, misa_mxl_max, initfn) \
    {                                                       \
        .name = (type_name),                                \
        .parent = TYPE_RISCV_BARE_CPU,                      \
        .instance_init = (initfn),                          \
        .class_init = riscv_cpu_class_init,                 \
        .class_data = GUINT_TO_POINTER(misa_mxl_max)        \
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
        .class_init = riscv_cpu_common_class_init,
    },
    {
        .name = TYPE_RISCV_DYNAMIC_CPU,
        .parent = TYPE_RISCV_CPU,
        .abstract = true,
    },
    {
        .name = TYPE_RISCV_VENDOR_CPU,
        .parent = TYPE_RISCV_CPU,
        .abstract = true,
    },
    {
        .name = TYPE_RISCV_BARE_CPU,
        .parent = TYPE_RISCV_CPU,
        .instance_init = riscv_bare_cpu_init,
        .abstract = true,
    },
#if defined(TARGET_RISCV32)
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_MAX,       MXL_RV32,  riscv_max_cpu_init),
#elif defined(TARGET_RISCV64)
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_MAX,       MXL_RV64,  riscv_max_cpu_init),
#endif

#if defined(TARGET_RISCV32) || \
    (defined(TARGET_RISCV64) && !defined(CONFIG_USER_ONLY))
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_BASE32,    MXL_RV32,  rv32_base_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_IBEX,       MXL_RV32,  rv32_ibex_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_SIFIVE_E31, MXL_RV32,  rv32_sifive_e_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_SIFIVE_E34, MXL_RV32,  rv32_imafcu_nommu_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_SIFIVE_U34, MXL_RV32,  rv32_sifive_u_cpu_init),
    DEFINE_BARE_CPU(TYPE_RISCV_CPU_RV32I,        MXL_RV32,  rv32i_bare_cpu_init),
    DEFINE_BARE_CPU(TYPE_RISCV_CPU_RV32E,        MXL_RV32,  rv32e_bare_cpu_init),
#endif

#if (defined(TARGET_RISCV64) && !defined(CONFIG_USER_ONLY))
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_MAX32,     MXL_RV32,  riscv_max_cpu_init),
#endif

#if defined(TARGET_RISCV64)
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_BASE64,    MXL_RV64,  rv64_base_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_SIFIVE_E51, MXL_RV64,  rv64_sifive_e_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_SIFIVE_U54, MXL_RV64,  rv64_sifive_u_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_SHAKTI_C,   MXL_RV64,  rv64_sifive_u_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_THEAD_C906, MXL_RV64,  rv64_thead_c906_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_TT_ASCALON, MXL_RV64,  rv64_tt_ascalon_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_VEYRON_V1,  MXL_RV64,  rv64_veyron_v1_cpu_init),
    DEFINE_VENDOR_CPU(TYPE_RISCV_CPU_XIANGSHAN_NANHU,
                                                 MXL_RV64, rv64_xiangshan_nanhu_cpu_init),
#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
    DEFINE_DYNAMIC_CPU(TYPE_RISCV_CPU_BASE128,   MXL_RV128, rv128_base_cpu_init),
#endif /* CONFIG_TCG && !CONFIG_USER_ONLY */
    DEFINE_BARE_CPU(TYPE_RISCV_CPU_RV64I,        MXL_RV64,  rv64i_bare_cpu_init),
    DEFINE_BARE_CPU(TYPE_RISCV_CPU_RV64E,        MXL_RV64,  rv64e_bare_cpu_init),
    DEFINE_PROFILE_CPU(TYPE_RISCV_CPU_RVA22U64,  MXL_RV64,  rva22u64_profile_cpu_init),
    DEFINE_PROFILE_CPU(TYPE_RISCV_CPU_RVA22S64,  MXL_RV64,  rva22s64_profile_cpu_init),
    DEFINE_PROFILE_CPU(TYPE_RISCV_CPU_RVA23U64,  MXL_RV64,  rva23u64_profile_cpu_init),
    DEFINE_PROFILE_CPU(TYPE_RISCV_CPU_RVA23S64,  MXL_RV64,  rva23s64_profile_cpu_init),
#endif /* TARGET_RISCV64 */
};

DEFINE_TYPES(riscv_cpu_type_infos)
