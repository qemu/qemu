/*
 * ASPEED SDRAM Memory Controller
 *
 * Copyright (C) 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/misc/aspeed_sdmc.h"
#include "hw/misc/aspeed_scu.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "trace.h"

/* Protection Key Register */
#define R_PROT            (0x00 / 4)
#define   PROT_KEY_UNLOCK     0xFC600309

/* Configuration Register */
#define R_CONF            (0x04 / 4)

/* Control/Status Register #1 (ast2500) */
#define R_STATUS1         (0x60 / 4)
#define   PHY_BUSY_STATE      BIT(0)

#define R_ECC_TEST_CTRL   (0x70 / 4)
#define   ECC_TEST_FINISHED   BIT(12)
#define   ECC_TEST_FAIL       BIT(13)

/*
 * Configuration register Ox4 (for Aspeed AST2400 SOC)
 *
 * These are for the record and future use. ASPEED_SDMC_DRAM_SIZE is
 * what we care about right now as it is checked by U-Boot to
 * determine the RAM size.
 */

#define ASPEED_SDMC_RESERVED            0xFFFFF800 /* 31:11 reserved */
#define ASPEED_SDMC_AST2300_COMPAT      (1 << 10)
#define ASPEED_SDMC_SCRAMBLE_PATTERN    (1 << 9)
#define ASPEED_SDMC_DATA_SCRAMBLE       (1 << 8)
#define ASPEED_SDMC_ECC_ENABLE          (1 << 7)
#define ASPEED_SDMC_VGA_COMPAT          (1 << 6) /* readonly */
#define ASPEED_SDMC_DRAM_BANK           (1 << 5)
#define ASPEED_SDMC_DRAM_BURST          (1 << 4)
#define ASPEED_SDMC_VGA_APERTURE(x)     ((x & 0x3) << 2) /* readonly */
#define     ASPEED_SDMC_VGA_8MB             0x0
#define     ASPEED_SDMC_VGA_16MB            0x1
#define     ASPEED_SDMC_VGA_32MB            0x2
#define     ASPEED_SDMC_VGA_64MB            0x3
#define ASPEED_SDMC_DRAM_SIZE(x)        (x & 0x3)
#define     ASPEED_SDMC_DRAM_64MB           0x0
#define     ASPEED_SDMC_DRAM_128MB          0x1
#define     ASPEED_SDMC_DRAM_256MB          0x2
#define     ASPEED_SDMC_DRAM_512MB          0x3

#define ASPEED_SDMC_READONLY_MASK                       \
    (ASPEED_SDMC_RESERVED | ASPEED_SDMC_VGA_COMPAT |    \
     ASPEED_SDMC_VGA_APERTURE(ASPEED_SDMC_VGA_64MB))
/*
 * Configuration register Ox4 (for Aspeed AST2500 SOC and higher)
 *
 * Incompatibilities are annotated in the list. ASPEED_SDMC_HW_VERSION
 * should be set to 1 for the AST2500 SOC.
 */
#define ASPEED_SDMC_HW_VERSION(x)       ((x & 0xf) << 28) /* readonly */
#define ASPEED_SDMC_SW_VERSION          ((x & 0xff) << 20)
#define ASPEED_SDMC_CACHE_INITIAL_DONE  (1 << 19)  /* readonly */
#define ASPEED_SDMC_AST2500_RESERVED    0x7C000 /* 18:14 reserved */
#define ASPEED_SDMC_CACHE_DDR4_CONF     (1 << 13)
#define ASPEED_SDMC_CACHE_INITIAL       (1 << 12)
#define ASPEED_SDMC_CACHE_RANGE_CTRL    (1 << 11)
#define ASPEED_SDMC_CACHE_ENABLE        (1 << 10) /* differs from AST2400 */
#define ASPEED_SDMC_DRAM_TYPE           (1 << 4)  /* differs from AST2400 */

/* DRAM size definitions differs */
#define     ASPEED_SDMC_AST2500_128MB       0x0
#define     ASPEED_SDMC_AST2500_256MB       0x1
#define     ASPEED_SDMC_AST2500_512MB       0x2
#define     ASPEED_SDMC_AST2500_1024MB      0x3

#define ASPEED_SDMC_AST2500_READONLY_MASK                               \
    (ASPEED_SDMC_HW_VERSION(0xf) | ASPEED_SDMC_CACHE_INITIAL_DONE |     \
     ASPEED_SDMC_AST2500_RESERVED | ASPEED_SDMC_VGA_COMPAT |            \
     ASPEED_SDMC_VGA_APERTURE(ASPEED_SDMC_VGA_64MB))

