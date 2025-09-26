/*
 * AMD/Xilinx Versal family SoC model.
 *
 * Copyright (c) 2018 Xilinx Inc.
 * Copyright (c) 2025 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qobject/qlist.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "system/system.h"
#include "hw/misc/unimp.h"
#include "hw/arm/xlnx-versal.h"
#include "qemu/log.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"
#include "system/device_tree.h"
#include "hw/arm/fdt.h"
#include "hw/char/pl011.h"
#include "hw/net/xlnx-versal-canfd.h"
#include "hw/sd/sdhci.h"
#include "hw/net/cadence_gem.h"
#include "hw/dma/xlnx-zdma.h"
#include "hw/misc/xlnx-versal-xramc.h"
#include "hw/usb/xlnx-usb-subsystem.h"
#include "hw/nvram/xlnx-versal-efuse.h"
#include "hw/ssi/xlnx-versal-ospi.h"
#include "hw/misc/xlnx-versal-pmc-iou-slcr.h"
#include "hw/nvram/xlnx-bbram.h"
#include "hw/misc/xlnx-versal-trng.h"
#include "hw/rtc/xlnx-zynqmp-rtc.h"
#include "hw/misc/xlnx-versal-cfu.h"
#include "hw/misc/xlnx-versal-cframe-reg.h"
#include "hw/or-irq.h"
#include "hw/misc/xlnx-versal-crl.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "hw/intc/arm_gic.h"
#include "hw/core/split-irq.h"
#include "target/arm/cpu.h"
#include "hw/cpu/cluster.h"
#include "hw/arm/bsa.h"

/*
 * IRQ descriptor to catch the following cases:
 *   - An IRQ can either connect to the GICs, to the PPU1 intc, or the the EAM
 *   - Multiple devices can connect to the same IRQ. They are OR'ed together.
 */
FIELD(VERSAL_IRQ, IRQ, 0, 16)
FIELD(VERSAL_IRQ, TARGET, 16, 2)
FIELD(VERSAL_IRQ, ORED, 18, 1)
FIELD(VERSAL_IRQ, OR_IDX, 19, 4) /* input index on the IRQ OR gate */

typedef enum VersalIrqTarget {
    IRQ_TARGET_GIC,
    IRQ_TARGET_PPU1,
    IRQ_TARGET_EAM,
} VersalIrqTarget;

#define PPU1_IRQ(irq) ((IRQ_TARGET_PPU1 << R_VERSAL_IRQ_TARGET_SHIFT) | (irq))
#define EAM_IRQ(irq) ((IRQ_TARGET_EAM << R_VERSAL_IRQ_TARGET_SHIFT) | (irq))
#define OR_IRQ(irq, or_idx) \
    (R_VERSAL_IRQ_ORED_MASK | ((or_idx) << R_VERSAL_IRQ_OR_IDX_SHIFT) | (irq))
#define PPU1_OR_IRQ(irq, or_idx) \
    ((IRQ_TARGET_PPU1 << R_VERSAL_IRQ_TARGET_SHIFT) | OR_IRQ(irq, or_idx))

typedef struct VersalSimplePeriphMap {
    uint64_t addr;
    int irq;
} VersalSimplePeriphMap;

typedef struct VersalMemMap {
    uint64_t addr;
    uint64_t size;
} VersalMemMap;

typedef struct VersalGicMap {
    int version;
    uint64_t dist;
    uint64_t redist;
    uint64_t cpu_iface;
    uint64_t its;
    size_t num_irq;
    bool has_its;
} VersalGicMap;

enum StartPoweredOffMode {
    SPO_SECONDARIES,
    SPO_ALL,
};

typedef struct VersalCpuClusterMap {
    VersalGicMap gic;
    /*
     * true: one GIC per cluster.
     * false: one GIC for all CPUs
     */
    bool per_cluster_gic;

    const char *name;
    const char *cpu_model;
    size_t num_core;
    size_t num_cluster;
    uint32_t qemu_cluster_id;
    bool dtb_expose;

    struct {
        uint64_t base;
        uint64_t core_shift;
        uint64_t cluster_shift;
    } mp_affinity;

    enum StartPoweredOffMode start_powered_off;
} VersalCpuClusterMap;

typedef struct VersalMap {
    VersalMemMap ocm;

    struct VersalDDRMap {
        VersalMemMap chan[4];
        size_t num_chan;
    } ddr;

    VersalCpuClusterMap apu;
    VersalCpuClusterMap rpu;

    VersalSimplePeriphMap uart[2];
    size_t num_uart;

    VersalSimplePeriphMap canfd[4];
    size_t num_canfd;

    VersalSimplePeriphMap sdhci[2];
    size_t num_sdhci;

    struct VersalGemMap {
        VersalSimplePeriphMap map;
        size_t num_prio_queue;
        const char *phy_mode;
        const uint32_t speed;
    } gem[3];
    size_t num_gem;

    struct VersalZDMAMap {
        const char *name;
        VersalSimplePeriphMap map;
        size_t num_chan;
        uint64_t chan_stride;
        int irq_stride;
    } zdma[2];
    size_t num_zdma;

    struct VersalXramMap {
        uint64_t mem;
        uint64_t mem_stride;
        uint64_t ctrl;
        uint64_t ctrl_stride;
        int irq;
        size_t num;
    } xram;

    struct VersalUsbMap {
        uint64_t xhci;
        uint64_t ctrl;
        int irq;
    } usb[2];
    size_t num_usb;

    struct VersalEfuseMap {
        uint64_t ctrl;
        uint64_t cache;
        int irq;
    } efuse;

    struct VersalOspiMap {
        uint64_t ctrl;
        uint64_t dac;
        uint64_t dac_sz;
        uint64_t dma_src;
        uint64_t dma_dst;
        int irq;
    } ospi;

    VersalSimplePeriphMap pmc_iou_slcr;
    VersalSimplePeriphMap bbram;
    VersalSimplePeriphMap trng;

    struct VersalRtcMap {
        VersalSimplePeriphMap map;
        int alarm_irq;
        int second_irq;
    } rtc;

    struct VersalCfuMap {
        uint64_t cframe_base;
        uint64_t cframe_stride;
        uint64_t cfu_fdro;
        uint64_t cframe_bcast_reg;
        uint64_t cframe_bcast_fdri;
        uint64_t cfu_apb;
        uint64_t cfu_stream;
        uint64_t cfu_stream_2;
        uint64_t cfu_sfr;
        int cfu_apb_irq;
        int cframe_irq;
        size_t num_cframe;
        struct VersalCfuCframeCfg {
            uint32_t blktype_frames[7];
        } cframe_cfg[15];
    } cfu;

    VersalSimplePeriphMap crl;

    /* reserved MMIO/IRQ space that can safely be used for virtio devices */
    struct VersalReserved {
        uint64_t mmio_start;
        int irq_start;
        int irq_num;
    } reserved;
} VersalMap;

static const VersalMap VERSAL_MAP = {
    .ocm = {
        .addr = 0xfffc0000,
        .size = 0x40000,
    },

    .ddr = {
        .chan[0] = { .addr = 0x0, .size = 2 * GiB },
        .chan[1] = { .addr = 0x800000000ull, .size = 32 * GiB },
        .chan[2] = { .addr = 0xc00000000ull, .size = 256 * GiB },
        .chan[3] = { .addr = 0x10000000000ull, .size = 734 * GiB },
        .num_chan = 4,
    },

    .apu = {
        .name = "apu",
        .cpu_model = ARM_CPU_TYPE_NAME("cortex-a72"),
        .num_cluster = 1,
        .num_core = 2,
        .qemu_cluster_id = 0,
        .mp_affinity = {
            .core_shift = ARM_AFF0_SHIFT,
            .cluster_shift = ARM_AFF1_SHIFT,
        },
        .start_powered_off = SPO_SECONDARIES,
        .dtb_expose = true,
        .gic = {
            .version = 3,
            .dist = 0xf9000000,
            .redist = 0xf9080000,
            .num_irq = 192,
            .has_its = true,
            .its = 0xf9020000,
        },
    },

    .rpu = {
        .name = "rpu",
        .cpu_model = ARM_CPU_TYPE_NAME("cortex-r5f"),
        .num_cluster = 1,
        .num_core = 2,
        .qemu_cluster_id = 1,
        .mp_affinity = {
            .base = 0x100,
            .core_shift = ARM_AFF0_SHIFT,
            .cluster_shift = ARM_AFF1_SHIFT,
        },
        .start_powered_off = SPO_ALL,
        .dtb_expose = false,
        .gic = {
            .version = 2,
            .dist = 0xf9000000,
            .cpu_iface = 0xf9001000,
            .num_irq = 192,
        },
    },

    .uart[0] = { 0xff000000, 18 },
    .uart[1] = { 0xff010000, 19 },
    .num_uart = 2,

    .canfd[0] = { 0xff060000, 20 },
    .canfd[1] = { 0xff070000, 21 },
    .num_canfd = 2,

    .sdhci[0] = { 0xf1040000, 126 },
    .sdhci[1] = { 0xf1050000, 128 },
    .num_sdhci = 2,

    .gem[0] = { { 0xff0c0000, 56 }, 2, "rgmii-id", 1000 },
    .gem[1] = { { 0xff0d0000, 58 }, 2, "rgmii-id", 1000 },
    .num_gem = 2,

    .zdma[0] = { "adma", { 0xffa80000, 60 }, 8, 0x10000, 1 },
    .num_zdma = 1,

    .xram = {
        .num = 4,
        .mem = 0xfe800000, .mem_stride = 1 * MiB,
        .ctrl = 0xff8e0000, .ctrl_stride = 0x10000,
        .irq = 79,
    },

    .usb[0] = { .xhci = 0xfe200000, .ctrl = 0xff9d0000, .irq = 22 },
    .num_usb = 1,

    .efuse = { .ctrl = 0xf1240000, .cache = 0xf1250000, .irq = 139 },

    .ospi = {
        .ctrl = 0xf1010000,
        .dac = 0xc0000000, .dac_sz = 0x20000000,
        .dma_src = 0xf1011000, .dma_dst = 0xf1011800,
        .irq = 124,
    },

    .pmc_iou_slcr = { 0xf1060000, OR_IRQ(121, 0) },
    .bbram = { 0xf11f0000, OR_IRQ(121, 1) },
    .trng = { 0xf1230000, 141 },
    .rtc = {
        { 0xf12a0000, OR_IRQ(121, 2) },
        .alarm_irq = 142, .second_irq = 143
    },

    .cfu = {
        .cframe_base = 0xf12d0000, .cframe_stride = 0x1000,
        .cframe_bcast_reg = 0xf12ee000, .cframe_bcast_fdri = 0xf12ef000,
        .cfu_apb = 0xf12b0000, .cfu_sfr = 0xf12c1000,
        .cfu_stream = 0xf12c0000, .cfu_stream_2 = 0xf1f80000,
        .cfu_fdro = 0xf12c2000,
        .cfu_apb_irq = 120, .cframe_irq = OR_IRQ(121, 3),
        .num_cframe = 15,
        .cframe_cfg = {
            { { 34111, 3528, 12800, 11, 5, 1, 1 } },
            { { 38498, 3841, 15361, 13, 7, 3, 1 } },
            { { 38498, 3841, 15361, 13, 7, 3, 1 } },
            { { 38498, 3841, 15361, 13, 7, 3, 1 } },
        },
    },

    .crl = { 0xff5e0000, 10 },

    .reserved = { 0xa0000000, 111, 8 },
};

