/*
 * QEMU RISC-V PMP (Physical Memory Protection)
 *
 * Author: Daire McNamara, daire.mcnamara@emdalo.com
 *         Ivan Griffin, ivan.griffin@emdalo.com
 *
 * This provides a RISC-V Physical Memory Protection implementation
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
#include "qemu/log.h"
#include "qapi/error.h"
#include "cpu.h"
#include "trace.h"
#include "exec/exec-all.h"
#include "exec/page-protection.h"

static bool pmp_write_cfg(CPURISCVState *env, uint32_t addr_index,
                          uint8_t val);
static uint8_t pmp_read_cfg(CPURISCVState *env, uint32_t addr_index);

/*
 * Accessor method to extract address matching type 'a field' from cfg reg
 */
static inline uint8_t pmp_get_a_field(uint8_t cfg)
{
    uint8_t a = cfg >> 3;
    return a & 0x3;
}

/*
 * Check whether a PMP is locked or not.
 */
static inline int pmp_is_locked(CPURISCVState *env, uint32_t pmp_index)
{
    /* mseccfg.RLB is set */
    if (MSECCFG_RLB_ISSET(env)) {
        return 0;
    }

    if (env->pmp_state.pmp[pmp_index].cfg_reg & PMP_LOCK) {
        return 1;
    }

    /* Top PMP has no 'next' to check */
    if ((pmp_index + 1u) >= MAX_RISCV_PMPS) {
        return 0;
    }

    return 0;
}

/*
 * Count the number of active rules.
 */
uint32_t pmp_get_num_rules(CPURISCVState *env)
{
     return env->pmp_state.num_rules;
}

/*
 * Accessor to get the cfg reg for a specific PMP/HART
 */
static inline uint8_t pmp_read_cfg(CPURISCVState *env, uint32_t pmp_index)
{
    if (pmp_index < MAX_RISCV_PMPS) {
        return env->pmp_state.pmp[pmp_index].cfg_reg;
    }

    return 0;
}


/*
 * Accessor to set the cfg reg for a specific PMP/HART
 * Bounds checks and relevant lock bit.
 */
