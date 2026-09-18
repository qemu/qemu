/*
 * ASPEED Secure Boot Controller
 *
 * Copyright (C) 2021-2022 IBM Corp.
 *
 * Joel Stanley <joel@jms.id.au>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/aspeed_sbc.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "crypto/akcipher.h"
#include "trace.h"

#define R_PROT          (0x000 / 4)
#define R_CMD           (0x004 / 4)
#define R_ADDR          (0x010 / 4)
#define R_STATUS        (0x014 / 4)
#define R_CAMP1         (0x020 / 4)
#define R_CAMP2         (0x024 / 4)
#define R_QSR           (0x040 / 4)
#define R_SEC_TRIGGER   (0x0bc / 4)

/*
 * SEC SRAM layout for a secp384r1 ECDSA verify operation. All operands are
 * 48-byte big-endian values.
 */
#define ECDSA_SRAM_QX   0x2080
#define ECDSA_SRAM_QY   0x20c0
#define ECDSA_SRAM_R    0x21c0
#define ECDSA_SRAM_S    0x2200
#define ECDSA_SRAM_M    0x2240
#define ECDSA_P384_COORD_LEN    48

/* R_STATUS */
#define ECDSA_VERIFY_PASS       BIT(21)
#define ECDSA_VERIFY_DONE       BIT(20)
#define ABR_EN                  BIT(14) /* Mirrors SCU510[11] */
#define ABR_IMAGE_SOURCE        BIT(13)
#define SPI_ABR_IMAGE_SOURCE    BIT(12)
#define SB_CRYPTO_KEY_EXP_DONE  BIT(11)
#define SB_CRYPTO_BUSY          BIT(10)
#define OTP_WP_EN               BIT(9)
#define OTP_ADDR_WP_EN          BIT(8)
#define LOW_SEC_KEY_EN          BIT(7)
#define SECURE_BOOT_EN          BIT(6)
#define UART_BOOT_EN            BIT(5)
/* bit 4 reserved*/
#define OTP_CHARGE_PUMP_READY   BIT(3)
#define OTP_IDLE                BIT(2)
#define OTP_MEM_IDLE            BIT(1)
#define OTP_COMPARE_STATUS      BIT(0)

/* R_SEC_TRIGGER */
#define ECDSA_CMD_TRIGGER BIT(1)
#define RSA_CMD_TRIGGER   BIT(0)

/* QSR */
#define QSR_RSA_MASK           (0x3 << 12)
#define QSR_HASH_MASK          (0x3 << 10)

/* OTP command */
#define SBC_OTP_CMD_READ 0x23b1e361
#define SBC_OTP_CMD_WRITE 0x23b1e362
#define SBC_OTP_CMD_PROG 0x23b1e364

/* Voltage mode */
#define MODE_REGISTER               (0x1000)
#define MODE_REGISTER_A             (0x3000)
#define MODE_REGISTER_B             (0x5000)

static uint64_t aspeed_sbc_read(void *opaque, hwaddr addr, unsigned int size)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);

    addr >>= 2;

    if (addr >= ASPEED_SBC_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return 0;
    }

    return s->regs[addr];
}

static bool aspeed_otp_read(AspeedSBCState *s, uint32_t otp_addr,
                             uint32_t *value)
{
    if (address_space_read(&s->otp.as, otp_addr << 2, MEMTXATTRS_UNSPECIFIED,
                            value, sizeof(*value)) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Failed to read OTP memory, addr = %x\n",
                      otp_addr);
        return false;
    }

    return true;
}

static bool aspeed_sbc_otp_read(AspeedSBCState *s,
                                   uint32_t otp_addr)
{
    uint32_t value;
    bool is_data = otp_addr < OTP_DATA_DWORD_COUNT;

    if (!aspeed_otp_read(s, otp_addr, &value)) {
        return false;
    }
    s->regs[R_CAMP1] = value;
    trace_aspeed_sbc_otp_read(otp_addr, value);

    if (is_data) {
        if (!aspeed_otp_read(s, otp_addr + 1, &value)) {
            return false;
        }
        s->regs[R_CAMP2] = value;
        trace_aspeed_sbc_otp_read(otp_addr + 1, value);
    }

    return true;
}

