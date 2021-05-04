/*
 * ASPEED Hash and Crypto Engine
 *
 * Copyright (C) 2021 IBM Corp.
 *
 * Joel Stanley <joel@jms.id.au>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/misc/aspeed_hace.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "crypto/hash.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"

#define R_CRYPT_CMD     (0x10 / 4)

#define R_STATUS        (0x1c / 4)
#define HASH_IRQ        BIT(9)
#define CRYPT_IRQ       BIT(12)
#define TAG_IRQ         BIT(15)

#define R_HASH_SRC      (0x20 / 4)
#define R_HASH_DEST     (0x24 / 4)
#define R_HASH_SRC_LEN  (0x2c / 4)

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
#define ASPEED_HACE_MAX_SG              256        /* max number of entries */

static const struct {
    uint32_t mask;
    QCryptoHashAlgorithm algo;
} hash_algo_map[] = {
    { HASH_ALGO_MD5, QCRYPTO_HASH_ALG_MD5 },
    { HASH_ALGO_SHA1, QCRYPTO_HASH_ALG_SHA1 },
    { HASH_ALGO_SHA224, QCRYPTO_HASH_ALG_SHA224 },
    { HASH_ALGO_SHA256, QCRYPTO_HASH_ALG_SHA256 },
    { HASH_ALGO_SHA512_SERIES | HASH_ALGO_SHA512_SHA512, QCRYPTO_HASH_ALG_SHA512 },
    { HASH_ALGO_SHA512_SERIES | HASH_ALGO_SHA512_SHA384, QCRYPTO_HASH_ALG_SHA384 },
    { HASH_ALGO_SHA512_SERIES | HASH_ALGO_SHA512_SHA256, QCRYPTO_HASH_ALG_SHA256 },
};

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

static void do_hash_operation(AspeedHACEState *s, int algo, bool sg_mode)
{
    struct iovec iov[ASPEED_HACE_MAX_SG];
    g_autofree uint8_t *digest_buf;
    size_t digest_len = 0;
    int i;

    if (sg_mode) {
        uint32_t len = 0;

        for (i = 0; !(len & SG_LIST_LEN_LAST); i++) {
            uint32_t addr, src;
            hwaddr plen;

            if (i == ASPEED_HACE_MAX_SG) {
                qemu_log_mask(LOG_GUEST_ERROR,
                        "aspeed_hace: guest failed to set end of sg list marker\n");
                break;
            }

            src = s->regs[R_HASH_SRC] + (i * SG_LIST_ENTRY_SIZE);

            len = address_space_ldl_le(&s->dram_as, src,
                                       MEMTXATTRS_UNSPECIFIED, NULL);

            addr = address_space_ldl_le(&s->dram_as, src + SG_LIST_LEN_SIZE,
                                        MEMTXATTRS_UNSPECIFIED, NULL);
            addr &= SG_LIST_ADDR_MASK;

            iov[i].iov_len = len & SG_LIST_LEN_MASK;
            plen = iov[i].iov_len;
            iov[i].iov_base = address_space_map(&s->dram_as, addr, &plen, false,
                                                MEMTXATTRS_UNSPECIFIED);
        }
    } else {
        hwaddr len = s->regs[R_HASH_SRC_LEN];

        iov[0].iov_len = len;
        iov[0].iov_base = address_space_map(&s->dram_as, s->regs[R_HASH_SRC],
                                            &len, false,
                                            MEMTXATTRS_UNSPECIFIED);
        i = 1;
    }

    if (qcrypto_hash_bytesv(algo, iov, i, &digest_buf, &digest_len, NULL) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: qcrypto failed\n", __func__);
        return;
    }

    if (address_space_write(&s->dram_as, s->regs[R_HASH_DEST],
                            MEMTXATTRS_UNSPECIFIED,
                            digest_buf, digest_len)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "aspeed_hace: address space write failed\n");
    }

    for (; i > 0; i--) {
        address_space_unmap(&s->dram_as, iov[i - 1].iov_base,
                            iov[i - 1].iov_len, false,
                            iov[i - 1].iov_len);
    }

    /*
     * Set status bits to indicate completion. Testing shows hardware sets
     * these irrespective of HASH_IRQ_EN.
     */
    s->regs[R_STATUS] |= HASH_IRQ;
}

static uint64_t aspeed_hace_read(void *opaque, hwaddr addr, unsigned int size)
{
    AspeedHACEState *s = ASPEED_HACE(opaque);

    addr >>= 2;

    if (addr >= ASPEED_HACE_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return 0;
    }

    return s->regs[addr];
}

