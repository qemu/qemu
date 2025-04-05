/*
 * Sparc CPU init helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qemu/module.h"
#include "qemu/qemu-print.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "exec/exec-all.h"
#include "exec/translation-block.h"
#include "hw/qdev-properties.h"
#include "qapi/visitor.h"
#include "tcg/tcg.h"
#include "fpu/softfloat.h"
#include "target/sparc/translate.h"

//#define DEBUG_FEATURES

static void sparc_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    SPARCCPUClass *scc = SPARC_CPU_GET_CLASS(obj);
    CPUSPARCState *env = cpu_env(cs);

    if (scc->parent_phases.hold) {
        scc->parent_phases.hold(obj, type);
    }

    memset(env, 0, offsetof(CPUSPARCState, end_reset_fields));
    env->cwp = 0;
#ifndef TARGET_SPARC64
    env->wim = 1;
#endif
    env->regwptr = env->regbase + (env->cwp * 16);
#if defined(CONFIG_USER_ONLY)
#ifdef TARGET_SPARC64
    env->cleanwin = env->nwindows - 2;
    env->cansave = env->nwindows - 2;
    env->pstate = PS_RMO | PS_PEF | PS_IE;
    env->asi = 0x82; /* Primary no-fault */
#endif
#else
#if !defined(TARGET_SPARC64)
    env->psret = 0;
    env->psrs = 1;
    env->psrps = 1;
#endif
#ifdef TARGET_SPARC64
    env->pstate = PS_PRIV | PS_RED | PS_PEF;
    if (!cpu_has_hypervisor(env)) {
        env->pstate |= PS_AG;
    }
    env->hpstate = cpu_has_hypervisor(env) ? HS_PRIV : 0;
    env->tl = env->maxtl;
    env->gl = 2;
    cpu_tsptr(env)->tt = TT_POWER_ON_RESET;
    env->lsu = 0;
#else
    env->mmuregs[0] &= ~(MMU_E | MMU_NF);
    env->mmuregs[0] |= env->def.mmu_bm;
#endif
    env->pc = 0;
    env->npc = env->pc + 4;
#endif
    env->cache_control = 0;
    cpu_put_fsr(env, 0);
}

#ifndef CONFIG_USER_ONLY
static bool sparc_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        CPUSPARCState *env = cpu_env(cs);

        if (cpu_interrupts_enabled(env) && env->interrupt_index > 0) {
            int pil = env->interrupt_index & 0xf;
            int type = env->interrupt_index & 0xf0;

            if (type != TT_EXTINT || cpu_pil_allowed(env, pil)) {
                cs->exception_index = env->interrupt_index;
                sparc_cpu_do_interrupt(cs);
                return true;
            }
        }
    }
    return false;
}
#endif /* !CONFIG_USER_ONLY */

static void cpu_sparc_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->print_insn = print_insn_sparc;
    info->endian = BFD_ENDIAN_BIG;
#ifdef TARGET_SPARC64
    info->mach = bfd_mach_sparc_v9b;
#endif
}

static void
cpu_add_feat_as_prop(const char *typename, const char *name, const char *val)
{
    GlobalProperty *prop = g_new0(typeof(*prop), 1);
    prop->driver = typename;
    prop->property = g_strdup(name);
    prop->value = g_strdup(val);
    qdev_prop_register_global(prop);
}

