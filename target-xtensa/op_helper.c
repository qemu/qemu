/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
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

#include "cpu.h"
#include "dyngen-exec.h"
#include "helpers.h"
#include "host-utils.h"

static void do_unaligned_access(target_ulong addr, int is_write, int is_user,
        void *retaddr);

#define ALIGNED_ONLY
#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

static void do_restore_state(void *pc_ptr)
{
    TranslationBlock *tb;
    uint32_t pc = (uint32_t)(intptr_t)pc_ptr;

    tb = tb_find_pc(pc);
    if (tb) {
        cpu_restore_state(tb, env, pc);
    }
}

static void do_unaligned_access(target_ulong addr, int is_write, int is_user,
        void *retaddr)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_UNALIGNED_EXCEPTION) &&
            !xtensa_option_enabled(env->config, XTENSA_OPTION_HW_ALIGNMENT)) {
        do_restore_state(retaddr);
        HELPER(exception_cause_vaddr)(
                env->pc, LOAD_STORE_ALIGNMENT_CAUSE, addr);
    }
}

void tlb_fill(target_ulong vaddr, int is_write, int mmu_idx, void *retaddr)
{
    CPUState *saved_env = env;

    env = cpu_single_env;
    {
        uint32_t paddr;
        uint32_t page_size;
        unsigned access;
        int ret = xtensa_get_physical_addr(env, vaddr, is_write, mmu_idx,
                &paddr, &page_size, &access);

        qemu_log("%s(%08x, %d, %d) -> %08x, ret = %d\n", __func__,
                vaddr, is_write, mmu_idx, paddr, ret);

        if (ret == 0) {
            tlb_set_page(env,
                    vaddr & TARGET_PAGE_MASK,
                    paddr & TARGET_PAGE_MASK,
                    access, mmu_idx, page_size);
        } else {
            do_restore_state(retaddr);
            HELPER(exception_cause_vaddr)(env->pc, ret, vaddr);
        }
    }
    env = saved_env;
}

void HELPER(exception)(uint32_t excp)
{
    env->exception_index = excp;
    cpu_loop_exit(env);
}

void HELPER(exception_cause)(uint32_t pc, uint32_t cause)
{
    uint32_t vector;

    env->pc = pc;
    if (env->sregs[PS] & PS_EXCM) {
        if (env->config->ndepc) {
            env->sregs[DEPC] = pc;
        } else {
            env->sregs[EPC1] = pc;
        }
        vector = EXC_DOUBLE;
    } else {
        env->sregs[EPC1] = pc;
        vector = (env->sregs[PS] & PS_UM) ? EXC_USER : EXC_KERNEL;
    }

    env->sregs[EXCCAUSE] = cause;
    env->sregs[PS] |= PS_EXCM;

    HELPER(exception)(vector);
}

void HELPER(exception_cause_vaddr)(uint32_t pc, uint32_t cause, uint32_t vaddr)
{
    env->sregs[EXCVADDR] = vaddr;
    HELPER(exception_cause)(pc, cause);
}

uint32_t HELPER(nsa)(uint32_t v)
{
    if (v & 0x80000000) {
        v = ~v;
    }
    return v ? clz32(v) - 1 : 31;
}

uint32_t HELPER(nsau)(uint32_t v)
{
    return v ? clz32(v) : 32;
}

static void copy_window_from_phys(CPUState *env,
        uint32_t window, uint32_t phys, uint32_t n)
{
    assert(phys < env->config->nareg);
    if (phys + n <= env->config->nareg) {
        memcpy(env->regs + window, env->phys_regs + phys,
                n * sizeof(uint32_t));
    } else {
        uint32_t n1 = env->config->nareg - phys;
        memcpy(env->regs + window, env->phys_regs + phys,
                n1 * sizeof(uint32_t));
        memcpy(env->regs + window + n1, env->phys_regs,
                (n - n1) * sizeof(uint32_t));
    }
}

