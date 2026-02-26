/*
 * QEMU monitor for RISC-V
 *
 * Copyright (c) 2019 Bin Meng <bmeng.cn@gmail.com>
 *
 * RISC-V specific monitor commands implementation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "cpu_bits.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "monitor/hmp-target.h"
#include "system/memory.h"

#ifdef TARGET_RISCV64
#define PTE_HEADER_FIELDS       "vaddr            paddr            "\
                                "size             attr\n"
#define PTE_HEADER_DELIMITER    "---------------- ---------------- "\
                                "---------------- -------\n"
#else
#define PTE_HEADER_FIELDS       "vaddr    paddr            size     attr\n"
#define PTE_HEADER_DELIMITER    "-------- ---------------- -------- -------\n"
#endif

/* Perform linear address sign extension */
static target_ulong addr_canonical(int va_bits, target_ulong addr)
{
#ifdef TARGET_RISCV64
    if (addr & (1UL << (va_bits - 1))) {
        addr |= (hwaddr)-(1L << va_bits);
    }
#endif

    return addr;
}

static void print_pte_header(Monitor *mon)
{
    monitor_printf(mon, PTE_HEADER_FIELDS);
    monitor_printf(mon, PTE_HEADER_DELIMITER);
}

static void print_pte(Monitor *mon, int va_bits, target_ulong vaddr,
                      hwaddr paddr, target_ulong size, int attr)
{
    /* sanity check on vaddr */
    if (vaddr >= (1UL << va_bits)) {
        return;
    }

    if (!size) {
        return;
    }

    monitor_printf(mon, TARGET_FMT_lx " " HWADDR_FMT_plx " " TARGET_FMT_lx
                   " %c%c%c%c%c%c%c\n",
                   addr_canonical(va_bits, vaddr),
                   paddr, size,
                   attr & PTE_R ? 'r' : '-',
                   attr & PTE_W ? 'w' : '-',
                   attr & PTE_X ? 'x' : '-',
                   attr & PTE_U ? 'u' : '-',
                   attr & PTE_G ? 'g' : '-',
                   attr & PTE_A ? 'a' : '-',
                   attr & PTE_D ? 'd' : '-');
}

static void walk_pte(Monitor *mon, AddressSpace *as,
                     hwaddr base, target_ulong start,
                     int level, int ptidxbits, int ptesize, int va_bits,
                     target_ulong *vbase, hwaddr *pbase, hwaddr *last_paddr,
                     target_ulong *last_size, int *last_attr)
{
    const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    hwaddr pte_addr;
    hwaddr paddr;
    target_ulong last_start = -1;
    target_ulong pgsize;
    target_ulong pte;
    int ptshift;
    int attr;
    int idx;

    if (level < 0) {
        return;
    }

    ptshift = level * ptidxbits;
    pgsize = 1UL << (PGSHIFT + ptshift);

    for (idx = 0; idx < (1UL << ptidxbits); idx++) {
        pte_addr = base + idx * ptesize;
        address_space_read(as, pte_addr, attrs, &pte, ptesize);

        paddr = (hwaddr)(pte >> PTE_PPN_SHIFT) << PGSHIFT;
        attr = pte & 0xff;

        /* PTE has to be valid */
        if (attr & PTE_V) {
            if (attr & (PTE_R | PTE_W | PTE_X)) {
                /*
                 * A leaf PTE has been found
                 *
                 * If current PTE's permission bits differ from the last one,
                 * or the current PTE breaks up a contiguous virtual or
                 * physical mapping, address block together with the last one,
                 * print out the last contiguous mapped block details.
                 */
                if ((*last_attr != attr) ||
                    (*last_paddr + *last_size != paddr) ||
                    (last_start + *last_size != start)) {
                    print_pte(mon, va_bits, *vbase, *pbase,
                              *last_paddr + *last_size - *pbase, *last_attr);

                    *vbase = start;
                    *pbase = paddr;
                    *last_attr = attr;
                }

                last_start = start;
                *last_paddr = paddr;
                *last_size = pgsize;
            } else {
                /* pointer to the next level of the page table */
                walk_pte(mon, as, paddr, start, level - 1, ptidxbits, ptesize,
                         va_bits, vbase, pbase, last_paddr,
                         last_size, last_attr);
            }
        }

        start += pgsize;
    }

}

