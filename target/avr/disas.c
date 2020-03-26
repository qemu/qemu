/*
 * AVR disassembler
 *
 * Copyright (c) 2019-2020 Richard Henderson <rth@twiddle.net>
 * Copyright (c) 2019-2020 Michael Rolnik <mrolnik@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"

typedef struct {
    disassemble_info *info;
    uint16_t next_word;
    bool next_word_used;
} DisasContext;

static int to_regs_16_31_by_one(DisasContext *ctx, int indx)
{
    return 16 + (indx % 16);
}

static int to_regs_16_23_by_one(DisasContext *ctx, int indx)
{
    return 16 + (indx % 8);
}

static int to_regs_24_30_by_two(DisasContext *ctx, int indx)
{
    return 24 + (indx % 4) * 2;
}

static int to_regs_00_30_by_two(DisasContext *ctx, int indx)
{
    return (indx % 16) * 2;
}

static uint16_t next_word(DisasContext *ctx)
{
    ctx->next_word_used = true;
    return ctx->next_word;
}

static int append_16(DisasContext *ctx, int x)
{
    return x << 16 | next_word(ctx);
}

/* Include the auto-generated decoder.  */
static bool decode_insn(DisasContext *ctx, uint16_t insn);
#include "decode_insn.inc.c"

#define output(mnemonic, format, ...) \
    (pctx->info->fprintf_func(pctx->info->stream, "%-9s " format, \
                              mnemonic, ##__VA_ARGS__))

int avr_print_insn(bfd_vma addr, disassemble_info *info)
{
    DisasContext ctx;
    DisasContext *pctx = &ctx;
    bfd_byte buffer[4];
    uint16_t insn;
    int status;

    ctx.info = info;

    status = info->read_memory_func(addr, buffer, 4, info);
    if (status != 0) {
        info->memory_error_func(status, addr, info);
        return -1;
    }
    insn = bfd_getl16(buffer);
    ctx.next_word = bfd_getl16(buffer + 2);
    ctx.next_word_used = false;

    if (!decode_insn(&ctx, insn)) {
        output(".db", "0x%02x, 0x%02x", buffer[0], buffer[1]);
    }

    return ctx.next_word_used ? 4 : 2;
}


#define INSN(opcode, format, ...)                                       \
static bool trans_##opcode(DisasContext *pctx, arg_##opcode * a)        \
{                                                                       \
    output(#opcode, format, ##__VA_ARGS__);                             \
    return true;                                                        \
}

#define INSN_MNEMONIC(opcode, mnemonic, format, ...)                    \
static bool trans_##opcode(DisasContext *pctx, arg_##opcode * a)        \
{                                                                       \
    output(mnemonic, format, ##__VA_ARGS__);                            \
    return true;                                                        \
}

/*
 *   C       Z       N       V       S       H       T       I
 *   0       1       2       3       4       5       6       7
 */
static const char brbc[][5] = {
    "BRCC", "BRNE", "BRPL", "BRVC", "BRGE", "BRHC", "BRTC", "BRID"
};

static const char brbs[][5] = {
    "BRCS", "BREQ", "BRMI", "BRVS", "BRLT", "BRHS", "BRTS", "BRIE"
};

static const char bset[][4] = {
    "SEC",  "SEZ",  "SEN",  "SEZ",  "SES",  "SEH",  "SET",  "SEI"
};

static const char bclr[][4] = {
    "CLC",  "CLZ",  "CLN",  "CLZ",  "CLS",  "CLH",  "CLT",  "CLI"
};

/*
 * Arithmetic Instructions
 */
INSN(ADD,    "r%d, r%d", a->rd, a->rr)
INSN(ADC,    "r%d, r%d", a->rd, a->rr)
INSN(ADIW,   "r%d:r%d, %d", a->rd + 1, a->rd, a->imm)
INSN(SUB,    "r%d, r%d", a->rd, a->rr)
INSN(SUBI,   "r%d, %d", a->rd, a->imm)
INSN(SBC,    "r%d, r%d", a->rd, a->rr)
INSN(SBCI,   "r%d, %d", a->rd, a->imm)
INSN(SBIW,   "r%d:r%d, %d", a->rd + 1, a->rd, a->imm)
INSN(AND,    "r%d, r%d", a->rd, a->rr)
INSN(ANDI,   "r%d, %d", a->rd, a->imm)
INSN(OR,     "r%d, r%d", a->rd, a->rr)
INSN(ORI,    "r%d, %d", a->rd, a->imm)
INSN(EOR,    "r%d, r%d", a->rd, a->rr)
INSN(COM,    "r%d", a->rd)
INSN(NEG,    "r%d", a->rd)
INSN(INC,    "r%d", a->rd)
INSN(DEC,    "r%d", a->rd)
INSN(MUL,    "r%d, r%d", a->rd, a->rr)
INSN(MULS,   "r%d, r%d", a->rd, a->rr)
INSN(MULSU,  "r%d, r%d", a->rd, a->rr)
INSN(FMUL,   "r%d, r%d", a->rd, a->rr)
INSN(FMULS,  "r%d, r%d", a->rd, a->rr)
INSN(FMULSU, "r%d, r%d", a->rd, a->rr)
INSN(DES,    "%d", a->imm)

/*
 * Branch Instructions
 */
INSN(RJMP,   ".%+d", a->imm * 2)
INSN(IJMP,   "")
INSN(EIJMP,  "")
INSN(JMP,    "0x%x", a->imm * 2)
INSN(RCALL,  ".%+d", a->imm * 2)
INSN(ICALL,  "")
INSN(EICALL, "")
INSN(CALL,   "0x%x", a->imm * 2)
INSN(RET,    "")
INSN(RETI,   "")
INSN(CPSE,   "r%d, r%d", a->rd, a->rr)
INSN(CP,     "r%d, r%d", a->rd, a->rr)
INSN(CPC,    "r%d, r%d", a->rd, a->rr)
INSN(CPI,    "r%d, %d", a->rd, a->imm)
INSN(SBRC,   "r%d, %d", a->rr, a->bit)
INSN(SBRS,   "r%d, %d", a->rr, a->bit)
INSN(SBIC,   "$%d, %d", a->reg, a->bit)
INSN(SBIS,   "$%d, %d", a->reg, a->bit)
INSN_MNEMONIC(BRBS,  brbs[a->bit], ".%+d", a->imm * 2)
INSN_MNEMONIC(BRBC,  brbc[a->bit], ".%+d", a->imm * 2)

/*
 * Data Transfer Instructions
 */
INSN(MOV,    "r%d, r%d", a->rd, a->rr)
INSN(MOVW,   "r%d:r%d, r%d:r%d", a->rd + 1, a->rd, a->rr + 1, a->rr)
INSN(LDI,    "r%d, %d", a->rd, a->imm)
INSN(LDS,    "r%d, %d", a->rd, a->imm)
INSN(LDX1,   "r%d, X", a->rd)
INSN(LDX2,   "r%d, X+", a->rd)
INSN(LDX3,   "r%d, -X", a->rd)
INSN(LDY2,   "r%d, Y+", a->rd)
INSN(LDY3,   "r%d, -Y", a->rd)
INSN(LDZ2,   "r%d, Z+", a->rd)
INSN(LDZ3,   "r%d, -Z", a->rd)
INSN(LDDY,   "r%d, Y+%d", a->rd, a->imm)
INSN(LDDZ,   "r%d, Z+%d", a->rd, a->imm)
INSN(STS,    "%d, r%d", a->imm, a->rd)
INSN(STX1,   "X, r%d", a->rr)
INSN(STX2,   "X+, r%d", a->rr)
INSN(STX3,   "-X, r%d", a->rr)
INSN(STY2,   "Y+, r%d", a->rd)
INSN(STY3,   "-Y, r%d", a->rd)
INSN(STZ2,   "Z+, r%d", a->rd)
INSN(STZ3,   "-Z, r%d", a->rd)
INSN(STDY,   "Y+%d, r%d", a->imm, a->rd)
INSN(STDZ,   "Z+%d, r%d", a->imm, a->rd)
INSN(LPM1,   "")
INSN(LPM2,   "r%d, Z", a->rd)
INSN(LPMX,   "r%d, Z+", a->rd)
INSN(ELPM1,  "")
INSN(ELPM2,  "r%d, Z", a->rd)
INSN(ELPMX,  "r%d, Z+", a->rd)
INSN(SPM,    "")
INSN(SPMX,   "Z+")
INSN(IN,     "r%d, $%d", a->rd, a->imm)
INSN(OUT,    "$%d, r%d", a->imm, a->rd)
INSN(PUSH,   "r%d", a->rd)
INSN(POP,    "r%d", a->rd)
INSN(XCH,    "Z, r%d", a->rd)
INSN(LAC,    "Z, r%d", a->rd)
INSN(LAS,    "Z, r%d", a->rd)
INSN(LAT,    "Z, r%d", a->rd)

/*
 * Bit and Bit-test Instructions
 */
INSN(LSR,    "r%d", a->rd)
INSN(ROR,    "r%d", a->rd)
INSN(ASR,    "r%d", a->rd)
INSN(SWAP,   "r%d", a->rd)
INSN(SBI,    "$%d, %d", a->reg, a->bit)
INSN(CBI,    "%d, %d", a->reg, a->bit)
INSN(BST,    "r%d, %d", a->rd, a->bit)
INSN(BLD,    "r%d, %d", a->rd, a->bit)
INSN_MNEMONIC(BSET,  bset[a->bit], "")
INSN_MNEMONIC(BCLR,  bclr[a->bit], "")

/*
 * MCU Control Instructions
 */
INSN(BREAK,  "")
INSN(NOP,    "")
INSN(SLEEP,  "")
INSN(WDR,    "")
