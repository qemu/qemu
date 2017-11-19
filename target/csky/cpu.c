/*
 * CSKY CPU
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"
#include "exec/exec-all.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"


static void csky_cpu_set_pc(CPUState *cs, vaddr value)
{
    CSKYCPU *cpu = CSKY_CPU(cs);

    cpu->env.pc = value;
}

static bool csky_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & CPU_INTERRUPT_HARD;
}

static inline void csky_set_feature(CPUCSKYState *env, uint64_t feature)
{
    env->features |= feature;
}

static inline bool csky_has_feature(CPUCSKYState *env, uint64_t feature)
{
    if (env->features & feature) {
        return true;
    } else {
        return false;
    }
}

static void csky_cpu_handle_opts(CPUCSKYState *env)
{
#ifndef CONFIG_USER_ONLY
    QemuOpts *opts;
    uint32_t n;
    bool b;
    char *str;
    opts = qemu_opts_find(qemu_find_opts("cpu-prop"), NULL);
    if (opts) {
        n = qemu_opt_get_number(opts, "pctrace", 0);
        if (n > 1024) {
            error_report("pctrace bigger than 1024");
            exit(1);
        }
        env->pctraces_max_num = n;

        n = qemu_opt_get_number(opts, "vdsp", 0);
        if (n != 0) {
            if (!csky_has_feature(env, CPU_810)) {
                error_report("only 810 support vdsp");
                exit(1);
            }

            if (n == 64) {
                csky_set_feature(env, ABIV2_VDSP64);
            } else if (n == 128) {
                csky_set_feature(env, ABIV2_VDSP128);
            } else {
                error_report("vdsp= only allow 64 or 128");
                exit(1);
            }
        }

        b = qemu_opt_get_bool(opts, "elrw", false);
        if (b) {
            csky_set_feature(env, ABIV2_ELRW);
        }

        str = qemu_opt_get_del(opts, "mem_prot");
        if (str != NULL) {
            if (!strcmp(str, "mmu")) {
                env->features |= CSKY_MMU;
                env->features &= ~CSKY_MGU;
            } else if (!strcmp(str, "mgu")) {
                env->features |= CSKY_MGU;
                env->features &= ~CSKY_MMU;
            } else if (!strcmp(str, "no")) {
                env->features &= ~CSKY_MGU;
                env->features &= ~CSKY_MMU;
            } else {
                error_report("mem_prot= only allow mmu/mgu/no");
                exit(1);
            }
        }

        b = qemu_opt_get_bool(opts, "mmu_default", false);
        if (b) {
            env->mmu_default = 1;
        }

        b = qemu_opt_get_bool(opts, "tb_trace", false);
        if (b) {
            env->tb_trace = 1;
        }

        b = qemu_opt_get_bool(opts, "unaligned_access", false);
        if (b) {
            csky_set_feature(env, UNALIGNED_ACCESS);
        }
    }
#endif
}

struct csky_trace_info tb_trace_info[TB_TRACE_NUM];

/* CPUClass::reset() */
static void csky_cpu_reset(CPUState *s)
{
    CSKYCPU *cpu = CSKY_CPU(s);
    CSKYCPUClass *mcc = CSKY_CPU_GET_CLASS(cpu);
    CPUCSKYState *env = &cpu->env;
    uint32_t cpidr;

    mcc->parent_reset(s);

    /* backup data before memset */
    cpidr = env->cp0.cpidr[0];

    memset(env, 0, offsetof(CPUCSKYState, features));

    env->cp0.cpidr[0] = cpidr;
    env->cp0.cpidr[1] = 0x17000000;
    env->cp0.cpidr[2] = 0x2ff0f20c;
    env->cp0.cpidr[3] = 0x30000000;

#if defined(TARGET_CSKYV1)
    env->cp1.fsr = 0x0;
#endif

#if defined(CONFIG_USER_ONLY)
    env->cp0.psr = 0x140;
#if defined(TARGET_CSKYV2)
    env->sce_condexec_bits = 1;
    env->sce_condexec_bits_bk = 1;
#endif

#else
    if (csky_has_feature(env, ABIV2_TEE)) {
        env->tee.nt_psr = 0x80000000;
        env->tee.t_psr = 0xc0000000;
        env->cp0.psr = env->tee.t_psr;
        env->psr_t = PSR_T(env->cp0.psr);
        env->mmu = env->t_mmu;
    } else {
        env->cp0.psr = 0x80000000;
        env->mmu = env->nt_mmu;
    }
    env->psr_s = PSR_S(env->cp0.psr);
#if defined(TARGET_CSKYV2)
    env->psr_bm = PSR_BM(env->cp0.psr);
    env->sce_condexec_bits = 1;
    env->sce_condexec_bits_bk = 1;
    env->mmu.msa0 = 0x1e;
    env->mmu.msa1 = 0x16;
#endif

#ifdef TARGET_WORDS_BIGENDIAN
    env->cp0.ccr = 0x80;
#endif

    csky_nommu_init(env);
#endif

    env->vfp.fp_status.flush_inputs_to_zero = 1;
    s->exception_index = -1;
    tlb_flush(s);

    env->trace_info = tb_trace_info;
    env->trace_index = 0;
    csky_cpu_handle_opts(env);
}

