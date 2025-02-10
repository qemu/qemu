/*
 * i386 CPU internal definitions to be shared between cpu.c and cpu-system.c
 *
 *  Copyright (c) 2003 Fabrice Bellard
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

#ifndef I386_CPU_INTERNAL_H
#define I386_CPU_INTERNAL_H

typedef enum FeatureWordType {
   CPUID_FEATURE_WORD,
   MSR_FEATURE_WORD,
} FeatureWordType;

typedef struct FeatureWordInfo {
    FeatureWordType type;
    /* feature flags names are taken from "Intel Processor Identification and
     * the CPUID Instruction" and AMD's "CPUID Specification".
     * In cases of disagreement between feature naming conventions,
     * aliases may be added.
     */
    const char *feat_names[64];
    union {
        /* If type==CPUID_FEATURE_WORD */
        struct {
            uint32_t eax;   /* Input EAX for CPUID */
            bool needs_ecx; /* CPUID instruction uses ECX as input */
            uint32_t ecx;   /* Input ECX value for CPUID */
            int reg;        /* output register (R_* constant) */
        } cpuid;
        /* If type==MSR_FEATURE_WORD */
        struct {
            uint32_t index;
        } msr;
    };
    uint64_t tcg_features; /* Feature flags supported by TCG */
    uint64_t unmigratable_flags; /* Feature flags known to be unmigratable */
    uint64_t migratable_flags; /* Feature flags known to be migratable */
    /* Features that shouldn't be auto-enabled by "-cpu host" */
    uint64_t no_autoenable_flags;
} FeatureWordInfo;

extern FeatureWordInfo feature_word_info[];

void x86_cpu_expand_features(X86CPU *cpu, Error **errp);

#ifndef CONFIG_USER_ONLY
GuestPanicInformation *x86_cpu_get_crash_info(CPUState *cs);
void x86_cpu_get_crash_info_qom(Object *obj, Visitor *v,
                                const char *name, void *opaque, Error **errp);

void x86_cpu_apic_create(X86CPU *cpu, Error **errp);
void x86_cpu_apic_realize(X86CPU *cpu, Error **errp);
void x86_cpu_machine_reset_cb(void *opaque);
#endif /* !CONFIG_USER_ONLY */

#endif /* I386_CPU_INTERNAL_H */