static const VersalMap VERSAL2_MAP = {
    .ocm = {
        .addr = 0xbbe00000,
        .size = 2 * MiB,
    },

    .ddr = {
        .chan[0] = { .addr = 0x0, .size = 2046 * MiB },
        .chan[1] = { .addr = 0x800000000ull, .size = 32 * GiB },
        .chan[2] = { .addr = 0xc00000000ull, .size = 256 * GiB },
        .chan[3] = { .addr = 0x10000000000ull, .size = 734 * GiB },
        .num_chan = 4,
    },

    .apu = {
        .name = "apu",
        .cpu_model = ARM_CPU_TYPE_NAME("cortex-a78ae"),
        .num_cluster = 4,
        .num_core = 2,
        .qemu_cluster_id = 0,
        .mp_affinity = {
            .base = 0x0, /* TODO: the MT bit should be set */
            .core_shift = ARM_AFF1_SHIFT,
            .cluster_shift = ARM_AFF2_SHIFT,
        },
        .start_powered_off = SPO_SECONDARIES,
        .dtb_expose = true,
        .gic = {
            .version = 3,
            .dist = 0xe2000000,
            .redist = 0xe2060000,
            .num_irq = 544,
            .has_its = true,
            .its = 0xe2040000,
        },
    },

    .rpu = {
        .name = "rpu",
        .cpu_model = ARM_CPU_TYPE_NAME("cortex-r52"),
        .num_cluster = 5,
        .num_core = 2,
        .qemu_cluster_id = 1,
        .mp_affinity = {
            .core_shift = ARM_AFF0_SHIFT,
            .cluster_shift = ARM_AFF1_SHIFT,
        },
        .start_powered_off = SPO_ALL,
        .dtb_expose = false,
        .per_cluster_gic = true,
        .gic = {
            .version = 3,
            .dist = 0x0,
            .redist = 0x100000,
            .num_irq = 288,
        },
    },

    .uart[0] = { 0xf1920000, 25 },
    .uart[1] = { 0xf1930000, 26 },
    .num_uart = 2,

    .canfd[0] = { 0xf19e0000, 27 },
    .canfd[1] = { 0xf19f0000, 28 },
    .canfd[2] = { 0xf1a00000, 95 },
    .canfd[3] = { 0xf1a10000, 96 },
    .num_canfd = 4,

    .gem[0] = { { 0xf1a60000, 39 }, 2, "rgmii-id", 1000 },
    .gem[1] = { { 0xf1a70000, 41 }, 2, "rgmii-id", 1000 },
    .gem[2] = { { 0xed920000, 164 }, 4, "usxgmii", 10000 }, /* MMI 10Gb GEM */
    .num_gem = 3,

    .zdma[0] = { "adma", { 0xebd00000, 72 }, 8, 0x10000, 1 },
    .zdma[1] = { "sdma", { 0xebd80000, 112 }, 8, 0x10000, 1 },
    .num_zdma = 2,

    .usb[0] = { .xhci = 0xf1b00000, .ctrl = 0xf1ee0000, .irq = 29 },
    .usb[1] = { .xhci = 0xf1c00000, .ctrl = 0xf1ef0000, .irq = 34 },
    .num_usb = 2,

    .efuse = { .ctrl = 0xf1240000, .cache = 0xf1250000, .irq = 230 },

    .ospi = {
        .ctrl = 0xf1010000,
        .dac = 0xc0000000, .dac_sz = 0x20000000,
        .dma_src = 0xf1011000, .dma_dst = 0xf1011800,
        .irq = 216,
    },

    .sdhci[0] = { 0xf1040000, 218 },
    .sdhci[1] = { 0xf1050000, 220 }, /* eMMC */
    .num_sdhci = 2,

    .pmc_iou_slcr = { 0xf1060000, 222 },
    .bbram = { 0xf11f0000, PPU1_OR_IRQ(18, 0) },
    .crl = { 0xeb5e0000 },
    .trng = { 0xf1230000, 233 },
    .rtc = {
        { 0xf12a0000, PPU1_OR_IRQ(18, 1) },
        .alarm_irq = 200, .second_irq = 201
    },

    .cfu = {
        .cframe_base = 0xf12d0000, .cframe_stride = 0x1000,
        .cframe_bcast_reg = 0xf12ee000, .cframe_bcast_fdri = 0xf12ef000,
        .cfu_apb = 0xf12b0000, .cfu_sfr = 0xf12c1000,
        .cfu_stream = 0xf12c0000, .cfu_stream_2 = 0xf1f80000,
        .cfu_fdro = 0xf12c2000,
        .cfu_apb_irq = 235, .cframe_irq = EAM_IRQ(7),
    },

    .reserved = { 0xf5e00000, 270, 8 },
};

static const VersalMap *VERSION_TO_MAP[] = {
    [VERSAL_VER_VERSAL] = &VERSAL_MAP,
    [VERSAL_VER_VERSAL2] = &VERSAL2_MAP,
};

static inline VersalVersion versal_get_version(Versal *s)
{
    return XLNX_VERSAL_BASE_GET_CLASS(s)->version;
}

static inline const VersalMap *versal_get_map(Versal *s)
{
    return VERSION_TO_MAP[versal_get_version(s)];
}

static inline Object *versal_get_child(Versal *s, const char *child)
{
    return object_resolve_path_at(OBJECT(s), child);
}

static inline Object *versal_get_child_idx(Versal *s, const char *child,
                                           size_t idx)
{
    g_autofree char *n = g_strdup_printf("%s[%zu]", child, idx);

    return versal_get_child(s, n);
}

/*
 * The SoC embeds multiple GICs. They all receives the same IRQ lines at the
 * same index. This function creates a TYPE_SPLIT_IRQ device to fan out the
 * given IRQ input to all the GICs.
 *
 * The TYPE_SPLIT_IRQ devices lie in the /soc/irq-splits QOM container
 */
static qemu_irq versal_get_gic_irq(Versal *s, int irq_idx)
{
    DeviceState *split;
    Object *container = versal_get_child(s, "irq-splits");
    int idx = FIELD_EX32(irq_idx, VERSAL_IRQ, IRQ);
    g_autofree char *name = g_strdup_printf("irq[%d]", idx);

    split = DEVICE(object_resolve_path_at(container, name));

    if (split == NULL) {
        size_t i;

        split = qdev_new(TYPE_SPLIT_IRQ);
        qdev_prop_set_uint16(split, "num-lines", s->intc->len);
        object_property_add_child(container, name, OBJECT(split));
        qdev_realize_and_unref(split, NULL, &error_abort);

        for (i = 0; i < s->intc->len; i++) {
            DeviceState *gic;

            gic = g_array_index(s->intc, DeviceState *, i);
            qdev_connect_gpio_out(split, i, qdev_get_gpio_in(gic, idx));
        }
    } else {
        g_assert(FIELD_EX32(irq_idx, VERSAL_IRQ, ORED));
    }

    return qdev_get_gpio_in(split, 0);
}

/*
 * When the R_VERSAL_IRQ_ORED flag is set on an IRQ descriptor, this function is
 * used to return the corresponding or gate input IRQ. The or gate is created if
 * not already existant.
 *
 * Or gates are placed under the /soc/irq-or-gates QOM container.
 */
static qemu_irq versal_get_irq_or_gate_in(Versal *s, int irq_idx,
                                          qemu_irq target_irq)
{
    static const char *TARGET_STR[] = {
        [IRQ_TARGET_GIC] = "gic",
        [IRQ_TARGET_PPU1] = "ppu1",
        [IRQ_TARGET_EAM] = "eam",
    };

    VersalIrqTarget target;
    Object *container = versal_get_child(s, "irq-or-gates");
    DeviceState *dev;
    g_autofree char *name;
    int idx, or_idx;

    idx = FIELD_EX32(irq_idx, VERSAL_IRQ, IRQ);
    or_idx = FIELD_EX32(irq_idx, VERSAL_IRQ, OR_IDX);
    target = FIELD_EX32(irq_idx, VERSAL_IRQ, TARGET);

    name = g_strdup_printf("%s-irq[%d]", TARGET_STR[target], idx);
    dev = DEVICE(object_resolve_path_at(container, name));

    if (dev == NULL) {
        dev = qdev_new(TYPE_OR_IRQ);
        object_property_add_child(container, name, OBJECT(dev));
        qdev_prop_set_uint16(dev, "num-lines", 1 << R_VERSAL_IRQ_OR_IDX_LENGTH);
        qdev_realize_and_unref(dev, NULL, &error_abort);
        qdev_connect_gpio_out(dev, 0, target_irq);
    }

    return qdev_get_gpio_in(dev, or_idx);
}

