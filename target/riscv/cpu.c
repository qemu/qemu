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
#include "internals.h"
#include "exec/exec-all.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "fpu/softfloat-helpers.h"
#include "sysemu/kvm.h"
#include "kvm_riscv.h"

/* RISC-V CPU definitions */

static const char riscv_exts[26] = "IEMAFDQCLBJTPVNSUHKORWXYZG";

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

static void set_resetvec(CPURISCVState *env, target_ulong resetvec)
{
#ifndef CONFIG_USER_ONLY
    env->resetvec = resetvec;
#endif
}

static void riscv_any_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
#if defined(TARGET_RISCV32)
    set_misa(env, MXL_RV32, RVI | RVM | RVA | RVF | RVD | RVC | RVU);
#elif defined(TARGET_RISCV64)
    set_misa(env, MXL_RV64, RVI | RVM | RVA | RVF | RVD | RVC | RVU);
#endif
    set_priv_version(env, PRIV_VERSION_1_11_0);
}

#if defined(TARGET_RISCV64)
static void rv64_base_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    /* We set this in the realise function */
    set_misa(env, MXL_RV64, 0);
}

static void rv64_sifive_u_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, MXL_RV64, RVI | RVM | RVA | RVF | RVD | RVC | RVS | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
}

static void rv64_sifive_e_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, MXL_RV64, RVI | RVM | RVA | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    qdev_prop_set_bit(DEVICE(obj), "mmu", false);
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
}
#else
static void rv32_base_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    /* We set this in the realise function */
    set_misa(env, MXL_RV32, 0);
}

static void rv32_sifive_u_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, MXL_RV32, RVI | RVM | RVA | RVF | RVD | RVC | RVS | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
}

static void rv32_sifive_e_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, MXL_RV32, RVI | RVM | RVA | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    qdev_prop_set_bit(DEVICE(obj), "mmu", false);
}

static void rv32_ibex_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, MXL_RV32, RVI | RVM | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    qdev_prop_set_bit(DEVICE(obj), "mmu", false);
    qdev_prop_set_bit(DEVICE(obj), "x-epmp", true);
}

static void rv32_imafcu_nommu_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, MXL_RV32, RVI | RVM | RVA | RVF | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    set_resetvec(env, DEFAULT_RSTVEC);
    qdev_prop_set_bit(DEVICE(obj), "mmu", false);
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
        qemu_fprintf(f, " %s %d\n", "V      =  ", riscv_cpu_virt_enabled(env));
    }
