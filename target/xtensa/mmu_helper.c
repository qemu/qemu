/*
 * Copyright (c) 2011 - 2019, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/qemu-print.h"
#include "qemu/units.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

#define XTENSA_MPU_SEGMENT_MASK 0x0000001f
#define XTENSA_MPU_ACC_RIGHTS_MASK 0x00000f00
#define XTENSA_MPU_ACC_RIGHTS_SHIFT 8
#define XTENSA_MPU_MEM_TYPE_MASK 0x001ff000
#define XTENSA_MPU_MEM_TYPE_SHIFT 12
#define XTENSA_MPU_ATTR_MASK 0x001fff00

#define XTENSA_MPU_PROBE_B 0x40000000
#define XTENSA_MPU_PROBE_V 0x80000000

#define XTENSA_MPU_SYSTEM_TYPE_DEVICE 0x0001
#define XTENSA_MPU_SYSTEM_TYPE_NC     0x0002
#define XTENSA_MPU_SYSTEM_TYPE_C      0x0003
#define XTENSA_MPU_SYSTEM_TYPE_MASK   0x0003

#define XTENSA_MPU_TYPE_SYS_C     0x0010
#define XTENSA_MPU_TYPE_SYS_W     0x0020
#define XTENSA_MPU_TYPE_SYS_R     0x0040
#define XTENSA_MPU_TYPE_CPU_C     0x0100
#define XTENSA_MPU_TYPE_CPU_W     0x0200
#define XTENSA_MPU_TYPE_CPU_R     0x0400
#define XTENSA_MPU_TYPE_CPU_CACHE 0x0800
#define XTENSA_MPU_TYPE_B         0x1000
#define XTENSA_MPU_TYPE_INT       0x2000

void HELPER(itlb_hit_test)(CPUXtensaState *env, uint32_t vaddr)
{
    /*
     * Attempt the memory load; we don't care about the result but
     * only the side-effects (ie any MMU or other exception)
     */
    cpu_ldub_code_ra(env, vaddr, GETPC());
}

void HELPER(wsr_rasid)(CPUXtensaState *env, uint32_t v)
{
    v = (v & 0xffffff00) | 0x1;
    if (v != env->sregs[RASID]) {
        env->sregs[RASID] = v;
        tlb_flush(env_cpu(env));
    }
}

static uint32_t get_page_size(const CPUXtensaState *env,
                              bool dtlb, uint32_t way)
{
    uint32_t tlbcfg = env->sregs[dtlb ? DTLBCFG : ITLBCFG];

    switch (way) {
    case 4:
        return (tlbcfg >> 16) & 0x3;

    case 5:
        return (tlbcfg >> 20) & 0x1;

    case 6:
        return (tlbcfg >> 24) & 0x1;

    default:
        return 0;
    }
}

/*!
 * Get bit mask for the virtual address bits translated by the TLB way
 */
static uint32_t xtensa_tlb_get_addr_mask(const CPUXtensaState *env,
                                         bool dtlb, uint32_t way)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        bool varway56 = dtlb ?
            env->config->dtlb.varway56 :
            env->config->itlb.varway56;

        switch (way) {
        case 4:
            return 0xfff00000 << get_page_size(env, dtlb, way) * 2;

        case 5:
            if (varway56) {
                return 0xf8000000 << get_page_size(env, dtlb, way);
            } else {
                return 0xf8000000;
            }

        case 6:
            if (varway56) {
                return 0xf0000000 << (1 - get_page_size(env, dtlb, way));
            } else {
                return 0xf0000000;
            }

        default:
            return 0xfffff000;
        }
    } else {
        return REGION_PAGE_MASK;
    }
}

/*!
 * Get bit mask for the 'VPN without index' field.
 * See ISA, 4.6.5.6, data format for RxTLB0
 */
static uint32_t get_vpn_mask(const CPUXtensaState *env, bool dtlb, uint32_t way)
{
    if (way < 4) {
        bool is32 = (dtlb ?
                env->config->dtlb.nrefillentries :
                env->config->itlb.nrefillentries) == 32;
        return is32 ? 0xffff8000 : 0xffffc000;
    } else if (way == 4) {
        return xtensa_tlb_get_addr_mask(env, dtlb, way) << 2;
    } else if (way <= 6) {
        uint32_t mask = xtensa_tlb_get_addr_mask(env, dtlb, way);
        bool varway56 = dtlb ?
            env->config->dtlb.varway56 :
            env->config->itlb.varway56;

        if (varway56) {
            return mask << (way == 5 ? 2 : 3);
        } else {
            return mask << 1;
        }
    } else {
        return 0xfffff000;
    }
}

/*!
 * Split virtual address into VPN (with index) and entry index
 * for the given TLB way
 */
