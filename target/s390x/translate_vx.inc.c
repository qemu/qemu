/*
 * QEMU TCG support -- s390x vector instruction translation functions
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * For most instructions that use the same element size for reads and
 * writes, we can use real gvec vector expansion, which potantially uses
 * real host vector instructions. As they only work up to 64 bit elements,
 * 128 bit elements (vector is a single element) have to be handled
 * differently. Operations that are too complicated to encode via TCG ops
 * are handled via gvec ool (out-of-line) handlers.
 *
 * As soon as instructions use different element sizes for reads and writes
 * or access elements "out of their element scope" we expand them manually
 * in fancy loops, as gvec expansion does not deal with actual element
 * numbers and does also not support access to other elements.
 *
 * 128 bit elements:
 *  As we only have i32/i64, such elements have to be loaded into two
 *  i64 values and can then be processed e.g. by tcg_gen_add2_i64.
 *
 * Sizes:
 *  On s390x, the operand size (oprsz) and the maximum size (maxsz) are
 *  always 16 (128 bit). What gvec code calls "vece", s390x calls "es",
 *  a.k.a. "element size". These values nicely map to MO_8 ... MO_64. Only
 *  128 bit element size has to be treated in a special way (MO_64 + 1).
 *  We will use ES_* instead of MO_* for this reason in this file.
 *
 * CC handling:
 *  As gvec ool-helpers can currently not return values (besides via
 *  pointers like vectors or cpu_env), whenever we have to set the CC and
 *  can't conclude the value from the result vector, we will directly
 *  set it in "env->cc_op" and mark it as static via set_cc_static()".
 *  Whenever this is done, the helper writes globals (cc_op).
 */

#define NUM_VEC_ELEMENT_BYTES(es) (1 << (es))
#define NUM_VEC_ELEMENTS(es) (16 / NUM_VEC_ELEMENT_BYTES(es))

#define ES_8    MO_8
#define ES_16   MO_16
#define ES_32   MO_32
#define ES_64   MO_64
#define ES_128  4

static inline bool valid_vec_element(uint8_t enr, TCGMemOp es)
{
    return !(enr & ~(NUM_VEC_ELEMENTS(es) - 1));
}

static void read_vec_element_i64(TCGv_i64 dst, uint8_t reg, uint8_t enr,
                                 TCGMemOp memop)
{
    const int offs = vec_reg_offset(reg, enr, memop & MO_SIZE);

    switch (memop) {
    case ES_8:
        tcg_gen_ld8u_i64(dst, cpu_env, offs);
        break;
    case ES_16:
        tcg_gen_ld16u_i64(dst, cpu_env, offs);
        break;
    case ES_32:
        tcg_gen_ld32u_i64(dst, cpu_env, offs);
        break;
    case ES_8 | MO_SIGN:
        tcg_gen_ld8s_i64(dst, cpu_env, offs);
        break;
    case ES_16 | MO_SIGN:
        tcg_gen_ld16s_i64(dst, cpu_env, offs);
        break;
    case ES_32 | MO_SIGN:
        tcg_gen_ld32s_i64(dst, cpu_env, offs);
        break;
    case ES_64:
    case ES_64 | MO_SIGN:
        tcg_gen_ld_i64(dst, cpu_env, offs);
        break;
    default:
        g_assert_not_reached();
    }
}

static void write_vec_element_i64(TCGv_i64 src, int reg, uint8_t enr,
                                  TCGMemOp memop)
{
    const int offs = vec_reg_offset(reg, enr, memop & MO_SIZE);

    switch (memop) {
    case ES_8:
        tcg_gen_st8_i64(src, cpu_env, offs);
        break;
    case ES_16:
        tcg_gen_st16_i64(src, cpu_env, offs);
        break;
    case ES_32:
        tcg_gen_st32_i64(src, cpu_env, offs);
        break;
    case ES_64:
        tcg_gen_st_i64(src, cpu_env, offs);
        break;
    default:
        g_assert_not_reached();
    }
}

static DisasJumpType op_vge(DisasContext *s, DisasOps *o)
{
    const uint8_t es = s->insn->data;
    const uint8_t enr = get_field(s->fields, m3);
    TCGv_i64 tmp;

    if (!valid_vec_element(enr, es)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tmp = tcg_temp_new_i64();
    read_vec_element_i64(tmp, get_field(s->fields, v2), enr, es);
    tcg_gen_add_i64(o->addr1, o->addr1, tmp);
    gen_addi_and_wrap_i64(s, o->addr1, o->addr1, 0);

    tcg_gen_qemu_ld_i64(tmp, o->addr1, get_mem_index(s), MO_TE | es);
    write_vec_element_i64(tmp, get_field(s->fields, v1), enr, es);
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}
