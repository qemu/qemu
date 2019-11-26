/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2001-2012  The Bochs Project
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA B 02110-1301 USA
/////////////////////////////////////////////////////////////////////////

#include "qemu/osdep.h"
#include "panic.h"
#include "qemu-common.h"
#include "x86_decode.h"
#include "x86.h"
#include "x86_emu.h"
#include "x86_mmu.h"
#include "x86_flags.h"
#include "vmcs.h"
#include "vmx.h"

void hvf_handle_io(struct CPUState *cpu, uint16_t port, void *data,
                   int direction, int size, uint32_t count);

#define EXEC_2OP_FLAGS_CMD(env, decode, cmd, FLAGS_FUNC, save_res) \
{                                                       \
    fetch_operands(env, decode, 2, true, true, false);  \
    switch (decode->operand_size) {                     \
    case 1:                                         \
    {                                               \
        uint8_t v1 = (uint8_t)decode->op[0].val;    \
        uint8_t v2 = (uint8_t)decode->op[1].val;    \
        uint8_t diff = v1 cmd v2;                   \
        if (save_res) {                              \
            write_val_ext(env, decode->op[0].ptr, diff, 1);  \
        } \
        FLAGS_FUNC##8(env, v1, v2, diff);           \
        break;                                      \
    }                                               \
    case 2:                                        \
    {                                               \
        uint16_t v1 = (uint16_t)decode->op[0].val;  \
        uint16_t v2 = (uint16_t)decode->op[1].val;  \
        uint16_t diff = v1 cmd v2;                  \
        if (save_res) {                              \
            write_val_ext(env, decode->op[0].ptr, diff, 2); \
        } \
        FLAGS_FUNC##16(env, v1, v2, diff);          \
        break;                                      \
    }                                               \
    case 4:                                        \
    {                                               \
        uint32_t v1 = (uint32_t)decode->op[0].val;  \
        uint32_t v2 = (uint32_t)decode->op[1].val;  \
        uint32_t diff = v1 cmd v2;                  \
        if (save_res) {                              \
            write_val_ext(env, decode->op[0].ptr, diff, 4); \
        } \
        FLAGS_FUNC##32(env, v1, v2, diff);          \
        break;                                      \
    }                                               \
    default:                                        \
        VM_PANIC("bad size\n");                    \
    }                                                   \
}                                                       \

target_ulong read_reg(CPUX86State *env, int reg, int size)
{
    switch (size) {
    case 1:
        return env->hvf_emul->regs[reg].lx;
    case 2:
        return env->hvf_emul->regs[reg].rx;
    case 4:
        return env->hvf_emul->regs[reg].erx;
    case 8:
        return env->hvf_emul->regs[reg].rrx;
    default:
        abort();
    }
    return 0;
}

void write_reg(CPUX86State *env, int reg, target_ulong val, int size)
{
    switch (size) {
    case 1:
        env->hvf_emul->regs[reg].lx = val;
        break;
    case 2:
        env->hvf_emul->regs[reg].rx = val;
        break;
    case 4:
        env->hvf_emul->regs[reg].rrx = (uint32_t)val;
        break;
    case 8:
        env->hvf_emul->regs[reg].rrx = val;
        break;
    default:
        abort();
    }
}

target_ulong read_val_from_reg(target_ulong reg_ptr, int size)
{
    target_ulong val;
    
    switch (size) {
    case 1:
        val = *(uint8_t *)reg_ptr;
        break;
    case 2:
        val = *(uint16_t *)reg_ptr;
        break;
    case 4:
        val = *(uint32_t *)reg_ptr;
        break;
    case 8:
        val = *(uint64_t *)reg_ptr;
        break;
    default:
        abort();
    }
    return val;
}

void write_val_to_reg(target_ulong reg_ptr, target_ulong val, int size)
{
    switch (size) {
    case 1:
        *(uint8_t *)reg_ptr = val;
        break;
    case 2:
        *(uint16_t *)reg_ptr = val;
        break;
    case 4:
        *(uint64_t *)reg_ptr = (uint32_t)val;
        break;
    case 8:
        *(uint64_t *)reg_ptr = val;
        break;
    default:
        abort();
    }
}

static bool is_host_reg(struct CPUX86State *env, target_ulong ptr)
{
    return (ptr - (target_ulong)&env->hvf_emul->regs[0]) < sizeof(env->hvf_emul->regs);
}

void write_val_ext(struct CPUX86State *env, target_ulong ptr, target_ulong val, int size)
{
    if (is_host_reg(env, ptr)) {
        write_val_to_reg(ptr, val, size);
        return;
    }
    vmx_write_mem(env_cpu(env), ptr, &val, size);
}

uint8_t *read_mmio(struct CPUX86State *env, target_ulong ptr, int bytes)
{
    vmx_read_mem(env_cpu(env), env->hvf_emul->mmio_buf, ptr, bytes);
    return env->hvf_emul->mmio_buf;
}


target_ulong read_val_ext(struct CPUX86State *env, target_ulong ptr, int size)
{
    target_ulong val;
    uint8_t *mmio_ptr;

    if (is_host_reg(env, ptr)) {
        return read_val_from_reg(ptr, size);
    }

    mmio_ptr = read_mmio(env, ptr, size);
    switch (size) {
    case 1:
        val = *(uint8_t *)mmio_ptr;
        break;
    case 2:
        val = *(uint16_t *)mmio_ptr;
        break;
    case 4:
        val = *(uint32_t *)mmio_ptr;
        break;
    case 8:
        val = *(uint64_t *)mmio_ptr;
        break;
    default:
        VM_PANIC("bad size\n");
        break;
    }
    return val;
}

