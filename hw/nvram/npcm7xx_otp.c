/*
 * Nuvoton NPCM7xx OTP (Fuse Array) Interface
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"

#include "hw/nvram/npcm7xx_otp.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"

/* Each module has 4 KiB of register space. Only a fraction of it is used. */
#define NPCM7XX_OTP_REGS_SIZE (4 * KiB)

/* 32-bit register indices. */
typedef enum NPCM7xxOTPRegister {
    NPCM7XX_OTP_FST,
    NPCM7XX_OTP_FADDR,
    NPCM7XX_OTP_FDATA,
    NPCM7XX_OTP_FCFG,
    /* Offset 0x10 is FKEYIND in OTP1, FUSTRAP in OTP2 */
    NPCM7XX_OTP_FKEYIND = 0x0010 / sizeof(uint32_t),
    NPCM7XX_OTP_FUSTRAP = 0x0010 / sizeof(uint32_t),
    NPCM7XX_OTP_FCTL,
    NPCM7XX_OTP_REGS_END,
} NPCM7xxOTPRegister;

/* Register field definitions. */
#define FST_RIEN BIT(2)
#define FST_RDST BIT(1)
#define FST_RDY BIT(0)
#define FST_RO_MASK (FST_RDST | FST_RDY)

#define FADDR_BYTEADDR(rv) extract32((rv), 0, 10)
#define FADDR_BITPOS(rv) extract32((rv), 10, 3)

#define FDATA_CLEAR 0x00000001

#define FCFG_FDIS BIT(31)
#define FCFG_FCFGLK_MASK 0x00ff0000

#define FCTL_PROG_CMD1 0x00000001
#define FCTL_PROG_CMD2 0xbf79e5d0
#define FCTL_READ_CMD 0x00000002

/**
 * struct NPCM7xxOTPClass - OTP module class.
 * @parent: System bus device class.
 * @mmio_ops: MMIO register operations for this type of module.
 *
 * The two OTP modules (key-storage and fuse-array) have slightly different
 * behavior, so we give them different MMIO register operations.
 */
struct NPCM7xxOTPClass {
    SysBusDeviceClass parent;

    const MemoryRegionOps *mmio_ops;
};

#define NPCM7XX_OTP_CLASS(klass) \
    OBJECT_CLASS_CHECK(NPCM7xxOTPClass, (klass), TYPE_NPCM7XX_OTP)
#define NPCM7XX_OTP_GET_CLASS(obj) \
    OBJECT_GET_CLASS(NPCM7xxOTPClass, (obj), TYPE_NPCM7XX_OTP)

static uint8_t ecc_encode_nibble(uint8_t n)
{
    uint8_t result = n;

    result |= (((n >> 0) & 1) ^ ((n >> 1) & 1)) << 4;
    result |= (((n >> 2) & 1) ^ ((n >> 3) & 1)) << 5;
    result |= (((n >> 0) & 1) ^ ((n >> 2) & 1)) << 6;
    result |= (((n >> 1) & 1) ^ ((n >> 3) & 1)) << 7;

    return result;
}

void npcm7xx_otp_array_write(NPCM7xxOTPState *s, const void *data,
                             unsigned int offset, unsigned int len)
{
    const uint8_t *src = data;
    uint8_t *dst = &s->array[offset];

    while (len-- > 0) {
        uint8_t c = *src++;

        *dst++ = ecc_encode_nibble(extract8(c, 0, 4));
        *dst++ = ecc_encode_nibble(extract8(c, 4, 4));
    }
}

/* Common register read handler for both OTP classes. */
static uint64_t npcm7xx_otp_read(NPCM7xxOTPState *s, NPCM7xxOTPRegister reg)
{
    uint32_t value = 0;

    switch (reg) {
    case NPCM7XX_OTP_FST:
    case NPCM7XX_OTP_FADDR:
    case NPCM7XX_OTP_FDATA:
    case NPCM7XX_OTP_FCFG:
        value = s->regs[reg];
        break;

    case NPCM7XX_OTP_FCTL:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from write-only FCTL register\n",
                      DEVICE(s)->canonical_path);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from invalid offset 0x%zx\n",
                      DEVICE(s)->canonical_path, reg * sizeof(uint32_t));
        break;
    }

    return value;
}

/* Read a byte from the OTP array into the data register. */
static void npcm7xx_otp_read_array(NPCM7xxOTPState *s)
{
    uint32_t faddr = s->regs[NPCM7XX_OTP_FADDR];

    s->regs[NPCM7XX_OTP_FDATA] = s->array[FADDR_BYTEADDR(faddr)];
    s->regs[NPCM7XX_OTP_FST] |= FST_RDST | FST_RDY;
}

