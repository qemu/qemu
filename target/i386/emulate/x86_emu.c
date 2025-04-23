/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
//  version 2.1 of the License, or (at your option) any later version.
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
#include "x86_decode.h"
#include "x86.h"
#include "x86_emu.h"
#include "x86_flags.h"

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
        return x86_reg(env, reg)->lx;
    case 2:
        return x86_reg(env, reg)->rx;
    case 4:
        return x86_reg(env, reg)->erx;
    case 8:
        return x86_reg(env, reg)->rrx;
    default:
        abort();
    }
    return 0;
}

void write_reg(CPUX86State *env, int reg, target_ulong val, int size)
{
    switch (size) {
    case 1:
        x86_reg(env, reg)->lx = val;
        break;
    case 2:
        x86_reg(env, reg)->rx = val;
        break;
    case 4:
        x86_reg(env, reg)->rrx = (uint32_t)val;
        break;
    case 8:
        x86_reg(env, reg)->rrx = val;
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

static bool is_host_reg(CPUX86State *env, target_ulong ptr)
{
    return (ptr - (target_ulong)&env->regs[0]) < sizeof(env->regs);
}

void write_val_ext(CPUX86State *env, target_ulong ptr, target_ulong val, int size)
{
    if (is_host_reg(env, ptr)) {
        write_val_to_reg(ptr, val, size);
        return;
    }
    emul_ops->write_mem(env_cpu(env), &val, ptr, size);
}

uint8_t *read_mmio(CPUX86State *env, target_ulong ptr, int bytes)
{
    emul_ops->read_mem(env_cpu(env), env->emu_mmio_buf, ptr, bytes);
    return env->emu_mmio_buf;
}


target_ulong read_val_ext(CPUX86State *env, target_ulong ptr, int size)
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

static void fetch_operands(CPUX86State *env, struct x86_decode *decode,
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

static void exec_mov(CPUX86State *env, struct x86_decode *decode)
{
    fetch_operands(env, decode, 2, false, true, false);
    write_val_ext(env, decode->op[0].ptr, decode->op[1].val,
                  decode->operand_size);

    env->eip += decode->len;
}

static void exec_add(CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, +, SET_FLAGS_OSZAPC_ADD, true);
    env->eip += decode->len;
}

static void exec_or(CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, |, SET_FLAGS_OSZAPC_LOGIC, true);
    env->eip += decode->len;
}

static void exec_adc(CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, +get_CF(env)+, SET_FLAGS_OSZAPC_ADD, true);
    env->eip += decode->len;
}

static void exec_sbb(CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, -get_CF(env)-, SET_FLAGS_OSZAPC_SUB, true);
    env->eip += decode->len;
}

static void exec_and(CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, &, SET_FLAGS_OSZAPC_LOGIC, true);
    env->eip += decode->len;
}

static void exec_sub(CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, -, SET_FLAGS_OSZAPC_SUB, true);
    env->eip += decode->len;
}

static void exec_xor(CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, ^, SET_FLAGS_OSZAPC_LOGIC, true);
    env->eip += decode->len;
}

static void exec_neg(CPUX86State *env, struct x86_decode *decode)
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
    env->eip += decode->len;
}

static void exec_cmp(CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, -, SET_FLAGS_OSZAPC_SUB, false);
    env->eip += decode->len;
}

static void exec_inc(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[1].type = X86_VAR_IMMEDIATE;
    decode->op[1].val = 0;

    EXEC_2OP_FLAGS_CMD(env, decode, +1+, SET_FLAGS_OSZAP_ADD, true);

    env->eip += decode->len;
}

static void exec_dec(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[1].type = X86_VAR_IMMEDIATE;
    decode->op[1].val = 0;

    EXEC_2OP_FLAGS_CMD(env, decode, -1-, SET_FLAGS_OSZAP_SUB, true);
    env->eip += decode->len;
}

static void exec_tst(CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, &, SET_FLAGS_OSZAPC_LOGIC, false);
    env->eip += decode->len;
}

static void exec_not(CPUX86State *env, struct x86_decode *decode)
{
    fetch_operands(env, decode, 1, true, false, false);

    write_val_ext(env, decode->op[0].ptr, ~decode->op[0].val,
                  decode->operand_size);
    env->eip += decode->len;
}

void exec_movzx(CPUX86State *env, struct x86_decode *decode)
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

    env->eip += decode->len;
}

