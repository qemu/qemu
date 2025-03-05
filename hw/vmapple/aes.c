/*
 * QEMU Apple AES device emulation
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "trace.h"
#include "crypto/hash.h"
#include "crypto/aes.h"
#include "crypto/cipher.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/vmapple/vmapple.h"
#include "migration/vmstate.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/dma.h"

OBJECT_DECLARE_SIMPLE_TYPE(AESState, APPLE_AES)

#define MAX_FIFO_SIZE     9

#define CMD_KEY           0x1
#define CMD_KEY_CONTEXT_SHIFT    27
#define CMD_KEY_CONTEXT_MASK     (0x1 << CMD_KEY_CONTEXT_SHIFT)
#define CMD_KEY_SELECT_MAX_IDX   0x7
#define CMD_KEY_SELECT_SHIFT     24
#define CMD_KEY_SELECT_MASK      (CMD_KEY_SELECT_MAX_IDX << CMD_KEY_SELECT_SHIFT)
#define CMD_KEY_KEY_LEN_NUM      4u
#define CMD_KEY_KEY_LEN_SHIFT    22
#define CMD_KEY_KEY_LEN_MASK     ((CMD_KEY_KEY_LEN_NUM - 1u) << CMD_KEY_KEY_LEN_SHIFT)
#define CMD_KEY_ENCRYPT_SHIFT    20
#define CMD_KEY_ENCRYPT_MASK     (0x1 << CMD_KEY_ENCRYPT_SHIFT)
#define CMD_KEY_BLOCK_MODE_SHIFT 16
#define CMD_KEY_BLOCK_MODE_MASK  (0x3 << CMD_KEY_BLOCK_MODE_SHIFT)
#define CMD_IV            0x2
#define CMD_IV_CONTEXT_SHIFT     26
#define CMD_IV_CONTEXT_MASK      (0x3 << CMD_KEY_CONTEXT_SHIFT)
#define CMD_DSB           0x3
#define CMD_SKG           0x4
#define CMD_DATA          0x5
#define CMD_DATA_KEY_CTX_SHIFT   27
#define CMD_DATA_KEY_CTX_MASK    (0x1 << CMD_DATA_KEY_CTX_SHIFT)
#define CMD_DATA_IV_CTX_SHIFT    25
#define CMD_DATA_IV_CTX_MASK     (0x3 << CMD_DATA_IV_CTX_SHIFT)
#define CMD_DATA_LEN_MASK        0xffffff
#define CMD_STORE_IV      0x6
#define CMD_STORE_IV_ADDR_MASK   0xffffff
#define CMD_WRITE_REG     0x7
#define CMD_FLAG          0x8
#define CMD_FLAG_STOP_MASK       BIT(26)
#define CMD_FLAG_RAISE_IRQ_MASK  BIT(27)
#define CMD_FLAG_INFO_MASK       0xff
#define CMD_MAX           0x10

#define CMD_SHIFT         28

#define REG_STATUS            0xc
#define REG_STATUS_DMA_READ_RUNNING     BIT(0)
#define REG_STATUS_DMA_READ_PENDING     BIT(1)
#define REG_STATUS_DMA_WRITE_RUNNING    BIT(2)
#define REG_STATUS_DMA_WRITE_PENDING    BIT(3)
#define REG_STATUS_BUSY                 BIT(4)
#define REG_STATUS_EXECUTING            BIT(5)
#define REG_STATUS_READY                BIT(6)
#define REG_STATUS_TEXT_DPA_SEEDED      BIT(7)
#define REG_STATUS_UNWRAP_DPA_SEEDED    BIT(8)

#define REG_IRQ_STATUS        0x18
#define REG_IRQ_STATUS_INVALID_CMD      BIT(2)
#define REG_IRQ_STATUS_FLAG             BIT(5)
#define REG_IRQ_ENABLE        0x1c
#define REG_WATERMARK         0x20
#define REG_Q_STATUS          0x24
#define REG_FLAG_INFO         0x30
#define REG_FIFO              0x200

static const uint32_t key_lens[CMD_KEY_KEY_LEN_NUM] = {
    [0] = 16,
    [1] = 24,
    [2] = 32,
    [3] = 64,
};

typedef struct Key {
    uint32_t key_len;
    uint8_t key[32];
} Key;

typedef struct IV {
    uint32_t iv[4];
} IV;

static Key builtin_keys[CMD_KEY_SELECT_MAX_IDX + 1] = {
    [1] = {
        .key_len = 32,
        .key = { 0x1 },
    },
    [2] = {
        .key_len = 32,
        .key = { 0x2 },
    },
    [3] = {
        .key_len = 32,
        .key = { 0x3 },
    }
};

struct AESState {
    SysBusDevice parent_obj;

    qemu_irq irq;
    MemoryRegion iomem1;
    MemoryRegion iomem2;
    AddressSpace *as;

    uint32_t status;
    uint32_t q_status;
    uint32_t irq_status;
    uint32_t irq_enable;
    uint32_t watermark;
    uint32_t flag_info;
    uint32_t fifo[MAX_FIFO_SIZE];
    uint32_t fifo_idx;
    Key key[2];
    IV iv[4];
    bool is_encrypt;
    QCryptoCipherMode block_mode;
};

static void aes_update_irq(AESState *s)
{
    qemu_set_irq(s->irq, !!(s->irq_status & s->irq_enable));
}

static uint64_t aes1_read(void *opaque, hwaddr offset, unsigned size)
{
    AESState *s = opaque;
    uint64_t res = 0;

    switch (offset) {
    case REG_STATUS:
        res = s->status;
        break;
    case REG_IRQ_STATUS:
        res = s->irq_status;
        break;
    case REG_IRQ_ENABLE:
        res = s->irq_enable;
        break;
    case REG_WATERMARK:
        res = s->watermark;
        break;
    case REG_Q_STATUS:
        res = s->q_status;
        break;
    case REG_FLAG_INFO:
        res = s->flag_info;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unknown AES MMIO offset %" PRIx64 "\n",
                      __func__, offset);
        break;
    }

    trace_aes_read(offset, res);

    return res;
}

static void fifo_append(AESState *s, uint64_t val)
{
    if (s->fifo_idx == MAX_FIFO_SIZE) {
        /* Exceeded the FIFO. Bail out */
        return;
    }

    s->fifo[s->fifo_idx++] = val;
}