static void split_tlb_entry_spec_way(const CPUXtensaState *env, uint32_t v,
                                     bool dtlb, uint32_t *vpn,
                                     uint32_t wi, uint32_t *ei)
{
    bool varway56 = dtlb ?
        env->config->dtlb.varway56 :
        env->config->itlb.varway56;

    if (!dtlb) {
        wi &= 7;
    }

    if (wi < 4) {
        bool is32 = (dtlb ?
                env->config->dtlb.nrefillentries :
                env->config->itlb.nrefillentries) == 32;
        *ei = (v >> 12) & (is32 ? 0x7 : 0x3);
    } else {
        switch (wi) {
        case 4:
            {
                uint32_t eibase = 20 + get_page_size(env, dtlb, wi) * 2;
                *ei = (v >> eibase) & 0x3;
            }
            break;

        case 5:
            if (varway56) {
                uint32_t eibase = 27 + get_page_size(env, dtlb, wi);
                *ei = (v >> eibase) & 0x3;
            } else {
                *ei = (v >> 27) & 0x1;
            }
            break;

        case 6:
            if (varway56) {
                uint32_t eibase = 29 - get_page_size(env, dtlb, wi);
                *ei = (v >> eibase) & 0x7;
            } else {
                *ei = (v >> 28) & 0x1;
            }
            break;

        default:
            *ei = 0;
            break;
        }
    }
    *vpn = v & xtensa_tlb_get_addr_mask(env, dtlb, wi);
}

/*!
 * Split TLB address into TLB way, entry index and VPN (with index).
 * See ISA, 4.6.5.5 - 4.6.5.8 for the TLB addressing format
 */
static void split_tlb_entry_spec(CPUXtensaState *env, uint32_t v, bool dtlb,
        uint32_t *vpn, uint32_t *wi, uint32_t *ei)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        *wi = v & (dtlb ? 0xf : 0x7);
        split_tlb_entry_spec_way(env, v, dtlb, vpn, *wi, ei);
    } else {
        *vpn = v & REGION_PAGE_MASK;
        *wi = 0;
        *ei = (v >> 29) & 0x7;
    }
}

static xtensa_tlb_entry *xtensa_tlb_get_entry(CPUXtensaState *env, bool dtlb,
                                              unsigned wi, unsigned ei)
{
    return dtlb ?
        env->dtlb[wi] + ei :
        env->itlb[wi] + ei;
}

static xtensa_tlb_entry *get_tlb_entry(CPUXtensaState *env,
        uint32_t v, bool dtlb, uint32_t *pwi)
{
    uint32_t vpn;
    uint32_t wi;
    uint32_t ei;

    split_tlb_entry_spec(env, v, dtlb, &vpn, &wi, &ei);
    if (pwi) {
        *pwi = wi;
    }
    return xtensa_tlb_get_entry(env, dtlb, wi, ei);
}

static void xtensa_tlb_set_entry_mmu(const CPUXtensaState *env,
                                     xtensa_tlb_entry *entry, bool dtlb,
                                     unsigned wi, unsigned ei, uint32_t vpn,
                                     uint32_t pte)
{
    entry->vaddr = vpn;
    entry->paddr = pte & xtensa_tlb_get_addr_mask(env, dtlb, wi);
    entry->asid = (env->sregs[RASID] >> ((pte >> 1) & 0x18)) & 0xff;
    entry->attr = pte & 0xf;
}

static void xtensa_tlb_set_entry(CPUXtensaState *env, bool dtlb,
                                 unsigned wi, unsigned ei,
                                 uint32_t vpn, uint32_t pte)
{
    CPUState *cs = env_cpu(env);
    xtensa_tlb_entry *entry = xtensa_tlb_get_entry(env, dtlb, wi, ei);

    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        if (entry->variable) {
            if (entry->asid) {
                tlb_flush_page(cs, entry->vaddr);
            }
            xtensa_tlb_set_entry_mmu(env, entry, dtlb, wi, ei, vpn, pte);
            tlb_flush_page(cs, entry->vaddr);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s %d, %d, %d trying to set immutable entry\n",
                          __func__, dtlb, wi, ei);
        }
    } else {
        tlb_flush_page(cs, entry->vaddr);
        if (xtensa_option_enabled(env->config,
                    XTENSA_OPTION_REGION_TRANSLATION)) {
            entry->paddr = pte & REGION_PAGE_MASK;
        }
        entry->attr = pte & 0xf;
    }
}

hwaddr xtensa_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    uint32_t paddr;
    uint32_t page_size;
    unsigned access;

    if (xtensa_get_physical_addr(&cpu->env, false, addr, 0, 0,
                &paddr, &page_size, &access) == 0) {
        return paddr;
    }
    if (xtensa_get_physical_addr(&cpu->env, false, addr, 2, 0,
                &paddr, &page_size, &access) == 0) {
        return paddr;
    }
    return ~0;
}

static void reset_tlb_mmu_all_ways(CPUXtensaState *env,
                                   const xtensa_tlb *tlb,
                                   xtensa_tlb_entry entry[][MAX_TLB_WAY_SIZE])
{
    unsigned wi, ei;

    for (wi = 0; wi < tlb->nways; ++wi) {
        for (ei = 0; ei < tlb->way_size[wi]; ++ei) {
            entry[wi][ei].asid = 0;
            entry[wi][ei].variable = true;
        }
    }
}

static void reset_tlb_mmu_ways56(CPUXtensaState *env,
                                 const xtensa_tlb *tlb,
                                 xtensa_tlb_entry entry[][MAX_TLB_WAY_SIZE])
{
    if (!tlb->varway56) {
        static const xtensa_tlb_entry way5[] = {
            {
                .vaddr = 0xd0000000,
                .paddr = 0,
                .asid = 1,
                .attr = 7,
                .variable = false,
            }, {
                .vaddr = 0xd8000000,
                .paddr = 0,
                .asid = 1,
                .attr = 3,
                .variable = false,
            }
        };
        static const xtensa_tlb_entry way6[] = {
            {
                .vaddr = 0xe0000000,
                .paddr = 0xf0000000,
                .asid = 1,
                .attr = 7,
                .variable = false,
            }, {
                .vaddr = 0xf0000000,
                .paddr = 0xf0000000,
                .asid = 1,
                .attr = 3,
                .variable = false,
            }
        };
        memcpy(entry[5], way5, sizeof(way5));
        memcpy(entry[6], way6, sizeof(way6));
    } else {
        uint32_t ei;
        for (ei = 0; ei < 8; ++ei) {
            entry[6][ei].vaddr = ei << 29;
            entry[6][ei].paddr = ei << 29;
            entry[6][ei].asid = 1;
            entry[6][ei].attr = 3;
        }
    }
}

