/*
 * GRLIB AHB APB PNP
 *
 *  Copyright (C) 2019 AdaCore
 *
 *  Developed by :
 *  Frederic Konrad   <frederic.konrad@adacore.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/misc/grlib_ahb_apb_pnp.h"

#define GRLIB_PNP_VENDOR_SHIFT (24)
#define GRLIB_PNP_VENDOR_SIZE   (8)
#define GRLIB_PNP_DEV_SHIFT    (12)
#define GRLIB_PNP_DEV_SIZE     (12)
#define GRLIB_PNP_VER_SHIFT     (5)
#define GRLIB_PNP_VER_SIZE      (5)
#define GRLIB_PNP_IRQ_SHIFT     (0)
#define GRLIB_PNP_IRQ_SIZE      (5)
#define GRLIB_PNP_ADDR_SHIFT   (20)
#define GRLIB_PNP_ADDR_SIZE    (12)
#define GRLIB_PNP_MASK_SHIFT    (4)
#define GRLIB_PNP_MASK_SIZE    (12)

#define GRLIB_AHB_DEV_ADDR_SHIFT   (20)
#define GRLIB_AHB_DEV_ADDR_SIZE    (12)
#define GRLIB_AHB_ENTRY_SIZE       (0x20)
#define GRLIB_AHB_MAX_DEV          (64)
#define GRLIB_AHB_SLAVE_OFFSET     (0x800)

#define GRLIB_APB_DEV_ADDR_SHIFT   (8)
#define GRLIB_APB_DEV_ADDR_SIZE    (12)
#define GRLIB_APB_ENTRY_SIZE       (0x08)
#define GRLIB_APB_MAX_DEV          (512)

#define GRLIB_PNP_MAX_REGS         (0x1000)

typedef struct AHBPnp {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t regs[GRLIB_PNP_MAX_REGS >> 2];
    uint8_t master_count;
    uint8_t slave_count;
} AHBPnp;

void grlib_ahb_pnp_add_entry(AHBPnp *dev, uint32_t address, uint32_t mask,
                             uint8_t vendor, uint16_t device, int slave,
                             int type)
{
    unsigned int reg_start;

    /*
     * AHB entries look like this:
     *
     * 31 -------- 23 -------- 11 ----- 9 -------- 4 --- 0
     *  | VENDOR ID | DEVICE ID | IRQ ? | VERSION  | IRQ |
     *  --------------------------------------------------
     *  |                      USER                      |
     *  --------------------------------------------------
     *  |                      USER                      |
     *  --------------------------------------------------
     *  |                      USER                      |
     *  --------------------------------------------------
     *  |                      USER                      |
     *  --------------------------------------------------
     * 31 ----------- 20 --- 15 ----------------- 3 ---- 0
     *  | ADDR[31..12] | 00PC |        MASK       | TYPE |
     *  --------------------------------------------------
     * 31 ----------- 20 --- 15 ----------------- 3 ---- 0
     *  | ADDR[31..12] | 00PC |        MASK       | TYPE |
     *  --------------------------------------------------
     * 31 ----------- 20 --- 15 ----------------- 3 ---- 0
     *  | ADDR[31..12] | 00PC |        MASK       | TYPE |
     *  --------------------------------------------------
     * 31 ----------- 20 --- 15 ----------------- 3 ---- 0
     *  | ADDR[31..12] | 00PC |        MASK       | TYPE |
     *  --------------------------------------------------
     */

    if (slave) {
        assert(dev->slave_count < GRLIB_AHB_MAX_DEV);
        reg_start = (GRLIB_AHB_SLAVE_OFFSET
                  + (dev->slave_count * GRLIB_AHB_ENTRY_SIZE)) >> 2;
        dev->slave_count++;
    } else {
        assert(dev->master_count < GRLIB_AHB_MAX_DEV);
        reg_start = (dev->master_count * GRLIB_AHB_ENTRY_SIZE) >> 2;
        dev->master_count++;
    }

    dev->regs[reg_start] = deposit32(dev->regs[reg_start],
                                     GRLIB_PNP_VENDOR_SHIFT,
                                     GRLIB_PNP_VENDOR_SIZE,
                                     vendor);
    dev->regs[reg_start] = deposit32(dev->regs[reg_start],
                                     GRLIB_PNP_DEV_SHIFT,
                                     GRLIB_PNP_DEV_SIZE,
                                     device);
    reg_start += 4;
    /* AHB Memory Space */
    dev->regs[reg_start] = type;
    dev->regs[reg_start] = deposit32(dev->regs[reg_start],
                                     GRLIB_PNP_ADDR_SHIFT,
                                     GRLIB_PNP_ADDR_SIZE,
                                     extract32(address,
                                               GRLIB_AHB_DEV_ADDR_SHIFT,
                                               GRLIB_AHB_DEV_ADDR_SIZE));
    dev->regs[reg_start] = deposit32(dev->regs[reg_start],
                                     GRLIB_PNP_MASK_SHIFT,
                                     GRLIB_PNP_MASK_SIZE,
                                     mask);
}

