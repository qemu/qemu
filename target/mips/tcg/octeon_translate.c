/*
 * Octeon-specific instructions translation routines
 *
 *  Copyright (c) 2022 Pavel Dovgalyuk
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"
#include "tcg/tcg-op-gvec.h"

/* Include the auto-generated decoder.  */
#include "decode-octeon.c.inc"

#define OCTEON_CRYPTO_OFFSET(FIELD) \
    offsetof(CPUMIPSState, octeon_crypto.FIELD)

#define CP2_MF_I64(NAME, FIELD) \
    TRANS(NAME, trans_octeon_cp2_mf_i64, OCTEON_CRYPTO_OFFSET(FIELD))
#define CP2_MF_S32(NAME, FIELD) \
    TRANS(NAME, trans_octeon_cp2_mf_s32, OCTEON_CRYPTO_OFFSET(FIELD))
#define CP2_MF_U16(NAME, FIELD) \
    TRANS(NAME, trans_octeon_cp2_mf_u16, OCTEON_CRYPTO_OFFSET(FIELD))
#define CP2_MF_U8(NAME, FIELD) \
    TRANS(NAME, trans_octeon_cp2_mf_u8, OCTEON_CRYPTO_OFFSET(FIELD))
#define CP2_MF_HSH_PAIR(NAME, FIELD, INDEX) \
    TRANS(NAME, trans_octeon_cp2_mf_hsh_pair, \
          OCTEON_CRYPTO_OFFSET(FIELD[2 * (INDEX)]), \
          OCTEON_CRYPTO_OFFSET(FIELD[2 * (INDEX) + 1]))
#define CP2_MF_REFLECT(NAME, FIELD) \
    TRANS(NAME, trans_octeon_cp2_mf_reflect, OCTEON_CRYPTO_OFFSET(FIELD))
