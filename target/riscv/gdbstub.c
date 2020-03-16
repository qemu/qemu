/*
 * RISC-V GDB Server Stub
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
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
#include "exec/gdbstub.h"
#include "cpu.h"

/*
 * The GDB CSR xml files list them in documentation order, not numerical order,
 * and are missing entries for unnamed CSRs.  So we need to map the gdb numbers
 * to the hardware numbers.
 */

static int csr_register_map[] = {
    CSR_USTATUS,
    CSR_UIE,
    CSR_UTVEC,
    CSR_USCRATCH,
    CSR_UEPC,
    CSR_UCAUSE,
    CSR_UTVAL,
    CSR_UIP,
    CSR_FFLAGS,
    CSR_FRM,
    CSR_FCSR,
    CSR_CYCLE,
    CSR_TIME,
    CSR_INSTRET,
    CSR_HPMCOUNTER3,
    CSR_HPMCOUNTER4,
    CSR_HPMCOUNTER5,
    CSR_HPMCOUNTER6,
    CSR_HPMCOUNTER7,
    CSR_HPMCOUNTER8,
    CSR_HPMCOUNTER9,
    CSR_HPMCOUNTER10,
    CSR_HPMCOUNTER11,
    CSR_HPMCOUNTER12,
    CSR_HPMCOUNTER13,
    CSR_HPMCOUNTER14,
    CSR_HPMCOUNTER15,
    CSR_HPMCOUNTER16,
    CSR_HPMCOUNTER17,
    CSR_HPMCOUNTER18,
    CSR_HPMCOUNTER19,
    CSR_HPMCOUNTER20,
    CSR_HPMCOUNTER21,
    CSR_HPMCOUNTER22,
    CSR_HPMCOUNTER23,
    CSR_HPMCOUNTER24,
    CSR_HPMCOUNTER25,
    CSR_HPMCOUNTER26,
    CSR_HPMCOUNTER27,
    CSR_HPMCOUNTER28,
    CSR_HPMCOUNTER29,
    CSR_HPMCOUNTER30,
    CSR_HPMCOUNTER31,
    CSR_CYCLEH,
    CSR_TIMEH,
    CSR_INSTRETH,
    CSR_HPMCOUNTER3H,
    CSR_HPMCOUNTER4H,
    CSR_HPMCOUNTER5H,
    CSR_HPMCOUNTER6H,
    CSR_HPMCOUNTER7H,
    CSR_HPMCOUNTER8H,
    CSR_HPMCOUNTER9H,
    CSR_HPMCOUNTER10H,
    CSR_HPMCOUNTER11H,
    CSR_HPMCOUNTER12H,
    CSR_HPMCOUNTER13H,
    CSR_HPMCOUNTER14H,
    CSR_HPMCOUNTER15H,
    CSR_HPMCOUNTER16H,
    CSR_HPMCOUNTER17H,
    CSR_HPMCOUNTER18H,
    CSR_HPMCOUNTER19H,
    CSR_HPMCOUNTER20H,
    CSR_HPMCOUNTER21H,
    CSR_HPMCOUNTER22H,
    CSR_HPMCOUNTER23H,
    CSR_HPMCOUNTER24H,
    CSR_HPMCOUNTER25H,
    CSR_HPMCOUNTER26H,
    CSR_HPMCOUNTER27H,
    CSR_HPMCOUNTER28H,
    CSR_HPMCOUNTER29H,
    CSR_HPMCOUNTER30H,
    CSR_HPMCOUNTER31H,
    CSR_SSTATUS,
    CSR_SEDELEG,
    CSR_SIDELEG,
    CSR_SIE,
    CSR_STVEC,
    CSR_SCOUNTEREN,
    CSR_SSCRATCH,
    CSR_SEPC,
    CSR_SCAUSE,
    CSR_STVAL,
    CSR_SIP,
    CSR_SATP,
    CSR_MVENDORID,
    CSR_MARCHID,
    CSR_MIMPID,
    CSR_MHARTID,
    CSR_MSTATUS,
    CSR_MISA,
    CSR_MEDELEG,
    CSR_MIDELEG,
    CSR_MIE,
    CSR_MTVEC,
    CSR_MCOUNTEREN,
    CSR_MSCRATCH,
    CSR_MEPC,
    CSR_MCAUSE,
    CSR_MTVAL,
    CSR_MIP,
    CSR_MTINST,
    CSR_MTVAL2,
    CSR_PMPCFG0,
    CSR_PMPCFG1,
    CSR_PMPCFG2,
    CSR_PMPCFG3,
    CSR_PMPADDR0,
    CSR_PMPADDR1,
    CSR_PMPADDR2,
    CSR_PMPADDR3,
    CSR_PMPADDR4,
    CSR_PMPADDR5,
    CSR_PMPADDR6,
    CSR_PMPADDR7,
    CSR_PMPADDR8,
    CSR_PMPADDR9,
    CSR_PMPADDR10,
    CSR_PMPADDR11,
    CSR_PMPADDR12,
    CSR_PMPADDR13,
    CSR_PMPADDR14,
    CSR_PMPADDR15,
    CSR_MCYCLE,
    CSR_MINSTRET,
    CSR_MHPMCOUNTER3,
    CSR_MHPMCOUNTER4,
    CSR_MHPMCOUNTER5,
    CSR_MHPMCOUNTER6,
    CSR_MHPMCOUNTER7,
    CSR_MHPMCOUNTER8,
    CSR_MHPMCOUNTER9,
    CSR_MHPMCOUNTER10,
    CSR_MHPMCOUNTER11,
    CSR_MHPMCOUNTER12,
    CSR_MHPMCOUNTER13,
    CSR_MHPMCOUNTER14,
    CSR_MHPMCOUNTER15,
    CSR_MHPMCOUNTER16,
    CSR_MHPMCOUNTER17,
    CSR_MHPMCOUNTER18,
    CSR_MHPMCOUNTER19,
    CSR_MHPMCOUNTER20,
    CSR_MHPMCOUNTER21,
    CSR_MHPMCOUNTER22,
    CSR_MHPMCOUNTER23,
    CSR_MHPMCOUNTER24,
    CSR_MHPMCOUNTER25,
    CSR_MHPMCOUNTER26,
    CSR_MHPMCOUNTER27,
    CSR_MHPMCOUNTER28,
    CSR_MHPMCOUNTER29,
    CSR_MHPMCOUNTER30,
    CSR_MHPMCOUNTER31,
    CSR_MCYCLEH,
    CSR_MINSTRETH,
    CSR_MHPMCOUNTER3H,
    CSR_MHPMCOUNTER4H,
    CSR_MHPMCOUNTER5H,
    CSR_MHPMCOUNTER6H,
    CSR_MHPMCOUNTER7H,
    CSR_MHPMCOUNTER8H,
    CSR_MHPMCOUNTER9H,
    CSR_MHPMCOUNTER10H,
    CSR_MHPMCOUNTER11H,
    CSR_MHPMCOUNTER12H,
    CSR_MHPMCOUNTER13H,
    CSR_MHPMCOUNTER14H,
    CSR_MHPMCOUNTER15H,
    CSR_MHPMCOUNTER16H,
    CSR_MHPMCOUNTER17H,
    CSR_MHPMCOUNTER18H,
    CSR_MHPMCOUNTER19H,
    CSR_MHPMCOUNTER20H,
    CSR_MHPMCOUNTER21H,
    CSR_MHPMCOUNTER22H,
    CSR_MHPMCOUNTER23H,
    CSR_MHPMCOUNTER24H,
    CSR_MHPMCOUNTER25H,
    CSR_MHPMCOUNTER26H,
    CSR_MHPMCOUNTER27H,
    CSR_MHPMCOUNTER28H,
    CSR_MHPMCOUNTER29H,
    CSR_MHPMCOUNTER30H,
    CSR_MHPMCOUNTER31H,
    CSR_MHPMEVENT3,
    CSR_MHPMEVENT4,
    CSR_MHPMEVENT5,
    CSR_MHPMEVENT6,
    CSR_MHPMEVENT7,
    CSR_MHPMEVENT8,
    CSR_MHPMEVENT9,
    CSR_MHPMEVENT10,
    CSR_MHPMEVENT11,
    CSR_MHPMEVENT12,
    CSR_MHPMEVENT13,
    CSR_MHPMEVENT14,
    CSR_MHPMEVENT15,
    CSR_MHPMEVENT16,
    CSR_MHPMEVENT17,
    CSR_MHPMEVENT18,
    CSR_MHPMEVENT19,
    CSR_MHPMEVENT20,
    CSR_MHPMEVENT21,
    CSR_MHPMEVENT22,
    CSR_MHPMEVENT23,
    CSR_MHPMEVENT24,
    CSR_MHPMEVENT25,
    CSR_MHPMEVENT26,
    CSR_MHPMEVENT27,
    CSR_MHPMEVENT28,
    CSR_MHPMEVENT29,
    CSR_MHPMEVENT30,
    CSR_MHPMEVENT31,
    CSR_TSELECT,
    CSR_TDATA1,
    CSR_TDATA2,
    CSR_TDATA3,
    CSR_DCSR,
    CSR_DPC,
    CSR_DSCRATCH,
    CSR_HSTATUS,
    CSR_HEDELEG,
    CSR_HIDELEG,
    CSR_HIE,
    CSR_HCOUNTEREN,
    CSR_HTVAL,
    CSR_HIP,
    CSR_HTINST,
    CSR_HGATP,
    CSR_MBASE,
    CSR_MBOUND,
    CSR_MIBASE,
    CSR_MIBOUND,
    CSR_MDBASE,
    CSR_MDBOUND,
    CSR_MUCOUNTEREN,
    CSR_MSCOUNTEREN,
    CSR_MHCOUNTEREN,
};

