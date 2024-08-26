/*
 * Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "sysemu/cpus.h"
#include "internal.h"
#include "exec/exec-all.h"
#include "hex_mmu.h"
#include "cpu_helper.h"
#include "macros.h"
#include "sys_macros.h"
#include "reg_fields.h"

#define GET_TLB_FIELD(ENTRY, FIELD)                               \
    ((uint64_t)fEXTRACTU_BITS(ENTRY, reg_field_info[FIELD].width, \
                              reg_field_info[FIELD].offset))

/*
 * PPD (physical page descriptor) is formed by putting the PTE_PA35 field
 * in the MSB of the PPD
 */
#define GET_PPD(ENTRY) \
    ((GET_TLB_FIELD((ENTRY), PTE_PPD) | \
     (GET_TLB_FIELD((ENTRY), PTE_PA35) << reg_field_info[PTE_PPD].width)))

#define NO_ASID      (1 << 8)

typedef enum {
    PGSIZE_4K,
    PGSIZE_16K,
    PGSIZE_64K,
    PGSIZE_256K,
    PGSIZE_1M,
    PGSIZE_4M,
    PGSIZE_16M,
    PGSIZE_64M,
    PGSIZE_256M,
    PGSIZE_1G,
    NUM_PGSIZE_TYPES
} tlb_pgsize_t;

static const char *pgsize_str[NUM_PGSIZE_TYPES] = {
    "4K",
    "16K",
    "64K",
    "256K",
    "1M",
    "4M",
    "16M",
    "64M",
    "256M",
    "1G"
};

#define INVALID_MASK 0xffffffffLL

static const uint64_t encmask_2_mask[] = {
    0x0fffLL,                           /* 4k,   0000 */
    0x3fffLL,                           /* 16k,  0001 */
    0xffffLL,                           /* 64k,  0010 */
    0x3ffffLL,                          /* 256k, 0011 */
    0xfffffLL,                          /* 1m,   0100 */
    0x3fffffLL,                         /* 4m,   0101 */
    0xffffffLL,                         /* 16m,  0110 */
    0x3ffffffLL,                        /* 64m,  0111 */
    0xfffffffLL,                        /* 256m, 1000 */
    0x3fffffffLL,                       /* 1g,   1001 */
    INVALID_MASK,                       /* RSVD, 0111 */
};

static inline int hex_tlb_pgsize(uint64_t entry)
{
    if (entry == 0) {
        qemu_log_mask(CPU_LOG_MMU, "%s: Supplied TLB entry was 0!\n", __func__);
        return 0;
    }
    int size = ctz64(entry);
    g_assert(size < NUM_PGSIZE_TYPES);
    return size;
}

static inline uint32_t hex_tlb_page_size(uint64_t entry)
{
    return 1 << (TARGET_PAGE_BITS + 2 * hex_tlb_pgsize(GET_PPD(entry)));
}

static inline uint64_t hex_tlb_phys_page_num(uint64_t entry)
{
    uint32_t ppd = GET_PPD(entry);
    return ppd >> 1;
}

static inline uint64_t hex_tlb_phys_addr(uint64_t entry)
{
    uint64_t pagemask = encmask_2_mask[hex_tlb_pgsize(entry)];
    uint64_t pagenum = hex_tlb_phys_page_num(entry);
    uint64_t PA = (pagenum << TARGET_PAGE_BITS) & (~pagemask);
    return PA;
}

static inline uint64_t hex_tlb_virt_addr(uint64_t entry)
{
    return GET_TLB_FIELD(entry, PTE_VPN) << TARGET_PAGE_BITS;
}