static void fetch_operands(struct CPUX86State *env, struct x86_decode *decode,
                           int n, bool val_op0, bool val_op1, bool val_op2)
{
    int i;
    bool calc_val[3] = {val_op0, val_op1, val_op2};

    for (i = 0; i < n; i++) {
        switch (decode->op[i].type) {
        case X86_VAR_IMMEDIATE:
            break;
        case X86_VAR_REG:
            VM_PANIC_ON(!decode->op[i].ptr);
            if (calc_val[i]) {
                decode->op[i].val = read_val_from_reg(decode->op[i].ptr,
                                                      decode->operand_size);
            }
            break;
        case X86_VAR_RM:
            calc_modrm_operand(env, decode, &decode->op[i]);
            if (calc_val[i]) {
                decode->op[i].val = read_val_ext(env, decode->op[i].ptr,
                                                 decode->operand_size);
            }
            break;
        case X86_VAR_OFFSET:
            decode->op[i].ptr = decode_linear_addr(env, decode,
                                                   decode->op[i].ptr,
                                                   R_DS);
            if (calc_val[i]) {
                decode->op[i].val = read_val_ext(env, decode->op[i].ptr,
                                                 decode->operand_size);
            }
            break;
        default:
            break;
        }
    }
}

static void exec_mov(struct CPUX86State *env, struct x86_decode *decode)
{
    fetch_operands(env, decode, 2, false, true, false);
    write_val_ext(env, decode->op[0].ptr, decode->op[1].val,
                  decode->operand_size);

    RIP(env) += decode->len;
}

static void exec_add(struct CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, +, SET_FLAGS_OSZAPC_ADD, true);
    RIP(env) += decode->len;
}

static void exec_or(struct CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, |, SET_FLAGS_OSZAPC_LOGIC, true);
    RIP(env) += decode->len;
}

static void exec_adc(struct CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, +get_CF(env)+, SET_FLAGS_OSZAPC_ADD, true);
    RIP(env) += decode->len;
}

static void exec_sbb(struct CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, -get_CF(env)-, SET_FLAGS_OSZAPC_SUB, true);
    RIP(env) += decode->len;
}

static void exec_and(struct CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, &, SET_FLAGS_OSZAPC_LOGIC, true);
    RIP(env) += decode->len;
}

static void exec_sub(struct CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, -, SET_FLAGS_OSZAPC_SUB, true);
    RIP(env) += decode->len;
}

static void exec_xor(struct CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, ^, SET_FLAGS_OSZAPC_LOGIC, true);
    RIP(env) += decode->len;
}

static void exec_neg(struct CPUX86State *env, struct x86_decode *decode)
{
    /*EXEC_2OP_FLAGS_CMD(env, decode, -, SET_FLAGS_OSZAPC_SUB, false);*/
    int32_t val;
    fetch_operands(env, decode, 2, true, true, false);

    val = 0 - sign(decode->op[1].val, decode->operand_size);
    write_val_ext(env, decode->op[1].ptr, val, decode->operand_size);

    if (4 == decode->operand_size) {
        SET_FLAGS_OSZAPC_SUB32(env, 0, 0 - val, val);
    } else if (2 == decode->operand_size) {
        SET_FLAGS_OSZAPC_SUB16(env, 0, 0 - val, val);
    } else if (1 == decode->operand_size) {
        SET_FLAGS_OSZAPC_SUB8(env, 0, 0 - val, val);
    } else {
        VM_PANIC("bad op size\n");
    }

    /*lflags_to_rflags(env);*/
    RIP(env) += decode->len;
}

static void exec_cmp(struct CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, -, SET_FLAGS_OSZAPC_SUB, false);
    RIP(env) += decode->len;
}

static void exec_inc(struct CPUX86State *env, struct x86_decode *decode)
{
    decode->op[1].type = X86_VAR_IMMEDIATE;
    decode->op[1].val = 0;

    EXEC_2OP_FLAGS_CMD(env, decode, +1+, SET_FLAGS_OSZAP_ADD, true);

    RIP(env) += decode->len;
}

static void exec_dec(struct CPUX86State *env, struct x86_decode *decode)
{
    decode->op[1].type = X86_VAR_IMMEDIATE;
    decode->op[1].val = 0;

    EXEC_2OP_FLAGS_CMD(env, decode, -1-, SET_FLAGS_OSZAP_SUB, true);
    RIP(env) += decode->len;
}

static void exec_tst(struct CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, &, SET_FLAGS_OSZAPC_LOGIC, false);
    RIP(env) += decode->len;
}

static void exec_not(struct CPUX86State *env, struct x86_decode *decode)
{
    fetch_operands(env, decode, 1, true, false, false);

    write_val_ext(env, decode->op[0].ptr, ~decode->op[0].val,
                  decode->operand_size);
    RIP(env) += decode->len;
}

void exec_movzx(struct CPUX86State *env, struct x86_decode *decode)
{
    int src_op_size;
    int op_size = decode->operand_size;

    fetch_operands(env, decode, 1, false, false, false);

    if (0xb6 == decode->opcode[1]) {
        src_op_size = 1;
    } else {
        src_op_size = 2;
    }
    decode->operand_size = src_op_size;
    calc_modrm_operand(env, decode, &decode->op[1]);
    decode->op[1].val = read_val_ext(env, decode->op[1].ptr, src_op_size);
    write_val_ext(env, decode->op[0].ptr, decode->op[1].val, op_size);

    RIP(env) += decode->len;
}

