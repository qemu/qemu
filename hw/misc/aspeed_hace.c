/*
 * ASPEED Hash and Crypto Engine
 *
 * Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates
 * Copyright (C) 2021 IBM Corp.
 *
 * Joel Stanley <joel@jms.id.au>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "hw/misc/aspeed_hace.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "crypto/hash.h"
#include "crypto/cipher.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "trace.h"

/* Crypto engine registers */
#define R_CRYPT_SRC         (0x00 / 4)
#define R_CRYPT_DEST        (0x04 / 4)
#define R_CRYPT_CONTEXT     (0x08 / 4)
#define R_CRYPT_DATA_LEN    (0x0c / 4)
/* HACE0C[27:0] holds the crypto data length */
#define  CRYPT_DATA_LEN_MASK    0x0FFFFFFF
#define R_CRYPT_CMD         (0x10 / 4)
/* AES-GCM associated data length (HACE14) and tag write buffer (HACE18) */
#define R_CRYPT_GCM_ADD_LEN (0x14 / 4)
#define R_CRYPT_GCM_TAG     (0x18 / 4)
/* Crypto engine command register (HACE10) bits */
#define  CRYPT_CMD_ENCRYPT          BIT(7)
#define  CRYPT_CMD_ISR_EN           BIT(12)
#define  CRYPT_CMD_DES_SELECT       BIT(16)
#define  CRYPT_CMD_TRIPLE_DES       BIT(17)
#define  CRYPT_CMD_SRC_SG_CTRL      BIT(18)
/* Operation mode HACE10[6:4] */
#define  CRYPT_CMD_OP_MODE_MASK     (0x7 << 4)
#define  CRYPT_CMD_ECB              (0x0 << 4)
#define  CRYPT_CMD_CBC              (0x1 << 4)
#define  CRYPT_CMD_CTR              (0x4 << 4)
#define  CRYPT_CMD_GCM              (0x5 << 4)
/* AES key length HACE10[3:2] */
#define  CRYPT_CMD_AES_KEY_LEN_MASK (0x3 << 2)
#define  CRYPT_CMD_AES256           (0x2 << 2)
#define  CRYPT_CMD_AES192           (0x1 << 2)
#define  CRYPT_CMD_AES128           (0x0 << 2)

/*
 * Crypto context buffer layout (HACE08). The IV is at the start of the buffer
 * (DES places its 8 byte IV at offset 8) and the cipher key at offset 0x10.
 */
#define CRYPT_CTX_IV_OFFSET         0x00
#define CRYPT_CTX_DES_IV_OFFSET     0x08
#define CRYPT_CTX_KEY_OFFSET        0x10
#define CRYPT_CTX_SIZE              0x30

/* AES-GCM uses a 96-bit IV and a 128-bit authentication tag */
#define CRYPT_GCM_IV_LEN            12
#define CRYPT_GCM_TAG_LEN           16

/* AST2700 64-bit DMA high address registers for the crypto command */
#define R_CRYPT_SRC_HI      (0x80 / 4)
#define R_CRYPT_DEST_HI     (0x84 / 4)
#define R_CRYPT_CONTEXT_HI  (0x88 / 4)
#define R_CRYPT_GCM_TAG_HI  (0x8c / 4)

#define R_STATUS        (0x1c / 4)
#define HASH_IRQ        BIT(9)
#define CRYPT_IRQ       BIT(12)
#define TAG_IRQ         BIT(15)

#define R_HASH_SRC      (0x20 / 4)
#define R_HASH_DIGEST   (0x24 / 4)
#define R_HASH_KEY_BUFF (0x28 / 4)
#define R_HASH_SRC_LEN  (0x2c / 4)
#define R_HASH_SRC_HI       (0x90 / 4)
#define R_HASH_DIGEST_HI    (0x94 / 4)
#define R_HASH_KEY_BUFF_HI  (0x98 / 4)

#define R_HASH_CMD      (0x30 / 4)
/* Hash algorithm selection */
#define  HASH_ALGO_MASK                 (BIT(4) | BIT(5) | BIT(6))
#define  HASH_ALGO_MD5                  0
#define  HASH_ALGO_SHA1                 BIT(5)
#define  HASH_ALGO_SHA224               BIT(6)
#define  HASH_ALGO_SHA256               (BIT(4) | BIT(6))
#define  HASH_ALGO_SHA512_SERIES        (BIT(5) | BIT(6))
/* SHA512 algorithm selection */
#define  SHA512_HASH_ALGO_MASK          (BIT(10) | BIT(11) | BIT(12))
#define  HASH_ALGO_SHA512_SHA512        0
#define  HASH_ALGO_SHA512_SHA384        BIT(10)
#define  HASH_ALGO_SHA512_SHA256        BIT(11)
#define  HASH_ALGO_SHA512_SHA224        (BIT(10) | BIT(11))
/* HMAC modes */
#define  HASH_HMAC_MASK                 (BIT(7) | BIT(8))
#define  HASH_DIGEST                    0
#define  HASH_DIGEST_HMAC               BIT(7)
#define  HASH_DIGEST_ACCUM              BIT(8)
#define  HASH_HMAC_KEY                  (BIT(7) | BIT(8))
/* Cascaded operation modes */
#define  HASH_ONLY                      0
#define  HASH_ONLY2                     BIT(0)
#define  HASH_CRYPT_THEN_HASH           BIT(1)
#define  HASH_HASH_THEN_CRYPT           (BIT(0) | BIT(1))
/* Other cmd bits */
#define  HASH_IRQ_EN                    BIT(9)
#define  HASH_SG_EN                     BIT(18)
/* Scatter-gather data list */
#define SG_LIST_LEN_SIZE                4
#define SG_LIST_LEN_MASK                0x0FFFFFFF
#define SG_LIST_LEN_LAST                BIT(31)
#define SG_LIST_ADDR_SIZE               4
#define SG_LIST_ADDR_MASK               0x7FFFFFFF
#define SG_LIST_ENTRY_SIZE              (SG_LIST_LEN_SIZE + SG_LIST_ADDR_SIZE)