static bool pmp_write_cfg(CPURISCVState *env, uint32_t pmp_index, uint8_t val)
{
    if (pmp_index < MAX_RISCV_PMPS) {
        bool locked = true;

        if (riscv_cpu_cfg(env)->ext_smepmp) {
            /* mseccfg.RLB is set */
            if (MSECCFG_RLB_ISSET(env)) {
                locked = false;
            }

            /* mseccfg.MML is not set */
            if (!MSECCFG_MML_ISSET(env) && !pmp_is_locked(env, pmp_index)) {
                locked = false;
            }

            /* mseccfg.MML is set */
            if (MSECCFG_MML_ISSET(env)) {
                /* not adding execute bit */
                if ((val & PMP_LOCK) != 0 && (val & PMP_EXEC) != PMP_EXEC) {
                    locked = false;
                }
                /* shared region and not adding X bit */
                if ((val & PMP_LOCK) != PMP_LOCK &&
                    (val & 0x7) != (PMP_WRITE | PMP_EXEC)) {
                    locked = false;
                }
            }
        } else {
            if (!pmp_is_locked(env, pmp_index)) {
                locked = false;
            }
        }

        if (locked) {
            qemu_log_mask(LOG_GUEST_ERROR, "ignoring pmpcfg write - locked\n");
        } else if (env->pmp_state.pmp[pmp_index].cfg_reg != val) {
            /* If !mseccfg.MML then ignore writes with encoding RW=01 */
            if ((val & PMP_WRITE) && !(val & PMP_READ) &&
                !MSECCFG_MML_ISSET(env)) {
                return false;
            }
            env->pmp_state.pmp[pmp_index].cfg_reg = val;
            pmp_update_rule_addr(env, pmp_index);
            return true;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring pmpcfg write - out of bounds\n");
    }

    return false;
}

void pmp_unlock_entries(CPURISCVState *env)
{
    uint32_t pmp_num = pmp_get_num_rules(env);
    int i;

    for (i = 0; i < pmp_num; i++) {
        env->pmp_state.pmp[i].cfg_reg &= ~(PMP_LOCK | PMP_AMATCH);
    }
}

static void pmp_decode_napot(hwaddr a, hwaddr *sa, hwaddr *ea)
{
    /*
     * aaaa...aaa0   8-byte NAPOT range
     * aaaa...aa01   16-byte NAPOT range
     * aaaa...a011   32-byte NAPOT range
     * ...
     * aa01...1111   2^XLEN-byte NAPOT range
     * a011...1111   2^(XLEN+1)-byte NAPOT range
     * 0111...1111   2^(XLEN+2)-byte NAPOT range
     * 1111...1111   Reserved
     */
    a = (a << 2) | 0x3;
    *sa = a & (a + 1);
    *ea = a | (a + 1);
}

void pmp_update_rule_addr(CPURISCVState *env, uint32_t pmp_index)
{
    uint8_t this_cfg = env->pmp_state.pmp[pmp_index].cfg_reg;
    target_ulong this_addr = env->pmp_state.pmp[pmp_index].addr_reg;
    target_ulong prev_addr = 0u;
    hwaddr sa = 0u;
    hwaddr ea = 0u;

    if (pmp_index >= 1u) {
        prev_addr = env->pmp_state.pmp[pmp_index - 1].addr_reg;
    }

    switch (pmp_get_a_field(this_cfg)) {
    case PMP_AMATCH_OFF:
        sa = 0u;
        ea = -1;
        break;

    case PMP_AMATCH_TOR:
        sa = prev_addr << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = (this_addr << 2) - 1u;
        if (sa > ea) {
            sa = ea = 0u;
        }
        break;

    case PMP_AMATCH_NA4:
        sa = this_addr << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = (sa + 4u) - 1u;
        break;

    case PMP_AMATCH_NAPOT:
        pmp_decode_napot(this_addr, &sa, &ea);
        break;

    default:
        sa = 0u;
        ea = 0u;
        break;
    }

    env->pmp_state.addr[pmp_index].sa = sa;
    env->pmp_state.addr[pmp_index].ea = ea;
}

void pmp_update_rule_nums(CPURISCVState *env)
{
    int i;

    env->pmp_state.num_rules = 0;
    for (i = 0; i < MAX_RISCV_PMPS; i++) {
        const uint8_t a_field =
            pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg);
        if (PMP_AMATCH_OFF != a_field) {
            env->pmp_state.num_rules++;
        }
    }
}

static int pmp_is_in_range(CPURISCVState *env, int pmp_index, hwaddr addr)
{
    int result = 0;

    if ((addr >= env->pmp_state.addr[pmp_index].sa) &&
        (addr <= env->pmp_state.addr[pmp_index].ea)) {
        result = 1;
    } else {
        result = 0;
    }

    return result;
}

/*
 * Check if the address has required RWX privs when no PMP entry is matched.
 */
static bool pmp_hart_has_privs_default(CPURISCVState *env, pmp_priv_t privs,
                                       pmp_priv_t *allowed_privs,
                                       target_ulong mode)
{
    bool ret;

    if (MSECCFG_MMWP_ISSET(env)) {
        /*
         * The Machine Mode Whitelist Policy (mseccfg.MMWP) is set
         * so we default to deny all, even for M-mode.
         */
        *allowed_privs = 0;
        return false;
    } else if (MSECCFG_MML_ISSET(env)) {
        /*
         * The Machine Mode Lockdown (mseccfg.MML) bit is set
         * so we can only execute code in M-mode with an applicable
         * rule. Other modes are disabled.
         */
        if (mode == PRV_M && !(privs & PMP_EXEC)) {
            ret = true;
            *allowed_privs = PMP_READ | PMP_WRITE;
        } else {
            ret = false;
            *allowed_privs = 0;
        }

        return ret;
    }

    if (!riscv_cpu_cfg(env)->pmp || (mode == PRV_M)) {
        /*
         * Privileged spec v1.10 states if HW doesn't implement any PMP entry
         * or no PMP entry matches an M-Mode access, the access succeeds.
         */
        ret = true;
        *allowed_privs = PMP_READ | PMP_WRITE | PMP_EXEC;
    } else {
        /*
         * Other modes are not allowed to succeed if they don't * match a rule,
         * but there are rules. We've checked for no rule earlier in this
         * function.
         */
        ret = false;
        *allowed_privs = 0;
    }

    return ret;
}


/*
 * Public Interface
 */