static void reset_tlb_region_way0(CPUXtensaState *env,
                                  xtensa_tlb_entry entry[][MAX_TLB_WAY_SIZE])
{
    unsigned ei;

    for (ei = 0; ei < 8; ++ei) {
        entry[0][ei].vaddr = ei << 29;
        entry[0][ei].paddr = ei << 29;
        entry[0][ei].asid = 1;
        entry[0][ei].attr = 2;
        entry[0][ei].variable = true;
    }
}

void reset_mmu(CPUXtensaState *env)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        env->sregs[RASID] = 0x04030201;
        env->sregs[ITLBCFG] = 0;
        env->sregs[DTLBCFG] = 0;
        env->autorefill_idx = 0;
        reset_tlb_mmu_all_ways(env, &env->config->itlb, env->itlb);
        reset_tlb_mmu_all_ways(env, &env->config->dtlb, env->dtlb);
        reset_tlb_mmu_ways56(env, &env->config->itlb, env->itlb);
        reset_tlb_mmu_ways56(env, &env->config->dtlb, env->dtlb);
    } else if (xtensa_option_enabled(env->config, XTENSA_OPTION_MPU)) {
        unsigned i;

        env->sregs[MPUENB] = 0;
        env->sregs[MPUCFG] = env->config->n_mpu_fg_segments;
        env->sregs[CACHEADRDIS] = 0;
        assert(env->config->n_mpu_bg_segments > 0 &&
               env->config->mpu_bg[0].vaddr == 0);
        for (i = 1; i < env->config->n_mpu_bg_segments; ++i) {
            assert(env->config->mpu_bg[i].vaddr >=
                   env->config->mpu_bg[i - 1].vaddr);
        }
    } else {
        env->sregs[CACHEATTR] = 0x22222222;
        reset_tlb_region_way0(env, env->itlb);
        reset_tlb_region_way0(env, env->dtlb);
    }
}

static unsigned get_ring(const CPUXtensaState *env, uint8_t asid)
{
    unsigned i;
    for (i = 0; i < 4; ++i) {
        if (((env->sregs[RASID] >> i * 8) & 0xff) == asid) {
            return i;
        }
    }
    return 0xff;
}

/*!
 * Lookup xtensa TLB for the given virtual address.
 * See ISA, 4.6.2.2
 *
 * \param pwi: [out] way index
 * \param pei: [out] entry index
 * \param pring: [out] access ring
 * \return 0 if ok, exception cause code otherwise
 */
static int xtensa_tlb_lookup(const CPUXtensaState *env,
                             uint32_t addr, bool dtlb,
                             uint32_t *pwi, uint32_t *pei, uint8_t *pring)
{
    const xtensa_tlb *tlb = dtlb ?
        &env->config->dtlb : &env->config->itlb;
    const xtensa_tlb_entry (*entry)[MAX_TLB_WAY_SIZE] = dtlb ?
        env->dtlb : env->itlb;

    int nhits = 0;
    unsigned wi;

    for (wi = 0; wi < tlb->nways; ++wi) {
        uint32_t vpn;
        uint32_t ei;
        split_tlb_entry_spec_way(env, addr, dtlb, &vpn, wi, &ei);
        if (entry[wi][ei].vaddr == vpn && entry[wi][ei].asid) {
            unsigned ring = get_ring(env, entry[wi][ei].asid);
            if (ring < 4) {
                if (++nhits > 1) {
                    return dtlb ?
                        LOAD_STORE_TLB_MULTI_HIT_CAUSE :
                        INST_TLB_MULTI_HIT_CAUSE;
                }
                *pwi = wi;
                *pei = ei;
                *pring = ring;
            }
        }
    }
    return nhits ? 0 :
        (dtlb ? LOAD_STORE_TLB_MISS_CAUSE : INST_TLB_MISS_CAUSE);
}

uint32_t HELPER(rtlb0)(CPUXtensaState *env, uint32_t v, uint32_t dtlb)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        uint32_t wi;
        const xtensa_tlb_entry *entry = get_tlb_entry(env, v, dtlb, &wi);
        return (entry->vaddr & get_vpn_mask(env, dtlb, wi)) | entry->asid;
    } else {
        return v & REGION_PAGE_MASK;
    }
}

uint32_t HELPER(rtlb1)(CPUXtensaState *env, uint32_t v, uint32_t dtlb)
{
    const xtensa_tlb_entry *entry = get_tlb_entry(env, v, dtlb, NULL);
    return entry->paddr | entry->attr;
}

void HELPER(itlb)(CPUXtensaState *env, uint32_t v, uint32_t dtlb)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        uint32_t wi;
        xtensa_tlb_entry *entry = get_tlb_entry(env, v, dtlb, &wi);
        if (entry->variable && entry->asid) {
            tlb_flush_page(env_cpu(env), entry->vaddr);
            entry->asid = 0;
        }
    }
}