static const struct {
    uint32_t mask;
    QCryptoHashAlgo algo;
} hash_algo_map[] = {
    { HASH_ALGO_MD5, QCRYPTO_HASH_ALGO_MD5 },
    { HASH_ALGO_SHA1, QCRYPTO_HASH_ALGO_SHA1 },
    { HASH_ALGO_SHA224, QCRYPTO_HASH_ALGO_SHA224 },
    { HASH_ALGO_SHA256, QCRYPTO_HASH_ALGO_SHA256 },
    { HASH_ALGO_SHA512_SERIES | HASH_ALGO_SHA512_SHA512,
      QCRYPTO_HASH_ALGO_SHA512 },
    { HASH_ALGO_SHA512_SERIES | HASH_ALGO_SHA512_SHA384,
      QCRYPTO_HASH_ALGO_SHA384 },
    { HASH_ALGO_SHA512_SERIES | HASH_ALGO_SHA512_SHA256,
      QCRYPTO_HASH_ALGO_SHA256 },
};

static void hace_hexdump(const char *desc, const char *buf, size_t size)
{
    g_autoptr(GString) str = g_string_sized_new(64);
    size_t len;
    size_t i;

    for (i = 0; i < size; i += len) {
        len = MIN(16, size - i);
        g_string_truncate(str, 0);
        qemu_hexdump_line(str, buf + i, len, 1, 4);
        trace_aspeed_hace_hexdump(desc, i, str->str);
    }
}

static void hace_iov_hexdump(const char *desc, const struct iovec *iov,
                             const unsigned int iov_cnt)
{
    size_t size = 0;
    char *buf;
    int i;

    for (i = 0; i < iov_cnt; i++) {
        size += iov[i].iov_len;
    }

    buf = g_malloc(size);

    if (!buf) {
        return;
    }

    iov_to_buf(iov, iov_cnt, 0, buf, size);
    hace_hexdump(desc, buf, size);
    g_free(buf);
}

static int hash_algo_lookup(uint32_t reg)
{
    int i;

    reg &= HASH_ALGO_MASK | SHA512_HASH_ALGO_MASK;

    for (i = 0; i < ARRAY_SIZE(hash_algo_map); i++) {
        if (reg == hash_algo_map[i].mask) {
            return hash_algo_map[i].algo;
        }
    }

    return -1;
}

/**
 * Check whether the request contains padding message.
 *
 * @param s             aspeed hace state object
 * @param iov           iov of current request
 * @param req_len       length of the current request
 * @param total_msg_len length of all acc_mode requests(excluding padding msg)
 * @param pad_offset    start offset of padding message
 */
static bool has_padding(AspeedHACEState *s, struct iovec *iov,
                        hwaddr req_len, uint32_t *total_msg_len,
                        uint32_t *pad_offset)
{
    /* Need at least 8 bytes to read the total message length field */
    if (req_len < 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid request length=0x%" HWADDR_PRIx "\n",
                      __func__, req_len);
        return false;
    }

    *total_msg_len = (uint32_t)(ldq_be_p(iov->iov_base + req_len - 8) / 8);
    /*
     * SG_LIST_LEN_LAST asserted in the request length doesn't mean it is the
     * last request. The last request should contain padding message.
     * We check whether message contains padding by
     *   1. Get total message length. If the current message contains
     *      padding, the last 8 bytes are total message length.
     *   2. Check whether the total message length is valid.
     *      If it is valid, the value should less than or equal to
     *      total_req_len.
     *   3. Current request len - padding_size to get padding offset.
     *      The padding message's first byte should be 0x80
     */
    if (*total_msg_len <= s->total_req_len) {
        uint32_t padding_size = s->total_req_len - *total_msg_len;
        uint8_t *padding = iov->iov_base;

        if (padding_size > req_len) {
            return false;
        }

        *pad_offset = req_len - padding_size;
        if (padding[*pad_offset] == 0x80) {
            return true;
        }
    }

    return false;
}

static uint64_t hash_get_source_addr(AspeedHACEState *s)
{
    AspeedHACEClass *ahc = ASPEED_HACE_GET_CLASS(s);
    uint64_t src_addr = 0;

    src_addr = deposit64(src_addr, 0, 32, s->regs[R_HASH_SRC]);
    if (ahc->has_dma64) {
        src_addr = deposit64(src_addr, 32, 32, s->regs[R_HASH_SRC_HI]);
    }

    return src_addr;
}

static bool hash_accumulate_len(AspeedHACEState *s, hwaddr plen)
{
    if (plen > UINT32_MAX - s->total_req_len) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: total_req_len overflow, current=0x%x, adding=0x%"
                      HWADDR_PRIx "\n", __func__, s->total_req_len, plen);
        return false;
    }

    s->total_req_len += plen;
    return true;
}

static void hash_iov_unmap(AspeedHACEState *s, struct iovec *iov,
                           hwaddr *mapped_lens, int iov_count)
{
    for (; iov_count > 0; iov_count--) {
        address_space_unmap(&s->dram_as, iov[iov_count - 1].iov_base,
                            mapped_lens[iov_count - 1], false,
                            mapped_lens[iov_count - 1]);
    }
}

