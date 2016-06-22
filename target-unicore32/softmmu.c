/*
 * Softmmu related functions
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or any later version.
 * See the COPYING file in the top-level directory.
 */
#ifdef CONFIG_USER_ONLY
#error This file only exist under softmmu circumstance
#endif

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"

#undef DEBUG_UC32

#ifdef DEBUG_UC32
#define DPRINTF(fmt, ...) printf("%s: " fmt , __func__, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define SUPERPAGE_SIZE             (1 << 22)
#define UC32_PAGETABLE_READ        (1 << 8)
#define UC32_PAGETABLE_WRITE       (1 << 7)
#define UC32_PAGETABLE_EXEC        (1 << 6)
#define UC32_PAGETABLE_EXIST       (1 << 2)
#define PAGETABLE_TYPE(x)          ((x) & 3)


/* Map CPU modes onto saved register banks.  */
static inline int bank_number(CPUUniCore32State *env, int mode)
{
    UniCore32CPU *cpu = uc32_env_get_cpu(env);

    switch (mode) {
    case ASR_MODE_USER:
    case ASR_MODE_SUSR:
        return 0;
    case ASR_MODE_PRIV:
        return 1;
    case ASR_MODE_TRAP:
        return 2;
    case ASR_MODE_EXTN:
        return 3;
    case ASR_MODE_INTR:
        return 4;
    }
    cpu_abort(CPU(cpu), "Bad mode %x\n", mode);
    return -1;
}

void switch_mode(CPUUniCore32State *env, int mode)
{
    int old_mode;
    int i;

    old_mode = env->uncached_asr & ASR_M;
    if (mode == old_mode) {
        return;
    }

    i = bank_number(env, old_mode);
    env->banked_r29[i] = env->regs[29];
    env->banked_r30[i] = env->regs[30];
    env->banked_bsr[i] = env->bsr;

    i = bank_number(env, mode);
    env->regs[29] = env->banked_r29[i];
    env->regs[30] = env->banked_r30[i];
    env->bsr = env->banked_bsr[i];
}

/* Handle a CPU exception.  */
void uc32_cpu_do_interrupt(CPUState *cs)
{
    UniCore32CPU *cpu = UNICORE32_CPU(cs);
    CPUUniCore32State *env = &cpu->env;
    uint32_t addr;
    int new_mode;

    switch (cs->exception_index) {
    case UC32_EXCP_PRIV:
        new_mode = ASR_MODE_PRIV;
        addr = 0x08;
        break;
    case UC32_EXCP_ITRAP:
        DPRINTF("itrap happened at %x\n", env->regs[31]);
        new_mode = ASR_MODE_TRAP;
        addr = 0x0c;
        break;
    case UC32_EXCP_DTRAP:
        DPRINTF("dtrap happened at %x\n", env->regs[31]);
        new_mode = ASR_MODE_TRAP;
        addr = 0x10;
        break;
    case UC32_EXCP_INTR:
        new_mode = ASR_MODE_INTR;
        addr = 0x18;
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
        return;
    }
    /* High vectors.  */
    if (env->cp0.c1_sys & (1 << 13)) {
        addr += 0xffff0000;
    }

    switch_mode(env, new_mode);
    env->bsr = cpu_asr_read(env);
    env->uncached_asr = (env->uncached_asr & ~ASR_M) | new_mode;
    env->uncached_asr |= ASR_I;
    /* The PC already points to the proper instruction.  */
    env->regs[30] = env->regs[31];
    env->regs[31] = addr;
    cs->interrupt_request |= CPU_INTERRUPT_EXITTB;
}