static void mem_info_svxx(Monitor *mon, CPUArchState *env)
{
    AddressSpace *as = env_cpu(env)->as;
    int levels, ptidxbits, ptesize, vm, va_bits;
    hwaddr base;
    target_ulong vbase;
    hwaddr pbase;
    hwaddr last_paddr;
    target_ulong last_size;
    int last_attr;

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        base = (hwaddr)get_field(env->satp, SATP32_PPN) << PGSHIFT;
        vm = get_field(env->satp, SATP32_MODE);
    } else {
        base = (hwaddr)get_field(env->satp, SATP64_PPN) << PGSHIFT;
        vm = get_field(env->satp, SATP64_MODE);
    }

    switch (vm) {
    case VM_1_10_SV32:
        levels = 2;
        ptidxbits = 10;
        ptesize = 4;
        break;
    case VM_1_10_SV39:
        levels = 3;
        ptidxbits = 9;
        ptesize = 8;
        break;
    case VM_1_10_SV48:
        levels = 4;
        ptidxbits = 9;
        ptesize = 8;
        break;
    case VM_1_10_SV57:
        levels = 5;
        ptidxbits = 9;
        ptesize = 8;
        break;
    default:
        g_assert_not_reached();
    }

    /* calculate virtual address bits */
    va_bits = PGSHIFT + levels * ptidxbits;

    /* print header */
    print_pte_header(mon);

    vbase = -1;
    pbase = -1;
    last_paddr = -1;
    last_size = 0;
    last_attr = 0;

    /* walk page tables, starting from address 0 */
    walk_pte(mon, as, base, 0, levels - 1, ptidxbits, ptesize, va_bits,
             &vbase, &pbase, &last_paddr, &last_size, &last_attr);

    /* don't forget the last one */
    print_pte(mon, va_bits, vbase, pbase,
              last_paddr + last_size - pbase, last_attr);
}

void hmp_info_mem(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;

    env = mon_get_cpu_env(mon);
    if (!env) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    if (!riscv_cpu_cfg(env)->mmu) {
        monitor_printf(mon, "S-mode MMU unavailable\n");
        return;
    }

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        if (!(env->satp & SATP32_MODE)) {
            monitor_printf(mon, "No translation or protection\n");
            return;
        }
    } else {
        if (!(env->satp & SATP64_MODE)) {
            monitor_printf(mon, "No translation or protection\n");
            return;
        }
    }

    mem_info_svxx(mon, env);
}

static bool reg_is_ulong_integer(CPURISCVState *env, const char *name,
                                 target_ulong *val, bool is_gprh)
{
    const char * const *reg_names;
    target_ulong *vals;

    if (is_gprh) {
        reg_names = riscv_int_regnamesh;
        vals = env->gprh;
    } else {
        reg_names = riscv_int_regnames;
        vals = env->gpr;
    }

    for (int i = 0; i < 32; i++) {
        g_auto(GStrv) reg_name = g_strsplit(reg_names[i], "/", 2);

        g_assert(reg_name[0]);
        g_assert(reg_name[1]);

        if (g_ascii_strcasecmp(reg_name[0], name) == 0 ||
            g_ascii_strcasecmp(reg_name[1], name) == 0) {
            *val = vals[i];
            return true;
        }
    }

    return false;
}

static bool reg_is_u64_fpu(CPURISCVState *env, const char *name, uint64_t *val)
{
    if (qemu_tolower(name[0]) != 'f') {
        return false;
    }

    for (int i = 0; i < 32; i++) {
        g_auto(GStrv) reg_name = g_strsplit(riscv_fpr_regnames[i], "/", 2);

        g_assert(reg_name[0]);
        g_assert(reg_name[1]);

        if (g_ascii_strcasecmp(reg_name[0], name) == 0 ||
            g_ascii_strcasecmp(reg_name[1], name) == 0) {
            *val = env->fpr[i];
            return true;
        }
    }

    return false;
}

static bool reg_is_vreg(const char *name)
{
    if (qemu_tolower(name[0]) != 'v' || strlen(name) > 3) {
        return false;
    }

    for (int i = 0; i < 32; i++) {
        if (strcasecmp(name, riscv_rvv_regnames[i]) == 0) {
            return true;
        }
    }

    return false;
}

int target_get_monitor_def(CPUState *cs, const char *name, uint64_t *pval)
{
    CPURISCVState *env = &RISCV_CPU(cs)->env;
    target_ulong val = 0;
    uint64_t val64 = 0;
    int i;

    if (reg_is_ulong_integer(env, name, &val, false) ||
        reg_is_ulong_integer(env, name, &val, true)) {
        *pval = val;
        return 0;
    }

    if (reg_is_u64_fpu(env, name, &val64)) {
        *pval = val64;
        return 0;
    }

    if (reg_is_vreg(name)) {
        if (!riscv_cpu_cfg(env)->ext_zve32x) {
            return -EINVAL;
        }

        qemu_printf("Unable to print the value of vector "
                    "vreg '%s' from this API\n", name);

        /*
         * We're returning 0 because returning -EINVAL triggers
         * an 'unknown register' message in exp_unary() later,
         * which feels ankward after our own error message.
         */
        *pval = 0;
        return 0;
    }

    for (i = 0; i < ARRAY_SIZE(csr_ops); i++) {
        RISCVException res;
        int csrno = i;

        /*
         * Early skip when possible since we're going
         * through a lot of NULL entries.
         */
        if (csr_ops[csrno].predicate == NULL) {
            continue;
        }

        if (strcasecmp(csr_ops[csrno].name, name) != 0) {
            continue;
        }

        res = riscv_csrrw_debug(env, csrno, &val, 0, 0);

        /*
         * Rely on the smode, hmode, etc, predicates within csr.c
         * to do the filtering of the registers that are present.
         */
        if (res == RISCV_EXCP_NONE) {
            *pval = val;
            return 0;
        }
    }

    return -EINVAL;
}
