/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * s390 cpacf aes
 *
 * Authors:
 *   Harald Freudenberger <freude@linux.ibm.com>
 */

#include "qemu/osdep.h"
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "accel/tcg/cpu-ldst-common.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "crypto/aes.h"
#include "crypto/aes-helpers.h"
#include "target/s390x/tcg/cpacf-arch.h"
#include "target/s390x/tcg/cpacf.h"
#include "target/s390x/tcg/crypto_helper.h"

/*
 * read exactly one AES block from guest memory into a local buffer
 */
static inline void aes_read_block(CPUS390XState *env, const int mmu_idx,
                                  const uintptr_t ra, uint64_t guest_addr,
                                  uint8_t *buf)
{
    read_guest_wrap_u8(env, mmu_idx, ra, guest_addr, buf, AES_BLOCK_SIZE);
}

/*
 * write exactly one AES block from local buffer to guest memory
 */
static void aes_write_block(CPUS390XState *env, const int mmu_idx,
                            const uintptr_t ra, uint64_t guest_addr,
                            uint8_t *buf)
{
    write_guest_wrap_u8(env, mmu_idx, ra, guest_addr, buf, AES_BLOCK_SIZE);
}

/*
 * Helper function returning the bitsize of the address mode.
 * Within this file only 64 bit and 32 bit are supported and
 * thus this function returns either 64 or 32 or 0.
 */
static int get_address_bitsize(CPUS390XState *env)
{
    if (env->psw.mask & PSW_MASK_64) {
        return 64;
    } else if (env->psw.mask & PSW_MASK_32) {
        return 32;
    } else {
        /* unknown or unsupported here */
        return 0;
    }
}

int cpacf_aes_ecb(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size;
    uint8_t key[32];
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KM);
    switch (fc) {
    case CPACF_KM_AES_128:
        keysize = 16;
        break;
    case CPACF_KM_AES_192:
        keysize = 24;
        break;
    case CPACF_KM_AES_256:
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        aes_read_block(env, mmu_idx, ra, *src_ptr_reg + done, in);
        if (mod) {
            AES_decrypt(in, out, &exkey);
        } else {
            AES_encrypt(in, out, &exkey);
        }
        aes_write_block(env, mmu_idx, ra, *dst_ptr_reg + done, out);
        len -= AES_BLOCK_SIZE;
        done += AES_BLOCK_SIZE;
    }

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}

int cpacf_aes_cbc(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t len = *src_len_reg, done = 0;
    uint8_t key[32], iv[AES_BLOCK_SIZE];
    int i, keysize, addr_reg_size;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KMC);

    switch (fc) {
    case CPACF_KMC_AES_128:
        keysize = 16;
        break;
    case CPACF_KMC_AES_192:
        keysize = 24;
        break;
    case CPACF_KMC_AES_256:
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch iv from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, iv, AES_BLOCK_SIZE);

    /* fetch key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + AES_BLOCK_SIZE, key, keysize);

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        aes_read_block(env, mmu_idx, ra, *src_ptr_reg + done, in);
        if (mod) {
            /* decrypt in => out */
            AES_cbc_decrypt(in, out, iv, &exkey);
        } else {
            /* encrypt in => out */
            AES_cbc_encrypt(in, out, iv, &exkey);
        }
        aes_write_block(env, mmu_idx, ra, *dst_ptr_reg + done, out);
        len -= AES_BLOCK_SIZE;
        done += AES_BLOCK_SIZE;
    }

    /* update iv in param block */
    write_guest_wrap_u8(env, mmu_idx, ra, param_addr, iv, AES_BLOCK_SIZE);

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}

int cpacf_aes_ctr(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint64_t *ctr_ptr_reg, uint32_t type,
                  uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t len = *src_len_reg, done = 0;
    uint8_t ctr[AES_BLOCK_SIZE], key[32];
    int i, keysize, addr_reg_size;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KMCTR);

    switch (fc) {
    case CPACF_KMCTR_AES_128:
        keysize = 16;
        break;
    case CPACF_KMCTR_AES_192:
        keysize = 24;
        break;
    case CPACF_KMCTR_AES_256:
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* read in nonce/ctr => ctr */
        aes_read_block(env, mmu_idx, ra, *ctr_ptr_reg + done, ctr);
        /* read in one block of input data => in */
        aes_read_block(env, mmu_idx, ra, *src_ptr_reg + done, in);
        /* encrypt ctr and xor with in => out */
        AES_ctr_encrypt(in, out, ctr, &exkey);
        /* write out the processed block */
        aes_write_block(env, mmu_idx, ra, *dst_ptr_reg + done, out);
        len -= AES_BLOCK_SIZE;
        done += AES_BLOCK_SIZE;
    }

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *ctr_ptr_reg = deposit64(*ctr_ptr_reg, 0, addr_reg_size,
                             *ctr_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}

int cpacf_aes_pcc(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint8_t fc)
{
    uint8_t key[32], tweak[AES_BLOCK_SIZE], buf[AES_BLOCK_SIZE];
    int keysize, i;
    AES_KEY exkey;

    switch (fc) {
    case CPACF_PCC_XTS_AES_128:
        keysize = 16;
        break;
    case CPACF_PCC_XTS_AES_256:
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    /* fetch block sequence nr from param block into buf */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize + AES_BLOCK_SIZE,
                       buf, AES_BLOCK_SIZE);

    /* is the block sequence nr 0 ? */
    for (i = 0; i < AES_BLOCK_SIZE && !buf[i]; i++) {
        ;
    }
    if (i < AES_BLOCK_SIZE) {
        /* no, sorry handling of non zero block sequence is not implemented */
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);

    /* fetch tweak from param block into tweak */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize, tweak, AES_BLOCK_SIZE);

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* encrypt tweak */
    AES_encrypt(tweak, buf, &exkey);

    /* store encrypted tweak into xts parameter field of the param block */
    write_guest_wrap_u8(env, mmu_idx, ra,
                        param_addr + keysize + 3 * AES_BLOCK_SIZE,
                        buf, AES_BLOCK_SIZE);

    return 0;
}
