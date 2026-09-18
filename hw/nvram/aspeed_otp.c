/*
 *  ASPEED OTP (One-Time Programmable) memory
 *
 *  Copyright (C) 2025 Aspeed
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "system/block-backend.h"
#include "hw/core/qdev-properties.h"
#include "hw/nvram/aspeed_otp.h"
#include "hw/nvram/trace.h"

static uint64_t aspeed_otp_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedOTPState *s = opaque;
    uint64_t val = 0;

    memcpy(&val, s->storage + offset, size);

    return val;
}

static bool valid_program_data(uint32_t otp_addr,
                                 uint32_t value, uint32_t prog_bit)
{
    uint32_t programmed_bits, has_programmable_bits;
    bool is_odd = otp_addr & 1;

    /*
     * prog_bit uses 0s to indicate target bits to program:
     *   - if OTP word is even-indexed, programmed bits flip 0->1
     *   - if odd, bits flip 1->0
     * Bit programming is one-way only and irreversible.
     */
    if (is_odd) {
        programmed_bits = ~value & prog_bit;
    } else {
        programmed_bits = value & (~prog_bit);
    }

    /* If any bit can be programmed, accept the request */
    has_programmable_bits = value ^ (~prog_bit);

    if (programmed_bits) {
        trace_aspeed_otp_prog_conflict(otp_addr, programmed_bits);
        for (int i = 0; i < 32; ++i) {
            if (programmed_bits & (1U << i)) {
                trace_aspeed_otp_prog_bit(i);
            }
        }
    }

    return has_programmable_bits != 0;
}

static bool program_otpmem_data(void *opaque, hwaddr otp_offset,
                             uint32_t prog_bit, uint32_t *value)
{
    AspeedOTPState *s = opaque;
    uint32_t otp_addr = otp_offset >> 2;
    bool is_odd = otp_addr & 1;

    memcpy(value, s->storage + otp_offset, sizeof(uint32_t));

    if (!valid_program_data(otp_addr, *value, prog_bit)) {
        return false;
    }

    if (is_odd) {
        *value &= ~prog_bit;
    } else {
        *value |= ~prog_bit;
    }

    return true;
}

static void aspeed_otp_write(void *opaque, hwaddr otp_offset,
                                uint64_t val, unsigned size)
{
    AspeedOTPState *s = opaque;
    uint32_t value;

    if (!program_otpmem_data(s, otp_offset, val, &value)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to program data, value = %x, bit = %"PRIx64"\n",
                      __func__, value, val);
        return;
    }

    memcpy(s->storage + otp_offset, &value, size);

    if (s->blk) {
        if (blk_pwrite(s->blk, otp_offset, size, &value, 0) < 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Failed to write %x to %"HWADDR_PRIx"\n",
                          __func__, value, otp_offset);

            return;
        }
    }
    trace_aspeed_otp_prog(otp_offset, val, value);
}

/*
 * Each OTP configuration setting contains 32 bits of data.
 * Configuration words are grouped in banks of 8, with banks spaced
 * 0x200 dwords apart and words within a bank spaced 2 dwords apart:
 *
 *   offset(n) = OTP_CFG0 + (n / 8) * 0x200 + (n % 8) * 2
 *
 * Returns 0 if the OTP read fails.
 */
uint32_t aspeed_otp_read_config(AspeedOTPState *s, unsigned int cfg_word)
{
    uint32_t otp_addr = OTP_CFG0 + (cfg_word / 8) * 0x200 + (cfg_word % 8) * 2;
    uint32_t value = 0;

    if (address_space_read(&s->as, otp_addr << 2, MEMTXATTRS_UNSPECIFIED,
                            &value, sizeof(value)) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to read OTP config word %u\n",
                      __func__, cfg_word);
        return 0;
    }

    return value;
}

