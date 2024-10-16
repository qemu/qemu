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
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "trace.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/visitor.h"

/* Protection Key Register */
#define R_PROT            (0x00 / 4)
#define   PROT_UNLOCKED      0x01
#define   PROT_HARDLOCKED    0x10  /* AST2600 */
#define   PROT_SOFTLOCKED    0x00

#define   PROT_KEY_UNLOCK     0xFC600309
#define   PROT_2700_KEY_UNLOCK  0x1688A8A8
#define   PROT_KEY_HARDLOCK   0xDEADDEAD /* AST2600 */

/* Configuration Register */
#define R_CONF            (0x04 / 4)

/* Interrupt control/status */
#define R_ISR             (0x50 / 4)

/* Control/Status Register #1 (ast2500) */
#define R_STATUS1         (0x60 / 4)
#define   PHY_BUSY_STATE      BIT(0)
#define   PHY_PLL_LOCK_STATUS BIT(4)

/* Reserved */
#define R_MCR6C           (0x6c / 4)

#define R_ECC_TEST_CTRL   (0x70 / 4)
#define   ECC_TEST_FINISHED   BIT(12)
#define   ECC_TEST_FAIL       BIT(13)

#define R_TEST_START_LEN  (0x74 / 4)
#define R_TEST_FAIL_DQ    (0x78 / 4)
#define R_TEST_INIT_VAL   (0x7c / 4)
#define R_DRAM_SW         (0x88 / 4)
#define R_DRAM_TIME       (0x8c / 4)
#define R_ECC_ERR_INJECT  (0xb4 / 4)

/* AST2700 Register */
#define R_2700_PROT                 (0x00 / 4)
#define R_INT_STATUS                (0x04 / 4)
#define R_INT_CLEAR                 (0x08 / 4)
#define R_INT_MASK                  (0x0c / 4)
#define R_MAIN_CONF                 (0x10 / 4)
#define R_MAIN_CONTROL              (0x14 / 4)
#define R_MAIN_STATUS               (0x18 / 4)
#define R_ERR_STATUS                (0x1c / 4)
#define R_ECC_FAIL_STATUS           (0x78 / 4)
#define R_ECC_FAIL_ADDR             (0x7c / 4)
#define R_ECC_TESTING_CONTROL       (0x80 / 4)
#define R_PROT_REGION_LOCK_STATUS   (0x94 / 4)
#define R_TEST_FAIL_ADDR            (0xd4 / 4)
#define R_TEST_FAIL_D0              (0xd8 / 4)
#define R_TEST_FAIL_D1              (0xdc / 4)
#define R_TEST_FAIL_D2              (0xe0 / 4)
#define R_TEST_FAIL_D3              (0xe4 / 4)
#define R_DBG_STATUS                (0xf4 / 4)
#define R_PHY_INTERFACE_STATUS      (0xf8 / 4)
#define R_GRAPHIC_MEM_BASE_ADDR     (0x10c / 4)
#define R_PORT0_INTERFACE_MONITOR0  (0x240 / 4)
#define R_PORT0_INTERFACE_MONITOR1  (0x244 / 4)
#define R_PORT0_INTERFACE_MONITOR2  (0x248 / 4)
#define R_PORT1_INTERFACE_MONITOR0  (0x2c0 / 4)
#define R_PORT1_INTERFACE_MONITOR1  (0x2c4 / 4)
#define R_PORT1_INTERFACE_MONITOR2  (0x2c8 / 4)
#define R_PORT2_INTERFACE_MONITOR0  (0x340 / 4)
#define R_PORT2_INTERFACE_MONITOR1  (0x344 / 4)
#define R_PORT2_INTERFACE_MONITOR2  (0x348 / 4)
#define R_PORT3_INTERFACE_MONITOR0  (0x3c0 / 4)
#define R_PORT3_INTERFACE_MONITOR1  (0x3c4 / 4)
#define R_PORT3_INTERFACE_MONITOR2  (0x3c8 / 4)
#define R_PORT4_INTERFACE_MONITOR0  (0x440 / 4)
#define R_PORT4_INTERFACE_MONITOR1  (0x444 / 4)
#define R_PORT4_INTERFACE_MONITOR2  (0x448 / 4)
#define R_PORT5_INTERFACE_MONITOR0  (0x4c0 / 4)
#define R_PORT5_INTERFACE_MONITOR1  (0x4c4 / 4)
#define R_PORT5_INTERFACE_MONITOR2  (0x4c8 / 4)

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