uint32_t HELPER(ptlb)(CPUXtensaState *env, uint32_t v, uint32_t dtlb)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        uint32_t wi;
        uint32_t ei;
        uint8_t ring;
        int res = xtensa_tlb_lookup(env, v, dtlb, &wi, &ei, &ring);

        switch (res) {
        case 0:
            if (ring >= xtensa_get_ring(env)) {
                return (v & 0xfffff000) | wi | (dtlb ? 0x10 : 0x8);
            }
            break;

        case INST_TLB_MULTI_HIT_CAUSE:
        case LOAD_STORE_TLB_MULTI_HIT_CAUSE:
            HELPER(exception_cause_vaddr)(env, env->pc, res, v);
            break;
        }
        return 0;
    } else {
        return (v & REGION_PAGE_MASK) | 0x1;
    }
}

void HELPER(wtlb)(CPUXtensaState *env, uint32_t p, uint32_t v, uint32_t dtlb)
{
    uint32_t vpn;
    uint32_t wi;
    uint32_t ei;
    split_tlb_entry_spec(env, v, dtlb, &vpn, &wi, &ei);
    xtensa_tlb_set_entry(env, dtlb, wi, ei, vpn, p);
}

/*!
 * Convert MMU ATTR to PAGE_{READ,WRITE,EXEC} mask.
 * See ISA, 4.6.5.10
 */
static unsigned mmu_attr_to_access(uint32_t attr)
{
    unsigned access = 0;

    if (attr < 12) {
        access |= PAGE_READ;
        if (attr & 0x1) {
            access |= PAGE_EXEC;
        }
        if (attr & 0x2) {
            access |= PAGE_WRITE;
        }

        switch (attr & 0xc) {
        case 0:
            access |= PAGE_CACHE_BYPASS;
            break;

        case 4:
            access |= PAGE_CACHE_WB;
            break;

        case 8:
            access |= PAGE_CACHE_WT;
            break;
        }
    } else if (attr == 13) {
        access |= PAGE_READ | PAGE_WRITE | PAGE_CACHE_ISOLATE;
    }
    return access;
}

/*!
 * Convert region protection ATTR to PAGE_{READ,WRITE,EXEC} mask.
 * See ISA, 4.6.3.3
 */
static unsigned region_attr_to_access(uint32_t attr)
{
    static const unsigned access[16] = {
         [0] = PAGE_READ | PAGE_WRITE             | PAGE_CACHE_WT,
         [1] = PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_CACHE_WT,
         [2] = PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_CACHE_BYPASS,
         [3] =                          PAGE_EXEC | PAGE_CACHE_WB,
         [4] = PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_CACHE_WB,
         [5] = PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_CACHE_WB,
        [14] = PAGE_READ | PAGE_WRITE             | PAGE_CACHE_ISOLATE,
    };

    return access[attr & 0xf];
}

/*!
 * Convert cacheattr to PAGE_{READ,WRITE,EXEC} mask.
 * See ISA, A.2.14 The Cache Attribute Register
 */
static unsigned cacheattr_attr_to_access(uint32_t attr)
{
    static const unsigned access[16] = {
         [0] = PAGE_READ | PAGE_WRITE             | PAGE_CACHE_WT,
         [1] = PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_CACHE_WT,
         [2] = PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_CACHE_BYPASS,
         [3] =                          PAGE_EXEC | PAGE_CACHE_WB,
         [4] = PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_CACHE_WB,
        [14] = PAGE_READ | PAGE_WRITE             | PAGE_CACHE_ISOLATE,
    };

    return access[attr & 0xf];
}

struct attr_pattern {
    uint32_t mask;
    uint32_t value;
};

static int attr_pattern_match(uint32_t attr,
                              const struct attr_pattern *pattern,
                              size_t n)
{
    size_t i;

    for (i = 0; i < n; ++i) {
        if ((attr & pattern[i].mask) == pattern[i].value) {
            return 1;
        }
    }
    return 0;
}

static unsigned mpu_attr_to_cpu_cache(uint32_t attr)
{
    static const struct attr_pattern cpu_c[] = {
        { .mask = 0x18f, .value = 0x089 },
        { .mask = 0x188, .value = 0x080 },
        { .mask = 0x180, .value = 0x180 },
    };

    unsigned type = 0;

    if (attr_pattern_match(attr, cpu_c, ARRAY_SIZE(cpu_c))) {
        type |= XTENSA_MPU_TYPE_CPU_CACHE;
        if (attr & 0x10) {
            type |= XTENSA_MPU_TYPE_CPU_C;
        }
        if (attr & 0x20) {
            type |= XTENSA_MPU_TYPE_CPU_W;
        }
        if (attr & 0x40) {
            type |= XTENSA_MPU_TYPE_CPU_R;
        }
    }
    return type;
}

