/*
 *  i386 on i386 translation
 * 
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "config.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

#ifdef USE_CODE_COPY

#include <signal.h>
#include <sys/mman.h>
#include <sys/ucontext.h>

extern char exec_loop;

/* operand size */
enum {
    OT_BYTE = 0,
    OT_WORD,
    OT_LONG, 
    OT_QUAD,
};

#define PREFIX_REPZ   0x01
#define PREFIX_REPNZ  0x02
#define PREFIX_LOCK   0x04
#define PREFIX_DATA   0x08
#define PREFIX_ADR    0x10

typedef struct DisasContext {
    /* current insn context */
    int override; /* -1 if no override */
    int prefix;
    int aflag, dflag;
    target_ulong pc; /* pc = eip + cs_base */
    int is_jmp; /* 1 = means jump (stop translation), 2 means CPU
                   static state change (stop translation) */
    /* code output */
    uint8_t *gen_code_ptr;
    uint8_t *gen_code_start;
    
    /* current block context */
    target_ulong cs_base; /* base of CS segment */
    int pe;     /* protected mode */
    int code32; /* 32 bit code segment */
    int f_st;   /* currently unused */
    int vm86;   /* vm86 mode */
    int cpl;
    int iopl;
    int flags;
    struct TranslationBlock *tb;
} DisasContext;

#define CPU_FIELD_OFFSET(field) offsetof(CPUState, field)

#define CPU_SEG 0x64 /* fs override */

static inline void gb(DisasContext *s, uint32_t val)
{
    *s->gen_code_ptr++ = val;
}

static inline void gw(DisasContext *s, uint32_t val)
{
    *s->gen_code_ptr++ = val;
    *s->gen_code_ptr++ = val >> 8;
}

static inline void gl(DisasContext *s, uint32_t val)
{
    *s->gen_code_ptr++ = val;
    *s->gen_code_ptr++ = val >> 8;
    *s->gen_code_ptr++ = val >> 16;
    *s->gen_code_ptr++ = val >> 24;
}

static inline void gjmp(DisasContext *s, long val)
{
    gb(s, 0xe9); /* jmp */
    gl(s, val - (long)(s->gen_code_ptr + 4));
}

static inline void gen_movl_addr_im(DisasContext *s, 
                                    uint32_t addr, uint32_t val)
{
    gb(s, CPU_SEG); /* seg movl im, addr */
    gb(s, 0xc7); 
    gb(s, 0x05);
    gl(s, addr);
    gl(s, val);
}

static inline void gen_movw_addr_im(DisasContext *s, 
                                    uint32_t addr, uint32_t val)
{
    gb(s, CPU_SEG); /* seg movl im, addr */
    gb(s, 0x66); 
    gb(s, 0xc7); 
    gb(s, 0x05);
    gl(s, addr);
    gw(s, val);
}


static void gen_jmp(DisasContext *s, uint32_t target_eip)
{
    TranslationBlock *tb = s->tb;

    gb(s, 0xe9); /* jmp */
    tb->tb_jmp_offset[0] = s->gen_code_ptr - s->gen_code_start;
    gl(s, 0);

    tb->tb_next_offset[0] = s->gen_code_ptr - s->gen_code_start;
    gen_movl_addr_im(s, CPU_FIELD_OFFSET(eip), target_eip);
    gen_movl_addr_im(s, CPU_FIELD_OFFSET(tmp0), (uint32_t)tb);
    gjmp(s, (long)&exec_loop);

    s->is_jmp = 1;
}

static void gen_jcc(DisasContext *s, int op,
                    uint32_t target_eip, uint32_t next_eip)
{
    TranslationBlock *tb = s->tb;

    gb(s, 0x0f); /* jcc */
    gb(s, 0x80 + op);
    tb->tb_jmp_offset[0] = s->gen_code_ptr - s->gen_code_start;
    gl(s, 0);
    gb(s, 0xe9); /* jmp */
    tb->tb_jmp_offset[1] = s->gen_code_ptr - s->gen_code_start;
    gl(s, 0);
    
    tb->tb_next_offset[0] = s->gen_code_ptr - s->gen_code_start;
    gen_movl_addr_im(s, CPU_FIELD_OFFSET(eip), target_eip);
    gen_movl_addr_im(s, CPU_FIELD_OFFSET(tmp0), (uint32_t)tb);
    gjmp(s, (long)&exec_loop);

    tb->tb_next_offset[1] = s->gen_code_ptr - s->gen_code_start;
    gen_movl_addr_im(s, CPU_FIELD_OFFSET(eip), next_eip);
    gen_movl_addr_im(s, CPU_FIELD_OFFSET(tmp0), (uint32_t)tb | 1);
    gjmp(s, (long)&exec_loop);

    s->is_jmp = 1;
}

