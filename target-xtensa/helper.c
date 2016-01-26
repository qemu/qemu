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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/gdbstub.h"
#include "qemu/host-utils.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif

static struct XtensaConfigList *xtensa_cores;

static void xtensa_core_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    XtensaCPUClass *xcc = XTENSA_CPU_CLASS(oc);
    const XtensaConfig *config = data;

    xcc->config = config;

    /* Use num_core_regs to see only non-privileged registers in an unmodified
     * gdb. Use num_regs to see all registers. gdb modification is required
     * for that: reset bit 0 in the 'flags' field of the registers definitions
     * in the gdb/xtensa-config.c inside gdb source tree or inside gdb overlay.
     */
    cc->gdb_num_core_regs = config->gdb_regmap.num_regs;
}

void xtensa_finalize_config(XtensaConfig *config)
{
    unsigned i, n = 0;

    if (config->gdb_regmap.num_regs) {
        return;
    }

    for (i = 0; config->gdb_regmap.reg[i].targno >= 0; ++i) {
        n += (config->gdb_regmap.reg[i].type != 6);
    }
    config->gdb_regmap.num_regs = n;
}

void xtensa_register_core(XtensaConfigList *node)
{
    TypeInfo type = {
        .parent = TYPE_XTENSA_CPU,
        .class_init = xtensa_core_class_init,
        .class_data = (void *)node->config,
    };

    node->next = xtensa_cores;
    xtensa_cores = node;
    type.name = g_strdup_printf("%s-" TYPE_XTENSA_CPU, node->config->name);
    type_register(&type);
    g_free((gpointer)type.name);
}

static uint32_t check_hw_breakpoints(CPUXtensaState *env)
{
    unsigned i;

    for (i = 0; i < env->config->ndbreak; ++i) {
        if (env->cpu_watchpoint[i] &&
                env->cpu_watchpoint[i]->flags & BP_WATCHPOINT_HIT) {
            return DEBUGCAUSE_DB | (i << DEBUGCAUSE_DBNUM_SHIFT);
        }
    }
    return 0;
}

void xtensa_breakpoint_handler(CPUState *cs)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    if (cs->watchpoint_hit) {
        if (cs->watchpoint_hit->flags & BP_CPU) {
            uint32_t cause;

            cs->watchpoint_hit = NULL;
            cause = check_hw_breakpoints(env);
            if (cause) {
                debug_exception_env(env, cause);
            }
            cpu_resume_from_signal(cs, NULL);
        }
    }
}

XtensaCPU *cpu_xtensa_init(const char *cpu_model)
{
    ObjectClass *oc;
    XtensaCPU *cpu;
    CPUXtensaState *env;

    oc = cpu_class_by_name(TYPE_XTENSA_CPU, cpu_model);
    if (oc == NULL) {
        return NULL;
    }

    cpu = XTENSA_CPU(object_new(object_class_get_name(oc)));
    env = &cpu->env;

    xtensa_irq_init(env);

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
}


void xtensa_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    XtensaConfigList *core = xtensa_cores;
    cpu_fprintf(f, "Available CPUs:\n");
    for (; core; core = core->next) {
        cpu_fprintf(f, "  %s\n", core->config->name);
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

static uint32_t relocated_vector(CPUXtensaState *env, uint32_t vector)
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
static void handle_interrupt(CPUXtensaState *env)
{
    int level = env->pending_irq_level;

    if (level > xtensa_get_cintlevel(env) &&
            level <= env->config->nlevel &&
            (env->config->level_mask[level] &
             env->sregs[INTSET] &
             env->sregs[INTENABLE])) {
        CPUState *cs = CPU(xtensa_env_get_cpu(env));

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
                cs->exception_index = EXC_DOUBLE;
            } else {
                env->sregs[EPC1] = env->pc;
                cs->exception_index =
                    (env->sregs[PS] & PS_UM) ? EXC_USER : EXC_KERNEL;
            }
            env->sregs[PS] |= PS_EXCM;
        }
        env->exception_taken = 1;
    }
}

void xtensa_cpu_do_interrupt(CPUState *cs)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    if (cs->exception_index == EXC_IRQ) {
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

    switch (cs->exception_index) {
    case EXC_WINDOW_OVERFLOW4:
    case EXC_WINDOW_UNDERFLOW4:
    case EXC_WINDOW_OVERFLOW8:
    case EXC_WINDOW_UNDERFLOW8:
    case EXC_WINDOW_OVERFLOW12:
    case EXC_WINDOW_UNDERFLOW12:
    case EXC_KERNEL:
    case EXC_USER:
    case EXC_DOUBLE:
    case EXC_DEBUG:
        qemu_log_mask(CPU_LOG_INT, "%s(%d) "
                "pc = %08x, a0 = %08x, ps = %08x, ccount = %08x\n",
                __func__, cs->exception_index,
                env->pc, env->regs[0], env->sregs[PS], env->sregs[CCOUNT]);
        if (env->config->exception_vector[cs->exception_index]) {
            env->pc = relocated_vector(env,
                    env->config->exception_vector[cs->exception_index]);
            env->exception_taken = 1;
        } else {
            qemu_log_mask(CPU_LOG_INT, "%s(pc = %08x) bad exception_index: %d\n",
                          __func__, env->pc, cs->exception_index);
        }
        break;

    case EXC_IRQ:
        break;

    default:
        qemu_log("%s(pc = %08x) unknown exception_index: %d\n",
                __func__, env->pc, cs->exception_index);
        break;
    }
    check_interrupts(env);
}