static unsigned mpu_attr_to_type(uint32_t attr)
{
    static const struct attr_pattern device_type[] = {
        { .mask = 0x1f6, .value = 0x000 },
        { .mask = 0x1f6, .value = 0x006 },
    };
    static const struct attr_pattern sys_nc_type[] = {
        { .mask = 0x1fe, .value = 0x018 },
        { .mask = 0x1fe, .value = 0x01e },
        { .mask = 0x18f, .value = 0x089 },
    };
    static const struct attr_pattern sys_c_type[] = {
        { .mask = 0x1f8, .value = 0x010 },
        { .mask = 0x188, .value = 0x080 },
        { .mask = 0x1f0, .value = 0x030 },
        { .mask = 0x180, .value = 0x180 },
    };
    static const struct attr_pattern b[] = {
        { .mask = 0x1f7, .value = 0x001 },
        { .mask = 0x1f7, .value = 0x007 },
        { .mask = 0x1ff, .value = 0x019 },
        { .mask = 0x1ff, .value = 0x01f },
    };

    unsigned type = 0;

    attr = (attr & XTENSA_MPU_MEM_TYPE_MASK) >> XTENSA_MPU_MEM_TYPE_SHIFT;
    if (attr_pattern_match(attr, device_type, ARRAY_SIZE(device_type))) {
        type |= XTENSA_MPU_SYSTEM_TYPE_DEVICE;
        if (attr & 0x80) {
            type |= XTENSA_MPU_TYPE_INT;
        }
    }
    if (attr_pattern_match(attr, sys_nc_type, ARRAY_SIZE(sys_nc_type))) {
        type |= XTENSA_MPU_SYSTEM_TYPE_NC;
    }
    if (attr_pattern_match(attr, sys_c_type, ARRAY_SIZE(sys_c_type))) {
        type |= XTENSA_MPU_SYSTEM_TYPE_C;
        if (attr & 0x1) {
            type |= XTENSA_MPU_TYPE_SYS_C;
        }
        if (attr & 0x2) {
            type |= XTENSA_MPU_TYPE_SYS_W;
        }
        if (attr & 0x4) {
            type |= XTENSA_MPU_TYPE_SYS_R;
        }
    }
    if (attr_pattern_match(attr, b, ARRAY_SIZE(b))) {
        type |= XTENSA_MPU_TYPE_B;
    }
    type |= mpu_attr_to_cpu_cache(attr);

    return type;
}

static unsigned mpu_attr_to_access(uint32_t attr, unsigned ring)
{
    static const unsigned access[2][16] = {
        [0] = {
             [4] = PAGE_READ,
             [5] = PAGE_READ              | PAGE_EXEC,
             [6] = PAGE_READ | PAGE_WRITE,
             [7] = PAGE_READ | PAGE_WRITE | PAGE_EXEC,
             [8] =             PAGE_WRITE,
             [9] = PAGE_READ | PAGE_WRITE,
            [10] = PAGE_READ | PAGE_WRITE,
            [11] = PAGE_READ | PAGE_WRITE | PAGE_EXEC,
            [12] = PAGE_READ,
            [13] = PAGE_READ              | PAGE_EXEC,
            [14] = PAGE_READ | PAGE_WRITE,
            [15] = PAGE_READ | PAGE_WRITE | PAGE_EXEC,
        },
        [1] = {
             [8] =             PAGE_WRITE,
             [9] = PAGE_READ | PAGE_WRITE | PAGE_EXEC,
            [10] = PAGE_READ,
            [11] = PAGE_READ              | PAGE_EXEC,
            [12] = PAGE_READ,
            [13] = PAGE_READ              | PAGE_EXEC,
            [14] = PAGE_READ | PAGE_WRITE,
            [15] = PAGE_READ | PAGE_WRITE | PAGE_EXEC,
        },
    };
    unsigned rv;
    unsigned type;

    type = mpu_attr_to_cpu_cache(attr);
    rv = access[ring != 0][(attr & XTENSA_MPU_ACC_RIGHTS_MASK) >>
        XTENSA_MPU_ACC_RIGHTS_SHIFT];

    if (type & XTENSA_MPU_TYPE_CPU_CACHE) {
        rv |= (type & XTENSA_MPU_TYPE_CPU_C) ? PAGE_CACHE_WB : PAGE_CACHE_WT;
    } else {
        rv |= PAGE_CACHE_BYPASS;
    }
    return rv;
}

static bool is_access_granted(unsigned access, int is_write)
{
    switch (is_write) {
    case 0:
        return access & PAGE_READ;

    case 1:
        return access & PAGE_WRITE;

    case 2:
        return access & PAGE_EXEC;

    default:
        return 0;
    }
}

static bool get_pte(CPUXtensaState *env, uint32_t vaddr, uint32_t *pte);