static void exec_out(struct CPUX86State *env, struct x86_decode *decode)
{
    switch (decode->opcode[0]) {
    case 0xe6:
        hvf_handle_io(env_cpu(env), decode->op[0].val, &AL(env), 1, 1, 1);
        break;
    case 0xe7:
        hvf_handle_io(env_cpu(env), decode->op[0].val, &RAX(env), 1,
                      decode->operand_size, 1);
        break;
    case 0xee:
        hvf_handle_io(env_cpu(env), DX(env), &AL(env), 1, 1, 1);
        break;
    case 0xef:
        hvf_handle_io(env_cpu(env), DX(env), &RAX(env), 1,
                      decode->operand_size, 1);
        break;
    default:
        VM_PANIC("Bad out opcode\n");
        break;
    }
    RIP(env) += decode->len;
}

static void exec_in(struct CPUX86State *env, struct x86_decode *decode)
{
    target_ulong val = 0;
    switch (decode->opcode[0]) {
    case 0xe4:
        hvf_handle_io(env_cpu(env), decode->op[0].val, &AL(env), 0, 1, 1);
        break;
    case 0xe5:
        hvf_handle_io(env_cpu(env), decode->op[0].val, &val, 0,
                      decode->operand_size, 1);
        if (decode->operand_size == 2) {
            AX(env) = val;
        } else {
            RAX(env) = (uint32_t)val;
        }
        break;
    case 0xec:
        hvf_handle_io(env_cpu(env), DX(env), &AL(env), 0, 1, 1);
        break;
    case 0xed:
        hvf_handle_io(env_cpu(env), DX(env), &val, 0, decode->operand_size, 1);
        if (decode->operand_size == 2) {
            AX(env) = val;
        } else {
            RAX(env) = (uint32_t)val;
        }

        break;
    default:
        VM_PANIC("Bad in opcode\n");
        break;
    }

    RIP(env) += decode->len;
}

static inline void string_increment_reg(struct CPUX86State *env, int reg,
                                        struct x86_decode *decode)
{
    target_ulong val = read_reg(env, reg, decode->addressing_size);
    if (env->hvf_emul->rflags.df) {
        val -= decode->operand_size;
    } else {
        val += decode->operand_size;
    }
    write_reg(env, reg, val, decode->addressing_size);
}

static inline void string_rep(struct CPUX86State *env, struct x86_decode *decode,
                              void (*func)(struct CPUX86State *env,
                                           struct x86_decode *ins), int rep)
{
    target_ulong rcx = read_reg(env, R_ECX, decode->addressing_size);
    while (rcx--) {
        func(env, decode);
        write_reg(env, R_ECX, rcx, decode->addressing_size);
        if ((PREFIX_REP == rep) && !get_ZF(env)) {
            break;
        }
        if ((PREFIX_REPN == rep) && get_ZF(env)) {
            break;
        }
    }
}

static void exec_ins_single(struct CPUX86State *env, struct x86_decode *decode)
{
    target_ulong addr = linear_addr_size(env_cpu(env), RDI(env),
                                         decode->addressing_size, R_ES);

    hvf_handle_io(env_cpu(env), DX(env), env->hvf_emul->mmio_buf, 0,
                  decode->operand_size, 1);
    vmx_write_mem(env_cpu(env), addr, env->hvf_emul->mmio_buf,
                  decode->operand_size);

    string_increment_reg(env, R_EDI, decode);
}

static void exec_ins(struct CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_ins_single, 0);
    } else {
        exec_ins_single(env, decode);
    }

    RIP(env) += decode->len;
}

static void exec_outs_single(struct CPUX86State *env, struct x86_decode *decode)
{
    target_ulong addr = decode_linear_addr(env, decode, RSI(env), R_DS);

    vmx_read_mem(env_cpu(env), env->hvf_emul->mmio_buf, addr,
                 decode->operand_size);
    hvf_handle_io(env_cpu(env), DX(env), env->hvf_emul->mmio_buf, 1,
                  decode->operand_size, 1);

    string_increment_reg(env, R_ESI, decode);
}

static void exec_outs(struct CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_outs_single, 0);
    } else {
        exec_outs_single(env, decode);
    }

    RIP(env) += decode->len;
}

static void exec_movs_single(struct CPUX86State *env, struct x86_decode *decode)
{
    target_ulong src_addr;
    target_ulong dst_addr;
    target_ulong val;

    src_addr = decode_linear_addr(env, decode, RSI(env), R_DS);
    dst_addr = linear_addr_size(env_cpu(env), RDI(env),
                                decode->addressing_size, R_ES);

    val = read_val_ext(env, src_addr, decode->operand_size);
    write_val_ext(env, dst_addr, val, decode->operand_size);

    string_increment_reg(env, R_ESI, decode);
    string_increment_reg(env, R_EDI, decode);
}

static void exec_movs(struct CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_movs_single, 0);
    } else {
        exec_movs_single(env, decode);
    }

    RIP(env) += decode->len;
}

static void exec_cmps_single(struct CPUX86State *env, struct x86_decode *decode)
{
    target_ulong src_addr;
    target_ulong dst_addr;

    src_addr = decode_linear_addr(env, decode, RSI(env), R_DS);
    dst_addr = linear_addr_size(env_cpu(env), RDI(env),
                                decode->addressing_size, R_ES);

    decode->op[0].type = X86_VAR_IMMEDIATE;
    decode->op[0].val = read_val_ext(env, src_addr, decode->operand_size);
    decode->op[1].type = X86_VAR_IMMEDIATE;
    decode->op[1].val = read_val_ext(env, dst_addr, decode->operand_size);

    EXEC_2OP_FLAGS_CMD(env, decode, -, SET_FLAGS_OSZAPC_SUB, false);

    string_increment_reg(env, R_ESI, decode);
    string_increment_reg(env, R_EDI, decode);
}

static void exec_cmps(struct CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_cmps_single, decode->rep);
    } else {
        exec_cmps_single(env, decode);
    }
    RIP(env) += decode->len;
}