static void csky_cpu_disas_set_info(CPUState *s, disassemble_info *info)
{
#if defined(TARGET_CSKYV1)
    info->print_insn = print_insn_csky_v1;
#else
    info->print_insn = print_insn_csky_v2;
#endif
}

/* CPU models */
static ObjectClass *csky_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }

    typename = g_strdup_printf("%s-" TYPE_CSKY_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && (object_class_dynamic_cast(oc, TYPE_CSKY_CPU) == NULL ||
                       object_class_is_abstract(oc))) {
        return NULL;
    }
    return oc;
}

static void ck510_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV1);
    csky_set_feature(env, CPU_510);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK510;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck520_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV1);
    csky_set_feature(env, CPU_520);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK520;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck610_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV1);
    csky_set_feature(env, CPU_610);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK610;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck610e_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV1);
    csky_set_feature(env, CPU_610);
    csky_set_feature(env, ABIV1_DSP);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK610;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck610f_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV1);
    csky_set_feature(env, CPU_610);
    csky_set_feature(env, ABIV1_FPU);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK610;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck610ef_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV1);
    csky_set_feature(env, CPU_610);
    csky_set_feature(env, ABIV1_DSP);
    csky_set_feature(env, ABIV1_FPU);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK610;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck801_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_801);
    csky_set_feature(env, ABIV2_ELRW);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK801;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck801t_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_801);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, ABIV2_ELRW);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK801;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck802_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_802);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK802;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck802j_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_802);
    csky_set_feature(env, ABIV2_JAVA);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK802;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck802t_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_802);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK802;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803t_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803f_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_FPU_803S);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803e_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_DSP);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803et_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_DSP);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803ef_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_DSP);
    csky_set_feature(env, ABIV2_FPU_803S);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803ft_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, ABIV2_FPU_803S);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803eft_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_DSP);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, ABIV2_FPU_803S);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803r1_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_803S_R1);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803tr1_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_803S_R1);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803fr1_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_803S_R1);
    csky_set_feature(env, ABIV2_FPU_803S);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803er1_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_803S_R1);
    csky_set_feature(env, ABIV2_EDSP);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803etr1_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_803S_R1);
    csky_set_feature(env, ABIV2_EDSP);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803efr1_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_803S_R1);
    csky_set_feature(env, ABIV2_EDSP);
    csky_set_feature(env, ABIV2_FPU_803S);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803ftr1_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_803S_R1);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, ABIV2_FPU_803S);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck803eftr1_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_803S);
    csky_set_feature(env, ABIV2_803S_R1);
    csky_set_feature(env, ABIV2_EDSP);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, ABIV2_FPU_803S);
    csky_set_feature(env, CSKY_MGU);
    env->cpuid = CSKY_CPUID_CK803S;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck807_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_807);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK807;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck807f_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_807);
    csky_set_feature(env, ABIV2_FPU);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK807;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck810_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_810);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK810;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck810v_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_810);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK810;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck810f_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_810);
    csky_set_feature(env, ABIV2_FPU);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK810;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck810t_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_810);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK810;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck810fv_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_810);
    csky_set_feature(env, ABIV2_FPU);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK810;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck810tv_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_810);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK810;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck810ft_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_810);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK810;
    env->cp0.cpidr[0] = env->cpuid;
}

static void ck810ftv_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_810);
    csky_set_feature(env, ABIV2_DSP);
    csky_set_feature(env, ABIV2_TEE);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK810;
    env->cp0.cpidr[0] = env->cpuid;
}