static qemu_irq versal_get_irq(Versal *s, int irq_idx)
{
    VersalIrqTarget target;
    qemu_irq irq;
    bool ored;

    target = FIELD_EX32(irq_idx, VERSAL_IRQ, TARGET);
    ored = FIELD_EX32(irq_idx, VERSAL_IRQ, ORED);

    switch (target) {
    case IRQ_TARGET_EAM:
        /* EAM not implemented */
        return NULL;

    case IRQ_TARGET_PPU1:
        /* PPU1 CPU not implemented */
        return NULL;

    case IRQ_TARGET_GIC:
        irq = versal_get_gic_irq(s, irq_idx);
        break;

    default:
        g_assert_not_reached();
    }

    if (ored) {
        irq = versal_get_irq_or_gate_in(s, irq_idx, irq);
    }

    return irq;
}

static void versal_sysbus_connect_irq(Versal *s, SysBusDevice *sbd,
                                      int sbd_idx, int irq_idx)
{
    qemu_irq irq = versal_get_irq(s, irq_idx);

    if (irq == NULL) {
        return;
    }

    sysbus_connect_irq(sbd, sbd_idx, irq);
}

static void versal_qdev_connect_gpio_out(Versal *s, DeviceState *dev,
                                         int dev_idx, int irq_idx)
{
    qemu_irq irq = versal_get_irq(s, irq_idx);

    if (irq == NULL) {
        return;
    }

    qdev_connect_gpio_out(dev, dev_idx, irq);
}

static inline char *versal_fdt_add_subnode(Versal *s, const char *path,
                                           uint64_t at, const char *compat,
                                           size_t compat_sz)
{
    char *p;

    p = g_strdup_printf("%s@%" PRIx64, path, at);
    qemu_fdt_add_subnode(s->cfg.fdt, p);

    if (!strncmp(compat, "memory", compat_sz)) {
        qemu_fdt_setprop(s->cfg.fdt, p, "device_type", compat, compat_sz);
    } else {
        qemu_fdt_setprop(s->cfg.fdt, p, "compatible", compat, compat_sz);
    }

    return p;
}

static inline char *versal_fdt_add_simple_subnode(Versal *s, const char *path,
                                                  uint64_t addr, uint64_t len,
                                                  const char *compat,
                                                  size_t compat_sz)
{
    char *p = versal_fdt_add_subnode(s, path, addr, compat, compat_sz);

    qemu_fdt_setprop_sized_cells(s->cfg.fdt, p, "reg", 2, addr, 2, len);
    return p;
}

static inline DeviceState *create_or_gate(Versal *s, Object *parent,
                                          const char *name, uint16_t num_lines,
                                          int irq_idx)
{
    DeviceState *or;

    or = qdev_new(TYPE_OR_IRQ);
    qdev_prop_set_uint16(or, "num-lines", num_lines);
    object_property_add_child(parent, name, OBJECT(or));
    qdev_realize_and_unref(or, NULL, &error_abort);
    versal_qdev_connect_gpio_out(s, or, 0, irq_idx);

    return or;
}

static MemoryRegion *create_cpu_mr(Versal *s, DeviceState *cluster,
                                   const VersalCpuClusterMap *map)
{
    MemoryRegion *mr, *root_alias;
    char *name;

    mr = g_new(MemoryRegion, 1);
    name = g_strdup_printf("%s-mr", map->name);
    memory_region_init(mr, OBJECT(cluster), name, UINT64_MAX);
    g_free(name);

    root_alias = g_new(MemoryRegion, 1);
    name = g_strdup_printf("ps-alias-for-%s", map->name);
    memory_region_init_alias(root_alias, OBJECT(cluster), name,
                             &s->mr_ps, 0, UINT64_MAX);
    g_free(name);
    memory_region_add_subregion(mr, 0, root_alias);

    return mr;
}

static void versal_create_gic_its(Versal *s,
                                  const VersalCpuClusterMap *map,
                                  DeviceState *gic,
                                  MemoryRegion *mr,
                                  char *gic_node)
{
    DeviceState *dev;
    SysBusDevice *sbd;
    g_autofree char *node_pat = NULL, *node = NULL;
    const char compatible[] = "arm,gic-v3-its";

    if (map->gic.version != 3) {
        return;
    }

    if (!map->gic.has_its) {
        return;
    }

    dev = qdev_new(TYPE_ARM_GICV3_ITS);
    sbd = SYS_BUS_DEVICE(dev);

    object_property_add_child(OBJECT(gic), "its", OBJECT(dev));
    object_property_set_link(OBJECT(dev), "parent-gicv3", OBJECT(gic),
                             &error_abort);

    sysbus_realize_and_unref(sbd, &error_abort);

    memory_region_add_subregion(mr, map->gic.its,
                                sysbus_mmio_get_region(sbd, 0));

    if (!map->dtb_expose) {
        return;
    }

    qemu_fdt_setprop(s->cfg.fdt, gic_node, "ranges", NULL, 0);
    qemu_fdt_setprop_cell(s->cfg.fdt, gic_node, "#address-cells", 2);
    qemu_fdt_setprop_cell(s->cfg.fdt, gic_node, "#size-cells", 2);

    node_pat = g_strdup_printf("%s/its", gic_node);
    node = versal_fdt_add_simple_subnode(s, node_pat, map->gic.its, 0x20000,
                                         compatible, sizeof(compatible));
    qemu_fdt_setprop(s->cfg.fdt, node, "msi-controller", NULL, 0);
    qemu_fdt_setprop_cell(s->cfg.fdt, node, "#msi-cells", 1);
}

static DeviceState *versal_create_gic(Versal *s,
                                      const VersalCpuClusterMap *map,
                                      MemoryRegion *mr,
                                      int first_cpu_idx,
                                      size_t num_cpu)
{
    DeviceState *dev;
    SysBusDevice *sbd;
    g_autofree char *node = NULL;
    g_autofree char *name = NULL;
    const char gicv3_compat[] = "arm,gic-v3";
    const char gicv2_compat[] = "arm,cortex-a15-gic";

    switch (map->gic.version) {
    case 2:
        dev = qdev_new(gic_class_name());
        break;

    case 3:
        dev = qdev_new(gicv3_class_name());
        break;

    default:
        g_assert_not_reached();
    }

    name = g_strdup_printf("%s-gic[*]", map->name);
    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    sbd = SYS_BUS_DEVICE(dev);
    qdev_prop_set_uint32(dev, "revision", map->gic.version);
    qdev_prop_set_uint32(dev, "num-cpu", num_cpu);
    qdev_prop_set_uint32(dev, "num-irq", map->gic.num_irq + 32);
    qdev_prop_set_bit(dev, "has-security-extensions", true);
    qdev_prop_set_uint32(dev, "first-cpu-index", first_cpu_idx);

    if (map->gic.version == 3) {
        QList *redist_region_count;

        redist_region_count = qlist_new();
        qlist_append_int(redist_region_count, num_cpu);
        qdev_prop_set_array(dev, "redist-region-count", redist_region_count);
        qdev_prop_set_bit(dev, "has-lpi", map->gic.has_its);
        object_property_set_link(OBJECT(dev), "sysmem", OBJECT(mr),
                                 &error_abort);

    }

    sysbus_realize_and_unref(sbd, &error_fatal);

    memory_region_add_subregion(mr, map->gic.dist,
                                sysbus_mmio_get_region(sbd, 0));

    if (map->gic.version == 3) {
        memory_region_add_subregion(mr, map->gic.redist,
                                    sysbus_mmio_get_region(sbd, 1));
    } else {
        memory_region_add_subregion(mr, map->gic.cpu_iface,
                                    sysbus_mmio_get_region(sbd, 1));
    }

    if (map->dtb_expose) {
        if (map->gic.version == 3) {
            node = versal_fdt_add_subnode(s, "/gic", map->gic.dist,
                                          gicv3_compat,
                                          sizeof(gicv3_compat));
            qemu_fdt_setprop_sized_cells(s->cfg.fdt, node, "reg",
                                         2, map->gic.dist,
                                         2, 0x10000,
                                         2, map->gic.redist,
                                         2, GICV3_REDIST_SIZE * num_cpu);
        } else {
            node = versal_fdt_add_subnode(s, "/gic", map->gic.dist,
                                          gicv2_compat,
                                          sizeof(gicv2_compat));
            qemu_fdt_setprop_sized_cells(s->cfg.fdt, node, "reg",
                                         2, map->gic.dist,
                                         2, 0x1000,
                                         2, map->gic.cpu_iface,
                                         2, 0x1000);
        }

        qemu_fdt_setprop_cell(s->cfg.fdt, node, "phandle", s->phandle.gic);
        qemu_fdt_setprop_cell(s->cfg.fdt, node, "#interrupt-cells", 3);
        qemu_fdt_setprop_cells(s->cfg.fdt, node, "interrupts",
                               GIC_FDT_IRQ_TYPE_PPI,
                               INTID_TO_PPI(ARCH_GIC_MAINT_IRQ),
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
        qemu_fdt_setprop(s->cfg.fdt, node, "interrupt-controller", NULL, 0);
    }

    versal_create_gic_its(s, map, dev, mr, node);

    g_array_append_val(s->intc, dev);

    return dev;
}

static void connect_gic_to_cpu(const VersalCpuClusterMap *map,
                               DeviceState *gic, DeviceState *cpu, size_t idx,
                               size_t num_cpu)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(gic);
    int ppibase = map->gic.num_irq + idx * GIC_INTERNAL + GIC_NR_SGIS;
    int ti;
    bool has_gtimer;
    /*
     * Mapping from the output timer irq lines from the CPU to the
     * GIC PPI inputs.
     */
    const int timer_irq[] = {
        [GTIMER_PHYS] = INTID_TO_PPI(ARCH_TIMER_NS_EL1_IRQ),
        [GTIMER_VIRT] = INTID_TO_PPI(ARCH_TIMER_VIRT_IRQ),
        [GTIMER_HYP]  = INTID_TO_PPI(ARCH_TIMER_NS_EL2_IRQ),
        [GTIMER_SEC]  = INTID_TO_PPI(ARCH_TIMER_S_EL1_IRQ),
    };

    has_gtimer = arm_feature(&ARM_CPU(cpu)->env, ARM_FEATURE_GENERIC_TIMER);

    if (has_gtimer) {
        for (ti = 0; ti < ARRAY_SIZE(timer_irq); ti++) {
            qdev_connect_gpio_out(cpu, ti,
                                  qdev_get_gpio_in(gic,
                                                   ppibase + timer_irq[ti]));
        }
    }

    if (map->gic.version == 3) {
        qemu_irq maint_irq;
        int maint_idx = ppibase + INTID_TO_PPI(ARCH_GIC_MAINT_IRQ);

        maint_irq = qdev_get_gpio_in(gic, maint_idx);
        qdev_connect_gpio_out_named(cpu, "gicv3-maintenance-interrupt",
                                    0, maint_irq);
    }

    sysbus_connect_irq(sbd, idx, qdev_get_gpio_in(cpu, ARM_CPU_IRQ));
    sysbus_connect_irq(sbd, idx + num_cpu,
                       qdev_get_gpio_in(cpu, ARM_CPU_FIQ));
    sysbus_connect_irq(sbd, idx + 2 * num_cpu,
                       qdev_get_gpio_in(cpu, ARM_CPU_VIRQ));
    sysbus_connect_irq(sbd, idx + 3 * num_cpu,
                       qdev_get_gpio_in(cpu, ARM_CPU_VFIQ));
}