#define ASPEED_SDMC_AST2500_READONLY_MASK                               \
    (ASPEED_SDMC_HW_VERSION(0xf) | ASPEED_SDMC_CACHE_INITIAL_DONE |     \
     ASPEED_SDMC_AST2500_RESERVED | ASPEED_SDMC_VGA_COMPAT |            \
     ASPEED_SDMC_VGA_APERTURE(ASPEED_SDMC_VGA_64MB))

/*
 * Main Configuration register Ox10 (for Aspeed AST2700 SOC and higher)
 *
 */
#define ASPEED_SDMC_AST2700_RESERVED        0xFFFF2082 /* 31:16, 13, 7, 1 */
#define ASPEED_SDMC_AST2700_DATA_SCRAMBLE           (1 << 8)
#define ASPEED_SDMC_AST2700_ECC_ENABLE              (1 << 6)
#define ASPEED_SDMC_AST2700_PAGE_MATCHING_ENABLE    (1 << 5)
#define ASPEED_SDMC_AST2700_DRAM_SIZE(x)            ((x & 0x7) << 2)

#define ASPEED_SDMC_AST2700_READONLY_MASK   \
     (ASPEED_SDMC_AST2700_RESERVED)

static uint64_t aspeed_sdmc_read(void *opaque, hwaddr addr, unsigned size)
{
    AspeedSDMCState *s = ASPEED_SDMC(opaque);

    addr >>= 2;

    if (addr >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr * 4);
        return 0;
    }

    trace_aspeed_sdmc_read(addr, s->regs[addr]);
    return s->regs[addr];
}