static void exec_stos_single(struct CPUX86State *env, struct x86_decode *decode)
{
    target_ulong addr;
    target_ulong val;

    addr = linear_addr_size(env_cpu(env), RDI(env),
                            decode->addressing_size, R_ES);
    val = read_reg(env, R_EAX, decode->operand_size);
    vmx_write_mem(env_cpu(env), addr, &val, decode->operand_size);

    string_increment_reg(env, R_EDI, decode);
}


static void exec_stos(struct CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_stos_single, 0);
    } else {
        exec_stos_single(env, decode);
    }

    RIP(env) += decode->len;
}

static void exec_scas_single(struct CPUX86State *env, struct x86_decode *decode)
{
    target_ulong addr;

    addr = linear_addr_size(env_cpu(env), RDI(env),
                            decode->addressing_size, R_ES);
    decode->op[1].type = X86_VAR_IMMEDIATE;
    vmx_read_mem(env_cpu(env), &decode->op[1].val, addr, decode->operand_size);

    EXEC_2OP_FLAGS_CMD(env, decode, -, SET_FLAGS_OSZAPC_SUB, false);
    string_increment_reg(env, R_EDI, decode);
}

static void exec_scas(struct CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_REG;
    decode->op[0].reg = R_EAX;
    if (decode->rep) {
        string_rep(env, decode, exec_scas_single, decode->rep);
    } else {
        exec_scas_single(env, decode);
    }

    RIP(env) += decode->len;
}

static void exec_lods_single(struct CPUX86State *env, struct x86_decode *decode)
{
    target_ulong addr;
    target_ulong val = 0;

    addr = decode_linear_addr(env, decode, RSI(env), R_DS);
    vmx_read_mem(env_cpu(env), &val, addr,  decode->operand_size);
    write_reg(env, R_EAX, val, decode->operand_size);

    string_increment_reg(env, R_ESI, decode);
}

static void exec_lods(struct CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_lods_single, 0);
    } else {
        exec_lods_single(env, decode);
    }

    RIP(env) += decode->len;
}

#define MSR_IA32_UCODE_REV 0x00000017

void simulate_rdmsr(struct CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    uint32_t msr = ECX(env);
    uint64_t val = 0;

    switch (msr) {
    case MSR_IA32_TSC:
        val = rdtscp() + rvmcs(cpu->hvf_fd, VMCS_TSC_OFFSET);
        break;
    case MSR_IA32_APICBASE:
        val = cpu_get_apic_base(X86_CPU(cpu)->apic_state);
        break;
    case MSR_IA32_UCODE_REV:
        val = (0x100000000ULL << 32) | 0x100000000ULL;
        break;
    case MSR_EFER:
        val = rvmcs(cpu->hvf_fd, VMCS_GUEST_IA32_EFER);
        break;
    case MSR_FSBASE:
        val = rvmcs(cpu->hvf_fd, VMCS_GUEST_FS_BASE);
        break;
    case MSR_GSBASE:
        val = rvmcs(cpu->hvf_fd, VMCS_GUEST_GS_BASE);
        break;
    case MSR_KERNELGSBASE:
        val = rvmcs(cpu->hvf_fd, VMCS_HOST_FS_BASE);
        break;
    case MSR_STAR:
        abort();
        break;
    case MSR_LSTAR:
        abort();
        break;
    case MSR_CSTAR:
        abort();
        break;
    case MSR_IA32_MISC_ENABLE:
        val = env->msr_ia32_misc_enable;
        break;
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        val = env->mtrr_var[(ECX(env) - MSR_MTRRphysBase(0)) / 2].base;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        val = env->mtrr_var[(ECX(env) - MSR_MTRRphysMask(0)) / 2].mask;
        break;
    case MSR_MTRRfix64K_00000:
        val = env->mtrr_fixed[0];
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        val = env->mtrr_fixed[ECX(env) - MSR_MTRRfix16K_80000 + 1];
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        val = env->mtrr_fixed[ECX(env) - MSR_MTRRfix4K_C0000 + 3];
        break;
    case MSR_MTRRdefType:
        val = env->mtrr_deftype;
        break;
    default:
        /* fprintf(stderr, "%s: unknown msr 0x%x\n", __func__, msr); */
        val = 0;
        break;
    }

    RAX(env) = (uint32_t)val;
    RDX(env) = (uint32_t)(val >> 32);
}

static void exec_rdmsr(struct CPUX86State *env, struct x86_decode *decode)
{
    simulate_rdmsr(env_cpu(env));
    RIP(env) += decode->len;
}

void simulate_wrmsr(struct CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    uint32_t msr = ECX(env);
    uint64_t data = ((uint64_t)EDX(env) << 32) | EAX(env);

    switch (msr) {
    case MSR_IA32_TSC:
        break;
    case MSR_IA32_APICBASE:
        cpu_set_apic_base(X86_CPU(cpu)->apic_state, data);
        break;
    case MSR_FSBASE:
        wvmcs(cpu->hvf_fd, VMCS_GUEST_FS_BASE, data);
        break;
    case MSR_GSBASE:
        wvmcs(cpu->hvf_fd, VMCS_GUEST_GS_BASE, data);
        break;
    case MSR_KERNELGSBASE:
        wvmcs(cpu->hvf_fd, VMCS_HOST_FS_BASE, data);
        break;
    case MSR_STAR:
        abort();
        break;
    case MSR_LSTAR:
        abort();
        break;
    case MSR_CSTAR:
        abort();
        break;
    case MSR_EFER:
        /*printf("new efer %llx\n", EFER(cpu));*/
        wvmcs(cpu->hvf_fd, VMCS_GUEST_IA32_EFER, data);
        if (data & MSR_EFER_NXE) {
            hv_vcpu_invalidate_tlb(cpu->hvf_fd);
        }
        break;
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        env->mtrr_var[(ECX(env) - MSR_MTRRphysBase(0)) / 2].base = data;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        env->mtrr_var[(ECX(env) - MSR_MTRRphysMask(0)) / 2].mask = data;
        break;
    case MSR_MTRRfix64K_00000:
        env->mtrr_fixed[ECX(env) - MSR_MTRRfix64K_00000] = data;
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        env->mtrr_fixed[ECX(env) - MSR_MTRRfix16K_80000 + 1] = data;
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        env->mtrr_fixed[ECX(env) - MSR_MTRRfix4K_C0000 + 3] = data;
        break;
    case MSR_MTRRdefType:
        env->mtrr_deftype = data;
        break;
    default:
        break;
    }

    /* Related to support known hypervisor interface */
    /* if (g_hypervisor_iface)
         g_hypervisor_iface->wrmsr_handler(cpu, msr, data);

    printf("write msr %llx\n", RCX(cpu));*/
}