static inline void versal_create_and_connect_gic(Versal *s,
                                                 const VersalCpuClusterMap *map,
                                                 MemoryRegion *mr,
                                                 DeviceState **cpus,
                                                 size_t num_cpu)
{
    DeviceState *gic;
    int first_cpu_idx;
    size_t i;

    first_cpu_idx = CPU(cpus[0])->cpu_index;
    gic = versal_create_gic(s, map, mr, first_cpu_idx, num_cpu);

    for (i = 0; i < num_cpu; i++) {
        connect_gic_to_cpu(map, gic, cpus[i], i, num_cpu);
    }
}

static DeviceState *versal_create_cpu(Versal *s,
                                      const VersalCpuClusterMap *map,
                                      DeviceState *qemu_cluster,
                                      MemoryRegion *cpu_mr,
                                      size_t cluster_idx,
                                      size_t core_idx)
{
    DeviceState *cpu = qdev_new(map->cpu_model);
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    Object *obj = OBJECT(cpu);
    uint64_t affinity;
    bool start_off;
    size_t idx = cluster_idx * map->num_core + core_idx;
    g_autofree char *name;
    g_autofree char *node = NULL;

    affinity = map->mp_affinity.base;
    affinity |= (cluster_idx & 0xff) << map->mp_affinity.cluster_shift;
    affinity |= (core_idx & 0xff) << map->mp_affinity.core_shift;

    start_off = map->start_powered_off == SPO_ALL
        || ((map->start_powered_off == SPO_SECONDARIES)
            && (cluster_idx || core_idx));

    name = g_strdup_printf("%s[*]", map->name);
    object_property_add_child(OBJECT(qemu_cluster), name, obj);
    object_property_set_bool(obj, "start-powered-off", start_off,
                             &error_abort);
    qdev_prop_set_uint64(cpu, "mp-affinity", affinity);
    qdev_prop_set_int32(cpu, "core-count",  map->num_core);
    object_property_set_link(obj, "memory", OBJECT(cpu_mr), &error_abort);
    qdev_realize_and_unref(cpu, NULL, &error_fatal);

    if (!map->dtb_expose) {
        return cpu;
    }

    node = versal_fdt_add_subnode(s, "/cpus/cpu", idx,
                                  arm_cpu->dtb_compatible,
                                  strlen(arm_cpu->dtb_compatible) + 1);
    qemu_fdt_setprop_cell(s->cfg.fdt, node, "reg",
                          arm_cpu_mp_affinity(arm_cpu) & ARM64_AFFINITY_MASK);
    qemu_fdt_setprop_string(s->cfg.fdt, node, "device_type", "cpu");
    qemu_fdt_setprop_string(s->cfg.fdt, node, "enable-method", "psci");

    return cpu;
}

static void versal_create_cpu_cluster(Versal *s, const VersalCpuClusterMap *map)
{
    size_t i, j;
    DeviceState *cluster;
    MemoryRegion *mr;
    char *name;
    g_autofree DeviceState **cpus;
    const char compatible[] = "arm,armv8-timer";
    bool has_gtimer;

    cluster = qdev_new(TYPE_CPU_CLUSTER);
    name = g_strdup_printf("%s-cluster", map->name);
    object_property_add_child(OBJECT(s), name, OBJECT(cluster));
    g_free(name);
    qdev_prop_set_uint32(cluster, "cluster-id", map->qemu_cluster_id);

    mr = create_cpu_mr(s, cluster, map);

    cpus = g_new(DeviceState *, map->num_cluster * map->num_core);

    if (map->dtb_expose) {
        qemu_fdt_add_subnode(s->cfg.fdt, "/cpus");
        qemu_fdt_setprop_cell(s->cfg.fdt, "/cpus", "#size-cells", 0);
        qemu_fdt_setprop_cell(s->cfg.fdt, "/cpus", "#address-cells", 1);
    }

    for (i = 0; i < map->num_cluster; i++) {
        for (j = 0; j < map->num_core; j++) {
            DeviceState *cpu = versal_create_cpu(s, map, cluster, mr, i, j);

            cpus[i * map->num_core + j] = cpu;
        }

        if (map->per_cluster_gic) {
            versal_create_and_connect_gic(s, map, mr, &cpus[i * map->num_core],
                                          map->num_core);
        }
    }

    qdev_realize_and_unref(cluster, NULL, &error_fatal);

    if (!map->per_cluster_gic) {
        versal_create_and_connect_gic(s, map, mr, cpus,
                                      map->num_cluster * map->num_core);
    }

    has_gtimer = arm_feature(&ARM_CPU(cpus[0])->env, ARM_FEATURE_GENERIC_TIMER);
    if (map->dtb_expose && has_gtimer) {
        qemu_fdt_add_subnode(s->cfg.fdt, "/timer");
        qemu_fdt_setprop_cells(s->cfg.fdt, "/timer", "interrupts",
                               GIC_FDT_IRQ_TYPE_PPI,
                               INTID_TO_PPI(ARCH_TIMER_S_EL1_IRQ),
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI,
                               GIC_FDT_IRQ_TYPE_PPI,
                               INTID_TO_PPI(ARCH_TIMER_NS_EL1_IRQ),
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI,
                               GIC_FDT_IRQ_TYPE_PPI,
                               INTID_TO_PPI(ARCH_TIMER_VIRT_IRQ),
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI,
                               GIC_FDT_IRQ_TYPE_PPI,
                               INTID_TO_PPI(ARCH_TIMER_NS_EL2_IRQ),
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
        qemu_fdt_setprop(s->cfg.fdt, "/timer", "compatible",
                         compatible, sizeof(compatible));
    }
}

static void versal_create_uart(Versal *s,
                               const VersalSimplePeriphMap *map,
                               int chardev_idx)
{
    DeviceState *dev;
    MemoryRegion *mr;
    g_autofree char *node;
    g_autofree char *alias;
    const char compatible[] = "arm,pl011\0arm,sbsa-uart";
    const char clocknames[] = "uartclk\0apb_pclk";

    dev = qdev_new(TYPE_PL011);
    object_property_add_child(OBJECT(s), "uart[*]", OBJECT(dev));
    qdev_prop_set_chr(dev, "chardev", serial_hd(chardev_idx));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion(&s->mr_ps, map->addr, mr);

    versal_sysbus_connect_irq(s, SYS_BUS_DEVICE(dev), 0, map->irq);

    node = versal_fdt_add_simple_subnode(s, "/uart", map->addr, 0x1000,
                                         compatible, sizeof(compatible));
    qemu_fdt_setprop_cell(s->cfg.fdt, node, "current-speed", 115200);
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "clocks",
                           s->phandle.clk_125mhz, s->phandle.clk_125mhz);
    qemu_fdt_setprop(s->cfg.fdt, node, "clock-names", clocknames,
                     sizeof(clocknames));
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "interrupts",
                           GIC_FDT_IRQ_TYPE_SPI, map->irq,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop(s->cfg.fdt, node, "u-boot,dm-pre-reloc", NULL, 0);

    alias = g_strdup_printf("serial%d", chardev_idx);
    qemu_fdt_setprop_string(s->cfg.fdt, "/aliases", alias, node);

    if (chardev_idx == 0) {
        qemu_fdt_setprop_string(s->cfg.fdt, "/chosen", "stdout-path", node);
    }
}

static void versal_create_canfd(Versal *s, const VersalSimplePeriphMap *map,
                                CanBusState *bus)
{
    SysBusDevice *sbd;
    MemoryRegion *mr;
    g_autofree char *node;
    const char compatible[] = "xlnx,canfd-2.0";
    const char clocknames[] = "can_clk\0s_axi_aclk";

    sbd = SYS_BUS_DEVICE(qdev_new(TYPE_XILINX_CANFD));
    object_property_add_child(OBJECT(s), "canfd[*]", OBJECT(sbd));

    object_property_set_int(OBJECT(sbd), "ext_clk_freq",
                            25 * 1000 * 1000 , &error_abort);

    object_property_set_link(OBJECT(sbd), "canfdbus", OBJECT(bus),
                             &error_abort);

    sysbus_realize_and_unref(sbd, &error_fatal);

    mr = sysbus_mmio_get_region(sbd, 0);
    memory_region_add_subregion(&s->mr_ps, map->addr, mr);

    versal_sysbus_connect_irq(s, sbd, 0, map->irq);

    node = versal_fdt_add_simple_subnode(s, "/canfd", map->addr, 0x10000,
                                         compatible, sizeof(compatible));
    qemu_fdt_setprop_cell(s->cfg.fdt, node, "rx-fifo-depth", 0x40);
    qemu_fdt_setprop_cell(s->cfg.fdt, node, "tx-mailbox-count", 0x20);
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "clocks",
                           s->phandle.clk_25mhz, s->phandle.clk_25mhz);
    qemu_fdt_setprop(s->cfg.fdt, node, "clock-names",
                     clocknames, sizeof(clocknames));
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "interrupts",
                           GIC_FDT_IRQ_TYPE_SPI, map->irq,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
}