static int hash_prepare_direct_iov(AspeedHACEState *s, struct iovec *iov,
                                   bool acc_mode, bool *acc_final_request,
                                   hwaddr *mapped_lens)
{
    uint32_t total_msg_len;
    uint32_t pad_offset;
    uint64_t src;
    void *haddr;
    hwaddr plen;
    int iov_idx;

    plen = s->regs[R_HASH_SRC_LEN];
    src = hash_get_source_addr(s);
    trace_aspeed_hace_hash_addr("src", src);
    haddr = address_space_map(&s->dram_as, src, &plen, false,
                              MEMTXATTRS_UNSPECIFIED);
    if (haddr == NULL) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unable to map address, addr=0x%" HWADDR_PRIx
                      " ,plen=0x%" HWADDR_PRIx "\n",
                      __func__, src, plen);
        return -1;
    }

    iov[0].iov_base = haddr;
    iov_idx = 1;
    mapped_lens[0] = plen;

    if (acc_mode) {
        if (!hash_accumulate_len(s, plen)) {
            hash_iov_unmap(s, iov, mapped_lens, 1);
            return -1;
        }

        if (has_padding(s, &iov[0], plen, &total_msg_len,
                        &pad_offset)) {
            /* Padding being present indicates the final request */
            *acc_final_request = true;
            iov[0].iov_len = pad_offset;
        } else {
            iov[0].iov_len = plen;
        }
    } else {
        iov[0].iov_len = plen;
    }

    return iov_idx;
}

static int hash_prepare_sg_iov(AspeedHACEState *s, struct iovec *iov,
                               bool acc_mode, bool *acc_final_request,
                               hwaddr *mapped_lens)
{
    uint32_t total_msg_len;
    uint32_t pad_offset;
    uint32_t len = 0;
    uint32_t sg_addr;
    uint64_t src;
    int iov_idx;
    hwaddr plen;
    void *haddr;
    int iov_mapped = 0;

    src = hash_get_source_addr(s);
    for (iov_idx = 0; !(len & SG_LIST_LEN_LAST); iov_idx++) {
        if (iov_idx == ASPEED_HACE_MAX_SG) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Failed to set end of sg list marker\n",
                          __func__);
            goto fail;
        }

        len = address_space_ldl_le(&s->dram_as, src,
                                   MEMTXATTRS_UNSPECIFIED, NULL);
        sg_addr = address_space_ldl_le(&s->dram_as, src + SG_LIST_LEN_SIZE,
                                       MEMTXATTRS_UNSPECIFIED, NULL);
        sg_addr &= SG_LIST_ADDR_MASK;
        trace_aspeed_hace_hash_sg(iov_idx, src, sg_addr, len);
        /*
         * To maintain compatibility with older SoCs such as the AST2600,
         * the AST2700 HW automatically set bit 34 of the 64-bit sg_addr.
         * As a result, the firmware only needs to provide a 32-bit sg_addr
         * containing bits [31:0]. This is sufficient for the AST2700, as
         * it uses a DRAM offset rather than a DRAM address.
         */
        plen = len & SG_LIST_LEN_MASK;
        haddr = address_space_map(&s->dram_as, sg_addr, &plen, false,
                                  MEMTXATTRS_UNSPECIFIED);

        if (haddr == NULL) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Unable to map address, sg_addr=0x%x, "
                          "plen=0x%" HWADDR_PRIx "\n",
                          __func__, sg_addr, plen);
            goto fail;
        }

        src += SG_LIST_ENTRY_SIZE;

        iov[iov_idx].iov_base = haddr;
        iov_mapped = iov_idx + 1;
        mapped_lens[iov_idx] = plen;
        if (acc_mode) {
            if (!hash_accumulate_len(s, plen)) {
                goto fail;
            }

            if (has_padding(s, &iov[iov_idx], plen, &total_msg_len,
                            &pad_offset)) {
                /* Padding being present indicates the final request */
                *acc_final_request = true;
                iov[iov_idx].iov_len = pad_offset;
            } else {
                iov[iov_idx].iov_len = plen;
            }
        } else {
            iov[iov_idx].iov_len = plen;
        }
    }

    return iov_idx;

fail:
    hash_iov_unmap(s, iov, mapped_lens, iov_mapped);
    return -1;
}

static uint64_t hash_get_digest_addr(AspeedHACEState *s)
{
    AspeedHACEClass *ahc = ASPEED_HACE_GET_CLASS(s);
    uint64_t digest_addr = 0;

    digest_addr = deposit64(digest_addr, 0, 32, s->regs[R_HASH_DIGEST]);
    if (ahc->has_dma64) {
        digest_addr = deposit64(digest_addr, 32, 32, s->regs[R_HASH_DIGEST_HI]);
    }

    return digest_addr;
}

static void hash_write_digest_and_unmap_iov(AspeedHACEState *s,
                                            struct iovec *iov,
                                            int iov_idx,
                                            hwaddr *mapped_lens,
                                            uint8_t *digest_buf,
                                            size_t digest_len)
{
    uint64_t digest_addr = 0;

    digest_addr = hash_get_digest_addr(s);
    trace_aspeed_hace_hash_addr("digest", digest_addr);
    if (address_space_write(&s->dram_as, digest_addr,
                            MEMTXATTRS_UNSPECIFIED,
                            digest_buf, digest_len)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to write digest to 0x%" HWADDR_PRIx "\n",
                      __func__, digest_addr);
    }

    if (trace_event_get_state_backends(TRACE_ASPEED_HACE_HEXDUMP)) {
        hace_hexdump("digest", (char *)digest_buf, digest_len);
    }

    hash_iov_unmap(s, iov, mapped_lens, iov_idx);
}

