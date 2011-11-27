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
#include "exec-all.h"
#include "gdbstub.h"
#include "qemu-common.h"
#include "host-utils.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif

static void reset_mmu(CPUState *env);

void cpu_reset(CPUXtensaState *env)
{
    env->exception_taken = 0;
    env->pc = env->config->exception_vector[EXC_RESET];
    env->sregs[LITBASE] &= ~1;
    env->sregs[PS] = xtensa_option_enabled(env->config,
            XTENSA_OPTION_INTERRUPT) ? 0x1f : 0x10;
    env->sregs[VECBASE] = env->config->vecbase;

    env->pending_irq_level = 0;
    reset_mmu(env);
}

static struct XtensaConfigList *xtensa_cores;

void xtensa_register_core(XtensaConfigList *node)
{
    node->next = xtensa_cores;
    xtensa_cores = node;
}

CPUXtensaState *cpu_xtensa_init(const char *cpu_model)
{
    static int tcg_inited;
    CPUXtensaState *env;
    const XtensaConfig *config = NULL;
    XtensaConfigList *core = xtensa_cores;

    for (; core; core = core->next)
        if (strcmp(core->config->name, cpu_model) == 0) {
            config = core->config;
            break;
        }

    if (config == NULL) {
        return NULL;
    }

    env = g_malloc0(sizeof(*env));
    env->config = config;
    cpu_exec_init(env);

    if (!tcg_inited) {
        tcg_inited = 1;
        xtensa_translate_init();
    }

    xtensa_irq_init(env);
    qemu_init_vcpu(env);
    return env;
}


void xtensa_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    XtensaConfigList *core = xtensa_cores;
    cpu_fprintf(f, "Available CPUs:\n");
    for (; core; core = core->next) {
        cpu_fprintf(f, "  %s\n", core->config->name);
    }
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    uint32_t paddr;
    uint32_t page_size;
    unsigned access;

    if (xtensa_get_physical_addr(env, addr, 0, 0,
                &paddr, &page_size, &access) == 0) {
        return paddr;
    }
    if (xtensa_get_physical_addr(env, addr, 2, 0,
                &paddr, &page_size, &access) == 0) {
        return paddr;
    }
    return ~0;
}

static uint32_t relocated_vector(CPUState *env, uint32_t vector)
{
    if (xtensa_option_enabled(env->config,
                XTENSA_OPTION_RELOCATABLE_VECTOR)) {
        return vector - env->config->vecbase + env->sregs[VECBASE];
    } else {
        return vector;
    }
}

/*!
 * Handle penging IRQ.
 * For the high priority interrupt jump to the corresponding interrupt vector.
 * For the level-1 interrupt convert it to either user, kernel or double
 * exception with the 'level-1 interrupt' exception cause.
 */
static void handle_interrupt(CPUState *env)
{
    int level = env->pending_irq_level;

    if (level > xtensa_get_cintlevel(env) &&
            level <= env->config->nlevel &&
            (env->config->level_mask[level] &
             env->sregs[INTSET] &
             env->sregs[INTENABLE])) {
        if (level > 1) {
            env->sregs[EPC1 + level - 1] = env->pc;
            env->sregs[EPS2 + level - 2] = env->sregs[PS];
            env->sregs[PS] =
                (env->sregs[PS] & ~PS_INTLEVEL) | level | PS_EXCM;
            env->pc = relocated_vector(env,
                    env->config->interrupt_vector[level]);
        } else {
            env->sregs[EXCCAUSE] = LEVEL1_INTERRUPT_CAUSE;

            if (env->sregs[PS] & PS_EXCM) {
                if (env->config->ndepc) {
                    env->sregs[DEPC] = env->pc;
                } else {
                    env->sregs[EPC1] = env->pc;
                }
                env->exception_index = EXC_DOUBLE;
            } else {
                env->sregs[EPC1] = env->pc;
                env->exception_index =
                    (env->sregs[PS] & PS_UM) ? EXC_USER : EXC_KERNEL;
            }
            env->sregs[PS] |= PS_EXCM;
        }
        env->exception_taken = 1;
    }
}

void do_interrupt(CPUState *env)
{
    if (env->exception_index == EXC_IRQ) {
        qemu_log_mask(CPU_LOG_INT,
                "%s(EXC_IRQ) level = %d, cintlevel = %d, "
                "pc = %08x, a0 = %08x, ps = %08x, "
                "intset = %08x, intenable = %08x, "
                "ccount = %08x\n",
                __func__, env->pending_irq_level, xtensa_get_cintlevel(env),
                env->pc, env->regs[0], env->sregs[PS],
                env->sregs[INTSET], env->sregs[INTENABLE],
                env->sregs[CCOUNT]);
        handle_interrupt(env);
    }

    switch (env->exception_index) {
    case EXC_WINDOW_OVERFLOW4:
    case EXC_WINDOW_UNDERFLOW4:
    case EXC_WINDOW_OVERFLOW8:
    case EXC_WINDOW_UNDERFLOW8:
    case EXC_WINDOW_OVERFLOW12:
    case EXC_WINDOW_UNDERFLOW12:
    case EXC_KERNEL:
    case EXC_USER:
    case EXC_DOUBLE:
        qemu_log_mask(CPU_LOG_INT, "%s(%d) "
                "pc = %08x, a0 = %08x, ps = %08x, ccount = %08x\n",
                __func__, env->exception_index,
                env->pc, env->regs[0], env->sregs[PS], env->sregs[CCOUNT]);
        if (env->config->exception_vector[env->exception_index]) {
            env->pc = relocated_vector(env,
                    env->config->exception_vector[env->exception_index]);
            env->exception_taken = 1;
        } else {
            qemu_log("%s(pc = %08x) bad exception_index: %d\n",
                    __func__, env->pc, env->exception_index);
        }
        break;

    case EXC_IRQ:
        break;

    default:
        qemu_log("%s(pc = %08x) unknown exception_index: %d\n",
                __func__, env->pc, env->exception_index);
        break;
    }
    check_interrupts(env);
}