static bool hex_dump_mmu_entry(FILE *f, uint64_t entry)
{
    if (GET_TLB_FIELD(entry, PTE_V)) {
        fprintf(f, "0x%016" PRIx64 ": ", entry);
        uint64_t PA = hex_tlb_phys_addr(entry);
        uint64_t VA = hex_tlb_virt_addr(entry);
        fprintf(f, "V:%" PRId64 " G:%" PRId64 " A1:%" PRId64 " A0:%" PRId64,
                GET_TLB_FIELD(entry, PTE_V), GET_TLB_FIELD(entry, PTE_G),
                GET_TLB_FIELD(entry, PTE_ATR1), GET_TLB_FIELD(entry, PTE_ATR0));
        fprintf(f, " ASID:0x%02" PRIx64 " VA:0x%08" PRIx64,
                GET_TLB_FIELD(entry, PTE_ASID), VA);
        fprintf(f,
                " X:%" PRId64 " W:%" PRId64 " R:%" PRId64 " U:%" PRId64
                " C:%" PRId64,
                GET_TLB_FIELD(entry, PTE_X), GET_TLB_FIELD(entry, PTE_W),
                GET_TLB_FIELD(entry, PTE_R), GET_TLB_FIELD(entry, PTE_U),
                GET_TLB_FIELD(entry, PTE_C));
        fprintf(f, " PA:0x%09" PRIx64 " SZ:%s (0x%x)", PA,
                pgsize_str[hex_tlb_pgsize(entry)], hex_tlb_page_size(entry));
        fprintf(f, "\n");
        return true;
    }

    /* Not valid */
    return false;
}

void dump_mmu(CPUHexagonState *env)
{
    int i;

    HexagonCPU *cpu = env_archcpu(env);
    for (i = 0; i < cpu->num_tlbs; i++) {
        uint64_t entry = env->hex_tlb->entries[i];
        if (GET_TLB_FIELD(entry, PTE_V)) {
            qemu_printf("0x%016" PRIx64 ": ", entry);
            uint64_t PA = hex_tlb_phys_addr(entry);
            uint64_t VA = hex_tlb_virt_addr(entry);
            qemu_printf(
                "V:%" PRId64 " G:%" PRId64 " A1:%" PRId64 " A0:%" PRId64,
                GET_TLB_FIELD(entry, PTE_V), GET_TLB_FIELD(entry, PTE_G),
                GET_TLB_FIELD(entry, PTE_ATR1), GET_TLB_FIELD(entry, PTE_ATR0));
            qemu_printf(" ASID:0x%02" PRIx64 " VA:0x%08" PRIx64,
                        GET_TLB_FIELD(entry, PTE_ASID), VA);
            qemu_printf(
                " X:%" PRId64 " W:%" PRId64 " R:%" PRId64 " U:%" PRId64
                " C:%" PRId64,
                GET_TLB_FIELD(entry, PTE_X), GET_TLB_FIELD(entry, PTE_W),
                GET_TLB_FIELD(entry, PTE_R), GET_TLB_FIELD(entry, PTE_U),
                GET_TLB_FIELD(entry, PTE_C));
            qemu_printf(" PA:0x%09" PRIx64 " SZ:%s (0x%x)", PA,
                        pgsize_str[hex_tlb_pgsize(entry)],
                        hex_tlb_page_size(entry));
            qemu_printf("\n");
        }
    }
}

static inline void hex_log_tlbw(uint32_t index, uint64_t entry)
{
    if (qemu_loglevel_mask(CPU_LOG_MMU)) {
        if (qemu_log_enabled()) {
            FILE *logfile = qemu_log_trylock();
            if (logfile) {
                fprintf(logfile, "tlbw[%03d]: ", index);
                if (!hex_dump_mmu_entry(logfile, entry)) {
                    fprintf(logfile, "invalid\n");
                }
                qemu_log_unlock(logfile);
            }
        }
    }
}

void hex_tlbw(CPUHexagonState *env, uint32_t index, uint64_t value)
{
    uint32_t myidx = fTLB_NONPOW2WRAP(fTLB_IDXMASK(index));
    bool old_entry_valid = GET_TLB_FIELD(env->hex_tlb->entries[myidx], PTE_V);
    if (old_entry_valid && hexagon_cpu_mmu_enabled(env)) {
        /* FIXME - Do we have to invalidate everything here? */
        CPUState *cs = env_cpu(env);

        tlb_flush(cs);
    }
    env->hex_tlb->entries[myidx] = (value);
    hex_log_tlbw(myidx, value);
}