static int get_physical_addr_mmu(CPUXtensaState *env, bool update_tlb,
                                 uint32_t vaddr, int is_write, int mmu_idx,
                                 uint32_t *paddr, uint32_t *page_size,
                                 unsigned *access, bool may_lookup_pt)
{
    bool dtlb = is_write != 2;
    uint32_t wi;
    uint32_t ei;
    uint8_t ring;
    uint32_t vpn;
    uint32_t pte;
    const xtensa_tlb_entry *entry = NULL;
    xtensa_tlb_entry tmp_entry;
    int ret = xtensa_tlb_lookup(env, vaddr, dtlb, &wi, &ei, &ring);

    if ((ret == INST_TLB_MISS_CAUSE || ret == LOAD_STORE_TLB_MISS_CAUSE) &&
        may_lookup_pt && get_pte(env, vaddr, &pte)) {
        ring = (pte >> 4) & 0x3;
        wi = 0;
        split_tlb_entry_spec_way(env, vaddr, dtlb, &vpn, wi, &ei);

        if (update_tlb) {
            wi = ++env->autorefill_idx & 0x3;
            xtensa_tlb_set_entry(env, dtlb, wi, ei, vpn, pte);
            env->sregs[EXCVADDR] = vaddr;
            qemu_log_mask(CPU_LOG_MMU, "%s: autorefill(%08x): %08x -> %08x\n",
                          __func__, vaddr, vpn, pte);
        } else {
            xtensa_tlb_set_entry_mmu(env, &tmp_entry, dtlb, wi, ei, vpn, pte);
            entry = &tmp_entry;
        }
        ret = 0;
    }
    if (ret != 0) {
        return ret;
    }

    if (entry == NULL) {
        entry = xtensa_tlb_get_entry(env, dtlb, wi, ei);
    }

    if (ring < mmu_idx) {
        return dtlb ?
            LOAD_STORE_PRIVILEGE_CAUSE :
            INST_FETCH_PRIVILEGE_CAUSE;
    }

    *access = mmu_attr_to_access(entry->attr) &
        ~(dtlb ? PAGE_EXEC : PAGE_READ | PAGE_WRITE);
    if (!is_access_granted(*access, is_write)) {
        return dtlb ?
            (is_write ?
             STORE_PROHIBITED_CAUSE :
             LOAD_PROHIBITED_CAUSE) :
            INST_FETCH_PROHIBITED_CAUSE;
    }

    *paddr = entry->paddr | (vaddr & ~xtensa_tlb_get_addr_mask(env, dtlb, wi));
    *page_size = ~xtensa_tlb_get_addr_mask(env, dtlb, wi) + 1;

    return 0;
}

static bool get_pte(CPUXtensaState *env, uint32_t vaddr, uint32_t *pte)
{
    CPUState *cs = env_cpu(env);
    uint32_t paddr;
    uint32_t page_size;
    unsigned access;
    uint32_t pt_vaddr =
        (env->sregs[PTEVADDR] | (vaddr >> 10)) & 0xfffffffc;
    int ret = get_physical_addr_mmu(env, false, pt_vaddr, 0, 0,
                                    &paddr, &page_size, &access, false);

    if (ret == 0) {
        qemu_log_mask(CPU_LOG_MMU,
                      "%s: autorefill(%08x): PTE va = %08x, pa = %08x\n",
                      __func__, vaddr, pt_vaddr, paddr);
    } else {
        qemu_log_mask(CPU_LOG_MMU,
                      "%s: autorefill(%08x): PTE va = %08x, failed (%d)\n",
                      __func__, vaddr, pt_vaddr, ret);
    }

    if (ret == 0) {
        MemTxResult result;

        *pte = address_space_ldl(cs->as, paddr, MEMTXATTRS_UNSPECIFIED,
                                 &result);
        if (result != MEMTX_OK) {
            qemu_log_mask(CPU_LOG_MMU,
                          "%s: couldn't load PTE: transaction failed (%u)\n",
                          __func__, (unsigned)result);
            ret = 1;
        }
    }
    return ret == 0;
}

static int get_physical_addr_region(CPUXtensaState *env,
                                    uint32_t vaddr, int is_write, int mmu_idx,
                                    uint32_t *paddr, uint32_t *page_size,
                                    unsigned *access)
{
    bool dtlb = is_write != 2;
    uint32_t wi = 0;
    uint32_t ei = (vaddr >> 29) & 0x7;
    const xtensa_tlb_entry *entry =
        xtensa_tlb_get_entry(env, dtlb, wi, ei);

    *access = region_attr_to_access(entry->attr);
    if (!is_access_granted(*access, is_write)) {
        return dtlb ?
            (is_write ?
             STORE_PROHIBITED_CAUSE :
             LOAD_PROHIBITED_CAUSE) :
            INST_FETCH_PROHIBITED_CAUSE;
    }

    *paddr = entry->paddr | (vaddr & ~REGION_PAGE_MASK);
    *page_size = ~REGION_PAGE_MASK + 1;

    return 0;
}

static int xtensa_mpu_lookup(const xtensa_mpu_entry *entry, unsigned n,
                             uint32_t vaddr, unsigned *segment)
{
    unsigned nhits = 0;
    unsigned i;

    for (i = 0; i < n; ++i) {
        if (vaddr >= entry[i].vaddr &&
            (i == n - 1 || vaddr < entry[i + 1].vaddr)) {
            if (nhits++) {
                break;
            }
            *segment = i;
        }
    }
    return nhits;
}

void HELPER(wsr_mpuenb)(CPUXtensaState *env, uint32_t v)
{
    v &= (2u << (env->config->n_mpu_fg_segments - 1)) - 1;

    if (v != env->sregs[MPUENB]) {
        env->sregs[MPUENB] = v;
        tlb_flush(env_cpu(env));
    }
}

void HELPER(wptlb)(CPUXtensaState *env, uint32_t p, uint32_t v)
{
    unsigned segment = p & XTENSA_MPU_SEGMENT_MASK;

    if (segment < env->config->n_mpu_fg_segments) {
        env->mpu_fg[segment].vaddr = v & -env->config->mpu_align;
        env->mpu_fg[segment].attr = p & XTENSA_MPU_ATTR_MASK;
        env->sregs[MPUENB] = deposit32(env->sregs[MPUENB], segment, 1, v);
        tlb_flush(env_cpu(env));
    }
}

