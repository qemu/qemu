/*
 * QEMU RISC-V PMP (Physical Memory Protection)
 *
 * Author: Daire McNamara, daire.mcnamara@emdalo.com
 *         Ivan Griffin, ivan.griffin@emdalo.com
 *
 * This provides a RISC-V Physical Memory Protection interface
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

#ifndef RISCV_PMP_H
#define RISCV_PMP_H

#define MAX_RISCV_PMPS (64)
#define OLD_MAX_RISCV_PMPS (16)
#define MIN_RISCV_PMP_GRANULARITY 4

typedef enum {
    PMP_READ  = 1 << 0,
    PMP_WRITE = 1 << 1,
    PMP_EXEC  = 1 << 2,
    PMP_AMATCH = (3 << 3),
    PMP_MTMATCH = (3 << 5),
    PMP_LOCK  = 1 << 7
} pmp_priv_t;

typedef enum {
    PMP_AMATCH_OFF,  /* Null (off)                            */
    PMP_AMATCH_TOR,  /* Top of Range                          */
    PMP_AMATCH_NA4,  /* Naturally aligned four-byte region    */
    PMP_AMATCH_NAPOT /* Naturally aligned power-of-two region */
} pmp_am_t;

typedef enum {
    MSECCFG_MML   = 1 << 0,
    MSECCFG_MMWP  = 1 << 1,
    MSECCFG_RLB   = 1 << 2,
    MSECCFG_USEED = 1 << 8,
    MSECCFG_SSEED = 1 << 9,
    MSECCFG_MLPE =  1 << 10,
    MSECCFG_PMM = 3ULL << 32,
} mseccfg_field_t;

typedef struct {
    uint64_t addr_reg;
    uint8_t  cfg_reg;
} pmp_entry_t;

typedef struct {
    hwaddr sa;
    hwaddr ea;
} pmp_addr_t;

typedef struct {
    pmp_entry_t pmp[MAX_RISCV_PMPS];
    pmp_addr_t  addr[MAX_RISCV_PMPS];
    uint32_t num_rules;
} pmp_table_t;

typedef struct CPUArchState CPURISCVState;

bool pmp_hart_has_privs(CPURISCVState *env, hwaddr addr,
                        int size, pmp_priv_t privs,
                        pmp_priv_t *allowed_privs,
                        privilege_mode_t mode);
uint64_t pmp_get_tlb_size(CPURISCVState *env, hwaddr addr);
void pmp_update_rule_addr(CPURISCVState *env, uint32_t pmp_index);
void pmp_update_rule_nums(CPURISCVState *env);
uint32_t pmp_get_num_rules(CPURISCVState *env);
int pmp_priv_to_page_prot(pmp_priv_t pmp_priv);
void pmp_unlock_entries(CPURISCVState *env);

#define MSECCFG_MML_ISSET(env) get_field(env->mseccfg, MSECCFG_MML)
#define MSECCFG_MMWP_ISSET(env) get_field(env->mseccfg, MSECCFG_MMWP)
#define MSECCFG_RLB_ISSET(env) get_field(env->mseccfg, MSECCFG_RLB)

#endif