/*
 * Check if the address has required RWX privs to complete desired operation
 * Return true if a pmp rule match or default match
 * Return false if no match
 */
bool pmp_hart_has_privs(CPURISCVState *env, hwaddr addr,
                        target_ulong size, pmp_priv_t privs,
                        pmp_priv_t *allowed_privs, target_ulong mode)
{
    int i = 0;
    int pmp_size = 0;
    hwaddr s = 0;
    hwaddr e = 0;

    /* Short cut if no rules */
    if (0 == pmp_get_num_rules(env)) {
        return pmp_hart_has_privs_default(env, privs, allowed_privs, mode);
    }

    if (size == 0) {
        if (riscv_cpu_cfg(env)->mmu) {
            /*
             * If size is unknown (0), assume that all bytes
             * from addr to the end of the page will be accessed.
             */
            pmp_size = -(addr | TARGET_PAGE_MASK);
        } else {
            pmp_size = 2 << riscv_cpu_mxl(env);
        }
    } else {
        pmp_size = size;
    }

    /*
     * 1.10 draft priv spec states there is an implicit order
     * from low to high
     */
    for (i = 0; i < MAX_RISCV_PMPS; i++) {
        s = pmp_is_in_range(env, i, addr);
        e = pmp_is_in_range(env, i, addr + pmp_size - 1);

        /* partially inside */
        if ((s + e) == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "pmp violation - access is partially inside\n");
            *allowed_privs = 0;
            return false;
        }

        /* fully inside */
        const uint8_t a_field =
            pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg);

        /*
         * Convert the PMP permissions to match the truth table in the
         * Smepmp spec.
         */
        const uint8_t smepmp_operation =
            ((env->pmp_state.pmp[i].cfg_reg & PMP_LOCK) >> 4) |
            ((env->pmp_state.pmp[i].cfg_reg & PMP_READ) << 2) |
            (env->pmp_state.pmp[i].cfg_reg & PMP_WRITE) |
            ((env->pmp_state.pmp[i].cfg_reg & PMP_EXEC) >> 2);

        if (((s + e) == 2) && (PMP_AMATCH_OFF != a_field)) {
            /*
             * If the PMP entry is not off and the address is in range,
             * do the priv check
             */
            if (!MSECCFG_MML_ISSET(env)) {
                /*
                 * If mseccfg.MML Bit is not set, do pmp priv check
                 * This will always apply to regular PMP.
                 */
                *allowed_privs = PMP_READ | PMP_WRITE | PMP_EXEC;
                if ((mode != PRV_M) || pmp_is_locked(env, i)) {
                    *allowed_privs &= env->pmp_state.pmp[i].cfg_reg;
                }
            } else {
                /*
                 * If mseccfg.MML Bit set, do the enhanced pmp priv check
                 */
                if (mode == PRV_M) {
                    switch (smepmp_operation) {
                    case 0:
                    case 1:
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                        *allowed_privs = 0;
                        break;
                    case 2:
                    case 3:
                    case 14:
                        *allowed_privs = PMP_READ | PMP_WRITE;
                        break;
                    case 9:
                    case 10:
                        *allowed_privs = PMP_EXEC;
                        break;
                    case 11:
                    case 13:
                        *allowed_privs = PMP_READ | PMP_EXEC;
                        break;
                    case 12:
                    case 15:
                        *allowed_privs = PMP_READ;
                        break;
                    default:
                        g_assert_not_reached();
                    }
                } else {
                    switch (smepmp_operation) {
                    case 0:
                    case 8:
                    case 9:
                    case 12:
                    case 13:
                    case 14:
                        *allowed_privs = 0;
                        break;
                    case 1:
                    case 10:
                    case 11:
                        *allowed_privs = PMP_EXEC;
                        break;
                    case 2:
                    case 4:
                    case 15:
                        *allowed_privs = PMP_READ;
                        break;
                    case 3:
                    case 6:
                        *allowed_privs = PMP_READ | PMP_WRITE;
                        break;
                    case 5:
                        *allowed_privs = PMP_READ | PMP_EXEC;
                        break;
                    case 7:
                        *allowed_privs = PMP_READ | PMP_WRITE | PMP_EXEC;
                        break;
                    default:
                        g_assert_not_reached();
                    }
                }
            }

            /*
             * If matching address range was found, the protection bits
             * defined with PMP must be used. We shouldn't fallback on
             * finding default privileges.
             */
            return (privs & *allowed_privs) == privs;
        }
    }

    /* No rule matched */
    return pmp_hart_has_privs_default(env, privs, allowed_privs, mode);
}

