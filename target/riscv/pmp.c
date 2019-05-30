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

/*
 * PMP (Physical Memory Protection) is as-of-yet unused and needs testing.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "cpu.h"

#ifndef CONFIG_USER_ONLY

#define RISCV_DEBUG_PMP 0
#define PMP_DEBUG(fmt, ...)                                                    \
    do {                                                                       \
        if (RISCV_DEBUG_PMP) {                                                 \
            qemu_log_mask(LOG_TRACE, "%s: " fmt "\n", __func__, ##__VA_ARGS__);\
        }                                                                      \
    } while (0)

static void pmp_write_cfg(CPURISCVState *env, uint32_t addr_index,
    uint8_t val);
static uint8_t pmp_read_cfg(CPURISCVState *env, uint32_t addr_index);
static void pmp_update_rule(CPURISCVState *env, uint32_t pmp_index);

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

    if (env->pmp_state.pmp[pmp_index].cfg_reg & PMP_LOCK) {
        return 1;
    }

    /* Top PMP has no 'next' to check */
    if ((pmp_index + 1u) >= MAX_RISCV_PMPS) {
        return 0;
    }

    /* In TOR mode, need to check the lock bit of the next pmp
     * (if there is a next)
     */
    const uint8_t a_field =
        pmp_get_a_field(env->pmp_state.pmp[pmp_index + 1].cfg_reg);
    if ((env->pmp_state.pmp[pmp_index + 1u].cfg_reg & PMP_LOCK) &&
         (PMP_AMATCH_TOR == a_field)) {
        return 1;
    }

    return 0;
}

/*
 * Count the number of active rules.
 */
static inline uint32_t pmp_get_num_rules(CPURISCVState *env)
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
static void pmp_write_cfg(CPURISCVState *env, uint32_t pmp_index, uint8_t val)
{
    if (pmp_index < MAX_RISCV_PMPS) {
        if (!pmp_is_locked(env, pmp_index)) {
            env->pmp_state.pmp[pmp_index].cfg_reg = val;
            pmp_update_rule(env, pmp_index);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "ignoring pmpcfg write - locked\n");
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring pmpcfg write - out of bounds\n");
    }
}

static void pmp_decode_napot(target_ulong a, target_ulong *sa, target_ulong *ea)
{
    /*
       aaaa...aaa0   8-byte NAPOT range
       aaaa...aa01   16-byte NAPOT range
       aaaa...a011   32-byte NAPOT range
       ...
       aa01...1111   2^XLEN-byte NAPOT range
       a011...1111   2^(XLEN+1)-byte NAPOT range
       0111...1111   2^(XLEN+2)-byte NAPOT range
       1111...1111   Reserved
    */
    if (a == -1) {
        *sa = 0u;
        *ea = -1;
        return;
    } else {
        target_ulong t1 = ctz64(~a);
        target_ulong base = (a & ~(((target_ulong)1 << t1) - 1)) << 2;
        target_ulong range = ((target_ulong)1 << (t1 + 3)) - 1;
        *sa = base;
        *ea = base + range;
    }
}


/* Convert cfg/addr reg values here into simple 'sa' --> start address and 'ea'
 *   end address values.
 *   This function is called relatively infrequently whereas the check that
 *   an address is within a pmp rule is called often, so optimise that one
 */
static void pmp_update_rule(CPURISCVState *env, uint32_t pmp_index)
{
    int i;

    env->pmp_state.num_rules = 0;

    uint8_t this_cfg = env->pmp_state.pmp[pmp_index].cfg_reg;
    target_ulong this_addr = env->pmp_state.pmp[pmp_index].addr_reg;
    target_ulong prev_addr = 0u;
    target_ulong sa = 0u;
    target_ulong ea = 0u;

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
        break;

    case PMP_AMATCH_NA4:
        sa = this_addr << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = (this_addr + 4u) - 1u;
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

    for (i = 0; i < MAX_RISCV_PMPS; i++) {
        const uint8_t a_field =
            pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg);
        if (PMP_AMATCH_OFF != a_field) {
            env->pmp_state.num_rules++;
        }
    }
}

static int pmp_is_in_range(CPURISCVState *env, int pmp_index, target_ulong addr)
{
    int result = 0;

    if ((addr >= env->pmp_state.addr[pmp_index].sa)
        && (addr <= env->pmp_state.addr[pmp_index].ea)) {
        result = 1;
    } else {
        result = 0;
    }

    return result;
}


/*
 * Public Interface
 */

/*
 * Check if the address has required RWX privs to complete desired operation
 */