static uint64_t aspeed_sdmc_read(void *opaque, hwaddr addr, unsigned size)
{
    AspeedSDMCState *s = ASPEED_SDMC(opaque);

    addr >>= 2;

    if (addr >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    return s->regs[addr];
}

static void aspeed_sdmc_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned int size)
{
    AspeedSDMCState *s = ASPEED_SDMC(opaque);

    addr >>= 2;

    if (addr >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    if (addr == R_PROT) {
        s->regs[addr] = (data == PROT_KEY_UNLOCK) ? 1 : 0;
        return;
    }

    if (!s->regs[R_PROT]) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: SDMC is locked!\n", __func__);
        return;
    }

    if (addr == R_CONF) {
        /* Make sure readonly bits are kept */
        switch (s->silicon_rev) {
        case AST2400_A0_SILICON_REV:
        case AST2400_A1_SILICON_REV:
            data &= ~ASPEED_SDMC_READONLY_MASK;
            data |= s->fixed_conf;
            break;
        case AST2500_A0_SILICON_REV:
        case AST2500_A1_SILICON_REV:
            data &= ~ASPEED_SDMC_AST2500_READONLY_MASK;
            data |= s->fixed_conf;
            break;
        default:
            g_assert_not_reached();
        }
    }
    if (s->silicon_rev == AST2500_A0_SILICON_REV ||
            s->silicon_rev == AST2500_A1_SILICON_REV) {
        switch (addr) {
        case R_STATUS1:
            /* Will never return 'busy' */
            data &= ~PHY_BUSY_STATE;
            break;
        case R_ECC_TEST_CTRL:
            /* Always done, always happy */
            data |= ECC_TEST_FINISHED;
            data &= ~ECC_TEST_FAIL;
            break;
        default:
            break;
        }
    }

    s->regs[addr] = data;
}

static const MemoryRegionOps aspeed_sdmc_ops = {
    .read = aspeed_sdmc_read,
    .write = aspeed_sdmc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static int ast2400_rambits(AspeedSDMCState *s)
{
    switch (s->ram_size >> 20) {
    case 64:
        return ASPEED_SDMC_DRAM_64MB;
    case 128:
        return ASPEED_SDMC_DRAM_128MB;
    case 256:
        return ASPEED_SDMC_DRAM_256MB;
    case 512:
        return ASPEED_SDMC_DRAM_512MB;
    default:
        break;
    }

    /* use a common default */
    warn_report("Invalid RAM size 0x%" PRIx64 ". Using default 256M",
                s->ram_size);
    s->ram_size = 256 << 20;
    return ASPEED_SDMC_DRAM_256MB;
}

static int ast2500_rambits(AspeedSDMCState *s)
{
    switch (s->ram_size >> 20) {
    case 128:
        return ASPEED_SDMC_AST2500_128MB;
    case 256:
        return ASPEED_SDMC_AST2500_256MB;
    case 512:
        return ASPEED_SDMC_AST2500_512MB;
    case 1024:
        return ASPEED_SDMC_AST2500_1024MB;
    default:
        break;
    }

    /* use a common default */
    warn_report("Invalid RAM size 0x%" PRIx64 ". Using default 512M",
                s->ram_size);
    s->ram_size = 512 << 20;
    return ASPEED_SDMC_AST2500_512MB;
}

static void aspeed_sdmc_reset(DeviceState *dev)
{
    AspeedSDMCState *s = ASPEED_SDMC(dev);

    memset(s->regs, 0, sizeof(s->regs));

    /* Set ram size bit and defaults values */
    s->regs[R_CONF] = s->fixed_conf;
}

static void aspeed_sdmc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedSDMCState *s = ASPEED_SDMC(dev);

    if (!is_supported_silicon_rev(s->silicon_rev)) {
        error_setg(errp, "Unknown silicon revision: 0x%" PRIx32,
                s->silicon_rev);
        return;
    }

    switch (s->silicon_rev) {
    case AST2400_A0_SILICON_REV:
    case AST2400_A1_SILICON_REV:
        s->ram_bits = ast2400_rambits(s);
        s->max_ram_size = 512 << 20;
        s->fixed_conf = ASPEED_SDMC_VGA_COMPAT |
            ASPEED_SDMC_DRAM_SIZE(s->ram_bits);
        break;
    case AST2500_A0_SILICON_REV:
    case AST2500_A1_SILICON_REV:
        s->ram_bits = ast2500_rambits(s);
        s->max_ram_size = 1024 << 20;
        s->fixed_conf = ASPEED_SDMC_HW_VERSION(1) |
            ASPEED_SDMC_VGA_APERTURE(ASPEED_SDMC_VGA_64MB) |
            ASPEED_SDMC_CACHE_INITIAL_DONE |
            ASPEED_SDMC_DRAM_SIZE(s->ram_bits);
        break;
    default:
        g_assert_not_reached();
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_sdmc_ops, s,
                          TYPE_ASPEED_SDMC, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aspeed_sdmc = {
    .name = "aspeed.sdmc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedSDMCState, ASPEED_SDMC_NR_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static Property aspeed_sdmc_properties[] = {
    DEFINE_PROP_UINT32("silicon-rev", AspeedSDMCState, silicon_rev, 0),
    DEFINE_PROP_UINT64("ram-size", AspeedSDMCState, ram_size, 0),
    DEFINE_PROP_UINT64("max-ram-size", AspeedSDMCState, max_ram_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_sdmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_sdmc_realize;
    dc->reset = aspeed_sdmc_reset;
    dc->desc = "ASPEED SDRAM Memory Controller";
    dc->vmsd = &vmstate_aspeed_sdmc;
    dc->props = aspeed_sdmc_properties;
}

static const TypeInfo aspeed_sdmc_info = {
    .name = TYPE_ASPEED_SDMC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedSDMCState),
    .class_init = aspeed_sdmc_class_init,
};

static void aspeed_sdmc_register_types(void)
{
    type_register_static(&aspeed_sdmc_info);
}

type_init(aspeed_sdmc_register_types);