static void hash_execute_non_acc_mode(AspeedHACEState *s, int algo,
                                      struct iovec *iov, int iov_idx,
                                      hwaddr *mapped_lens)
{
    g_autofree uint8_t *digest_buf = NULL;
    Error *local_err = NULL;
    size_t digest_len = 0;

    if (qcrypto_hash_bytesv(algo, iov, iov_idx, &digest_buf,
                            &digest_len, &local_err) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: qcrypto hash bytesv failed : %s",
                      __func__, error_get_pretty(local_err));
        error_free(local_err);
        hash_iov_unmap(s, iov, mapped_lens, iov_idx);
        return;
    }

    hash_write_digest_and_unmap_iov(s, iov, iov_idx, mapped_lens,
                                    digest_buf, digest_len);
}

static void hash_execute_acc_mode(AspeedHACEState *s, int algo,
                                  struct iovec *iov, int iov_idx,
                                  bool final_request, hwaddr *mapped_lens)
{
    g_autofree uint8_t *digest_buf = NULL;
    Error *local_err = NULL;
    size_t digest_len = 0;

    trace_aspeed_hace_hash_execute_acc_mode(final_request);

    if (s->hash_ctx == NULL) {
        s->hash_ctx = qcrypto_hash_new(algo, &local_err);
        if (s->hash_ctx == NULL) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: qcrypto hash new failed : %s",
                          __func__, error_get_pretty(local_err));
            error_free(local_err);
            hash_iov_unmap(s, iov, mapped_lens, iov_idx);
            return;
        }
    }

    if (qcrypto_hash_updatev(s->hash_ctx, iov, iov_idx, &local_err) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: qcrypto hash updatev failed : %s",
                      __func__, error_get_pretty(local_err));
        error_free(local_err);
        hash_iov_unmap(s, iov, mapped_lens, iov_idx);
        return;
    }

    if (final_request) {
        if (qcrypto_hash_finalize_bytes(s->hash_ctx, &digest_buf,
                                        &digest_len, &local_err)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: qcrypto hash finalize bytes failed : %s",
                          __func__, error_get_pretty(local_err));
            error_free(local_err);
            local_err = NULL;
        }

        qcrypto_hash_free(s->hash_ctx);

        s->hash_ctx = NULL;
        s->total_req_len = 0;
    }

    hash_write_digest_and_unmap_iov(s, iov, iov_idx, mapped_lens,
                                    digest_buf, digest_len);
}

static void do_hash_operation(AspeedHACEState *s, int algo, bool sg_mode,
                              bool acc_mode)
{
    QEMU_UNINITIALIZED struct iovec iov[ASPEED_HACE_MAX_SG];
    hwaddr mapped_lens[ASPEED_HACE_MAX_SG] = { 0 };
    bool acc_final_request = false;
    int iov_idx = -1;

    /* Prepares the iov for hashing operations based on the selected mode */
    if (sg_mode) {
        iov_idx = hash_prepare_sg_iov(s, iov, acc_mode, &acc_final_request,
                                      mapped_lens);
    } else {
        iov_idx = hash_prepare_direct_iov(s, iov, acc_mode,
                                          &acc_final_request, mapped_lens);
    }

    if (iov_idx <= 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to prepare iov\n", __func__);
         return;
    }

    if (trace_event_get_state_backends(TRACE_ASPEED_HACE_HEXDUMP)) {
        hace_iov_hexdump("plaintext", iov, iov_idx);
    }

    /* Executes the hash operation */
    if (acc_mode) {
        hash_execute_acc_mode(s, algo, iov, iov_idx, acc_final_request,
                              mapped_lens);
    } else {
        hash_execute_non_acc_mode(s, algo, iov, iov_idx, mapped_lens);
    }
}

static bool crypt_aes_alg(uint32_t cmd, QCryptoCipherAlgo *alg, size_t *keylen)
{
    switch (cmd & CRYPT_CMD_AES_KEY_LEN_MASK) {
    case CRYPT_CMD_AES128:
        *alg = QCRYPTO_CIPHER_ALGO_AES_128;
        *keylen = 16;
        break;
    case CRYPT_CMD_AES192:
        *alg = QCRYPTO_CIPHER_ALGO_AES_192;
        *keylen = 24;
        break;
    case CRYPT_CMD_AES256:
        *alg = QCRYPTO_CIPHER_ALGO_AES_256;
        *keylen = 32;
        break;
    default:
        return false;
    }

    return true;
}

/*
 * Decode the crypto command register into a libqcrypto algorithm/mode pair
 * and the block/IV geometry. Returns false for unsupported selections.
 */
static bool crypt_decode_cmd(uint32_t cmd, QCryptoCipherAlgo *alg,
                             QCryptoCipherMode *mode, size_t *keylen,
                             size_t *blocklen, size_t *iv_offset)
{
    if (cmd & CRYPT_CMD_DES_SELECT) {
        *blocklen = 8;
        *iv_offset = CRYPT_CTX_DES_IV_OFFSET;
        if (cmd & CRYPT_CMD_TRIPLE_DES) {
            *alg = QCRYPTO_CIPHER_ALGO_3DES;
            *keylen = 24;
        } else {
            *alg = QCRYPTO_CIPHER_ALGO_DES;
            *keylen = 8;
        }
    } else {
        *blocklen = 16;
        *iv_offset = CRYPT_CTX_IV_OFFSET;
        if (!crypt_aes_alg(cmd, alg, keylen)) {
            return false;
        }
    }

    switch (cmd & CRYPT_CMD_OP_MODE_MASK) {
    case CRYPT_CMD_ECB:
        *mode = QCRYPTO_CIPHER_MODE_ECB;
        break;
    case CRYPT_CMD_CBC:
        *mode = QCRYPTO_CIPHER_MODE_CBC;
        break;
    case CRYPT_CMD_CTR:
        *mode = QCRYPTO_CIPHER_MODE_CTR;
        break;
    case CRYPT_CMD_GCM:
        *mode = QCRYPTO_CIPHER_MODE_GCM;
        break;
    default:
        return false;
    }

    return true;
}