static void exec_wrmsr(struct CPUX86State *env, struct x86_decode *decode)
{
    simulate_wrmsr(env_cpu(env));
    RIP(env) += decode->len;
}

/*
 * flag:
 * 0 - bt, 1 - btc, 2 - bts, 3 - btr
 */
static void do_bt(struct CPUX86State *env, struct x86_decode *decode, int flag)
{
    int32_t displacement;
    uint8_t index;
    bool cf;
    int mask = (4 == decode->operand_size) ? 0x1f : 0xf;

    VM_PANIC_ON(decode->rex.rex);

    fetch_operands(env, decode, 2, false, true, false);
    index = decode->op[1].val & mask;

    if (decode->op[0].type != X86_VAR_REG) {
        if (4 == decode->operand_size) {
            displacement = ((int32_t) (decode->op[1].val & 0xffffffe0)) / 32;
            decode->op[0].ptr += 4 * displacement;
        } else if (2 == decode->operand_size) {
            displacement = ((int16_t) (decode->op[1].val & 0xfff0)) / 16;
            decode->op[0].ptr += 2 * displacement;
        } else {
            VM_PANIC("bt 64bit\n");
        }
    }
    decode->op[0].val = read_val_ext(env, decode->op[0].ptr,
                                     decode->operand_size);
    cf = (decode->op[0].val >> index) & 0x01;

    switch (flag) {
    case 0:
        set_CF(env, cf);
        return;
    case 1:
        decode->op[0].val ^= (1u << index);
        break;
    case 2:
        decode->op[0].val |= (1u << index);
        break;
    case 3:
        decode->op[0].val &= ~(1u << index);
        break;
    }
    write_val_ext(env, decode->op[0].ptr, decode->op[0].val,
                  decode->operand_size);
    set_CF(env, cf);
}

static void exec_bt(struct CPUX86State *env, struct x86_decode *decode)
{
    do_bt(env, decode, 0);
    RIP(env) += decode->len;
}

static void exec_btc(struct CPUX86State *env, struct x86_decode *decode)
{
    do_bt(env, decode, 1);
    RIP(env) += decode->len;
}

static void exec_btr(struct CPUX86State *env, struct x86_decode *decode)
{
    do_bt(env, decode, 3);
    RIP(env) += decode->len;
}

static void exec_bts(struct CPUX86State *env, struct x86_decode *decode)
{
    do_bt(env, decode, 2);
    RIP(env) += decode->len;
}

void exec_shl(struct CPUX86State *env, struct x86_decode *decode)
{
    uint8_t count;
    int of = 0, cf = 0;

    fetch_operands(env, decode, 2, true, true, false);

    count = decode->op[1].val;
    count &= 0x1f;      /* count is masked to 5 bits*/
    if (!count) {
        goto exit;
    }

    switch (decode->operand_size) {
    case 1:
    {
        uint8_t res = 0;
        if (count <= 8) {
            res = (decode->op[0].val << count);
            cf = (decode->op[0].val >> (8 - count)) & 0x1;
            of = cf ^ (res >> 7);
        }

        write_val_ext(env, decode->op[0].ptr, res, 1);
        SET_FLAGS_OSZAPC_LOGIC8(env, 0, 0, res);
        SET_FLAGS_OxxxxC(env, of, cf);
        break;
    }
    case 2:
    {
        uint16_t res = 0;

        /* from bochs */
        if (count <= 16) {
            res = (decode->op[0].val << count);
            cf = (decode->op[0].val >> (16 - count)) & 0x1;
            of = cf ^ (res >> 15); /* of = cf ^ result15 */
        }

        write_val_ext(env, decode->op[0].ptr, res, 2);
        SET_FLAGS_OSZAPC_LOGIC16(env, 0, 0, res);
        SET_FLAGS_OxxxxC(env, of, cf);
        break;
    }
    case 4:
    {
        uint32_t res = decode->op[0].val << count;

        write_val_ext(env, decode->op[0].ptr, res, 4);
        SET_FLAGS_OSZAPC_LOGIC32(env, 0, 0, res);
        cf = (decode->op[0].val >> (32 - count)) & 0x1;
        of = cf ^ (res >> 31); /* of = cf ^ result31 */
        SET_FLAGS_OxxxxC(env, of, cf);
        break;
    }
    default:
        abort();
    }

exit:
    /* lflags_to_rflags(env); */
    RIP(env) += decode->len;
}