#endif
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "pc      ", env->pc);
#ifndef CONFIG_USER_ONLY
    {
        static const int dump_csrs[] = {
            CSR_MHARTID,
            CSR_MSTATUS,
            CSR_MSTATUSH,
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

static void riscv_cpu_synchronize_from_tb(CPUState *cs,
                                          const TranslationBlock *tb)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    RISCVMXL xl = FIELD_EX32(tb->flags, TB_FLAGS, XL);

    if (xl == MXL_RV32) {
        env->pc = (int32_t)tb->pc;
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
    return (env->mip & env->mie) != 0;
#else
    return true;
#endif
}

void restore_state_to_opc(CPURISCVState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    RISCVMXL xl = FIELD_EX32(tb->flags, TB_FLAGS, XL);
    if (xl == MXL_RV32) {
        env->pc = (int32_t)data[0];
    } else {
        env->pc = data[0];
    }
}

static void riscv_cpu_reset(DeviceState *dev)
{
#ifndef CONFIG_USER_ONLY
    uint8_t iprio;
    int i, irq, rdzero;
#endif
    CPUState *cs = CPU(dev);
    RISCVCPU *cpu = RISCV_CPU(cs);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    CPURISCVState *env = &cpu->env;

    mcc->parent_reset(dev);
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
    env->two_stage_lookup = false;

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
    env->mmte |= (PM_EXT_INITIAL | MMTE_M_PM_CURRENT);
#endif
    env->xl = riscv_cpu_mxl(env);
    riscv_cpu_update_mask(env);
    cs->exception_index = RISCV_EXCP_NONE;
    env->load_res = -1;
    set_default_nan_mode(1, &env->fp_status);

#ifndef CONFIG_USER_ONLY
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

static void riscv_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    RISCVCPU *cpu = RISCV_CPU(dev);
    CPURISCVState *env = &cpu->env;
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(dev);
    CPUClass *cc = CPU_CLASS(mcc);
    int priv_version = 0;
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    if (cpu->cfg.priv_spec) {
        if (!g_strcmp0(cpu->cfg.priv_spec, "v1.11.0")) {
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

    if (priv_version) {
        set_priv_version(env, priv_version);
    } else if (!env->priv_ver) {
        set_priv_version(env, PRIV_VERSION_1_11_0);
    }

    if (cpu->cfg.mmu) {
        riscv_set_feature(env, RISCV_FEATURE_MMU);
    }

    if (cpu->cfg.pmp) {
        riscv_set_feature(env, RISCV_FEATURE_PMP);

        /*
         * Enhanced PMP should only be available
         * on harts with PMP support
         */
        if (cpu->cfg.epmp) {
            riscv_set_feature(env, RISCV_FEATURE_EPMP);
        }
    }

    if (cpu->cfg.aia) {
        riscv_set_feature(env, RISCV_FEATURE_AIA);
    }

    set_resetvec(env, cpu->cfg.resetvec);

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

    /* If only MISA_EXT is unset for misa, then set it from properties */
    if (env->misa_ext == 0) {
        uint32_t ext = 0;

        /* Do some ISA extension error checking */
        if (cpu->cfg.ext_i && cpu->cfg.ext_e) {
            error_setg(errp,
                       "I and E extensions are incompatible");
                       return;
       }

        if (!cpu->cfg.ext_i && !cpu->cfg.ext_e) {
            error_setg(errp,
                       "Either I or E extension must be set");
                       return;
       }

       if (cpu->cfg.ext_g && !(cpu->cfg.ext_i & cpu->cfg.ext_m &
                               cpu->cfg.ext_a & cpu->cfg.ext_f &
                               cpu->cfg.ext_d)) {
            warn_report("Setting G will also set IMAFD");
            cpu->cfg.ext_i = true;
            cpu->cfg.ext_m = true;
            cpu->cfg.ext_a = true;
            cpu->cfg.ext_f = true;
            cpu->cfg.ext_d = true;
        }

        if (cpu->cfg.ext_zdinx || cpu->cfg.ext_zhinx ||
            cpu->cfg.ext_zhinxmin) {
            cpu->cfg.ext_zfinx = true;
        }

        /* Set the ISA extensions, checks should have happened above */
        if (cpu->cfg.ext_i) {
            ext |= RVI;
        }
        if (cpu->cfg.ext_e) {
            ext |= RVE;
        }
        if (cpu->cfg.ext_m) {
            ext |= RVM;
        }
        if (cpu->cfg.ext_a) {
            ext |= RVA;
        }
        if (cpu->cfg.ext_f) {
            ext |= RVF;
        }
        if (cpu->cfg.ext_d) {
            ext |= RVD;
        }
        if (cpu->cfg.ext_c) {
            ext |= RVC;
        }
        if (cpu->cfg.ext_s) {
            ext |= RVS;
        }
        if (cpu->cfg.ext_u) {
            ext |= RVU;
        }
        if (cpu->cfg.ext_h) {
            ext |= RVH;
        }
        if (cpu->cfg.ext_v) {
            int vext_version = VEXT_VERSION_1_00_0;
            ext |= RVV;
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
            if (cpu->cfg.elen > 64 || cpu->cfg.vlen < 8) {
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
        if ((cpu->cfg.ext_zve32f || cpu->cfg.ext_zve64f) && !cpu->cfg.ext_f) {
            error_setg(errp, "Zve32f/Zve64f extension depends upon RVF.");
            return;
        }
        if (cpu->cfg.ext_j) {
            ext |= RVJ;
        }
        if (cpu->cfg.ext_zfinx && ((ext & (RVF | RVD)) || cpu->cfg.ext_zfh ||
                                   cpu->cfg.ext_zfhmin)) {
            error_setg(errp,
                    "'Zfinx' cannot be supported together with 'F', 'D', 'Zfh',"
                    " 'Zfhmin'");
            return;
        }

        set_misa(env, env->misa_mxl, ext);
    }

    riscv_cpu_register_gdb_regs_for_features(cs);

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    mcc->parent_realize(dev, errp);
}

#ifndef CONFIG_USER_ONLY
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
        case IRQ_S_EXT:
        case IRQ_VS_EXT:
        case IRQ_M_EXT:
             if (kvm_enabled()) {
                kvm_riscv_set_irq(cpu, irq, level);
             } else {
                riscv_cpu_update_mip(cpu, 1 << irq, BOOL_TO_MASK(level));
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
        riscv_cpu_update_mip(cpu, MIP_SGEIP,
                             BOOL_TO_MASK(!!(env->hgeie & env->hgeip)));
    } else {
        g_assert_not_reached();
    }
}
#endif /* CONFIG_USER_ONLY */

static void riscv_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);

    cpu_set_cpustate_pointers(cpu);

#ifndef CONFIG_USER_ONLY
    qdev_init_gpio_in(DEVICE(cpu), riscv_cpu_set_irq,
                      IRQ_LOCAL_MAX + IRQ_LOCAL_GUEST_MAX);
#endif /* CONFIG_USER_ONLY */
}

static Property riscv_cpu_properties[] = {
    /* Defaults for standard extensions */
    DEFINE_PROP_BOOL("i", RISCVCPU, cfg.ext_i, true),
    DEFINE_PROP_BOOL("e", RISCVCPU, cfg.ext_e, false),
    DEFINE_PROP_BOOL("g", RISCVCPU, cfg.ext_g, true),
    DEFINE_PROP_BOOL("m", RISCVCPU, cfg.ext_m, true),
    DEFINE_PROP_BOOL("a", RISCVCPU, cfg.ext_a, true),
    DEFINE_PROP_BOOL("f", RISCVCPU, cfg.ext_f, true),
    DEFINE_PROP_BOOL("d", RISCVCPU, cfg.ext_d, true),
    DEFINE_PROP_BOOL("c", RISCVCPU, cfg.ext_c, true),
    DEFINE_PROP_BOOL("s", RISCVCPU, cfg.ext_s, true),
    DEFINE_PROP_BOOL("u", RISCVCPU, cfg.ext_u, true),
    DEFINE_PROP_BOOL("v", RISCVCPU, cfg.ext_v, false),
    DEFINE_PROP_BOOL("h", RISCVCPU, cfg.ext_h, true),
    DEFINE_PROP_BOOL("Counters", RISCVCPU, cfg.ext_counters, true),
    DEFINE_PROP_BOOL("Zifencei", RISCVCPU, cfg.ext_ifencei, true),
    DEFINE_PROP_BOOL("Zicsr", RISCVCPU, cfg.ext_icsr, true),
    DEFINE_PROP_BOOL("Zfh", RISCVCPU, cfg.ext_zfh, false),
    DEFINE_PROP_BOOL("Zfhmin", RISCVCPU, cfg.ext_zfhmin, false),
    DEFINE_PROP_BOOL("Zve32f", RISCVCPU, cfg.ext_zve32f, false),
    DEFINE_PROP_BOOL("Zve64f", RISCVCPU, cfg.ext_zve64f, false),
    DEFINE_PROP_BOOL("mmu", RISCVCPU, cfg.mmu, true),
    DEFINE_PROP_BOOL("pmp", RISCVCPU, cfg.pmp, true),

    DEFINE_PROP_STRING("priv_spec", RISCVCPU, cfg.priv_spec),
    DEFINE_PROP_STRING("vext_spec", RISCVCPU, cfg.vext_spec),
    DEFINE_PROP_UINT16("vlen", RISCVCPU, cfg.vlen, 128),
    DEFINE_PROP_UINT16("elen", RISCVCPU, cfg.elen, 64),

    DEFINE_PROP_BOOL("svinval", RISCVCPU, cfg.ext_svinval, false),
    DEFINE_PROP_BOOL("svnapot", RISCVCPU, cfg.ext_svnapot, false),
    DEFINE_PROP_BOOL("svpbmt", RISCVCPU, cfg.ext_svpbmt, false),

    DEFINE_PROP_BOOL("zba", RISCVCPU, cfg.ext_zba, true),
    DEFINE_PROP_BOOL("zbb", RISCVCPU, cfg.ext_zbb, true),
    DEFINE_PROP_BOOL("zbc", RISCVCPU, cfg.ext_zbc, true),
    DEFINE_PROP_BOOL("zbs", RISCVCPU, cfg.ext_zbs, true),

    DEFINE_PROP_BOOL("zdinx", RISCVCPU, cfg.ext_zdinx, false),
    DEFINE_PROP_BOOL("zfinx", RISCVCPU, cfg.ext_zfinx, false),
    DEFINE_PROP_BOOL("zhinx", RISCVCPU, cfg.ext_zhinx, false),
    DEFINE_PROP_BOOL("zhinxmin", RISCVCPU, cfg.ext_zhinxmin, false),

    /* Vendor-specific custom extensions */
    DEFINE_PROP_BOOL("xventanacondops", RISCVCPU, cfg.ext_XVentanaCondOps, false),

    /* These are experimental so mark with 'x-' */
    DEFINE_PROP_BOOL("x-j", RISCVCPU, cfg.ext_j, false),
    /* ePMP 0.9.3 */
    DEFINE_PROP_BOOL("x-epmp", RISCVCPU, cfg.epmp, false),
    DEFINE_PROP_BOOL("x-aia", RISCVCPU, cfg.aia, false),

    DEFINE_PROP_UINT64("resetvec", RISCVCPU, cfg.resetvec, DEFAULT_RSTVEC),
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

#ifndef CONFIG_USER_ONLY
    .tlb_fill = riscv_cpu_tlb_fill,
    .cpu_exec_interrupt = riscv_cpu_exec_interrupt,
    .do_interrupt = riscv_cpu_do_interrupt,
    .do_transaction_failed = riscv_cpu_do_transaction_failed,
    .do_unaligned_access = riscv_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};

static void riscv_cpu_class_init(ObjectClass *c, void *data)
{
    RISCVCPUClass *mcc = RISCV_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);

    device_class_set_parent_realize(dc, riscv_cpu_realize,
                                    &mcc->parent_realize);

    device_class_set_parent_reset(dc, riscv_cpu_reset, &mcc->parent_reset);

    cc->class_by_name = riscv_cpu_class_by_name;
    cc->has_work = riscv_cpu_has_work;
    cc->dump_state = riscv_cpu_dump_state;
    cc->set_pc = riscv_cpu_set_pc;
    cc->gdb_read_register = riscv_cpu_gdb_read_register;
    cc->gdb_write_register = riscv_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 33;
    cc->gdb_stop_before_watchpoint = true;
    cc->disas_set_info = riscv_cpu_disas_set_info;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &riscv_sysemu_ops;
#endif
    cc->gdb_arch_name = riscv_gdb_arch_name;
    cc->gdb_get_dynamic_xml = riscv_gdb_get_dynamic_xml;
    cc->tcg_ops = &riscv_tcg_ops;

    device_class_set_props(dc, riscv_cpu_properties);
}

char *riscv_isa_string(RISCVCPU *cpu)
{
    int i;
    const size_t maxlen = sizeof("rv128") + sizeof(riscv_exts) + 1;
    char *isa_str = g_new(char, maxlen);
    char *p = isa_str + snprintf(isa_str, maxlen, "rv%d", TARGET_LONG_BITS);
    for (i = 0; i < sizeof(riscv_exts); i++) {
        if (cpu->env.misa_ext & RV(riscv_exts[i])) {
            *p++ = qemu_tolower(riscv_exts[i]);
        }
    }
    *p = '\0';
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
    DEFINE_CPU(TYPE_RISCV_CPU_ANY,              riscv_any_cpu_init),
#if defined(CONFIG_KVM)
    DEFINE_CPU(TYPE_RISCV_CPU_HOST,             riscv_host_cpu_init),
#endif
#if defined(TARGET_RISCV32)
    DEFINE_CPU(TYPE_RISCV_CPU_BASE32,           rv32_base_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_IBEX,             rv32_ibex_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_E31,       rv32_sifive_e_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_E34,       rv32_imafcu_nommu_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_U34,       rv32_sifive_u_cpu_init),
#elif defined(TARGET_RISCV64)
    DEFINE_CPU(TYPE_RISCV_CPU_BASE64,           rv64_base_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_E51,       rv64_sifive_e_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_U54,       rv64_sifive_u_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SHAKTI_C,         rv64_sifive_u_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_BASE128,          rv128_base_cpu_init),
#endif
};

DEFINE_TYPES(riscv_cpu_type_infos)