uint32_t HELPER(rptlb0)(CPUXtensaState *env, uint32_t s)
{
    unsigned segment = s & XTENSA_MPU_SEGMENT_MASK;

    if (segment < env->config->n_mpu_fg_segments) {
        return env->mpu_fg[segment].vaddr |
            extract32(env->sregs[MPUENB], segment, 1);
    } else {
        return 0;
    }
}

uint32_t HELPER(rptlb1)(CPUXtensaState *env, uint32_t s)
{
    unsigned segment = s & XTENSA_MPU_SEGMENT_MASK;

    if (segment < env->config->n_mpu_fg_segments) {
        return env->mpu_fg[segment].attr;
    } else {
        return 0;
    }
}

uint32_t HELPER(pptlb)(CPUXtensaState *env, uint32_t v)
{
    unsigned nhits;
    unsigned segment = XTENSA_MPU_PROBE_B;
    unsigned bg_segment;

    nhits = xtensa_mpu_lookup(env->mpu_fg, env->config->n_mpu_fg_segments,
                              v, &segment);
    if (nhits > 1) {
        HELPER(exception_cause_vaddr)(env, env->pc,
                                      LOAD_STORE_TLB_MULTI_HIT_CAUSE, v);
    } else if (nhits == 1 && (env->sregs[MPUENB] & (1u << segment))) {
        return env->mpu_fg[segment].attr | segment | XTENSA_MPU_PROBE_V;
    } else {
        xtensa_mpu_lookup(env->config->mpu_bg,
                          env->config->n_mpu_bg_segments,
                          v, &bg_segment);
        return env->config->mpu_bg[bg_segment].attr | segment;
    }
}

static int get_physical_addr_mpu(CPUXtensaState *env,
                                 uint32_t vaddr, int is_write, int mmu_idx,
                                 uint32_t *paddr, uint32_t *page_size,
                                 unsigned *access)
{
    unsigned nhits;
    unsigned segment;
    uint32_t attr;

    nhits = xtensa_mpu_lookup(env->mpu_fg, env->config->n_mpu_fg_segments,
                              vaddr, &segment);
    if (nhits > 1) {
        return is_write < 2 ?
            LOAD_STORE_TLB_MULTI_HIT_CAUSE :
            INST_TLB_MULTI_HIT_CAUSE;
    } else if (nhits == 1 && (env->sregs[MPUENB] & (1u << segment))) {
        attr = env->mpu_fg[segment].attr;
    } else {
        xtensa_mpu_lookup(env->config->mpu_bg,
                          env->config->n_mpu_bg_segments,
                          vaddr, &segment);
        attr = env->config->mpu_bg[segment].attr;
    }

    *access = mpu_attr_to_access(attr, mmu_idx);
    if (!is_access_granted(*access, is_write)) {
        return is_write < 2 ?
            (is_write ?
             STORE_PROHIBITED_CAUSE :
             LOAD_PROHIBITED_CAUSE) :
            INST_FETCH_PROHIBITED_CAUSE;
    }
    *paddr = vaddr;
    *page_size = env->config->mpu_align;
    return 0;
}

/*!
 * Convert virtual address to physical addr.
 * MMU may issue pagewalk and change xtensa autorefill TLB way entry.
 *
 * \return 0 if ok, exception cause code otherwise
 */
int xtensa_get_physical_addr(CPUXtensaState *env, bool update_tlb,
                             uint32_t vaddr, int is_write, int mmu_idx,
                             uint32_t *paddr, uint32_t *page_size,
                             unsigned *access)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        return get_physical_addr_mmu(env, update_tlb,
                                     vaddr, is_write, mmu_idx, paddr,
                                     page_size, access, true);
    } else if (xtensa_option_bits_enabled(env->config,
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_PROTECTION) |
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_TRANSLATION))) {
        return get_physical_addr_region(env, vaddr, is_write, mmu_idx,
                                        paddr, page_size, access);
    } else if (xtensa_option_enabled(env->config, XTENSA_OPTION_MPU)) {
        return get_physical_addr_mpu(env, vaddr, is_write, mmu_idx,
                                     paddr, page_size, access);
    } else {
        *paddr = vaddr;
        *page_size = TARGET_PAGE_SIZE;
        *access = cacheattr_attr_to_access(env->sregs[CACHEATTR] >>
                                           ((vaddr & 0xe0000000) >> 27));
        return 0;
    }
}