static void copy_phys_from_window(CPUState *env,
        uint32_t phys, uint32_t window, uint32_t n)
{
    assert(phys < env->config->nareg);
    if (phys + n <= env->config->nareg) {
        memcpy(env->phys_regs + phys, env->regs + window,
                n * sizeof(uint32_t));
    } else {
        uint32_t n1 = env->config->nareg - phys;
        memcpy(env->phys_regs + phys, env->regs + window,
                n1 * sizeof(uint32_t));
        memcpy(env->phys_regs, env->regs + window + n1,
                (n - n1) * sizeof(uint32_t));
    }
}


static inline unsigned windowbase_bound(unsigned a, const CPUState *env)
{
    return a & (env->config->nareg / 4 - 1);
}

static inline unsigned windowstart_bit(unsigned a, const CPUState *env)
{
    return 1 << windowbase_bound(a, env);
}

void xtensa_sync_window_from_phys(CPUState *env)
{
    copy_window_from_phys(env, 0, env->sregs[WINDOW_BASE] * 4, 16);
}

void xtensa_sync_phys_from_window(CPUState *env)
{
    copy_phys_from_window(env, env->sregs[WINDOW_BASE] * 4, 0, 16);
}

static void rotate_window_abs(uint32_t position)
{
    xtensa_sync_phys_from_window(env);
    env->sregs[WINDOW_BASE] = windowbase_bound(position, env);
    xtensa_sync_window_from_phys(env);
}

static void rotate_window(uint32_t delta)
{
    rotate_window_abs(env->sregs[WINDOW_BASE] + delta);
}

void HELPER(wsr_windowbase)(uint32_t v)
{
    rotate_window_abs(v);
}

void HELPER(entry)(uint32_t pc, uint32_t s, uint32_t imm)
{
    int callinc = (env->sregs[PS] & PS_CALLINC) >> PS_CALLINC_SHIFT;
    if (s > 3 || ((env->sregs[PS] & (PS_WOE | PS_EXCM)) ^ PS_WOE) != 0) {
        qemu_log("Illegal entry instruction(pc = %08x), PS = %08x\n",
                pc, env->sregs[PS]);
        HELPER(exception_cause)(pc, ILLEGAL_INSTRUCTION_CAUSE);
    } else {
        env->regs[(callinc << 2) | (s & 3)] = env->regs[s] - (imm << 3);
        rotate_window(callinc);
        env->sregs[WINDOW_START] |=
            windowstart_bit(env->sregs[WINDOW_BASE], env);
    }
}

void HELPER(window_check)(uint32_t pc, uint32_t w)
{
    uint32_t windowbase = windowbase_bound(env->sregs[WINDOW_BASE], env);
    uint32_t windowstart = env->sregs[WINDOW_START];
    uint32_t m, n;

    if ((env->sregs[PS] & (PS_WOE | PS_EXCM)) ^ PS_WOE) {
        return;
    }

    for (n = 1; ; ++n) {
        if (n > w) {
            return;
        }
        if (windowstart & windowstart_bit(windowbase + n, env)) {
            break;
        }
    }

    m = windowbase_bound(windowbase + n, env);
    rotate_window(n);
    env->sregs[PS] = (env->sregs[PS] & ~PS_OWB) |
        (windowbase << PS_OWB_SHIFT) | PS_EXCM;
    env->sregs[EPC1] = env->pc = pc;

    if (windowstart & windowstart_bit(m + 1, env)) {
        HELPER(exception)(EXC_WINDOW_OVERFLOW4);
    } else if (windowstart & windowstart_bit(m + 2, env)) {
        HELPER(exception)(EXC_WINDOW_OVERFLOW8);
    } else {
        HELPER(exception)(EXC_WINDOW_OVERFLOW12);
    }
}