/*
 * Handle a write to a pmpcfg CSR
 */
void pmpcfg_csr_write(CPURISCVState *env, uint32_t reg_index,
                      target_ulong val)
{
    int i;
    uint8_t cfg_val;
    int pmpcfg_nums = 2 << riscv_cpu_mxl(env);
    bool modified = false;

    trace_pmpcfg_csr_write(env->mhartid, reg_index, val);

    for (i = 0; i < pmpcfg_nums; i++) {
        cfg_val = (val >> 8 * i)  & 0xff;
        modified |= pmp_write_cfg(env, (reg_index * 4) + i, cfg_val);
    }

    /* If PMP permission of any addr has been changed, flush TLB pages. */
    if (modified) {
        pmp_update_rule_nums(env);
        tlb_flush(env_cpu(env));
    }
}


/*
 * Handle a read from a pmpcfg CSR
 */
target_ulong pmpcfg_csr_read(CPURISCVState *env, uint32_t reg_index)
{
    int i;
    target_ulong cfg_val = 0;
    target_ulong val = 0;
    int pmpcfg_nums = 2 << riscv_cpu_mxl(env);

    for (i = 0; i < pmpcfg_nums; i++) {
        val = pmp_read_cfg(env, (reg_index * 4) + i);
        cfg_val |= (val << (i * 8));
    }
    trace_pmpcfg_csr_read(env->mhartid, reg_index, cfg_val);

    return cfg_val;
}


/*
 * Handle a write to a pmpaddr CSR
 */
void pmpaddr_csr_write(CPURISCVState *env, uint32_t addr_index,
                       target_ulong val)
{
    trace_pmpaddr_csr_write(env->mhartid, addr_index, val);
    bool is_next_cfg_tor = false;

    if (addr_index < MAX_RISCV_PMPS) {
        /*
         * In TOR mode, need to check the lock bit of the next pmp
         * (if there is a next).
         */
        if (addr_index + 1 < MAX_RISCV_PMPS) {
            uint8_t pmp_cfg = env->pmp_state.pmp[addr_index + 1].cfg_reg;
            is_next_cfg_tor = PMP_AMATCH_TOR == pmp_get_a_field(pmp_cfg);

            if (pmp_cfg & PMP_LOCK && is_next_cfg_tor) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "ignoring pmpaddr write - pmpcfg + 1 locked\n");
                return;
            }
        }

        if (!pmp_is_locked(env, addr_index)) {
            if (env->pmp_state.pmp[addr_index].addr_reg != val) {
                env->pmp_state.pmp[addr_index].addr_reg = val;
                pmp_update_rule_addr(env, addr_index);
                if (is_next_cfg_tor) {
                    pmp_update_rule_addr(env, addr_index + 1);
                }
                tlb_flush(env_cpu(env));
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ignoring pmpaddr write - locked\n");
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring pmpaddr write - out of bounds\n");
    }
}


/*
 * Handle a read from a pmpaddr CSR
 */
target_ulong pmpaddr_csr_read(CPURISCVState *env, uint32_t addr_index)
{
    target_ulong val = 0;

    if (addr_index < MAX_RISCV_PMPS) {
        val = env->pmp_state.pmp[addr_index].addr_reg;
        trace_pmpaddr_csr_read(env->mhartid, addr_index, val);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring pmpaddr read - out of bounds\n");
    }

    return val;
}

/*
 * Handle a write to a mseccfg CSR
 */
