/*
 *  i386 CPUID helper functions
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2017 Google Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * cpuid
 */

#include "qemu/osdep.h"
#include "qemu/cpuid.h"
#include "host/cpuinfo.h"
#include "cpu.h"
#include "emulate/x86.h"
#include "vmx.h"
#include "system/hvf.h"
#include "hvf-i386.h"

static bool cached_xcr0;
static uint64_t supported_xcr0;

static void cache_host_xcr0(void)
{
    if (cached_xcr0) {
        return;
    }

    if (cpuinfo & CPUINFO_OSXSAVE) {
        uint64_t host_xcr0 = xgetbv_low(0);

        /* Only show xcr0 bits corresponding to usable features.  */
        supported_xcr0 = host_xcr0 & (XSTATE_FP_MASK |
                                      XSTATE_SSE_MASK | XSTATE_YMM_MASK |
                                      XSTATE_OPMASK_MASK | XSTATE_ZMM_Hi256_MASK |
                                      XSTATE_Hi16_ZMM_MASK);
        if ((supported_xcr0 & (XSTATE_FP_MASK | XSTATE_SSE_MASK)) !=
            (XSTATE_FP_MASK | XSTATE_SSE_MASK)) {
            supported_xcr0 = 0;
        }
    }

    cached_xcr0 = true;
}

uint32_t hvf_get_supported_cpuid(uint32_t func, uint32_t idx,
                                 int reg)
{
    uint64_t cap;
    uint32_t eax, ebx, ecx, edx;

    cache_host_xcr0();
    host_cpuid(func, idx, &eax, &ebx, &ecx, &edx);

    switch (func) {
    case 0:
        eax = eax < (uint32_t)0xd ? eax : (uint32_t)0xd;
        break;
    case 1:
        edx &= CPUID_FP87 | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC |
             CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC |
             CPUID_SEP | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV |
             CPUID_PAT | CPUID_PSE36 | CPUID_CLFLUSH | CPUID_MMX |
             CPUID_FXSR | CPUID_SSE | CPUID_SSE2 | CPUID_SS | CPUID_HT;
        ecx &= CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSSE3 |
             CPUID_EXT_FMA | CPUID_EXT_CX16 | CPUID_EXT_PCID |
             CPUID_EXT_SSE41 | CPUID_EXT_SSE42 | CPUID_EXT_MOVBE |
             CPUID_EXT_POPCNT | CPUID_EXT_AES | CPUID_EXT_X2APIC |
             (supported_xcr0 ? CPUID_EXT_XSAVE : 0) |
             CPUID_EXT_AVX | CPUID_EXT_F16C | CPUID_EXT_RDRAND;
        ecx |= CPUID_EXT_HYPERVISOR;
        break;
    case 6:
        eax = CPUID_6_EAX_ARAT;
        ebx = 0;
        ecx = 0;
        edx = 0;
        break;
    case 7:
        if (idx == 0) {
            ebx &= CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
                    CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 |
                    CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_BMI2 |
                    CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_RTM |
                    CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
                    CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_AVX512IFMA |
                    CPUID_7_0_EBX_AVX512F | CPUID_7_0_EBX_AVX512PF |
                    CPUID_7_0_EBX_AVX512ER | CPUID_7_0_EBX_AVX512CD |
                    CPUID_7_0_EBX_CLFLUSHOPT | CPUID_7_0_EBX_CLWB |
                    CPUID_7_0_EBX_AVX512DQ | CPUID_7_0_EBX_SHA_NI |
                    CPUID_7_0_EBX_AVX512BW | CPUID_7_0_EBX_AVX512VL |
                    CPUID_7_0_EBX_INVPCID;

            hv_vmx_read_capability(HV_VMX_CAP_PROCBASED2, &cap);
            if (!(cap & CPU_BASED2_INVPCID)) {
                ebx &= ~CPUID_7_0_EBX_INVPCID;
            }

            ecx &= CPUID_7_0_ECX_AVX512_VBMI | CPUID_7_0_ECX_AVX512_VPOPCNTDQ |
                   CPUID_7_0_ECX_RDPID;
            edx &= CPUID_7_0_EDX_AVX512_4VNNIW | CPUID_7_0_EDX_AVX512_4FMAPS;
        } else {
            ebx = 0;
            ecx = 0;
            edx = 0;
        }
        eax = 0;
        break;
    case 0xD:
        if (!supported_xcr0 || idx >= 63 ||
            (idx > 1 && !(supported_xcr0 & (UINT64_C(1) << idx)))) {
            eax = ebx = ecx = edx = 0;
            break;
        }

        if (idx == 0) {
            eax = supported_xcr0;
        } else if (idx == 1) {
            hv_vmx_read_capability(HV_VMX_CAP_PROCBASED2, &cap);
            eax &= CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XGETBV1;
            if (!(cap & CPU_BASED2_XSAVES_XRSTORS)) {
                eax &= ~CPUID_XSAVE_XSAVES;
            }
        }
        break;
    case 0x80000001:
        /* LM only if HVF in 64-bit mode */
        edx &= CPUID_FP87 | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC |
                CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC |
                CPUID_EXT2_SYSCALL | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV |
                CPUID_PAT | CPUID_PSE36 | CPUID_EXT2_MMXEXT | CPUID_MMX |
                CPUID_FXSR | CPUID_EXT2_FXSR | CPUID_EXT2_PDPE1GB | CPUID_EXT2_3DNOWEXT |
                CPUID_EXT2_3DNOW | CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX;
        hv_vmx_read_capability(HV_VMX_CAP_PROCBASED2, &cap);
        if (!(cap2ctrl(cap, CPU_BASED2_RDTSCP) & CPU_BASED2_RDTSCP)) {
            edx &= ~CPUID_EXT2_RDTSCP;
        }
        hv_vmx_read_capability(HV_VMX_CAP_PROCBASED, &cap);
        if (!(cap2ctrl(cap, CPU_BASED_TSC_OFFSET) & CPU_BASED_TSC_OFFSET)) {
            edx &= ~CPUID_EXT2_RDTSCP;
        }
        ecx &= CPUID_EXT3_LAHF_LM | CPUID_EXT3_CMP_LEG | CPUID_EXT3_CR8LEG |
                CPUID_EXT3_ABM | CPUID_EXT3_SSE4A | CPUID_EXT3_MISALIGNSSE |
                CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_OSVW | CPUID_EXT3_XOP |
                CPUID_EXT3_FMA4 | CPUID_EXT3_TBM;
        break;
    case 0x80000007:
        edx &= CPUID_APM_INVTSC;
        eax = ebx = ecx = 0;
        break;
    default:
        return 0;
    }

    switch (reg) {
    case R_EAX:
        return eax;
    case R_EBX:
        return ebx;
    case R_ECX:
        return ecx;
    case R_EDX:
        return edx;
    default:
        return 0;
    }
}