uint32_t HELPER(retw)(uint32_t pc)
{
    int n = (env->regs[0] >> 30) & 0x3;
    int m = 0;
    uint32_t windowbase = windowbase_bound(env->sregs[WINDOW_BASE], env);
    uint32_t windowstart = env->sregs[WINDOW_START];
    uint32_t ret_pc = 0;

    if (windowstart & windowstart_bit(windowbase - 1, env)) {
        m = 1;
    } else if (windowstart & windowstart_bit(windowbase - 2, env)) {
        m = 2;
    } else if (windowstart & windowstart_bit(windowbase - 3, env)) {
        m = 3;
    }

    if (n == 0 || (m != 0 && m != n) ||
            ((env->sregs[PS] & (PS_WOE | PS_EXCM)) ^ PS_WOE) != 0) {
        qemu_log("Illegal retw instruction(pc = %08x), "
                "PS = %08x, m = %d, n = %d\n",
                pc, env->sregs[PS], m, n);
        HELPER(exception_cause)(pc, ILLEGAL_INSTRUCTION_CAUSE);
    } else {
        int owb = windowbase;

        ret_pc = (pc & 0xc0000000) | (env->regs[0] & 0x3fffffff);

        rotate_window(-n);
        if (windowstart & windowstart_bit(env->sregs[WINDOW_BASE], env)) {
            env->sregs[WINDOW_START] &= ~windowstart_bit(owb, env);
        } else {
            /* window underflow */
            env->sregs[PS] = (env->sregs[PS] & ~PS_OWB) |
                (windowbase << PS_OWB_SHIFT) | PS_EXCM;
            env->sregs[EPC1] = env->pc = pc;

            if (n == 1) {
                HELPER(exception)(EXC_WINDOW_UNDERFLOW4);
            } else if (n == 2) {
                HELPER(exception)(EXC_WINDOW_UNDERFLOW8);
            } else if (n == 3) {
                HELPER(exception)(EXC_WINDOW_UNDERFLOW12);
            }
        }
    }
    return ret_pc;
}

void HELPER(rotw)(uint32_t imm4)
{
    rotate_window(imm4);
}

void HELPER(restore_owb)(void)
{
    rotate_window_abs((env->sregs[PS] & PS_OWB) >> PS_OWB_SHIFT);
}

void HELPER(movsp)(uint32_t pc)
{
    if ((env->sregs[WINDOW_START] &
            (windowstart_bit(env->sregs[WINDOW_BASE] - 3, env) |
             windowstart_bit(env->sregs[WINDOW_BASE] - 2, env) |
             windowstart_bit(env->sregs[WINDOW_BASE] - 1, env))) == 0) {
        HELPER(exception_cause)(pc, ALLOCA_CAUSE);
    }
}

void HELPER(wsr_lbeg)(uint32_t v)
{
    if (env->sregs[LBEG] != v) {
        tb_invalidate_phys_page_range(
                env->sregs[LEND] - 1, env->sregs[LEND], 0);
        env->sregs[LBEG] = v;
    }
}

void HELPER(wsr_lend)(uint32_t v)
{
    if (env->sregs[LEND] != v) {
        tb_invalidate_phys_page_range(
                env->sregs[LEND] - 1, env->sregs[LEND], 0);
        env->sregs[LEND] = v;
        tb_invalidate_phys_page_range(
                env->sregs[LEND] - 1, env->sregs[LEND], 0);
    }
}

void HELPER(dump_state)(void)
{
    cpu_dump_state(env, stderr, fprintf, 0);
}

void HELPER(waiti)(uint32_t pc, uint32_t intlevel)
{
    env->pc = pc;
    env->sregs[PS] = (env->sregs[PS] & ~PS_INTLEVEL) |
        (intlevel << PS_INTLEVEL_SHIFT);
    check_interrupts(env);
    if (env->pending_irq_level) {
        cpu_loop_exit(env);
        return;
    }

    if (xtensa_option_enabled(env->config, XTENSA_OPTION_TIMER_INTERRUPT)) {
        int i;
        uint32_t wake_ccount = env->sregs[CCOUNT] - 1;

        for (i = 0; i < env->config->nccompare; ++i) {
            if (env->sregs[CCOMPARE + i] - env->sregs[CCOUNT] <
                    wake_ccount - env->sregs[CCOUNT]) {
                wake_ccount = env->sregs[CCOMPARE + i];
            }
        }
        env->wake_ccount = wake_ccount;
        qemu_mod_timer(env->ccompare_timer, qemu_get_clock_ns(vm_clock) +
                muldiv64(wake_ccount - env->sregs[CCOUNT],
                    1000000, env->config->clock_freq_khz));
    }
    env->halt_clock = qemu_get_clock_ns(vm_clock);
    env->halted = 1;
    HELPER(exception)(EXCP_HLT);
}

void HELPER(timer_irq)(uint32_t id, uint32_t active)
{
    xtensa_timer_irq(env, id, active);
}