static void gen_eob(DisasContext *s)
{
    gen_movl_addr_im(s, CPU_FIELD_OFFSET(tmp0), 0);
    gjmp(s, (long)&exec_loop);

    s->is_jmp = 1;
}

static inline void gen_lea_modrm(DisasContext *s, int modrm)
{
    int havesib;
    int base, disp;
    int index;
    int scale;
    int mod, rm, code;

    mod = (modrm >> 6) & 3;
    rm = modrm & 7;

    if (s->aflag) {

        havesib = 0;
        base = rm;
        index = 0;
        scale = 0;
        
        if (base == 4) {
            havesib = 1;
            code = ldub_code(s->pc++);
            scale = (code >> 6) & 3;
            index = (code >> 3) & 7;
            base = code & 7;
        }

        switch (mod) {
        case 0:
            if (base == 5) {
                base = -1;
                disp = ldl_code(s->pc);
                s->pc += 4;
            } else {
                disp = 0;
            }
            break;
        case 1:
            disp = (int8_t)ldub_code(s->pc++);
            break;
        default:
        case 2:
            disp = ldl_code(s->pc);
            s->pc += 4;
            break;
        }
        
    } else {
        switch (mod) {
        case 0:
            if (rm == 6) {
                disp = lduw_code(s->pc);
                s->pc += 2;
            } else {
                disp = 0;
            }
            break;
        case 1:
            disp = (int8_t)ldub_code(s->pc++);
            break;
        default:
        case 2:
            disp = lduw_code(s->pc);
            s->pc += 2;
            break;
        }
    }
}

static inline void parse_modrm(DisasContext *s, int modrm)
{
    if ((modrm & 0xc0) != 0xc0)
        gen_lea_modrm(s, modrm);        
}

static inline uint32_t insn_get(DisasContext *s, int ot)
{
    uint32_t ret;

    switch(ot) {
    case OT_BYTE:
        ret = ldub_code(s->pc);
        s->pc++;
        break;
    case OT_WORD:
        ret = lduw_code(s->pc);
        s->pc += 2;
        break;
    default:
    case OT_LONG:
        ret = ldl_code(s->pc);
        s->pc += 4;
        break;
    }
    return ret;
}

/* convert one instruction. s->is_jmp is set if the translation must
   be stopped.  */