/*
 * Direct access mode: the source/destination register (HACE00/HACE04) points
 * at a single contiguous buffer in DRAM. Copy @len bytes between it and the
 * bounce buffer @buf; when @to_dram is true @buf is written out, otherwise it
 * is read in. Returns true on success.
 */
static bool crypt_prepare_direct(AspeedHACEState *s, uint64_t addr,
                                 uint8_t *buf, uint32_t len, bool to_dram)
{
    return !address_space_rw(&s->dram_as, addr, MEMTXATTRS_UNSPECIFIED,
                             buf, len, to_dram);
}

/*
 * Scatter-gather mode: the source/destination register points at an SG list
 * whose entries are a length word (SG_LIST_LEN_LAST flags the final entry)
 * followed by a DRAM address, matching the hash engine layout. Gather @len
 * bytes into @buf, or scatter @buf back out when @to_dram is true.
 * Returns true on success.
 */
static bool crypt_prepare_sg(AspeedHACEState *s, uint64_t addr,
                             uint8_t *buf, uint32_t len, bool to_dram)
{
    uint32_t copied = 0;
    uint32_t sg_addr;
    uint32_t sg_len;
    uint32_t entry;
    int i;

    for (i = 0; i < ASPEED_HACE_MAX_SG && copied < len; i++) {
        entry = address_space_ldl_le(&s->dram_as, addr,
                                     MEMTXATTRS_UNSPECIFIED, NULL);
        sg_addr = address_space_ldl_le(&s->dram_as, addr + SG_LIST_LEN_SIZE,
                                       MEMTXATTRS_UNSPECIFIED, NULL);
        sg_len = entry & SG_LIST_LEN_MASK;

        sg_addr &= SG_LIST_ADDR_MASK;
        addr += SG_LIST_ENTRY_SIZE;

        if (sg_len > len - copied) {
            sg_len = len - copied;
        }
        if (address_space_rw(&s->dram_as, sg_addr, MEMTXATTRS_UNSPECIFIED,
                             buf + copied, sg_len, to_dram)) {
            return false;
        }
        copied += sg_len;

        if (entry & SG_LIST_LEN_LAST) {
            break;
        }
    }

    return copied == len;
}

/*
 * Add @add to the big-endian counter block @ctr (@len bytes) in place, so the
 * CTR mode counter can be advanced by the number of blocks just consumed.
 */
static void crypt_be_add(uint8_t *ctr, size_t len, uint64_t add)
{
    size_t i = len;

    while (i > 0 && add) {
        i--;
        add += ctr[i];
        ctr[i] = add & 0xff;
        add >>= 8;
    }
}

static uint64_t crypt_get_addr(AspeedHACEState *s, int reg, int reg_hi)
{
    AspeedHACEClass *ahc = ASPEED_HACE_GET_CLASS(s);
    uint64_t addr;

    addr = deposit64(0, 0, 32, s->regs[reg]);
    if (ahc->has_dma64) {
        addr = deposit64(addr, 32, 32, s->regs[reg_hi]);
    }

    return addr;
}

/*
 * Perform an AES/DES/3DES ECB/CBC/CTR or AES-GCM operation. The source and
 * destination are either single contiguous buffers (direct access mode) or
 * scatter-gather lists (HACE10[18]/[19]), addressed by HACE00/HACE04; the
 * IV/key come from the context buffer (HACE08). For CBC and CTR the resulting
 * chaining state is written back to the context buffer so the driver can
 * continue; for GCM the authentication tag is written to the tag buffer.
 */