static uint64_t grlib_ahb_pnp_read(void *opaque, hwaddr offset, unsigned size)
{
    AHBPnp *ahb_pnp = GRLIB_AHB_PNP(opaque);

    return ahb_pnp->regs[offset >> 2];
}

static const MemoryRegionOps grlib_ahb_pnp_ops = {
    .read       = grlib_ahb_pnp_read,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void grlib_ahb_pnp_realize(DeviceState *dev, Error **errp)
{
    AHBPnp *ahb_pnp = GRLIB_AHB_PNP(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&ahb_pnp->iomem, OBJECT(dev), &grlib_ahb_pnp_ops,
                          ahb_pnp, TYPE_GRLIB_AHB_PNP, GRLIB_PNP_MAX_REGS);
    sysbus_init_mmio(sbd, &ahb_pnp->iomem);
}

static void grlib_ahb_pnp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = grlib_ahb_pnp_realize;
}

static const TypeInfo grlib_ahb_pnp_info = {
    .name          = TYPE_GRLIB_AHB_PNP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AHBPnp),
    .class_init    = grlib_ahb_pnp_class_init,
};

/* APBPnp */

typedef struct APBPnp {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t regs[GRLIB_PNP_MAX_REGS >> 2];
    uint32_t entry_count;
} APBPnp;

void grlib_apb_pnp_add_entry(APBPnp *dev, uint32_t address, uint32_t mask,
                             uint8_t vendor, uint16_t device, uint8_t version,
                             uint8_t irq, int type)
{
    unsigned int reg_start;

    /*
     * APB entries look like this:
     *
     * 31 -------- 23 -------- 11 ----- 9 ------- 4 --- 0
     *  | VENDOR ID | DEVICE ID | IRQ ? | VERSION | IRQ |
     *
     * 31 ---------- 20 --- 15 ----------------- 3 ---- 0
     *  | ADDR[20..8] | 0000 |        MASK       | TYPE |
     */

    assert(dev->entry_count < GRLIB_APB_MAX_DEV);
    reg_start = (dev->entry_count * GRLIB_APB_ENTRY_SIZE) >> 2;
    dev->entry_count++;

    dev->regs[reg_start] = deposit32(dev->regs[reg_start],
                                     GRLIB_PNP_VENDOR_SHIFT,
                                     GRLIB_PNP_VENDOR_SIZE,
                                     vendor);
    dev->regs[reg_start] = deposit32(dev->regs[reg_start],
                                     GRLIB_PNP_DEV_SHIFT,
                                     GRLIB_PNP_DEV_SIZE,
                                     device);
    dev->regs[reg_start] = deposit32(dev->regs[reg_start],
                                     GRLIB_PNP_VER_SHIFT,
                                     GRLIB_PNP_VER_SIZE,
                                     version);
    dev->regs[reg_start] = deposit32(dev->regs[reg_start],
                                     GRLIB_PNP_IRQ_SHIFT,
                                     GRLIB_PNP_IRQ_SIZE,
                                     irq);
    reg_start += 1;
    dev->regs[reg_start] = type;
    dev->regs[reg_start] = deposit32(dev->regs[reg_start],
                                     GRLIB_PNP_ADDR_SHIFT,
                                     GRLIB_PNP_ADDR_SIZE,
                                     extract32(address,
                                               GRLIB_APB_DEV_ADDR_SHIFT,
                                               GRLIB_APB_DEV_ADDR_SIZE));
    dev->regs[reg_start] = deposit32(dev->regs[reg_start],
                                     GRLIB_PNP_MASK_SHIFT,
                                     GRLIB_PNP_MASK_SIZE,
                                     mask);
}

static uint64_t grlib_apb_pnp_read(void *opaque, hwaddr offset, unsigned size)
{
    APBPnp *apb_pnp = GRLIB_APB_PNP(opaque);

    return apb_pnp->regs[offset >> 2];
}

static const MemoryRegionOps grlib_apb_pnp_ops = {
    .read       = grlib_apb_pnp_read,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void grlib_apb_pnp_realize(DeviceState *dev, Error **errp)
{
    APBPnp *apb_pnp = GRLIB_APB_PNP(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&apb_pnp->iomem, OBJECT(dev), &grlib_apb_pnp_ops,
                          apb_pnp, TYPE_GRLIB_APB_PNP, GRLIB_PNP_MAX_REGS);
    sysbus_init_mmio(sbd, &apb_pnp->iomem);
}

static void grlib_apb_pnp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = grlib_apb_pnp_realize;
}

static const TypeInfo grlib_apb_pnp_info = {
    .name          = TYPE_GRLIB_APB_PNP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(APBPnp),
    .class_init    = grlib_apb_pnp_class_init,
};

static void grlib_ahb_apb_pnp_register_types(void)
{
    type_register_static(&grlib_ahb_pnp_info);
    type_register_static(&grlib_apb_pnp_info);
}

type_init(grlib_ahb_apb_pnp_register_types)