static void aspeed_hace_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned int size)
{
    AspeedHACEState *s = ASPEED_HACE(opaque);
    AspeedHACEClass *ahc = ASPEED_HACE_GET_CLASS(s);

    addr >>= 2;

    if (addr >= ASPEED_HACE_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    }

    switch (addr) {
    case R_STATUS:
        if (data & HASH_IRQ) {
            data &= ~HASH_IRQ;

            if (s->regs[addr] & HASH_IRQ) {
                qemu_irq_lower(s->irq);
            }
        }
        break;
    case R_HASH_SRC:
        data &= ahc->src_mask;
        break;
    case R_HASH_DEST:
        data &= ahc->dest_mask;
        break;
    case R_HASH_SRC_LEN:
        data &= 0x0FFFFFFF;
        break;
    case R_HASH_CMD: {
        int algo;
        data &= ahc->hash_mask;

        if ((data & HASH_HMAC_MASK)) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: HMAC engine command mode %"PRIx64" not implemented",
                          __func__, (data & HASH_HMAC_MASK) >> 8);
        }
        if (data & BIT(1)) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: Cascaded mode not implemented",
                          __func__);
        }
        algo = hash_algo_lookup(data);
        if (algo < 0) {
                qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: Invalid hash algorithm selection 0x%"PRIx64"\n",
                        __func__, data & ahc->hash_mask);
                break;
        }
        do_hash_operation(s, algo, data & HASH_SG_EN);

        if (data & HASH_IRQ_EN) {
            qemu_irq_raise(s->irq);
        }
        break;
    }
    case R_CRYPT_CMD:
        qemu_log_mask(LOG_UNIMP, "%s: Crypt commands not implemented\n",
                       __func__);
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

static void aspeed_hace_reset(DeviceState *dev)
{
    struct AspeedHACEState *s = ASPEED_HACE(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void aspeed_hace_realize(DeviceState *dev, Error **errp)
{
    AspeedHACEState *s = ASPEED_HACE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_hace_ops, s,
            TYPE_ASPEED_HACE, 0x1000);

    if (!s->dram_mr) {
        error_setg(errp, TYPE_ASPEED_HACE ": 'dram' link not set");
        return;
    }

    address_space_init(&s->dram_as, s->dram_mr, "dram");

    sysbus_init_mmio(sbd, &s->iomem);
}

static Property aspeed_hace_properties[] = {
    DEFINE_PROP_LINK("dram", AspeedHACEState, dram_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};


static const VMStateDescription vmstate_aspeed_hace = {
    .name = TYPE_ASPEED_HACE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedHACEState, ASPEED_HACE_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static void aspeed_hace_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_hace_realize;
    dc->reset = aspeed_hace_reset;
    device_class_set_props(dc, aspeed_hace_properties);
    dc->vmsd = &vmstate_aspeed_hace;
}

static const TypeInfo aspeed_hace_info = {
    .name = TYPE_ASPEED_HACE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedHACEState),
    .class_init = aspeed_hace_class_init,
    .class_size = sizeof(AspeedHACEClass)
};

static void aspeed_ast2400_hace_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedHACEClass *ahc = ASPEED_HACE_CLASS(klass);

    dc->desc = "AST2400 Hash and Crypto Engine";

    ahc->src_mask = 0x0FFFFFFF;
    ahc->dest_mask = 0x0FFFFFF8;
    ahc->hash_mask = 0x000003ff; /* No SG or SHA512 modes */
}

static const TypeInfo aspeed_ast2400_hace_info = {
    .name = TYPE_ASPEED_AST2400_HACE,
    .parent = TYPE_ASPEED_HACE,
    .class_init = aspeed_ast2400_hace_class_init,
};

static void aspeed_ast2500_hace_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedHACEClass *ahc = ASPEED_HACE_CLASS(klass);

    dc->desc = "AST2500 Hash and Crypto Engine";

    ahc->src_mask = 0x3fffffff;
    ahc->dest_mask = 0x3ffffff8;
    ahc->hash_mask = 0x000003ff; /* No SG or SHA512 modes */
}

static const TypeInfo aspeed_ast2500_hace_info = {
    .name = TYPE_ASPEED_AST2500_HACE,
    .parent = TYPE_ASPEED_HACE,
    .class_init = aspeed_ast2500_hace_class_init,
};

static void aspeed_ast2600_hace_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedHACEClass *ahc = ASPEED_HACE_CLASS(klass);

    dc->desc = "AST2600 Hash and Crypto Engine";

    ahc->src_mask = 0x7FFFFFFF;
    ahc->dest_mask = 0x7FFFFFF8;
    ahc->hash_mask = 0x00147FFF;
}

static const TypeInfo aspeed_ast2600_hace_info = {
    .name = TYPE_ASPEED_AST2600_HACE,
    .parent = TYPE_ASPEED_HACE,
    .class_init = aspeed_ast2600_hace_class_init,
};

static void aspeed_hace_register_types(void)
{
    type_register_static(&aspeed_ast2400_hace_info);
    type_register_static(&aspeed_ast2500_hace_info);
    type_register_static(&aspeed_ast2600_hace_info);
    type_register_static(&aspeed_hace_info);
}

type_init(aspeed_hace_register_types);