static void aspeed_sdmc_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned int size)
{
    AspeedSDMCState *s = ASPEED_SDMC(opaque);
    AspeedSDMCClass *asc = ASPEED_SDMC_GET_CLASS(s);

    addr >>= 2;

    if (addr >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    trace_aspeed_sdmc_write(addr, data);
    asc->write(s, addr, data);
}

static const MemoryRegionOps aspeed_sdmc_ops = {
    .read = aspeed_sdmc_read,
    .write = aspeed_sdmc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void aspeed_sdmc_reset(DeviceState *dev)
{
    AspeedSDMCState *s = ASPEED_SDMC(dev);
    AspeedSDMCClass *asc = ASPEED_SDMC_GET_CLASS(s);

    memset(s->regs, 0, sizeof(s->regs));

    /* Set ram size bit and defaults values */
    s->regs[R_CONF] = asc->compute_conf(s, 0);

    /*
     * PHY status:
     *  - set phy status ok (set bit 1)
     *  - initial PVT calibration ok (clear bit 3)
     *  - runtime calibration ok (clear bit 5)
     */
    s->regs[0x100] = BIT(1);

    /* PHY eye window: set all as passing */
    s->regs[0x100 | (0x68 / 4)] = 0xff;
    s->regs[0x100 | (0x7c / 4)] = 0xff;
    s->regs[0x100 | (0x50 / 4)] = 0xfffffff;
}

static void aspeed_sdmc_get_ram_size(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    AspeedSDMCState *s = ASPEED_SDMC(obj);
    int64_t value = s->ram_size;

    visit_type_int(v, name, &value, errp);
}

static void aspeed_sdmc_set_ram_size(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    int i;
    char *sz;
    int64_t value;
    AspeedSDMCState *s = ASPEED_SDMC(obj);
    AspeedSDMCClass *asc = ASPEED_SDMC_GET_CLASS(s);

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    for (i = 0; asc->valid_ram_sizes[i]; i++) {
        if (value == asc->valid_ram_sizes[i]) {
            s->ram_size = value;
            return;
        }
    }

    sz = size_to_str(value);
    error_setg(errp, "Invalid RAM size %s", sz);
    g_free(sz);
}

static void aspeed_sdmc_initfn(Object *obj)
{
    object_property_add(obj, "ram-size", "int",
                        aspeed_sdmc_get_ram_size, aspeed_sdmc_set_ram_size,
                        NULL, NULL);
}

static void aspeed_sdmc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedSDMCState *s = ASPEED_SDMC(dev);
    AspeedSDMCClass *asc = ASPEED_SDMC_GET_CLASS(s);

    assert(asc->max_ram_size < 4 * GiB || asc->is_bus64bit);

    if (!s->ram_size) {
        error_setg(errp, "RAM size is not set");
        return;
    }

    s->max_ram_size = asc->max_ram_size;

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_sdmc_ops, s,
                          TYPE_ASPEED_SDMC, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aspeed_sdmc = {
    .name = "aspeed.sdmc",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedSDMCState, ASPEED_SDMC_NR_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static Property aspeed_sdmc_properties[] = {
    DEFINE_PROP_UINT64("max-ram-size", AspeedSDMCState, max_ram_size, 0),
    DEFINE_PROP_BOOL("unlocked", AspeedSDMCState, unlocked, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_sdmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_sdmc_realize;
    device_class_set_legacy_reset(dc, aspeed_sdmc_reset);
    dc->desc = "ASPEED SDRAM Memory Controller";
    dc->vmsd = &vmstate_aspeed_sdmc;
    device_class_set_props(dc, aspeed_sdmc_properties);
}

static const TypeInfo aspeed_sdmc_info = {
    .name = TYPE_ASPEED_SDMC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedSDMCState),
    .instance_init = aspeed_sdmc_initfn,
    .class_init = aspeed_sdmc_class_init,
    .class_size = sizeof(AspeedSDMCClass),
    .abstract   = true,
};

static int aspeed_sdmc_get_ram_bits(AspeedSDMCState *s)
{
    AspeedSDMCClass *asc = ASPEED_SDMC_GET_CLASS(s);
    int i;

    /*
     * The bitfield value encoding the RAM size is the index of the
     * possible RAM size array
     */
    for (i = 0; asc->valid_ram_sizes[i]; i++) {
        if (s->ram_size == asc->valid_ram_sizes[i]) {
            return i;
        }
    }

    /*
     * Invalid RAM sizes should have been excluded when setting the
     * SoC RAM size.
     */
    g_assert_not_reached();
}

static uint32_t aspeed_2400_sdmc_compute_conf(AspeedSDMCState *s, uint32_t data)
{
    uint32_t fixed_conf = ASPEED_SDMC_VGA_COMPAT |
        ASPEED_SDMC_DRAM_SIZE(aspeed_sdmc_get_ram_bits(s));

    /* Make sure readonly bits are kept */
    data &= ~ASPEED_SDMC_READONLY_MASK;

    return data | fixed_conf;
}

static void aspeed_2400_sdmc_write(AspeedSDMCState *s, uint32_t reg,
                                   uint32_t data)
{
    if (reg == R_PROT) {
        s->regs[reg] =
            (data == PROT_KEY_UNLOCK) ? PROT_UNLOCKED : PROT_SOFTLOCKED;
        return;
    }

    if (!s->regs[R_PROT]) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: SDMC is locked!\n", __func__);
        return;
    }

    switch (reg) {
    case R_CONF:
        data = aspeed_2400_sdmc_compute_conf(s, data);
        break;
    default:
        break;
    }

    s->regs[reg] = data;
}

static const uint64_t
aspeed_2400_ram_sizes[] = { 64 * MiB, 128 * MiB, 256 * MiB, 512 * MiB, 0};

static void aspeed_2400_sdmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSDMCClass *asc = ASPEED_SDMC_CLASS(klass);

    dc->desc = "ASPEED 2400 SDRAM Memory Controller";
    asc->max_ram_size = 512 * MiB;
    asc->compute_conf = aspeed_2400_sdmc_compute_conf;
    asc->write = aspeed_2400_sdmc_write;
    asc->valid_ram_sizes = aspeed_2400_ram_sizes;
}

static const TypeInfo aspeed_2400_sdmc_info = {
    .name = TYPE_ASPEED_2400_SDMC,
    .parent = TYPE_ASPEED_SDMC,
    .class_init = aspeed_2400_sdmc_class_init,
};

static uint32_t aspeed_2500_sdmc_compute_conf(AspeedSDMCState *s, uint32_t data)
{
    uint32_t fixed_conf = ASPEED_SDMC_HW_VERSION(1) |
        ASPEED_SDMC_VGA_APERTURE(ASPEED_SDMC_VGA_64MB) |
        ASPEED_SDMC_CACHE_INITIAL_DONE |
        ASPEED_SDMC_DRAM_SIZE(aspeed_sdmc_get_ram_bits(s));

    /* Make sure readonly bits are kept */
    data &= ~ASPEED_SDMC_AST2500_READONLY_MASK;

    return data | fixed_conf;
}

static void aspeed_2500_sdmc_write(AspeedSDMCState *s, uint32_t reg,
                                   uint32_t data)
{
    if (reg == R_PROT) {
        s->regs[reg] =
            (data == PROT_KEY_UNLOCK) ? PROT_UNLOCKED : PROT_SOFTLOCKED;
        return;
    }

    if (!s->regs[R_PROT]) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: SDMC is locked!\n", __func__);
        return;
    }

    switch (reg) {
    case R_CONF:
        data = aspeed_2500_sdmc_compute_conf(s, data);
        break;
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

    s->regs[reg] = data;
}

static const uint64_t
aspeed_2500_ram_sizes[] = { 128 * MiB, 256 * MiB, 512 * MiB, 1024 * MiB, 0};

static void aspeed_2500_sdmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSDMCClass *asc = ASPEED_SDMC_CLASS(klass);

    dc->desc = "ASPEED 2500 SDRAM Memory Controller";
    asc->max_ram_size = 1 * GiB;
    asc->compute_conf = aspeed_2500_sdmc_compute_conf;
    asc->write = aspeed_2500_sdmc_write;
    asc->valid_ram_sizes = aspeed_2500_ram_sizes;
}