bool xtensa_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        cs->exception_index = EXC_IRQ;
        xtensa_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}

static void reset_tlb_mmu_all_ways(CPUXtensaState *env,
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

static void reset_tlb_mmu_ways56(CPUXtensaState *env,
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
    } else {
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
int xtensa_tlb_lookup(const CPUXtensaState *env, uint32_t addr, bool dtlb,
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

static int get_pte(CPUXtensaState *env, uint32_t vaddr, uint32_t *pte);

static int get_physical_addr_mmu(CPUXtensaState *env, bool update_tlb,
        uint32_t vaddr, int is_write, int mmu_idx,
        uint32_t *paddr, uint32_t *page_size, unsigned *access,
        bool may_lookup_pt)
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
            may_lookup_pt && get_pte(env, vaddr, &pte) == 0) {
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

static int get_pte(CPUXtensaState *env, uint32_t vaddr, uint32_t *pte)
{
    CPUState *cs = CPU(xtensa_env_get_cpu(env));
    uint32_t paddr;
    uint32_t page_size;
    unsigned access;
    uint32_t pt_vaddr =
        (env->sregs[PTEVADDR] | (vaddr >> 10)) & 0xfffffffc;
    int ret = get_physical_addr_mmu(env, false, pt_vaddr, 0, 0,
            &paddr, &page_size, &access, false);

    qemu_log_mask(CPU_LOG_MMU, "%s: trying autorefill(%08x) -> %08x\n",
                  __func__, vaddr, ret ? ~0 : paddr);

    if (ret == 0) {
        *pte = ldl_phys(cs->as, paddr);
    }
    return ret;
}

static int get_physical_addr_region(CPUXtensaState *env,
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
int xtensa_get_physical_addr(CPUXtensaState *env, bool update_tlb,
        uint32_t vaddr, int is_write, int mmu_idx,
        uint32_t *paddr, uint32_t *page_size, unsigned *access)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        return get_physical_addr_mmu(env, update_tlb,
                vaddr, is_write, mmu_idx, paddr, page_size, access, true);
    } else if (xtensa_option_bits_enabled(env->config,
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_PROTECTION) |
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_TRANSLATION))) {
        return get_physical_addr_region(env, vaddr, is_write, mmu_idx,
                paddr, page_size, access);
    } else {
        *paddr = vaddr;
        *page_size = TARGET_PAGE_SIZE;
        *access = cacheattr_attr_to_access(
                env->sregs[CACHEATTR] >> ((vaddr & 0xe0000000) >> 27));
        return 0;
    }
}

static void dump_tlb(FILE *f, fprintf_function cpu_fprintf,
        CPUXtensaState *env, bool dtlb)
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
            sz >>= 20;
            sz_text = "MB";
        } else {
            sz >>= 10;
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
                    cpu_fprintf(f, "Way %u (%d %s)\n", wi, sz, sz_text);
                    cpu_fprintf(f,
                            "\tVaddr       Paddr       ASID  Attr RWX Cache\n"
                            "\t----------  ----------  ----  ---- --- -------\n");
                }
                cpu_fprintf(f,
                        "\t0x%08x  0x%08x  0x%02x  0x%02x %c%c%c %-7s\n",
                        entry->vaddr,
                        entry->paddr,
                        entry->asid,
                        entry->attr,
                        (access & PAGE_READ) ? 'R' : '-',
                        (access & PAGE_WRITE) ? 'W' : '-',
                        (access & PAGE_EXEC) ? 'X' : '-',
                        cache_text[cache_idx] ? cache_text[cache_idx] :
                            "Invalid");
            }
        }
    }
}

void dump_mmu(FILE *f, fprintf_function cpu_fprintf, CPUXtensaState *env)
{
    if (xtensa_option_bits_enabled(env->config,
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_PROTECTION) |
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_TRANSLATION) |
                XTENSA_OPTION_BIT(XTENSA_OPTION_MMU))) {

        cpu_fprintf(f, "ITLB:\n");
        dump_tlb(f, cpu_fprintf, env, false);
        cpu_fprintf(f, "\nDTLB:\n");
        dump_tlb(f, cpu_fprintf, env, true);
    } else {
        cpu_fprintf(f, "No TLB for this CPU core\n");
    }
}