/*
 * OTP straps are a 64-bit value split across two 32-bit config words,
 * each of which is repeated OTP_STRAP_COPY_NUM times for reliability.
 * The two words interleave starting at OTP_STRAP_START_INDEX:
 *
 *   cfg_word:    16   17   18   19   20   21   22   23   24   25   26   27
 *              +----+----+----+----+----+----+----+----+----+----+----+----+
 *              | L0 | H0 | L1 | H1 | L2 | H2 | L3 | H3 | L4 | H4 | L5 | H5 |
 *              +----+----+----+----+----+----+----+----+----+----+----+----+
 *
 *   Lx = copy #x of bit 0-31, Hx = copy #x of bit 32-63
 *   (x = 0 .. OTP_STRAP_COPY_NUM - 1)
 *
 * To resolve strap bit `n` (0 <= n < OTP_STRAP_BIT_NUM):
 *   half    = n / 32   -- 0 selects the L* words, 1 selects the H* words
 *   bit_pos = n % 32   -- bit position within the word
 *   word(i) = OTP_STRAP_START_INDEX + half + i * (OTP_STRAP_BIT_NUM / 32)
 *
 * The effective value of bit `n` is the XOR of bit `bit_pos` across all
 * OTP_STRAP_COPY_NUM copies of word(i).
 */
bool aspeed_otp_read_strap(AspeedOTPState *s, unsigned int bit)
{
    uint32_t cfg_word = OTP_STRAP_START_INDEX + bit / 32;
    uint32_t bit_pos = bit % 32;
    bool enable = false;
    int i;

    assert(bit < OTP_STRAP_BIT_NUM);

    for (i = 0; i < OTP_STRAP_COPY_NUM; i++) {
        enable ^= (aspeed_otp_read_config(s, cfg_word) >> bit_pos) & 0x1;
        cfg_word += OTP_STRAP_BIT_NUM / 32;
    }

    return enable;
}

static bool aspeed_otp_init_storage(AspeedOTPState *s, Error **errp)
{
    uint32_t *p;
    int i, num;
    uint64_t perm;

    if (s->blk) {
        perm = BLK_PERM_CONSISTENT_READ |
               (blk_supports_write_perm(s->blk) ? BLK_PERM_WRITE : 0);
        if (blk_set_perm(s->blk, perm, BLK_PERM_ALL, errp) < 0) {
            return false;
        }
        if (blk_pread(s->blk, 0, s->size, s->storage, 0) < 0) {
            error_setg(errp, "Failed to read the initial flash content");
            return false;
        }
    } else {
        num = s->size / sizeof(uint32_t);
        p = (uint32_t *)s->storage;
        for (i = 0; i < num; i++) {
            p[i] = (i % 2 == 0) ? 0x00000000 : 0xFFFFFFFF;
        }
    }
    return true;
}

static const MemoryRegionOps aspeed_otp_ops = {
    .read = aspeed_otp_read,
    .write = aspeed_otp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .valid.unaligned = true,
    .impl.unaligned = true
};

static void aspeed_otp_realize(DeviceState *dev, Error **errp)
{
    AspeedOTPState *s = ASPEED_OTP(dev);

    if (s->size == 0) {
        error_setg(errp, "aspeed.otp: 'size' property must be set");
        return;
    }

    s->storage = blk_blockalign(s->blk, s->size);

    if (!aspeed_otp_init_storage(s, errp)) {
        return;
    }

    memory_region_init_io(&s->mmio, OBJECT(dev), &aspeed_otp_ops,
                          s, "aspeed.otp", s->size);
    address_space_init(&s->as, &s->mmio, NULL);
}

static const Property aspeed_otp_properties[] = {
    DEFINE_PROP_UINT64("size", AspeedOTPState, size, 0),
    DEFINE_PROP_DRIVE("drive", AspeedOTPState, blk),
};

static void aspeed_otp_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_otp_realize;
    device_class_set_props(dc, aspeed_otp_properties);
}

static const TypeInfo aspeed_otp_types[] = {
    {
        .name          = TYPE_ASPEED_OTP,
        .parent        = TYPE_DEVICE,
        .instance_size = sizeof(AspeedOTPState),
        .class_init    = aspeed_otp_class_init,
    }
};

DEFINE_TYPES(aspeed_otp_types)