static void versal_create_usb(Versal *s,
                              const struct VersalUsbMap *map)
{
    DeviceState *dev;
    MemoryRegion *mr;
    g_autofree char *node, *subnode;
    const char clocknames[] = "bus_clk\0ref_clk";
    const char irq_name[] = "dwc_usb3";
    const char compat_versal_dwc3[] = "xlnx,versal-dwc3";
    const char compat_dwc3[] = "snps,dwc3";

    dev = qdev_new(TYPE_XILINX_VERSAL_USB2);
    object_property_add_child(OBJECT(s), "usb[*]", OBJECT(dev));

    object_property_set_link(OBJECT(dev), "dma", OBJECT(&s->mr_ps),
                             &error_abort);
    qdev_prop_set_uint32(dev, "intrs", 1);
    qdev_prop_set_uint32(dev, "slots", 2);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion(&s->mr_ps, map->xhci, mr);

    versal_sysbus_connect_irq(s, SYS_BUS_DEVICE(dev), 0, map->irq);

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_add_subregion(&s->mr_ps, map->ctrl, mr);

    node = versal_fdt_add_simple_subnode(s, "/usb", map->ctrl, 0x10000,
                                         compat_versal_dwc3,
                                         sizeof(compat_versal_dwc3));
    qemu_fdt_setprop(s->cfg.fdt, node, "clock-names",
                         clocknames, sizeof(clocknames));
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "clocks",
                               s->phandle.clk_25mhz, s->phandle.clk_125mhz);
    qemu_fdt_setprop(s->cfg.fdt, node, "ranges", NULL, 0);
    qemu_fdt_setprop_cell(s->cfg.fdt, node, "#address-cells", 2);
    qemu_fdt_setprop_cell(s->cfg.fdt, node, "#size-cells", 2);

    subnode = g_strdup_printf("/%s/dwc3", node);
    g_free(node);

    node = versal_fdt_add_simple_subnode(s, subnode, map->xhci, 0x10000,
                                         compat_dwc3,
                                         sizeof(compat_dwc3));
    qemu_fdt_setprop(s->cfg.fdt, node, "interrupt-names",
                     irq_name, sizeof(irq_name));
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, map->irq,
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop_cell(s->cfg.fdt, node,
                          "snps,quirk-frame-length-adjustment", 0x20);
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "#stream-id-cells", 1);
    qemu_fdt_setprop_string(s->cfg.fdt, node, "dr_mode", "host");
    qemu_fdt_setprop_string(s->cfg.fdt, node, "phy-names", "usb3-phy");
    qemu_fdt_setprop(s->cfg.fdt, node, "snps,dis_u2_susphy_quirk", NULL, 0);
    qemu_fdt_setprop(s->cfg.fdt, node, "snps,dis_u3_susphy_quirk", NULL, 0);
    qemu_fdt_setprop(s->cfg.fdt, node, "snps,refclk_fladj", NULL, 0);
    qemu_fdt_setprop(s->cfg.fdt, node, "snps,mask_phy_reset", NULL, 0);
    qemu_fdt_setprop_string(s->cfg.fdt, node, "maximum-speed", "high-speed");
}

static void versal_create_gem(Versal *s,
                              const struct VersalGemMap *map)
{
    DeviceState *dev;
    MemoryRegion *mr;
    DeviceState *or;
    int i;

    dev = qdev_new(TYPE_CADENCE_GEM);
    object_property_add_child(OBJECT(s), "gem[*]", OBJECT(dev));

    qemu_configure_nic_device(dev, true, NULL);
    object_property_set_int(OBJECT(dev), "phy-addr", 23, &error_abort);
    object_property_set_int(OBJECT(dev), "num-priority-queues",
                            map->num_prio_queue, &error_abort);

    object_property_set_link(OBJECT(dev), "dma", OBJECT(&s->mr_ps),
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion(&s->mr_ps, map->map.addr, mr);

    /*
     * The GEM controller exposes one IRQ line per priority queue. In Versal
     * family devices, those are OR'ed together.
     */
    or = create_or_gate(s, OBJECT(dev), "irq-orgate",
                        map->num_prio_queue, map->map.irq);

    for (i = 0; i < map->num_prio_queue; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, qdev_get_gpio_in(or, i));
    }
}

static void versal_create_gem_fdt(Versal *s,
                                  const struct VersalGemMap *map)
{
    int i;
    g_autofree char *node;
    g_autofree char *phy_node;
    int phy_phandle;
    const char compatible[] = "cdns,zynqmp-gem\0cdns,gem";
    const char clocknames[] = "pclk\0hclk\0tx_clk\0rx_clk";
    g_autofree uint32_t *irq_prop;

    node = versal_fdt_add_simple_subnode(s, "/ethernet", map->map.addr, 0x1000,
                                         compatible, sizeof(compatible));
    phy_node = g_strdup_printf("%s/fixed-link", node);
    phy_phandle = qemu_fdt_alloc_phandle(s->cfg.fdt);

    /* Fixed link PHY node */
    qemu_fdt_add_subnode(s->cfg.fdt, phy_node);
    qemu_fdt_setprop_cell(s->cfg.fdt, phy_node, "phandle", phy_phandle);
    qemu_fdt_setprop(s->cfg.fdt, phy_node, "full-duplex", NULL, 0);
    qemu_fdt_setprop_cell(s->cfg.fdt, phy_node, "speed", map->speed);

    qemu_fdt_setprop_string(s->cfg.fdt, node, "phy-mode", map->phy_mode);
    qemu_fdt_setprop_cell(s->cfg.fdt, node, "phy-handle", phy_phandle);
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "clocks",
                           s->phandle.clk_25mhz, s->phandle.clk_25mhz,
                           s->phandle.clk_125mhz, s->phandle.clk_125mhz);
    qemu_fdt_setprop(s->cfg.fdt, node, "clock-names",
                     clocknames, sizeof(clocknames));

    irq_prop = g_new(uint32_t, map->num_prio_queue * 3);
    for (i = 0; i < map->num_prio_queue; i++) {
        irq_prop[3 * i] = cpu_to_be32(GIC_FDT_IRQ_TYPE_SPI);
        irq_prop[3 * i + 1] = cpu_to_be32(map->map.irq);
        irq_prop[3 * i + 2] = cpu_to_be32(GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    }
    qemu_fdt_setprop(s->cfg.fdt, node, "interrupts", irq_prop,
                     sizeof(uint32_t) * map->num_prio_queue * 3);
}

static void versal_create_zdma(Versal *s,
                               const struct VersalZDMAMap *map)
{
    DeviceState *dev;
    MemoryRegion *mr;
    g_autofree char *name;
    const char compatible[] = "xlnx,zynqmp-dma-1.0";
    const char clocknames[] = "clk_main\0clk_apb";
    size_t i;

    name = g_strdup_printf("%s[*]", map->name);

    for (i = 0; i < map->num_chan; i++) {
        uint64_t addr = map->map.addr + map->chan_stride * i;
        int irq = map->map.irq + map->irq_stride * i;
        g_autofree char *node;

        dev = qdev_new(TYPE_XLNX_ZDMA);
        object_property_add_child(OBJECT(s), name, OBJECT(dev));
        object_property_set_int(OBJECT(dev), "bus-width", 128, &error_abort);
        object_property_set_link(OBJECT(dev), "dma",
                                 OBJECT(get_system_memory()), &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(&s->mr_ps, addr, mr);

        versal_sysbus_connect_irq(s, SYS_BUS_DEVICE(dev), 0, irq);

        node = versal_fdt_add_simple_subnode(s, "/dma", addr, 0x1000,
                                             compatible, sizeof(compatible));
        qemu_fdt_setprop_cell(s->cfg.fdt, node, "xlnx,bus-width", 64);
        qemu_fdt_setprop_cells(s->cfg.fdt, node, "clocks",
                               s->phandle.clk_25mhz, s->phandle.clk_25mhz);
        qemu_fdt_setprop(s->cfg.fdt, node, "clock-names",
                         clocknames, sizeof(clocknames));
        qemu_fdt_setprop_cells(s->cfg.fdt, node, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, irq,
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    }
}

#define SDHCI_CAPABILITIES  0x280737ec6481 /* Same as on ZynqMP.  */
static void versal_create_sdhci(Versal *s,
                                const VersalSimplePeriphMap *map)
{
    DeviceState *dev;
    MemoryRegion *mr;
    g_autofree char *node;
    const char compatible[] = "arasan,sdhci-8.9a";
    const char clocknames[] = "clk_xin\0clk_ahb";

    dev = qdev_new(TYPE_SYSBUS_SDHCI);
    object_property_add_child(OBJECT(s), "sdhci[*]", OBJECT(dev));

    object_property_set_uint(OBJECT(dev), "sd-spec-version", 3,
                             &error_fatal);
    object_property_set_uint(OBJECT(dev), "capareg", SDHCI_CAPABILITIES,
                             &error_fatal);
    object_property_set_uint(OBJECT(dev), "uhs", UHS_I, &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion(&s->mr_ps, map->addr, mr);

    versal_sysbus_connect_irq(s, SYS_BUS_DEVICE(dev), 0, map->irq);

    node = versal_fdt_add_simple_subnode(s, "/sdhci", map->addr, 0x10000,
                                         compatible, sizeof(compatible));
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "clocks",
                           s->phandle.clk_25mhz, s->phandle.clk_25mhz);
    qemu_fdt_setprop(s->cfg.fdt, node, "clock-names",
                     clocknames, sizeof(clocknames));
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "interrupts",
                           GIC_FDT_IRQ_TYPE_SPI, map->irq,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
}

static void versal_create_rtc(Versal *s, const struct VersalRtcMap *map)
{
    SysBusDevice *sbd;
    MemoryRegion *mr;
    g_autofree char *node;
    const char compatible[] = "xlnx,zynqmp-rtc";
    const char interrupt_names[] = "alarm\0sec";

    sbd = SYS_BUS_DEVICE(qdev_new(TYPE_XLNX_ZYNQMP_RTC));
    object_property_add_child(OBJECT(s), "rtc", OBJECT(sbd));
    sysbus_realize_and_unref(sbd, &error_abort);

    mr = sysbus_mmio_get_region(sbd, 0);
    memory_region_add_subregion(&s->mr_ps, map->map.addr, mr);

    /*
     * TODO: Connect the ALARM and SECONDS interrupts once our RTC model
     * supports them.
     */
    versal_sysbus_connect_irq(s, sbd, 0, map->map.irq);

    node = versal_fdt_add_simple_subnode(s, "/rtc", map->map.addr, 0x10000,
                                         compatible, sizeof(compatible));
    qemu_fdt_setprop_cells(s->cfg.fdt, node, "interrupts",
                           GIC_FDT_IRQ_TYPE_SPI, map->alarm_irq,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI,
                           GIC_FDT_IRQ_TYPE_SPI, map->second_irq,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop(s->cfg.fdt, node, "interrupt-names",
                     interrupt_names, sizeof(interrupt_names));
}

static void versal_create_trng(Versal *s, const VersalSimplePeriphMap *map)
{
    SysBusDevice *sbd;
    MemoryRegion *mr;

    sbd = SYS_BUS_DEVICE(qdev_new(TYPE_XLNX_VERSAL_TRNG));
    object_property_add_child(OBJECT(s), "trng", OBJECT(sbd));
    sysbus_realize_and_unref(sbd, &error_abort);

    mr = sysbus_mmio_get_region(sbd, 0);
    memory_region_add_subregion(&s->mr_ps, map->addr, mr);
    versal_sysbus_connect_irq(s, sbd, 0, map->irq);
}

static void versal_create_xrams(Versal *s, const struct VersalXramMap *map)
{
    SysBusDevice *sbd;
    MemoryRegion *mr;
    DeviceState *or;
    size_t i;

    or = create_or_gate(s, OBJECT(s), "xram-orgate", map->num, map->irq);

    for (i = 0; i < map->num; i++) {
        hwaddr ctrl, mem;

        sbd = SYS_BUS_DEVICE(qdev_new(TYPE_XLNX_XRAM_CTRL));
        object_property_add_child(OBJECT(s), "xram[*]", OBJECT(sbd));
        sysbus_realize_and_unref(sbd, &error_fatal);

        ctrl = map->ctrl + map->ctrl_stride * i;
        mem = map->mem + map->mem_stride * i;

        mr = sysbus_mmio_get_region(sbd, 0);
        memory_region_add_subregion(&s->mr_ps, ctrl, mr);
        mr = sysbus_mmio_get_region(sbd, 1);
        memory_region_add_subregion(&s->mr_ps, mem, mr);

        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(or, i));
    }
}

static void versal_create_bbram(Versal *s,
                                const VersalSimplePeriphMap *map)
{
    DeviceState *dev;
    SysBusDevice *sbd;

    dev = qdev_new(TYPE_XLNX_BBRAM);
    sbd = SYS_BUS_DEVICE(dev);

    object_property_add_child(OBJECT(s), "bbram", OBJECT(dev));
    qdev_prop_set_uint32(dev, "crc-zpads", 0);
    sysbus_realize_and_unref(sbd, &error_abort);
    memory_region_add_subregion(&s->mr_ps, map->addr,
                                sysbus_mmio_get_region(sbd, 0));
    versal_sysbus_connect_irq(s, sbd, 0, map->irq);
}

static void versal_create_efuse(Versal *s,
                                const struct VersalEfuseMap *map)
{
    DeviceState *bits;
    DeviceState *ctrl;
    DeviceState *cache;

    if (versal_get_version(s) != VERSAL_VER_VERSAL) {
        /* TODO for versal2 */
        return;
    }

    ctrl = qdev_new(TYPE_XLNX_VERSAL_EFUSE_CTRL);
    cache = qdev_new(TYPE_XLNX_VERSAL_EFUSE_CACHE);
    bits = qdev_new(TYPE_XLNX_EFUSE);

    qdev_prop_set_uint32(bits, "efuse-nr", 3);
    qdev_prop_set_uint32(bits, "efuse-size", 8192);

    object_property_add_child(OBJECT(s), "efuse", OBJECT(bits));
    qdev_realize_and_unref(bits, NULL, &error_abort);

    object_property_set_link(OBJECT(ctrl), "efuse", OBJECT(bits), &error_abort);

    object_property_set_link(OBJECT(cache), "efuse", OBJECT(bits),
                             &error_abort);

    object_property_add_child(OBJECT(s), "efuse-cache", OBJECT(cache));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(cache), &error_abort);

    object_property_add_child(OBJECT(s), "efuse-ctrl", OBJECT(ctrl));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ctrl), &error_abort);

    memory_region_add_subregion(&s->mr_ps, map->ctrl,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(ctrl),
                                                       0));
    memory_region_add_subregion(&s->mr_ps, map->cache,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(cache),
                                                       0));
    versal_sysbus_connect_irq(s, SYS_BUS_DEVICE(ctrl), 0, map->irq);
}