void hex_mmu_realize(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    if (cs->cpu_index == 0) {
        env->hex_tlb = g_malloc0(sizeof(CPUHexagonTLBContext));
    } else {
        CPUState *cpu0_s = NULL;
        CPUHexagonState *env0 = NULL;
        CPU_FOREACH(cpu0_s) {
            assert(cpu0_s->cpu_index == 0);
            env0 = &(HEXAGON_CPU(cpu0_s)->env);
            break;
        }
        env->hex_tlb = env0->hex_tlb;
    }
}

void hex_mmu_on(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    qemu_log_mask(CPU_LOG_MMU, "Hexagon MMU turned on!\n");
    tlb_flush(cs);

}

void hex_mmu_off(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    qemu_log_mask(CPU_LOG_MMU, "Hexagon MMU turned off!\n");
    tlb_flush(cs);
}

void hex_mmu_mode_change(CPUHexagonState *env)
{
    qemu_log_mask(CPU_LOG_MMU, "Hexagon mode change!\n");
    CPUState *cs = env_cpu(env);
    tlb_flush(cs);
}

static inline bool hex_tlb_entry_match_noperm(uint64_t entry, uint32_t asid,
                                              target_ulong VA)
{
    if (GET_TLB_FIELD(entry, PTE_V)) {
        if (GET_TLB_FIELD(entry, PTE_G)) {
            /* Global entry - ingnore ASID */
        } else if (asid != NO_ASID) {
            uint32_t tlb_asid = GET_TLB_FIELD(entry, PTE_ASID);
            if (tlb_asid != asid) {
                return false;
            }
        }

        uint64_t page_size = hex_tlb_page_size(entry);
        uint64_t page_start = hex_tlb_virt_addr(entry);
        page_start &= ~(page_size - 1);
        if (page_start <= VA && VA < page_start + page_size) {
            /* FIXME - Anything else we need to check? */
            return true;
        }
    }
    return false;
}

static inline void hex_tlb_entry_get_perm(CPUHexagonState *env, uint64_t entry,
                                          MMUAccessType access_type,
                                          int mmu_idx, int *prot,
                                          int32_t *excp)
{
    g_assert_not_reached();
}

static inline bool hex_tlb_entry_match(CPUHexagonState *env, uint64_t entry,
                                       uint8_t asid, target_ulong VA,
                                       MMUAccessType access_type, hwaddr *PA,
                                       int *prot, int *size, int32_t *excp,
                                       int mmu_idx)
{
    if (hex_tlb_entry_match_noperm(entry, asid, VA)) {
        hex_tlb_entry_get_perm(env, entry, access_type, mmu_idx, prot, excp);
        *PA = hex_tlb_phys_addr(entry);
        *size = hex_tlb_page_size(entry);
        return true;
    }
    return false;
}

bool hex_tlb_find_match(CPUHexagonState *env, target_ulong VA,
                        MMUAccessType access_type, hwaddr *PA, int *prot,
                        int *size, int32_t *excp, int mmu_idx)
{
    *PA = 0;
    *prot = 0;
    *size = 0;
    *excp = 0;
    uint32_t ssr = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SSR);
    uint8_t asid = GET_SSR_FIELD(SSR_ASID, ssr);
    int i;
    HexagonCPU *cpu = env_archcpu(env);
    for (i = 0; i < cpu->num_tlbs; i++) {
        uint64_t entry = env->hex_tlb->entries[i];
        if (hex_tlb_entry_match(env, entry, asid, VA, access_type, PA, prot,
                                size, excp, mmu_idx)) {
            return true;
        }
    }
    return false;
}

static uint32_t hex_tlb_lookup_by_asid(CPUHexagonState *env, uint32_t asid,
                                       uint32_t VA)
{
    g_assert_not_reached();
}

/* Called from tlbp instruction */
uint32_t hex_tlb_lookup(CPUHexagonState *env, uint32_t ssr, uint32_t VA)
{
    return hex_tlb_lookup_by_asid(env, GET_SSR_FIELD(SSR_ASID, ssr), VA);
}