static bool mode_handler(uint32_t otp_addr)
{
    switch (otp_addr) {
    case MODE_REGISTER:
    case MODE_REGISTER_A:
    case MODE_REGISTER_B:
        /* HW behavior, do nothing here */
        return true;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Unsupported address 0x%x\n",
                      otp_addr);
        return false;
    }
}

static bool aspeed_sbc_otp_write(AspeedSBCState *s,
                                    uint32_t otp_addr)
{
    if (otp_addr == 0) {
        trace_aspeed_sbc_ignore_cmd(otp_addr);
        return true;
    } else {
        if (mode_handler(otp_addr) == false) {
            return false;
        }
    }

    return true;
}

static bool aspeed_sbc_otp_prog(AspeedSBCState *s,
                                   uint32_t otp_addr)
{
    MemTxResult ret;
    AspeedOTPState *otp = &s->otp;
    uint32_t value = s->regs[R_CAMP1];
    uint32_t otp_offset = otp_addr << 2;

    ret = address_space_write(&otp->as, otp_offset, MEMTXATTRS_UNSPECIFIED,
                              &value, sizeof(value));
    if (ret != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Failed to write OTP memory, addr = %x\n",
                      otp_addr);
        return false;
    }

    trace_aspeed_sbc_otp_prog(otp_addr, value);

    return true;
}

static void aspeed_sbc_handle_command(void *opaque, uint32_t cmd)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);
    AspeedSBCClass *sc = ASPEED_SBC_GET_CLASS(opaque);
    bool ret = false;
    uint32_t otp_addr;

    if (!sc->has_otp) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: OTP memory is not supported\n",
                      __func__);
        return;
    }

    s->regs[R_STATUS] &= ~(OTP_MEM_IDLE | OTP_IDLE);
    otp_addr = s->regs[R_ADDR];

    switch (cmd) {
    case SBC_OTP_CMD_READ:
        ret = aspeed_sbc_otp_read(s, otp_addr);
        break;
    case SBC_OTP_CMD_WRITE:
        ret = aspeed_sbc_otp_write(s, otp_addr);
        break;
    case SBC_OTP_CMD_PROG:
        ret = aspeed_sbc_otp_prog(s, otp_addr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unknown command 0x%x\n",
                      __func__, cmd);
        break;
    }

    trace_aspeed_sbc_handle_cmd(cmd, otp_addr, ret);
    s->regs[R_STATUS] |= (OTP_MEM_IDLE | OTP_IDLE);
}

static void sbc_ecdsa_hexdump(const char *desc, const char *buf, size_t size)
{
    g_autoptr(GString) str = g_string_sized_new(64);
    size_t len;
    size_t i;

    for (i = 0; i < size; i += len) {
        len = MIN(16, size - i);
        g_string_truncate(str, 0);
        qemu_hexdump_line(str, buf + i, len, 1, 4);
        trace_aspeed_sbc_ecdsa_hexdump(desc, i, str->str);
    }
}

/*
 * The hardware only supports ECDSA secp384r1 (NIST P-384). The firmware has
 * already staged the public key, signature and digest in the SEC SRAM; read
 * them out and defer the actual verification to the crypto backend.
 */