static void do_crypt_operation(AspeedHACEState *s, uint32_t cmd)
{
    bool sg_mode = cmd & CRYPT_CMD_SRC_SG_CTRL;
    uint32_t len = s->regs[R_CRYPT_DATA_LEN];
    bool encrypt = cmd & CRYPT_CMD_ENCRYPT;
    g_autoptr(QCryptoCipher) cipher = NULL;
    g_autofree uint8_t *src_buf = NULL;
    g_autofree uint8_t *dst_buf = NULL;
    uint8_t tag[CRYPT_GCM_TAG_LEN];
    uint8_t ctx[CRYPT_CTX_SIZE];
    Error *local_err = NULL;
    QCryptoCipherMode mode;
    QCryptoCipherAlgo alg;
    const uint8_t *next_iv;
    uint64_t ctx_addr;
    uint64_t src_addr;
    uint64_t dst_addr;
    uint64_t tag_addr;
    uint32_t aad_len;
    size_t iv_offset;
    size_t blocklen;
    size_t buf_len;
    size_t keylen;
    size_t ivlen;
    bool status;

    if (len == 0) {
        return;
    }

    if (!crypt_decode_cmd(cmd, &alg, &mode, &keylen, &blocklen, &iv_offset)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: Unsupported crypt command 0x%x\n", __func__, cmd);
        return;
    }

    if (!qcrypto_cipher_supports(alg, mode)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: cipher mode not supported by the crypto backend\n",
                      __func__);
        return;
    }

    /* GCM uses a 96-bit IV; the block modes use a full-block IV. */
    ivlen = (mode == QCRYPTO_CIPHER_MODE_GCM) ? CRYPT_GCM_IV_LEN : blocklen;

    /*
     * The hardware GCM path is only exercised without associated data (the
     * driver falls back to software when there is any), so AAD is not modelled.
     */
    aad_len = s->regs[R_CRYPT_GCM_ADD_LEN];
    if (mode == QCRYPTO_CIPHER_MODE_GCM && aad_len != 0) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: GCM associated data is not implemented\n", __func__);
        return;
    }

    /* Fetch the IV and key from the context buffer in DRAM. */
    ctx_addr = crypt_get_addr(s, R_CRYPT_CONTEXT, R_CRYPT_CONTEXT_HI);
    if (address_space_read(&s->dram_as, ctx_addr, MEMTXATTRS_UNSPECIFIED,
                           ctx, sizeof(ctx))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to read context, addr=0x%" HWADDR_PRIx "\n",
                      __func__, ctx_addr);
        return;
    }

    if (trace_event_get_state_backends(TRACE_ASPEED_HACE_HEXDUMP)) {
        hace_hexdump("context", (char *)ctx, sizeof(ctx));
    }

    cipher = qcrypto_cipher_new(alg, mode, ctx + CRYPT_CTX_KEY_OFFSET, keylen,
                                &local_err);
    if (cipher == NULL) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: qcrypto cipher new failed: %s\n",
                      __func__, error_get_pretty(local_err));
        error_free(local_err);
        return;
    }

    if (mode != QCRYPTO_CIPHER_MODE_ECB &&
        qcrypto_cipher_setiv(cipher, ctx + iv_offset, ivlen,
                             &local_err) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: qcrypto cipher setiv failed: %s\n",
                      __func__, error_get_pretty(local_err));
        error_free(local_err);
        return;
    }

    /*
     * Round the working buffers up to a whole block. Block modes are already
     * block-aligned; the stream-like CTR mode may leave a partial final block
     * that the engine still processes a full block at a time. GCM handles a
     * partial final block itself, so it operates on the exact length.
     */
    buf_len = (mode == QCRYPTO_CIPHER_MODE_GCM) ?
              len : QEMU_ALIGN_UP(len, blocklen);
    src_buf = g_malloc0(buf_len);
    dst_buf = g_malloc0(buf_len);

    /* Gather the source into the bounce buffer, per the selected mode. */
    src_addr = crypt_get_addr(s, R_CRYPT_SRC, R_CRYPT_SRC_HI);
    if (sg_mode) {
        status = crypt_prepare_sg(s, src_addr, src_buf, len, false);
    } else {
        status = crypt_prepare_direct(s, src_addr, src_buf, len, false);
    }
    if (!status) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to read src, addr=0x%" HWADDR_PRIx "\n",
                      __func__, src_addr);
        return;
    }

    if (trace_event_get_state_backends(TRACE_ASPEED_HACE_HEXDUMP)) {
        hace_hexdump("src", (char *)src_buf, len);
    }

    if (encrypt) {
        if (qcrypto_cipher_encrypt(cipher, src_buf, dst_buf, buf_len,
                                   &local_err) < 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: encrypt failed: %s\n",
                          __func__, error_get_pretty(local_err));
            error_free(local_err);
            return;
        }
    } else {
        if (qcrypto_cipher_decrypt(cipher, src_buf, dst_buf, buf_len,
                                   &local_err) < 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: decrypt failed: %s\n",
                          __func__, error_get_pretty(local_err));
            error_free(local_err);
            return;
        }
    }

    /* Scatter the result back out, per the selected mode. */
    dst_addr = crypt_get_addr(s, R_CRYPT_DEST, R_CRYPT_DEST_HI);
    if (sg_mode) {
        status = crypt_prepare_sg(s, dst_addr, dst_buf, len, true);
    } else {
        status = crypt_prepare_direct(s, dst_addr, dst_buf, len, true);
    }
    if (!status) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to write dst, addr=0x%" HWADDR_PRIx "\n",
                      __func__, dst_addr);
        return;
    }

    if (trace_event_get_state_backends(TRACE_ASPEED_HACE_HEXDUMP)) {
        hace_hexdump("dst", (char *)dst_buf, len);
    }

    if (mode == QCRYPTO_CIPHER_MODE_CBC) {
        /*
         * CBC chains on the last ciphertext block: the final block of the
         * output when encrypting, or of the input when decrypting. Write it
         * back as the IV for the next request.
         */
        next_iv = (encrypt ? dst_buf : src_buf) + buf_len - blocklen;
        if (address_space_write(&s->dram_as, ctx_addr + iv_offset,
                                MEMTXATTRS_UNSPECIFIED, next_iv, blocklen)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Failed to write IV, addr=0x%" HWADDR_PRIx "\n",
                          __func__, ctx_addr + iv_offset);
        }
    } else if (mode == QCRYPTO_CIPHER_MODE_CTR) {
        /*
         * CTR chains on the counter, which advances by one per block. Add the
         * number of blocks processed (buf_len / blocklen) and write it back.
         */
        crypt_be_add(ctx + iv_offset, blocklen, buf_len / blocklen);
        if (address_space_write(&s->dram_as, ctx_addr + iv_offset,
                                MEMTXATTRS_UNSPECIFIED, ctx + iv_offset,
                                blocklen)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Failed to write IV, addr=0x%" HWADDR_PRIx "\n",
                          __func__, ctx_addr + iv_offset);
        }
    } else if (mode == QCRYPTO_CIPHER_MODE_GCM) {
        /*
         * GCM authenticates the message and writes the resulting tag to the
         * dedicated tag buffer (HACE18/HACE8C).
         */
        if (qcrypto_cipher_gettag(cipher, tag, sizeof(tag), &local_err) < 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: qcrypto cipher gettag failed: "
                          "%s\n", __func__, error_get_pretty(local_err));
            error_free(local_err);
            return;
        }
        tag_addr = crypt_get_addr(s, R_CRYPT_GCM_TAG, R_CRYPT_GCM_TAG_HI);
        if (address_space_write(&s->dram_as, tag_addr, MEMTXATTRS_UNSPECIFIED,
                                tag, sizeof(tag))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Failed to write tag, addr=0x%" HWADDR_PRIx "\n",
                          __func__, tag_addr);
        }
    }
}

