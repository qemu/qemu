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
#define NUM_VEC_ELEMENT_BITS(es) (NUM_VEC_ELEMENT_BYTES(es) * BITS_PER_BYTE)

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


static void get_vec_element_ptr_i64(TCGv_ptr ptr, uint8_t reg, TCGv_i64 enr,
                                    uint8_t es)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    /* mask off invalid parts from the element nr */
    tcg_gen_andi_i64(tmp, enr, NUM_VEC_ELEMENTS(es) - 1);

    /* convert it to an element offset relative to cpu_env (vec_reg_offset() */
    tcg_gen_shli_i64(tmp, tmp, es);
#ifndef HOST_WORDS_BIGENDIAN
    tcg_gen_xori_i64(tmp, tmp, 8 - NUM_VEC_ELEMENT_BYTES(es));
#endif
    tcg_gen_addi_i64(tmp, tmp, vec_full_reg_offset(reg));

    /* generate the final ptr by adding cpu_env */
    tcg_gen_trunc_i64_ptr(ptr, tmp);
    tcg_gen_add_ptr(ptr, ptr, cpu_env);

    tcg_temp_free_i64(tmp);
}

#define gen_gvec_3_ool(v1, v2, v3, data, fn) \
    tcg_gen_gvec_3_ool(vec_full_reg_offset(v1), vec_full_reg_offset(v2), \
                       vec_full_reg_offset(v3), 16, 16, data, fn)
#define gen_gvec_3_ptr(v1, v2, v3, ptr, data, fn) \
    tcg_gen_gvec_3_ptr(vec_full_reg_offset(v1), vec_full_reg_offset(v2), \
                       vec_full_reg_offset(v3), ptr, 16, 16, data, fn)
#define gen_gvec_4(v1, v2, v3, v4, gen) \
    tcg_gen_gvec_4(vec_full_reg_offset(v1), vec_full_reg_offset(v2), \
                   vec_full_reg_offset(v3), vec_full_reg_offset(v4), \
                   16, 16, gen)
#define gen_gvec_4_ool(v1, v2, v3, v4, data, fn) \
    tcg_gen_gvec_4_ool(vec_full_reg_offset(v1), vec_full_reg_offset(v2), \
                       vec_full_reg_offset(v3), vec_full_reg_offset(v4), \
                       16, 16, data, fn)
#define gen_gvec_dup_i64(es, v1, c) \
    tcg_gen_gvec_dup_i64(es, vec_full_reg_offset(v1), 16, 16, c)
#define gen_gvec_mov(v1, v2) \
    tcg_gen_gvec_mov(0, vec_full_reg_offset(v1), vec_full_reg_offset(v2), 16, \
                     16)
#define gen_gvec_dup64i(v1, c) \
    tcg_gen_gvec_dup64i(vec_full_reg_offset(v1), 16, 16, c)

static void gen_gvec_dupi(uint8_t es, uint8_t reg, uint64_t c)
{
    switch (es) {
    case ES_8:
        tcg_gen_gvec_dup8i(vec_full_reg_offset(reg), 16, 16, c);
        break;
    case ES_16:
        tcg_gen_gvec_dup16i(vec_full_reg_offset(reg), 16, 16, c);
        break;
    case ES_32:
        tcg_gen_gvec_dup32i(vec_full_reg_offset(reg), 16, 16, c);
        break;
    case ES_64:
        gen_gvec_dup64i(reg, c);
        break;
    default:
        g_assert_not_reached();
    }
}

