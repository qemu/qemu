/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009 Ulrich Hecht <uli@suse.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

static const int tcg_target_reg_alloc_order[] = {
};

static const int tcg_target_call_iarg_regs[] = {
};

static const int tcg_target_call_oarg_regs[] = {
};

static void patch_reloc(uint8_t *code_ptr, int type,
                tcg_target_long value, tcg_target_long addend)
{
    tcg_abort();
}

static inline int tcg_target_get_call_iarg_regs_count(int flags)
{
    tcg_abort();
    return 0;
}

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    tcg_abort();
    return 0;
}

/* Test if a constant matches the constraint. */
static inline int tcg_target_const_match(tcg_target_long val,
                const TCGArgConstraint *arg_ct)
{
    tcg_abort();
    return 0;
}

/* load a register with an immediate value */
static inline void tcg_out_movi(TCGContext *s, TCGType type,
                int ret, tcg_target_long arg)
{
    tcg_abort();
}

/* load data without address translation or endianness conversion */
static inline void tcg_out_ld(TCGContext *s, TCGType type, int arg,
                int arg1, tcg_target_long arg2)
{
    tcg_abort();
}

static inline void tcg_out_st(TCGContext *s, TCGType type, int arg,
                              int arg1, tcg_target_long arg2)
{
    tcg_abort();
}

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                const TCGArg *args, const int *const_args)
{
    tcg_abort();
}

void tcg_target_init(TCGContext *s)
{
    /* gets called with KVM */
}

void tcg_target_qemu_prologue(TCGContext *s)
{
    /* gets called with KVM */
}

static inline void tcg_out_mov(TCGContext *s, TCGType type, int ret, int arg)
{
    tcg_abort();
}

static inline void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    tcg_abort();
}