static uint64_t aspeed_hace_read(void *opaque, hwaddr addr, unsigned int size)
{
    AspeedHACEState *s = ASPEED_HACE(opaque);

    addr >>= 2;

    trace_aspeed_hace_read(addr << 2, s->regs[addr]);

    return s->regs[addr];
}

static void aspeed_hace_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned int size)
{
    AspeedHACEState *s = ASPEED_HACE(opaque);
    AspeedHACEClass *ahc = ASPEED_HACE_GET_CLASS(s);

    addr >>= 2;

    trace_aspeed_hace_write(addr << 2, data);

    switch (addr) {
    case R_STATUS:
        if (data & HASH_IRQ) {
            data &= ~HASH_IRQ;

            if (s->regs[addr] & HASH_IRQ) {
                qemu_irq_lower(s->irq);
            }
        }
        if (data & CRYPT_IRQ) {
            data &= ~CRYPT_IRQ;

            if (s->regs[addr] & CRYPT_IRQ) {
                qemu_irq_lower(s->irq);
            }
        }
        break;
    case R_CRYPT_SRC:
    case R_CRYPT_DEST:
    case R_CRYPT_CONTEXT:
    case R_CRYPT_GCM_TAG:
        data &= ahc->src_mask;
        break;
    case R_CRYPT_DATA_LEN:
    case R_CRYPT_GCM_ADD_LEN:
        data &= CRYPT_DATA_LEN_MASK;
        break;
    case R_HASH_SRC:
        data &= ahc->src_mask;
        break;
    case R_HASH_DIGEST:
        data &= ahc->dest_mask;
        break;
    case R_HASH_KEY_BUFF:
        data &= ahc->key_mask;
        break;
    case R_HASH_SRC_LEN:
        data &= 0x0FFFFFFF;
        break;
    case R_HASH_CMD: {
        int algo;
        data &= ahc->hash_mask;

        if ((data & HASH_DIGEST_HMAC)) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: HMAC mode not implemented\n",
                          __func__);
        }
        if (data & BIT(1)) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: Cascaded mode not implemented\n",
                          __func__);
        }
        algo = hash_algo_lookup(data);
        if (algo < 0) {
                qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: Invalid hash algorithm selection 0x%"PRIx64"\n",
                        __func__, data & ahc->hash_mask);
        } else {
            do_hash_operation(s, algo, data & HASH_SG_EN,
                    ((data & HASH_HMAC_MASK) == HASH_DIGEST_ACCUM));
        }

        /*
         * Set status bits to indicate completion. Testing shows hardware sets
         * these irrespective of HASH_IRQ_EN.
         */
        s->regs[R_STATUS] |= HASH_IRQ;

        if (data & HASH_IRQ_EN) {
            qemu_irq_raise(s->irq);
        }
        break;
    }
    case R_CRYPT_CMD:
        do_crypt_operation(s, data);

        /* Hardware raises the crypt interrupt once the command finishes. */
        s->regs[R_STATUS] |= CRYPT_IRQ;
        if (data & CRYPT_CMD_ISR_EN) {
            qemu_irq_raise(s->irq);
        }
        break;
    case R_HASH_SRC_HI:
        data &= ahc->src_hi_mask;
        break;
    case R_HASH_DIGEST_HI:
        data &= ahc->dest_hi_mask;
        break;
    case R_HASH_KEY_BUFF_HI:
        data &= ahc->key_hi_mask;
        break;
    case R_CRYPT_SRC_HI:
        data &= ahc->src_hi_mask;
        break;
    case R_CRYPT_DEST_HI:
    case R_CRYPT_GCM_TAG_HI:
        data &= ahc->dest_hi_mask;
        break;
    case R_CRYPT_CONTEXT_HI:
        data &= ahc->key_hi_mask;
        break;
    default:
        break;
    }

    s->regs[addr] = data;
}

static const MemoryRegionOps aspeed_hace_ops = {
    .read = aspeed_hace_read,
    .write = aspeed_hace_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_hace_reset_hold(Object *obj, ResetType type)
{
    AspeedHACEState *s = ASPEED_HACE(obj);
    AspeedHACEClass *ahc = ASPEED_HACE_GET_CLASS(s);

    if (s->hash_ctx != NULL) {
        qcrypto_hash_free(s->hash_ctx);
        s->hash_ctx = NULL;
    }

    memset(s->regs, 0, ahc->nr_regs << 2);
    s->total_req_len = 0;
}

static void aspeed_hace_realize(DeviceState *dev, Error **errp)
{
    AspeedHACEState *s = ASPEED_HACE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedHACEClass *ahc = ASPEED_HACE_GET_CLASS(s);

    sysbus_init_irq(sbd, &s->irq);

    s->regs = g_new(uint32_t, ahc->nr_regs);
    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_hace_ops, s,
                          TYPE_ASPEED_HACE, ahc->nr_regs << 2);

    if (!s->dram_mr) {
        error_setg(errp, TYPE_ASPEED_HACE ": 'dram' link not set");
        return;
    }

    address_space_init(&s->dram_as, s->dram_mr, "dram");

    sysbus_init_mmio(sbd, &s->iomem);
}