void mseccfg_csr_write(CPURISCVState *env, target_ulong val)
{
    int i;
    uint64_t mask = MSECCFG_MMWP | MSECCFG_MML;
    /* Update PMM field only if the value is valid according to Zjpm v1.0 */
    if (riscv_cpu_cfg(env)->ext_smmpm &&
        riscv_cpu_mxl(env) == MXL_RV64 &&
        get_field(val, MSECCFG_PMM) != PMM_FIELD_RESERVED) {
        mask |= MSECCFG_PMM;
    }

    trace_mseccfg_csr_write(env->mhartid, val);

    /* RLB cannot be enabled if it's already 0 and if any regions are locked */
    if (!MSECCFG_RLB_ISSET(env)) {
        for (i = 0; i < MAX_RISCV_PMPS; i++) {
            if (pmp_is_locked(env, i)) {
                val &= ~MSECCFG_RLB;
                break;
            }
        }
    }

    if (riscv_cpu_cfg(env)->ext_smepmp) {
        /* Sticky bits */
        val |= (env->mseccfg & mask);
        if ((val ^ env->mseccfg) & mask) {
            tlb_flush(env_cpu(env));
        }
    } else {
        mask |= MSECCFG_RLB;
        val &= ~(mask);
    }

    /* M-mode forward cfi to be enabled if cfi extension is implemented */
    if (env_archcpu(env)->cfg.ext_zicfilp) {
        val |= (val & MSECCFG_MLPE);
    }

    env->mseccfg = val;
}

/*
 * Handle a read from a mseccfg CSR
 */
target_ulong mseccfg_csr_read(CPURISCVState *env)
{
    trace_mseccfg_csr_read(env->mhartid, env->mseccfg);
    return env->mseccfg;
}

/*
 * Calculate the TLB size.
 * It's possible that PMP regions only cover partial of the TLB page, and
 * this may split the page into regions with different permissions.
 * For example if PMP0 is (0x80000008~0x8000000F, R) and PMP1 is (0x80000000
 * ~0x80000FFF, RWX), then region 0x80000008~0x8000000F has R permission, and
 * the other regions in this page have RWX permissions.
 * A write access to 0x80000000 will match PMP1. However we cannot cache the
 * translation result in the TLB since this will make the write access to
 * 0x80000008 bypass the check of PMP0.
 * To avoid this we return a size of 1 (which means no caching) if the PMP
 * region only covers partial of the TLB page.
 */
target_ulong pmp_get_tlb_size(CPURISCVState *env, hwaddr addr)
{
    hwaddr pmp_sa;
    hwaddr pmp_ea;
    hwaddr tlb_sa = addr & ~(TARGET_PAGE_SIZE - 1);
    hwaddr tlb_ea = tlb_sa + TARGET_PAGE_SIZE - 1;
    int i;

    /*
     * If PMP is not supported or there are no PMP rules, the TLB page will not
     * be split into regions with different permissions by PMP so we set the
     * size to TARGET_PAGE_SIZE.
     */
    if (!riscv_cpu_cfg(env)->pmp || !pmp_get_num_rules(env)) {
        return TARGET_PAGE_SIZE;
    }

    for (i = 0; i < MAX_RISCV_PMPS; i++) {
        if (pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg) == PMP_AMATCH_OFF) {
            continue;
        }

        pmp_sa = env->pmp_state.addr[i].sa;
        pmp_ea = env->pmp_state.addr[i].ea;

        /*
         * Only the first PMP entry that covers (whole or partial of) the TLB
         * page really matters:
         * If it covers the whole TLB page, set the size to TARGET_PAGE_SIZE,
         * since the following PMP entries have lower priority and will not
         * affect the permissions of the page.
         * If it only covers partial of the TLB page, set the size to 1 since
         * the allowed permissions of the region may be different from other
         * region of the page.
         */
        if (pmp_sa <= tlb_sa && pmp_ea >= tlb_ea) {
            return TARGET_PAGE_SIZE;
        } else if ((pmp_sa >= tlb_sa && pmp_sa <= tlb_ea) ||
                   (pmp_ea >= tlb_sa && pmp_ea <= tlb_ea)) {
            return 1;
        }
    }

    /*
     * If no PMP entry matches the TLB page, the TLB page will also not be
     * split into regions with different permissions by PMP so we set the size
     * to TARGET_PAGE_SIZE.
     */
    return TARGET_PAGE_SIZE;
}

/*
 * Convert PMP privilege to TLB page privilege.
 */
int pmp_priv_to_page_prot(pmp_priv_t pmp_priv)
{
    int prot = 0;

    if (pmp_priv & PMP_READ) {
        prot |= PAGE_READ;
    }
    if (pmp_priv & PMP_WRITE) {
        prot |= PAGE_WRITE;
    }
    if (pmp_priv & PMP_EXEC) {
        prot |= PAGE_EXEC;
    }

    return prot;
}