static const TypeInfo aspeed_2500_sdmc_info = {
    .name = TYPE_ASPEED_2500_SDMC,
    .parent = TYPE_ASPEED_SDMC,
    .class_init = aspeed_2500_sdmc_class_init,
};

static uint32_t aspeed_2600_sdmc_compute_conf(AspeedSDMCState *s, uint32_t data)
{
    uint32_t fixed_conf = ASPEED_SDMC_HW_VERSION(3) |
        ASPEED_SDMC_VGA_APERTURE(ASPEED_SDMC_VGA_64MB) |
        ASPEED_SDMC_DRAM_SIZE(aspeed_sdmc_get_ram_bits(s));

    /* Make sure readonly bits are kept (use ast2500 mask) */
    data &= ~ASPEED_SDMC_AST2500_READONLY_MASK;

    return data | fixed_conf;
}

static void aspeed_2600_sdmc_write(AspeedSDMCState *s, uint32_t reg,
                                   uint32_t data)
{
    /* Unprotected registers */
    switch (reg) {
    case R_ISR:
    case R_MCR6C:
    case R_TEST_START_LEN:
    case R_TEST_FAIL_DQ:
    case R_TEST_INIT_VAL:
    case R_DRAM_SW:
    case R_DRAM_TIME:
    case R_ECC_ERR_INJECT:
        s->regs[reg] = data;
        return;
    }

    if (s->regs[R_PROT] == PROT_HARDLOCKED) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SDMC is locked until system reset!\n",
                      __func__);
        return;
    }

    if (reg != R_PROT && s->regs[R_PROT] == PROT_SOFTLOCKED) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SDMC is locked! (write to MCR%02x blocked)\n",
                      __func__, reg * 4);
        return;
    }

    switch (reg) {
    case R_PROT:
        if (data == PROT_KEY_UNLOCK)  {
            data = PROT_UNLOCKED;
        } else if (data == PROT_KEY_HARDLOCK) {
            data = PROT_HARDLOCKED;
        } else {
            data = PROT_SOFTLOCKED;
        }
        break;
    case R_CONF:
        data = aspeed_2600_sdmc_compute_conf(s, data);
        break;
    case R_STATUS1:
        /* Will never return 'busy'. 'lock status' is always set */
        data &= ~PHY_BUSY_STATE;
        data |= PHY_PLL_LOCK_STATUS;
        break;
    case R_ECC_TEST_CTRL:
        /* Always done, always happy */
        data |= ECC_TEST_FINISHED;
        data &= ~ECC_TEST_FAIL;
        break;
    default:
        break;
    }

    s->regs[reg] = data;
}