void exec_movsx(CPUX86State *env, struct x86_decode *decode)
{
    int src_op_size;
    int op_size = decode->operand_size;

    fetch_operands(env, decode, 2, false, false, false);

    if (0xbe == decode->opcode[1]) {
        src_op_size = 1;
    } else {
        src_op_size = 2;
    }

    decode->operand_size = src_op_size;
    calc_modrm_operand(env, decode, &decode->op[1]);
    decode->op[1].val = sign(read_val_ext(env, decode->op[1].ptr, src_op_size),
                             src_op_size);

    write_val_ext(env, decode->op[0].ptr, decode->op[1].val, op_size);

    RIP(env) += decode->len;
}

void exec_ror(struct CPUX86State *env, struct x86_decode *decode)
{
    uint8_t count;

    fetch_operands(env, decode, 2, true, true, false);
    count = decode->op[1].val;

    switch (decode->operand_size) {
    case 1:
    {
        uint32_t bit6, bit7;
        uint8_t res;

        if ((count & 0x07) == 0) {
            if (count & 0x18) {
                bit6 = ((uint8_t)decode->op[0].val >> 6) & 1;
                bit7 = ((uint8_t)decode->op[0].val >> 7) & 1;
                SET_FLAGS_OxxxxC(env, bit6 ^ bit7, bit7);
             }
        } else {
            count &= 0x7; /* use only bottom 3 bits */
            res = ((uint8_t)decode->op[0].val >> count) |
                   ((uint8_t)decode->op[0].val << (8 - count));
            write_val_ext(env, decode->op[0].ptr, res, 1);
            bit6 = (res >> 6) & 1;
            bit7 = (res >> 7) & 1;
            /* set eflags: ROR count affects the following flags: C, O */
            SET_FLAGS_OxxxxC(env, bit6 ^ bit7, bit7);
        }
        break;
    }
    case 2:
    {
        uint32_t bit14, bit15;
        uint16_t res;

        if ((count & 0x0f) == 0) {
            if (count & 0x10) {
                bit14 = ((uint16_t)decode->op[0].val >> 14) & 1;
                bit15 = ((uint16_t)decode->op[0].val >> 15) & 1;
                /* of = result14 ^ result15 */
                SET_FLAGS_OxxxxC(env, bit14 ^ bit15, bit15);
            }
        } else {
            count &= 0x0f;  /* use only 4 LSB's */
            res = ((uint16_t)decode->op[0].val >> count) |
                   ((uint16_t)decode->op[0].val << (16 - count));
            write_val_ext(env, decode->op[0].ptr, res, 2);

            bit14 = (res >> 14) & 1;
            bit15 = (res >> 15) & 1;
            /* of = result14 ^ result15 */
            SET_FLAGS_OxxxxC(env, bit14 ^ bit15, bit15);
        }
        break;
    }
    case 4:
    {
        uint32_t bit31, bit30;
        uint32_t res;

        count &= 0x1f;
        if (count) {
            res = ((uint32_t)decode->op[0].val >> count) |
                   ((uint32_t)decode->op[0].val << (32 - count));
            write_val_ext(env, decode->op[0].ptr, res, 4);

            bit31 = (res >> 31) & 1;
            bit30 = (res >> 30) & 1;
            /* of = result30 ^ result31 */
            SET_FLAGS_OxxxxC(env, bit30 ^ bit31, bit31);
        }
        break;
        }
    }
    RIP(env) += decode->len;
}

void exec_rol(struct CPUX86State *env, struct x86_decode *decode)
{
    uint8_t count;

    fetch_operands(env, decode, 2, true, true, false);
    count = decode->op[1].val;

    switch (decode->operand_size) {
    case 1:
    {
        uint32_t bit0, bit7;
        uint8_t res;

        if ((count & 0x07) == 0) {
            if (count & 0x18) {
                bit0 = ((uint8_t)decode->op[0].val & 1);
                bit7 = ((uint8_t)decode->op[0].val >> 7);
                SET_FLAGS_OxxxxC(env, bit0 ^ bit7, bit0);
            }
        }  else {
            count &= 0x7; /* use only lowest 3 bits */
            res = ((uint8_t)decode->op[0].val << count) |
                   ((uint8_t)decode->op[0].val >> (8 - count));

            write_val_ext(env, decode->op[0].ptr, res, 1);
            /* set eflags:
             * ROL count affects the following flags: C, O
             */
            bit0 = (res &  1);
            bit7 = (res >> 7);
            SET_FLAGS_OxxxxC(env, bit0 ^ bit7, bit0);
        }
        break;
    }
    case 2:
    {
        uint32_t bit0, bit15;
        uint16_t res;

        if ((count & 0x0f) == 0) {
            if (count & 0x10) {
                bit0  = ((uint16_t)decode->op[0].val & 0x1);
                bit15 = ((uint16_t)decode->op[0].val >> 15);
                /* of = cf ^ result15 */
                SET_FLAGS_OxxxxC(env, bit0 ^ bit15, bit0);
            }
        } else {
            count &= 0x0f; /* only use bottom 4 bits */
            res = ((uint16_t)decode->op[0].val << count) |
                   ((uint16_t)decode->op[0].val >> (16 - count));

            write_val_ext(env, decode->op[0].ptr, res, 2);
            bit0  = (res & 0x1);
            bit15 = (res >> 15);
            /* of = cf ^ result15 */
            SET_FLAGS_OxxxxC(env, bit0 ^ bit15, bit0);
        }
        break;
    }
    case 4:
    {
        uint32_t bit0, bit31;
        uint32_t res;

        count &= 0x1f;
        if (count) {
            res = ((uint32_t)decode->op[0].val << count) |
                   ((uint32_t)decode->op[0].val >> (32 - count));

            write_val_ext(env, decode->op[0].ptr, res, 4);
            bit0  = (res & 0x1);
            bit31 = (res >> 31);
            /* of = cf ^ result31 */
            SET_FLAGS_OxxxxC(env, bit0 ^ bit31, bit0);
        }
        break;
        }
    }
    RIP(env) += decode->len;
}