/* Program a byte from the data register into the OTP array. */
static void npcm7xx_otp_program_array(NPCM7xxOTPState *s)
{
    uint32_t faddr = s->regs[NPCM7XX_OTP_FADDR];

    /* Bits can only go 0->1, never 1->0. */
    s->array[FADDR_BYTEADDR(faddr)] |= (1U << FADDR_BITPOS(faddr));
    s->regs[NPCM7XX_OTP_FST] |= FST_RDST | FST_RDY;
}

/* Compute the next value of the FCFG register. */
static uint32_t npcm7xx_otp_compute_fcfg(uint32_t cur_value, uint32_t new_value)
{
    uint32_t lock_mask;
    uint32_t value;

    /*
     * FCFGLK holds sticky bits 16..23, indicating which bits in FPRGLK (8..15)
     * and FRDLK (0..7) that are read-only.
     */
    lock_mask = (cur_value & FCFG_FCFGLK_MASK) >> 8;
    lock_mask |= lock_mask >> 8;
    /* FDIS and FCFGLK bits are sticky (write 1 to set; can't clear). */
    value = cur_value & (FCFG_FDIS | FCFG_FCFGLK_MASK);
    /* Preserve read-only bits in FPRGLK and FRDLK */
    value |= cur_value & lock_mask;
    /* Set all bits that aren't read-only. */
    value |= new_value & ~lock_mask;

    return value;
}

/* Common register write handler for both OTP classes. */
static void npcm7xx_otp_write(NPCM7xxOTPState *s, NPCM7xxOTPRegister reg,
                              uint32_t value)
{
    switch (reg) {
    case NPCM7XX_OTP_FST:
        /* RDST is cleared by writing 1 to it. */
        if (value & FST_RDST) {
            s->regs[NPCM7XX_OTP_FST] &= ~FST_RDST;
        }
        /* Preserve read-only and write-one-to-clear bits */
        value &= ~FST_RO_MASK;
        value |= s->regs[NPCM7XX_OTP_FST] & FST_RO_MASK;
        break;

    case NPCM7XX_OTP_FADDR:
        break;

    case NPCM7XX_OTP_FDATA:
        /*
         * This register is cleared by writing a magic value to it; no other
         * values can be written.
         */
        if (value == FDATA_CLEAR) {
            value = 0;
        } else {
            value = s->regs[NPCM7XX_OTP_FDATA];
        }
        break;

    case NPCM7XX_OTP_FCFG:
        value = npcm7xx_otp_compute_fcfg(s->regs[NPCM7XX_OTP_FCFG], value);
        break;

    case NPCM7XX_OTP_FCTL:
        switch (value) {
        case FCTL_READ_CMD:
            npcm7xx_otp_read_array(s);
            break;

        case FCTL_PROG_CMD1:
            /*
             * Programming requires writing two separate magic values to this
             * register; this is the first one. Just store it so it can be
             * verified later when the second magic value is received.
             */
            break;

        case FCTL_PROG_CMD2:
            /*
             * Only initiate programming if we received the first half of the
             * command immediately before this one.
             */
            if (s->regs[NPCM7XX_OTP_FCTL] == FCTL_PROG_CMD1) {
                npcm7xx_otp_program_array(s);
            }
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: unrecognized FCNTL value 0x%" PRIx32 "\n",
                          DEVICE(s)->canonical_path, value);
            break;
        }
        if (value != FCTL_PROG_CMD1) {
            value = 0;
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to invalid offset 0x%zx\n",
                      DEVICE(s)->canonical_path, reg * sizeof(uint32_t));
        return;
    }

    s->regs[reg] = value;
}

/* Register read handler specific to the fuse array OTP module. */
static uint64_t npcm7xx_fuse_array_read(void *opaque, hwaddr addr,
                                        unsigned int size)
{
    NPCM7xxOTPRegister reg = addr / sizeof(uint32_t);
    NPCM7xxOTPState *s = opaque;
    uint32_t value;

    /*
     * Only the Fuse Strap register needs special handling; all other registers
     * work the same way for both kinds of OTP modules.
     */
    if (reg != NPCM7XX_OTP_FUSTRAP) {
        value = npcm7xx_otp_read(s, reg);
    } else {
        /* FUSTRAP is stored as three copies in the OTP array. */
        uint32_t fustrap[3];

        memcpy(fustrap, &s->array[0], sizeof(fustrap));

        /* Determine value by a majority vote on each bit. */
        value = (fustrap[0] & fustrap[1]) | (fustrap[0] & fustrap[2]) |
                (fustrap[1] & fustrap[2]);
    }

    return value;
}

/* Register write handler specific to the fuse array OTP module. */
static void npcm7xx_fuse_array_write(void *opaque, hwaddr addr, uint64_t v,
                                     unsigned int size)
{
    NPCM7xxOTPRegister reg = addr / sizeof(uint32_t);
    NPCM7xxOTPState *s = opaque;

    /*
     * The Fuse Strap register is read-only. Other registers are handled by
     * common code.
     */
    if (reg != NPCM7XX_OTP_FUSTRAP) {
        npcm7xx_otp_write(s, reg, v);
    }
}