static const Property aspeed_hace_properties[] = {
    DEFINE_PROP_LINK("dram", AspeedHACEState, dram_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};


static const VMStateDescription vmstate_aspeed_hace = {
    .name = TYPE_ASPEED_HACE,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(total_req_len, AspeedHACEState),
        VMSTATE_END_OF_LIST(),
    }
};

static void aspeed_hace_unrealize(DeviceState *dev)
{
    AspeedHACEState *s = ASPEED_HACE(dev);

    g_free(s->regs);
    s->regs = NULL;
}

static void aspeed_hace_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = aspeed_hace_realize;
    dc->unrealize = aspeed_hace_unrealize;
    rc->phases.hold = aspeed_hace_reset_hold;
    device_class_set_props(dc, aspeed_hace_properties);
    dc->vmsd = &vmstate_aspeed_hace;
}

static void aspeed_ast2400_hace_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedHACEClass *ahc = ASPEED_HACE_CLASS(klass);

    dc->desc = "AST2400 Hash and Crypto Engine";

    ahc->nr_regs = 0x64 >> 2;
    ahc->src_mask = 0x0FFFFFFF;
    ahc->dest_mask = 0x0FFFFFF8;
    ahc->key_mask = 0x0FFFFFC0;
    ahc->hash_mask = 0x000003ff; /* No SG or SHA512 modes */
}

static void aspeed_ast2500_hace_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedHACEClass *ahc = ASPEED_HACE_CLASS(klass);

    dc->desc = "AST2500 Hash and Crypto Engine";

    ahc->nr_regs = 0x64 >> 2;
    ahc->src_mask = 0x3fffffff;
    ahc->dest_mask = 0x3ffffff8;
    ahc->key_mask = 0x3FFFFFC0;
    ahc->hash_mask = 0x000003ff; /* No SG or SHA512 modes */
}

static void aspeed_ast2600_hace_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedHACEClass *ahc = ASPEED_HACE_CLASS(klass);

    dc->desc = "AST2600 Hash and Crypto Engine";

    ahc->nr_regs = 0x64 >> 2;
    ahc->src_mask = 0x7FFFFFFF;
    ahc->dest_mask = 0x7FFFFFF8;
    ahc->key_mask = 0x7FFFFFF8;
    ahc->hash_mask = 0x00147FFF;
}

static void aspeed_ast1030_hace_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedHACEClass *ahc = ASPEED_HACE_CLASS(klass);

    dc->desc = "AST1030 Hash and Crypto Engine";

    ahc->nr_regs = 0x64 >> 2;
    ahc->src_mask = 0x7FFFFFFF;
    ahc->dest_mask = 0x7FFFFFF8;
    ahc->key_mask = 0x7FFFFFF8;
    ahc->hash_mask = 0x00147FFF;
}

static void aspeed_ast2700_hace_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedHACEClass *ahc = ASPEED_HACE_CLASS(klass);

    dc->desc = "AST2700 Hash and Crypto Engine";

    ahc->nr_regs = 0x9C >> 2;
    ahc->src_mask = 0x7FFFFFFF;
    ahc->dest_mask = 0x7FFFFFF8;
    ahc->key_mask = 0x7FFFFFF8;
    ahc->hash_mask = 0x00147FFF;

    /*
     * The AST2700 supports a maximum DRAM size of 8 GB, with a DRAM
     * addressable range from 0x0_0000_0000 to 0x1_FFFF_FFFF. Since this range
     * fits within 34 bits, only bits [33:0] are needed to store the DRAM
     * offset. To optimize address storage, the high physical address bits
     * [1:0] of the source, digest and key buffer addresses are stored as
     * dram_offset bits [33:32].
     *
     * This approach eliminates the need to reduce the high part of the DRAM
     * physical address for DMA operations. Previously, this was calculated as
     * (high physical address bits [7:0] - 4), since the DRAM start address is
     * 0x4_00000000, making the high part address [7:0] - 4.
     */
    ahc->src_hi_mask = 0x00000003;
    ahc->dest_hi_mask = 0x00000003;
    ahc->key_hi_mask = 0x00000003;

    ahc->has_dma64 = true;
}

static const TypeInfo aspeed_hace_types[] = {
    {
        .name = TYPE_ASPEED_HACE,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AspeedHACEState),
        .class_init = aspeed_hace_class_init,
        .class_size = sizeof(AspeedHACEClass),
    },
    {
        .name = TYPE_ASPEED_AST1030_HACE,
        .parent = TYPE_ASPEED_HACE,
        .class_init = aspeed_ast1030_hace_class_init,
    },
    {
        .name = TYPE_ASPEED_AST2400_HACE,
        .parent = TYPE_ASPEED_HACE,
        .class_init = aspeed_ast2400_hace_class_init,
    },
    {
        .name = TYPE_ASPEED_AST2500_HACE,
        .parent = TYPE_ASPEED_HACE,
        .class_init = aspeed_ast2500_hace_class_init,
    },
    {
        .name = TYPE_ASPEED_AST2600_HACE,
        .parent = TYPE_ASPEED_HACE,
        .class_init = aspeed_ast2600_hace_class_init,
    },
    {
        .name = TYPE_ASPEED_AST2700_HACE,
        .parent = TYPE_ASPEED_HACE,
        .class_init = aspeed_ast2700_hace_class_init,
    }
};

DEFINE_TYPES(aspeed_hace_types)