#define CP2_MF_HELPER(NAME, SUFFIX) \
    TRANS(NAME, trans_octeon_cp2_mf_helper, \
          gen_helper_octeon_cp2_mf_ ## SUFFIX)
#define CP2_MT_I64(NAME, FIELD) \
    TRANS(NAME, trans_octeon_cp2_mt_i64, OCTEON_CRYPTO_OFFSET(FIELD))
#define CP2_MT_U32(NAME, FIELD) \
    TRANS(NAME, trans_octeon_cp2_mt_u32, OCTEON_CRYPTO_OFFSET(FIELD))
#define CP2_MT_U16(NAME, FIELD) \
    TRANS(NAME, trans_octeon_cp2_mt_u16, OCTEON_CRYPTO_OFFSET(FIELD))
#define CP2_MT_U8_MASKED(NAME, FIELD, MASK) \
    TRANS(NAME, trans_octeon_cp2_mt_u8_masked, \
          OCTEON_CRYPTO_OFFSET(FIELD), MASK)
#define CP2_MT_HSH_PAIR(NAME, FIELD, INDEX) \
    TRANS(NAME, trans_octeon_cp2_mt_hsh_pair, \
          OCTEON_CRYPTO_OFFSET(FIELD[2 * (INDEX)]), \
          OCTEON_CRYPTO_OFFSET(FIELD[2 * (INDEX) + 1]))
#define CP2_MT_REFLECT(NAME, FIELD) \
    TRANS(NAME, trans_octeon_cp2_mt_reflect, OCTEON_CRYPTO_OFFSET(FIELD))
#define CP2_MT_HELPER(NAME, SUFFIX) \
    TRANS(NAME, trans_octeon_cp2_mt_helper, \
          gen_helper_octeon_cp2_mt_ ## SUFFIX)
#define CP2_MT_HELPER_ENV(NAME, SUFFIX) \
    TRANS(NAME, trans_octeon_cp2_mt_helper_env, \
          gen_helper_octeon_cp2_mt_ ## SUFFIX)
#define CP2_MT_XOR_I64(NAME, FIELD) \
    TRANS(NAME, trans_octeon_cp2_mt_xor_i64, OCTEON_CRYPTO_OFFSET(FIELD))

#define OCTEON_LO32_OFFSET (HOST_BIG_ENDIAN ? 4 : 0)

static bool trans_CP2_Undef(DisasContext *ctx, arg_CP2_Undef *a)
{
    generate_exception_err(ctx, EXCP_CpU, 2);
    return true;
}

static bool trans_octeon_cp2_mf_i64(DisasContext *ctx, arg_cp2 *a, int offset)
{
    TCGv_i64 value = tcg_temp_new_i64();

    tcg_gen_ld_i64(value, tcg_env, offset);
    gen_store_gpr(value, a->rt);
    return true;
}

static bool trans_octeon_cp2_mf_s32(DisasContext *ctx, arg_cp2 *a, int offset)
{
    TCGv_i64 value = tcg_temp_new_i64();

    tcg_gen_ld32s_i64(value, tcg_env, offset);
    gen_store_gpr(value, a->rt);
    return true;
}

static bool trans_octeon_cp2_mf_u16(DisasContext *ctx, arg_cp2 *a, int offset)
{
    TCGv_i64 value = tcg_temp_new_i64();

    tcg_gen_ld16u_i64(value, tcg_env, offset);
    gen_store_gpr(value, a->rt);
    return true;
}

static bool trans_octeon_cp2_mf_u8(DisasContext *ctx, arg_cp2 *a, int offset)
{
    TCGv_i64 value = tcg_temp_new_i64();

    tcg_gen_ld8u_i64(value, tcg_env, offset);
    gen_store_gpr(value, a->rt);
    return true;
}

static bool trans_octeon_cp2_mf_hsh_pair(DisasContext *ctx, arg_cp2 *a,
                                         int hi_offset, int lo_offset)
{
    TCGv_i64 hi = tcg_temp_new_i64();
    TCGv_i64 lo = tcg_temp_new_i64();

    tcg_gen_ld_i64(hi, tcg_env, hi_offset);
    tcg_gen_ld_i64(lo, tcg_env, lo_offset);
    tcg_gen_concat32_i64(lo, lo, hi);
    gen_store_gpr(lo, a->rt);
    return true;
}

static bool trans_octeon_cp2_mf_reflect(DisasContext *ctx, arg_cp2 *a,
                                        int offset)
{
    TCGv_i64 value = tcg_temp_new_i64();

    tcg_gen_ld_i64(value, tcg_env, offset);
    tcg_gen_revbit64_i64(value, value);
    gen_store_gpr(value, a->rt);
    return true;
}

static bool trans_octeon_cp2_mf_helper(DisasContext *ctx, arg_cp2 *a,
                                       void (*gen_helper)(TCGv_i64, TCGv_env))
{
    TCGv_i64 value = tcg_temp_new_i64();

    gen_helper(value, tcg_env);
    gen_store_gpr(value, a->rt);
    return true;
}

static bool trans_octeon_cp2_mt_i64(DisasContext *ctx, arg_cp2 *a, int offset)
{
    TCGv_i64 value = tcg_temp_new_i64();

    gen_load_gpr(value, a->rt);
    tcg_gen_st_i64(value, tcg_env, offset);
    return true;
}

static bool trans_octeon_cp2_mt_u32(DisasContext *ctx, arg_cp2 *a, int offset)
{
    TCGv_i64 value = tcg_temp_new_i64();

    gen_load_gpr(value, a->rt);
    tcg_gen_st32_i64(value, tcg_env, offset);
    return true;
}

static bool trans_octeon_cp2_mt_u16(DisasContext *ctx, arg_cp2 *a, int offset)
{
    TCGv_i64 value = tcg_temp_new_i64();

    gen_load_gpr(value, a->rt);
    tcg_gen_st16_i64(value, tcg_env, offset);
    return true;
}

static bool trans_octeon_cp2_mt_u8_masked(DisasContext *ctx, arg_cp2 *a,
                                          int offset, uint8_t mask)
{
    TCGv_i64 value = tcg_temp_new_i64();

    gen_load_gpr(value, a->rt);
    tcg_gen_andi_i64(value, value, mask);
    tcg_gen_st8_i64(value, tcg_env, offset);
    return true;
}

static bool trans_octeon_cp2_mt_hsh_pair(DisasContext *ctx, arg_cp2 *a,
                                         int hi_offset, int lo_offset)
{
    TCGv_i64 value = tcg_temp_new_i64();

    gen_load_gpr(value, a->rt);
    tcg_gen_st32_i64(value, tcg_env, lo_offset + OCTEON_LO32_OFFSET);
    tcg_gen_shri_i64(value, value, 32);
    tcg_gen_st32_i64(value, tcg_env, hi_offset + OCTEON_LO32_OFFSET);
    return true;
}

static bool trans_octeon_cp2_mt_xor_i64(DisasContext *ctx, arg_cp2 *a,
                                        int offset)
{
    TCGv_i64 value = tcg_temp_new_i64();
    TCGv_i64 old = tcg_temp_new_i64();

    gen_load_gpr(value, a->rt);
    tcg_gen_ld_i64(old, tcg_env, offset);
    tcg_gen_xor_i64(old, old, value);
    tcg_gen_st_i64(old, tcg_env, offset);
    return true;
}

static bool trans_octeon_cp2_mt_reflect(DisasContext *ctx, arg_cp2 *a,
                                        int offset)
{
    TCGv_i64 value = tcg_temp_new_i64();

    gen_load_gpr(value, a->rt);
    tcg_gen_revbit64_i64(value, value);
    tcg_gen_st_i64(value, tcg_env, offset);
    return true;
}

static bool trans_octeon_cp2_mt_helper(DisasContext *ctx, arg_cp2 *a,
                                       void (*gen_helper)(TCGv_env, TCGv_i64))
{
    TCGv_i64 value = tcg_temp_new_i64();

    gen_load_gpr(value, a->rt);
    gen_helper(tcg_env, value);
    return true;
}

static bool trans_octeon_cp2_mt_helper_env(DisasContext *ctx, arg_cp2 *a,
                                           void (*gen_helper)(TCGv_env))
{
    gen_helper(tcg_env);
    return true;
}

static void gen_helper_octeon_cp2_mt_gfm_xor0_reflect(TCGv_env t_env,
                                                      TCGv_i64 value)
{
    TCGv_i64 resinp = tcg_temp_new_i64();

    tcg_gen_revbit64_i64(value, value);
    tcg_gen_ld_i64(resinp, t_env, OCTEON_CRYPTO_OFFSET(gfm_resinp[0]));
    tcg_gen_xor_i64(resinp, resinp, value);
    tcg_gen_st_i64(resinp, t_env, OCTEON_CRYPTO_OFFSET(gfm_resinp[0]));
}

CP2_MF_HSH_PAIR(CVM_MF_HSH_DAT0, hsh_dat, 0);
CP2_MF_HSH_PAIR(CVM_MF_HSH_DAT1, hsh_dat, 1);
CP2_MF_HSH_PAIR(CVM_MF_HSH_DAT2, hsh_dat, 2);
CP2_MF_HSH_PAIR(CVM_MF_HSH_DAT3, hsh_dat, 3);
CP2_MF_HSH_PAIR(CVM_MF_HSH_DAT4, hsh_dat, 4);
CP2_MF_HSH_PAIR(CVM_MF_HSH_DAT5, hsh_dat, 5);
CP2_MF_HSH_PAIR(CVM_MF_HSH_DAT6, hsh_dat, 6);
CP2_MF_HSH_PAIR(CVM_MF_HSH_IV0, hsh_iv, 0);
CP2_MF_HSH_PAIR(CVM_MF_HSH_IV1, hsh_iv, 1);
CP2_MF_HSH_PAIR(CVM_MF_HSH_IV2, hsh_iv, 2);
CP2_MF_HSH_PAIR(CVM_MF_HSH_IV3, hsh_iv, 3);
CP2_MF_I64(CVM_MF_3DES_KEY0, des3_key[0]);
CP2_MF_I64(CVM_MF_3DES_KEY1, des3_key[1]);
CP2_MF_I64(CVM_MF_3DES_KEY2, des3_key[2]);
CP2_MF_I64(CVM_MF_3DES_IV, des3_iv);
CP2_MF_I64(CVM_MF_3DES_RESULT, des3_result);
CP2_MF_I64(CVM_MF_KAS_RESULT, des3_result);
CP2_MF_I64(CVM_MF_AES_RESINP0, aes_resinp[0]);
CP2_MF_I64(CVM_MF_AES_RESINP1, aes_resinp[1]);
CP2_MF_I64(CVM_MF_AES_IV0, aes_iv[0]);
CP2_MF_I64(CVM_MF_AES_IV1, aes_iv[1]);
CP2_MF_I64(CVM_MF_AES_KEY0, aes_key[0]);
CP2_MF_I64(CVM_MF_AES_KEY1, aes_key[1]);
CP2_MF_I64(CVM_MF_AES_KEY2, aes_key[2]);
CP2_MF_I64(CVM_MF_AES_KEY3, aes_key[3]);
CP2_MF_U8(CVM_MF_AES_KEYLENGTH, aes_keylen);
CP2_MF_I64(CVM_MF_AES_INP0, aes_resinp[0]);
CP2_MF_S32(CVM_MF_CRC_POLYNOMIAL, crc_poly);
CP2_MF_S32(CVM_MF_CRC_IV, crc_iv);
CP2_MF_U8(CVM_MF_CRC_LEN, crc_len);
CP2_MF_I64(CVM_MF_GFM_MUL0, gfm_mul[0]);
CP2_MF_I64(CVM_MF_GFM_MUL1, gfm_mul[1]);
CP2_MF_I64(CVM_MF_GFM_RESINP0, gfm_resinp[0]);
CP2_MF_I64(CVM_MF_GFM_RESINP1, gfm_resinp[1]);
CP2_MF_U16(CVM_MF_GFM_POLY, gfm_poly);
CP2_MF_I64(CVM_MF_CHORD, chord);
CP2_MF_I64(CVM_MF_LLM_DATA0, llm_data[0]);
CP2_MF_I64(CVM_MF_LLM_DATA1, llm_data[1]);

CP2_MF_HELPER(CVM_MF_CRC_IV_REFLECT, crc_iv_reflect);
CP2_MF_I64(CVM_MF_SHA3_DAT24, sha3_dat24);
CP2_MF_REFLECT(CVM_MF_GFM_MUL_REFLECT0, gfm_mul[0])
CP2_MF_REFLECT(CVM_MF_GFM_MUL_REFLECT1, gfm_mul[1])
CP2_MF_REFLECT(CVM_MF_GFM_RESINP_REFLECT0, gfm_resinp[0])
CP2_MF_REFLECT(CVM_MF_GFM_RESINP_REFLECT1, gfm_resinp[1])
CP2_MF_I64(CVM_MF_HSH_DATW0, hsh_dat[0]);
CP2_MF_I64(CVM_MF_HSH_DATW1, hsh_dat[1]);
CP2_MF_I64(CVM_MF_HSH_DATW2, hsh_dat[2]);
CP2_MF_I64(CVM_MF_HSH_DATW3, hsh_dat[3]);
CP2_MF_I64(CVM_MF_HSH_DATW4, hsh_dat[4]);
CP2_MF_I64(CVM_MF_HSH_DATW5, hsh_dat[5]);
CP2_MF_I64(CVM_MF_HSH_DATW6, hsh_dat[6]);
CP2_MF_I64(CVM_MF_HSH_DATW7, hsh_dat[7]);
CP2_MF_I64(CVM_MF_HSH_DATW8, hsh_dat[8]);
CP2_MF_I64(CVM_MF_HSH_DATW9, hsh_dat[9]);
CP2_MF_I64(CVM_MF_HSH_DATW10, hsh_dat[10]);
CP2_MF_I64(CVM_MF_HSH_DATW11, hsh_dat[11]);
CP2_MF_I64(CVM_MF_HSH_DATW12, hsh_dat[12]);
CP2_MF_I64(CVM_MF_HSH_DATW13, hsh_dat[13]);
CP2_MF_I64(CVM_MF_HSH_DATW14, hsh_dat[14]);
CP2_MF_I64(CVM_MF_HSH_DATW15, hsh_dat[15]);
CP2_MF_I64(CVM_MF_HSH_IVW0, hsh_iv[0]);
CP2_MF_I64(CVM_MF_HSH_IVW1, hsh_iv[1]);
CP2_MF_I64(CVM_MF_HSH_IVW2, hsh_iv[2]);
CP2_MF_I64(CVM_MF_HSH_IVW3, hsh_iv[3]);
CP2_MF_I64(CVM_MF_HSH_IVW4, hsh_iv[4]);
CP2_MF_I64(CVM_MF_HSH_IVW5, hsh_iv[5]);
CP2_MF_I64(CVM_MF_HSH_IVW6, hsh_iv[6]);
CP2_MF_I64(CVM_MF_HSH_IVW7, hsh_iv[7]);

CP2_MT_HSH_PAIR(CVM_MT_HSH_DAT0, hsh_dat, 0);
CP2_MT_HSH_PAIR(CVM_MT_HSH_DAT1, hsh_dat, 1);
CP2_MT_HSH_PAIR(CVM_MT_HSH_DAT2, hsh_dat, 2);
CP2_MT_HSH_PAIR(CVM_MT_HSH_DAT3, hsh_dat, 3);
CP2_MT_HSH_PAIR(CVM_MT_HSH_DAT4, hsh_dat, 4);
CP2_MT_HSH_PAIR(CVM_MT_HSH_DAT5, hsh_dat, 5);
CP2_MT_HSH_PAIR(CVM_MT_HSH_DAT6, hsh_dat, 6);
CP2_MT_HSH_PAIR(CVM_MT_HSH_IV0, hsh_iv, 0);
CP2_MT_HSH_PAIR(CVM_MT_HSH_IV1, hsh_iv, 1);
CP2_MT_HSH_PAIR(CVM_MT_HSH_IV2, hsh_iv, 2);
CP2_MT_HSH_PAIR(CVM_MT_HSH_IV3, hsh_iv, 3);
CP2_MT_REFLECT(CVM_MT_GFM_MUL_REFLECT0, gfm_mul[0]);
CP2_MT_REFLECT(CVM_MT_GFM_MUL_REFLECT1, gfm_mul[1]);
CP2_MT_HELPER(CVM_MT_GFM_XOR0_REFLECT, gfm_xor0_reflect);
CP2_MT_I64(CVM_MT_3DES_KEY0, des3_key[0]);
CP2_MT_I64(CVM_MT_3DES_KEY1, des3_key[1]);
CP2_MT_I64(CVM_MT_3DES_KEY2, des3_key[2]);
CP2_MT_I64(CVM_MT_3DES_IV, des3_iv);
CP2_MT_I64(CVM_MT_3DES_RESULT, des3_result);
CP2_MT_I64(CVM_MT_AES_RESINP0, aes_resinp[0]);
CP2_MT_I64(CVM_MT_AES_RESINP1, aes_resinp[1]);
CP2_MT_I64(CVM_MT_AES_IV0, aes_iv[0]);
CP2_MT_I64(CVM_MT_AES_IV1, aes_iv[1]);
CP2_MT_I64(CVM_MT_AES_KEY0, aes_key[0]);
CP2_MT_I64(CVM_MT_AES_KEY1, aes_key[1]);
CP2_MT_I64(CVM_MT_AES_KEY2, aes_key[2]);
CP2_MT_I64(CVM_MT_AES_KEY3, aes_key[3]);
CP2_MT_I64(CVM_MT_AES_ENC_CBC0, aes_resinp[0]);
CP2_MT_I64(CVM_MT_AES_ENC0, aes_resinp[0]);
CP2_MT_I64(CVM_MT_AES_DEC_CBC0, aes_resinp[0]);
CP2_MT_I64(CVM_MT_AES_DEC0, aes_resinp[0]);
CP2_MT_U8_MASKED(CVM_MT_AES_KEYLENGTH, aes_keylen, 3);
CP2_MT_U32(CVM_MT_CRC_IV, crc_iv);
CP2_MT_I64(CVM_MT_GFM_MUL0, gfm_mul[0]);
CP2_MT_I64(CVM_MT_GFM_MUL1, gfm_mul[1]);
CP2_MT_I64(CVM_MT_GFM_RESINP0, gfm_resinp[0]);
CP2_MT_I64(CVM_MT_GFM_RESINP1, gfm_resinp[1]);
CP2_MT_XOR_I64(CVM_MT_GFM_XOR0, gfm_resinp[0]);
CP2_MT_U16(CVM_MT_GFM_POLY, gfm_poly);
CP2_MT_I64(CVM_MT_LLM_DATA0, llm_data[0]);
CP2_MT_I64(CVM_MT_LLM_DATA1, llm_data[1]);
CP2_MT_U8_MASKED(CVM_MT_CRC_LEN, crc_len, 0xf);
CP2_MT_U32(CVM_MT_CRC_POLYNOMIAL, crc_poly);

CP2_MT_HELPER(CVM_MT_CRC_POLYNOMIAL_REFLECT, crc_write_polynomial_reflect);

CP2_MT_HELPER(CVM_MT_CRC_IV_REFLECT, crc_write_iv_reflect);
CP2_MT_HELPER(CVM_MT_CRC_BYTE, crc_write_byte);
CP2_MT_HELPER(CVM_MT_CRC_HALF, crc_write_half);
CP2_MT_HELPER(CVM_MT_CRC_WORD, crc_write_word);
CP2_MT_HELPER(CVM_MT_CRC_BYTE_REFLECT, crc_write_byte_reflect);
CP2_MT_HELPER(CVM_MT_CRC_HALF_REFLECT, crc_write_half_reflect);
CP2_MT_HELPER(CVM_MT_CRC_WORD_REFLECT, crc_write_word_reflect);
CP2_MT_HELPER(CVM_MT_CRC_DWORD, crc_write_dword);
CP2_MT_HELPER(CVM_MT_CRC_VAR, crc_write_var);
CP2_MT_HELPER(CVM_MT_CRC_DWORD_REFLECT, crc_write_dword_reflect);
CP2_MT_HELPER(CVM_MT_CRC_VAR_REFLECT, crc_write_var_reflect);
CP2_MT_HELPER(CVM_MT_GFM_XORMUL1_REFLECT, gfm_xormul1_reflect);
CP2_MT_HELPER(CVM_MT_GFM_XORMUL1, gfm_xormul1);
CP2_MT_I64(CVM_MT_SHA3_DAT24, sha3_dat24);
CP2_MT_I64(CVM_MT_SHA3_DAT15, hsh_dat[15]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT0, sha3_dat[0]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT1, sha3_dat[1]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT2, sha3_dat[2]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT3, sha3_dat[3]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT4, sha3_dat[4]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT5, sha3_dat[5]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT6, sha3_dat[6]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT7, sha3_dat[7]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT8, sha3_dat[8]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT9, sha3_dat[9]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT10, sha3_dat[10]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT11, sha3_dat[11]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT12, sha3_dat[12]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT13, sha3_dat[13]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT14, sha3_dat[14]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT15, sha3_dat[15]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT16, sha3_dat[16]);
CP2_MT_XOR_I64(CVM_MT_SHA3_XORDAT17, sha3_dat[17]);
CP2_MT_HELPER_ENV(CVM_MT_SHA3_STARTOP, sha3_startop);
CP2_MT_HELPER(CVM_MT_ZUC_START, zuc_start);
CP2_MT_HELPER(CVM_MT_ZUC_MORE, zuc_more);
CP2_MT_HELPER(CVM_MT_SNOW3G_START, snow3g_start);
CP2_MT_HELPER(CVM_MT_SNOW3G_MORE, snow3g_more);
CP2_MT_HELPER(CVM_MT_AES_ENC_CBC1, aes_enc_cbc1);
CP2_MT_HELPER(CVM_MT_AES_ENC1, aes_enc1);
CP2_MT_HELPER(CVM_MT_AES_DEC_CBC1, aes_dec_cbc1);
CP2_MT_HELPER(CVM_MT_AES_DEC1, aes_dec1);
CP2_MT_HELPER(CVM_MT_SMS4_ENC_CBC1, sms4_enc_cbc1);
CP2_MT_HELPER(CVM_MT_SMS4_ENC1, sms4_enc1);
CP2_MT_HELPER(CVM_MT_SMS4_DEC_CBC1, sms4_dec_cbc1);
CP2_MT_HELPER(CVM_MT_SMS4_DEC1, sms4_dec1);
CP2_MT_HELPER(CVM_MT_3DES_ENC_CBC, des3_enc_cbc);
CP2_MT_HELPER(CVM_MT_KAS_ENC_CBC, kas_enc_cbc);
CP2_MT_HELPER(CVM_MT_3DES_ENC, des3_enc);
CP2_MT_HELPER(CVM_MT_KAS_ENC, kas_enc);
CP2_MT_HELPER(CVM_MT_3DES_DEC_CBC, des3_dec_cbc);
CP2_MT_HELPER(CVM_MT_3DES_DEC, des3_dec);
CP2_MT_HELPER(CVM_MT_CAMELLIA_FL, camellia_fl);
CP2_MT_HELPER(CVM_MT_CAMELLIA_FLINV, camellia_flinv);
CP2_MT_HELPER(CVM_MT_CAMELLIA_ROUND, camellia_round);
CP2_MT_HELPER(CVM_MT_HSH_STARTSHA1_COMPAT, hsh_startsha1_compat);
CP2_MT_I64(CVM_MT_HSH_DATW0, hsh_dat[0]);
CP2_MT_I64(CVM_MT_HSH_DATW1, hsh_dat[1]);
CP2_MT_I64(CVM_MT_HSH_DATW2, hsh_dat[2]);
CP2_MT_I64(CVM_MT_HSH_DATW3, hsh_dat[3]);
CP2_MT_I64(CVM_MT_HSH_DATW4, hsh_dat[4]);
CP2_MT_I64(CVM_MT_HSH_DATW5, hsh_dat[5]);
CP2_MT_I64(CVM_MT_HSH_DATW6, hsh_dat[6]);
CP2_MT_I64(CVM_MT_HSH_DATW7, hsh_dat[7]);
CP2_MT_I64(CVM_MT_HSH_DATW8, hsh_dat[8]);
CP2_MT_I64(CVM_MT_HSH_DATW9, hsh_dat[9]);
CP2_MT_I64(CVM_MT_HSH_DATW10, hsh_dat[10]);
CP2_MT_I64(CVM_MT_HSH_DATW11, hsh_dat[11]);
CP2_MT_I64(CVM_MT_HSH_DATW12, hsh_dat[12]);
CP2_MT_I64(CVM_MT_HSH_DATW13, hsh_dat[13]);
CP2_MT_I64(CVM_MT_HSH_DATW14, hsh_dat[14]);
CP2_MT_I64(CVM_MT_HSH_DATW15, hsh_dat[15]);
CP2_MT_I64(CVM_MT_HSH_IVW0, hsh_iv[0]);
CP2_MT_I64(CVM_MT_HSH_IVW1, hsh_iv[1]);
CP2_MT_I64(CVM_MT_HSH_IVW2, hsh_iv[2]);
CP2_MT_I64(CVM_MT_HSH_IVW3, hsh_iv[3]);
CP2_MT_I64(CVM_MT_HSH_IVW4, hsh_iv[4]);
CP2_MT_I64(CVM_MT_HSH_IVW5, hsh_iv[5]);
CP2_MT_I64(CVM_MT_HSH_IVW6, hsh_iv[6]);
CP2_MT_I64(CVM_MT_HSH_IVW7, hsh_iv[7]);
CP2_MT_HELPER(CVM_MT_HSH_STARTMD5, hsh_startmd5);
CP2_MT_HELPER(CVM_MT_HSH_STARTSHA256, hsh_startsha256);
CP2_MT_HELPER(CVM_MT_HSH_STARTSHA, hsh_startsha);
CP2_MT_HELPER(CVM_MT_HSH_STARTSHA512, hsh_startsha512);
CP2_MT_HELPER(CVM_MT_LLM_READ_ADDR0, llm_read_addr0);
CP2_MT_HELPER(CVM_MT_LLM_WRITE_ADDR0, llm_write_addr0);
CP2_MT_HELPER(CVM_MT_LLM_READ64_ADDR0, llm_read64_addr0);
CP2_MT_HELPER(CVM_MT_LLM_WRITE64_ADDR0, llm_write64_addr0);
CP2_MT_HELPER(CVM_MT_LLM_READ_ADDR1, llm_read_addr1);
CP2_MT_HELPER(CVM_MT_LLM_WRITE_ADDR1, llm_write_addr1);
CP2_MT_HELPER(CVM_MT_LLM_READ64_ADDR1, llm_read64_addr1);
CP2_MT_HELPER(CVM_MT_LLM_WRITE64_ADDR1, llm_write64_addr1);

static bool trans_BBIT(DisasContext *ctx, arg_BBIT *a)
{
    TCGv_i64 p;

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        LOG_DISAS("Branch in delay / forbidden slot at PC 0x%" VADDR_PRIx "\n",
                  ctx->base.pc_next);
        generate_exception_end(ctx, EXCP_RI);
        return true;
    }

    /* Load needed operands */
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);

    p = tcg_constant_i64(1ULL << a->p);
    if (a->set) {
        tcg_gen_and_i64(bcond, p, t0);
    } else {
        tcg_gen_andc_i64(bcond, p, t0);
    }

    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->btarget = ctx->base.pc_next + 4 + a->offset * 4;
    ctx->hflags |= MIPS_HFLAG_BDS32;
    return true;
}

static bool trans_BADDU(DisasContext *ctx, arg_BADDU *a)
{
    TCGv_i64 t0, t1;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_andi_i64(t0, t0, 0xff);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_DMUL(DisasContext *ctx, arg_DMUL *a)
{
    TCGv_i64 t0, t1;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_mul_i64(t0, t0, t1);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_EXTS(DisasContext *ctx, arg_EXTS *a)
{
    TCGv_i64 t0;

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    tcg_gen_sextract_i64(t0, t0, a->p, a->lenm1 + 1);
    gen_store_gpr(t0, a->rt);
    return true;
}

static bool trans_CINS(DisasContext *ctx, arg_CINS *a)
{
    TCGv_i64 t0;

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    tcg_gen_deposit_z_i64(t0, t0, a->p, a->lenm1 + 1);
    gen_store_gpr(t0, a->rt);
    return true;
}

static bool trans_POP(DisasContext *ctx, arg_POP *a)
{
    TCGv_i64 t0;

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    if (!a->dw) {
        tcg_gen_andi_i64(t0, t0, 0xffffffff);
    }
    tcg_gen_ctpop_i64(t0, t0);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool do_seq_sne(DisasContext *ctx, const arg_decode_ext_octeon1 *a,
                       TCGCond cond)
{
    TCGv_i64 t0, t1;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_setcond_i64(cond, t0, t1, t0);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_SEQ(DisasContext *ctx, arg_SEQ *a)
{
    return do_seq_sne(ctx, a, TCG_COND_EQ);
}

static bool trans_SNE(DisasContext *ctx, arg_SNE *a)
{
    return do_seq_sne(ctx, a, TCG_COND_NE);
}

static bool do_seqi_snei(DisasContext *ctx, const arg_cmpi *a, TCGCond cond)
{
    TCGv_i64 t0;

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);

    tcg_gen_setcondi_i64(cond, t0, t0, a->imm);
    gen_store_gpr(t0, a->rt);
    return true;
}

static bool trans_SEQI(DisasContext *ctx, arg_SEQI *a)
{
    return do_seqi_snei(ctx, a, TCG_COND_EQ);
}

static bool trans_SNEI(DisasContext *ctx, arg_SNEI *a)
{
    return do_seqi_snei(ctx, a, TCG_COND_NE);
}

static bool trans_lx(DisasContext *ctx, arg_lx *a, MemOp mop)
{
    gen_lx(ctx, a->rd, a->base, a->index, mop);

    return true;
}

TRANS(LBX,  trans_lx, MO_SB);
TRANS(LBUX, trans_lx, MO_UB);
TRANS(LHX,  trans_lx, MO_SW);
TRANS(LHUX, trans_lx, MO_UW);
TRANS(LWX,  trans_lx, MO_SL);
TRANS(LWUX, trans_lx, MO_UL);
TRANS(LDX,  trans_lx, MO_UQ);

static bool trans_saa(DisasContext *ctx, arg_saa *a, MemOp mop)
{
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 value = tcg_temp_new_i64();
    TCGv_i64 old = tcg_temp_new_i64();
    MemOp amo = mo_endian(ctx) | mop | MO_ALIGN;

    gen_base_offset_addr(ctx, addr, a->base, 0);
    gen_load_gpr(value, a->rt);
    tcg_gen_atomic_fetch_add_i64(old, addr, value, ctx->mem_idx, amo);
    return true;
}

TRANS(SAA,  trans_saa, MO_32);
TRANS(SAAD, trans_saa, MO_64);

typedef void AtomicThreeOpFn(TCGv_i64, TCGv_va, TCGv_i64, TCGArg, MemOp);

static bool do_atomic_la(DisasContext *ctx, arg_la *a,
                         AtomicThreeOpFn *atomic_fn, int64_t imm, MemOp mop)
{
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 old = tcg_temp_new_i64();
    MemOp amo = mo_endian(ctx) | mop | MO_ALIGN;

    gen_base_offset_addr(ctx, addr, a->base, 0);

    atomic_fn(old, addr, tcg_constant_i64(imm), ctx->mem_idx, amo);
    gen_store_gpr(old, a->rd);
    return true;
}

static bool do_atomic_laa(DisasContext *ctx, arg_laa *a,
                          AtomicThreeOpFn *atomic_fn, MemOp mop)
{
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 old = tcg_temp_new_i64();
    TCGv_i64 value = tcg_temp_new_i64();
    MemOp amo = mo_endian(ctx) | mop | MO_ALIGN;

    gen_base_offset_addr(ctx, addr, a->base, 0);
    gen_load_gpr(value, a->add);

    atomic_fn(old, addr, value, ctx->mem_idx, amo);
    gen_store_gpr(old, a->rd);
    return true;
}

TRANS(LAI,  do_atomic_la,  tcg_gen_atomic_fetch_add_i64,  1, MO_SL);
TRANS(LAID, do_atomic_la,  tcg_gen_atomic_fetch_add_i64,  1, MO_UQ);
TRANS(LAD,  do_atomic_la,  tcg_gen_atomic_fetch_add_i64, -1, MO_SL);
TRANS(LADD, do_atomic_la,  tcg_gen_atomic_fetch_add_i64, -1, MO_UQ);
TRANS(LAA,  do_atomic_laa, tcg_gen_atomic_fetch_add_i64,     MO_SL);
TRANS(LAAD, do_atomic_laa, tcg_gen_atomic_fetch_add_i64,     MO_UQ);
TRANS(LAS,  do_atomic_la,  tcg_gen_atomic_xchg_i64,      -1, MO_SL);
TRANS(LASD, do_atomic_la,  tcg_gen_atomic_xchg_i64,      -1, MO_UQ);
TRANS(LAC,  do_atomic_la,  tcg_gen_atomic_xchg_i64,       0, MO_SL);
TRANS(LACD, do_atomic_la,  tcg_gen_atomic_xchg_i64,       0, MO_UQ);
TRANS(LAW,  do_atomic_laa, tcg_gen_atomic_xchg_i64,          MO_SL);
TRANS(LAWD, do_atomic_laa, tcg_gen_atomic_xchg_i64,          MO_UQ);

static bool trans_ZCB(DisasContext *ctx, arg_ZCB *a)
{
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 line = tcg_temp_new_i64();
    TCGv_i128 zero128 = tcg_zero_i128();
    const MemOp mop = mo_endian(ctx) | MO_128 | MO_ATOM_NONE;

    gen_base_offset_addr(ctx, addr, a->base, 0);

    /*
     * QEMU models ZCB/ZCBT as zeroing the containing 128-byte cache line
     * in guest memory.
     */
    tcg_gen_andi_i64(line, addr, ~0x7fULL);

    for (int i = 0; i < 8; i++) {
        TCGv_i64 slot = tcg_temp_new_i64();

        tcg_gen_addi_i64(slot, line, i * 16);
        tcg_gen_qemu_st_i128(zero128, slot, ctx->mem_idx, mop);
    }

    return true;
}

static void octeon_zero_partial_product_state(void)
{
    for (int i = 0; i < OCTEON_MULTIPLIER_REGS; i++) {
        tcg_gen_movi_i64(oct_p[i], 0);
    }
}

static bool trans_mtm(DisasContext *ctx, arg_r2 *a, unsigned int index)
{
    /*
     * Octeon3 two-source MTM forms load lane index from rs and lane index + 3
     * from rt.  Legacy one-source forms encode rt as $zero.
     */
    gen_load_gpr(oct_mpl[index], a->rs);
    gen_load_gpr(oct_mpl[index + 3], a->rt);

    /*
     * Octeon3 clears MPL1 with a write to MPL0 so that VMULU sequences remain
     * backward compatible with Octeon2.
     */
    if (index == 0) {
        tcg_gen_movi_i64(oct_mpl[1], 0);
    }

    octeon_zero_partial_product_state();
    return true;
}

TRANS(MTM0, trans_mtm, 0);
TRANS(MTM1, trans_mtm, 1);
TRANS(MTM2, trans_mtm, 2);

static bool trans_mtp(DisasContext *ctx, arg_r2 *a, unsigned int index)
{
    /*
     * Octeon3 two-source MTP forms load lane index from rs and lane index + 3
     * from rt.  Legacy one-source forms encode rt as $zero.
     */
    gen_load_gpr(oct_p[index], a->rs);
    gen_load_gpr(oct_p[index + 3], a->rt);

    /*
     * Octeon3 clears P1 with a write to P0 so that VMULU sequences remain
     * backward compatible with Octeon2.
     */
    if (index == 0) {
        tcg_gen_movi_i64(oct_p[1], 0);
    }
    return true;
}

TRANS(MTP0, trans_mtp, 0);
TRANS(MTP1, trans_mtp, 1);
TRANS(MTP2, trans_mtp, 2);

static bool trans_VMULU(DisasContext *ctx, arg_VMULU *a)
{
    TCGv_i64 x[3], y[3], z[3];
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_constant_i64(0);

    z[0] = y[0] = tcg_temp_new_i64();
    z[1] = y[1] = tcg_temp_new_i64();
    z[2] = y[2] = tcg_temp_new_i64();
    x[0] = tcg_temp_new_i64();
    x[1] = tcg_temp_new_i64();
    x[2] = zero;

    /* Z = rs * (mpl1 : mpl0) + rt */
    gen_load_gpr(tmp, a->rs);
    gen_load_gpr(y[0], a->rt);
    tcg_gen_mulu2_i64(x[0], x[1], tmp, oct_mpl[0]);
    tcg_gen_mulu2_i64(y[1], y[2], tmp, oct_mpl[1]);
    tcg_gen_addN_i64(3, z, y, x);

    /* X == (0 : p1 : p0) */
    x[0] = oct_p[0];
    x[1] = oct_p[1];

    /* Y == (p1 : p0 : tmp) */
    y[0] = tmp;
    y[1] = oct_p[0];
    y[2] = oct_p[1];

    /* (p1 : p0 : rd) = Z + (0 : p1 : p0) */
    tcg_gen_addN_i64(3, y, z, x);
    gen_store_gpr(tmp, a->rd);
    return true;
}

static bool trans_VMM0(DisasContext *ctx, arg_VMM0 *a)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    gen_load_gpr(tmp, a->rs);
    tcg_gen_mul_i64(oct_mpl[0], oct_mpl[0], tmp);
    gen_load_gpr(tmp, a->rt);
    tcg_gen_add_i64(oct_mpl[0], oct_mpl[0], tmp);
    tcg_gen_add_i64(oct_mpl[0], oct_mpl[0], oct_p[0]);
    gen_store_gpr(oct_mpl[0], a->rd);

    tcg_gen_movi_i64(oct_mpl[1], 0);
    octeon_zero_partial_product_state();
    return true;
}

static bool trans_V3MULU(DisasContext *ctx, arg_V3MULU *a)
{
    TCGv_i64 x[7], y[7], z[7];
    TCGv_i64 tmp = tcg_temp_new_i64();

    for (int i = 0; i < 7; ++i) {
        z[i] = tcg_temp_new_i64();
        y[i] = tcg_temp_new_i64();
    }
    memcpy(&x[0], z, 6 * sizeof(TCGv_i64));
    x[6] = tcg_constant_i64(0);

    /*
     * Z = rs * mpl -- 64x384->448 bit multiply
     * Compute even partial products into X and odd partial products into Y.
     * Include RT into the odd partial products, which are 0 in bits [63:0].
     */
    gen_load_gpr(tmp, a->rs);
    gen_load_gpr(y[0], a->rt);
    for (int i = 0; i < 6; i += 2) {
        tcg_gen_mulu2_i64(x[i + 0], x[i + 1], tmp, oct_mpl[i]);
        tcg_gen_mulu2_i64(y[i + 1], y[i + 2], tmp, oct_mpl[i + 1]);
    }

    /* Sum even and odd to produce final product, plus rt. */
    tcg_gen_addN_i64(7, z, x, y);

    /* X == (0 : p5 : p4 : p3 : p2 : p1 : p0) -- x[6] is still 0 */
    memcpy(&x[0], oct_p, 6 * sizeof(TCGv_i64));

    /* Y == (p5 : p4 : p3 : p2 : p1 : p0 : tmp) */
    memcpy(&y[1], oct_p, 6 * sizeof(TCGv_i64));
    y[0] = tmp;

    /* (p* : rd) = (0 : p*) + (rs * mpl + rt) */
    tcg_gen_addN_i64(7, y, x, z);
    gen_store_gpr(tmp, a->rd);
    return true;
}

static bool trans_QMAC(DisasContext *ctx, arg_QMAC *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    gen_load_gpr(t0, a->rt);
    gen_load_gpr(t1, a->rs);

    /* t0 = rt<0> * rs<lane> * 2 */
    tcg_gen_ext16s_i64(t0, t0);
    tcg_gen_sextract_i64(t1, t1, a->lane * 16, 16);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_add_i64(t0, t0, t0);

    /* Saturate -0x8000 * -0x8000 * 2 = 0x80000000 -> 0x7fffffff */
    tcg_gen_smin_i64(t0, t0, tcg_constant_i64(INT32_MAX));

    /* HI:LO += t0 */
    tcg_gen_concat32_i64(t1, cpu_LO[0], cpu_HI[0]);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_sextract_i64(cpu_LO[0], t0, 0, 32);
    tcg_gen_sextract_i64(cpu_HI[0], t0, 32, 32);
    return true;
}

static bool trans_QMACS(DisasContext *ctx, arg_QMACS *a)
{
    TCGv_i64 min32 = tcg_constant_i64(INT32_MIN);
    TCGv_i64 max32 = tcg_constant_i64(INT32_MAX);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    gen_load_gpr(t0, a->rt);
    gen_load_gpr(t1, a->rs);

    /* t0 = rt<0> * rs<lane> * 2 */
    tcg_gen_ext16s_i64(t0, t0);
    tcg_gen_sextract_i64(t1, t1, a->lane * 16, 16);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_add_i64(t0, t0, t0);

    /*
     * Saturate -0x8000 * -0x8000 * 2 = 0x80000000 -> 0x7fffffff.
     * Accumulate overflow in HI[0].
     */
    tcg_gen_smin_i64(t1, t0, max32);
    tcg_gen_setcond_i64(TCG_COND_NE, t0, t0, t1);
    tcg_gen_or_i64(cpu_HI[0], cpu_HI[0], t0);

    /*
     * LO = sat32(LO + t0)
     * Accumulate overflow in HI[0].
     */
    tcg_gen_ext32s_i64(t0, cpu_LO[0]);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_smin_i64(cpu_LO[0], t0, max32);
    tcg_gen_smax_i64(cpu_LO[0], cpu_LO[0], min32);
    tcg_gen_setcond_i64(TCG_COND_NE, t0, t0, cpu_LO[0]);
    tcg_gen_or_i64(cpu_HI[0], cpu_HI[0], t0);
    return true;
}