/* Parse "+feature,-feature,feature=foo" CPU feature string */
static void sparc_cpu_parse_features(const char *typename, char *features,
                                     Error **errp)
{
    GList *l, *plus_features = NULL, *minus_features = NULL;
    char *featurestr; /* Single 'key=value" string being parsed */
    static bool cpu_globals_initialized;

    if (cpu_globals_initialized) {
        return;
    }
    cpu_globals_initialized = true;

    if (!features) {
        return;
    }

    for (featurestr = strtok(features, ",");
         featurestr;
         featurestr = strtok(NULL, ",")) {
        const char *name;
        const char *val = NULL;
        char *eq = NULL;

        /* Compatibility syntax: */
        if (featurestr[0] == '+') {
            plus_features = g_list_append(plus_features,
                                          g_strdup(featurestr + 1));
            continue;
        } else if (featurestr[0] == '-') {
            minus_features = g_list_append(minus_features,
                                           g_strdup(featurestr + 1));
            continue;
        }

        eq = strchr(featurestr, '=');
        name = featurestr;
        if (eq) {
            *eq++ = 0;
            val = eq;

            /*
             * Temporarily, only +feat/-feat will be supported
             * for boolean properties until we remove the
             * minus-overrides-plus semantics and just follow
             * the order options appear on the command-line.
             *
             * TODO: warn if user is relying on minus-override-plus semantics
             * TODO: remove minus-override-plus semantics after
             *       warning for a few releases
             */
            if (!strcasecmp(val, "on") ||
                !strcasecmp(val, "off") ||
                !strcasecmp(val, "true") ||
                !strcasecmp(val, "false")) {
                error_setg(errp, "Boolean properties in format %s=%s"
                                 " are not supported", name, val);
                return;
            }
        } else {
            error_setg(errp, "Unsupported property format: %s", name);
            return;
        }
        cpu_add_feat_as_prop(typename, name, val);
    }

    for (l = plus_features; l; l = l->next) {
        const char *name = l->data;
        cpu_add_feat_as_prop(typename, name, "on");
    }
    g_list_free_full(plus_features, g_free);

    for (l = minus_features; l; l = l->next) {
        const char *name = l->data;
        cpu_add_feat_as_prop(typename, name, "off");
    }
    g_list_free_full(minus_features, g_free);
}

void cpu_sparc_set_id(CPUSPARCState *env, unsigned int cpu)
{
#if !defined(TARGET_SPARC64)
    env->mxccregs[7] = ((cpu + 8) & 0xf) << 24;
#endif
}