static void any_cpu_initfn(Object *obj)
{
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;

    csky_set_feature(env, CPU_ABIV2);
    csky_set_feature(env, CPU_810);
    csky_set_feature(env, ABIV2_DSP);
    csky_set_feature(env, ABIV2_FPU);
    csky_set_feature(env, ABIV2_VDSP128);
    csky_set_feature(env, CSKY_MMU);
    env->cpuid = CSKY_CPUID_CK810;
    env->cp0.cpidr[0] = env->cpuid;
}

typedef struct CSKYCPUInfo {
    const char *name;
    void (*instance_init)(Object *obj);
} CSKYCPUInfo;

/*
 * postfix is alphabetical order: c, e, f, h, j, m, t, v, x
 * Crypto, Edsp, Float, sHield, Java, Memory, Trust, Vdsp, Xcore
 */
static const CSKYCPUInfo csky_cpus[] = {
    { .name = "ck510",       .instance_init = ck510_cpu_initfn },
    { .name = "ck520",       .instance_init = ck520_cpu_initfn },
    { .name = "ck610",       .instance_init = ck610_cpu_initfn },
    { .name = "ck610e",      .instance_init = ck610e_cpu_initfn },
    { .name = "ck610f",      .instance_init = ck610f_cpu_initfn },
    { .name = "ck610ef",     .instance_init = ck610ef_cpu_initfn },
    { .name = "ck801",       .instance_init = ck801_cpu_initfn },
    { .name = "ck801t",      .instance_init = ck801t_cpu_initfn },
    { .name = "ck802",       .instance_init = ck802_cpu_initfn },
    { .name = "ck802h",      .instance_init = ck802_cpu_initfn },
    { .name = "ck802j",      .instance_init = ck802j_cpu_initfn },
    { .name = "ck802t",      .instance_init = ck802t_cpu_initfn },
    { .name = "ck802ht",     .instance_init = ck802t_cpu_initfn },
    { .name = "ck803",       .instance_init = ck803_cpu_initfn },
    { .name = "ck803h",      .instance_init = ck803_cpu_initfn },
    { .name = "ck803t",      .instance_init = ck803t_cpu_initfn },
    { .name = "ck803ht",     .instance_init = ck803t_cpu_initfn },
    { .name = "ck803f",      .instance_init = ck803f_cpu_initfn },
    { .name = "ck803fh",     .instance_init = ck803f_cpu_initfn },
    { .name = "ck803e",      .instance_init = ck803e_cpu_initfn },
    { .name = "ck803eh",     .instance_init = ck803e_cpu_initfn },
    { .name = "ck803et",     .instance_init = ck803et_cpu_initfn },
    { .name = "ck803eht",    .instance_init = ck803et_cpu_initfn },
    { .name = "ck803ef",     .instance_init = ck803ef_cpu_initfn },
    { .name = "ck803efh",    .instance_init = ck803ef_cpu_initfn },
    { .name = "ck803ft",     .instance_init = ck803ft_cpu_initfn },
    { .name = "ck803eft",    .instance_init = ck803eft_cpu_initfn },
    { .name = "ck803efht",   .instance_init = ck803eft_cpu_initfn },
    { .name = "ck803r1",     .instance_init = ck803r1_cpu_initfn },
    { .name = "ck803hr1",    .instance_init = ck803r1_cpu_initfn },
    { .name = "ck803tr1",    .instance_init = ck803tr1_cpu_initfn },
    { .name = "ck803htr1",   .instance_init = ck803tr1_cpu_initfn },
    { .name = "ck803fr1",    .instance_init = ck803fr1_cpu_initfn },
    { .name = "ck803fhr1",   .instance_init = ck803fr1_cpu_initfn },
    { .name = "ck803er1",    .instance_init = ck803er1_cpu_initfn },
    { .name = "ck803ehr1",   .instance_init = ck803er1_cpu_initfn },
    { .name = "ck803etr1",   .instance_init = ck803etr1_cpu_initfn },
    { .name = "ck803ehtr1",  .instance_init = ck803etr1_cpu_initfn },
    { .name = "ck803efr1",   .instance_init = ck803efr1_cpu_initfn },
    { .name = "ck803efhr1",  .instance_init = ck803efr1_cpu_initfn },
    { .name = "ck803ftr1",   .instance_init = ck803ftr1_cpu_initfn },
    { .name = "ck803fhtr1",  .instance_init = ck803ftr1_cpu_initfn },
    { .name = "ck803eftr1",  .instance_init = ck803eftr1_cpu_initfn },
    { .name = "ck803efhtr1", .instance_init = ck803eftr1_cpu_initfn },
    { .name = "ck803s",      .instance_init = ck803_cpu_initfn },
    { .name = "ck803sf",     .instance_init = ck803f_cpu_initfn },
    { .name = "ck803sef",    .instance_init = ck803ef_cpu_initfn },
    { .name = "ck803st",     .instance_init = ck803t_cpu_initfn },
    { .name = "ck807",       .instance_init = ck807_cpu_initfn },
    { .name = "ck807e",      .instance_init = ck807_cpu_initfn },
    { .name = "ck807f",      .instance_init = ck807f_cpu_initfn },
    { .name = "ck807ef",     .instance_init = ck807f_cpu_initfn },
    { .name = "ck810",       .instance_init = ck810_cpu_initfn },
    { .name = "ck810v",      .instance_init = ck810v_cpu_initfn },
    { .name = "ck810f",      .instance_init = ck810f_cpu_initfn },
    { .name = "ck810t",      .instance_init = ck810t_cpu_initfn },
    { .name = "ck810fv",     .instance_init = ck810fv_cpu_initfn },
    { .name = "ck810tv",     .instance_init = ck810tv_cpu_initfn },
    { .name = "ck810ft",     .instance_init = ck810ft_cpu_initfn },
    { .name = "ck810ftv",    .instance_init = ck810ftv_cpu_initfn },
    { .name = "ck810e",      .instance_init = ck810_cpu_initfn },
    { .name = "ck810et",     .instance_init = ck810t_cpu_initfn },
    { .name = "ck810ef",     .instance_init = ck810f_cpu_initfn },
    { .name = "ck810efm",    .instance_init = ck810f_cpu_initfn },
    { .name = "ck810eft",    .instance_init = ck810ft_cpu_initfn },
    { .name = "any",         .instance_init = any_cpu_initfn },
    { .name = NULL }
};