void HELPER(advance_ccount)(uint32_t d)
{
    xtensa_advance_ccount(env, d);
}

void HELPER(check_interrupts)(CPUState *env)
{
    check_interrupts(env);
}

void HELPER(wsr_rasid)(uint32_t v)
{
    v = (v & 0xffffff00) | 0x1;
    if (v != env->sregs[RASID]) {
        env->sregs[RASID] = v;
        tlb_flush(env, 1);
    }
}

static uint32_t get_page_size(const CPUState *env, bool dtlb, uint32_t way)
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
uint32_t xtensa_tlb_get_addr_mask(const CPUState *env, bool dtlb, uint32_t way)
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
static uint32_t get_vpn_mask(const CPUState *env, bool dtlb, uint32_t way)
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
void split_tlb_entry_spec_way(const CPUState *env, uint32_t v, bool dtlb,
        uint32_t *vpn, uint32_t wi, uint32_t *ei)
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
static void split_tlb_entry_spec(uint32_t v, bool dtlb,
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

static xtensa_tlb_entry *get_tlb_entry(uint32_t v, bool dtlb, uint32_t *pwi)
{
    uint32_t vpn;
    uint32_t wi;
    uint32_t ei;

    split_tlb_entry_spec(v, dtlb, &vpn, &wi, &ei);
    if (pwi) {
        *pwi = wi;
    }
    return xtensa_tlb_get_entry(env, dtlb, wi, ei);
}

uint32_t HELPER(rtlb0)(uint32_t v, uint32_t dtlb)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        uint32_t wi;
        const xtensa_tlb_entry *entry = get_tlb_entry(v, dtlb, &wi);
        return (entry->vaddr & get_vpn_mask(env, dtlb, wi)) | entry->asid;
    } else {
        return v & REGION_PAGE_MASK;
    }
}

uint32_t HELPER(rtlb1)(uint32_t v, uint32_t dtlb)
{
    const xtensa_tlb_entry *entry = get_tlb_entry(v, dtlb, NULL);
    return entry->paddr | entry->attr;
}

void HELPER(itlb)(uint32_t v, uint32_t dtlb)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        uint32_t wi;
        xtensa_tlb_entry *entry = get_tlb_entry(v, dtlb, &wi);
        if (entry->variable && entry->asid) {
            tlb_flush_page(env, entry->vaddr);
            entry->asid = 0;
        }
    }
}

uint32_t HELPER(ptlb)(uint32_t v, uint32_t dtlb)
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
            HELPER(exception_cause_vaddr)(env->pc, res, v);
            break;
        }
        return 0;
    } else {
        return (v & REGION_PAGE_MASK) | 0x1;
    }
}

void xtensa_tlb_set_entry(CPUState *env, bool dtlb,
        unsigned wi, unsigned ei, uint32_t vpn, uint32_t pte)
{
    xtensa_tlb_entry *entry = xtensa_tlb_get_entry(env, dtlb, wi, ei);

    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        if (entry->variable) {
            if (entry->asid) {
                tlb_flush_page(env, entry->vaddr);
            }
            entry->vaddr = vpn;
            entry->paddr = pte & xtensa_tlb_get_addr_mask(env, dtlb, wi);
            entry->asid = (env->sregs[RASID] >> ((pte >> 1) & 0x18)) & 0xff;
            entry->attr = pte & 0xf;
        } else {
            qemu_log("%s %d, %d, %d trying to set immutable entry\n",
                    __func__, dtlb, wi, ei);
        }
    } else {
        tlb_flush_page(env, entry->vaddr);
        if (xtensa_option_enabled(env->config,
                    XTENSA_OPTION_REGION_TRANSLATION)) {
            entry->paddr = pte & REGION_PAGE_MASK;
        }
        entry->attr = pte & 0xf;
    }
}

void HELPER(wtlb)(uint32_t p, uint32_t v, uint32_t dtlb)
{
    uint32_t vpn;
    uint32_t wi;
    uint32_t ei;
    split_tlb_entry_spec(v, dtlb, &vpn, &wi, &ei);
    xtensa_tlb_set_entry(env, dtlb, wi, ei, vpn, p);
}