static DeviceState *versal_create_pmc_iou_slcr(Versal *s,
                                               const VersalSimplePeriphMap *map)
{
    SysBusDevice *sbd;
    DeviceState *dev;

    dev = qdev_new(TYPE_XILINX_VERSAL_PMC_IOU_SLCR);
    object_property_add_child(OBJECT(s), "pmc-iou-slcr", OBJECT(dev));

    sbd = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sbd, &error_fatal);

    memory_region_add_subregion(&s->mr_ps, map->addr,
                                sysbus_mmio_get_region(sbd, 0));

    versal_sysbus_connect_irq(s, sbd, 0, map->irq);

    return dev;
}

static DeviceState *versal_create_ospi(Versal *s,
                                       const struct VersalOspiMap *map)
{
    SysBusDevice *sbd;
    MemoryRegion *mr_dac;
    DeviceState *dev, *dma_dst, *dma_src, *orgate;
    MemoryRegion *linear_mr = g_new(MemoryRegion, 1);

    dev = qdev_new(TYPE_XILINX_VERSAL_OSPI);
    object_property_add_child(OBJECT(s), "ospi", OBJECT(dev));

    memory_region_init(linear_mr, OBJECT(dev), "linear-mr", map->dac_sz);

    mr_dac = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_add_subregion(linear_mr, 0x0, mr_dac);

    /* Create the OSPI destination DMA */
    dma_dst = qdev_new(TYPE_XLNX_CSU_DMA);
    object_property_add_child(OBJECT(dev), "dma-dst-dev", OBJECT(dma_dst));
    object_property_set_link(OBJECT(dma_dst), "dma",
                             OBJECT(get_system_memory()), &error_abort);

    sbd = SYS_BUS_DEVICE(dma_dst);
    sysbus_realize_and_unref(sbd, &error_fatal);

    memory_region_add_subregion(&s->mr_ps, map->dma_dst,
                                sysbus_mmio_get_region(sbd, 0));

    /* Create the OSPI source DMA */
    dma_src = qdev_new(TYPE_XLNX_CSU_DMA);
    object_property_add_child(OBJECT(dev), "dma-src-dev", OBJECT(dma_src));

    object_property_set_bool(OBJECT(dma_src), "is-dst", false, &error_abort);

    object_property_set_link(OBJECT(dma_src), "dma", OBJECT(mr_dac),
                             &error_abort);

    object_property_set_link(OBJECT(dma_src), "stream-connected-dma",
                             OBJECT(dma_dst), &error_abort);

    sbd = SYS_BUS_DEVICE(dma_src);
    sysbus_realize_and_unref(sbd, &error_fatal);

    memory_region_add_subregion(&s->mr_ps, map->dma_src,
                                sysbus_mmio_get_region(sbd, 0));

    /* Realize the OSPI */
    object_property_set_link(OBJECT(dev), "dma-src",
                             OBJECT(dma_src), &error_abort);

    sbd = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sbd, &error_fatal);

    memory_region_add_subregion(&s->mr_ps, map->ctrl,
                                sysbus_mmio_get_region(sbd, 0));

    memory_region_add_subregion(&s->mr_ps, map->dac,
                                linear_mr);

    /* OSPI irq */
    orgate = create_or_gate(s, OBJECT(dev), "irq-orgate", 3,
                            map->irq);

    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(orgate, 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(dma_src), 0, qdev_get_gpio_in(orgate, 1));
    sysbus_connect_irq(SYS_BUS_DEVICE(dma_dst), 0, qdev_get_gpio_in(orgate, 2));

    return dev;
}