static const sparc_def_t sparc_defs[] = {
#ifdef TARGET_SPARC64
    {
        .name = "Fujitsu-Sparc64",
        .iu_version = ((0x04ULL << 48) | (0x02ULL << 32) | (0ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 4,
        .maxtl = 4,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Fujitsu-Sparc64-III",
        .iu_version = ((0x04ULL << 48) | (0x03ULL << 32) | (0ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 5,
        .maxtl = 4,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Fujitsu-Sparc64-IV",
        .iu_version = ((0x04ULL << 48) | (0x04ULL << 32) | (0ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Fujitsu-Sparc64-V",
        .iu_version = ((0x04ULL << 48) | (0x05ULL << 32) | (0x51ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-UltraSparc-I",
        .iu_version = ((0x17ULL << 48) | (0x10ULL << 32) | (0x40ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-UltraSparc-II",
        .iu_version = ((0x17ULL << 48) | (0x11ULL << 32) | (0x20ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-UltraSparc-IIi",
        .iu_version = ((0x17ULL << 48) | (0x12ULL << 32) | (0x91ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-UltraSparc-IIe",
        .iu_version = ((0x17ULL << 48) | (0x13ULL << 32) | (0x14ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun-UltraSparc-III",
        .iu_version = ((0x3eULL << 48) | (0x14ULL << 32) | (0x34ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun-UltraSparc-III-Cu",
        .iu_version = ((0x3eULL << 48) | (0x15ULL << 32) | (0x41ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_3,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun-UltraSparc-IIIi",
        .iu_version = ((0x3eULL << 48) | (0x16ULL << 32) | (0x34ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun-UltraSparc-IV",
        .iu_version = ((0x3eULL << 48) | (0x18ULL << 32) | (0x31ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_4,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun-UltraSparc-IV-plus",
        .iu_version = ((0x3eULL << 48) | (0x19ULL << 32) | (0x22ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES | CPU_FEATURE_CMT,
    },
    {
        .name = "Sun-UltraSparc-IIIi-plus",
        .iu_version = ((0x3eULL << 48) | (0x22ULL << 32) | (0ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_3,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun-UltraSparc-T1",
        /* defined in sparc_ifu_fdp.v and ctu.h */
        .iu_version = ((0x3eULL << 48) | (0x23ULL << 32) | (0x02ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_sun4v,
        .nwindows = 8,
        .maxtl = 6,
        .features = CPU_DEFAULT_FEATURES | CPU_FEATURE_HYPV | CPU_FEATURE_CMT
        | CPU_FEATURE_GL,
    },
    {
        .name = "Sun-UltraSparc-T2",
        /* defined in tlu_asi_ctl.v and n2_revid_cust.v */
        .iu_version = ((0x3eULL << 48) | (0x24ULL << 32) | (0x02ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_sun4v,
        .nwindows = 8,
        .maxtl = 6,
        .features = CPU_DEFAULT_FEATURES | CPU_FEATURE_HYPV | CPU_FEATURE_CMT
        | CPU_FEATURE_GL,
    },
    {
        .name = "NEC-UltraSparc-I",
        .iu_version = ((0x22ULL << 48) | (0x10ULL << 32) | (0x40ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
#else
    {
        .name = "Fujitsu-MB86904",
        .iu_version = 0x04 << 24, /* Impl 0, ver 4 */
        .fpu_version = 4 << FSR_VER_SHIFT, /* FPU version 4 (Meiko) */
        .mmu_version = 0x04 << 24, /* Impl 0, ver 4 */
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x00ffffc0,
        .mmu_cxr_mask = 0x000000ff,
        .mmu_sfsr_mask = 0x00016fff,
        .mmu_trcr_mask = 0x00ffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Fujitsu-MB86907",
        .iu_version = 0x05 << 24, /* Impl 0, ver 5 */
        .fpu_version = 4 << FSR_VER_SHIFT, /* FPU version 4 (Meiko) */
        .mmu_version = 0x05 << 24, /* Impl 0, ver 5 */
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x000000ff,
        .mmu_sfsr_mask = 0x00016fff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-MicroSparc-I",
        .iu_version = 0x41000000,
        .fpu_version = 4 << FSR_VER_SHIFT,
        .mmu_version = 0x41000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0x00016fff,
        .mmu_trcr_mask = 0x0000003f,
        .nwindows = 7,
        .features = CPU_FEATURE_MUL | CPU_FEATURE_DIV,
    },
    {
        .name = "TI-MicroSparc-II",
        .iu_version = 0x42000000,
        .fpu_version = 4 << FSR_VER_SHIFT,
        .mmu_version = 0x02000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x00ffffc0,
        .mmu_cxr_mask = 0x000000ff,
        .mmu_sfsr_mask = 0x00016fff,
        .mmu_trcr_mask = 0x00ffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-MicroSparc-IIep",
        .iu_version = 0x42000000,
        .fpu_version = 4 << FSR_VER_SHIFT,
        .mmu_version = 0x04000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x00ffffc0,
        .mmu_cxr_mask = 0x000000ff,
        .mmu_sfsr_mask = 0x00016bff,
        .mmu_trcr_mask = 0x00ffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-SuperSparc-40", /* STP1020NPGA */
        .iu_version = 0x41000000, /* SuperSPARC 2.x */
        .fpu_version = 0 << FSR_VER_SHIFT,
        .mmu_version = 0x00000800, /* SuperSPARC 2.x, no MXCC */
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-SuperSparc-50", /* STP1020PGA */
        .iu_version = 0x40000000, /* SuperSPARC 3.x */
        .fpu_version = 0 << FSR_VER_SHIFT,
        .mmu_version = 0x01000800, /* SuperSPARC 3.x, no MXCC */
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-SuperSparc-51",
        .iu_version = 0x40000000, /* SuperSPARC 3.x */
        .fpu_version = 0 << FSR_VER_SHIFT,
        .mmu_version = 0x01000000, /* SuperSPARC 3.x, MXCC */
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .mxcc_version = 0x00000104,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-SuperSparc-60", /* STP1020APGA */
        .iu_version = 0x40000000, /* SuperSPARC 3.x */
        .fpu_version = 0 << FSR_VER_SHIFT,
        .mmu_version = 0x01000800, /* SuperSPARC 3.x, no MXCC */
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-SuperSparc-61",
        .iu_version = 0x44000000, /* SuperSPARC 3.x */
        .fpu_version = 0 << FSR_VER_SHIFT,
        .mmu_version = 0x01000000, /* SuperSPARC 3.x, MXCC */
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .mxcc_version = 0x00000104,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI-SuperSparc-II",
        .iu_version = 0x40000000, /* SuperSPARC II 1.x */
        .fpu_version = 0 << FSR_VER_SHIFT,
        .mmu_version = 0x08000000, /* SuperSPARC II 1.x, MXCC */
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .mxcc_version = 0x00000104,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "LEON2",
        .iu_version = 0xf2000000,
        .fpu_version = 4 << FSR_VER_SHIFT, /* FPU version 4 (Meiko) */
        .mmu_version = 0xf2000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES | CPU_FEATURE_TA0_SHUTDOWN,
    },
    {
        .name = "LEON3",
        .iu_version = 0xf3000000,
        .fpu_version = 4 << FSR_VER_SHIFT, /* FPU version 4 (Meiko) */
        .mmu_version = 0xf3000000,
        .mmu_bm = 0x00000000,
        .mmu_ctpr_mask = 0xfffffffc,
        .mmu_cxr_mask = 0x000000ff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES | CPU_FEATURE_TA0_SHUTDOWN |
        CPU_FEATURE_ASR17 | CPU_FEATURE_CACHE_CTRL | CPU_FEATURE_POWERDOWN |
        CPU_FEATURE_CASA,
    },
#endif
};

/* This must match sparc_cpu_properties[]. */
static const char * const feature_name[] = {
    [CPU_FEATURE_BIT_FLOAT128] = "float128",
#ifdef TARGET_SPARC64
    [CPU_FEATURE_BIT_CMT] = "cmt",
    [CPU_FEATURE_BIT_GL] = "gl",
    [CPU_FEATURE_BIT_HYPV] = "hypv",
    [CPU_FEATURE_BIT_VIS1] = "vis1",
    [CPU_FEATURE_BIT_VIS2] = "vis2",
    [CPU_FEATURE_BIT_FMAF] = "fmaf",
    [CPU_FEATURE_BIT_VIS3] = "vis3",
    [CPU_FEATURE_BIT_IMA] = "ima",
    [CPU_FEATURE_BIT_VIS4] = "vis4",
#else
    [CPU_FEATURE_BIT_MUL] = "mul",
    [CPU_FEATURE_BIT_DIV] = "div",
    [CPU_FEATURE_BIT_FSMULD] = "fsmuld",
#endif
};

static void print_features(uint32_t features, const char *prefix)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(feature_name); i++) {
        if (feature_name[i] && (features & (1 << i))) {
            if (prefix) {
                qemu_printf("%s", prefix);
            }
            qemu_printf("%s ", feature_name[i]);
        }
    }
}

void sparc_cpu_list(void)
{
    unsigned int i;

    qemu_printf("Available CPU types:\n");
    for (i = 0; i < ARRAY_SIZE(sparc_defs); i++) {
        qemu_printf(" %-20s (IU " TARGET_FMT_lx
                    " FPU %08x MMU %08x NWINS %d) ",
                    sparc_defs[i].name,
                    sparc_defs[i].iu_version,
                    sparc_defs[i].fpu_version,
                    sparc_defs[i].mmu_version,
                    sparc_defs[i].nwindows);
        print_features(CPU_DEFAULT_FEATURES & ~sparc_defs[i].features, "-");
        print_features(~CPU_DEFAULT_FEATURES & sparc_defs[i].features, "+");
        qemu_printf("\n");
    }
    qemu_printf("Default CPU feature flags (use '-' to remove): ");
    print_features(CPU_DEFAULT_FEATURES, NULL);
    qemu_printf("\n");
    qemu_printf("Available CPU feature flags (use '+' to add): ");
    print_features(~CPU_DEFAULT_FEATURES, NULL);
    qemu_printf("\n");
    qemu_printf("Numerical features (use '=' to set): iu_version "
                "fpu_version mmu_version nwindows\n");
}

static void cpu_print_cc(FILE *f, uint32_t cc)
{
    qemu_fprintf(f, "%c%c%c%c", cc & PSR_NEG ? 'N' : '-',
                 cc & PSR_ZERO ? 'Z' : '-', cc & PSR_OVF ? 'V' : '-',
                 cc & PSR_CARRY ? 'C' : '-');
}

#ifdef TARGET_SPARC64
#define REGS_PER_LINE 4
#else
#define REGS_PER_LINE 8
#endif

static void sparc_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CPUSPARCState *env = cpu_env(cs);
    int i, x;

    qemu_fprintf(f, "pc: " TARGET_FMT_lx "  npc: " TARGET_FMT_lx "\n", env->pc,
                 env->npc);

    for (i = 0; i < 8; i++) {
        if (i % REGS_PER_LINE == 0) {
            qemu_fprintf(f, "%%g%d-%d:", i, i + REGS_PER_LINE - 1);
        }
        qemu_fprintf(f, " " TARGET_FMT_lx, env->gregs[i]);
        if (i % REGS_PER_LINE == REGS_PER_LINE - 1) {
            qemu_fprintf(f, "\n");
        }
    }
    for (x = 0; x < 3; x++) {
        for (i = 0; i < 8; i++) {
            if (i % REGS_PER_LINE == 0) {
                qemu_fprintf(f, "%%%c%d-%d: ",
                             x == 0 ? 'o' : (x == 1 ? 'l' : 'i'),
                             i, i + REGS_PER_LINE - 1);
            }
            qemu_fprintf(f, TARGET_FMT_lx " ", env->regwptr[i + x * 8]);
            if (i % REGS_PER_LINE == REGS_PER_LINE - 1) {
                qemu_fprintf(f, "\n");
            }
        }
    }

    if (flags & CPU_DUMP_FPU) {
        for (i = 0; i < TARGET_DPREGS; i++) {
            if ((i & 3) == 0) {
                qemu_fprintf(f, "%%f%02d: ", i * 2);
            }
            qemu_fprintf(f, " %016" PRIx64, env->fpr[i].ll);
            if ((i & 3) == 3) {
                qemu_fprintf(f, "\n");
            }
        }
    }

#ifdef TARGET_SPARC64
    qemu_fprintf(f, "pstate: %08x ccr: %02x (icc: ", env->pstate,
                 (unsigned)cpu_get_ccr(env));
    cpu_print_cc(f, cpu_get_ccr(env) << PSR_CARRY_SHIFT);
    qemu_fprintf(f, " xcc: ");
    cpu_print_cc(f, cpu_get_ccr(env) << (PSR_CARRY_SHIFT - 4));
    qemu_fprintf(f, ") asi: %02x tl: %d pil: %x gl: %d\n", env->asi, env->tl,
                 env->psrpil, env->gl);
    qemu_fprintf(f, "tbr: " TARGET_FMT_lx " hpstate: " TARGET_FMT_lx " htba: "
                 TARGET_FMT_lx "\n", env->tbr, env->hpstate, env->htba);
    qemu_fprintf(f, "cansave: %d canrestore: %d otherwin: %d wstate: %d "
                 "cleanwin: %d cwp: %d\n",
                 env->cansave, env->canrestore, env->otherwin, env->wstate,
                 env->cleanwin, env->nwindows - 1 - env->cwp);
    qemu_fprintf(f, "fsr: " TARGET_FMT_lx " y: " TARGET_FMT_lx " fprs: %016x\n",
                 cpu_get_fsr(env), env->y, env->fprs);

#else
    qemu_fprintf(f, "psr: %08x (icc: ", cpu_get_psr(env));
    cpu_print_cc(f, cpu_get_psr(env));
    qemu_fprintf(f, " SPE: %c%c%c) wim: %08x\n", env->psrs ? 'S' : '-',
                 env->psrps ? 'P' : '-', env->psret ? 'E' : '-',
                 env->wim);
    qemu_fprintf(f, "fsr: " TARGET_FMT_lx " y: " TARGET_FMT_lx "\n",
                 cpu_get_fsr(env), env->y);
#endif
    qemu_fprintf(f, "\n");
}

static void sparc_cpu_set_pc(CPUState *cs, vaddr value)
{
    SPARCCPU *cpu = SPARC_CPU(cs);

    cpu->env.pc = value;
    cpu->env.npc = value + 4;
}

static vaddr sparc_cpu_get_pc(CPUState *cs)
{
    SPARCCPU *cpu = SPARC_CPU(cs);

    return cpu->env.pc;
}

static void sparc_cpu_synchronize_from_tb(CPUState *cs,
                                          const TranslationBlock *tb)
{
    SPARCCPU *cpu = SPARC_CPU(cs);

    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
    cpu->env.pc = tb->pc;
    cpu->env.npc = tb->cs_base;
}

void cpu_get_tb_cpu_state(CPUSPARCState *env, vaddr *pc,
                          uint64_t *cs_base, uint32_t *pflags)
{
    uint32_t flags;
    *pc = env->pc;
    *cs_base = env->npc;
    flags = cpu_mmu_index(env_cpu(env), false);
#ifndef CONFIG_USER_ONLY
    if (cpu_supervisor_mode(env)) {
        flags |= TB_FLAG_SUPER;
    }
#endif
#ifdef TARGET_SPARC64
#ifndef CONFIG_USER_ONLY
    if (cpu_hypervisor_mode(env)) {
        flags |= TB_FLAG_HYPER;
    }
#endif
    if (env->pstate & PS_AM) {
        flags |= TB_FLAG_AM_ENABLED;
    }
    if ((env->pstate & PS_PEF) && (env->fprs & FPRS_FEF)) {
        flags |= TB_FLAG_FPU_ENABLED;
    }
    flags |= env->asi << TB_FLAG_ASI_SHIFT;
#else
    if (env->psref) {
        flags |= TB_FLAG_FPU_ENABLED;
    }
#ifndef CONFIG_USER_ONLY
    if (env->fsr_qne) {
        flags |= TB_FLAG_FSR_QNE;
    }
#endif /* !CONFIG_USER_ONLY */
#endif /* TARGET_SPARC64 */
    *pflags = flags;
}

static void sparc_restore_state_to_opc(CPUState *cs,
                                       const TranslationBlock *tb,
                                       const uint64_t *data)
{
    CPUSPARCState *env = cpu_env(cs);
    target_ulong pc = data[0];
    target_ulong npc = data[1];

    env->pc = pc;
    if (npc == DYNAMIC_PC) {
        /* dynamic NPC: already stored */
    } else if (npc & JUMP_PC) {
        /* jump PC: use 'cond' and the jump targets of the translation */
        if (env->cond) {
            env->npc = npc & ~3;
        } else {
            env->npc = pc + 4;
        }
    } else {
        env->npc = npc;
    }
}

#ifndef CONFIG_USER_ONLY
static bool sparc_cpu_has_work(CPUState *cs)
{
    return (cs->interrupt_request & CPU_INTERRUPT_HARD) &&
           cpu_interrupts_enabled(cpu_env(cs));
}
#endif /* !CONFIG_USER_ONLY */

static int sparc_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    CPUSPARCState *env = cpu_env(cs);

#ifndef TARGET_SPARC64
    if ((env->mmuregs[0] & MMU_E) == 0) { /* MMU disabled */
        return MMU_PHYS_IDX;
    } else {
        return env->psrs;
    }
#else
    /* IMMU or DMMU disabled.  */
    if (ifetch
        ? (env->lsu & IMMU_E) == 0 || (env->pstate & PS_RED) != 0
        : (env->lsu & DMMU_E) == 0) {
        return MMU_PHYS_IDX;
    } else if (cpu_hypervisor_mode(env)) {
        return MMU_PHYS_IDX;
    } else if (env->tl > 0) {
        return MMU_NUCLEUS_IDX;
    } else if (cpu_supervisor_mode(env)) {
        return MMU_KERNEL_IDX;
    } else {
        return MMU_USER_IDX;
    }
#endif
}

static char *sparc_cpu_type_name(const char *cpu_model)
{
    char *name = g_strdup_printf(SPARC_CPU_TYPE_NAME("%s"), cpu_model);
    char *s = name;

    /* SPARC cpu model names happen to have whitespaces,
     * as type names shouldn't have spaces replace them with '-'
     */
    while ((s = strchr(s, ' '))) {
        *s = '-';
    }

    return name;
}

static ObjectClass *sparc_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    typename = sparc_cpu_type_name(cpu_model);

    /* Fix up legacy names with '+' in it */
    if (g_str_equal(typename, SPARC_CPU_TYPE_NAME("Sun-UltraSparc-IV+"))) {
        g_free(typename);
        typename = g_strdup(SPARC_CPU_TYPE_NAME("Sun-UltraSparc-IV-plus"));
    } else if (g_str_equal(typename, SPARC_CPU_TYPE_NAME("Sun-UltraSparc-IIIi+"))) {
        g_free(typename);
        typename = g_strdup(SPARC_CPU_TYPE_NAME("Sun-UltraSparc-IIIi-plus"));
    }

    oc = object_class_by_name(typename);
    g_free(typename);
    return oc;
}

static void sparc_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    SPARCCPUClass *scc = SPARC_CPU_GET_CLASS(dev);
    Error *local_err = NULL;
    CPUSPARCState *env = cpu_env(cs);

#if defined(CONFIG_USER_ONLY)
    /* We are emulating the kernel, which will trap and emulate float128. */
    env->def.features |= CPU_FEATURE_FLOAT128;
#endif

    env->version = env->def.iu_version;
    env->nwindows = env->def.nwindows;
#if !defined(TARGET_SPARC64)
    env->mmuregs[0] |= env->def.mmu_version;
    cpu_sparc_set_id(env, 0);
    env->mxccregs[7] |= env->def.mxcc_version;
#else
    env->mmu_version = env->def.mmu_version;
    env->maxtl = env->def.maxtl;
    env->version |= env->def.maxtl << 8;
    env->version |= env->def.nwindows - 1;
#endif

    /*
     * Prefer SNaN over QNaN, order B then A. It's OK to do this in realize
     * rather than reset, because fp_status is after 'end_reset_fields' in
     * the CPU state struct so it won't get zeroed on reset.
     */
    set_float_2nan_prop_rule(float_2nan_prop_s_ba, &env->fp_status);
    /* For fused-multiply add, prefer SNaN over QNaN, then C->B->A */
    set_float_3nan_prop_rule(float_3nan_prop_s_cba, &env->fp_status);
    /* For inf * 0 + NaN, return the input NaN */
    set_float_infzeronan_rule(float_infzeronan_dnan_never, &env->fp_status);
    /* Default NaN value: sign bit clear, all frac bits set */
    set_float_default_nan_pattern(0b01111111, &env->fp_status);

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);

    scc->parent_realize(dev, errp);
}

static void sparc_cpu_initfn(Object *obj)
{
    SPARCCPU *cpu = SPARC_CPU(obj);
    SPARCCPUClass *scc = SPARC_CPU_GET_CLASS(obj);
    CPUSPARCState *env = &cpu->env;

    if (scc->cpu_def) {
        env->def = *scc->cpu_def;
    }
}

static void sparc_get_nwindows(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    SPARCCPU *cpu = SPARC_CPU(obj);
    int64_t value = cpu->env.def.nwindows;

    visit_type_int(v, name, &value, errp);
}

static void sparc_set_nwindows(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    const int64_t min = MIN_NWINDOWS;
    const int64_t max = MAX_NWINDOWS;
    SPARCCPU *cpu = SPARC_CPU(obj);
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    if (value < min || value > max) {
        error_setg(errp, "Property %s.%s doesn't take value %" PRId64
                   " (minimum: %" PRId64 ", maximum: %" PRId64 ")",
                   object_get_typename(obj), name ? name : "null",
                   value, min, max);
        return;
    }
    cpu->env.def.nwindows = value;
}

static const PropertyInfo qdev_prop_nwindows = {
    .type  = "int",
    .description = "Number of register windows",
    .get   = sparc_get_nwindows,
    .set   = sparc_set_nwindows,
};

/* This must match feature_name[]. */
static const Property sparc_cpu_properties[] = {
    DEFINE_PROP_BIT("float128", SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_FLOAT128, false),
#ifdef TARGET_SPARC64
    DEFINE_PROP_BIT("cmt",      SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_CMT, false),
    DEFINE_PROP_BIT("gl",       SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_GL, false),
    DEFINE_PROP_BIT("hypv",     SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_HYPV, false),
    DEFINE_PROP_BIT("vis1",     SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_VIS1, false),
    DEFINE_PROP_BIT("vis2",     SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_VIS2, false),
    DEFINE_PROP_BIT("fmaf",     SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_FMAF, false),
    DEFINE_PROP_BIT("vis3",     SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_VIS3, false),
    DEFINE_PROP_BIT("ima",      SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_IMA, false),
    DEFINE_PROP_BIT("vis4",     SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_VIS4, false),
#else
    DEFINE_PROP_BIT("mul",      SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_MUL, false),
    DEFINE_PROP_BIT("div",      SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_DIV, false),
    DEFINE_PROP_BIT("fsmuld",   SPARCCPU, env.def.features,
                    CPU_FEATURE_BIT_FSMULD, false),
#endif
    DEFINE_PROP_UNSIGNED("iu-version", SPARCCPU, env.def.iu_version, 0,
                         qdev_prop_uint64, target_ulong),
    DEFINE_PROP_UINT32("fpu-version", SPARCCPU, env.def.fpu_version, 0),
    DEFINE_PROP_UINT32("mmu-version", SPARCCPU, env.def.mmu_version, 0),
    DEFINE_PROP("nwindows", SPARCCPU, env.def.nwindows,
                qdev_prop_nwindows, uint32_t),
};

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps sparc_sysemu_ops = {
    .has_work = sparc_cpu_has_work,
    .get_phys_page_debug = sparc_cpu_get_phys_page_debug,
    .legacy_vmsd = &vmstate_sparc_cpu,
};
#endif

#ifdef CONFIG_TCG
#include "accel/tcg/cpu-ops.h"

static const TCGCPUOps sparc_tcg_ops = {
    /*
     * From Oracle SPARC Architecture 2015:
     *
     *   Compatibility notes: The PSO memory model described in SPARC V8 and
     *   SPARC V9 compatibility architecture specifications was never
     *   implemented in a SPARC V9 implementation and is not included in the
     *   Oracle SPARC Architecture specification.
     *
     *   The RMO memory model described in the SPARC V9 specification was
     *   implemented in some non-Sun SPARC V9 implementations, but is not
     *   directly supported in Oracle SPARC Architecture 2015 implementations.
     *
     * Therefore always use TSO in QEMU.
     *
     * D.5 Specification of Partial Store Order (PSO)
     *   ... [loads] are followed by an implied MEMBAR #LoadLoad | #LoadStore.
     *
     * D.6 Specification of Total Store Order (TSO)
     *   ... PSO with the additional requirement that all [stores] are followed
     *   by an implied MEMBAR #StoreStore.
     */
    .guest_default_memory_order = TCG_MO_LD_LD | TCG_MO_LD_ST | TCG_MO_ST_ST,
    .mttcg_supported = true,

    .initialize = sparc_tcg_init,
    .translate_code = sparc_translate_code,
    .synchronize_from_tb = sparc_cpu_synchronize_from_tb,
    .restore_state_to_opc = sparc_restore_state_to_opc,
    .mmu_index = sparc_cpu_mmu_index,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = sparc_cpu_tlb_fill,
    .cpu_exec_interrupt = sparc_cpu_exec_interrupt,
    .cpu_exec_halt = sparc_cpu_has_work,
    .do_interrupt = sparc_cpu_do_interrupt,
    .do_transaction_failed = sparc_cpu_do_transaction_failed,
    .do_unaligned_access = sparc_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};
#endif /* CONFIG_TCG */

static void sparc_cpu_class_init(ObjectClass *oc, void *data)
{
    SPARCCPUClass *scc = SPARC_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, sparc_cpu_realizefn,
                                    &scc->parent_realize);
    device_class_set_props(dc, sparc_cpu_properties);

    resettable_class_set_parent_phases(rc, NULL, sparc_cpu_reset_hold, NULL,
                                       &scc->parent_phases);

    cc->class_by_name = sparc_cpu_class_by_name;
    cc->parse_features = sparc_cpu_parse_features;
    cc->dump_state = sparc_cpu_dump_state;
#if !defined(TARGET_SPARC64) && !defined(CONFIG_USER_ONLY)
    cc->memory_rw_debug = sparc_cpu_memory_rw_debug;
#endif
    cc->set_pc = sparc_cpu_set_pc;
    cc->get_pc = sparc_cpu_get_pc;
    cc->gdb_read_register = sparc_cpu_gdb_read_register;
    cc->gdb_write_register = sparc_cpu_gdb_write_register;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &sparc_sysemu_ops;
#endif
    cc->disas_set_info = cpu_sparc_disas_set_info;

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
    cc->gdb_num_core_regs = 86;
#else
    cc->gdb_num_core_regs = 72;
#endif
    cc->tcg_ops = &sparc_tcg_ops;
}

static const TypeInfo sparc_cpu_type_info = {
    .name = TYPE_SPARC_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(SPARCCPU),
    .instance_align = __alignof(SPARCCPU),
    .instance_init = sparc_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(SPARCCPUClass),
    .class_init = sparc_cpu_class_init,
};

static void sparc_cpu_cpudef_class_init(ObjectClass *oc, void *data)
{
    SPARCCPUClass *scc = SPARC_CPU_CLASS(oc);
    scc->cpu_def = data;
}

static void sparc_register_cpudef_type(const struct sparc_def_t *def)
{
    char *typename = sparc_cpu_type_name(def->name);
    TypeInfo ti = {
        .name = typename,
        .parent = TYPE_SPARC_CPU,
        .class_init = sparc_cpu_cpudef_class_init,
        .class_data = (void *)def,
    };

    type_register_static(&ti);
    g_free(typename);
}

static void sparc_cpu_register_types(void)
{
    int i;

    type_register_static(&sparc_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(sparc_defs); i++) {
        sparc_register_cpudef_type(&sparc_defs[i]);
    }
}

type_init(sparc_cpu_register_types)