int riscv_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n < 32) {
        return gdb_get_regl(mem_buf, env->gpr[n]);
    } else if (n == 32) {
        return gdb_get_regl(mem_buf, env->pc);
    }
    return 0;
}

int riscv_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n == 0) {
        /* discard writes to x0 */
        return sizeof(target_ulong);
    } else if (n < 32) {
        env->gpr[n] = ldtul_p(mem_buf);
        return sizeof(target_ulong);
    } else if (n == 32) {
        env->pc = ldtul_p(mem_buf);
        return sizeof(target_ulong);
    }
    return 0;
}

static int riscv_gdb_get_fpu(CPURISCVState *env, GByteArray *buf, int n)
{
    if (n < 32) {
        if (env->misa & RVD) {
            return gdb_get_reg64(buf, env->fpr[n]);
        }
        if (env->misa & RVF) {
            return gdb_get_reg32(buf, env->fpr[n]);
        }
    /* there is hole between ft11 and fflags in fpu.xml */
    } else if (n < 36 && n > 32) {
        target_ulong val = 0;
        int result;
        /*
         * CSR_FFLAGS is at index 8 in csr_register, and gdb says it is FP
         * register 33, so we recalculate the map index.
         * This also works for CSR_FRM and CSR_FCSR.
         */
        result = riscv_csrrw_debug(env, n - 33 + csr_register_map[8], &val,
                                   0, 0);
        if (result == 0) {
            return gdb_get_regl(buf, val);
        }
    }
    return 0;
}

