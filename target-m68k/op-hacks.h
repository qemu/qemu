/* Various hacks to make code written for a dynamic code generator work
   with regular QEMU.  */

static int free_qreg;

#define QMODE_I32 1
#define QMODE_F32 1
#define QMODE_F64 2

static inline int gen_new_qreg(int mode)
{
    int qreg;

    qreg = free_qreg;
    free_qreg += mode;
    if (free_qreg > MAX_QREGS) {
        fprintf(stderr, "qreg overflow\n");
        abort();
    }
    return qreg + TARGET_NUM_QREGS;
}

static inline int gen_im32(uint32_t i)
{
    int qreg = gen_new_qreg(QMODE_I32);
    gen_op_mov32_im(qreg, i);
    return qreg;
}

static inline void gen_op_ldf32_raw(int dest, int addr)
{
    gen_op_ld32_raw(dest, addr);
}

static inline void gen_op_stf32_raw(int addr, int dest)
{
    gen_op_st32_raw(addr, dest);
}

#if !defined(CONFIG_USER_ONLY)
static inline void gen_op_ldf32_user(int dest, int addr)
{
    gen_op_ld32_user(dest, addr);
}

static inline void gen_op_stf32_user(int addr, int dest)
{
    gen_op_st32_user(addr, dest);
}

static inline void gen_op_ldf32_kernel(int dest, int addr)
{
    gen_op_ld32_kernel(dest, addr);
}

static inline void gen_op_stf32_kernel(int addr, int dest)
{
    gen_op_st32_kernel(addr, dest);
}
#endif

static inline void gen_op_pack_32_f32(int dest, int src)
{
    gen_op_mov32(dest, src);
}

static inline void gen_op_pack_f32_32(int dest, int src)
{
    gen_op_mov32(dest, src);
}

static inline void gen_op_flags_set(void)
{
    /* Dummy op.  */
}

static inline void gen_op_shl_im_cc(int val, int shift)
{
    gen_op_shl_cc(val, gen_im32(shift));
}

static inline void gen_op_shr_im_cc(int val, int shift)
{
    gen_op_shr_cc(val, gen_im32(shift));
}

static inline void gen_op_sar_im_cc(int val, int shift)
{
    gen_op_sar_cc(val, gen_im32(shift));
}

#ifdef USE_DIRECT_JUMP
#define TBPARAM(x)
#else
#define TBPARAM(x) (long)(x)
#endif

static inline void gen_op_goto_tb(int dummy, int n, long tb)
{
    if (n == 0) {
        gen_op_goto_tb0(TBPARAM(tb));
    } else {
        gen_op_goto_tb1(TBPARAM(tb));
    }
}

static inline void gen_op_jmp_z32(int val, int label)
{
    gen_op_set_T0_z32(val);
    gen_op_jmp_T0(label);
}

static inline void gen_op_jmp_nz32(int val, int label)
{
    gen_op_set_T0_nz32(val);
    gen_op_jmp_T0(label);
}

static inline void gen_op_jmp_s32(int val, int label)
{
    gen_op_set_T0_s32(val);
    gen_op_jmp_T0(label);
}

static inline void gen_op_jmp_ns32(int val, int label)
{
    gen_op_set_T0_ns32(val);
    gen_op_jmp_T0(label);
}