static void dump_tlb(CPUXtensaState *env, bool dtlb)
{
    unsigned wi, ei;
    const xtensa_tlb *conf =
        dtlb ? &env->config->dtlb : &env->config->itlb;
    unsigned (*attr_to_access)(uint32_t) =
        xtensa_option_enabled(env->config, XTENSA_OPTION_MMU) ?
        mmu_attr_to_access : region_attr_to_access;

    for (wi = 0; wi < conf->nways; ++wi) {
        uint32_t sz = ~xtensa_tlb_get_addr_mask(env, dtlb, wi) + 1;
        const char *sz_text;
        bool print_header = true;

        if (sz >= 0x100000) {
            sz /= MiB;
            sz_text = "MB";
        } else {
            sz /= KiB;
            sz_text = "KB";
        }

        for (ei = 0; ei < conf->way_size[wi]; ++ei) {
            const xtensa_tlb_entry *entry =
                xtensa_tlb_get_entry(env, dtlb, wi, ei);

            if (entry->asid) {
                static const char * const cache_text[8] = {
                    [PAGE_CACHE_BYPASS >> PAGE_CACHE_SHIFT] = "Bypass",
                    [PAGE_CACHE_WT >> PAGE_CACHE_SHIFT] = "WT",
                    [PAGE_CACHE_WB >> PAGE_CACHE_SHIFT] = "WB",
                    [PAGE_CACHE_ISOLATE >> PAGE_CACHE_SHIFT] = "Isolate",
                };
                unsigned access = attr_to_access(entry->attr);
                unsigned cache_idx = (access & PAGE_CACHE_MASK) >>
                    PAGE_CACHE_SHIFT;

                if (print_header) {
                    print_header = false;
                    qemu_printf("Way %u (%d %s)\n", wi, sz, sz_text);
                    qemu_printf("\tVaddr       Paddr       ASID  Attr RWX Cache\n"
                                "\t----------  ----------  ----  ---- --- -------\n");
                }
                qemu_printf("\t0x%08x  0x%08x  0x%02x  0x%02x %c%c%c %-7s\n",
                            entry->vaddr,
                            entry->paddr,
                            entry->asid,
                            entry->attr,
                            (access & PAGE_READ) ? 'R' : '-',
                            (access & PAGE_WRITE) ? 'W' : '-',
                            (access & PAGE_EXEC) ? 'X' : '-',
                            cache_text[cache_idx] ?
                            cache_text[cache_idx] : "Invalid");
            }
        }
    }
}

static void dump_mpu(CPUXtensaState *env,
                     const xtensa_mpu_entry *entry, unsigned n)
{
    unsigned i;

    qemu_printf("\t%s  Vaddr       Attr        Ring0  Ring1  System Type    CPU cache\n"
                "\t%s  ----------  ----------  -----  -----  -------------  ---------\n",
                env ? "En" : "  ",
                env ? "--" : "  ");

    for (i = 0; i < n; ++i) {
        uint32_t attr = entry[i].attr;
        unsigned access0 = mpu_attr_to_access(attr, 0);
        unsigned access1 = mpu_attr_to_access(attr, 1);
        unsigned type = mpu_attr_to_type(attr);
        char cpu_cache = (type & XTENSA_MPU_TYPE_CPU_CACHE) ? '-' : ' ';

        qemu_printf("\t %c  0x%08x  0x%08x   %c%c%c    %c%c%c   ",
                    env ?
                    ((env->sregs[MPUENB] & (1u << i)) ? '+' : '-') : ' ',
                    entry[i].vaddr, attr,
                    (access0 & PAGE_READ) ? 'R' : '-',
                    (access0 & PAGE_WRITE) ? 'W' : '-',
                    (access0 & PAGE_EXEC) ? 'X' : '-',
                    (access1 & PAGE_READ) ? 'R' : '-',
                    (access1 & PAGE_WRITE) ? 'W' : '-',
                    (access1 & PAGE_EXEC) ? 'X' : '-');

        switch (type & XTENSA_MPU_SYSTEM_TYPE_MASK) {
        case XTENSA_MPU_SYSTEM_TYPE_DEVICE:
            qemu_printf("Device %cB %3s\n",
                        (type & XTENSA_MPU_TYPE_B) ? ' ' : 'n',
                        (type & XTENSA_MPU_TYPE_INT) ? "int" : "");
            break;
        case XTENSA_MPU_SYSTEM_TYPE_NC:
            qemu_printf("Sys NC %cB      %c%c%c\n",
                        (type & XTENSA_MPU_TYPE_B) ? ' ' : 'n',
                        (type & XTENSA_MPU_TYPE_CPU_R) ? 'r' : cpu_cache,
                        (type & XTENSA_MPU_TYPE_CPU_W) ? 'w' : cpu_cache,
                        (type & XTENSA_MPU_TYPE_CPU_C) ? 'c' : cpu_cache);
            break;
        case XTENSA_MPU_SYSTEM_TYPE_C:
            qemu_printf("Sys  C %c%c%c     %c%c%c\n",
                        (type & XTENSA_MPU_TYPE_SYS_R) ? 'R' : '-',
                        (type & XTENSA_MPU_TYPE_SYS_W) ? 'W' : '-',
                        (type & XTENSA_MPU_TYPE_SYS_C) ? 'C' : '-',
                        (type & XTENSA_MPU_TYPE_CPU_R) ? 'r' : cpu_cache,
                        (type & XTENSA_MPU_TYPE_CPU_W) ? 'w' : cpu_cache,
                        (type & XTENSA_MPU_TYPE_CPU_C) ? 'c' : cpu_cache);
            break;
        default:
            qemu_printf("Unknown\n");
            break;
        }
    }
}

void dump_mmu(CPUXtensaState *env)
{
    if (xtensa_option_bits_enabled(env->config,
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_PROTECTION) |
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_TRANSLATION) |
                XTENSA_OPTION_BIT(XTENSA_OPTION_MMU))) {

        qemu_printf("ITLB:\n");
        dump_tlb(env, false);
        qemu_printf("\nDTLB:\n");
        dump_tlb(env, true);
    } else if (xtensa_option_enabled(env->config, XTENSA_OPTION_MPU)) {
        qemu_printf("Foreground map:\n");
        dump_mpu(env, env->mpu_fg, env->config->n_mpu_fg_segments);
        qemu_printf("\nBackground map:\n");
        dump_mpu(NULL, env->config->mpu_bg, env->config->n_mpu_bg_segments);
    } else {
        qemu_printf("No TLB for this CPU core\n");
    }
}