bool pmp_hart_has_privs(CPURISCVState *env, target_ulong addr,
    target_ulong size, pmp_priv_t privs, target_ulong mode)
{
    int i = 0;
    int ret = -1;
    target_ulong s = 0;
    target_ulong e = 0;
    pmp_priv_t allowed_privs = 0;

    /* Short cut if no rules */
    if (0 == pmp_get_num_rules(env)) {
        return true;
    }

    /* 1.10 draft priv spec states there is an implicit order
         from low to high */
    for (i = 0; i < MAX_RISCV_PMPS; i++) {
        s = pmp_is_in_range(env, i, addr);
        e = pmp_is_in_range(env, i, addr + size - 1);

        /* partially inside */
        if ((s + e) == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "pmp violation - access is partially inside\n");
            ret = 0;
            break;
        }

        /* fully inside */
        const uint8_t a_field =
            pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg);

        /*
         * If the PMP entry is not off and the address is in range, do the priv
         * check
         */
        if (((s + e) == 2) && (PMP_AMATCH_OFF != a_field)) {
            allowed_privs = PMP_READ | PMP_WRITE | PMP_EXEC;
            if ((mode != PRV_M) || pmp_is_locked(env, i)) {
                allowed_privs &= env->pmp_state.pmp[i].cfg_reg;
            }

            if ((privs & allowed_privs) == privs) {
                ret = 1;
                break;
            } else {
                ret = 0;
                break;
            }
        }
    }

    /* No rule matched */
    if (ret == -1) {
        if (mode == PRV_M) {
            ret = 1; /* Privileged spec v1.10 states if no PMP entry matches an
                      * M-Mode access, the access succeeds */
        } else {
            ret = 0; /* Other modes are not allowed to succeed if they don't
                      * match a rule, but there are rules.  We've checked for
                      * no rule earlier in this function. */
        }
    }

    return ret == 1 ? true : false;
}


/*
 * Handle a write to a pmpcfg CSP
 */
void pmpcfg_csr_write(CPURISCVState *env, uint32_t reg_index,
    target_ulong val)
{
    int i;
    uint8_t cfg_val;

    PMP_DEBUG("hart " TARGET_FMT_ld ": reg%d, val: 0x" TARGET_FMT_lx,
        env->mhartid, reg_index, val);

    if ((reg_index & 1) && (sizeof(target_ulong) == 8)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring pmpcfg write - incorrect address\n");
        return;
    }

    for (i = 0; i < sizeof(target_ulong); i++) {
        cfg_val = (val >> 8 * i)  & 0xff;
        pmp_write_cfg(env, (reg_index * sizeof(target_ulong)) + i,
            cfg_val);
    }
}


/*
 * Handle a read from a pmpcfg CSP
 */
target_ulong pmpcfg_csr_read(CPURISCVState *env, uint32_t reg_index)
{
    int i;
    target_ulong cfg_val = 0;
    target_ulong val = 0;

    for (i = 0; i < sizeof(target_ulong); i++) {
        val = pmp_read_cfg(env, (reg_index * sizeof(target_ulong)) + i);
        cfg_val |= (val << (i * 8));
    }

    PMP_DEBUG("hart " TARGET_FMT_ld ": reg%d, val: 0x" TARGET_FMT_lx,
        env->mhartid, reg_index, cfg_val);

    return cfg_val;
}


/*
 * Handle a write to a pmpaddr CSP
 */
void pmpaddr_csr_write(CPURISCVState *env, uint32_t addr_index,
    target_ulong val)
{
    PMP_DEBUG("hart " TARGET_FMT_ld ": addr%d, val: 0x" TARGET_FMT_lx,
        env->mhartid, addr_index, val);

    if (addr_index < MAX_RISCV_PMPS) {
        if (!pmp_is_locked(env, addr_index)) {
            env->pmp_state.pmp[addr_index].addr_reg = val;
            pmp_update_rule(env, addr_index);
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
 * Handle a read from a pmpaddr CSP
 */
target_ulong pmpaddr_csr_read(CPURISCVState *env, uint32_t addr_index)
{
    PMP_DEBUG("hart " TARGET_FMT_ld ": addr%d, val: 0x" TARGET_FMT_lx,
        env->mhartid, addr_index,
        env->pmp_state.pmp[addr_index].addr_reg);
    if (addr_index < MAX_RISCV_PMPS) {
        return env->pmp_state.pmp[addr_index].addr_reg;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring pmpaddr read - out of bounds\n");
        return 0;
    }
}

#endif