static bool hex_tlb_is_match(CPUHexagonState *env,
                             uint64_t entry1, uint64_t entry2,
                             bool consider_gbit)
{
    bool valid1 = GET_TLB_FIELD(entry1, PTE_V);
    bool valid2 = GET_TLB_FIELD(entry2, PTE_V);
    uint64_t size1 = hex_tlb_page_size(entry1);
    uint64_t vaddr1 = hex_tlb_virt_addr(entry1);
    vaddr1 &= ~(size1 - 1);
    uint64_t size2 = hex_tlb_page_size(entry2);
    uint64_t vaddr2 = hex_tlb_virt_addr(entry2);
    vaddr2 &= ~(size2 - 1);
    int asid1 = GET_TLB_FIELD(entry1, PTE_ASID);
    int asid2 = GET_TLB_FIELD(entry2, PTE_ASID);
    bool gbit1 = GET_TLB_FIELD(entry1, PTE_G);
    bool gbit2 = GET_TLB_FIELD(entry2, PTE_G);

    if (!valid1 || !valid2) {
        return false;
    }

    if (((vaddr1 <= vaddr2) && (vaddr2 < (vaddr1 + size1))) ||
        ((vaddr2 <= vaddr1) && (vaddr1 < (vaddr2 + size2)))) {
        if (asid1 == asid2) {
            return true;
        }
        if ((consider_gbit && gbit1) || gbit2) {
            return true;
        }
    }
    return false;
}

/*
 * Return codes:
 * 0 or positive             index of match
 * -1                        multiple matches
 * -2                        no match
 */
int hex_tlb_check_overlap(CPUHexagonState *env, uint64_t entry, uint64_t index)
{
    int matches = 0;
    int last_match = 0;
    int i;

    HexagonCPU *cpu = env_archcpu(env);
    for (i = 0; i < cpu->num_tlbs; i++) {
        if (hex_tlb_is_match(env, entry, env->hex_tlb->entries[i], false)) {
            matches++;
            last_match = i;
        }
    }

    if (matches == 1) {
        return last_match;
    }
    if (matches == 0) {
        return -2;
    }
    return -1;
}

static inline void print_thread(const char *str, CPUState *cs)
{
    g_assert(bql_locked());
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *thread = &cpu->env;
    bool is_stopped = cpu_is_stopped(cs);
    int exe_mode = get_exe_mode(thread);
    hex_lock_state_t lock_state = thread->tlb_lock_state;
    qemu_log_mask(CPU_LOG_MMU,
           "%s: threadId = %d: %s, exe_mode = %s, tlb_lock_state = %s\n",
           str,
           thread->threadId,
           is_stopped ? "stopped" : "running",
           exe_mode == HEX_EXE_MODE_OFF ? "off" :
           exe_mode == HEX_EXE_MODE_RUN ? "run" :
           exe_mode == HEX_EXE_MODE_WAIT ? "wait" :
           exe_mode == HEX_EXE_MODE_DEBUG ? "debug" :
           "unknown",
           lock_state == HEX_LOCK_UNLOCKED ? "unlocked" :
           lock_state == HEX_LOCK_WAITING ? "waiting" :
           lock_state == HEX_LOCK_OWNER ? "owner" :
           "unknown");
}

static inline void print_thread_states(const char *str)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        print_thread(str, cs);
    }
}

/*
 * A tlb_lock is taken with either a tlbfault or an explicit
 * tlblock insn.  The insn tlblock only advances the PC
 * after the lock is acquired, similar to k0lock.
 */