static bool aspeed_sbc_ecdsa_verify(AspeedSBCState *s)
{
    QCryptoAkCipherOptions opts = {
        .alg = QCRYPTO_AK_CIPHER_ALGO_ECDSA,
        .u.ecdsa.curve_id = QCRYPTO_CURVE_ID_SECP384R1,
    };
    g_autoptr(QCryptoAkCipher) akcipher = NULL;
    uint8_t pubkey[ECDSA_P384_COORD_LEN * 2];
    uint8_t sig[ECDSA_P384_COORD_LEN * 2];
    uint8_t dgst[ECDSA_P384_COORD_LEN];
    Error *err = NULL;

    if (!qcrypto_akcipher_supports(&opts)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: ECDSA secp384r1 is not supported by the crypto "
                      "backend\n", __func__);
        return false;
    }

    if (address_space_read(&s->sram_as, ECDSA_SRAM_QX,
                           MEMTXATTRS_UNSPECIFIED, pubkey,
                           ECDSA_P384_COORD_LEN) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: failed to read ECDSA QX from SEC SRAM\n", __func__);
        return false;
    }
    if (address_space_read(&s->sram_as, ECDSA_SRAM_QY,
                           MEMTXATTRS_UNSPECIFIED,
                           pubkey + ECDSA_P384_COORD_LEN,
                           ECDSA_P384_COORD_LEN) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: failed to read ECDSA QY from SEC SRAM\n", __func__);
        return false;
    }
    if (address_space_read(&s->sram_as, ECDSA_SRAM_R,
                           MEMTXATTRS_UNSPECIFIED, sig,
                           ECDSA_P384_COORD_LEN) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: failed to read ECDSA R from SEC SRAM\n", __func__);
        return false;
    }
    if (address_space_read(&s->sram_as, ECDSA_SRAM_S,
                           MEMTXATTRS_UNSPECIFIED, sig + ECDSA_P384_COORD_LEN,
                           ECDSA_P384_COORD_LEN) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: failed to read ECDSA S from SEC SRAM\n", __func__);
        return false;
    }
    if (address_space_read(&s->sram_as, ECDSA_SRAM_M,
                           MEMTXATTRS_UNSPECIFIED, dgst,
                           ECDSA_P384_COORD_LEN) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: failed to read ECDSA M from SEC SRAM\n",
                      __func__);
        return false;
    }

    if (trace_event_get_state_backends(TRACE_ASPEED_SBC_ECDSA_HEXDUMP)) {
        sbc_ecdsa_hexdump("pubkey", (char *)pubkey, sizeof(pubkey));
        sbc_ecdsa_hexdump("signature", (char *)sig, sizeof(sig));
        sbc_ecdsa_hexdump("digest", (char *)dgst, sizeof(dgst));
    }

    akcipher = qcrypto_akcipher_new(&opts, QCRYPTO_AK_CIPHER_KEY_TYPE_PUBLIC,
                                    pubkey, sizeof(pubkey), &err);
    if (!akcipher) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %s\n", __func__,
                      error_get_pretty(err));
        error_free(err);
        return false;
    }

    if (qcrypto_akcipher_verify(akcipher, sig, sizeof(sig),
                                dgst, sizeof(dgst), &err) != 0) {
        error_free(err);
        return false;
    }

    return true;
}

static void aspeed_sbc_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned int size)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);
    AspeedSBCClass *sc = ASPEED_SBC_GET_CLASS(s);

    addr >>= 2;

    if (addr >= ASPEED_SBC_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    }

    switch (addr) {
    case R_STATUS:
    case R_QSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read only register 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    case R_CMD:
        aspeed_sbc_handle_command(opaque, data);
        return;
    case R_SEC_TRIGGER:
        if (data & RSA_CMD_TRIGGER) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: RSA is not supported\n", __func__);
        }
        if (data & ECDSA_CMD_TRIGGER) {
            if (!sc->has_ecdsa) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: ECDSA is not supported\n", __func__);
                return;
            }
            s->regs[R_STATUS] &= ~(ECDSA_VERIFY_DONE | ECDSA_VERIFY_PASS);
            if (aspeed_sbc_ecdsa_verify(s)) {
                s->regs[R_STATUS] |= ECDSA_VERIFY_PASS;
            }
            s->regs[R_STATUS] |= ECDSA_VERIFY_DONE;
            trace_aspeed_sbc_ecdsa_verify(
                (s->regs[R_STATUS] & ECDSA_VERIFY_PASS) ? "pass" : "fail");
        }
        return;
    default:
        break;
    }

    s->regs[addr] = data;
}