static int disas_insn(DisasContext *s)
{
    target_ulong pc_start, pc_tmp, pc_start_insn;
    int b, prefixes, aflag, dflag, next_eip, val;
    int ot;
    int modrm, mod, op, rm;

    pc_start = s->pc;
    prefixes = 0;
    aflag = s->code32;
    dflag = s->code32;
    s->override = -1;
 next_byte:
    b = ldub_code(s->pc);
    s->pc++;
    /* check prefixes */
    switch (b) {
    case 0xf3:
        prefixes |= PREFIX_REPZ;
        goto next_byte;
    case 0xf2:
        prefixes |= PREFIX_REPNZ;
        goto next_byte;
    case 0xf0:
        prefixes |= PREFIX_LOCK;
        goto next_byte;
    case 0x2e:
        s->override = R_CS;
        goto next_byte;
    case 0x36:
        s->override = R_SS;
        goto next_byte;
    case 0x3e:
        s->override = R_DS;
        goto next_byte;
    case 0x26:
        s->override = R_ES;
        goto next_byte;
    case 0x64:
        s->override = R_FS;
        goto next_byte;
    case 0x65:
        s->override = R_GS;
        goto next_byte;
    case 0x66:
        prefixes |= PREFIX_DATA;
        goto next_byte;
    case 0x67:
        prefixes |= PREFIX_ADR;
        goto next_byte;
    }

    if (prefixes & PREFIX_DATA)
        dflag ^= 1;
    if (prefixes & PREFIX_ADR)
        aflag ^= 1;

    s->prefix = prefixes;
    s->aflag = aflag;
    s->dflag = dflag;

    /* lock generation */
    if (prefixes & PREFIX_LOCK)
        goto unsupported_op;
    if (s->override == R_FS || s->override == R_GS || s->override == R_CS)
        goto unsupported_op;

    pc_start_insn = s->pc - 1;
    /* now check op code */
 reswitch:
    switch(b) {
    case 0x0f:
        /**************************/
        /* extended op code */
        b = ldub_code(s->pc++) | 0x100;
        goto reswitch;
        
        /**************************/
        /* arith & logic */
    case 0x00 ... 0x05:
    case 0x08 ... 0x0d:
    case 0x10 ... 0x15:
    case 0x18 ... 0x1d:
    case 0x20 ... 0x25:
    case 0x28 ... 0x2d:
    case 0x30 ... 0x35:
    case 0x38 ... 0x3d:
        {
            int f;
            f = (b >> 1) & 3;

            if ((b & 1) == 0)
                ot = OT_BYTE;
            else
                ot = dflag ? OT_LONG : OT_WORD;
            
            switch(f) {
            case 0: /* OP Ev, Gv */
                modrm = ldub_code(s->pc++);
                parse_modrm(s, modrm);
                break;
            case 1: /* OP Gv, Ev */
                modrm = ldub_code(s->pc++);
                parse_modrm(s, modrm);
                break;
            case 2: /* OP A, Iv */
                insn_get(s, ot);
                break;
            }
        }
        break;

    case 0x80: /* GRP1 */
    case 0x81:
    case 0x82:
    case 0x83:
        {
            if ((b & 1) == 0)
                ot = OT_BYTE;
            else
                ot = dflag ? OT_LONG : OT_WORD;
            
            modrm = ldub_code(s->pc++);
            parse_modrm(s, modrm);

            switch(b) {
            default:
            case 0x80:
            case 0x81:
            case 0x82:
                insn_get(s, ot);
                break;
            case 0x83:
                insn_get(s, OT_BYTE);
                break;
            }
        }
        break;

        /**************************/
        /* inc, dec, and other misc arith */
    case 0x40 ... 0x47: /* inc Gv */
        break;
    case 0x48 ... 0x4f: /* dec Gv */
        break;
    case 0xf6: /* GRP3 */
    case 0xf7:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;

        modrm = ldub_code(s->pc++);
        op = (modrm >> 3) & 7;
        parse_modrm(s, modrm);

        switch(op) {
        case 0: /* test */
            insn_get(s, ot);
            break;
        case 2: /* not */
            break;
        case 3: /* neg */
            break;
        case 4: /* mul */
            break;
        case 5: /* imul */
            break;
        case 6: /* div */
            break;
        case 7: /* idiv */
            break;
        default:
            goto illegal_op;
        }
        break;

    case 0xfe: /* GRP4 */
    case 0xff: /* GRP5 */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;

        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        op = (modrm >> 3) & 7;
        if (op >= 2 && b == 0xfe) {
            goto illegal_op;
        }
        pc_tmp = s->pc;
        parse_modrm(s, modrm);

        switch(op) {
        case 0: /* inc Ev */
            break;
        case 1: /* dec Ev */
            break;
        case 2: /* call Ev */
            /* XXX: optimize and handle MEM exceptions specifically
               fs movl %eax, regs[0] 
               movl Ev, %eax 
               pushl next_eip
               fs movl %eax, eip
            */
            goto unsupported_op;
        case 3: /* lcall Ev */
            goto unsupported_op;
        case 4: /* jmp Ev */
            /* XXX: optimize and handle MEM exceptions specifically
               fs movl %eax, regs[0] 
               movl Ev, %eax 
               fs movl %eax, eip
            */
            goto unsupported_op;
        case 5: /* ljmp Ev */
            goto unsupported_op;
        case 6: /* push Ev */
            break;
        default:
            goto illegal_op;
        }
        break;
    case 0xa8: /* test eAX, Iv */
    case 0xa9:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        insn_get(s, ot);
        break;
        
    case 0x98: /* CWDE/CBW */
        break;
    case 0x99: /* CDQ/CWD */
        break;
    case 0x1af: /* imul Gv, Ev */
    case 0x69: /* imul Gv, Ev, I */
    case 0x6b:
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        parse_modrm(s, modrm);
        if (b == 0x69) {
            insn_get(s, ot);
        } else if (b == 0x6b) {
            insn_get(s, OT_BYTE);
        } else {
        }
        break;

    case 0x84: /* test Ev, Gv */
    case 0x85: 
        
    case 0x1c0:
    case 0x1c1: /* xadd Ev, Gv */

    case 0x1b0:
    case 0x1b1: /* cmpxchg Ev, Gv */

    case 0x8f: /* pop Ev */

    case 0x88:
    case 0x89: /* mov Gv, Ev */

    case 0x8a:
    case 0x8b: /* mov Ev, Gv */

    case 0x1b6: /* movzbS Gv, Eb */
    case 0x1b7: /* movzwS Gv, Eb */
    case 0x1be: /* movsbS Gv, Eb */
    case 0x1bf: /* movswS Gv, Eb */

    case 0x86:
    case 0x87: /* xchg Ev, Gv */

    case 0xd0:
    case 0xd1: /* shift Ev,1 */

    case 0xd2:
    case 0xd3: /* shift Ev,cl */

    case 0x1a5: /* shld cl */
    case 0x1ad: /* shrd cl */

    case 0x190 ... 0x19f: /* setcc Gv */

    /* XXX: emulate cmov if not available ? */
    case 0x140 ... 0x14f: /* cmov Gv, Ev */

    case 0x1a3: /* bt Gv, Ev */
    case 0x1ab: /* bts */
    case 0x1b3: /* btr */
    case 0x1bb: /* btc */

    case 0x1bc: /* bsf */
    case 0x1bd: /* bsr */

        modrm = ldub_code(s->pc++);
        parse_modrm(s, modrm);
        break;

    case 0x1c7: /* cmpxchg8b */
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        if (mod == 3)
            goto illegal_op;
        parse_modrm(s, modrm);
        break;
        
        /**************************/
        /* push/pop */
    case 0x50 ... 0x57: /* push */
    case 0x58 ... 0x5f: /* pop */
    case 0x60: /* pusha */
    case 0x61: /* popa */
        break;

    case 0x68: /* push Iv */
    case 0x6a:
        ot = dflag ? OT_LONG : OT_WORD;
        if (b == 0x68)
            insn_get(s, ot);
        else
            insn_get(s, OT_BYTE);
        break;
    case 0xc8: /* enter */
        lduw_code(s->pc);
        s->pc += 2;
        ldub_code(s->pc++);
        break;
    case 0xc9: /* leave */
        break;

    case 0x06: /* push es */
    case 0x0e: /* push cs */
    case 0x16: /* push ss */
    case 0x1e: /* push ds */
        /* XXX: optimize:
         push segs[n].selector
        */
        goto unsupported_op;
    case 0x1a0: /* push fs */
    case 0x1a8: /* push gs */
        goto unsupported_op;
    case 0x07: /* pop es */
    case 0x17: /* pop ss */
    case 0x1f: /* pop ds */
        goto unsupported_op;
    case 0x1a1: /* pop fs */
    case 0x1a9: /* pop gs */
        goto unsupported_op;
    case 0x8e: /* mov seg, Gv */
        /* XXX: optimize:
           fs movl r, regs[]
           movl segs[].selector, r
           mov r, Gv
           fs movl regs[], r
        */
        goto unsupported_op;
    case 0x8c: /* mov Gv, seg */
        goto unsupported_op;
    case 0xc4: /* les Gv */
        op = R_ES;
        goto do_lxx;
    case 0xc5: /* lds Gv */
        op = R_DS;
        goto do_lxx;
    case 0x1b2: /* lss Gv */
        op = R_SS;
        goto do_lxx;
    case 0x1b4: /* lfs Gv */
        op = R_FS;
        goto do_lxx;
    case 0x1b5: /* lgs Gv */
        op = R_GS;
    do_lxx:
        goto unsupported_op;
        /************************/
        /* floats */
    case 0xd8 ... 0xdf: 
#if 1
        /* currently not stable enough */
        goto unsupported_op;
#else
        if (s->flags & (HF_EM_MASK | HF_TS_MASK))
            goto unsupported_op;
#endif
#if 0
        /* for testing FPU context switch */
        {
            static int count;
            count = (count + 1) % 3;
            if (count != 0)
                goto unsupported_op;
        }
#endif
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        op = ((b & 7) << 3) | ((modrm >> 3) & 7);
        if (mod != 3) {
            /* memory op */
            parse_modrm(s, modrm);
            switch(op) {
            case 0x00 ... 0x07: /* fxxxs */
            case 0x10 ... 0x17: /* fixxxl */
            case 0x20 ... 0x27: /* fxxxl */
            case 0x30 ... 0x37: /* fixxx */
                break;
            case 0x08: /* flds */
            case 0x0a: /* fsts */
            case 0x0b: /* fstps */
            case 0x18: /* fildl */
            case 0x1a: /* fistl */
            case 0x1b: /* fistpl */
            case 0x28: /* fldl */
            case 0x2a: /* fstl */
            case 0x2b: /* fstpl */
            case 0x38: /* filds */
            case 0x3a: /* fists */
            case 0x3b: /* fistps */
            case 0x0c: /* fldenv mem */
            case 0x0d: /* fldcw mem */
            case 0x0e: /* fnstenv mem */
            case 0x0f: /* fnstcw mem */
            case 0x1d: /* fldt mem */
            case 0x1f: /* fstpt mem */
            case 0x2c: /* frstor mem */
            case 0x2e: /* fnsave mem */
            case 0x2f: /* fnstsw mem */
            case 0x3c: /* fbld */
            case 0x3e: /* fbstp */
            case 0x3d: /* fildll */
            case 0x3f: /* fistpll */
                break;
            default:
                goto illegal_op;
            }
        } else {
            /* register float ops */
            switch(op) {
            case 0x08: /* fld sti */
            case 0x09: /* fxchg sti */
                break;
            case 0x0a: /* grp d9/2 */
                switch(rm) {
                case 0: /* fnop */
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x0c: /* grp d9/4 */
                switch(rm) {
                case 0: /* fchs */
                case 1: /* fabs */
                case 4: /* ftst */
                case 5: /* fxam */
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x0d: /* grp d9/5 */
                switch(rm) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x0e: /* grp d9/6 */
                break;
            case 0x0f: /* grp d9/7 */
                break;
            case 0x00: case 0x01: case 0x04 ... 0x07: /* fxxx st, sti */
            case 0x20: case 0x21: case 0x24 ... 0x27: /* fxxx sti, st */
            case 0x30: case 0x31: case 0x34 ... 0x37: /* fxxxp sti, st */
                break;
            case 0x02: /* fcom */
                break;
            case 0x03: /* fcomp */
                break;
            case 0x15: /* da/5 */
                switch(rm) {
                case 1: /* fucompp */
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x1c:
                switch(rm) {
                case 0: /* feni (287 only, just do nop here) */
                case 1: /* fdisi (287 only, just do nop here) */
                    goto unsupported_op;
                case 2: /* fclex */
                case 3: /* fninit */
                case 4: /* fsetpm (287 only, just do nop here) */
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x1d: /* fucomi */
                break;
            case 0x1e: /* fcomi */
                break;
            case 0x28: /* ffree sti */
                break;
            case 0x2a: /* fst sti */
                break;
            case 0x2b: /* fstp sti */
                break;
            case 0x2c: /* fucom st(i) */
                break;
            case 0x2d: /* fucomp st(i) */
                break;
            case 0x33: /* de/3 */
                switch(rm) {
                case 1: /* fcompp */
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x3c: /* df/4 */
                switch(rm) {
                case 0:
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x3d: /* fucomip */
                break;
            case 0x3e: /* fcomip */
                break;
            case 0x10 ... 0x13: /* fcmovxx */
            case 0x18 ... 0x1b:
                break;
            default:
                goto illegal_op;
            }
        }
        s->tb->cflags |= CF_TB_FP_USED;
        break;

        /**************************/
        /* mov */
    case 0xc6:
    case 0xc7: /* mov Ev, Iv */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        parse_modrm(s, modrm);
        insn_get(s, ot);
        break;

    case 0x8d: /* lea */
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        if (mod == 3)
            goto illegal_op;
        parse_modrm(s, modrm);
        break;
        
    case 0xa0: /* mov EAX, Ov */
    case 0xa1:
    case 0xa2: /* mov Ov, EAX */
    case 0xa3:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (s->aflag)
            insn_get(s, OT_LONG);
        else
            insn_get(s, OT_WORD);
        break;
    case 0xd7: /* xlat */
        break;
    case 0xb0 ... 0xb7: /* mov R, Ib */
        insn_get(s, OT_BYTE);
        break;
    case 0xb8 ... 0xbf: /* mov R, Iv */
        ot = dflag ? OT_LONG : OT_WORD;
        insn_get(s, ot);
        break;

    case 0x91 ... 0x97: /* xchg R, EAX */
        break;

        /************************/
        /* shifts */
    case 0xc0:
    case 0xc1: /* shift Ev,imm */

    case 0x1a4: /* shld imm */
    case 0x1ac: /* shrd imm */
        modrm = ldub_code(s->pc++);
        parse_modrm(s, modrm);
        ldub_code(s->pc++);
        break;
        
        /************************/
        /* string ops */

    case 0xa4: /* movsS */
    case 0xa5:
        break;
        
    case 0xaa: /* stosS */
    case 0xab:
        break;

    case 0xac: /* lodsS */
    case 0xad:
        break;

    case 0xae: /* scasS */
    case 0xaf:
        break;

    case 0xa6: /* cmpsS */
    case 0xa7:
        break;

    case 0x6c: /* insS */
    case 0x6d:
        goto unsupported_op;

    case 0x6e: /* outsS */
    case 0x6f:
        goto unsupported_op;

        /************************/
        /* port I/O */
    case 0xe4:
    case 0xe5:
        goto unsupported_op;

    case 0xe6:
    case 0xe7:
        goto unsupported_op;

    case 0xec:
    case 0xed:
        goto unsupported_op;

    case 0xee:
    case 0xef:
        goto unsupported_op;

        /************************/
        /* control */
#if 0
    case 0xc2: /* ret im */
        val = ldsw_code(s->pc);
        s->pc += 2;
        gen_pop_T0(s);
        gen_stack_update(s, val + (2 << s->dflag));
        if (s->dflag == 0)
            gen_op_andl_T0_ffff();
        gen_op_jmp_T0();
        gen_eob(s);
        break;
#endif

    case 0xc3: /* ret */
        gb(s, CPU_SEG);
        if (!s->dflag)  
            gb(s, 0x66); /* d16 */
        gb(s, 0x8f); /* pop addr */
        gb(s, 0x05);
        gl(s, CPU_FIELD_OFFSET(eip));
        if (!s->dflag) {
            /* reset high bits of EIP */
            gen_movw_addr_im(s, CPU_FIELD_OFFSET(eip) + 2, 0);
        }
        gen_eob(s);
        goto no_copy;
    case 0xca: /* lret im */
    case 0xcb: /* lret */
    case 0xcf: /* iret */
    case 0x9a: /* lcall im */
    case 0xea: /* ljmp im */
        goto unsupported_op;

    case 0xe8: /* call im */
        ot = dflag ? OT_LONG : OT_WORD;
        val = insn_get(s, ot);
        next_eip = s->pc - s->cs_base;
        val += next_eip;
        if (s->dflag) {
            gb(s, 0x68); /* pushl imm */
            gl(s, next_eip);
        } else {
            gb(s, 0x66); /* pushw imm */
            gb(s, 0x68);
            gw(s, next_eip);
            val &= 0xffff;
        }
        gen_jmp(s, val);
        goto no_copy;
    case 0xe9: /* jmp */
        ot = dflag ? OT_LONG : OT_WORD;
        val = insn_get(s, ot);
        val += s->pc - s->cs_base;
        if (s->dflag == 0)
            val = val & 0xffff;
        gen_jmp(s, val);
        goto no_copy;
    case 0xeb: /* jmp Jb */
        val = (int8_t)insn_get(s, OT_BYTE);
        val += s->pc - s->cs_base;
        if (s->dflag == 0)
            val = val & 0xffff;
        gen_jmp(s, val);
        goto no_copy;
    case 0x70 ... 0x7f: /* jcc Jb */
        val = (int8_t)insn_get(s, OT_BYTE);
        goto do_jcc;
    case 0x180 ... 0x18f: /* jcc Jv */
        if (dflag) {
            val = insn_get(s, OT_LONG);
        } else {
            val = (int16_t)insn_get(s, OT_WORD); 
        }
    do_jcc:
        next_eip = s->pc - s->cs_base;
        val += next_eip;
        if (s->dflag == 0)
            val &= 0xffff;
        gen_jcc(s, b & 0xf, val, next_eip);
        goto no_copy;

        /************************/
        /* flags */
    case 0x9c: /* pushf */
        /* XXX: put specific code ? */
        goto unsupported_op;
    case 0x9d: /* popf */
        goto unsupported_op;

    case 0x9e: /* sahf */
    case 0x9f: /* lahf */
    case 0xf5: /* cmc */
    case 0xf8: /* clc */
    case 0xf9: /* stc */
    case 0xfc: /* cld */
    case 0xfd: /* std */
        break;

        /************************/
        /* bit operations */
    case 0x1ba: /* bt/bts/btr/btc Gv, im */
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        op = (modrm >> 3) & 7;
        parse_modrm(s, modrm);
        /* load shift */
        ldub_code(s->pc++);
        if (op < 4)
            goto illegal_op;
        break;
        /************************/
        /* bcd */
    case 0x27: /* daa */
        break;
    case 0x2f: /* das */
        break;
    case 0x37: /* aaa */
        break;
    case 0x3f: /* aas */
        break;
    case 0xd4: /* aam */
        ldub_code(s->pc++);
        break;
    case 0xd5: /* aad */
        ldub_code(s->pc++);
        break;
        /************************/
        /* misc */
    case 0x90: /* nop */
        break;
    case 0x9b: /* fwait */
        if ((s->flags & (HF_MP_MASK | HF_TS_MASK)) == 
            (HF_MP_MASK | HF_TS_MASK)) {
            goto unsupported_op;
        }
        break;
    case 0xcc: /* int3 */
        goto unsupported_op;
    case 0xcd: /* int N */
        goto unsupported_op;
    case 0xce: /* into */
        goto unsupported_op;
    case 0xf1: /* icebp (undocumented, exits to external debugger) */
        goto unsupported_op;
    case 0xfa: /* cli */
        goto unsupported_op;
    case 0xfb: /* sti */
        goto unsupported_op;
    case 0x62: /* bound */
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        if (mod == 3)
            goto illegal_op;
        parse_modrm(s, modrm);
        break;
    case 0x1c8 ... 0x1cf: /* bswap reg */
        break;
    case 0xd6: /* salc */
        break;
    case 0xe0: /* loopnz */
    case 0xe1: /* loopz */
    case 0xe2: /* loop */
    case 0xe3: /* jecxz */
        goto unsupported_op;

    case 0x130: /* wrmsr */
    case 0x132: /* rdmsr */
        goto unsupported_op;
    case 0x131: /* rdtsc */
        goto unsupported_op;
    case 0x1a2: /* cpuid */
        goto unsupported_op;
    case 0xf4: /* hlt */
        goto unsupported_op;
    case 0x100:
        goto unsupported_op;
    case 0x101:
        goto unsupported_op;
    case 0x108: /* invd */
    case 0x109: /* wbinvd */
        goto unsupported_op;
    case 0x63: /* arpl */
        goto unsupported_op;
    case 0x102: /* lar */
    case 0x103: /* lsl */
        goto unsupported_op;
    case 0x118:
        goto unsupported_op;
    case 0x120: /* mov reg, crN */
    case 0x122: /* mov crN, reg */
        goto unsupported_op;
    case 0x121: /* mov reg, drN */
    case 0x123: /* mov drN, reg */
        goto unsupported_op;
    case 0x106: /* clts */
        goto unsupported_op;
    default:
        goto illegal_op;
    }

    /* just copy the code */

    /* no override yet */
    if (!s->dflag)
        gb(s, 0x66);
    if (!s->aflag)
        gb(s, 0x67);
    if (prefixes & PREFIX_REPZ)
        gb(s, 0xf3);
    else if (prefixes & PREFIX_REPNZ)
        gb(s, 0xf2);
    {
        int len, i;
        len = s->pc - pc_start_insn;
        for(i = 0; i < len; i++) {
            *s->gen_code_ptr++ = ldub_code(pc_start_insn + i);
        }
    }
 no_copy:
    return 0;
 illegal_op:
 unsupported_op:
    /* fall back to slower code gen necessary */
    s->pc = pc_start;
    return -1;
}

#define GEN_CODE_MAX_SIZE      8192
#define GEN_CODE_MAX_INSN_SIZE 512

static inline int gen_intermediate_code_internal(CPUState *env,
                                                 TranslationBlock *tb, 
                                                 uint8_t *gen_code_ptr,
                                                 int *gen_code_size_ptr,
                                                 int search_pc,
                                                 uint8_t *tc_ptr)
{
    DisasContext dc1, *dc = &dc1;
    target_ulong pc_insn, pc_start, cs_base;
    uint8_t *gen_code_end;
    int flags, ret;

    if (env->nb_breakpoints > 0 ||
        env->singlestep_enabled)
        return -1;
    flags = tb->flags;
    if (flags & (HF_TF_MASK | HF_ADDSEG_MASK | 
                 HF_SOFTMMU_MASK | HF_INHIBIT_IRQ_MASK))
        return -1;
    if (!(flags & HF_SS32_MASK))
        return -1;
    if (tb->cflags & CF_SINGLE_INSN)
        return -1;
    gen_code_end = gen_code_ptr + 
        GEN_CODE_MAX_SIZE - GEN_CODE_MAX_INSN_SIZE;
    dc->gen_code_ptr = gen_code_ptr;
    dc->gen_code_start = gen_code_ptr;

    /* generate intermediate code */
    pc_start = tb->pc;
    cs_base = tb->cs_base;
    dc->pc = pc_start;
    dc->cs_base = cs_base;
    dc->pe = (flags >> HF_PE_SHIFT) & 1;
    dc->code32 = (flags >> HF_CS32_SHIFT) & 1;
    dc->f_st = 0;
    dc->vm86 = (flags >> VM_SHIFT) & 1;
    dc->cpl = (flags >> HF_CPL_SHIFT) & 3;
    dc->iopl = (flags >> IOPL_SHIFT) & 3;
    dc->tb = tb;
    dc->flags = flags;

    dc->is_jmp = 0;

    for(;;) {
        pc_insn = dc->pc;
        ret = disas_insn(dc);
        if (ret < 0) {
            /* unsupported insn */
            if (dc->pc == pc_start) {
                /* if first instruction, signal that no copying was done */
                return -1;
            } else {
                gen_jmp(dc, dc->pc - dc->cs_base);
                dc->is_jmp = 1;
            }
        }
        if (search_pc) {
            /* search pc mode */
            if (tc_ptr < dc->gen_code_ptr) {
                env->eip = pc_insn - cs_base;
                return 0;
            }
        }
        /* stop translation if indicated */
        if (dc->is_jmp)
            break;
        /* if too long translation, stop generation */
        if (dc->gen_code_ptr >= gen_code_end ||
            (dc->pc - pc_start) >= (TARGET_PAGE_SIZE - 32)) {
            gen_jmp(dc, dc->pc - dc->cs_base);
            break;
        }
    }
    
#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "----------------\n");
        fprintf(logfile, "IN: COPY: %s fpu=%d\n", 
                lookup_symbol(pc_start),
                tb->cflags & CF_TB_FP_USED ? 1 : 0);
	target_disas(logfile, pc_start, dc->pc - pc_start, !dc->code32);
        fprintf(logfile, "\n");
    }
#endif

    if (!search_pc) {
        *gen_code_size_ptr = dc->gen_code_ptr - dc->gen_code_start;
        tb->size = dc->pc - pc_start;
        tb->cflags |= CF_CODE_COPY;
        return 0;
    } else {
        return -1;
    }
}

/* generate code by just copying data. Return -1 if cannot generate
   any code. Return 0 if code was generated */
int cpu_gen_code_copy(CPUState *env, TranslationBlock *tb,
                      int max_code_size, int *gen_code_size_ptr)
{
    /* generate machine code */
    tb->tb_next_offset[0] = 0xffff;
    tb->tb_next_offset[1] = 0xffff;
#ifdef USE_DIRECT_JUMP
    /* the following two entries are optional (only used for string ops) */
    tb->tb_jmp_offset[2] = 0xffff;
    tb->tb_jmp_offset[3] = 0xffff;
#endif
    return gen_intermediate_code_internal(env, tb, 
                                          tb->tc_ptr, gen_code_size_ptr,
                                          0, NULL);
}

static uint8_t dummy_gen_code_buf[GEN_CODE_MAX_SIZE];

int cpu_restore_state_copy(TranslationBlock *tb, 
                           CPUState *env, unsigned long searched_pc,
                           void *puc)
{
    struct ucontext *uc = puc;
    int ret, eflags;

    /* find opc index corresponding to search_pc */
    if (searched_pc < (unsigned long)tb->tc_ptr)
        return -1;
    searched_pc = searched_pc - (long)tb->tc_ptr + (long)dummy_gen_code_buf;
    ret = gen_intermediate_code_internal(env, tb, 
                                         dummy_gen_code_buf, NULL,
                                         1, (uint8_t *)searched_pc);
    if (ret < 0)
        return ret;
    /* restore all the CPU state from the CPU context from the
       signal. The FPU context stays in the host CPU. */
    
    env->regs[R_EAX] = uc->uc_mcontext.gregs[REG_EAX];
    env->regs[R_ECX] = uc->uc_mcontext.gregs[REG_ECX];
    env->regs[R_EDX] = uc->uc_mcontext.gregs[REG_EDX];
    env->regs[R_EBX] = uc->uc_mcontext.gregs[REG_EBX];
    env->regs[R_ESP] = uc->uc_mcontext.gregs[REG_ESP];
    env->regs[R_EBP] = uc->uc_mcontext.gregs[REG_EBP];
    env->regs[R_ESI] = uc->uc_mcontext.gregs[REG_ESI];
    env->regs[R_EDI] = uc->uc_mcontext.gregs[REG_EDI];
    eflags = uc->uc_mcontext.gregs[REG_EFL];
    env->df = 1 - (2 * ((eflags >> 10) & 1));
    env->cc_src = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    env->cc_op = CC_OP_EFLAGS;
    return 0;
}

#endif /* USE_CODE_COPY */