static void zero_vec(uint8_t reg)
{
    tcg_gen_gvec_dup8i(vec_full_reg_offset(reg), 16, 16, 0);
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

static uint64_t generate_byte_mask(uint8_t mask)
{
    uint64_t r = 0;
    int i;

    for (i = 0; i < 8; i++) {
        if ((mask >> i) & 1) {
            r |= 0xffull << (i * 8);
        }
    }
    return r;
}

static DisasJumpType op_vgbm(DisasContext *s, DisasOps *o)
{
    const uint16_t i2 = get_field(s->fields, i2);

    if (i2 == (i2 & 0xff) * 0x0101) {
        /*
         * Masks for both 64 bit elements of the vector are the same.
         * Trust tcg to produce a good constant loading.
         */
        gen_gvec_dup64i(get_field(s->fields, v1),
                        generate_byte_mask(i2 & 0xff));
    } else {
        TCGv_i64 t = tcg_temp_new_i64();

        tcg_gen_movi_i64(t, generate_byte_mask(i2 >> 8));
        write_vec_element_i64(t, get_field(s->fields, v1), 0, ES_64);
        tcg_gen_movi_i64(t, generate_byte_mask(i2));
        write_vec_element_i64(t, get_field(s->fields, v1), 1, ES_64);
        tcg_temp_free_i64(t);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_vgm(DisasContext *s, DisasOps *o)
{
    const uint8_t es = get_field(s->fields, m4);
    const uint8_t bits = NUM_VEC_ELEMENT_BITS(es);
    const uint8_t i2 = get_field(s->fields, i2) & (bits - 1);
    const uint8_t i3 = get_field(s->fields, i3) & (bits - 1);
    uint64_t mask = 0;
    int i;

    if (es > ES_64) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    /* generate the mask - take care of wrapping */
    for (i = i2; ; i = (i + 1) % bits) {
        mask |= 1ull << (bits - i - 1);
        if (i == i3) {
            break;
        }
    }

    gen_gvec_dupi(es, get_field(s->fields, v1), mask);
    return DISAS_NEXT;
}

static DisasJumpType op_vl(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_qemu_ld_i64(t0, o->addr1, get_mem_index(s), MO_TEQ);
    gen_addi_and_wrap_i64(s, o->addr1, o->addr1, 8);
    tcg_gen_qemu_ld_i64(t1, o->addr1, get_mem_index(s), MO_TEQ);
    write_vec_element_i64(t0, get_field(s->fields, v1), 0, ES_64);
    write_vec_element_i64(t1, get_field(s->fields, v1), 1, ES_64);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return DISAS_NEXT;
}

static DisasJumpType op_vlr(DisasContext *s, DisasOps *o)
{
    gen_gvec_mov(get_field(s->fields, v1), get_field(s->fields, v2));
    return DISAS_NEXT;
}

static DisasJumpType op_vlrep(DisasContext *s, DisasOps *o)
{
    const uint8_t es = get_field(s->fields, m3);
    TCGv_i64 tmp;

    if (es > ES_64) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tmp = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(tmp, o->addr1, get_mem_index(s), MO_TE | es);
    gen_gvec_dup_i64(es, get_field(s->fields, v1), tmp);
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static DisasJumpType op_vle(DisasContext *s, DisasOps *o)
{
    const uint8_t es = s->insn->data;
    const uint8_t enr = get_field(s->fields, m3);
    TCGv_i64 tmp;

    if (!valid_vec_element(enr, es)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tmp = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(tmp, o->addr1, get_mem_index(s), MO_TE | es);
    write_vec_element_i64(tmp, get_field(s->fields, v1), enr, es);
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static DisasJumpType op_vlei(DisasContext *s, DisasOps *o)
{
    const uint8_t es = s->insn->data;
    const uint8_t enr = get_field(s->fields, m3);
    TCGv_i64 tmp;

    if (!valid_vec_element(enr, es)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tmp = tcg_const_i64((int16_t)get_field(s->fields, i2));
    write_vec_element_i64(tmp, get_field(s->fields, v1), enr, es);
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static DisasJumpType op_vlgv(DisasContext *s, DisasOps *o)
{
    const uint8_t es = get_field(s->fields, m4);
    TCGv_ptr ptr;

    if (es > ES_64) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    /* fast path if we don't need the register content */
    if (!get_field(s->fields, b2)) {
        uint8_t enr = get_field(s->fields, d2) & (NUM_VEC_ELEMENTS(es) - 1);

        read_vec_element_i64(o->out, get_field(s->fields, v3), enr, es);
        return DISAS_NEXT;
    }

    ptr = tcg_temp_new_ptr();
    get_vec_element_ptr_i64(ptr, get_field(s->fields, v3), o->addr1, es);
    switch (es) {
    case ES_8:
        tcg_gen_ld8u_i64(o->out, ptr, 0);
        break;
    case ES_16:
        tcg_gen_ld16u_i64(o->out, ptr, 0);
        break;
    case ES_32:
        tcg_gen_ld32u_i64(o->out, ptr, 0);
        break;
    case ES_64:
        tcg_gen_ld_i64(o->out, ptr, 0);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_temp_free_ptr(ptr);

    return DISAS_NEXT;
}

static DisasJumpType op_vllez(DisasContext *s, DisasOps *o)
{
    uint8_t es = get_field(s->fields, m3);
    uint8_t enr;
    TCGv_i64 t;

    switch (es) {
    /* rightmost sub-element of leftmost doubleword */
    case ES_8:
        enr = 7;
        break;
    case ES_16:
        enr = 3;
        break;
    case ES_32:
        enr = 1;
        break;
    case ES_64:
        enr = 0;
        break;
    /* leftmost sub-element of leftmost doubleword */
    case 6:
        if (s390_has_feat(S390_FEAT_VECTOR_ENH)) {
            es = ES_32;
            enr = 0;
            break;
        }
    default:
        /* fallthrough */
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(t, o->addr1, get_mem_index(s), MO_TE | es);
    zero_vec(get_field(s->fields, v1));
    write_vec_element_i64(t, get_field(s->fields, v1), enr, es);
    tcg_temp_free_i64(t);
    return DISAS_NEXT;
}

static DisasJumpType op_vlm(DisasContext *s, DisasOps *o)
{
    const uint8_t v3 = get_field(s->fields, v3);
    uint8_t v1 = get_field(s->fields, v1);
    TCGv_i64 t0, t1;

    if (v3 < v1 || (v3 - v1 + 1) > 16) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    /*
     * Check for possible access exceptions by trying to load the last
     * element. The first element will be checked first next.
     */
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_addi_and_wrap_i64(s, t0, o->addr1, (v3 - v1) * 16 + 8);
    tcg_gen_qemu_ld_i64(t0, t0, get_mem_index(s), MO_TEQ);

    for (;; v1++) {
        tcg_gen_qemu_ld_i64(t1, o->addr1, get_mem_index(s), MO_TEQ);
        write_vec_element_i64(t1, v1, 0, ES_64);
        if (v1 == v3) {
            break;
        }
        gen_addi_and_wrap_i64(s, o->addr1, o->addr1, 8);
        tcg_gen_qemu_ld_i64(t1, o->addr1, get_mem_index(s), MO_TEQ);
        write_vec_element_i64(t1, v1, 1, ES_64);
        gen_addi_and_wrap_i64(s, o->addr1, o->addr1, 8);
    }

    /* Store the last element, loaded first */
    write_vec_element_i64(t0, v1, 1, ES_64);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    return DISAS_NEXT;
}

static DisasJumpType op_vlbb(DisasContext *s, DisasOps *o)
{
    const int64_t block_size = (1ull << (get_field(s->fields, m3) + 6));
    const int v1_offs = vec_full_reg_offset(get_field(s->fields, v1));
    TCGv_ptr a0;
    TCGv_i64 bytes;

    if (get_field(s->fields, m3) > 6) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    bytes = tcg_temp_new_i64();
    a0 = tcg_temp_new_ptr();
    /* calculate the number of bytes until the next block boundary */
    tcg_gen_ori_i64(bytes, o->addr1, -block_size);
    tcg_gen_neg_i64(bytes, bytes);

    tcg_gen_addi_ptr(a0, cpu_env, v1_offs);
    gen_helper_vll(cpu_env, a0, o->addr1, bytes);
    tcg_temp_free_i64(bytes);
    tcg_temp_free_ptr(a0);
    return DISAS_NEXT;
}

static DisasJumpType op_vlvg(DisasContext *s, DisasOps *o)
{
    const uint8_t es = get_field(s->fields, m4);
    TCGv_ptr ptr;

    if (es > ES_64) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    /* fast path if we don't need the register content */
    if (!get_field(s->fields, b2)) {
        uint8_t enr = get_field(s->fields, d2) & (NUM_VEC_ELEMENTS(es) - 1);

        write_vec_element_i64(o->in2, get_field(s->fields, v1), enr, es);
        return DISAS_NEXT;
    }

    ptr = tcg_temp_new_ptr();
    get_vec_element_ptr_i64(ptr, get_field(s->fields, v1), o->addr1, es);
    switch (es) {
    case ES_8:
        tcg_gen_st8_i64(o->in2, ptr, 0);
        break;
    case ES_16:
        tcg_gen_st16_i64(o->in2, ptr, 0);
        break;
    case ES_32:
        tcg_gen_st32_i64(o->in2, ptr, 0);
        break;
    case ES_64:
        tcg_gen_st_i64(o->in2, ptr, 0);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_temp_free_ptr(ptr);

    return DISAS_NEXT;
}

static DisasJumpType op_vlvgp(DisasContext *s, DisasOps *o)
{
    write_vec_element_i64(o->in1, get_field(s->fields, v1), 0, ES_64);
    write_vec_element_i64(o->in2, get_field(s->fields, v1), 1, ES_64);
    return DISAS_NEXT;
}

static DisasJumpType op_vll(DisasContext *s, DisasOps *o)
{
    const int v1_offs = vec_full_reg_offset(get_field(s->fields, v1));
    TCGv_ptr a0 = tcg_temp_new_ptr();

    /* convert highest index into an actual length */
    tcg_gen_addi_i64(o->in2, o->in2, 1);
    tcg_gen_addi_ptr(a0, cpu_env, v1_offs);
    gen_helper_vll(cpu_env, a0, o->addr1, o->in2);
    tcg_temp_free_ptr(a0);
    return DISAS_NEXT;
}

static DisasJumpType op_vmr(DisasContext *s, DisasOps *o)
{
    const uint8_t v1 = get_field(s->fields, v1);
    const uint8_t v2 = get_field(s->fields, v2);
    const uint8_t v3 = get_field(s->fields, v3);
    const uint8_t es = get_field(s->fields, m4);
    int dst_idx, src_idx;
    TCGv_i64 tmp;

    if (es > ES_64) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tmp = tcg_temp_new_i64();
    if (s->fields->op2 == 0x61) {
        /* iterate backwards to avoid overwriting data we might need later */
        for (dst_idx = NUM_VEC_ELEMENTS(es) - 1; dst_idx >= 0; dst_idx--) {
            src_idx = dst_idx / 2;
            if (dst_idx % 2 == 0) {
                read_vec_element_i64(tmp, v2, src_idx, es);
            } else {
                read_vec_element_i64(tmp, v3, src_idx, es);
            }
            write_vec_element_i64(tmp, v1, dst_idx, es);
        }
    } else {
        /* iterate forward to avoid overwriting data we might need later */
        for (dst_idx = 0; dst_idx < NUM_VEC_ELEMENTS(es); dst_idx++) {
            src_idx = (dst_idx + NUM_VEC_ELEMENTS(es)) / 2;
            if (dst_idx % 2 == 0) {
                read_vec_element_i64(tmp, v2, src_idx, es);
            } else {
                read_vec_element_i64(tmp, v3, src_idx, es);
            }
            write_vec_element_i64(tmp, v1, dst_idx, es);
        }
    }
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static DisasJumpType op_vpk(DisasContext *s, DisasOps *o)
{
    const uint8_t v1 = get_field(s->fields, v1);
    const uint8_t v2 = get_field(s->fields, v2);
    const uint8_t v3 = get_field(s->fields, v3);
    const uint8_t es = get_field(s->fields, m4);
    static gen_helper_gvec_3 * const vpk[3] = {
        gen_helper_gvec_vpk16,
        gen_helper_gvec_vpk32,
        gen_helper_gvec_vpk64,
    };
     static gen_helper_gvec_3 * const vpks[3] = {
        gen_helper_gvec_vpks16,
        gen_helper_gvec_vpks32,
        gen_helper_gvec_vpks64,
    };
    static gen_helper_gvec_3_ptr * const vpks_cc[3] = {
        gen_helper_gvec_vpks_cc16,
        gen_helper_gvec_vpks_cc32,
        gen_helper_gvec_vpks_cc64,
    };
    static gen_helper_gvec_3 * const vpkls[3] = {
        gen_helper_gvec_vpkls16,
        gen_helper_gvec_vpkls32,
        gen_helper_gvec_vpkls64,
    };
    static gen_helper_gvec_3_ptr * const vpkls_cc[3] = {
        gen_helper_gvec_vpkls_cc16,
        gen_helper_gvec_vpkls_cc32,
        gen_helper_gvec_vpkls_cc64,
    };

    if (es == ES_8 || es > ES_64) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    switch (s->fields->op2) {
    case 0x97:
        if (get_field(s->fields, m5) & 0x1) {
            gen_gvec_3_ptr(v1, v2, v3, cpu_env, 0, vpks_cc[es - 1]);
            set_cc_static(s);
        } else {
            gen_gvec_3_ool(v1, v2, v3, 0, vpks[es - 1]);
        }
        break;
    case 0x95:
        if (get_field(s->fields, m5) & 0x1) {
            gen_gvec_3_ptr(v1, v2, v3, cpu_env, 0, vpkls_cc[es - 1]);
            set_cc_static(s);
        } else {
            gen_gvec_3_ool(v1, v2, v3, 0, vpkls[es - 1]);
        }
        break;
    case 0x94:
        /* If sources and destination dont't overlap -> fast path */
        if (v1 != v2 && v1 != v3) {
            const uint8_t src_es = get_field(s->fields, m4);
            const uint8_t dst_es = src_es - 1;
            TCGv_i64 tmp = tcg_temp_new_i64();
            int dst_idx, src_idx;

            for (dst_idx = 0; dst_idx < NUM_VEC_ELEMENTS(dst_es); dst_idx++) {
                src_idx = dst_idx;
                if (src_idx < NUM_VEC_ELEMENTS(src_es)) {
                    read_vec_element_i64(tmp, v2, src_idx, src_es);
                } else {
                    src_idx -= NUM_VEC_ELEMENTS(src_es);
                    read_vec_element_i64(tmp, v3, src_idx, src_es);
                }
                write_vec_element_i64(tmp, v1, dst_idx, dst_es);
            }
            tcg_temp_free_i64(tmp);
        } else {
            gen_gvec_3_ool(v1, v2, v3, 0, vpk[es - 1]);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return DISAS_NEXT;
}

static DisasJumpType op_vperm(DisasContext *s, DisasOps *o)
{
    gen_gvec_4_ool(get_field(s->fields, v1), get_field(s->fields, v2),
                   get_field(s->fields, v3), get_field(s->fields, v4),
                   0, gen_helper_gvec_vperm);
    return DISAS_NEXT;
}

static DisasJumpType op_vpdi(DisasContext *s, DisasOps *o)
{
    const uint8_t i2 = extract32(get_field(s->fields, m4), 2, 1);
    const uint8_t i3 = extract32(get_field(s->fields, m4), 0, 1);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    read_vec_element_i64(t0, get_field(s->fields, v2), i2, ES_64);
    read_vec_element_i64(t1, get_field(s->fields, v3), i3, ES_64);
    write_vec_element_i64(t0, get_field(s->fields, v1), 0, ES_64);
    write_vec_element_i64(t1, get_field(s->fields, v1), 1, ES_64);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    return DISAS_NEXT;
}

static DisasJumpType op_vrep(DisasContext *s, DisasOps *o)
{
    const uint8_t enr = get_field(s->fields, i2);
    const uint8_t es = get_field(s->fields, m4);

    if (es > ES_64 || !valid_vec_element(enr, es)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tcg_gen_gvec_dup_mem(es, vec_full_reg_offset(get_field(s->fields, v1)),
                         vec_reg_offset(get_field(s->fields, v3), enr, es),
                         16, 16);
    return DISAS_NEXT;
}

static DisasJumpType op_vrepi(DisasContext *s, DisasOps *o)
{
    const int64_t data = (int16_t)get_field(s->fields, i2);
    const uint8_t es = get_field(s->fields, m3);

    if (es > ES_64) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    gen_gvec_dupi(es, get_field(s->fields, v1), data);
    return DISAS_NEXT;
}

static DisasJumpType op_vsce(DisasContext *s, DisasOps *o)
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

    read_vec_element_i64(tmp, get_field(s->fields, v1), enr, es);
    tcg_gen_qemu_st_i64(tmp, o->addr1, get_mem_index(s), MO_TE | es);
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static void gen_sel_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b, TCGv_i64 c)
{
    TCGv_i64 t = tcg_temp_new_i64();

    /* bit in c not set -> copy bit from b */
    tcg_gen_andc_i64(t, b, c);
    /* bit in c set -> copy bit from a */
    tcg_gen_and_i64(d, a, c);
    /* merge the results */
    tcg_gen_or_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_sel_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b,
                        TCGv_vec c)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_andc_vec(vece, t, b, c);
    tcg_gen_and_vec(vece, d, a, c);
    tcg_gen_or_vec(vece, d, d, t);
    tcg_temp_free_vec(t);
}

static DisasJumpType op_vsel(DisasContext *s, DisasOps *o)
{
    static const GVecGen4 gvec_op = {
        .fni8 = gen_sel_i64,
        .fniv = gen_sel_vec,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };

    gen_gvec_4(get_field(s->fields, v1), get_field(s->fields, v2),
               get_field(s->fields, v3), get_field(s->fields, v4), &gvec_op);
    return DISAS_NEXT;
}

static DisasJumpType op_vseg(DisasContext *s, DisasOps *o)
{
    const uint8_t es = get_field(s->fields, m3);
    int idx1, idx2;
    TCGv_i64 tmp;

    switch (es) {
    case ES_8:
        idx1 = 7;
        idx2 = 15;
        break;
    case ES_16:
        idx1 = 3;
        idx2 = 7;
        break;
    case ES_32:
        idx1 = 1;
        idx2 = 3;
        break;
    default:
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tmp = tcg_temp_new_i64();
    read_vec_element_i64(tmp, get_field(s->fields, v2), idx1, es | MO_SIGN);
    write_vec_element_i64(tmp, get_field(s->fields, v1), 0, ES_64);
    read_vec_element_i64(tmp, get_field(s->fields, v2), idx2, es | MO_SIGN);
    write_vec_element_i64(tmp, get_field(s->fields, v1), 1, ES_64);
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static DisasJumpType op_vst(DisasContext *s, DisasOps *o)
{
    TCGv_i64 tmp = tcg_const_i64(16);

    /* Probe write access before actually modifying memory */
    gen_helper_probe_write_access(cpu_env, o->addr1, tmp);

    read_vec_element_i64(tmp,  get_field(s->fields, v1), 0, ES_64);
    tcg_gen_qemu_st_i64(tmp, o->addr1, get_mem_index(s), MO_TEQ);
    gen_addi_and_wrap_i64(s, o->addr1, o->addr1, 8);
    read_vec_element_i64(tmp,  get_field(s->fields, v1), 1, ES_64);
    tcg_gen_qemu_st_i64(tmp, o->addr1, get_mem_index(s), MO_TEQ);
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static DisasJumpType op_vste(DisasContext *s, DisasOps *o)
{
    const uint8_t es = s->insn->data;
    const uint8_t enr = get_field(s->fields, m3);
    TCGv_i64 tmp;

    if (!valid_vec_element(enr, es)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tmp = tcg_temp_new_i64();
    read_vec_element_i64(tmp, get_field(s->fields, v1), enr, es);
    tcg_gen_qemu_st_i64(tmp, o->addr1, get_mem_index(s), MO_TE | es);
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static DisasJumpType op_vstm(DisasContext *s, DisasOps *o)
{
    const uint8_t v3 = get_field(s->fields, v3);
    uint8_t v1 = get_field(s->fields, v1);
    TCGv_i64 tmp;

    while (v3 < v1 || (v3 - v1 + 1) > 16) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    /* Probe write access before actually modifying memory */
    tmp = tcg_const_i64((v3 - v1 + 1) * 16);
    gen_helper_probe_write_access(cpu_env, o->addr1, tmp);

    for (;; v1++) {
        read_vec_element_i64(tmp, v1, 0, ES_64);
        tcg_gen_qemu_st_i64(tmp, o->addr1, get_mem_index(s), MO_TEQ);
        gen_addi_and_wrap_i64(s, o->addr1, o->addr1, 8);
        read_vec_element_i64(tmp, v1, 1, ES_64);
        tcg_gen_qemu_st_i64(tmp, o->addr1, get_mem_index(s), MO_TEQ);
        if (v1 == v3) {
            break;
        }
        gen_addi_and_wrap_i64(s, o->addr1, o->addr1, 8);
    }
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static DisasJumpType op_vstl(DisasContext *s, DisasOps *o)
{
    const int v1_offs = vec_full_reg_offset(get_field(s->fields, v1));
    TCGv_ptr a0 = tcg_temp_new_ptr();

    /* convert highest index into an actual length */
    tcg_gen_addi_i64(o->in2, o->in2, 1);
    tcg_gen_addi_ptr(a0, cpu_env, v1_offs);
    gen_helper_vstl(cpu_env, a0, o->addr1, o->in2);
    tcg_temp_free_ptr(a0);
    return DISAS_NEXT;
}

static DisasJumpType op_vup(DisasContext *s, DisasOps *o)
{
    const bool logical = s->fields->op2 == 0xd4 || s->fields->op2 == 0xd5;
    const uint8_t v1 = get_field(s->fields, v1);
    const uint8_t v2 = get_field(s->fields, v2);
    const uint8_t src_es = get_field(s->fields, m3);
    const uint8_t dst_es = src_es + 1;
    int dst_idx, src_idx;
    TCGv_i64 tmp;

    if (src_es > ES_32) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tmp = tcg_temp_new_i64();
    if (s->fields->op2 == 0xd7 || s->fields->op2 == 0xd5) {
        /* iterate backwards to avoid overwriting data we might need later */
        for (dst_idx = NUM_VEC_ELEMENTS(dst_es) - 1; dst_idx >= 0; dst_idx--) {
            src_idx = dst_idx;
            read_vec_element_i64(tmp, v2, src_idx,
                                 src_es | (logical ? 0 : MO_SIGN));
            write_vec_element_i64(tmp, v1, dst_idx, dst_es);
        }

    } else {
        /* iterate forward to avoid overwriting data we might need later */
        for (dst_idx = 0; dst_idx < NUM_VEC_ELEMENTS(dst_es); dst_idx++) {
            src_idx = dst_idx + NUM_VEC_ELEMENTS(src_es) / 2;
            read_vec_element_i64(tmp, v2, src_idx,
                                 src_es | (logical ? 0 : MO_SIGN));
            write_vec_element_i64(tmp, v1, dst_idx, dst_es);
        }
    }
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}