static int riscv_gdb_set_fpu(CPURISCVState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        env->fpr[n] = ldq_p(mem_buf); /* always 64-bit */
        return sizeof(uint64_t);
    /* there is hole between ft11 and fflags in fpu.xml */
    } else if (n < 36 && n > 32) {
        target_ulong val = ldtul_p(mem_buf);
        int result;
        /*
         * CSR_FFLAGS is at index 8 in csr_register, and gdb says it is FP
         * register 33, so we recalculate the map index.
         * This also works for CSR_FRM and CSR_FCSR.
         */
        result = riscv_csrrw_debug(env, n - 33 + csr_register_map[8], NULL,
                                   val, -1);
        if (result == 0) {
            return sizeof(target_ulong);
        }
    }
    return 0;
}

static int riscv_gdb_get_csr(CPURISCVState *env, GByteArray *buf, int n)
{
    if (n < ARRAY_SIZE(csr_register_map)) {
        target_ulong val = 0;
        int result;

        result = riscv_csrrw_debug(env, csr_register_map[n], &val, 0, 0);
        if (result == 0) {
            return gdb_get_regl(buf, val);
        }
    }
    return 0;
}

static int riscv_gdb_set_csr(CPURISCVState *env, uint8_t *mem_buf, int n)
{
    if (n < ARRAY_SIZE(csr_register_map)) {
        target_ulong val = ldtul_p(mem_buf);
        int result;

        result = riscv_csrrw_debug(env, csr_register_map[n], NULL, val, -1);
        if (result == 0) {
            return sizeof(target_ulong);
        }
    }
    return 0;
}

static int riscv_gdb_get_virtual(CPURISCVState *cs, GByteArray *buf, int n)
{
    if (n == 0) {
#ifdef CONFIG_USER_ONLY
        return gdb_get_regl(buf, 0);
#else
        return gdb_get_regl(buf, cs->priv);
#endif
    }
    return 0;
}

static int riscv_gdb_set_virtual(CPURISCVState *cs, uint8_t *mem_buf, int n)
{
    if (n == 0) {
#ifndef CONFIG_USER_ONLY
        cs->priv = ldtul_p(mem_buf) & 0x3;
        if (cs->priv == PRV_H) {
            cs->priv = PRV_S;
        }
#endif
        return sizeof(target_ulong);
    }
    return 0;
}

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    if (env->misa & RVD) {
        gdb_register_coprocessor(cs, riscv_gdb_get_fpu, riscv_gdb_set_fpu,
                                 36, "riscv-64bit-fpu.xml", 0);
    } else if (env->misa & RVF) {
        gdb_register_coprocessor(cs, riscv_gdb_get_fpu, riscv_gdb_set_fpu,
                                 36, "riscv-32bit-fpu.xml", 0);
    }
#if defined(TARGET_RISCV32)
    gdb_register_coprocessor(cs, riscv_gdb_get_csr, riscv_gdb_set_csr,
                             240, "riscv-32bit-csr.xml", 0);

    gdb_register_coprocessor(cs, riscv_gdb_get_virtual, riscv_gdb_set_virtual,
                             1, "riscv-32bit-virtual.xml", 0);
#elif defined(TARGET_RISCV64)
    gdb_register_coprocessor(cs, riscv_gdb_get_csr, riscv_gdb_set_csr,
                             240, "riscv-64bit-csr.xml", 0);

    gdb_register_coprocessor(cs, riscv_gdb_get_virtual, riscv_gdb_set_virtual,
                             1, "riscv-64bit-virtual.xml", 0);
#endif
}