static const uint64_t
aspeed_2600_ram_sizes[] = { 256 * MiB, 512 * MiB, 1024 * MiB, 2048 * MiB, 0};

static void aspeed_2600_sdmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSDMCClass *asc = ASPEED_SDMC_CLASS(klass);

    dc->desc = "ASPEED 2600 SDRAM Memory Controller";
    asc->max_ram_size = 2 * GiB;
    asc->compute_conf = aspeed_2600_sdmc_compute_conf;
    asc->write = aspeed_2600_sdmc_write;
    asc->valid_ram_sizes = aspeed_2600_ram_sizes;
}

static const TypeInfo aspeed_2600_sdmc_info = {
    .name = TYPE_ASPEED_2600_SDMC,
    .parent = TYPE_ASPEED_SDMC,
    .class_init = aspeed_2600_sdmc_class_init,
};

static void aspeed_2700_sdmc_reset(DeviceState *dev)
{
    AspeedSDMCState *s = ASPEED_SDMC(dev);
    AspeedSDMCClass *asc = ASPEED_SDMC_GET_CLASS(s);

    memset(s->regs, 0, sizeof(s->regs));

    /* Set ram size bit and defaults values */
    s->regs[R_MAIN_CONF] = asc->compute_conf(s, 0);

    if (s->unlocked) {
        s->regs[R_2700_PROT] = PROT_UNLOCKED;
    }
}

static uint32_t aspeed_2700_sdmc_compute_conf(AspeedSDMCState *s, uint32_t data)
{
    uint32_t fixed_conf = ASPEED_SDMC_AST2700_PAGE_MATCHING_ENABLE |
        ASPEED_SDMC_AST2700_DRAM_SIZE(aspeed_sdmc_get_ram_bits(s));

    /* Make sure readonly bits are kept */
    data &= ~ASPEED_SDMC_AST2700_READONLY_MASK;

    return data | fixed_conf;
}