static int get_phys_addr_ucv2(CPUUniCore32State *env, uint32_t address,
        int access_type, int is_user, uint32_t *phys_ptr, int *prot,
        target_ulong *page_size)
{
    UniCore32CPU *cpu = uc32_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    int code;
    uint32_t table;
    uint32_t desc;
    uint32_t phys_addr;

    /* Pagetable walk.  */
    /* Lookup l1 descriptor.  */
    table = env->cp0.c2_base & 0xfffff000;
    table |= (address >> 20) & 0xffc;
    desc = ldl_phys(cs->as, table);
    code = 0;
    switch (PAGETABLE_TYPE(desc)) {
    case 3:
        /* Superpage  */
        if (!(desc & UC32_PAGETABLE_EXIST)) {
            code = 0x0b; /* superpage miss */
            goto do_fault;
        }
        phys_addr = (desc & 0xffc00000) | (address & 0x003fffff);
        *page_size = SUPERPAGE_SIZE;
        break;
    case 0:
        /* Lookup l2 entry.  */
        if (is_user) {
            DPRINTF("PGD address %x, desc %x\n", table, desc);
        }
        if (!(desc & UC32_PAGETABLE_EXIST)) {
            code = 0x05; /* second pagetable miss */
            goto do_fault;
        }
        table = (desc & 0xfffff000) | ((address >> 10) & 0xffc);
        desc = ldl_phys(cs->as, table);
        /* 4k page.  */
        if (is_user) {
            DPRINTF("PTE address %x, desc %x\n", table, desc);
        }
        if (!(desc & UC32_PAGETABLE_EXIST)) {
            code = 0x08; /* page miss */
            goto do_fault;
        }
        switch (PAGETABLE_TYPE(desc)) {
        case 0:
            phys_addr = (desc & 0xfffff000) | (address & 0xfff);
            *page_size = TARGET_PAGE_SIZE;
            break;
        default:
            cpu_abort(CPU(cpu), "wrong page type!");
        }
        break;
    default:
        cpu_abort(CPU(cpu), "wrong page type!");
    }

    *phys_ptr = phys_addr;
    *prot = 0;
    /* Check access permissions.  */
    if (desc & UC32_PAGETABLE_READ) {
        *prot |= PAGE_READ;
    } else {
        if (is_user && (access_type == 0)) {
            code = 0x11; /* access unreadable area */
            goto do_fault;
        }
    }

    if (desc & UC32_PAGETABLE_WRITE) {
        *prot |= PAGE_WRITE;
    } else {
        if (is_user && (access_type == 1)) {
            code = 0x12; /* access unwritable area */
            goto do_fault;
        }
    }

    if (desc & UC32_PAGETABLE_EXEC) {
        *prot |= PAGE_EXEC;
    } else {
        if (is_user && (access_type == 2)) {
            code = 0x13; /* access unexecutable area */
            goto do_fault;
        }
    }

do_fault:
    return code;
}

int uc32_cpu_handle_mmu_fault(CPUState *cs, vaddr address,
                              int access_type, int mmu_idx)
{
    UniCore32CPU *cpu = UNICORE32_CPU(cs);
    CPUUniCore32State *env = &cpu->env;
    uint32_t phys_addr;
    target_ulong page_size;
    int prot;
    int ret, is_user;

    ret = 1;
    is_user = mmu_idx == MMU_USER_IDX;

    if ((env->cp0.c1_sys & 1) == 0) {
        /* MMU disabled.  */
        phys_addr = address;
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        page_size = TARGET_PAGE_SIZE;
        ret = 0;
    } else {
        if ((address & (1 << 31)) || (is_user)) {
            ret = get_phys_addr_ucv2(env, address, access_type, is_user,
                                    &phys_addr, &prot, &page_size);
            if (is_user) {
                DPRINTF("user space access: ret %x, address %" VADDR_PRIx ", "
                        "access_type %x, phys_addr %x, prot %x\n",
                        ret, address, access_type, phys_addr, prot);
            }
        } else {
            /*IO memory */
            phys_addr = address | (1 << 31);
            prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            page_size = TARGET_PAGE_SIZE;
            ret = 0;
        }
    }

    if (ret == 0) {
        /* Map a single page.  */
        phys_addr &= TARGET_PAGE_MASK;
        address &= TARGET_PAGE_MASK;
        tlb_set_page(cs, address, phys_addr, prot, mmu_idx, page_size);
        return 0;
    }

    env->cp0.c3_faultstatus = ret;
    env->cp0.c4_faultaddr = address;
    if (access_type == 2) {
        cs->exception_index = UC32_EXCP_ITRAP;
    } else {
        cs->exception_index = UC32_EXCP_DTRAP;
    }
    return ret;
}

hwaddr uc32_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    UniCore32CPU *cpu = UNICORE32_CPU(cs);

    cpu_abort(CPU(cpu), "%s not supported yet\n", __func__);
    return addr;
}