void exec_rcl(struct CPUX86State *env, struct x86_decode *decode)
{
    uint8_t count;
    int of = 0, cf = 0;

    fetch_operands(env, decode, 2, true, true, false);
    count = decode->op[1].val & 0x1f;

    switch (decode->operand_size) {
    case 1:
    {
        uint8_t op1_8 = decode->op[0].val;
        uint8_t res;
        count %= 9;
        if (!count) {
            break;
        }

        if (1 == count) {
            res = (op1_8 << 1) | get_CF(env);
        } else {
            res = (op1_8 << count) | (get_CF(env) << (count - 1)) |
                   (op1_8 >> (9 - count));
        }

        write_val_ext(env, decode->op[0].ptr, res, 1);

        cf = (op1_8 >> (8 - count)) & 0x01;
        of = cf ^ (res >> 7); /* of = cf ^ result7 */
        SET_FLAGS_OxxxxC(env, of, cf);
        break;
    }
    case 2:
    {
        uint16_t res;
        uint16_t op1_16 = decode->op[0].val;

        count %= 17;
        if (!count) {
            break;
        }

        if (1 == count) {
            res = (op1_16 << 1) | get_CF(env);
        } else if (count == 16) {
            res = (get_CF(env) << 15) | (op1_16 >> 1);
        } else { /* 2..15 */
            res = (op1_16 << count) | (get_CF(env) << (count - 1)) |
                   (op1_16 >> (17 - count));
        }

        write_val_ext(env, decode->op[0].ptr, res, 2);

        cf = (op1_16 >> (16 - count)) & 0x1;
        of = cf ^ (res >> 15); /* of = cf ^ result15 */
        SET_FLAGS_OxxxxC(env, of, cf);
        break;
    }
    case 4:
    {
        uint32_t res;
        uint32_t op1_32 = decode->op[0].val;

        if (!count) {
            break;
        }

        if (1 == count) {
            res = (op1_32 << 1) | get_CF(env);
        } else {
            res = (op1_32 << count) | (get_CF(env) << (count - 1)) |
                   (op1_32 >> (33 - count));
        }

        write_val_ext(env, decode->op[0].ptr, res, 4);

        cf = (op1_32 >> (32 - count)) & 0x1;
        of = cf ^ (res >> 31); /* of = cf ^ result31 */
        SET_FLAGS_OxxxxC(env, of, cf);
        break;
        }
    }
    RIP(env) += decode->len;
}

void exec_rcr(struct CPUX86State *env, struct x86_decode *decode)
{
    uint8_t count;
    int of = 0, cf = 0;

    fetch_operands(env, decode, 2, true, true, false);
    count = decode->op[1].val & 0x1f;

    switch (decode->operand_size) {
    case 1:
    {
        uint8_t op1_8 = decode->op[0].val;
        uint8_t res;

        count %= 9;
        if (!count) {
            break;
        }
        res = (op1_8 >> count) | (get_CF(env) << (8 - count)) |
               (op1_8 << (9 - count));

        write_val_ext(env, decode->op[0].ptr, res, 1);

        cf = (op1_8 >> (count - 1)) & 0x1;
        of = (((res << 1) ^ res) >> 7) & 0x1; /* of = result6 ^ result7 */
        SET_FLAGS_OxxxxC(env, of, cf);
        break;
    }
    case 2:
    {
        uint16_t op1_16 = decode->op[0].val;
        uint16_t res;

        count %= 17;
        if (!count) {
            break;
        }
        res = (op1_16 >> count) | (get_CF(env) << (16 - count)) |
               (op1_16 << (17 - count));

        write_val_ext(env, decode->op[0].ptr, res, 2);

        cf = (op1_16 >> (count - 1)) & 0x1;
        of = ((uint16_t)((res << 1) ^ res) >> 15) & 0x1; /* of = result15 ^
                                                            result14 */
        SET_FLAGS_OxxxxC(env, of, cf);
        break;
    }
    case 4:
    {
        uint32_t res;
        uint32_t op1_32 = decode->op[0].val;

        if (!count) {
            break;
        }

        if (1 == count) {
            res = (op1_32 >> 1) | (get_CF(env) << 31);
        } else {
            res = (op1_32 >> count) | (get_CF(env) << (32 - count)) |
                   (op1_32 << (33 - count));
        }

        write_val_ext(env, decode->op[0].ptr, res, 4);

        cf = (op1_32 >> (count - 1)) & 0x1;
        of = ((res << 1) ^ res) >> 31; /* of = result30 ^ result31 */
        SET_FLAGS_OxxxxC(env, of, cf);
        break;
        }
    }
    RIP(env) += decode->len;
}

static void exec_xchg(struct CPUX86State *env, struct x86_decode *decode)
{
    fetch_operands(env, decode, 2, true, true, false);

    write_val_ext(env, decode->op[0].ptr, decode->op[1].val,
                  decode->operand_size);
    write_val_ext(env, decode->op[1].ptr, decode->op[0].val,
                  decode->operand_size);

    RIP(env) += decode->len;
}

static void exec_xadd(struct CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, +, SET_FLAGS_OSZAPC_ADD, true);
    write_val_ext(env, decode->op[1].ptr, decode->op[0].val,
                  decode->operand_size);

    RIP(env) += decode->len;
}