static void exec_out(CPUX86State *env, struct x86_decode *decode)
{
    switch (decode->opcode[0]) {
    case 0xe6:
        emul_ops->handle_io(env_cpu(env), decode->op[0].val, &AL(env), 1, 1, 1);
        break;
    case 0xe7:
        emul_ops->handle_io(env_cpu(env), decode->op[0].val, &RAX(env), 1,
                            decode->operand_size, 1);
        break;
    case 0xee:
        emul_ops->handle_io(env_cpu(env), DX(env), &AL(env), 1, 1, 1);
        break;
    case 0xef:
        emul_ops->handle_io(env_cpu(env), DX(env), &RAX(env), 1,
                            decode->operand_size, 1);
        break;
    default:
        VM_PANIC("Bad out opcode\n");
        break;
    }
    env->eip += decode->len;
}

static void exec_in(CPUX86State *env, struct x86_decode *decode)
{
    target_ulong val = 0;
    switch (decode->opcode[0]) {
    case 0xe4:
        emul_ops->handle_io(env_cpu(env), decode->op[0].val, &AL(env), 0, 1, 1);
        break;
    case 0xe5:
        emul_ops->handle_io(env_cpu(env), decode->op[0].val, &val, 0,
                      decode->operand_size, 1);
        if (decode->operand_size == 2) {
            AX(env) = val;
        } else {
            RAX(env) = (uint32_t)val;
        }
        break;
    case 0xec:
        emul_ops->handle_io(env_cpu(env), DX(env), &AL(env), 0, 1, 1);
        break;
    case 0xed:
        emul_ops->handle_io(env_cpu(env), DX(env), &val, 0,
                            decode->operand_size, 1);
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

    env->eip += decode->len;
}

static inline void string_increment_reg(CPUX86State *env, int reg,
                                        struct x86_decode *decode)
{
    target_ulong val = read_reg(env, reg, decode->addressing_size);
    if (env->eflags & DF_MASK) {
        val -= decode->operand_size;
    } else {
        val += decode->operand_size;
    }
    write_reg(env, reg, val, decode->addressing_size);
}

static inline void string_rep(CPUX86State *env, struct x86_decode *decode,
                              void (*func)(CPUX86State *env,
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

static void exec_ins_single(CPUX86State *env, struct x86_decode *decode)
{
    target_ulong addr = linear_addr_size(env_cpu(env), RDI(env),
                                         decode->addressing_size, R_ES);

    emul_ops->handle_io(env_cpu(env), DX(env), env->emu_mmio_buf, 0,
                        decode->operand_size, 1);
    emul_ops->write_mem(env_cpu(env), env->emu_mmio_buf, addr,
                        decode->operand_size);

    string_increment_reg(env, R_EDI, decode);
}

static void exec_ins(CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_ins_single, 0);
    } else {
        exec_ins_single(env, decode);
    }

    env->eip += decode->len;
}

static void exec_outs_single(CPUX86State *env, struct x86_decode *decode)
{
    target_ulong addr = decode_linear_addr(env, decode, RSI(env), R_DS);

    emul_ops->read_mem(env_cpu(env), env->emu_mmio_buf, addr,
                       decode->operand_size);
    emul_ops->handle_io(env_cpu(env), DX(env), env->emu_mmio_buf, 1,
                        decode->operand_size, 1);

    string_increment_reg(env, R_ESI, decode);
}

static void exec_outs(CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_outs_single, 0);
    } else {
        exec_outs_single(env, decode);
    }

    env->eip += decode->len;
}

static void exec_movs_single(CPUX86State *env, struct x86_decode *decode)
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

static void exec_movs(CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_movs_single, 0);
    } else {
        exec_movs_single(env, decode);
    }

    env->eip += decode->len;
}

static void exec_cmps_single(CPUX86State *env, struct x86_decode *decode)
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

static void exec_cmps(CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_cmps_single, decode->rep);
    } else {
        exec_cmps_single(env, decode);
    }
    env->eip += decode->len;
}


static void exec_stos_single(CPUX86State *env, struct x86_decode *decode)
{
    target_ulong addr;
    target_ulong val;

    addr = linear_addr_size(env_cpu(env), RDI(env),
                            decode->addressing_size, R_ES);
    val = read_reg(env, R_EAX, decode->operand_size);
    emul_ops->write_mem(env_cpu(env), &val, addr, decode->operand_size);

    string_increment_reg(env, R_EDI, decode);
}


static void exec_stos(CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_stos_single, 0);
    } else {
        exec_stos_single(env, decode);
    }

    env->eip += decode->len;
}

static void exec_scas_single(CPUX86State *env, struct x86_decode *decode)
{
    target_ulong addr;

    addr = linear_addr_size(env_cpu(env), RDI(env),
                            decode->addressing_size, R_ES);
    decode->op[1].type = X86_VAR_IMMEDIATE;
    emul_ops->read_mem(env_cpu(env), &decode->op[1].val, addr, decode->operand_size);

    EXEC_2OP_FLAGS_CMD(env, decode, -, SET_FLAGS_OSZAPC_SUB, false);
    string_increment_reg(env, R_EDI, decode);
}