static const MemoryRegionOps npcm7xx_fuse_array_ops = {
    .read       = npcm7xx_fuse_array_read,
    .write      = npcm7xx_fuse_array_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = {
        .min_access_size        = 4,
        .max_access_size        = 4,
        .unaligned              = false,
    },
};

/* Register read handler specific to the key storage OTP module. */
static uint64_t npcm7xx_key_storage_read(void *opaque, hwaddr addr,
                                         unsigned int size)
{
    NPCM7xxOTPRegister reg = addr / sizeof(uint32_t);
    NPCM7xxOTPState *s = opaque;

    /*
     * Only the Fuse Key Index register needs special handling; all other
     * registers work the same way for both kinds of OTP modules.
     */
    if (reg != NPCM7XX_OTP_FKEYIND) {
        return npcm7xx_otp_read(s, reg);
    }

    qemu_log_mask(LOG_UNIMP, "%s: FKEYIND is not implemented\n", __func__);

    return s->regs[NPCM7XX_OTP_FKEYIND];
}

/* Register write handler specific to the key storage OTP module. */
static void npcm7xx_key_storage_write(void *opaque, hwaddr addr, uint64_t v,
                                      unsigned int size)
{
    NPCM7xxOTPRegister reg = addr / sizeof(uint32_t);
    NPCM7xxOTPState *s = opaque;

    /*
     * Only the Fuse Key Index register needs special handling; all other
     * registers work the same way for both kinds of OTP modules.
     */
    if (reg != NPCM7XX_OTP_FKEYIND) {
        npcm7xx_otp_write(s, reg, v);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "%s: FKEYIND is not implemented\n", __func__);

    s->regs[NPCM7XX_OTP_FKEYIND] = v;
}

static const MemoryRegionOps npcm7xx_key_storage_ops = {
    .read       = npcm7xx_key_storage_read,
    .write      = npcm7xx_key_storage_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = {
        .min_access_size        = 4,
        .max_access_size        = 4,
        .unaligned              = false,
    },
};

static void npcm7xx_otp_enter_reset(Object *obj, ResetType type)
{
    NPCM7xxOTPState *s = NPCM7XX_OTP(obj);

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[NPCM7XX_OTP_FST] = 0x00000001;
    s->regs[NPCM7XX_OTP_FCFG] = 0x20000000;
}

static void npcm7xx_otp_realize(DeviceState *dev, Error **errp)
{
    NPCM7xxOTPClass *oc = NPCM7XX_OTP_GET_CLASS(dev);
    NPCM7xxOTPState *s = NPCM7XX_OTP(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memset(s->array, 0, sizeof(s->array));

    memory_region_init_io(&s->mmio, OBJECT(s), oc->mmio_ops, s, "regs",
                          NPCM7XX_OTP_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
}

static const VMStateDescription vmstate_npcm7xx_otp = {
    .name = "npcm7xx-otp",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, NPCM7xxOTPState, NPCM7XX_OTP_NR_REGS),
        VMSTATE_UINT8_ARRAY(array, NPCM7xxOTPState, NPCM7XX_OTP_ARRAY_BYTES),
        VMSTATE_END_OF_LIST(),
    },
};

static void npcm7xx_otp_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    QEMU_BUILD_BUG_ON(NPCM7XX_OTP_REGS_END > NPCM7XX_OTP_NR_REGS);

    dc->realize = npcm7xx_otp_realize;
    dc->vmsd = &vmstate_npcm7xx_otp;
    rc->phases.enter = npcm7xx_otp_enter_reset;
}

static void npcm7xx_key_storage_class_init(ObjectClass *klass, void *data)
{
    NPCM7xxOTPClass *oc = NPCM7XX_OTP_CLASS(klass);

    oc->mmio_ops = &npcm7xx_key_storage_ops;
}

static void npcm7xx_fuse_array_class_init(ObjectClass *klass, void *data)
{
    NPCM7xxOTPClass *oc = NPCM7XX_OTP_CLASS(klass);

    oc->mmio_ops = &npcm7xx_fuse_array_ops;
}

static const TypeInfo npcm7xx_otp_types[] = {
    {
        .name = TYPE_NPCM7XX_OTP,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NPCM7xxOTPState),
        .class_size = sizeof(NPCM7xxOTPClass),
        .class_init = npcm7xx_otp_class_init,
        .abstract = true,
    },
    {
        .name = TYPE_NPCM7XX_KEY_STORAGE,
        .parent = TYPE_NPCM7XX_OTP,
        .class_init = npcm7xx_key_storage_class_init,
    },
    {
        .name = TYPE_NPCM7XX_FUSE_ARRAY,
        .parent = TYPE_NPCM7XX_OTP,
        .class_init = npcm7xx_fuse_array_class_init,
    },
};
DEFINE_TYPES(npcm7xx_otp_types);