static struct cmd_handler {
    enum x86_decode_cmd cmd;
    void (*handler)(struct CPUX86State *env, struct x86_decode *ins);
} handlers[] = {
    {X86_DECODE_CMD_INVL, NULL,},
    {X86_DECODE_CMD_MOV, exec_mov},
    {X86_DECODE_CMD_ADD, exec_add},
    {X86_DECODE_CMD_OR, exec_or},
    {X86_DECODE_CMD_ADC, exec_adc},
    {X86_DECODE_CMD_SBB, exec_sbb},
    {X86_DECODE_CMD_AND, exec_and},
    {X86_DECODE_CMD_SUB, exec_sub},
    {X86_DECODE_CMD_NEG, exec_neg},
    {X86_DECODE_CMD_XOR, exec_xor},
    {X86_DECODE_CMD_CMP, exec_cmp},
    {X86_DECODE_CMD_INC, exec_inc},
    {X86_DECODE_CMD_DEC, exec_dec},
    {X86_DECODE_CMD_TST, exec_tst},
    {X86_DECODE_CMD_NOT, exec_not},
    {X86_DECODE_CMD_MOVZX, exec_movzx},
    {X86_DECODE_CMD_OUT, exec_out},
    {X86_DECODE_CMD_IN, exec_in},
    {X86_DECODE_CMD_INS, exec_ins},
    {X86_DECODE_CMD_OUTS, exec_outs},
    {X86_DECODE_CMD_RDMSR, exec_rdmsr},
    {X86_DECODE_CMD_WRMSR, exec_wrmsr},
    {X86_DECODE_CMD_BT, exec_bt},
    {X86_DECODE_CMD_BTR, exec_btr},
    {X86_DECODE_CMD_BTC, exec_btc},
    {X86_DECODE_CMD_BTS, exec_bts},
    {X86_DECODE_CMD_SHL, exec_shl},
    {X86_DECODE_CMD_ROL, exec_rol},
    {X86_DECODE_CMD_ROR, exec_ror},
    {X86_DECODE_CMD_RCR, exec_rcr},
    {X86_DECODE_CMD_RCL, exec_rcl},
    /*{X86_DECODE_CMD_CPUID, exec_cpuid},*/
    {X86_DECODE_CMD_MOVS, exec_movs},
    {X86_DECODE_CMD_CMPS, exec_cmps},
    {X86_DECODE_CMD_STOS, exec_stos},
    {X86_DECODE_CMD_SCAS, exec_scas},
    {X86_DECODE_CMD_LODS, exec_lods},
    {X86_DECODE_CMD_MOVSX, exec_movsx},
    {X86_DECODE_CMD_XCHG, exec_xchg},
    {X86_DECODE_CMD_XADD, exec_xadd},
};

static struct cmd_handler _cmd_handler[X86_DECODE_CMD_LAST];

static void init_cmd_handler()
{
    int i;
    for (i = 0; i < ARRAY_SIZE(handlers); i++) {
        _cmd_handler[handlers[i].cmd] = handlers[i];
    }
}

void load_regs(struct CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    int i = 0;
    RRX(env, R_EAX) = rreg(cpu->hvf_fd, HV_X86_RAX);
    RRX(env, R_EBX) = rreg(cpu->hvf_fd, HV_X86_RBX);
    RRX(env, R_ECX) = rreg(cpu->hvf_fd, HV_X86_RCX);
    RRX(env, R_EDX) = rreg(cpu->hvf_fd, HV_X86_RDX);
    RRX(env, R_ESI) = rreg(cpu->hvf_fd, HV_X86_RSI);
    RRX(env, R_EDI) = rreg(cpu->hvf_fd, HV_X86_RDI);
    RRX(env, R_ESP) = rreg(cpu->hvf_fd, HV_X86_RSP);
    RRX(env, R_EBP) = rreg(cpu->hvf_fd, HV_X86_RBP);
    for (i = 8; i < 16; i++) {
        RRX(env, i) = rreg(cpu->hvf_fd, HV_X86_RAX + i);
    }

    RFLAGS(env) = rreg(cpu->hvf_fd, HV_X86_RFLAGS);
    rflags_to_lflags(env);
    RIP(env) = rreg(cpu->hvf_fd, HV_X86_RIP);
}

void store_regs(struct CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    int i = 0;
    wreg(cpu->hvf_fd, HV_X86_RAX, RAX(env));
    wreg(cpu->hvf_fd, HV_X86_RBX, RBX(env));
    wreg(cpu->hvf_fd, HV_X86_RCX, RCX(env));
    wreg(cpu->hvf_fd, HV_X86_RDX, RDX(env));
    wreg(cpu->hvf_fd, HV_X86_RSI, RSI(env));
    wreg(cpu->hvf_fd, HV_X86_RDI, RDI(env));
    wreg(cpu->hvf_fd, HV_X86_RBP, RBP(env));
    wreg(cpu->hvf_fd, HV_X86_RSP, RSP(env));
    for (i = 8; i < 16; i++) {
        wreg(cpu->hvf_fd, HV_X86_RAX + i, RRX(env, i));
    }

    lflags_to_rflags(env);
    wreg(cpu->hvf_fd, HV_X86_RFLAGS, RFLAGS(env));
    macvm_set_rip(cpu, RIP(env));
}

bool exec_instruction(struct CPUX86State *env, struct x86_decode *ins)
{
    /*if (hvf_vcpu_id(cpu))
    printf("%d, %llx: exec_instruction %s\n", hvf_vcpu_id(cpu),  RIP(cpu),
          decode_cmd_to_string(ins->cmd));*/

    if (!_cmd_handler[ins->cmd].handler) {
        printf("Unimplemented handler (%llx) for %d (%x %x) \n", RIP(env),
                ins->cmd, ins->opcode[0],
                ins->opcode_len > 1 ? ins->opcode[1] : 0);
        RIP(env) += ins->len;
        return true;
    }

    _cmd_handler[ins->cmd].handler(env, ins);
    return true;
}

void init_emu()
{
    init_cmd_handler();
}