static bool has_payload(AESState *s, uint32_t elems)
{
    return s->fifo_idx >= elems + 1;
}

static bool cmd_key(AESState *s)
{
    uint32_t cmd = s->fifo[0];
    uint32_t key_select = (cmd & CMD_KEY_SELECT_MASK) >> CMD_KEY_SELECT_SHIFT;
    uint32_t ctxt = (cmd & CMD_KEY_CONTEXT_MASK) >> CMD_KEY_CONTEXT_SHIFT;
    uint32_t key_len;

    switch ((cmd & CMD_KEY_BLOCK_MODE_MASK) >> CMD_KEY_BLOCK_MODE_SHIFT) {
    case 0:
        s->block_mode = QCRYPTO_CIPHER_MODE_ECB;
        break;
    case 1:
        s->block_mode = QCRYPTO_CIPHER_MODE_CBC;
        break;
    default:
        return false;
    }

    s->is_encrypt = cmd & CMD_KEY_ENCRYPT_MASK;
    key_len = key_lens[(cmd & CMD_KEY_KEY_LEN_MASK) >> CMD_KEY_KEY_LEN_SHIFT];

    if (key_select) {
        trace_aes_cmd_key_select_builtin(ctxt, key_select,
                                         s->is_encrypt ? "en" : "de",
                                         QCryptoCipherMode_str(s->block_mode));
        s->key[ctxt] = builtin_keys[key_select];
    } else {
        trace_aes_cmd_key_select_new(ctxt, key_len,
                                     s->is_encrypt ? "en" : "de",
                                     QCryptoCipherMode_str(s->block_mode));
        if (key_len > sizeof(s->key[ctxt].key)) {
            return false;
        }
        if (!has_payload(s, key_len / sizeof(uint32_t))) {
            /* wait for payload */
            qemu_log_mask(LOG_GUEST_ERROR, "%s: No payload\n", __func__);
            return false;
        }
        memcpy(&s->key[ctxt].key, &s->fifo[1], key_len);
        s->key[ctxt].key_len = key_len;
    }

    return true;
}

static bool cmd_iv(AESState *s)
{
    uint32_t cmd = s->fifo[0];
    uint32_t ctxt = (cmd & CMD_IV_CONTEXT_MASK) >> CMD_IV_CONTEXT_SHIFT;

    if (!has_payload(s, 4)) {
        /* wait for payload */
        return false;
    }
    memcpy(&s->iv[ctxt].iv, &s->fifo[1], sizeof(s->iv[ctxt].iv));
    trace_aes_cmd_iv(ctxt, s->fifo[1], s->fifo[2], s->fifo[3], s->fifo[4]);

    return true;
}