static const MemoryRegionOps aspeed_sbc_ops = {
    .read = aspeed_sbc_read,
    .write = aspeed_sbc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_sbc_reset_hold(Object *obj, ResetType type)
{
    AspeedSBCState *s = ASPEED_SBC(obj);

    memset(s->regs, 0, sizeof(s->regs));

    /* Set secure boot enabled with RSA4096_SHA256 and enable eMMC ABR */
    s->regs[R_STATUS] = OTP_IDLE | OTP_MEM_IDLE;

    if (s->emmc_abr) {
        s->regs[R_STATUS] &= ABR_EN;
    }

    if (s->signing_settings) {
        s->regs[R_STATUS] &= SECURE_BOOT_EN;
    }

    s->regs[R_QSR] = s->signing_settings;
}

static void aspeed_sbc_instance_init(Object *obj)
{
    AspeedSBCClass *sc = ASPEED_SBC_GET_CLASS(obj);
    AspeedSBCState *s = ASPEED_SBC(obj);

    if (sc->has_otp) {
        object_initialize_child(OBJECT(s), "otp", &s->otp,
                                TYPE_ASPEED_OTP);
    }
}

static void aspeed_sbc_realize(DeviceState *dev, Error **errp)
{
    AspeedSBCState *s = ASPEED_SBC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedSBCClass *sc = ASPEED_SBC_GET_CLASS(dev);

    if (sc->has_otp) {
        object_property_set_int(OBJECT(&s->otp), "size",
                                OTP_MEMORY_SIZE, &error_abort);
        if (!qdev_realize(DEVICE(&s->otp), NULL, errp)) {
            return;
        }
    }

    if (sc->has_ecdsa) {
        if (!s->sram) {
            error_setg(errp, TYPE_ASPEED_SBC ": 'sram' link not set");
            return;
        }
        address_space_init(&s->sram_as, s->sram, TYPE_ASPEED_SBC ".sram");
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_sbc_ops, s,
            TYPE_ASPEED_SBC, ASPEED_SBC_NR_REGS << 2);

    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aspeed_sbc = {
    .name = TYPE_ASPEED_SBC,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedSBCState, ASPEED_SBC_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property aspeed_sbc_properties[] = {
    DEFINE_PROP_BOOL("emmc-abr", AspeedSBCState, emmc_abr, 0),
    DEFINE_PROP_UINT32("signing-settings", AspeedSBCState, signing_settings, 0),
    DEFINE_PROP_LINK("sram", AspeedSBCState, sram,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void aspeed_sbc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = aspeed_sbc_realize;
    rc->phases.hold = aspeed_sbc_reset_hold;
    dc->vmsd = &vmstate_aspeed_sbc;
    device_class_set_props(dc, aspeed_sbc_properties);
}


static void aspeed_ast2600_sbc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSBCClass *sc = ASPEED_SBC_CLASS(klass);

    dc->desc = "AST2600 Secure Boot Controller";
    sc->has_otp = true;
}

static void aspeed_ast10x0_sbc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSBCClass *sc = ASPEED_SBC_CLASS(klass);

    dc->desc = "AST10X0 Secure Boot Controller";
    sc->has_otp = true;
    sc->has_ecdsa = true;
}

static const TypeInfo aspeed_sbc_types[] = {
    {
        .name = TYPE_ASPEED_SBC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AspeedSBCState),
        .instance_init = aspeed_sbc_instance_init,
        .class_init = aspeed_sbc_class_init,
        .class_size = sizeof(AspeedSBCClass),
    },
    {
        .name = TYPE_ASPEED_AST10X0_SBC,
        .parent = TYPE_ASPEED_SBC,
        .class_init = aspeed_ast10x0_sbc_class_init,
    },
    {
        .name = TYPE_ASPEED_AST2600_SBC,
        .parent = TYPE_ASPEED_SBC,
        .class_init = aspeed_ast2600_sbc_class_init,
    }
};

DEFINE_TYPES(aspeed_sbc_types)