static void csky_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    /* CSKYCPU *cpu = CSKY_CPU(dev); */
    CSKYCPUClass *cc = CSKY_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    /* csky_cpu_init_gdb(cpu); */

    cpu_reset(cs);
    qemu_init_vcpu(cs);

    cc->parent_realize(dev, errp);
}

static void csky_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    CSKYCPU *cpu = CSKY_CPU(obj);
    CPUCSKYState *env = &cpu->env;
    static bool inited;

    cs->env_ptr = env;

    if (tcg_enabled() && !inited) {
        inited = true;
        csky_translate_init();
    }
}

static const VMStateDescription vmstate_csky_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

static void csky_cpu_class_init(ObjectClass *c, void *data)
{
    CSKYCPUClass *mcc = CSKY_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);

    mcc->parent_realize = dc->realize;
    dc->realize = csky_cpu_realizefn;

    mcc->parent_reset = cc->reset;
    cc->reset = csky_cpu_reset;

    cc->class_by_name = csky_cpu_class_by_name;
    cc->has_work = csky_cpu_has_work;
    cc->do_interrupt = csky_cpu_do_interrupt;
    cc->do_unaligned_access = csky_cpu_do_unaligned_access;
    cc->cpu_exec_interrupt = csky_cpu_exec_interrupt;
    cc->dump_state = csky_cpu_dump_state;
    cc->set_pc = csky_cpu_set_pc;
    cc->gdb_read_register = csky_cpu_gdb_read_register;
    cc->gdb_write_register = csky_cpu_gdb_write_register;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = csky_cpu_handle_mmu_fault;
#else
    cc->get_phys_page_debug = csky_cpu_get_phys_page_debug;
#endif
    cc->disas_set_info = csky_cpu_disas_set_info;

    cc->gdb_num_core_regs = 188;

    dc->vmsd = &vmstate_csky_cpu;

#ifdef CONFIG_TCG
    cc->tcg_initialize = csky_translate_init;
#endif
}

static void register_cpu_type(const CSKYCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_CSKY_CPU,
        .instance_init = info->instance_init,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_CSKY_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo csky_cpu_type_info = {
    .name = TYPE_CSKY_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(CSKYCPU),
    .instance_init = csky_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(CSKYCPUClass),
    .class_init = csky_cpu_class_init,
};

static void csky_cpu_register_types(void)
{
    int i;

    type_register_static(&csky_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(csky_cpus); i++) {
        register_cpu_type(&csky_cpus[i]);
    }
}

type_init(csky_cpu_register_types)