static void dump_data(const char *desc, const void *p, size_t len)
{
    static const size_t MAX_LEN = 0x1000;
    char hex[MAX_LEN * 2 + 1] = "";

    if (len > MAX_LEN) {
        return;
    }

    qemu_hexdump_to_buffer(hex, sizeof(hex), p, len);
    trace_aes_dump_data(desc, hex);
}

static bool cmd_data(AESState *s)
{
    uint32_t cmd = s->fifo[0];
    uint32_t ctxt_iv = 0;
    uint32_t ctxt_key = (cmd & CMD_DATA_KEY_CTX_MASK) >> CMD_DATA_KEY_CTX_SHIFT;
    uint32_t len = cmd & CMD_DATA_LEN_MASK;
    uint64_t src_addr = s->fifo[2];
    uint64_t dst_addr = s->fifo[3];
    QCryptoCipherAlgo alg;
    g_autoptr(QCryptoCipher) cipher = NULL;
    g_autoptr(GByteArray) src = NULL;
    g_autoptr(GByteArray) dst = NULL;
    MemTxResult r;

    src_addr |= ((uint64_t)s->fifo[1] << 16) & 0xffff00000000ULL;
    dst_addr |= ((uint64_t)s->fifo[1] << 32) & 0xffff00000000ULL;

    trace_aes_cmd_data(ctxt_key, ctxt_iv, src_addr, dst_addr, len);

    if (!has_payload(s, 3)) {
        /* wait for payload */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: No payload\n", __func__);
        return false;
    }

    if (ctxt_key >= ARRAY_SIZE(s->key) ||
        ctxt_iv >= ARRAY_SIZE(s->iv)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid key or iv\n", __func__);
        return false;
    }

    src = g_byte_array_sized_new(len);
    g_byte_array_set_size(src, len);
    dst = g_byte_array_sized_new(len);
    g_byte_array_set_size(dst, len);

    r = dma_memory_read(s->as, src_addr, src->data, len, MEMTXATTRS_UNSPECIFIED);
    if (r != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA read of %"PRIu32" bytes "
                      "from 0x%"PRIx64" failed. (r=%d)\n",
                      __func__, len, src_addr, r);
        return false;
    }

    dump_data("cmd_data(): src_data=", src->data, len);

    switch (s->key[ctxt_key].key_len) {
    case 128 / 8:
        alg = QCRYPTO_CIPHER_ALGO_AES_128;
        break;
    case 192 / 8:
        alg = QCRYPTO_CIPHER_ALGO_AES_192;
        break;
    case 256 / 8:
        alg = QCRYPTO_CIPHER_ALGO_AES_256;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid key length\n", __func__);
        return false;
    }
    cipher = qcrypto_cipher_new(alg, s->block_mode,
                                s->key[ctxt_key].key,
                                s->key[ctxt_key].key_len, NULL);
    if (!cipher) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to create cipher object\n",
                      __func__);
        return false;
    }
    if (s->block_mode != QCRYPTO_CIPHER_MODE_ECB) {
        if (qcrypto_cipher_setiv(cipher, (void *)s->iv[ctxt_iv].iv,
                                 sizeof(s->iv[ctxt_iv].iv), NULL) != 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to set IV\n", __func__);
            return false;
        }
    }
    if (s->is_encrypt) {
        if (qcrypto_cipher_encrypt(cipher, src->data, dst->data, len, NULL) != 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Encryption failed\n", __func__);
            return false;
        }
    } else {
        if (qcrypto_cipher_decrypt(cipher, src->data, dst->data, len, NULL) != 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Decryption failed\n", __func__);
            return false;
        }
    }

    dump_data("cmd_data(): dst_data=", dst->data, len);
    r = dma_memory_write(s->as, dst_addr, dst->data, len, MEMTXATTRS_UNSPECIFIED);
    if (r != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA write of %"PRIu32" bytes "
                      "to 0x%"PRIx64" failed. (r=%d)\n",
                      __func__, len, src_addr, r);
        return false;
    }

    return true;
}