static void aspeed_2700_sdmc_write(AspeedSDMCState *s, uint32_t reg,
                                   uint32_t data)
{
    /* Unprotected registers */
    switch (reg) {
    case R_INT_STATUS:
    case R_INT_CLEAR:
    case R_INT_MASK:
    case R_ERR_STATUS:
    case R_ECC_FAIL_STATUS:
    case R_ECC_FAIL_ADDR:
    case R_PROT_REGION_LOCK_STATUS:
    case R_TEST_FAIL_ADDR:
    case R_TEST_FAIL_D0:
    case R_TEST_FAIL_D1:
    case R_TEST_FAIL_D2:
    case R_TEST_FAIL_D3:
    case R_DBG_STATUS:
    case R_PHY_INTERFACE_STATUS:
    case R_GRAPHIC_MEM_BASE_ADDR:
    case R_PORT0_INTERFACE_MONITOR0:
    case R_PORT0_INTERFACE_MONITOR1:
    case R_PORT0_INTERFACE_MONITOR2:
    case R_PORT1_INTERFACE_MONITOR0:
    case R_PORT1_INTERFACE_MONITOR1:
    case R_PORT1_INTERFACE_MONITOR2:
    case R_PORT2_INTERFACE_MONITOR0:
    case R_PORT2_INTERFACE_MONITOR1:
    case R_PORT2_INTERFACE_MONITOR2:
    case R_PORT3_INTERFACE_MONITOR0:
    case R_PORT3_INTERFACE_MONITOR1:
    case R_PORT3_INTERFACE_MONITOR2:
    case R_PORT4_INTERFACE_MONITOR0:
    case R_PORT4_INTERFACE_MONITOR1:
    case R_PORT4_INTERFACE_MONITOR2:
    case R_PORT5_INTERFACE_MONITOR0:
    case R_PORT5_INTERFACE_MONITOR1:
    case R_PORT5_INTERFACE_MONITOR2:
        s->regs[reg] = data;
        return;
    }

    if (s->regs[R_2700_PROT] == PROT_HARDLOCKED) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SDMC is locked until system reset!\n",
                      __func__);
        return;
    }

    if (reg != R_2700_PROT && s->regs[R_2700_PROT] == PROT_SOFTLOCKED) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SDMC is locked! (write to MCR%02x blocked)\n",
                      __func__, reg * 4);
        return;
    }

    switch (reg) {
    case R_2700_PROT:
        if (data == PROT_2700_KEY_UNLOCK)  {
            data = PROT_UNLOCKED;
        } else if (data == PROT_KEY_HARDLOCK) {
            data = PROT_HARDLOCKED;
        } else {
            data = PROT_SOFTLOCKED;
        }
        break;
    case R_MAIN_CONF:
        data = aspeed_2700_sdmc_compute_conf(s, data);
        break;
    case R_MAIN_STATUS:
        /* Will never return 'busy'. */
        data &= ~PHY_BUSY_STATE;
        break;
    default:
        break;
    }

    s->regs[reg] = data;
}

static const uint64_t
    aspeed_2700_ram_sizes[] = { 256 * MiB, 512 * MiB, 1024 * MiB,
                                2048 * MiB, 4096 * MiB, 8192 * MiB, 0};

static void aspeed_2700_sdmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSDMCClass *asc = ASPEED_SDMC_CLASS(klass);

    dc->desc = "ASPEED 2700 SDRAM Memory Controller";
    device_class_set_legacy_reset(dc, aspeed_2700_sdmc_reset);

    asc->is_bus64bit = true;
    asc->max_ram_size = 8 * GiB;
    asc->compute_conf = aspeed_2700_sdmc_compute_conf;
    asc->write = aspeed_2700_sdmc_write;
    asc->valid_ram_sizes = aspeed_2700_ram_sizes;
}

static const TypeInfo aspeed_2700_sdmc_info = {
    .name = TYPE_ASPEED_2700_SDMC,
    .parent = TYPE_ASPEED_SDMC,
    .class_init = aspeed_2700_sdmc_class_init,
};

static void aspeed_sdmc_register_types(void)
{
    type_register_static(&aspeed_sdmc_info);
    type_register_static(&aspeed_2400_sdmc_info);
    type_register_static(&aspeed_2500_sdmc_info);
    type_register_static(&aspeed_2600_sdmc_info);
    type_register_static(&aspeed_2700_sdmc_info);
}

type_init(aspeed_sdmc_register_types);