void hex_tlb_lock(CPUHexagonState *env)
{
    qemu_log_mask(CPU_LOG_MMU, "hex_tlb_lock: %d\n", env->threadId);
    BQL_LOCK_GUARD();
    g_assert((env->tlb_lock_count == 0) || (env->tlb_lock_count == 1));

    uint32_t syscfg = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SYSCFG);
    uint8_t tlb_lock = GET_SYSCFG_FIELD(SYSCFG_TLBLOCK, syscfg);
    if (tlb_lock) {
        if (env->tlb_lock_state == HEX_LOCK_QUEUED) {
            env->next_PC += 4;
            env->tlb_lock_count++;
            env->tlb_lock_state = HEX_LOCK_OWNER;
            SET_SYSCFG_FIELD(env, SYSCFG_TLBLOCK, 1);
            return;
        }
        if (env->tlb_lock_state == HEX_LOCK_OWNER) {
            qemu_log_mask(CPU_LOG_MMU | LOG_GUEST_ERROR,
                          "Double tlblock at PC: 0x%x, thread may hang\n",
                          env->next_PC);
            env->next_PC += 4;
            CPUState *cs = env_cpu(env);
            cpu_interrupt(cs, CPU_INTERRUPT_HALT);
            return;
        }
        env->tlb_lock_state = HEX_LOCK_WAITING;
        CPUState *cs = env_cpu(env);
        cpu_interrupt(cs, CPU_INTERRUPT_HALT);
    } else {
        env->next_PC += 4;
        env->tlb_lock_count++;
        env->tlb_lock_state = HEX_LOCK_OWNER;
        SET_SYSCFG_FIELD(env, SYSCFG_TLBLOCK, 1);
    }

    if (qemu_loglevel_mask(CPU_LOG_MMU)) {
        qemu_log_mask(CPU_LOG_MMU, "Threads after hex_tlb_lock:\n");
        print_thread_states("\tThread");
    }
}

void hex_tlb_unlock(CPUHexagonState *env)
{
    BQL_LOCK_GUARD();
    g_assert((env->tlb_lock_count == 0) || (env->tlb_lock_count == 1));

    /* Nothing to do if the TLB isn't locked by this thread */
    uint32_t syscfg = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SYSCFG);
    uint8_t tlb_lock = GET_SYSCFG_FIELD(SYSCFG_TLBLOCK, syscfg);
    if ((tlb_lock == 0) ||
        (env->tlb_lock_state != HEX_LOCK_OWNER)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thread %d attempted to tlbunlock without having the "
                      "lock, tlb_lock state = %d\n",
                      env->threadId, env->tlb_lock_state);
        g_assert(env->tlb_lock_state != HEX_LOCK_WAITING);
        return;
    }

    env->tlb_lock_count--;
    env->tlb_lock_state = HEX_LOCK_UNLOCKED;
    SET_SYSCFG_FIELD(env, SYSCFG_TLBLOCK, 0);

    /* Look for a thread to unlock */
    unsigned int this_threadId = env->threadId;
    CPUHexagonState *unlock_thread = NULL;
    CPUState *cs;
    CPU_FOREACH(cs) {
        HexagonCPU *cpu = HEXAGON_CPU(cs);
        CPUHexagonState *thread = &cpu->env;

        /*
         * The hardware implements round-robin fairness, so we look for threads
         * starting at env->threadId + 1 and incrementing modulo the number of
         * threads.
         *
         * To implement this, we check if thread is a earlier in the modulo
         * sequence than unlock_thread.
         *     if unlock thread is higher than this thread
         *         thread must be between this thread and unlock_thread
         *     else
         *         thread higher than this thread is ahead of unlock_thread
         *         thread must be lower then unlock thread
         */
        if (thread->tlb_lock_state == HEX_LOCK_WAITING) {
            if (!unlock_thread) {
                unlock_thread = thread;
            } else if (unlock_thread->threadId > this_threadId) {
                if (this_threadId < thread->threadId &&
                    thread->threadId < unlock_thread->threadId) {
                    unlock_thread = thread;
                }
            } else {
                if (thread->threadId > this_threadId) {
                    unlock_thread = thread;
                }
                if (thread->threadId < unlock_thread->threadId) {
                    unlock_thread = thread;
                }
            }
        }
    }
    if (unlock_thread) {
        cs = env_cpu(unlock_thread);
        print_thread("\tWaiting thread found", cs);
        unlock_thread->tlb_lock_state = HEX_LOCK_QUEUED;
        SET_SYSCFG_FIELD(unlock_thread, SYSCFG_TLBLOCK, 1);
        cpu_interrupt(cs, CPU_INTERRUPT_TLB_UNLOCK);
    }

    if (qemu_loglevel_mask(CPU_LOG_MMU)) {
        qemu_log_mask(CPU_LOG_MMU, "Threads after hex_tlb_unlock:\n");
        print_thread_states("\tThread");
    }

}