static void versal_create_cfu(Versal *s, const struct VersalCfuMap *map)
{
    SysBusDevice *sbd;
    Object *container;
    DeviceState *cfu_fdro, *cfu_apb, *cfu_sfr, *cframe_bcast;
    DeviceState *cframe_irq_or;
    int i;

    container = object_new(TYPE_CONTAINER);
    object_property_add_child(OBJECT(s), "cfu", container);
    object_unref(container);

    /* CFU FDRO */
    cfu_fdro = qdev_new(TYPE_XLNX_VERSAL_CFU_FDRO);
    object_property_add_child(container, "cfu-fdro", OBJECT(cfu_fdro));
    sbd = SYS_BUS_DEVICE(cfu_fdro);

    sysbus_realize_and_unref(sbd, &error_fatal);
    memory_region_add_subregion(&s->mr_ps, map->cfu_fdro,
                                sysbus_mmio_get_region(sbd, 0));

    /* cframe bcast */
    cframe_bcast = qdev_new(TYPE_XLNX_VERSAL_CFRAME_BCAST_REG);
    object_property_add_child(container, "cframe-bcast", OBJECT(cframe_bcast));

    /* CFU APB */
    cfu_apb = qdev_new(TYPE_XLNX_VERSAL_CFU_APB);
    object_property_add_child(container, "cfu-apb", OBJECT(cfu_apb));

    /* IRQ or gate for cframes */
    cframe_irq_or = qdev_new(TYPE_OR_IRQ);
    object_property_add_child(container, "cframe-irq-or-gate",
                              OBJECT(cframe_irq_or));
    qdev_prop_set_uint16(cframe_irq_or, "num-lines", map->num_cframe);
    qdev_realize_and_unref(cframe_irq_or, NULL, &error_abort);
    versal_qdev_connect_gpio_out(s, cframe_irq_or, 0, map->cframe_irq);

    /* cframe reg */
    for (i = 0; i < map->num_cframe; i++) {
        uint64_t reg_base;
        uint64_t fdri_base;
        DeviceState *dev;
        g_autofree char *prop_name;
        size_t j;

        dev = qdev_new(TYPE_XLNX_VERSAL_CFRAME_REG);
        object_property_add_child(container, "cframe[*]", OBJECT(dev));

        sbd = SYS_BUS_DEVICE(dev);

        for (j = 0; j < ARRAY_SIZE(map->cframe_cfg[i].blktype_frames); j++) {
            g_autofree char *blktype_prop_name;

            blktype_prop_name = g_strdup_printf("blktype%zu-frames", j);
            object_property_set_int(OBJECT(dev), blktype_prop_name,
                                    map->cframe_cfg[i].blktype_frames[j],
                                    &error_abort);
        }

        object_property_set_link(OBJECT(dev), "cfu-fdro",
                                 OBJECT(cfu_fdro), &error_abort);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_abort);

        reg_base = map->cframe_base + i * map->cframe_stride * 2;
        fdri_base = reg_base + map->cframe_stride;
        memory_region_add_subregion(&s->mr_ps, reg_base,
                                    sysbus_mmio_get_region(sbd, 0));
        memory_region_add_subregion(&s->mr_ps, fdri_base,
                                    sysbus_mmio_get_region(sbd, 1));
        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(cframe_irq_or, i));

        prop_name = g_strdup_printf("cframe%d", i);
        object_property_set_link(OBJECT(cframe_bcast), prop_name,
                                 OBJECT(dev), &error_abort);
        object_property_set_link(OBJECT(cfu_apb), prop_name,
                                 OBJECT(dev), &error_abort);
    }

    sbd = SYS_BUS_DEVICE(cframe_bcast);
    sysbus_realize_and_unref(sbd, &error_abort);
    memory_region_add_subregion(&s->mr_ps, map->cframe_bcast_reg,
                                sysbus_mmio_get_region(sbd, 0));
    memory_region_add_subregion(&s->mr_ps, map->cframe_bcast_fdri,
                                sysbus_mmio_get_region(sbd, 1));

    sbd = SYS_BUS_DEVICE(cfu_apb);
    sysbus_realize_and_unref(sbd, &error_fatal);
    memory_region_add_subregion(&s->mr_ps, map->cfu_apb,
                                sysbus_mmio_get_region(sbd, 0));
    memory_region_add_subregion(&s->mr_ps, map->cfu_stream,
                                sysbus_mmio_get_region(sbd, 1));
    memory_region_add_subregion(&s->mr_ps, map->cfu_stream_2,
                                sysbus_mmio_get_region(sbd, 2));
    versal_sysbus_connect_irq(s, sbd, 0, map->cfu_apb_irq);

    /* CFU SFR */
    cfu_sfr = qdev_new(TYPE_XLNX_VERSAL_CFU_SFR);
    object_property_add_child(container, "cfu-sfr", OBJECT(cfu_sfr));
    sbd = SYS_BUS_DEVICE(cfu_sfr);

    object_property_set_link(OBJECT(cfu_sfr),
                            "cfu", OBJECT(cfu_apb), &error_abort);
    sysbus_realize_and_unref(sbd, &error_fatal);
    memory_region_add_subregion(&s->mr_ps, map->cfu_sfr,
                                sysbus_mmio_get_region(sbd, 0));
}

static inline void crl_connect_dev(Object *crl, Object *dev)
{
    const char *prop = object_get_canonical_path_component(dev);

    /* The component part of the device path matches the CRL property name */
    object_property_set_link(crl, prop, dev, &error_abort);
}

static inline void crl_connect_dev_by_name(Versal *s, Object *crl,
                                           const char *name, size_t num)
{
    size_t i;

    for (i = 0; i < num; i++) {
        Object *dev = versal_get_child_idx(s, name, i);

        crl_connect_dev(crl, dev);
    }
}

static inline void versal_create_crl(Versal *s)
{
    const VersalMap *map;
    VersalVersion ver;
    const char *crl_class;
    DeviceState *dev;
    size_t num_gem;
    Object *obj;

    map = versal_get_map(s);
    ver = versal_get_version(s);

    crl_class = xlnx_versal_crl_class_name(ver);
    dev = qdev_new(crl_class);
    obj = OBJECT(dev);
    object_property_add_child(OBJECT(s), "crl", obj);

    /*
     * The 3rd GEM controller on versal2 is in the MMI subsystem.
     * Its reset line is not connected to the CRL. Consider only the first two
     * ones.
     */
    num_gem = ver == VERSAL_VER_VERSAL2 ? 2 : map->num_gem;

    crl_connect_dev_by_name(s, obj, "rpu-cluster/rpu",
                            map->rpu.num_cluster * map->rpu.num_core);
    crl_connect_dev_by_name(s, obj, map->zdma[0].name, map->zdma[0].num_chan);
    crl_connect_dev_by_name(s, obj, "uart", map->num_uart);
    crl_connect_dev_by_name(s, obj, "gem", num_gem);
    crl_connect_dev_by_name(s, obj, "usb", map->num_usb);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_abort);

    memory_region_add_subregion(&s->mr_ps, map->crl.addr,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));

    if (ver == VERSAL_VER_VERSAL) {
        /* CRL IRQ line has been removed in versal2 */
        versal_sysbus_connect_irq(s, SYS_BUS_DEVICE(dev), 0, map->crl.irq);
    }
}

/*
 * This takes the board allocated linear DDR memory and creates aliases
 * for each split DDR range/aperture on the Versal address map.
 */
static void versal_map_ddr(Versal *s, const struct VersalDDRMap *map)
{
    uint64_t size = memory_region_size(s->cfg.mr_ddr);
    uint64_t offset = 0;
    int i;

    for (i = 0; i < map->num_chan && size; i++) {
        uint64_t mapsize;
        MemoryRegion *alias;

        mapsize = MIN(size, map->chan[i].size);

        /* Create the MR alias.  */
        alias = g_new(MemoryRegion, 1);
        memory_region_init_alias(alias, OBJECT(s), "noc-ddr-range",
                                 s->cfg.mr_ddr, offset, mapsize);

        /* Map it onto the NoC MR.  */
        memory_region_add_subregion(&s->mr_ps, map->chan[i].addr, alias);
        offset += mapsize;
        size -= mapsize;
    }
}

void versal_fdt_add_memory_nodes(Versal *s, uint64_t size)
{
    const struct VersalDDRMap *map = &versal_get_map(s)->ddr;
    g_autofree char *node;
    g_autofree uint64_t *reg;
    int i;

    reg = g_new(uint64_t, map->num_chan * 2);

    for (i = 0; i < map->num_chan && size; i++) {
        uint64_t mapsize;

        mapsize = MIN(size, map->chan[i].size);

        reg[i * 2] = cpu_to_be64(map->chan[i].addr);
        reg[i * 2 + 1] = cpu_to_be64(mapsize);

        size -= mapsize;
    }

    node = versal_fdt_add_subnode(s, "/memory", 0, "memory", sizeof("memory"));
    qemu_fdt_setprop(s->cfg.fdt, node, "reg", reg, sizeof(uint64_t) * i * 2);
}

static void versal_unimp_area(Versal *s, const char *name,
                                MemoryRegion *mr,
                                hwaddr base, hwaddr size)
{
    DeviceState *dev = qdev_new(TYPE_UNIMPLEMENTED_DEVICE);
    MemoryRegion *mr_dev;

    qdev_prop_set_string(dev, "name", name);
    qdev_prop_set_uint64(dev, "size", size);
    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    mr_dev = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion(mr, base, mr_dev);
}

static void versal_unimp_sd_emmc_sel(void *opaque, int n, int level)
{
    qemu_log_mask(LOG_UNIMP,
                  "Selecting between enabling SD mode or eMMC mode on "
                  "controller %d is not yet implemented\n", n);
}

static void versal_unimp_qspi_ospi_mux_sel(void *opaque, int n, int level)
{
    qemu_log_mask(LOG_UNIMP,
                  "Selecting between enabling the QSPI or OSPI linear address "
                  "region is not yet implemented\n");
}

static void versal_unimp_irq_parity_imr(void *opaque, int n, int level)
{
    qemu_log_mask(LOG_UNIMP,
                  "PMC SLCR parity interrupt behaviour "
                  "is not yet implemented\n");
}

static void versal_unimp_common(Versal *s)
{
    DeviceState *slcr;
    qemu_irq gpio_in;

    versal_unimp_area(s, "crp", &s->mr_ps, 0xf1260000, 0x10000);

    qdev_init_gpio_in_named(DEVICE(s), versal_unimp_sd_emmc_sel,
                            "sd-emmc-sel-dummy", 2);
    qdev_init_gpio_in_named(DEVICE(s), versal_unimp_qspi_ospi_mux_sel,
                            "qspi-ospi-mux-sel-dummy", 1);
    qdev_init_gpio_in_named(DEVICE(s), versal_unimp_irq_parity_imr,
                            "irq-parity-imr-dummy", 1);

    slcr = DEVICE(versal_get_child(s, "pmc-iou-slcr"));
    gpio_in = qdev_get_gpio_in_named(DEVICE(s), "sd-emmc-sel-dummy", 0);
    qdev_connect_gpio_out_named(slcr, "sd-emmc-sel", 0, gpio_in);

    gpio_in = qdev_get_gpio_in_named(DEVICE(s), "sd-emmc-sel-dummy", 1);
    qdev_connect_gpio_out_named(slcr, "sd-emmc-sel", 1, gpio_in);

    gpio_in = qdev_get_gpio_in_named(DEVICE(s), "qspi-ospi-mux-sel-dummy", 0);
    qdev_connect_gpio_out_named(slcr, "qspi-ospi-mux-sel", 0, gpio_in);

    gpio_in = qdev_get_gpio_in_named(DEVICE(s), "irq-parity-imr-dummy", 0);
    qdev_connect_gpio_out_named(slcr, SYSBUS_DEVICE_GPIO_IRQ, 0, gpio_in);
}

static void versal_unimp(Versal *s)
{
    versal_unimp_area(s, "psm", &s->mr_ps, 0xffc80000, 0x70000);
    versal_unimp_area(s, "crf", &s->mr_ps, 0xfd1a0000, 0x140000);
    versal_unimp_area(s, "apu", &s->mr_ps, 0xfd5c0000, 0x100);
    versal_unimp_area(s, "iou-scntr", &s->mr_ps, 0xff130000, 0x10000);
    versal_unimp_area(s, "iou-scntr-secure", &s->mr_ps, 0xff140000, 0x10000);

    versal_unimp_common(s);
}

static void versal2_unimp(Versal *s)
{
    versal_unimp_area(s, "fpd-systmr-ctrl", &s->mr_ps, 0xec920000, 0x1000);
    versal_unimp_area(s, "crf", &s->mr_ps, 0xec200000, 0x100000);

    versal_unimp_common(s);
}