static bool cmd_store_iv(AESState *s)
{
    uint32_t cmd = s->fifo[0];
    uint32_t ctxt = (cmd & CMD_IV_CONTEXT_MASK) >> CMD_IV_CONTEXT_SHIFT;
    uint64_t addr = s->fifo[1];
    MemTxResult dma_result;

    if (!has_payload(s, 1)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: No payload\n", __func__);
        return false;
    }

    if (ctxt >= ARRAY_SIZE(s->iv)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid context. ctxt = %u, allowed: 0..%zu\n",
                      __func__, ctxt, ARRAY_SIZE(s->iv) - 1);
        return false;
    }

    addr |= ((uint64_t)cmd << 32) & 0xff00000000ULL;
    dma_result = dma_memory_write(&address_space_memory, addr,
                                  &s->iv[ctxt].iv, sizeof(s->iv[ctxt].iv),
                                  MEMTXATTRS_UNSPECIFIED);

    trace_aes_cmd_store_iv(ctxt, addr, s->iv[ctxt].iv[0], s->iv[ctxt].iv[1],
                           s->iv[ctxt].iv[2], s->iv[ctxt].iv[3]);

    return dma_result == MEMTX_OK;
}

static bool cmd_flag(AESState *s)
{
    uint32_t cmd = s->fifo[0];
    uint32_t raise_irq = cmd & CMD_FLAG_RAISE_IRQ_MASK;

    /* We always process data when it's coming in, so fire an IRQ immediately */
    if (raise_irq) {
        s->irq_status |= REG_IRQ_STATUS_FLAG;
    }

    s->flag_info = cmd & CMD_FLAG_INFO_MASK;

    trace_aes_cmd_flag(!!raise_irq, s->flag_info);

    return true;
}

static void fifo_process(AESState *s)
{
    uint32_t cmd = s->fifo[0] >> CMD_SHIFT;
    bool success = false;

    if (!s->fifo_idx) {
        return;
    }

    switch (cmd) {
    case CMD_KEY:
        success = cmd_key(s);
        break;
    case CMD_IV:
        success = cmd_iv(s);
        break;
    case CMD_DATA:
        success = cmd_data(s);
        break;
    case CMD_STORE_IV:
        success = cmd_store_iv(s);
        break;
    case CMD_FLAG:
        success = cmd_flag(s);
        break;
    default:
        s->irq_status |= REG_IRQ_STATUS_INVALID_CMD;
        break;
    }

    if (success) {
        s->fifo_idx = 0;
    }

    trace_aes_fifo_process(cmd, success);
}

static void aes1_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    AESState *s = opaque;

    trace_aes_write(offset, val);

    switch (offset) {
    case REG_IRQ_STATUS:
        s->irq_status &= ~val;
        break;
    case REG_IRQ_ENABLE:
        s->irq_enable = val;
        break;
    case REG_FIFO:
        fifo_append(s, val);
        fifo_process(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Unknown AES MMIO offset %"PRIx64", data %"PRIx64"\n",
                      __func__, offset, val);
        return;
    }

    aes_update_irq(s);
}

static const MemoryRegionOps aes1_ops = {
    .read = aes1_read,
    .write = aes1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t aes2_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t res = 0;

    switch (offset) {
    case 0:
        res = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Unknown AES MMIO 2 offset %"PRIx64"\n",
                      __func__, offset);
        break;
    }

    trace_aes_2_read(offset, res);

    return res;
}

static void aes2_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    trace_aes_2_write(offset, val);

    switch (offset) {
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Unknown AES MMIO 2 offset %"PRIx64", data %"PRIx64"\n",
                      __func__, offset, val);
        return;
    }
}

static const MemoryRegionOps aes2_ops = {
    .read = aes2_read,
    .write = aes2_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void aes_reset(Object *obj, ResetType type)
{
    AESState *s = APPLE_AES(obj);

    s->status = 0x3f80;
    s->q_status = 2;
    s->irq_status = 0;
    s->irq_enable = 0;
    s->watermark = 0;
}

static void aes_init(Object *obj)
{
    AESState *s = APPLE_AES(obj);

    memory_region_init_io(&s->iomem1, obj, &aes1_ops, s, TYPE_APPLE_AES, 0x4000);
    memory_region_init_io(&s->iomem2, obj, &aes2_ops, s, TYPE_APPLE_AES, 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem1);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem2);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq);
    s->as = &address_space_memory;
}

static void aes_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = aes_reset;
}

static const TypeInfo aes_info = {
    .name          = TYPE_APPLE_AES,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AESState),
    .class_init    = aes_class_init,
    .instance_init = aes_init,
};

static void aes_register_types(void)
{
    type_register_static(&aes_info);
}

type_init(aes_register_types)