static void exec_scas(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_REG;
    decode->op[0].reg = R_EAX;
    if (decode->rep) {
        string_rep(env, decode, exec_scas_single, decode->rep);
    } else {
        exec_scas_single(env, decode);
    }

    env->eip += decode->len;
}

static void exec_lods_single(CPUX86State *env, struct x86_decode *decode)
{
    target_ulong addr;
    target_ulong val = 0;

    addr = decode_linear_addr(env, decode, RSI(env), R_DS);
    emul_ops->read_mem(env_cpu(env), &val, addr,  decode->operand_size);
    write_reg(env, R_EAX, val, decode->operand_size);

    string_increment_reg(env, R_ESI, decode);
}

static void exec_lods(CPUX86State *env, struct x86_decode *decode)
{
    if (decode->rep) {
        string_rep(env, decode, exec_lods_single, 0);
    } else {
        exec_lods_single(env, decode);
    }

    env->eip += decode->len;
}

void x86_emul_raise_exception(CPUX86State *env, int exception_index, int error_code)
{
    env->exception_nr = exception_index;
    env->error_code = error_code;
    env->has_error_code = true;
    env->exception_injected = 1;
}

static void exec_rdmsr(CPUX86State *env, struct x86_decode *decode)
{
    emul_ops->simulate_rdmsr(env_cpu(env));
    env->eip += decode->len;
}

static void exec_wrmsr(CPUX86State *env, struct x86_decode *decode)
{
    emul_ops->simulate_wrmsr(env_cpu(env));
    env->eip += decode->len;
}

/*
 * flag:
 * 0 - bt, 1 - btc, 2 - bts, 3 - btr
 */
static void do_bt(CPUX86State *env, struct x86_decode *decode, int flag)
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

static void exec_bt(CPUX86State *env, struct x86_decode *decode)
{
    do_bt(env, decode, 0);
    env->eip += decode->len;
}

static void exec_btc(CPUX86State *env, struct x86_decode *decode)
{
    do_bt(env, decode, 1);
    env->eip += decode->len;
}

static void exec_btr(CPUX86State *env, struct x86_decode *decode)
{
    do_bt(env, decode, 3);
    env->eip += decode->len;
}

static void exec_bts(CPUX86State *env, struct x86_decode *decode)
{
    do_bt(env, decode, 2);
    env->eip += decode->len;
}

void exec_shl(CPUX86State *env, struct x86_decode *decode)
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
    env->eip += decode->len;
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

    env->eip += decode->len;
}

void exec_ror(CPUX86State *env, struct x86_decode *decode)
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
    env->eip += decode->len;
}

void exec_rol(CPUX86State *env, struct x86_decode *decode)
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
    env->eip += decode->len;
}


void exec_rcl(CPUX86State *env, struct x86_decode *decode)
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
    env->eip += decode->len;
}

void exec_rcr(CPUX86State *env, struct x86_decode *decode)
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
    env->eip += decode->len;
}

static void exec_xchg(CPUX86State *env, struct x86_decode *decode)
{
    fetch_operands(env, decode, 2, true, true, false);

    write_val_ext(env, decode->op[0].ptr, decode->op[1].val,
                  decode->operand_size);
    write_val_ext(env, decode->op[1].ptr, decode->op[0].val,
                  decode->operand_size);

    env->eip += decode->len;
}

static void exec_xadd(CPUX86State *env, struct x86_decode *decode)
{
    EXEC_2OP_FLAGS_CMD(env, decode, +, SET_FLAGS_OSZAPC_ADD, true);
    write_val_ext(env, decode->op[1].ptr, decode->op[0].val,
                  decode->operand_size);

    env->eip += decode->len;
}

static struct cmd_handler {
    enum x86_decode_cmd cmd;
    void (*handler)(CPUX86State *env, struct x86_decode *ins);
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

const struct x86_emul_ops *emul_ops;

static void init_cmd_handler(void)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(handlers); i++) {
        _cmd_handler[handlers[i].cmd] = handlers[i];
    }
}

bool exec_instruction(CPUX86State *env, struct x86_decode *ins)
{
    if (!_cmd_handler[ins->cmd].handler) {
        printf("Unimplemented handler (%llx) for %d (%x %x) \n", env->eip,
                ins->cmd, ins->opcode[0],
                ins->opcode_len > 1 ? ins->opcode[1] : 0);
        env->eip += ins->len;
        return true;
    }

    _cmd_handler[ins->cmd].handler(env, ins);
    return true;
}

void init_emu(const struct x86_emul_ops *o)
{
    emul_ops = o;
    init_cmd_handler();
}