static uint32_t fdt_add_clk_node(Versal *s, const char *name,
                                 unsigned int freq_hz)
{
    uint32_t phandle;

    phandle = qemu_fdt_alloc_phandle(s->cfg.fdt);

    qemu_fdt_add_subnode(s->cfg.fdt, name);
    qemu_fdt_setprop_cell(s->cfg.fdt, name, "phandle", phandle);
    qemu_fdt_setprop_cell(s->cfg.fdt, name, "clock-frequency", freq_hz);
    qemu_fdt_setprop_cell(s->cfg.fdt, name, "#clock-cells", 0x0);
    qemu_fdt_setprop_string(s->cfg.fdt, name, "compatible", "fixed-clock");
    qemu_fdt_setprop(s->cfg.fdt, name, "u-boot,dm-pre-reloc", NULL, 0);

    return phandle;
}

static void versal_realize_common(Versal *s)
{
    DeviceState *slcr, *ospi;
    MemoryRegion *ocm;
    Object *container;
    const VersalMap *map = versal_get_map(s);
    size_t i;

    g_assert(s->cfg.fdt != NULL);

    s->phandle.clk_25mhz = fdt_add_clk_node(s, "/clk25", 25 * 1000 * 1000);
    s->phandle.clk_125mhz = fdt_add_clk_node(s, "/clk125", 125 * 1000 * 1000);
    s->phandle.gic = qemu_fdt_alloc_phandle(s->cfg.fdt);

    container = object_new(TYPE_CONTAINER);
    object_property_add_child(OBJECT(s), "irq-splits", container);
    object_unref(container);

    container = object_new(TYPE_CONTAINER);
    object_property_add_child(OBJECT(s), "irq-or-gates", container);
    object_unref(container);

    qemu_fdt_setprop_cell(s->cfg.fdt, "/", "interrupt-parent", s->phandle.gic);
    qemu_fdt_setprop_cell(s->cfg.fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(s->cfg.fdt, "/", "#address-cells", 0x2);

    versal_create_cpu_cluster(s, &map->apu);
    versal_create_cpu_cluster(s, &map->rpu);

    for (i = 0; i < map->num_uart; i++) {
        versal_create_uart(s, &map->uart[i], i);
    }

    for (i = 0; i < map->num_canfd; i++) {
        versal_create_canfd(s, &map->canfd[i], s->cfg.canbus[i]);
    }

    for (i = 0; i < map->num_sdhci; i++) {
        versal_create_sdhci(s, &map->sdhci[i]);
    }

    for (i = 0; i < map->num_gem; i++) {
        versal_create_gem(s, &map->gem[i]);
        /*
         * Create fdt node in reverse order to keep backward compatibility with
         * previous versions of the generated FDT. This affects Linux kernel
         * interface naming order when persistent naming scheme is not in use.
         */
        versal_create_gem_fdt(s, &map->gem[map->num_gem - 1 - i]);
    }

    for (i = 0; i < map->num_zdma; i++) {
        versal_create_zdma(s, &map->zdma[i]);
    }

    versal_create_xrams(s, &map->xram);

    for (i = 0; i < map->num_usb; i++) {
        versal_create_usb(s, &map->usb[i]);
    }

    versal_create_efuse(s, &map->efuse);
    ospi = versal_create_ospi(s, &map->ospi);
    slcr = versal_create_pmc_iou_slcr(s, &map->pmc_iou_slcr);

    qdev_connect_gpio_out_named(slcr, "ospi-mux-sel", 0,
                                qdev_get_gpio_in_named(ospi,
                                                       "ospi-mux-sel", 0));

    versal_create_bbram(s, &map->bbram);
    versal_create_trng(s, &map->trng);
    versal_create_rtc(s, &map->rtc);
    versal_create_cfu(s, &map->cfu);
    versal_create_crl(s);

    versal_map_ddr(s, &map->ddr);

    /* Create the On Chip Memory (OCM).  */
    ocm = g_new(MemoryRegion, 1);
    memory_region_init_ram(ocm, OBJECT(s), "ocm", map->ocm.size, &error_fatal);
    memory_region_add_subregion_overlap(&s->mr_ps, map->ocm.addr, ocm, 0);
}

static void versal_realize(DeviceState *dev, Error **errp)
{
    Versal *s = XLNX_VERSAL_BASE(dev);

    versal_realize_common(s);
    versal_unimp(s);
}

static void versal2_realize(DeviceState *dev, Error **errp)
{
    Versal *s = XLNX_VERSAL_BASE(dev);

    versal_realize_common(s);
    versal2_unimp(s);
}

DeviceState *versal_get_boot_cpu(Versal *s)
{
    return DEVICE(versal_get_child_idx(s, "apu-cluster/apu", 0));
}

void versal_sdhci_plug_card(Versal *s, int sd_idx, BlockBackend *blk)
{
    DeviceState *sdhci, *card;

    sdhci = DEVICE(versal_get_child_idx(s, "sdhci", sd_idx));

    if (sdhci == NULL) {
        return;
    }

    card = qdev_new(TYPE_SD_CARD);
    object_property_add_child(OBJECT(sdhci), "card[*]", OBJECT(card));
    qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
    qdev_realize_and_unref(card, qdev_get_child_bus(DEVICE(sdhci), "sd-bus"),
                           &error_fatal);
}

void versal_efuse_attach_drive(Versal *s, BlockBackend *blk)
{
    DeviceState *efuse;

    efuse = DEVICE(versal_get_child(s, "efuse"));

    if (efuse == NULL) {
        return;
    }

    qdev_prop_set_drive(efuse, "drive", blk);
}

void versal_bbram_attach_drive(Versal *s, BlockBackend *blk)
{
    DeviceState *bbram;

    bbram = DEVICE(versal_get_child(s, "bbram"));

    if (bbram == NULL) {
        return;
    }

    qdev_prop_set_drive(bbram, "drive", blk);
}

void versal_ospi_create_flash(Versal *s, int flash_idx, const char *flash_mdl,
                              BlockBackend *blk)
{
    BusState *spi_bus;
    DeviceState *flash, *ospi;
    qemu_irq cs_line;

    ospi = DEVICE(versal_get_child(s, "ospi"));
    spi_bus = qdev_get_child_bus(ospi, "spi0");

    flash = qdev_new(flash_mdl);

    if (blk) {
        qdev_prop_set_drive_err(flash, "drive", blk, &error_fatal);
    }
    qdev_prop_set_uint8(flash, "cs", flash_idx);
    qdev_realize_and_unref(flash, spi_bus, &error_fatal);

    cs_line = qdev_get_gpio_in_named(flash, SSI_GPIO_CS, 0);

    sysbus_connect_irq(SYS_BUS_DEVICE(ospi),
                       flash_idx + 1, cs_line);
}

qemu_irq versal_get_reserved_irq(Versal *s, int idx, int *dtb_idx)
{
    const VersalMap *map = versal_get_map(s);

    g_assert(idx < map->reserved.irq_num);

    *dtb_idx = map->reserved.irq_start + idx;
    return versal_get_irq(s, *dtb_idx);
}

hwaddr versal_get_reserved_mmio_addr(Versal *s)
{
    const VersalMap *map = versal_get_map(s);

    return map->reserved.mmio_start;
}

int versal_get_num_cpu(VersalVersion version)
{
    const VersalMap *map = VERSION_TO_MAP[version];

    return map->apu.num_cluster * map->apu.num_core
        + map->rpu.num_cluster * map->rpu.num_core;
}

int versal_get_num_can(VersalVersion version)
{
    const VersalMap *map = VERSION_TO_MAP[version];

    return map->num_canfd;
}

int versal_get_num_sdhci(VersalVersion version)
{
    const VersalMap *map = VERSION_TO_MAP[version];

    return map->num_sdhci;
}

static void versal_base_init(Object *obj)
{
    Versal *s = XLNX_VERSAL_BASE(obj);
    size_t i, num_can;

    memory_region_init(&s->mr_ps, obj, "mr-ps-switch", UINT64_MAX);
    s->intc = g_array_new(false, false, sizeof(DeviceState *));

    num_can = versal_get_map(s)->num_canfd;
    s->cfg.canbus = g_new0(CanBusState *, num_can);

    for (i = 0; i < num_can; i++) {
        g_autofree char *prop_name = g_strdup_printf("canbus%zu", i);

        object_property_add_link(obj, prop_name, TYPE_CAN_BUS,
                                 (Object **) &s->cfg.canbus[i],
                                 object_property_allow_set_link, 0);
    }
}

static void versal_base_finalize(Object *obj)
{
    Versal *s = XLNX_VERSAL_BASE(obj);

    g_array_free(s->intc, true);
    g_free(s->cfg.canbus);
}

static const Property versal_properties[] = {
    DEFINE_PROP_LINK("ddr", Versal, cfg.mr_ddr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void versal_base_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, versal_properties);
    /* No VMSD since we haven't got any top-level SoC state to save.  */
}

static void versal_class_init(ObjectClass *klass, const void *data)
{
    VersalClass *vc = XLNX_VERSAL_BASE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    vc->version = VERSAL_VER_VERSAL;
    dc->realize = versal_realize;
}

static void versal2_class_init(ObjectClass *klass, const void *data)
{
    VersalClass *vc = XLNX_VERSAL_BASE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    vc->version = VERSAL_VER_VERSAL2;
    dc->realize = versal2_realize;
}

static const TypeInfo versal_base_info = {
    .name = TYPE_XLNX_VERSAL_BASE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Versal),
    .instance_init = versal_base_init,
    .instance_finalize = versal_base_finalize,
    .class_init = versal_base_class_init,
    .class_size = sizeof(VersalClass),
    .abstract = true,
};

static const TypeInfo versal_info = {
    .name = TYPE_XLNX_VERSAL,
    .parent = TYPE_XLNX_VERSAL_BASE,
    .class_init = versal_class_init,
};

static const TypeInfo versal2_info = {
    .name = TYPE_XLNX_VERSAL2,
    .parent = TYPE_XLNX_VERSAL_BASE,
    .class_init = versal2_class_init,
};

static void versal_register_types(void)
{
    type_register_static(&versal_base_info);
    type_register_static(&versal_info);
    type_register_static(&versal2_info);
}

type_init(versal_register_types);