static void reset_tlb_mmu_all_ways(CPUState *env,
        const xtensa_tlb *tlb, xtensa_tlb_entry entry[][MAX_TLB_WAY_SIZE])
{
    unsigned wi, ei;

    for (wi = 0; wi < tlb->nways; ++wi) {
        for (ei = 0; ei < tlb->way_size[wi]; ++ei) {
            entry[wi][ei].asid = 0;
            entry[wi][ei].variable = true;
        }
    }
}

static void reset_tlb_mmu_ways56(CPUState *env,
        const xtensa_tlb *tlb, xtensa_tlb_entry entry[][MAX_TLB_WAY_SIZE])
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

static void reset_tlb_region_way0(CPUState *env,
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

static void reset_mmu(CPUState *env)
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
    } else {
        reset_tlb_region_way0(env, env->itlb);
        reset_tlb_region_way0(env, env->dtlb);
    }
}

static unsigned get_ring(const CPUState *env, uint8_t asid)
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
int xtensa_tlb_lookup(const CPUState *env, uint32_t addr, bool dtlb,
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
    } else if (attr == 13) {
        access |= PAGE_READ | PAGE_WRITE;
    }
    return access;
}

/*!
 * Convert region protection ATTR to PAGE_{READ,WRITE,EXEC} mask.
 * See ISA, 4.6.3.3
 */
static unsigned region_attr_to_access(uint32_t attr)
{
    unsigned access = 0;
    if ((attr < 6 && attr != 3) || attr == 14) {
        access |= PAGE_READ | PAGE_WRITE;
    }
    if (attr > 0 && attr < 6) {
        access |= PAGE_EXEC;
    }
    return access;
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

static int autorefill_mmu(CPUState *env, uint32_t vaddr, bool dtlb,
        uint32_t *wi, uint32_t *ei, uint8_t *ring);

static int get_physical_addr_mmu(CPUState *env,
        uint32_t vaddr, int is_write, int mmu_idx,
        uint32_t *paddr, uint32_t *page_size, unsigned *access)
{
    bool dtlb = is_write != 2;
    uint32_t wi;
    uint32_t ei;
    uint8_t ring;
    int ret = xtensa_tlb_lookup(env, vaddr, dtlb, &wi, &ei, &ring);

    if ((ret == INST_TLB_MISS_CAUSE || ret == LOAD_STORE_TLB_MISS_CAUSE) &&
            (mmu_idx != 0 || ((vaddr ^ env->sregs[PTEVADDR]) & 0xffc00000)) &&
            autorefill_mmu(env, vaddr, dtlb, &wi, &ei, &ring) == 0) {
        ret = 0;
    }
    if (ret != 0) {
        return ret;
    }

    const xtensa_tlb_entry *entry =
        xtensa_tlb_get_entry(env, dtlb, wi, ei);

    if (ring < mmu_idx) {
        return dtlb ?
            LOAD_STORE_PRIVILEGE_CAUSE :
            INST_FETCH_PRIVILEGE_CAUSE;
    }

    *access = mmu_attr_to_access(entry->attr);
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

static int autorefill_mmu(CPUState *env, uint32_t vaddr, bool dtlb,
        uint32_t *wi, uint32_t *ei, uint8_t *ring)
{
    uint32_t paddr;
    uint32_t page_size;
    unsigned access;
    uint32_t pt_vaddr =
        (env->sregs[PTEVADDR] | (vaddr >> 10)) & 0xfffffffc;
    int ret = get_physical_addr_mmu(env, pt_vaddr, 0, 0,
            &paddr, &page_size, &access);

    qemu_log("%s: trying autorefill(%08x) -> %08x\n", __func__,
            vaddr, ret ? ~0 : paddr);

    if (ret == 0) {
        uint32_t vpn;
        uint32_t pte = ldl_phys(paddr);

        *ring = (pte >> 4) & 0x3;
        *wi = (++env->autorefill_idx) & 0x3;
        split_tlb_entry_spec_way(env, vaddr, dtlb, &vpn, *wi, ei);
        xtensa_tlb_set_entry(env, dtlb, *wi, *ei, vpn, pte);
        qemu_log("%s: autorefill(%08x): %08x -> %08x\n",
                __func__, vaddr, vpn, pte);
    }
    return ret;
}

static int get_physical_addr_region(CPUState *env,
        uint32_t vaddr, int is_write, int mmu_idx,
        uint32_t *paddr, uint32_t *page_size, unsigned *access)
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

/*!
 * Convert virtual address to physical addr.
 * MMU may issue pagewalk and change xtensa autorefill TLB way entry.
 *
 * \return 0 if ok, exception cause code otherwise
 */
int xtensa_get_physical_addr(CPUState *env,
        uint32_t vaddr, int is_write, int mmu_idx,
        uint32_t *paddr, uint32_t *page_size, unsigned *access)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        return get_physical_addr_mmu(env, vaddr, is_write, mmu_idx,
                paddr, page_size, access);
    } else if (xtensa_option_bits_enabled(env->config,
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_PROTECTION) |
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_TRANSLATION))) {
        return get_physical_addr_region(env, vaddr, is_write, mmu_idx,
                paddr, page_size, access);
    } else {
        *paddr = vaddr;
        *page_size = TARGET_PAGE_SIZE;
        *access = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return 0;
    }
}
